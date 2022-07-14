// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car AVS Support
 *
 *  Copyright (C) 2022 Renesas Electronics Corporation
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
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

/* Change the default opp_table pattern in device tree.
 * Set opp_pattern_num is default.
 */

static int change_default_opp_pattern(struct device_node *avs_node,
				      unsigned int opp_pattern_num)
{
	struct device_node *dev_node = NULL;
	int dev_nums, i;

	__be32 *list, *pp_val;
	int size;
	struct property *pp;

	dev_nums = of_count_phandle_with_args(avs_node, "target_devices", NULL);

	for (i = 0; i < dev_nums; i++) {
		dev_node = of_parse_phandle(avs_node, "target_devices", i);
		pp = of_find_property(dev_node, "operating-points-v2", &size);
		if (!pp || !pp->value)
			return -ENOENT;

		pp_val = pp->value;
		size = size / sizeof(*pp_val);
		if (size > opp_pattern_num) {
			list = kzalloc(sizeof(*pp_val), GFP_KERNEL);
			if (!list)
				return -ENOMEM;

			*list = *(pp_val + opp_pattern_num);
			pp->value = list;
		}
		pp->length = sizeof(*list); /* opp fw only accept 1 opp_tb */

		pr_info("rcar-avs: %s is running with: %s\n",
			of_node_full_name(dev_node),
			of_node_full_name(of_find_node_by_phandle(
					be32_to_cpup(pp->value))));
	}

	return 0;
}

/* Get AVS value */
#define VOLCOND_MASK  0x1ff	/* VOLCOND[8:0] bits of ADVADJP register */

#define AVS_MAX_VALUE	7

static const struct of_device_id rcar_avs_matches[] = {
	{ .compatible = "renesas,rcar-gen3-avs" },
	{ .compatible = "renesas,rcar-gen4-avs" },
	{ .compatible = "renesas,r8a77951-avs" },
	{ .compatible = "renesas,r8a77960-avs" },
	{ .compatible = "renesas,r8a77961-avs" },
	{ .compatible = "renesas,r8a779g0-avs" },
	{ /* sentinel */ }
};

static int __init rcar_avs_init(void)
{
	struct clk *clk;
	u32 avs_val, volcond_val;
	struct device_node *np;
	void __iomem *advadjp;
	int ret = 0, i;

	/* Map and get ADVADJP register */
	np = of_find_matching_node(NULL, rcar_avs_matches);
	if (!np) {
		pr_warn("%s: cannot find compatible dts node\n", __func__);
		return -ENODEV;
	}

	clk = of_clk_get(np, 0);

	if (IS_ERR(clk)) {
		pr_err("avs could not get clk\n");
		ret = PTR_ERR(clk);
		goto err;
	}

	advadjp = of_iomap(np, 0); /* ADVADJP register from dts */
	if (!advadjp) {
		pr_warn("%s: Cannot map regs\n", np->full_name);
		ret = -ENOMEM;
		goto err;
	}

	/* Get and check avs value */
	avs_val = 0; /* default avs table value */

	clk_prepare_enable(clk);
	volcond_val = ioread32(advadjp);
	volcond_val &= VOLCOND_MASK;
	clk_disable_unprepare(clk);

	iounmap(advadjp);

	for (i = 0; i < AVS_MAX_VALUE; i++) {
		if (volcond_val == BIT(i)) {
			avs_val = i + 1; /* found AVS value */
			break;
		}
	}

	pr_info("rcar-avs: use avs value: %d\n", avs_val);

	/* Apply avs value */
	ret = change_default_opp_pattern(np, avs_val);
err:
	of_node_put(np);

	return ret;
}

subsys_initcall(rcar_avs_init);

MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_DESCRIPTION("R-Car AVS module");
MODULE_LICENSE("GPL v2");
