/*
 *  R-Car Gen3 Emergency shutdown for thermal management
 *
 * Copyright (C) 2016 Renesas Electronics Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 */
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>

#include <linux/soc/renesas/rcar_ems_ctrl.h>

#define EMS_THERMAL_ZONE_MAX	10

static void rcar_ems_monitor(struct work_struct *ws);
static DECLARE_DELAYED_WORK(rcar_ems_monitor_work, rcar_ems_monitor);

static RAW_NOTIFIER_HEAD(rcar_ems_chain);

static int ems_mode = RCAR_EMS_MODE_OFF;
static int ems_mode_on_temp;
static int ems_mode_off_temp;
static int ems_poll;

static int thermal_zone_num;
static struct thermal_zone_device *thermal_zone[EMS_THERMAL_ZONE_MAX];

#ifdef CONFIG_RCAR_THERMAL_EMS_ENABLED
static bool ems_enabled = true;
#else
static bool ems_enabled = false;
#endif

static int rcar_ems_notify(unsigned long state, void *p)
{
	return raw_notifier_call_chain(&rcar_ems_chain, state, p);
}

int register_rcar_ems_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&rcar_ems_chain, nb);
}
EXPORT_SYMBOL(register_rcar_ems_notifier);

void unregister_rcar_ems_notifier(struct notifier_block *nb)
{
	raw_notifier_chain_unregister(&rcar_ems_chain, nb);
}
EXPORT_SYMBOL(unregister_rcar_ems_notifier);

static void rcar_ems_monitor(struct work_struct *ws)
{
	int i, ret;
	int temp, max_temp;

	max_temp = INT_MIN;
	for (i = 0; i < thermal_zone_num; i++) {
		if (thermal_zone[i]) {
			ret = thermal_zone_get_temp(
					thermal_zone[i], &temp);
			if (!ret) {
				if (max_temp < temp)
					max_temp = temp;
			}
		}
	}

	if (max_temp == INT_MIN)
		goto skip;

	if (ems_mode == RCAR_EMS_MODE_OFF) {
		if (max_temp >= ems_mode_on_temp) {
			ems_mode  = RCAR_EMS_MODE_ON;
			rcar_ems_notify(RCAR_EMS_MODE_ON,
					(void *)(long)max_temp);
		}
	} else {
		if (max_temp <= ems_mode_off_temp) {
			ems_mode = RCAR_EMS_MODE_OFF;
			rcar_ems_notify(RCAR_EMS_MODE_OFF,
					(void *)(long)max_temp);
		}
	}

skip:
	schedule_delayed_work(&rcar_ems_monitor_work, ems_poll);

}


int rcar_ems_get_mode(void)
{
	return ems_mode;
}
EXPORT_SYMBOL(rcar_ems_get_mode);

static int __init rcar_ems_ctrl_init(void)
{
	struct device_node *np, *c;
	struct thermal_zone_device *zone;
	u32 value;

	if (ems_enabled == false) {
		pr_err("thermal emergency: disabled\n");
		return 0;
	}

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np)
		return 0;

	for_each_child_of_node(np, c) {
		if (!strcmp(c->name, "emergency")) {
			if (!of_property_read_u32(c,
				"polling-delay", &value))
				ems_poll = msecs_to_jiffies(value);

			if (!of_property_read_u32(c,
				"on-temperature", &value))
				ems_mode_on_temp = value;

			if (!of_property_read_u32(c,
				"off-temperature", &value))
				ems_mode_off_temp = value;
		} else {
			zone = thermal_zone_get_zone_by_name(c->name);
			if (IS_ERR(zone))
				continue;

			if (thermal_zone_num < EMS_THERMAL_ZONE_MAX) {
				thermal_zone[thermal_zone_num] = zone;
				thermal_zone_num++;
			}
		}
	}
	of_node_put(np);

	if (thermal_zone_num == 0) {
		pr_err("thermal emergency: not find thermal_zone\n");
		return 0;
	}

	if (ems_poll == 0 ||
	    ems_mode_on_temp == 0 || ems_mode_off_temp == 0) {
		pr_err("thermal emergency: not set value\n");
		return 0;
	}

	schedule_delayed_work(&rcar_ems_monitor_work, ems_poll);

	pr_info("thermal emergency: set temperature to %d celsius\n",
		ems_mode_on_temp / 1000);

	return 0;
}

