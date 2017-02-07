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
#include <linux/of_address.h>
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
#define VOLCOND_MASK_0_3  0x0f	/* VOLCOND[3:0] bits of KSEN_ADJCNTS register */

#define AVS_TABLE_NUM	7

#endif /* CONFIG_POWER_AVS */

static const struct of_device_id rcar_avs_matches[] = {
#if defined(CONFIG_ARCH_R8A7795) || defined(CONFIG_ARCH_R8A7796)
	{ .compatible = "renesas,rcar-gen3-avs" },
#endif
	{ /* sentinel */ }
};

int __init rcar_avs_init(void)
{
#ifdef CONFIG_POWER_AVS
	int avs_val;
	struct device_node *np;
	void __iomem *ksen_adjcnts;

	/* Map and get KSEN_ADJCNTS register */
	np = of_find_matching_node(NULL, rcar_avs_matches);
	if (!np)
		return -ENODEV;

	ksen_adjcnts = of_iomap(np, 0); /* KSEN_ADJCNTS register from dts */
	if (!ksen_adjcnts) {
		pr_warn("%s: Cannot map regs\n", np->full_name);
		return -ENOMEM;
	}

	/* Get and check avs value */
	avs_val = ioread32(ksen_adjcnts);

	avs_val &= VOLCOND_MASK_0_3;
	if (!(avs_val >= 0 && avs_val < AVS_TABLE_NUM)) {
		avs_val = 0;
		pr_debug("rcar-cpufreq: hw get invalid avs value, use avs_tb0\n");
	}
	pr_info("rcar-cpufreq: use avs value: %d\n", avs_val);

	/* Apply avs value */
	change_default_opp_pattern(avs_val);
#endif /* CONFIG_POWER_AVS */
	return 0;
}

subsys_initcall(rcar_avs_init);

MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_DESCRIPTION("R-Car AVS module");
MODULE_LICENSE("GPL v2");
