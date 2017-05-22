/*
 * r8a77995 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2016 Cogent Embedded Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation; of the License.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/renesas.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/renesas-cpg-mssr.h>
#include <dt-bindings/clock/r8a77995-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen3-cpg.h"

#define CPG_PLL0CR		0x00d8

#define DEF_R8A77995_PLL0_CKSEL(_name, _id, _parent, _div)	\
	DEF_BASE(_name, _id, CLK_TYPE_R8A77995_PLL0_CKSEL, _parent, \
		 .div = _div, .mult = 1)

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A77995_CLK_CP,

	/* External Input Clocks */
	CLK_EXTAL,
	CLK_LOCO,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL3,

	CLK_PLL0_DIV2,
	CLK_PLL0_DIV3,
	CLK_PLL0_DIV5,

	CLK_S0,
	CLK_S1,
	CLK_S2,
	CLK_S3,

	CLK_PE,

	/* Module Clocks */
	MOD_CLK_BASE
};

enum r8a77995_clk_types {
	CLK_TYPE_R8A77995_MAIN = CLK_TYPE_GEN3_OSC,
	CLK_TYPE_R8A77995_PLL0,
	CLK_TYPE_R8A77995_PLL1,
	CLK_TYPE_R8A77995_PLL3,

	CLK_TYPE_R8A77995_PLL0_CKSEL,
};

static const struct cpg_core_clk r8a77995_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",  CLK_EXTAL),

	/* Internal Core Clocks */
	DEF_BASE(".main", CLK_MAIN, CLK_TYPE_R8A77995_MAIN, CLK_EXTAL),
	DEF_BASE(".pll0", CLK_PLL0, CLK_TYPE_R8A77995_PLL0, CLK_MAIN),
	DEF_BASE(".pll1", CLK_PLL1, CLK_TYPE_R8A77995_PLL1, CLK_MAIN),
	DEF_BASE(".pll3", CLK_PLL3, CLK_TYPE_R8A77995_PLL3, CLK_MAIN),

	DEF_FIXED(".pll0_div2", CLK_PLL0_DIV2, CLK_PLL0,  2, 1),
	DEF_FIXED(".pll0_div3", CLK_PLL0_DIV3, CLK_PLL0,  3, 1),
	DEF_FIXED(".pll0_div5", CLK_PLL0_DIV5, CLK_PLL0,  5, 1),

	DEF_FIXED(".s0",        CLK_S0,        CLK_PLL1,  2, 1),
	DEF_FIXED(".s1",        CLK_S1,        CLK_PLL1,  3, 1),
	DEF_FIXED(".s2",        CLK_S2,        CLK_PLL1,  4, 1),
	DEF_FIXED(".s3",        CLK_S3,        CLK_PLL1,  6, 1),

	/* Core Clock Outputs */
	DEF_BASE("z2",   R8A77995_CLK_Z2,   CLK_TYPE_GEN3_Z2,   CLK_PLL0_DIV3),

	DEF_FIXED("ztr",        R8A77995_CLK_ZTR,   CLK_PLL1,       6, 1),
	DEF_FIXED("zt",         R8A77995_CLK_ZT,    CLK_PLL1,       4, 1),
	DEF_FIXED("zx",         R8A77995_CLK_ZX,    CLK_PLL1,       3, 1),

	DEF_FIXED("s0d1",       R8A77995_CLK_S0D1,  CLK_S0,         1, 1),
	DEF_FIXED("s1d1",       R8A77995_CLK_S1D1,  CLK_S1,         1, 1),
	DEF_FIXED("s1d2",       R8A77995_CLK_S1D2,  CLK_S1,         2, 1),
	DEF_FIXED("s1d4",       R8A77995_CLK_S1D4,  CLK_S1,         4, 1),
	DEF_FIXED("s2d1",       R8A77995_CLK_S2D1,  CLK_S2,         1, 1),
	DEF_FIXED("s2d2",       R8A77995_CLK_S2D2,  CLK_S2,         2, 1),
	DEF_FIXED("s2d4",       R8A77995_CLK_S2D4,  CLK_S2,         4, 1),
	DEF_FIXED("s3d1",       R8A77995_CLK_S3D1,  CLK_S3,         1, 1),
	DEF_FIXED("s3d2",       R8A77995_CLK_S3D2,  CLK_S3,         2, 1),
	DEF_FIXED("s3d4",       R8A77995_CLK_S3D4,  CLK_S3,         4, 1),

	DEF_FIXED("pe",         CLK_PE,             CLK_PLL0_DIV3,  4, 1),

	DEF_R8A77995_PLL0_CKSEL("s1d4c", R8A77995_CLK_S1D4C, CLK_S1, 4),
	DEF_R8A77995_PLL0_CKSEL("s3d1c", R8A77995_CLK_S3D1C, CLK_S3, 1),
	DEF_R8A77995_PLL0_CKSEL("s3d2c", R8A77995_CLK_S3D2C, CLK_S3, 2),
	DEF_R8A77995_PLL0_CKSEL("s3d4c", R8A77995_CLK_S3D4C, CLK_S3, 4),

	DEF_FIXED("cl",         R8A77995_CLK_CL,    CLK_PLL1,      48, 1),

	DEF_FIXED("cp",         R8A77995_CLK_CP,    CLK_EXTAL,      2, 1),
};

