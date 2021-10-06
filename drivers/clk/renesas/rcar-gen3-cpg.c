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
#include "rcar-cpg-lib.h"
#include "rcar-gen3-cpg.h"

#define CPG_PLLECR		0x00d0
#define CPG_PLL0CR		0x00d8
#define CPG_PLL2CR		0x002c
#define CPG_PLL4CR		0x01f4

#define CPG_PLLECR_PLL0ST	BIT(8)
#define CPG_PLLECR_PLL2ST	BIT(10)
#define CPG_PLLCR_STC_MASK	GENMASK(30, 24) /* Bits in PLL0/2/4 CR */
#define CPG_RCKCR_CKSEL	BIT(15)	/* RCLK Clock Source Select */

static u32 cpg_quirks;

#define PLL_ERRATA	BIT(0)		/* Missing PLL0/2/4 post-divider */
#define RCKCR_CKSEL	BIT(1)		/* Manual RCLK parent selection */
#define SD_SKIP_FIRST	BIT(2)		/* Skip first clock in SD table */
#define ZG_PARENT_PLL0	BIT(3)		/* Use PLL0 as ZG clock parent */
#define SD_HS400_4TAP	BIT(4)		/* SDnCKCR 4TAP Setting */
/*
 * Z2: SYS-CPU divider 2 on V3H seems to be fixed to 1/2 and 1 on V3M.
 * It is not 100% clear from the User's Manual but at least
 * FRQCRC register is missed on V3x.
 */
#define Z2_SYSCPU_1	BIT(5)	/* Z2 is fixed with SYS-CPU divider 2 set to 1   - V3M */
#define Z2_SYSCPU_2	BIT(6)	/* Z2 is fixed with SYS-CPU divider 2 set to 1/2 - V3H */

/* PLL0 Clock and PLL2 Clock */
struct cpg_pll_clk {
	struct clk_hw hw;
	void __iomem *pllcr_reg;
	void __iomem *pllecr_reg;
	unsigned int fixed_mult;
	unsigned long pllecr_pllst_mask;
};

#define to_pll_clk(_hw)   container_of(_hw, struct cpg_pll_clk, hw)

static unsigned long cpg_pll_clk_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct cpg_pll_clk *pll_clk = to_pll_clk(hw);
	unsigned int val;
	unsigned long rate;

	val = (clk_readl(pll_clk->pllcr_reg) & CPG_PLLCR_STC_MASK);

	rate = parent_rate * ((val >> __bf_shf(CPG_PLLCR_STC_MASK)) + 1)
			   * pll_clk->fixed_mult;

	if (cpg_quirks & PLL_ERRATA)
		rate *= 2; /* PLL output multiplied by 2 */

	return rate;
}

static long cpg_pll_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	struct cpg_pll_clk *pll_clk = to_pll_clk(hw);
	unsigned long prate = *parent_rate;
	unsigned int mult;

	if (cpg_quirks & PLL_ERRATA)
		prate *= 2; /* PLL output multiplied by 2 */

	mult = DIV_ROUND_CLOSEST_ULL(rate, prate) / pll_clk->fixed_mult;

	rate = prate * mult * pll_clk->fixed_mult;

	return rate;
}

static int cpg_pll_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long prate)
{
	struct cpg_pll_clk *pll_clk = to_pll_clk(hw);
	unsigned int mult, i;
	u32 val;

	if (cpg_quirks & PLL_ERRATA)
		prate *= 2; /* PLL output multiplied by 2 */

	mult = DIV_ROUND_CLOSEST_ULL(rate, prate) / pll_clk->fixed_mult;

	val = clk_readl(pll_clk->pllcr_reg) & ~CPG_PLLCR_STC_MASK;
	val |= ((mult - 1) << __bf_shf(CPG_PLLCR_STC_MASK))
		& CPG_PLLCR_STC_MASK;
	clk_writel(val, pll_clk->pllcr_reg);

	for (i = 1000; i; i--) {
		if (clk_readl(pll_clk->pllecr_reg) & pll_clk->pllecr_pllst_mask)

			return 0;

		cpu_relax();
	}

	if (i == 0)
		pr_warn("%s(): PLL %s: long settled time: %d\n",
			__func__, hw->init->name, i);

	return 0;
}

static const struct clk_ops cpg_pll_clk_ops = {
	.recalc_rate = cpg_pll_clk_recalc_rate,
	.round_rate = cpg_pll_clk_round_rate,
	.set_rate = cpg_pll_clk_set_rate,
};