static void __exit rcar_ems_ctrl_exit(void)
{
	cancel_delayed_work_sync(&rcar_ems_monitor_work);
}

late_initcall(rcar_ems_ctrl_init)
module_exit(rcar_ems_ctrl_exit)


/* emergency cpu shutdown function */
static struct cpumask target_cpus;

static int rcar_ems_cpufreq_notifier_call(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;
	int mode;

	if (!cpumask_test_cpu(policy->cpu, &target_cpus))
		return NOTIFY_DONE;

	switch (event) {
	case CPUFREQ_ADJUST:
		mode = rcar_ems_get_mode();
		if (mode == RCAR_EMS_MODE_ON) {
			cpufreq_verify_within_limits(policy,
					    policy->cpuinfo.min_freq,
					    policy->cpuinfo.min_freq);
		}
		break;

	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static int rcar_ems_thermal_notifier_call(struct notifier_block *nb,
	unsigned long state, void *val)
{
	long temp = (long)val;
	int cpu, first = 0;

	pr_info("thermal emergency notifier: state=%ld (temp=%ld)\n",
		state, temp);

	switch (state) {
	case RCAR_EMS_MODE_ON:
		for_each_cpu(cpu, &target_cpus) {
			if (cpu_online(cpu)) {
				if (first != 0)
					cpu_down(cpu);

				first = 1;
			}
		}
		break;

	case RCAR_EMS_MODE_OFF:
		for_each_cpu(cpu, &target_cpus) {
			if  (!cpu_online(cpu))
				cpu_up(cpu);
		}
		break;

	default:
		return NOTIFY_DONE;
	}

	cpufreq_update_policy(cpumask_any(&target_cpus));

	return NOTIFY_OK;
}

static struct notifier_block ems_thermal_notifier_block = {
	.notifier_call = rcar_ems_thermal_notifier_call,
};
static struct notifier_block ems_cpufreq_notifier_block = {
	.notifier_call = rcar_ems_cpufreq_notifier_call,
};

static int __init rcar_ems_cpu_shutdown_init(void)
{
	int cpu;
	struct device_node *cpu_node;

	if (ems_enabled == false)
		return 0;

	cpumask_clear(&target_cpus);

	for_each_compatible_node(cpu_node, "cpu", "arm,cortex-a57") {
		for_each_possible_cpu(cpu) {
			if (of_get_cpu_node(cpu, NULL) == cpu_node) {
				cpumask_set_cpu(cpu, &target_cpus);
				break;
			}
		}
	}

	if (cpumask_weight(&target_cpus) <= 1) {
		pr_err("thermal emergency: shutdown cpu none\n");
		return 0;
	}

	register_rcar_ems_notifier(&ems_thermal_notifier_block);
	cpufreq_register_notifier(&ems_cpufreq_notifier_block,
				  CPUFREQ_POLICY_NOTIFIER);

	pr_info("thermal emergency: shutdown target cpus %*pbl\n",
		cpumask_pr_args(&target_cpus));

	return 0;
}

static void __exit rcar_ems_cpu_shutdown_exit(void)
{
	unregister_rcar_ems_notifier(&ems_thermal_notifier_block);
	cpufreq_unregister_notifier(&ems_cpufreq_notifier_block,
				    CPUFREQ_POLICY_NOTIFIER);
}

late_initcall(rcar_ems_cpu_shutdown_init);
module_exit(rcar_ems_cpu_shutdown_exit);
