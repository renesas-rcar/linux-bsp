/*
 * rcar_gen3 Core CPG Clocks
 *
 * Copyright (C) 2015 Renesas Electronics Corp.
 *
 * Based on rcar_gen2 Core CPG Clocks driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/shmobile.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/spinlock.h>

struct rcar_gen3_cpg {
	struct clk_onecell_data data;
	spinlock_t lock;
	void __iomem *reg;
};

#define CPG_PLL0CR	0x00d8
#define CPG_PLL2CR	0x002c

/*
 * common function
 */
#define rcar_clk_readl(cpg, _reg) clk_readl(cpg->reg + _reg)

/*
 * Reset register definitions.
 */
#define MODEMR 0xe6160060

static u32 rcar_gen3_read_mode_pins(void)
{
	static u32 mode;
	static bool mode_valid;

	if (!mode_valid) {
		void __iomem *modemr = ioremap_nocache(MODEMR, 4);

		BUG_ON(!modemr);
		mode = ioread32(modemr);
		iounmap(modemr);
		mode_valid = true;
	}

	return mode;
}

/* -----------------------------------------------------------------------------
 * CPG Clock Data
 */

/*
 *   MD		EXTAL		PLL0	PLL1	PLL2	PLL3	PLL4
 * 14 13 19 17	(MHz)		*1	*1	*1
 *-------------------------------------------------------------------
 * 0  0  0  0	16.66 x 1	x180/2	x192/2	x144/2	x192	x144
 * 0  0  0  1	16.66 x 1	x180/2	x192/2	x144/2	x128	x144
 * 0  0  1  0	Prohibited setting
 * 0  0  1  1	16.66 x 1	x180/2	x192/2	x144/2	x192	x144
 * 0  1  0  0	20    x 1	x150/2	x156/2	x120/2	x156	x120
 * 0  1  0  1	20    x 1	x150/2	x156/2	x120/2	x106	x120
 * 0  1  1  0	Prohibited setting
 * 0  1  1  1	20    x 1	x150/2	x156/2	x120/2	x156	x120
 * 1  0  0  0	25    x 1	x120/2	x128/2	x96/2	x128	x96
 * 1  0  0  1	25    x 1	x120/2	x128/2	x96/2	x84	x96
 * 1  0  1  0	Prohibited setting
 * 1  0  1  1	25    x 1	x120/2	x128/2	x96/2	x128	x96
 * 1  1  0  0	33.33 / 2	x180/2	x192/2	x144/2	x192	x144
 * 1  1  0  1	33.33 / 2	x180/2	x192/2	x144/2	x128	x144
 * 1  1  1  0	Prohibited setting
 * 1  1  1  1	33.33 / 2	x180/2	x192/2	x144/2	x192	x144
 *
 * *1 : datasheet indicates VCO output (PLLx = VCO/2)
 *
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 11) | \
					 (((md) & BIT(13)) >> 11) | \
					 (((md) & BIT(19)) >> 18) | \
					 (((md) & BIT(17)) >> 17))
struct cpg_pll_config {
	unsigned int extal_div;
	unsigned int pll1_mult;
	unsigned int pll3_mult;
	unsigned int pll4_mult;
};

static const struct cpg_pll_config cpg_pll_configs[16] __initconst = {
/* EXTAL div	PLL1	PLL3	PLL4 */
	{ 1,	192,	192,	144, },
	{ 1,	192,	128,	144, },
	{ 0,	0,	0,	0,   }, /* Prohibited setting */
	{ 1,	192,	192,	144, },
	{ 1,	156,	156,	120, },
	{ 1,	156,	106,	120, },
	{ 0,	0,	0,	0,   }, /* Prohibited setting */
	{ 1,	156,	156,	120, },
	{ 1,	128,	128,	96,  },
	{ 1,	128,	84,	96,  },
	{ 0,	0,	0,	0,   }, /* Prohibited setting */
	{ 1,	128,	128,	96,  },
	{ 2,	192,	192,	144, },
	{ 2,	192,	128,	144, },
	{ 0,	0,	0,	0,   }, /* Prohibited setting */
	{ 2,	192,	192,	144, },
};

