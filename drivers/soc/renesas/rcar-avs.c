/*
 * Renesas R-Car AVS Support
 *
 *  Copyright (C) 2016 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>

#ifdef CONFIG_POWER_AVS
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>

/* Change the default opp_table pattern in device tree.
 * Set opp_pattern_num is default.
 */

int change_default_opp_pattern(unsigned int opp_pattern_num)
{
	struct device_node *cpu_node = NULL;

	__be32 *list, *pp_val;
	int size;
	struct property *pp;

	for_each_node_with_property(cpu_node, "operating-points-v2") {
		pp = of_find_property(cpu_node, "operating-points-v2", &size);
		if (!pp || !pp->value)
			return -ENOENT;

		pp_val = pp->value;
		size = size / sizeof(*pp_val);
		if (size > opp_pattern_num) {
			list = kzalloc(sizeof(*pp_val), GFP_KERNEL);
			if (!list) {
				pr_debug("%s(): kzalloc fail, return -ENOMEM\n",
						__func__);
				return -ENOMEM;
			}
			*list = *(pp_val + opp_pattern_num);
			pp->value = list;
		}
		pp->length = sizeof(*list); /* opp fw only accept 1 opp_tb */

		pr_info("rcar-cpufreq: %s is running with: %s\n",
			of_node_full_name(cpu_node),
			of_node_full_name(of_find_node_by_phandle(
				be32_to_cpup(pp->value))));
	}

	return 0;
}

/* Get AVS value */
#define ADVFS_BASE		0xE60A0000
#define KSEN_ADJCNTS		(ADVFS_BASE + 0x13C)
#define VOLCOND_MASK_0_3	0x0f	/* VOLCOND[3:0] */

#define AVS_TABLE_NUM	7

unsigned int get_avs_value(void)
{
	unsigned int ret;
	void __iomem *ksen_adjcnts = ioremap_nocache(KSEN_ADJCNTS, 4);
	u32 ksen_adjcnts_value = ioread32(ksen_adjcnts);

	ksen_adjcnts_value &= VOLCOND_MASK_0_3;
	if (ksen_adjcnts_value >= 0 && ksen_adjcnts_value < AVS_TABLE_NUM) {
		ret = ksen_adjcnts_value;
	} else {
		ret = 0;
		pr_debug("rcar-cpufreq: hw get invalid avs value, use avs_tb0\n");
	}
	pr_info("rcar-cpufreq: use avs value: %d\n", ksen_adjcnts_value);
	iounmap(ksen_adjcnts);

	return ret;
}
#endif /* CONFIG_POWER_AVS */

int __init rcar_avs_init(void)
{
#ifdef CONFIG_POWER_AVS
	int avs_val = get_avs_value();

	change_default_opp_pattern(avs_val);
#endif /* CONFIG_POWER_AVS */
	return 0;
}

subsys_initcall(rcar_avs_init);

MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_DESCRIPTION("R-Car AVS module");
MODULE_LICENSE("GPL v2");
