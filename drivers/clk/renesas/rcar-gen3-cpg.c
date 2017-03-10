/*
 * R-Car Gen3 Clock Pulse Generator
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
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
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/soc/renesas/rcar-rst.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen3-cpg.h"

static const struct soc_device_attribute r8a7795es10[] = {
	{ .soc_id = "r8a7795", .revision = "ES1.0" },
	{ /* sentinel */ }
};

static const struct soc_device_attribute r8a7795es11[] = {
	{ .soc_id = "r8a7795", .revision = "ES1.1" },
	{ /* sentinel */ }
};

static const struct soc_device_attribute r8a7796es10[] = {
	{ .soc_id = "r8a7796", .revision = "ES1.0" },
	{ /* sentinel */ }
};

#define CPG_PLL0CR		0x00d8
#define CPG_PLL2CR		0x002c
#define CPG_PLL4CR		0x01f4

/* Implementation for customized clocks (Z-clk, Z2-clk, PLL0-clk) for CPUFreq */
#define CPG_PLLECR     0x00D0
#define CPG_PLLECR_PLL0ST (1 << 8)


/* Define for PLL0 clk driver */
#define CPG_PLL0CR_STC_MASK             0x7f000000
#define CPG_PLL0CR_STC_SHIFT            24

#ifdef CONFIG_RCAR_Z_CLK_MAX_THRESHOLD
#define Z_CLK_MAX_THRESHOLD     CONFIG_RCAR_Z_CLK_MAX_THRESHOLD
#else
#define Z_CLK_MAX_THRESHOLD             1500000000
#endif

struct cpg_pll0_clk {
	struct clk_hw hw;
	void __iomem *reg;
	void __iomem *pllecr_reg;
};

#define to_pll0_clk(_hw)   container_of(_hw, struct cpg_pll0_clk, hw)

static int cpg_pll0_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long prate)
{
	struct cpg_pll0_clk *pll0_clk = to_pll0_clk(hw);
	unsigned int stc_val;
	u32 val;
	int i;

	/* Start clock issue W/A (for H3 WS1.0) */
	if (soc_device_match(r8a7795es10))
		prate *= 2; /* PLL0 output multiplied by 2 */
	/* End clock issue W/A */

	stc_val = DIV_ROUND_CLOSEST(rate, prate);
	stc_val = clamp(stc_val, 90U, 120U);/*Lowest value is 1.5G (stc == 90)*/
	pr_debug("%s(): prate: %lu, rate: %lu, pll0-mult: %d\n",
		__func__, prate, rate, stc_val);

	stc_val -= 1;
	val = clk_readl(pll0_clk->reg);
	val &= ~CPG_PLL0CR_STC_MASK;
	val |= stc_val << CPG_PLL0CR_STC_SHIFT;
	clk_writel(val, pll0_clk->reg);

	i = 0;
	while (!(clk_readl(pll0_clk->pllecr_reg) & CPG_PLLECR_PLL0ST)) {
		cpu_relax();
		i++;
	}

	if (i > 1000)
		pr_warn("%s(): PLL0: long settled time: %d\n", __func__, i);

	return 0;
}

static long cpg_pll0_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	unsigned long prate = *parent_rate;
	unsigned int mult;

	if (rate < Z_CLK_MAX_THRESHOLD)
		rate = Z_CLK_MAX_THRESHOLD; /* Set lowest value: 1.5GHz */

	/* Start clock issue W/A (for H3 WS1.0) */
	if (soc_device_match(r8a7795es10))
		prate *= 2; /* PLL0 output multiplied by 2 */
	/* End clock issue W/A */

	mult = DIV_ROUND_CLOSEST(rate, prate);
	mult = clamp(mult, 90U, 120U); /* 1.5G => (stc == 90)*/

	rate = prate * mult;

	/* Round to closest value at 100MHz unit */
	rate = 100000000 * DIV_ROUND_CLOSEST(rate, 100000000);
	pr_debug("%s(): output rate: %lu, parent_rate: %lu, pll0-mult: %d\n",
		__func__, rate, prate, mult);
	return rate;
}

