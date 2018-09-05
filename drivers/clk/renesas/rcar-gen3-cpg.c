/*
 * R-Car Gen3 Clock Pulse Generator
 *
 * Copyright (C) 2015-2016 Glider bvba
 *
 * Based on clk-rcar-gen3.c
 *
 * Copyright (C) 2015 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
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
#include "rcar-gen3-cpg.h"

#define CPG_PLLECR		0x00d0
#define CPG_PLL0CR		0x00d8
#define CPG_PLL2CR		0x002c
#define CPG_PLL4CR		0x01f4

#define CPG_PLLECR_PLL0ST	BIT(8)
#define CPG_PLLECR_PLL2ST	BIT(10)
#define CPG_PLLCR_STC_MASK	GENMASK(30, 24) /* Bits in PLL0/2/4 CR */

static u32 cpg_quirks;

#define PLL_ERRATA		BIT(0)	/* Missing PLL0/2/4 post-divider */
#define RCLK_CKSEL_RESEVED	BIT(1)	/* Resverd RCLK clock soruce select */

struct cpg_simple_notifier {
	struct notifier_block nb;
	void __iomem *reg;
	u32 saved;
};

static int cpg_simple_notifier_call(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	struct cpg_simple_notifier *csn =
		container_of(nb, struct cpg_simple_notifier, nb);

	switch (action) {
	case PM_EVENT_SUSPEND:
		csn->saved = readl(csn->reg);
		return NOTIFY_OK;

	case PM_EVENT_RESUME:
		writel(csn->saved, csn->reg);
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static void cpg_simple_notifier_register(struct raw_notifier_head *notifiers,
					 struct cpg_simple_notifier *csn)
{
	csn->nb.notifier_call = cpg_simple_notifier_call;
	raw_notifier_chain_register(notifiers, &csn->nb);
}

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
 * Z Clock & Z2 Clock & ZG Clock
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is adjustable.  clk->rate = (parent->rate * mult / 32 ) / 2
 * parent - fixed parent.  No clk_set_parent support
 */

#define CPG_FRQCRB			0x00000004
#define CPG_FRQCRB_KICK			BIT(31)
#define CPG_FRQCRB_ZGFC_MASK		GENMASK(28, 24)
#define CPG_FRQCRC			0x000000e0
#define CPG_FRQCRC_ZFC_MASK		GENMASK(12, 8)
#define CPG_FRQCRC_Z2FC_MASK		GENMASK(4, 0)

#define Z_CLK_ROUND(f)	(100000000 * DIV_ROUND_CLOSEST_ULL((f), 100000000))

struct cpg_z_clk {
	struct clk_hw hw;
	void __iomem *reg;
	void __iomem *kick_reg;
	unsigned int fixed_div;
	unsigned long mask;
	unsigned long max_freq;
};

#define to_z_clk(_hw)	container_of(_hw, struct cpg_z_clk, hw)

static unsigned long cpg_z_clk_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned long prate = parent_rate / zclk->fixed_div;
	unsigned int mult;
	u32 val;

	val = readl(zclk->reg) & zclk->mask;
	mult = 32 - (val >> __ffs(zclk->mask));

	return Z_CLK_ROUND(prate * mult / 32);
}

static long cpg_z_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned long prate = *parent_rate / zclk->fixed_div;
	unsigned int mult;

	if (!zclk->max_freq) {
		/* Set parent as aargment */
		mult = div_u64((u64)rate * 32, prate);
	} else if (rate <= zclk->max_freq) {
		prate = zclk->max_freq; /* Set parent as init value */
		mult = div_u64((u64)rate * 32, prate);
	} else {
		/* Focus on changing parent. Fix z-clock divider is 32/32 */
		prate = rate;
		mult = 32;
	}
	mult = clamp(mult, 1U, 32U);
	*parent_rate = prate * zclk->fixed_div;

	return Z_CLK_ROUND(prate * mult / 32);
}

