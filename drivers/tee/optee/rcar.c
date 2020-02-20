// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2017, Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/arm-smccc.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tee_drv.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include "optee_private.h"
#include "optee_smc.h"

#include <linux/suspend.h>
#include "optee_rcar.h"

static char *remaped_log_buffer;
struct optee *rcar_optee;

#define TEE_LOG_NS_BASE		(0x0407FEC000U)
#define TEE_LOG_NS_SIZE		(81920U)
#define LOG_NS_CPU_AREA_SIZE	(1024U)
#define TEE_CORE_NB_CORE	(8U)

static void debug_log_work_handler(struct work_struct *work);
static int tz_rcar_suspend(void);
static int tz_rcar_power_event(struct notifier_block *this,
			       unsigned long event, void *ptr);
static int rcar_optee_add_suspend_callback(void);
static void rcar_optee_del_suspend_callback(void);
static int rcar_optee_init_debug_log(void);

static void debug_log_work_handler(struct work_struct *work)
{
	pr_alert("%s", (int8_t *)(&work[1]));
	kfree(work);
}

void handle_rpc_func_cmd_debug_log(struct optee_msg_arg *arg)
{
	struct optee_msg_param *params;
	u32 cpu_id;
	char *p;
	struct work_struct *work = NULL;
	size_t logmsg_size;

	if (arg->num_params == 1) {
		params = arg->params;
		cpu_id = params[0].u.value.a;

		if (cpu_id < TEE_CORE_NB_CORE) {
			p = &remaped_log_buffer[cpu_id * LOG_NS_CPU_AREA_SIZE];
			logmsg_size = strlen(p) + 1;
			work = kmalloc(sizeof(*work) + logmsg_size, GFP_KERNEL);
			if (work) {
				strcpy((int8_t *)(&work[1]), p);
				INIT_WORK(work, debug_log_work_handler);
				schedule_work(work);
			} else {
				pr_alert("%s", p);
			}
			arg->ret = TEEC_SUCCESS;
		} else {
			arg->ret = TEEC_ERROR_BAD_PARAMETERS;
		}
	} else {
		arg->ret = TEEC_ERROR_BAD_PARAMETERS;
	}
}

/*
 * It makes no sense to go into suspend while the OP-TEE is running.
 */
static int tz_rcar_suspend(void)
{
	int empty;
	int ret;
	struct optee *optee;

	optee = rcar_optee;

	mutex_lock(&optee->call_queue.mutex);
	empty = list_empty(&optee->call_queue.waiters);
	mutex_unlock(&optee->call_queue.mutex);

	if (empty) {
		ret = NOTIFY_DONE;
	} else {
		pr_err("Linux cannot be suspended while the OP-TEE is in use\n");
		ret = notifier_from_errno(-EBUSY);
	}

	return ret;
}

static int tz_rcar_power_event(struct notifier_block *this,
			       unsigned long event, void *ptr)
{
	int ret;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		ret = tz_rcar_suspend();
		break;
	default:
		ret = NOTIFY_DONE;
		break;
	}

	return ret;
}

static struct notifier_block tz_rcar_power_notifier = {
	.notifier_call = tz_rcar_power_event,
};

static int rcar_optee_add_suspend_callback(void)
{
	int ret;

	ret = register_pm_notifier(&tz_rcar_power_notifier);
	if (ret != 0)
		pr_err("failed to register the pm_notifier (ret=%d)\n", ret);

	return ret;
}

static void rcar_optee_del_suspend_callback(void)
{
	unregister_pm_notifier(&tz_rcar_power_notifier);
	pr_info("%s: unregister tz_rcar_power_event function\n", __func__);
}

static int rcar_optee_init_debug_log(void)
{
	int ret = 0;

	remaped_log_buffer = ioremap_nocache(TEE_LOG_NS_BASE, TEE_LOG_NS_SIZE);
	if (!remaped_log_buffer) {
		pr_err("failed to ioremap_nocache(TEE_LOG_NS_BASE)\n");
		ret = -ENOMEM;
	}

	return ret;
}

int optee_rcar_probe(struct optee *optee)
{
	int ret;

	rcar_optee = optee;

	pr_info("R-Car Rev.%s\n", VERSION_OF_RENESAS);

	ret = rcar_optee_add_suspend_callback();
	if (ret == 0) {
		ret = rcar_optee_init_debug_log();
		if (ret != 0)
			rcar_optee_del_suspend_callback();
	}

	return ret;
}

void optee_rcar_remove(void)
{
	rcar_optee_del_suspend_callback();
}