static unsigned long cpg_pll0_clk_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct cpg_pll0_clk *pll0_clk = to_pll0_clk(hw);
	unsigned int val;
	unsigned long rate;

	val = (clk_readl(pll0_clk->reg) & CPG_PLL0CR_STC_MASK)
		>> CPG_PLL0CR_STC_SHIFT;

	rate = (u64)parent_rate * (val + 1);

	/* Start clock issue W/A (for H3 WS1.0) */
	if (soc_device_match(r8a7795es10))
		rate *= 2; /* PLL0 output multiplied by 2 */
	/* End clock issue W/A */

	/* Round to closest value at 100MHz unit */
	rate = 100000000 * DIV_ROUND_CLOSEST(rate, 100000000);
	return rate;
}

static const struct clk_ops cpg_pll0_clk_ops = {
	.recalc_rate = cpg_pll0_clk_recalc_rate,
	.round_rate = cpg_pll0_clk_round_rate,
	.set_rate = cpg_pll0_clk_set_rate,
};

static struct clk * __init cpg_pll0_clk_register(const char *name,
				const char *parent_name,
				void __iomem *cpg_base)
{
	struct clk_init_data init;
	struct cpg_pll0_clk *pll0_clk;
	struct clk *clk;

	pll0_clk = kzalloc(sizeof(*pll0_clk), GFP_KERNEL);
	if (!pll0_clk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_pll0_clk_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll0_clk->reg = cpg_base + CPG_PLL0CR;
	pll0_clk->pllecr_reg = cpg_base + CPG_PLLECR;
	pll0_clk->hw.init = &init;

	clk = clk_register(NULL, &pll0_clk->hw);
	if (IS_ERR(clk))
		kfree(pll0_clk);

	return clk;
}

/* Modify for Z-clock and Z2-clock
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is adjustable.  clk->rate = parent->rate * mult / 32
 * parent - fixed parent.  No clk_set_parent support
 */
#define CPG_FRQCRB			0x00000004
#define CPG_FRQCRB_KICK			BIT(31)
#define CPG_FRQCRC			0x000000e0
#define CPG_FRQCRC_ZFC_MASK		(0x1f << 8)
#define CPG_FRQCRC_ZFC_SHIFT		8
#define CPG_FRQCRC_Z2FC_MASK		0x1f


struct cpg_z_clk {
	struct clk_hw hw;
	void __iomem *reg;
	void __iomem *kick_reg;
};

#define to_z_clk(_hw)	container_of(_hw, struct cpg_z_clk, hw)

static unsigned long cpg_z_clk_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned int mult;
	unsigned int val;
	unsigned long rate;

	val = (clk_readl(zclk->reg) & CPG_FRQCRC_ZFC_MASK)
	    >> CPG_FRQCRC_ZFC_SHIFT;
	mult = 32 - val;

	rate = div_u64((u64)parent_rate * mult + 16, 32);
	/* Round to closest value at 100MHz unit */
	rate = 100000000*DIV_ROUND_CLOSEST(rate, 100000000);
	return rate;
}

static unsigned long cpg_z2_clk_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned int mult;
	unsigned int val;
	unsigned long rate;

	val = (clk_readl(zclk->reg) & CPG_FRQCRC_Z2FC_MASK);
	mult = 32 - val;

	rate = div_u64((u64)parent_rate * mult + 16, 32);
	/* Round to closest value at 100MHz unit */
	rate = 100000000*DIV_ROUND_CLOSEST(rate, 100000000);
	return rate;
}

static long cpg_z_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *parent_rate)
{
	unsigned long prate  = *parent_rate;
	unsigned int mult;

	if (!prate)
		prate = 1;

	if (rate <= Z_CLK_MAX_THRESHOLD) { /* Focus on changing z-clock */
		prate = Z_CLK_MAX_THRESHOLD; /* Set parent to: 1.5GHz */
		mult = div_u64((u64)rate * 32 + prate/2, prate);
	} else {
		/* Focus on changing parent. Fix z-clock divider is 32/32 */
		mult = 32;
	}

	mult = clamp(mult, 1U, 32U);

	/* Re-calculate the parent_rate to propagate new rate for it */
	prate = div_u64((u64)rate * 32 + mult/2, mult);
	prate = 100000000 * DIV_ROUND_CLOSEST(prate, 100000000);
	rate = 100000000 * DIV_ROUND_CLOSEST(prate / 32 * mult, 100000000);
	pr_debug("%s():z-clk mult:%d, re-calculated prate:%lu, return: %lu\n",
		__func__, mult, prate, rate);
	*parent_rate = prate;

	return rate;
}

