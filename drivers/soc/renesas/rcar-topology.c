// SPDX-License-Identifier: GPL-2.0
/*
 *  R-Car CPU topology for ARM big.LITTLE platforms
 *
 * Copyright (C) 2017 Renesas Electronics Corporation.
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

#include <linux/cpuset.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/sched/topology.h>
#include <linux/topology.h>

static int rcar_cpu_cpu_flags(void)
{
	return SD_ASYM_CPUCAPACITY;
}

static struct sched_domain_topology_level rcar_topology[] = {
#ifdef CONFIG_SCHED_MC
	{ cpu_coregroup_mask, cpu_core_flags, SD_INIT_NAME(MC) },
#endif
	{ cpu_cpu_mask, rcar_cpu_cpu_flags, SD_INIT_NAME(DIE) },
	{ NULL, }
};

static int __init rcar_topology_init(void)
{
	if (of_machine_is_compatible("renesas,r8a7795") ||
	    of_machine_is_compatible("renesas,r8a7796"))
		set_sched_topology(rcar_topology);

	return 0;
}
early_initcall(rcar_topology_init);