static int cpg_z_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned long prate = parent_rate / zclk->fixed_div;
	unsigned int mult;
	unsigned int i;
	u32 val, kick;

	if (!zclk->max_freq)
		mult = DIV_ROUND_CLOSEST_ULL(rate * 32ULL, prate);
	else if (rate <= zclk->max_freq)
		mult = DIV_ROUND_CLOSEST_ULL(rate * 32ULL, zclk->max_freq);
	else
		mult = 32;

	mult = clamp(mult, 1U, 32U);

	if (readl(zclk->kick_reg) & CPG_FRQCRB_KICK)
		return -EBUSY;

	val = readl(zclk->reg) & ~zclk->mask;
	val |= ((32 - mult) << __ffs(zclk->mask)) & zclk->mask;
	writel(val, zclk->reg);

	/*
	 * Set KICK bit in FRQCRB to update hardware setting and wait for
	 * clock change completion.
	 */
	kick = readl(zclk->kick_reg);
	kick |= CPG_FRQCRB_KICK;
	writel(kick, zclk->kick_reg);

	/*
	 * Note: There is no HW information about the worst case latency.
	 *
	 * Using experimental measurements, it seems that no more than
	 * ~10 iterations are needed, independently of the CPU rate.
	 * Since this value might be dependent of external xtal rate, pll1
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
	.round_rate = cpg_z_clk_round_rate,
	.set_rate = cpg_z_clk_set_rate,
};

static struct clk * __init cpg_z_clk_register(const char *name,
					      const char *parent_name,
					      void __iomem *reg,
					      unsigned long mask,
					      unsigned int div)
{
	struct clk_init_data init;
	struct cpg_z_clk *zclk;
	struct clk *clk, *parent;

	zclk = kzalloc(sizeof(*zclk), GFP_KERNEL);
	if (!zclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_z_clk_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_PARENT;

	zclk->reg = reg + CPG_FRQCRC;
	zclk->kick_reg = reg + CPG_FRQCRB;
	zclk->hw.init = &init;
	zclk->mask = mask;
	zclk->fixed_div = div; /* PLLVCO x 1/div x SYS-CPU divider */
	zclk->max_freq = 1;

	clk = clk_register(NULL, &zclk->hw);
	if (IS_ERR(clk)) {
		kfree(zclk);
	} else {
		parent = clk_get_parent(clk);
		zclk->max_freq = clk_get_rate(parent) / zclk->fixed_div;
	}

	return clk;
}

static struct clk * __init cpg_zg_clk_register(const char *name,
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
	init.ops = &cpg_z_clk_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	zclk->reg = reg + CPG_FRQCRB;
	zclk->kick_reg = reg + CPG_FRQCRB;
	zclk->hw.init = &init;
	zclk->mask = CPG_FRQCRB_ZGFC_MASK;
	zclk->fixed_div = div; /* PLLVCO x 1/div1 x 3DGE divider x 1/div2 */

	clk = clk_register(NULL, &zclk->hw);
	if (IS_ERR(clk))
		kfree(zclk);

	return clk;
}

/*
 * SDn Clock
 */
#define CPG_SD_STP_HCK		BIT(9)
#define CPG_SD_STP_CK		BIT(8)

#define CPG_SD_STP_MASK		(CPG_SD_STP_HCK | CPG_SD_STP_CK)
#define CPG_SD_FC_MASK		(0x7 << 2 | 0x3 << 0)

#define CPG_SD_DIV_TABLE_DATA(stp_hck, stp_ck, sd_srcfc, sd_fc, sd_div) \
{ \
	.val = ((stp_hck) ? CPG_SD_STP_HCK : 0) | \
	       ((stp_ck) ? CPG_SD_STP_CK : 0) | \
	       ((sd_srcfc) << 2) | \
	       ((sd_fc) << 0), \
	.div = (sd_div), \
}

struct sd_div_table {
	u32 val;
	unsigned int div;
};

struct sd_clock {
	struct clk_hw hw;
	const struct sd_div_table *div_table;
	struct cpg_simple_notifier csn;
	unsigned int div_num;
	unsigned int div_min;
	unsigned int div_max;
	unsigned int cur_div_idx;
};