static long cpg_z2_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	unsigned long prate  = *parent_rate;
	unsigned int mult;

	mult = div_u64((u64)rate * 32 + prate/2, prate);
	mult = clamp(mult, 1U, 32U);

	return prate / 32 * mult;
}

static int cpg_z_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned int mult;
	u32 val, kick;
	unsigned int i;

	if (rate <= Z_CLK_MAX_THRESHOLD) { /* Focus on changing z-clock */
		parent_rate = Z_CLK_MAX_THRESHOLD; /* Set parent to: 1.5GHz */
		mult = div_u64((u64)rate * 32 + parent_rate/2, parent_rate);
	} else {
		mult = 32;
	}
	mult = clamp(mult, 1U, 32U);

	pr_debug("%s(): rate: %lu, set prate to: %lu, z-clk mult: %d\n",
		__func__, rate, parent_rate, mult);
	if (clk_readl(zclk->kick_reg) & CPG_FRQCRB_KICK)
		return -EBUSY;

	val = clk_readl(zclk->reg);
	val &= ~CPG_FRQCRC_ZFC_MASK;
	val |= (32 - mult) << CPG_FRQCRC_ZFC_SHIFT;
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
	 * Since this value might be dependent of external xtal rate, pll1
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

static int cpg_z2_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned int mult;
	u32 val, kick;
	unsigned int i;

	mult = div_u64((u64)rate * 32 + parent_rate/2, parent_rate);
	mult = clamp(mult, 1U, 32U);

	if (clk_readl(zclk->kick_reg) & CPG_FRQCRB_KICK)
		return -EBUSY;

	val = clk_readl(zclk->reg);
	val &= ~CPG_FRQCRC_Z2FC_MASK;
	val |= 32 - mult;
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
	 * Since this value might be dependent of external xtal rate, pll1
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

static const struct clk_ops cpg_z_clk_ops = {
	.recalc_rate = cpg_z_clk_recalc_rate,
	.round_rate = cpg_z_clk_round_rate,
	.set_rate = cpg_z_clk_set_rate,
};

static const struct clk_ops cpg_z2_clk_ops = {
	.recalc_rate = cpg_z2_clk_recalc_rate,
	.round_rate = cpg_z2_clk_round_rate,
	.set_rate = cpg_z2_clk_set_rate,
};