/* -----------------------------------------------------------------------------
 * Initialization
 */

static u32 cpg_mode __initdata;

static struct clk * __init
rcar_gen3_cpg_register_clock(struct device_node *np, struct rcar_gen3_cpg *cpg,
			     const struct cpg_pll_config *config,
			     const char *name)
{
	const char *parent_name;
	unsigned int mult = 1;
	unsigned int div = 1;

	if (!strcmp(name, "main")) {
		parent_name = of_clk_get_parent_name(np, 0);
		div = config->extal_div;
	} else if (!strcmp(name, "pll0")) {
		/* PLL0 is a configurable multiplier clock. Register it as a
		 * fixed factor clock for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		u32 value = rcar_clk_readl(cpg, CPG_PLL0CR);

		parent_name = "main";
		mult = ((value >> 24) & ((1 << 7) - 1)) + 1;
	} else if (!strcmp(name, "pll1")) {
		parent_name = "main";
		mult = config->pll1_mult / 2;
	} else if (!strcmp(name, "pll2")) {
		/* PLL2 is a configurable multiplier clock. Register it as a
		 * fixed factor clock for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		u32 value = rcar_clk_readl(cpg, CPG_PLL2CR);

		parent_name = "main";
		mult = ((value >> 24) & ((1 << 7) - 1)) + 1;
	} else if (!strcmp(name, "pll3")) {
		parent_name = "main";
		mult = config->pll3_mult;
	} else if (!strcmp(name, "pll4")) {
		parent_name = "main";
		mult = config->pll4_mult;
	} else {
		return ERR_PTR(-EINVAL);
	}

	return clk_register_fixed_factor(NULL, name, parent_name, 0, mult, div);
}

static void __init rcar_gen3_cpg_clocks_init(struct device_node *np)
{
	const struct cpg_pll_config *config;
	struct rcar_gen3_cpg *cpg;
	struct clk **clks;
	unsigned int i;
	int num_clks;

	cpg_mode = rcar_gen3_read_mode_pins();

	num_clks = of_property_count_strings(np, "clock-output-names");
	if (num_clks < 0) {
		pr_err("%s: failed to count clocks\n", __func__);
		return;
	}

	cpg = kzalloc(sizeof(*cpg), GFP_KERNEL);
	clks = kzalloc((num_clks * sizeof(*clks)), GFP_KERNEL);
	if (cpg == NULL || clks == NULL) {
		/* We're leaking memory on purpose, there's no point in cleaning
		 * up as the system won't boot anyway.
		 */
		pr_err("%s: failed to allocate cpg\n", __func__);
		return;
	}

	spin_lock_init(&cpg->lock);

	cpg->data.clks = clks;
	cpg->data.clk_num = num_clks;

	cpg->reg = of_iomap(np, 0);
	if (WARN_ON(cpg->reg == NULL))
		return;

	config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];
	if (!config->extal_div) {
		pr_err("%s: Prohibited setting (cpg_mode=0x%x)\n",
		       __func__, cpg_mode);
		return;
	}

	for (i = 0; i < num_clks; ++i) {
		const char *name;
		struct clk *clk;

		of_property_read_string_index(np, "clock-output-names", i,
					      &name);

		clk = rcar_gen3_cpg_register_clock(np, cpg, config, name);
		if (IS_ERR(clk))
			pr_err("%s: failed to register %s %s clock (%ld)\n",
			       __func__, np->name, name, PTR_ERR(clk));
		else
			cpg->data.clks[i] = clk;
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, &cpg->data);
}
CLK_OF_DECLARE(rcar_gen3_cpg_clks, "renesas,rcar-gen3-cpg-clocks",
	       rcar_gen3_cpg_clocks_init);