/* SDn divider
 *                     sd_srcfc   sd_fc   div
 * stp_hck   stp_ck    (div)      (div)     = sd_srcfc x sd_fc
 *-------------------------------------------------------------------
 *  0         0         0 (1)      1 (4)      4
 *  0         0         1 (2)      1 (4)      8
 *  1         0         2 (4)      1 (4)     16
 *  1         0         3 (8)      1 (4)     32
 *  1         0         4 (16)     1 (4)     64
 *  0         0         0 (1)      0 (2)      2
 *  0         0         1 (2)      0 (2)      4
 *  1         0         2 (4)      0 (2)      8
 *  1         0         3 (8)      0 (2)     16
 *  1         0         4 (16)     0 (2)     32
 */
static const struct sd_div_table cpg_sd_div_table[] = {
/*	CPG_SD_DIV_TABLE_DATA(stp_hck,  stp_ck,   sd_srcfc,   sd_fc,  sd_div) */
	CPG_SD_DIV_TABLE_DATA(0,        0,        0,          1,        4),
	CPG_SD_DIV_TABLE_DATA(0,        0,        1,          1,        8),
	CPG_SD_DIV_TABLE_DATA(1,        0,        2,          1,       16),
	CPG_SD_DIV_TABLE_DATA(1,        0,        3,          1,       32),
	CPG_SD_DIV_TABLE_DATA(1,        0,        4,          1,       64),
	CPG_SD_DIV_TABLE_DATA(0,        0,        0,          0,        2),
	CPG_SD_DIV_TABLE_DATA(0,        0,        1,          0,        4),
	CPG_SD_DIV_TABLE_DATA(1,        0,        2,          0,        8),
	CPG_SD_DIV_TABLE_DATA(1,        0,        3,          0,       16),
	CPG_SD_DIV_TABLE_DATA(1,        0,        4,          0,       32),
};

#define to_sd_clock(_hw) container_of(_hw, struct sd_clock, hw)

static int cpg_sd_clock_enable(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);
	u32 val = readl(clock->csn.reg);

	val &= ~(CPG_SD_STP_MASK);
	val |= clock->div_table[clock->cur_div_idx].val & CPG_SD_STP_MASK;

	writel(val, clock->csn.reg);

	return 0;
}

static void cpg_sd_clock_disable(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);

	writel(readl(clock->csn.reg) | CPG_SD_STP_MASK, clock->csn.reg);
}

static int cpg_sd_clock_is_enabled(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);

	return !(readl(clock->csn.reg) & CPG_SD_STP_MASK);
}

static unsigned long cpg_sd_clock_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);

	return DIV_ROUND_CLOSEST(parent_rate,
				 clock->div_table[clock->cur_div_idx].div);
}

static unsigned int cpg_sd_clock_calc_div(struct sd_clock *clock,
					  unsigned long rate,
					  unsigned long parent_rate)
{
	unsigned int div;

	if (!rate)
		rate = 1;

	div = DIV_ROUND_CLOSEST(parent_rate, rate);

	return clamp_t(unsigned int, div, clock->div_min, clock->div_max);
}

static long cpg_sd_clock_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);
	unsigned int div = cpg_sd_clock_calc_div(clock, rate, *parent_rate);

	return DIV_ROUND_CLOSEST(*parent_rate, div);
}

static int cpg_sd_clock_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);
	unsigned int div = cpg_sd_clock_calc_div(clock, rate, parent_rate);
	u32 val;
	unsigned int i;

	for (i = 0; i < clock->div_num; i++)
		if (div == clock->div_table[i].div)
			break;

	if (i >= clock->div_num)
		return -EINVAL;

	clock->cur_div_idx = i;

	val = readl(clock->csn.reg);
	val &= ~(CPG_SD_STP_MASK | CPG_SD_FC_MASK);
	val |= clock->div_table[i].val & (CPG_SD_STP_MASK | CPG_SD_FC_MASK);
	writel(val, clock->csn.reg);

	return 0;
}

static const struct clk_ops cpg_sd_clock_ops = {
	.enable = cpg_sd_clock_enable,
	.disable = cpg_sd_clock_disable,
	.is_enabled = cpg_sd_clock_is_enabled,
	.recalc_rate = cpg_sd_clock_recalc_rate,
	.round_rate = cpg_sd_clock_round_rate,
	.set_rate = cpg_sd_clock_set_rate,
};