static struct clk * __init cpg_z_clk_register(const char *name,
					      const char *parent_name,
					      void __iomem *reg)
{
	struct clk_init_data init;
	struct cpg_z_clk *zclk;
	struct clk *clk;

	zclk = kzalloc(sizeof(*zclk), GFP_KERNEL);
	if (!zclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_z_clk_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	zclk->reg = reg + CPG_FRQCRC;
	zclk->kick_reg = reg + CPG_FRQCRB;
	zclk->hw.init = &init;

	clk = clk_register(NULL, &zclk->hw);
	if (IS_ERR(clk))
		kfree(zclk);

	return clk;
}

static struct clk * __init cpg_z2_clk_register(const char *name,
					      const char *parent_name,
					      void __iomem *reg)
{
	struct clk_init_data init;
	struct cpg_z_clk *zclk;
	struct clk *clk;

	zclk = kzalloc(sizeof(*zclk), GFP_KERNEL);
	if (!zclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_z2_clk_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	zclk->reg = reg + CPG_FRQCRC;
	zclk->kick_reg = reg + CPG_FRQCRB;
	zclk->hw.init = &init;

	clk = clk_register(NULL, &zclk->hw);
	if (IS_ERR(clk))
		kfree(zclk);

	return clk;
}
/** End of modifying for Z-clock, Z2-clock and PLL0-clock */

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
	void __iomem *reg;
	const struct sd_div_table *div_table;
	unsigned int div_num;
	unsigned int div_min;
	unsigned int div_max;
};

/* SDn divider
 *                     sd_srcfc   sd_fc   div
 * stp_hck   stp_ck    (div)      (div)     = sd_srcfc x sd_fc
 *-------------------------------------------------------------------
 *  0         0         1 (2)      0 (-)      2 : HS400
 *  0         0         0 (1)      1 (4)      4 : SDR104 / HS200
 *  0         0         1 (2)      1 (4)      8 : SDR50
 *  1         0         2 (4)      1 (4)     16 : HS / SDR25
 *  1         0         3 (8)      1 (4)     32 : NS / SDR12
 *  0         0         0 (1)      0 (2)      2 : (no case)
 *  1         0         2 (4)      0 (2)      8 : (no case)
 *  1         0         3 (8)      0 (2)     16 : (no case)
 *  1         0         4 (16)     0 (2)     32 : (no case)
 *  1         0         4 (16)     1 (4)     64 : (no case)
 */
static const struct sd_div_table cpg_sd_div_table[] = {
/*	CPG_SD_DIV_TABLE_DATA(stp_hck,  stp_ck,   sd_srcfc,   sd_fc,  sd_div) */
	CPG_SD_DIV_TABLE_DATA(0,	0,	  1,	      0,        2),
	CPG_SD_DIV_TABLE_DATA(0,	0,	  0,	      1,        4),
	CPG_SD_DIV_TABLE_DATA(0,	0,	  1,	      1,        8),
	CPG_SD_DIV_TABLE_DATA(1,	0,	  2,	      1,       16),
	CPG_SD_DIV_TABLE_DATA(1,	0,	  3,	      1,       32),
	CPG_SD_DIV_TABLE_DATA(0,	0,	  0,	      0,        2),
	CPG_SD_DIV_TABLE_DATA(1,	0,	  2,	      0,        8),
	CPG_SD_DIV_TABLE_DATA(1,	0,	  3,	      0,       16),
	CPG_SD_DIV_TABLE_DATA(1,	0,	  4,	      0,       32),
	CPG_SD_DIV_TABLE_DATA(1,	0,	  4,	      1,       64),
};

#define to_sd_clock(_hw) container_of(_hw, struct sd_clock, hw)

static int cpg_sd_clock_enable(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);
	u32 val, sd_fc;
	unsigned int i;

	val = readl(clock->reg);

	sd_fc = val & CPG_SD_FC_MASK;
	for (i = 0; i < clock->div_num; i++)
		if (sd_fc == (clock->div_table[i].val & CPG_SD_FC_MASK))
			break;

	if (i >= clock->div_num)
		return -EINVAL;

	val &= ~(CPG_SD_STP_MASK);
	val |= clock->div_table[i].val & CPG_SD_STP_MASK;

	writel(val, clock->reg);

	return 0;
}

static void cpg_sd_clock_disable(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);

	writel(readl(clock->reg) | CPG_SD_STP_MASK, clock->reg);
}

static int cpg_sd_clock_is_enabled(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);

	return !(readl(clock->reg) & CPG_SD_STP_MASK);
}