static struct clk * __init cpg_pll_clk_register(const char *name,
						const char *parent_name,
						void __iomem *cpg_base,
						unsigned long pllcr_reg,
						unsigned long pllecr_pllst_mask)

{
	struct clk_init_data init;
	struct cpg_pll_clk *pll_clk;
	struct clk *clk;

	pll_clk = kzalloc(sizeof(*pll_clk), GFP_KERNEL);
	if (!pll_clk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_pll_clk_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll_clk->pllcr_reg = cpg_base + pllcr_reg;
	pll_clk->pllecr_reg = cpg_base + CPG_PLLECR;
	pll_clk->hw.init = &init;
	pll_clk->pllecr_pllst_mask = pllecr_pllst_mask;
	pll_clk->fixed_mult = 2; /*PLL reference clock x (setting+1) x 2*/

	clk = clk_register(NULL, &pll_clk->hw);
	if (IS_ERR(clk))
		kfree(pll_clk);

	return clk;
}

/*
 * Z Clock & Z2 Clock
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is adjustable.
 *        clk->rate = (parent->rate * mult / 32 ) / fixed_div
 * parent - fixed parent.  No clk_set_parent support
 */
#define CPG_FRQCRB			0x00000004
#define CPG_FRQCRB_KICK			BIT(31)
#define CPG_FRQCRB_ZGFC_MASK		GENMASK(28, 24)
#define CPG_FRQCRC			0x000000e0
#define Z_CLK_ROUND(f)	(100000000 * DIV_ROUND_CLOSEST_ULL((f), 100000000))

struct cpg_z_clk {
	struct clk_hw hw;
	void __iomem *reg;
	void __iomem *kick_reg;
	unsigned long max_rate;		/* Maximum rate for normal mode */
	unsigned int fixed_div;
	u32 mask;

};

#define to_z_clk(_hw)	container_of(_hw, struct cpg_z_clk, hw)

static unsigned long cpg_z_clk_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned long prate = parent_rate / zclk->fixed_div;
	unsigned int mult;
	u32 val;

	if (cpg_quirks & Z2_SYSCPU_1) {
		/* SYS-CPU divider 2 is 1 == 32/32) */
		mult = 32;
	} else if (cpg_quirks & Z2_SYSCPU_2) {
		/* SYS-CPU divider 2 is 1/2 == 16/32) */
		mult = 16;
	} else {
		val = readl(zclk->reg) & zclk->mask;
		mult = 32 - (val >> __ffs(zclk->mask));
	}

	return Z_CLK_ROUND(prate * mult / 32);
}

static int cpg_z_clk_determine_rate(struct clk_hw *hw,
				    struct clk_rate_request *req)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned int min_mult, max_mult, mult;
	unsigned long rate, prate;

	rate = min(req->rate, req->max_rate);
	if (rate <= zclk->max_rate) {
		/* Set parent rate to initial value for normal modes */
		prate = zclk->max_rate;
	} else {
		/* Set increased parent rate for boost modes */
		prate = rate;
	}
	req->best_parent_rate = clk_hw_round_rate(clk_hw_get_parent(hw),
						  prate * zclk->fixed_div);

	prate = req->best_parent_rate / zclk->fixed_div;
	min_mult = max(div64_ul(req->min_rate * 32ULL, prate), 1ULL);
	max_mult = min(div64_ul(req->max_rate * 32ULL, prate), 32ULL);
	if (max_mult < min_mult)
		return -EINVAL;

	mult = DIV_ROUND_CLOSEST_ULL(rate * 32ULL, prate);
	mult = clamp(mult, min_mult, max_mult);

	req->rate = DIV_ROUND_CLOSEST_ULL((u64)prate * mult, 32);
	return 0;
}

static int cpg_z_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned long prate = parent_rate / zclk->fixed_div;
	unsigned int mult;
	unsigned int i;

	if (!zclk->max_rate)
		mult = DIV_ROUND_CLOSEST_ULL(rate * 32ULL, prate);
	else if (rate <= zclk->max_rate)
		mult = DIV_ROUND_CLOSEST_ULL(rate * 32ULL, zclk->max_rate);
	else
		mult = 32;

	mult = clamp(mult, 1U, 32U);

	if (readl(zclk->kick_reg) & CPG_FRQCRB_KICK)
		return -EBUSY;

	cpg_reg_modify(zclk->reg, zclk->mask, (32 - mult) << __ffs(zclk->mask));

	/*
	 * Set KICK bit in FRQCRB to update hardware setting and wait for
	 * clock change completion.
	 */
	cpg_reg_modify(zclk->kick_reg, 0, CPG_FRQCRB_KICK);

	/*
	 * Note: There is no HW information about the worst case latency.
	 *
	 * Using experimental measurements, it seems that no more than
	 * ~10 iterations are needed, independently of the CPU rate.
	 * Since this value might be dependent on external xtal rate, pll1
	 * rate or even the other emulation clocks rate, use 1000 as a
	 * "super" safe value.
	 */
	for (i = 1000; i; i--) {
		if (!(readl(zclk->kick_reg) & CPG_FRQCRB_KICK))
			return 0;

		cpu_relax();
	}

	return -ETIMEDOUT;
}