static struct clk * __init cpg_sd_clk_register(const struct cpg_core_clk *core,
	void __iomem *base, const char *parent_name,
	struct raw_notifier_head *notifiers)
{
	struct clk_init_data init;
	struct sd_clock *clock;
	struct clk *clk;
	unsigned int i;
	u32 sd_fc;

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return ERR_PTR(-ENOMEM);

	init.name = core->name;
	init.ops = &cpg_sd_clock_ops;
	init.flags = CLK_IS_BASIC | CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->csn.reg = base + core->offset;
	clock->hw.init = &init;
	clock->div_table = cpg_sd_div_table;
	clock->div_num = ARRAY_SIZE(cpg_sd_div_table);

	sd_fc = readl(clock->csn.reg) & CPG_SD_FC_MASK;
	for (i = 0; i < clock->div_num; i++)
		if (sd_fc == (clock->div_table[i].val & CPG_SD_FC_MASK))
			break;

	if (WARN_ON(i >= clock->div_num)) {
		kfree(clock);
		return ERR_PTR(-EINVAL);
	}

	clock->cur_div_idx = i;

	clock->div_max = clock->div_table[0].div;
	clock->div_min = clock->div_max;
	for (i = 1; i < clock->div_num; i++) {
		clock->div_max = max(clock->div_max, clock->div_table[i].div);
		clock->div_min = min(clock->div_min, clock->div_table[i].div);
	}

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk))
		goto free_clock;

	cpg_simple_notifier_register(notifiers, &clock->csn);
	return clk;

free_clock:
	kfree(clock);
	return clk;
}

static const struct rcar_gen3_cpg_pll_config *cpg_pll_config __initdata;
static unsigned int cpg_clk_extalr __initdata;
static u32 cpg_mode __initdata;

static const struct soc_device_attribute cpg_quirks_match[] __initconst = {
	{
		.soc_id = "r8a7795", .revision = "ES1.0",
		.data = (void *)(PLL_ERRATA | RCLK_CKSEL_RESEVED),
	},
	{
		.soc_id = "r8a7795", .revision = "ES1.*",
		.data = (void *)RCLK_CKSEL_RESEVED,
	},
	{
		.soc_id = "r8a7796", .revision = "ES1.0",
		.data = (void *)RCLK_CKSEL_RESEVED,
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

	parent = clks[core->parent & 0xffff];	/* CLK_TYPE_PE uses high bits */
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
		return cpg_sd_clk_register(core, base, __clk_get_name(parent),
					   notifiers);

	case CLK_TYPE_GEN3_RINT:
		div = cpg_pll_config->rint;
		break;

	case CLK_TYPE_GEN3_OSC:
		div = cpg_pll_config->osc;
		break;

	case CLK_TYPE_GEN3_R:
		/*
		 * SoC not compatible with RCLK clock source selection by MD pin
		 * is internal RCLK fixed.
		 */
		if (cpg_quirks & RCLK_CKSEL_RESEVED)
			break;

		/* Select parent clock of RCLK by MD28 */
		if (cpg_mode & BIT(28))
			parent = clks[cpg_clk_extalr];
		break;

	case CLK_TYPE_GEN3_PE:
		/*
		 * Peripheral clock with a fixed divider, selectable between
		 * clean and spread spectrum parents using MD12
		 */
		if (cpg_mode & BIT(12)) {
			/* Clean */
			div = core->div & 0xffff;
		} else {
			/* SCCG */
			parent = clks[core->parent >> 16];
			if (IS_ERR(parent))
				return ERR_CAST(parent);
			div = core->div >> 16;
		}
		mult = 1;
		break;

	case CLK_TYPE_GEN3_Z:
		return cpg_z_clk_register(core->name, __clk_get_name(parent),
					  base, CPG_FRQCRC_ZFC_MASK, core->div);

	case CLK_TYPE_GEN3_Z2:
		return cpg_z_clk_register(core->name, __clk_get_name(parent),
					  base, CPG_FRQCRC_Z2FC_MASK,
					  core->div);

	case CLK_TYPE_GEN3_ZG:
		return cpg_zg_clk_register(core->name, __clk_get_name(parent),
					   base, core->div);

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
	return 0;
}