static unsigned long cpg_sd_clock_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);
	unsigned long rate = parent_rate;
	u32 val, sd_fc;
	unsigned int i;

	val = readl(clock->reg);

	sd_fc = val & CPG_SD_FC_MASK;
	for (i = 0; i < clock->div_num; i++)
		if (sd_fc == (clock->div_table[i].val & CPG_SD_FC_MASK))
			break;

	if (i >= clock->div_num)
		return -EINVAL;

	return DIV_ROUND_CLOSEST(rate, clock->div_table[i].div);
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

	val = readl(clock->reg);
	val &= ~(CPG_SD_STP_MASK | CPG_SD_FC_MASK);
	val |= clock->div_table[i].val & (CPG_SD_STP_MASK | CPG_SD_FC_MASK);
	writel(val, clock->reg);

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
					       void __iomem *base,
					       const char *parent_name)
{
	struct clk_init_data init;
	struct sd_clock *clock;
	struct clk *clk;
	unsigned int i;

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return ERR_PTR(-ENOMEM);

	init.name = core->name;
	init.ops = &cpg_sd_clock_ops;
	init.flags = CLK_IS_BASIC | CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->reg = base + core->offset;
	clock->hw.init = &init;
	clock->div_table = cpg_sd_div_table;
	clock->div_num = ARRAY_SIZE(cpg_sd_div_table);

	clock->div_max = clock->div_table[0].div;
	clock->div_min = clock->div_max;
	for (i = 1; i < clock->div_num; i++) {
		clock->div_max = max(clock->div_max, clock->div_table[i].div);
		clock->div_min = min(clock->div_min, clock->div_table[i].div);
	}

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk))
		kfree(clock);

	return clk;
}


static const struct rcar_gen3_cpg_pll_config *cpg_pll_config __initdata;
static unsigned int cpg_clk_extalr __initdata;

struct clk * __init rcar_gen3_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct clk **clks, void __iomem *base)
{
	const struct clk *parent;
	unsigned int mult = 1;
	unsigned int div = 1;
	u32 value;
	u32 cpg_mode;
	int error;

	parent = clks[core->parent];
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

		return cpg_pll0_clk_register(core->name,
				__clk_get_name(parent), base);

	case CLK_TYPE_GEN3_PLL1:
		mult = cpg_pll_config->pll1_mult;
		break;

	case CLK_TYPE_GEN3_PLL2:
		/*
		 * PLL2 is a configurable multiplier clock. Register it as a
		 * fixed factor clock for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		value = readl(base + CPG_PLL2CR);
		mult = ((value >> 24) & 0x7f) + 1;
		/* Start clock issue W/A (for H3 WS1.0) */
		if (soc_device_match(r8a7795es10))
			mult *= 2; /* PLL0 output multiplied by 2 */
		/* End clock issue W/A */
		break;

	case CLK_TYPE_GEN3_PLL3:
		mult = cpg_pll_config->pll3_mult;
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
		/* Start clock issue W/A (for H3 WS1.0) */
		if (soc_device_match(r8a7795es10))
			mult *= 2; /* PLL0 output multiplied by 2 */
		/* End clock issue W/A */
		break;

	case CLK_TYPE_GEN3_SD:
		return cpg_sd_clk_register(core, base, __clk_get_name(parent));

	case CLK_TYPE_GEN3_R:
		if (soc_device_match(r8a7795es10) ||
		    soc_device_match(r8a7795es11) ||
		    soc_device_match(r8a7796es10)) {
			/*
			 * RINT is default.
			 * Only if EXTALR is populated, we switch to it.
			 */
			value = readl(base + CPG_RCKCR) & 0x3f;

			if (clk_get_rate(clks[cpg_clk_extalr])) {
				parent = clks[cpg_clk_extalr];
				value |= BIT(15);
			}

			writel(value, base + CPG_RCKCR);
		} else {

			error = rcar_rst_read_mode_pins(&cpg_mode);
			if (error)
				return ERR_PTR(error);

			/* Select parent clock of RCLK by MD28 */
			if (cpg_mode & BIT(28))
				parent = clks[cpg_clk_extalr];
		}
		break;

	case CLK_TYPE_GEN3_Z:
		return cpg_z_clk_register(core->name, __clk_get_name(parent),
					  base);

	case CLK_TYPE_GEN3_Z2:
		return cpg_z2_clk_register(core->name, __clk_get_name(parent),
					   base);

	default:
		return ERR_PTR(-EINVAL);
	}

	return clk_register_fixed_factor(NULL, core->name,
					 __clk_get_name(parent), 0, mult, div);
}

int __init rcar_gen3_cpg_init(const struct rcar_gen3_cpg_pll_config *config,
			      unsigned int clk_extalr)
{
	cpg_pll_config = config;
	cpg_clk_extalr = clk_extalr;
	return 0;
}