static const struct mssr_mod_clk r8a77995_mod_clks[] __initconst = {
	DEF_MOD("scif5",		 202,	R8A77995_CLK_S3D4C),
	DEF_MOD("scif4",		 203,	R8A77995_CLK_S3D4C),
	DEF_MOD("scif3",		 204,	R8A77995_CLK_S3D4C),
	DEF_MOD("scif1",		 206,	R8A77995_CLK_S3D4C),
	DEF_MOD("scif0",		 207,	R8A77995_CLK_S3D4C),
	DEF_MOD("scif2",		 310,	R8A77995_CLK_S3D4C),
	DEF_MOD("intc-ex",		 407,	R8A77995_CLK_CP),
	DEF_MOD("intc-ap",		 408,	R8A77995_CLK_S1D2),
	DEF_MOD("gpio6",		 906,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio5",		 907,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio4",		 908,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio3",		 909,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio2",		 910,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio1",		 911,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio0",		 912,	R8A77995_CLK_S3D4),
};

static const unsigned int r8a77995_crit_mod_clks[] __initconst = {
	MOD_CLK_ID(408),	/* INTC-SYS (GIC) */
};

/*
 * CPG Clock Data
 */

/*
 * MD	EXTAL                           Internal
 * 19	(MHz)	PLL0	PLL1	PLL3    RCLK		OSCCLK
 *-------------------------------------------------------------
 * 0	1	x250/4	x100/3	x100/3  x 1/1536	x 1/384
 * 1	1	x250/4	x100/3	x116/6  x 1/1536	x 1/384
 */
#define CPG_PLL_CONFIG_INDEX(md)	(((md) & BIT(19)) >> 19)

static const struct rcar_gen3_cpg_pll_config cpg_pll_configs[2] __initconst = {
	/*
	 *		PLL1 mult	PLL3 mult	internal R	OSC
	 * EXTAL div	(not refs)	(not refs)	div		div
	 */
	{ 1,		33,		33,		1536,		384 },
	{ 1,		33,		19,		1536,		384 },
};

struct extra_cpg_pll_config {
	unsigned int extal_div;
	unsigned int pll1_mult;
	unsigned int pll1_div;
	unsigned int pll3_mult;
	unsigned int pll3_div;
};

static const struct extra_cpg_pll_config extra_cpg_pll_confs[2] __initconst = {
	/*
	 * EXTAL	PLL1		PLL3
	 * div		mult, div	mult, div
	 */
	{ 1,		100, 3,		100, 3,	},
	{ 1,		100, 3,		116, 6,	},
};

static u32 cpg_mode __initdata;

static struct clk * __init r8a77995_cpg_clk_register(
	struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct clk **clks, void __iomem *base)
{
	const struct extra_cpg_pll_config *extra_cpg_pll_config;
	const struct clk *parent;
	const char *parent_name;
	unsigned int mult = 1;
	unsigned int div = 1;
	u32 value;

	extra_cpg_pll_config =
		&extra_cpg_pll_confs[CPG_PLL_CONFIG_INDEX(cpg_mode)];

	parent = clks[core->parent];
	if (IS_ERR(parent))
		return ERR_CAST(parent);
	parent_name = __clk_get_name(parent);

	switch (core->type) {
	case CLK_TYPE_R8A77995_MAIN:
		div = extra_cpg_pll_config->extal_div;
		break;

	case CLK_TYPE_R8A77995_PLL0:
		/*
		 * PLL2 is a configurable multiplier clock. Register it as a
		 * fixed factor clock for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		value = readl(base + CPG_PLL0CR);
		mult = (((value >> 24) & 0x7f) + 1) * 2;
		div = ((value >> 20) & 0x3) + 1;
		break;

	case CLK_TYPE_R8A77995_PLL1:
		mult = extra_cpg_pll_config->pll1_mult;
		div = extra_cpg_pll_config->pll1_div;
		break;

	case CLK_TYPE_R8A77995_PLL3:
		mult = extra_cpg_pll_config->pll3_mult;
		div = extra_cpg_pll_config->pll3_div;
		break;

	case CLK_TYPE_R8A77995_PLL0_CKSEL:
		mult = core->mult;
		div = core->div;

		value = readl(base + CPG_PLL0CR);
		if (value & BIT(13))
			parent_name = "pe";
		break;

	default:
		return rcar_gen3_cpg_clk_register(dev, core, info, clks, base);
	}

	return clk_register_fixed_factor(NULL, core->name,
					 parent_name, 0, mult, div);
}

static int __init r8a77995_cpg_mssr_init(struct device *dev)
{
	const struct rcar_gen3_cpg_pll_config *cpg_pll_config;
	int error;

	error = rcar_rst_read_mode_pins(&cpg_mode);
	if (error)
		return error;

	cpg_pll_config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];
	if (!cpg_pll_config->extal_div) {
		dev_err(dev, "Prohibited setting (cpg_mode=0x%x)\n", cpg_mode);
		return -EINVAL;
	}

	return rcar_gen3_cpg_init(cpg_pll_config, CLK_LOCO, cpg_mode);
}

const struct cpg_mssr_info r8a77995_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a77995_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a77995_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a77995_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a77995_mod_clks),
	.num_hw_mod_clks = 12 * 32,

	/* Critical Module Clocks */
	.crit_mod_clks = r8a77995_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r8a77995_crit_mod_clks),

	/* Callbacks */
	.init = r8a77995_cpg_mssr_init,
	.cpg_clk_register = r8a77995_cpg_clk_register,
};
