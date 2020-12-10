// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015-2020, Renesas Electronics Corporation
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
#include <linux/kthread.h>
#include "optee_rcar.h"

static char *remaped_log_buffer;
static struct optee *rcar_optee;
static struct rcar_debug_log_info dlog_info;

#define TEE_LOG_NS_BASE		(0x0407FEC000U)
#define TEE_LOG_NS_SIZE		(81920U)
#define LOG_NS_CPU_AREA_SIZE	(1024U)
#define TEE_CORE_NB_CORE	(8U)

static int debug_log_kthread(void *arg);
static int tz_rcar_suspend(void);
static int tz_rcar_power_event(struct notifier_block *this,
			       unsigned long event, void *ptr);
static int rcar_optee_add_suspend_callback(void);
static void rcar_optee_del_suspend_callback(void);
static int rcar_optee_init_debug_log(struct optee *optee);
static void rcar_optee_final_debug_log(void);

static int debug_log_kthread(void *arg)
{
	struct rcar_debug_log_info *dlog;
	struct rcar_debug_log_node *node;
	bool thread_exit = false;

	dlog = (struct rcar_debug_log_info *)arg;

	while (1) {
		spin_lock(&dlog->q_lock);
		while (!list_empty(&dlog->queue)) {
			node = list_first_entry(&dlog->queue,
						struct rcar_debug_log_node,
						list);
			spin_unlock(&dlog->q_lock);

			if (node->logmsg)
				pr_alert("%s", node->logmsg);
			else
				thread_exit = true;

			spin_lock(&dlog->q_lock);
			list_del(&node->list);
			kfree(node);
		}
		spin_unlock(&dlog->q_lock);
		if (thread_exit)
			break;
		wait_event_interruptible(dlog->waitq,
					 !list_empty(&dlog->queue));
	}

	pr_info("%s Exit\n", __func__);
	return 0;
}

void handle_rpc_func_cmd_debug_log(struct optee_msg_arg *arg)
{
	struct rcar_debug_log_info *dlog;
	struct optee_msg_param *params;
	u32 cpu_id;
	char *p;
	struct rcar_debug_log_node *node = NULL;
	size_t alloc_size;

	dlog = (struct rcar_debug_log_info *)&dlog_info;

	if (arg->num_params == 1) {
		params = arg->params;
		cpu_id = params[0].u.value.a;

		if (cpu_id < TEE_CORE_NB_CORE) {
			p = &remaped_log_buffer[cpu_id * LOG_NS_CPU_AREA_SIZE];
			alloc_size = sizeof(*node) + strlen(p) + 1;
			node = kmalloc(alloc_size, GFP_KERNEL);
			if (node) {
				node->logmsg = (char *)&node[1];
				INIT_LIST_HEAD(&node->list);
				strcpy(node->logmsg, p);
				spin_lock(&dlog->q_lock);
				list_add_tail(&node->list, &dlog->queue);
				spin_unlock(&dlog->q_lock);
				wake_up_interruptible(&dlog->waitq);
				arg->ret = TEEC_SUCCESS;
			} else {
				arg->ret = TEEC_ERROR_OUT_OF_MEMORY;
			}
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

static int rcar_optee_init_debug_log(struct optee *optee)
{
	int ret = 0;
	struct task_struct *thread;
	struct arm_smccc_res smccc;

	remaped_log_buffer = ioremap_nocache(TEE_LOG_NS_BASE, TEE_LOG_NS_SIZE);
	if (!remaped_log_buffer) {
		pr_err("failed to ioremap_nocache(TEE_LOG_NS_BASE)\n");
		ret = -ENOMEM;
	}

	if (ret == 0) {
		init_waitqueue_head(&dlog_info.waitq);
		INIT_LIST_HEAD(&dlog_info.queue);
		spin_lock_init(&dlog_info.q_lock);

		thread = kthread_run(debug_log_kthread, &dlog_info,
				     "optee_debug_log");
		if (IS_ERR(thread)) {
			pr_err("failed to kthread_run\n");
			ret = -ENOMEM;
			goto end;
		}
	}

	/* Notify the start of debug log output to optee_os */
	optee->invoke_fn(OPTEE_SMC_GET_SHM_CONFIG, SMC_RCAR_CMD,
		START_DLOG_OUTPUT, 0, 0, 0, 0, 0, &smccc);

end:
	return ret;
}

static void rcar_optee_final_debug_log(void)
{
	struct rcar_debug_log_node *node;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (node) {
		INIT_LIST_HEAD(&node->list);
		node->logmsg = NULL; /* exit kthread */
		spin_lock(&dlog_info.q_lock);
		list_add_tail(&node->list, &dlog_info.queue);
		spin_unlock(&dlog_info.q_lock);
		wake_up(&dlog_info.waitq);
	} else {
		pr_err("failed to kmalloc(rcar_debug_log_node)\n");
	}
}

int optee_rcar_probe(struct optee *optee)
{
	int ret;

	rcar_optee = optee;

	pr_info("optee driver R-Car Rev.%s\n", VERSION_OF_RENESAS);

	ret = rcar_optee_add_suspend_callback();
	if (ret == 0) {
		ret = rcar_optee_init_debug_log(optee);
		if (ret != 0)
			rcar_optee_del_suspend_callback();
	}

	return ret;
}

void optee_rcar_remove(void)
{
	rcar_optee_final_debug_log();
	rcar_optee_del_suspend_callback();
}