static const struct clk_ops cpg_z_clk_ops = {
	.recalc_rate = cpg_z_clk_recalc_rate,
	.determine_rate = cpg_z_clk_determine_rate,
	.set_rate = cpg_z_clk_set_rate,
};

static struct clk * __init cpg_z_clk_register(const char *name,
					      const char *parent_name,
					      void __iomem *reg,
					      unsigned int div,
					      unsigned int offset)
{
	struct clk_init_data init = {};
	struct cpg_z_clk *zclk;
	struct clk *clk, *parent;

	zclk = kzalloc(sizeof(*zclk), GFP_KERNEL);
	if (!zclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_z_clk_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_PARENT;

	zclk->reg = reg + CPG_FRQCRC;
	zclk->kick_reg = reg + CPG_FRQCRB;
	zclk->hw.init = &init;
	zclk->mask = GENMASK(offset + 4, offset);
	zclk->fixed_div = div; /* PLLVCO x 1/div x SYS-CPU divider */
	zclk->max_rate = 1;

	clk = clk_register(NULL, &zclk->hw);
	if (IS_ERR(clk)) {
		kfree(zclk);
	} else {
		parent = clk_get_parent(clk);
		zclk->max_rate = clk_get_rate(parent) / zclk->fixed_div;
	}

	return clk;
}

static struct clk * __init cpg_zg_clk_register(const char *name,
					       const char *parent_name,
					       void __iomem *reg,
					       unsigned int div,
						   unsigned int offset)
{
	struct clk_init_data init;
	struct cpg_z_clk *zclk;
	struct clk *clk;

	zclk = kzalloc(sizeof(*zclk), GFP_KERNEL);
	if (!zclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_z_clk_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	zclk->reg = reg + CPG_FRQCRB;
	zclk->kick_reg = reg + CPG_FRQCRB;
	zclk->hw.init = &init;
	zclk->mask = GENMASK(offset + 4, offset);
	zclk->fixed_div = div; /* PLLVCO x 1/div1 x 3DGE divider x 1/div2 */

	clk = clk_register(NULL, &zclk->hw);
	if (IS_ERR(clk))
		kfree(zclk);

	return clk;
}

static unsigned long cpg_zg_pll0_clk_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned long prate = parent_rate / zclk->fixed_div;
	unsigned int div;
	u32 val;

	val = clk_readl(zclk->reg) & zclk->mask;
	div = ((val >> __bf_shf(zclk->mask)) & 0x4) ? 2 : 1;
	return Z_CLK_ROUND(prate / div);
}

static long cpg_zg_pll0_clk_round_rate(struct clk_hw *hw,
				       unsigned long rate,
				       unsigned long *parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned long prate = *parent_rate / zclk->fixed_div;
	unsigned int div;

	div = DIV_ROUND_CLOSEST(prate, rate);
	div = clamp(div, 1U, 2U);
	*parent_rate = prate * zclk->fixed_div;

	return Z_CLK_ROUND(prate / div);
}

static int cpg_zg_pll0_clk_set_rate(struct clk_hw *hw,
				    unsigned long rate,
				    unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned long prate = parent_rate / zclk->fixed_div;
	unsigned int div;
	unsigned int i;
	u32 val, kick;

	div = DIV_ROUND_CLOSEST(prate, rate);
	div = clamp(div, 1U, 2U);

	if (clk_readl(zclk->kick_reg) & CPG_FRQCRB_KICK)
		return -EBUSY;

	val = clk_readl(zclk->reg) & ~zclk->mask;
	val |= (((div == 2) ? 0x4 : 0x0) << __bf_shf(zclk->mask)) & zclk->mask;
	clk_writel(val, zclk->reg);

	/*
	 * Set KICK bit in FRQCRB to update hardware setting and wait for
	 * clock change completion.
	 */
	kick = clk_readl(zclk->kick_reg);
	kick |= CPG_FRQCRB_KICK;
	clk_writel(kick, zclk->kick_reg);

	/*
	 * Note: There is no HW information about the worst case latency.
	 *
	 * Using experimental measurements, it seems that no more than
	 * ~10 iterations are needed, independently of the CPU rate.
	 * Since this value might be dependent of external xtal rate, pll0
	 * rate or even the other emulation clocks rate, use 1000 as a
	 * "super" safe value.
	 */
	for (i = 1000; i; i--) {
		if (!(clk_readl(zclk->kick_reg) & CPG_FRQCRB_KICK))
			return 0;

		cpu_relax();
	}

	return -ETIMEDOUT;
}

static const struct clk_ops cpg_zg_pll0_clk_ops = {
	.recalc_rate = cpg_zg_pll0_clk_recalc_rate,
	.round_rate = cpg_zg_pll0_clk_round_rate,
	.set_rate = cpg_zg_pll0_clk_set_rate,
};

static struct clk * __init cpg_zg_pll0_clk_register(const char *name,
						    const char *parent_name,
						    void __iomem *reg,
						    unsigned int div)
{
	struct clk_init_data init;
	struct cpg_z_clk *zclk;
	struct clk *clk;

	zclk = kzalloc(sizeof(*zclk), GFP_KERNEL);
	if (!zclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_zg_pll0_clk_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	zclk->reg = reg + CPG_FRQCRB;
	zclk->kick_reg = reg + CPG_FRQCRB;
	zclk->hw.init = &init;
	zclk->mask = CPG_FRQCRB_ZGFC_MASK;
	zclk->fixed_div = div; /* PLLVCO x 1/div x 3DGE divider */

	clk = clk_register(NULL, &zclk->hw);
	if (IS_ERR(clk)) {
		kfree(zclk);
		return clk;
	}

	zclk->max_rate = clk_hw_get_rate(clk_hw_get_parent(&zclk->hw)) /
			 zclk->fixed_div;
	return clk;
}

static const struct clk_div_table cpg_rpcsrc_div_table[] = {
	{ 2, 5 }, { 3, 6 }, { 0, 0 },
};

static const struct rcar_gen3_cpg_pll_config *cpg_pll_config __initdata;
static unsigned int cpg_clk_extalr __initdata;
static u32 cpg_mode __initdata;

static const struct soc_device_attribute cpg_quirks_match[] __initconst = {
	{
		.soc_id = "r8a7795", .revision = "ES1.0",
		.data = (void *)(PLL_ERRATA | RCKCR_CKSEL | SD_HS400_4TAP),
	},
	{
		.soc_id = "r8a7795", .revision = "ES1.*",
		.data = (void *)(RCKCR_CKSEL | SD_HS400_4TAP),
	},
	{
		.soc_id = "r8a7795", .revision = "ES2.0",
		.data = (void *)SD_HS400_4TAP,
	},
	{
		.soc_id = "r8a7796", .revision = "ES1.0",
		.data = (void *)(RCKCR_CKSEL | SD_HS400_4TAP),
	},
	{
		.soc_id = "r8a7796", .revision = "ES1.*",
		.data = (void *)SD_HS400_4TAP,
	},
	{
		.soc_id = "r8a77990",
		.data = (void *)ZG_PARENT_PLL0,
	},
	{
		.soc_id = "r8a77970",
		.data = (void *)(Z2_SYSCPU_1),
	},
	{
		.soc_id = "r8a77980",
		.data = (void *)(Z2_SYSCPU_2),
	},
	{ /* sentinel */ }
};

struct clk * __init rcar_gen3_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct clk **clks, void __iomem *base,
	struct raw_notifier_head *notifiers)
{
	const struct clk *parent;
	unsigned int mult = 1;
	unsigned int div = 1;
	u32 value;

	parent = clks[core->parent & 0xffff];	/* some types use high bits */
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	switch (core->type) {
	case CLK_TYPE_GEN3_MAIN:
		div = cpg_pll_config->extal_div;
		break;

	case CLK_TYPE_GEN3_PLL0:
		/*
		 * The PLL0 is implemented as customized clock,
		 * it changes the multiplier when cpufreq changes between
		 * normal and override mode.
		 */
		return cpg_pll_clk_register(core->name, __clk_get_name(parent),
				base, CPG_PLL0CR, CPG_PLLECR_PLL0ST);

	case CLK_TYPE_GEN3_PLL1:
		mult = cpg_pll_config->pll1_mult;
		div = cpg_pll_config->pll1_div;
		break;

	case CLK_TYPE_GEN3_PLL2:
		return cpg_pll_clk_register(core->name, __clk_get_name(parent),
				base, CPG_PLL2CR, CPG_PLLECR_PLL2ST);

	case CLK_TYPE_GEN3_PLL3:
		mult = cpg_pll_config->pll3_mult;
		div = cpg_pll_config->pll3_div;
		break;

	case CLK_TYPE_GEN3_PLL4:
		/*
		 * PLL4 is a configurable multiplier clock. Register it as a
		 * fixed factor clock for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		value = readl(base + CPG_PLL4CR);
		mult = (((value >> 24) & 0x7f) + 1) * 2;
		if (cpg_quirks & PLL_ERRATA)
			mult *= 2;
		break;

	case CLK_TYPE_GEN3_SD:
		return cpg_sd_clk_register(core->name, base, core->offset,
					   __clk_get_name(parent), notifiers,
					   cpg_quirks & SD_SKIP_FIRST,
					   cpg_quirks & SD_HS400_4TAP);

	case CLK_TYPE_GEN3_R:
		if (cpg_quirks & RCKCR_CKSEL) {
			struct cpg_simple_notifier *csn;

			csn = kzalloc(sizeof(*csn), GFP_KERNEL);
			if (!csn)
				return ERR_PTR(-ENOMEM);

			csn->reg = base + CPG_RCKCR;

			/*
			 * RINT is default.
			 * Only if EXTALR is populated, we switch to it.
			 */
			value = readl(csn->reg) & 0x3f;

			if (clk_get_rate(clks[cpg_clk_extalr])) {
				parent = clks[cpg_clk_extalr];
				value |= CPG_RCKCR_CKSEL;
			}

			writel(value, csn->reg);
			cpg_simple_notifier_register(notifiers, csn);
			break;
		}

		/* Select parent clock of RCLK by MD28 */
		if (cpg_mode & BIT(28))
			parent = clks[cpg_clk_extalr];
		break;

	case CLK_TYPE_GEN3_MDSEL:
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

	case CLK_TYPE_GEN3_Z:
		return cpg_z_clk_register(core->name, __clk_get_name(parent),
					  base, core->div, core->offset);

	case CLK_TYPE_GEN3_OSC:
		/*
		 * Clock combining OSC EXTAL predivider and a fixed divider
		 */
		div = cpg_pll_config->osc_prediv * core->div;
		break;

	case CLK_TYPE_GEN3_RCKSEL:
		/*
		 * Clock selectable between two parents and two fixed dividers
		 * using RCKCR.CKSEL
		 */
		if (readl(base + CPG_RCKCR) & CPG_RCKCR_CKSEL) {
			div = core->div & 0xffff;
		} else {
			parent = clks[core->parent >> 16];
			if (IS_ERR(parent))
				return ERR_CAST(parent);
			div = core->div >> 16;
		}
		break;

	case CLK_TYPE_GEN3_ZG:
		if (cpg_quirks & ZG_PARENT_PLL0)
			return cpg_zg_pll0_clk_register(core->name,
							__clk_get_name(parent),
							base, core->div);
		return cpg_zg_clk_register(core->name, __clk_get_name(parent),
					   base, core->div, core->offset);

	case CLK_TYPE_GEN3_RPCSRC:
		return clk_register_divider_table(NULL, core->name,
						  __clk_get_name(parent), 0,
						  base + CPG_RPCCKCR, 3, 2, 0,
						  cpg_rpcsrc_div_table,
						  &cpg_lock);

	case CLK_TYPE_GEN3_RPC:
		return cpg_rpc_clk_register(core->name, base + CPG_RPCCKCR,
					    __clk_get_name(parent), notifiers);

	case CLK_TYPE_GEN3_RPCD2:
		return cpg_rpcd2_clk_register(core->name, base + CPG_RPCCKCR,
					      __clk_get_name(parent));

	default:
		return ERR_PTR(-EINVAL);
	}

	return clk_register_fixed_factor(NULL, core->name,
					 __clk_get_name(parent), 0, mult, div);
}

int __init rcar_gen3_cpg_init(const struct rcar_gen3_cpg_pll_config *config,
			      unsigned int clk_extalr, u32 mode)
{
	const struct soc_device_attribute *attr;

	cpg_pll_config = config;
	cpg_clk_extalr = clk_extalr;
	cpg_mode = mode;
	attr = soc_device_match(cpg_quirks_match);
	if (attr)
		cpg_quirks = (uintptr_t)attr->data;
	pr_debug("%s: mode = 0x%x quirks = 0x%x\n", __func__, mode, cpg_quirks);

	spin_lock_init(&cpg_lock);

	return 0;
}
