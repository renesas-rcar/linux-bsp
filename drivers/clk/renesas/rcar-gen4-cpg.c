// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car Gen3 Clock Pulse Generator
 *
 * Copyright (C) 2015-2018 Glider bvba
 * Copyright (C) 2019 Renesas Electronics Corp.
 *
 * Based on clk-rcar-gen3.c
 *
 * Copyright (C) 2015 Renesas Electronics Corp.
 */

#include <linux/bug.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen4-cpg.h"
#include "rcar-cpg-lib.h"

/*
 * Z clock T.B.D
 */

/*
 * PLL clock T.B.D
 */

/*
 * RPC/RPCD2 clock T.B.D
 */
struct rpc_clock {
	struct clk_divider div;
	struct clk_gate gate;
	/*
	 * One notifier covers both RPC and RPCD2 clocks as they are both
	 * controlled by the same RPCCKCR register...
	 */
	struct cpg_simple_notifier csn;
};

static const struct clk_div_table cpg_rpcsrc_div_table[] = {
	{ 2, 5 }, { 3, 6 }, { 0, 0 },
};

static const struct clk_div_table cpg_rpc_div_table[] = {
	{ 1, 2 }, { 3, 4 }, { 5, 6 }, { 7, 8 }, { 0, 0 },
};

static struct clk * __init cpg_rpc_clk_register(const char *name,
	void __iomem *base, const char *parent_name,
	struct raw_notifier_head *notifiers)
{
	struct rpc_clock *rpc;
	struct clk *clk;

	rpc = kzalloc(sizeof(*rpc), GFP_KERNEL);
	if (!rpc)
		return ERR_PTR(-ENOMEM);

	rpc->div.reg = base + CPG_RPCCKCR;
	rpc->div.width = 3;
	rpc->div.table = cpg_rpc_div_table;
	rpc->div.lock = &cpg_lock;

	rpc->gate.reg = base + CPG_RPCCKCR;
	rpc->gate.bit_idx = 8;
	rpc->gate.flags = CLK_GATE_SET_TO_DISABLE;
	rpc->gate.lock = &cpg_lock;

	rpc->csn.reg = base + CPG_RPCCKCR;

	clk = clk_register_composite(NULL, name, &parent_name, 1, NULL, NULL,
				     &rpc->div.hw,  &clk_divider_ops,
				     &rpc->gate.hw, &clk_gate_ops,
				     CLK_SET_RATE_PARENT);
	if (IS_ERR(clk)) {
		kfree(rpc);
		return clk;
	}

	cpg_simple_notifier_register(notifiers, &rpc->csn);
	return clk;
}

struct rpcd2_clock {
	struct clk_fixed_factor fixed;
	struct clk_gate gate;
};

static struct clk * __init cpg_rpcd2_clk_register(const char *name,
						  void __iomem *base,
						  const char *parent_name)
{
	struct rpcd2_clock *rpcd2;
	struct clk *clk;

	rpcd2 = kzalloc(sizeof(*rpcd2), GFP_KERNEL);
	if (!rpcd2)
		return ERR_PTR(-ENOMEM);

	rpcd2->fixed.mult = 1;
	rpcd2->fixed.div = 2;

	rpcd2->gate.reg = base + CPG_RPCCKCR;
	rpcd2->gate.bit_idx = 9;
	rpcd2->gate.flags = CLK_GATE_SET_TO_DISABLE;
	rpcd2->gate.lock = &cpg_lock;

	clk = clk_register_composite(NULL, name, &parent_name, 1, NULL, NULL,
				     &rpcd2->fixed.hw, &clk_fixed_factor_ops,
				     &rpcd2->gate.hw, &clk_gate_ops,
				     CLK_SET_RATE_PARENT);
	if (IS_ERR(clk))
		kfree(rpcd2);

	return clk;
}


static const struct rcar_gen4_cpg_pll_config *cpg_pll_config __initdata;
static unsigned int cpg_clk_extalr __initdata;
static u32 cpg_mode __initdata;

struct clk * __init rcar_gen4_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct clk **clks, void __iomem *base,
	struct raw_notifier_head *notifiers)
{
	const struct clk *parent;
	unsigned int mult = 1;
	unsigned int div = 1;

	parent = clks[core->parent & 0xffff];	/* some types use high bits */
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	switch (core->type) {
	case CLK_TYPE_GEN4_MAIN:
		div = cpg_pll_config->extal_div;
		break;

	case CLK_TYPE_GEN4_PLL1:
		mult = cpg_pll_config->pll1_mult;
		div = cpg_pll_config->pll1_div;
		break;

	case CLK_TYPE_GEN4_PLL2:
		mult = cpg_pll_config->pll2_mult;
		div = cpg_pll_config->pll2_div;
		break;

	case CLK_TYPE_GEN4_PLL3:
		mult = cpg_pll_config->pll3_mult;
		div = cpg_pll_config->pll3_div;
		break;

	case CLK_TYPE_GEN4_PLL5:
		mult = cpg_pll_config->pll5_mult;
		div = cpg_pll_config->pll5_div;
		break;

	case CLK_TYPE_GEN4_PLL6:
		mult = cpg_pll_config->pll6_mult;
		div = cpg_pll_config->pll6_div;
		break;

	case CLK_TYPE_GEN4_MDSEL:
		/*
		 * Clock selectable between two parents and two fixed dividers
		 * using a mode pin
		 */
		if (cpg_mode & BIT(core->offset)) {
			div = core->div & 0xffff;
		} else {
			parent = clks[core->parent >> 16];
			if (IS_ERR(parent))
				return ERR_CAST(parent);
			div = core->div >> 16;
		}
		mult = 1;
		break;

	case CLK_TYPE_GEN4_OSC:
		/*
		 * Clock combining OSC EXTAL predivider and a fixed divider
		 */
		div = cpg_pll_config->osc_prediv * core->div;
		break;

	case CLK_TYPE_GEN4_RPCSRC:
		return clk_register_divider_table(NULL, core->name,
						  __clk_get_name(parent), 0,
						  base + CPG_RPCCKCR, 3, 2, 0,
						  cpg_rpcsrc_div_table,
						  &cpg_lock);

	case CLK_TYPE_GEN4_RPC:
		return cpg_rpc_clk_register(core->name, base,
					    __clk_get_name(parent), notifiers);

	case CLK_TYPE_GEN4_RPCD2:
		return cpg_rpcd2_clk_register(core->name, base,
					      __clk_get_name(parent));

	case CLK_TYPE_GEN4_SD:
		return cpg_sd_clk_register(core->name, base, core->offset,
					   __clk_get_name(parent), notifiers,
					   0, 0);

	default:
		return ERR_PTR(-EINVAL);
	}

	return clk_register_fixed_factor(NULL, core->name,
					 __clk_get_name(parent), 0, mult, div);
}

int __init rcar_gen4_cpg_init(const struct rcar_gen4_cpg_pll_config *config,
			      unsigned int clk_extalr, u32 mode)
{
	cpg_pll_config = config;
	cpg_clk_extalr = clk_extalr;
	cpg_mode = mode;

	spin_lock_init(&cpg_lock);

	return 0;
}
