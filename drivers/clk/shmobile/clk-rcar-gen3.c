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
#define CPG_RCKCR	0x0240
#define CPG_SD0CKCR	0x0074
#define CPG_SD1CKCR	0x0078
#define CPG_SD2CKCR	0x0268
#define CPG_SD3CKCR	0x026c

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

/** Modify for Z-clock
/ -----------------------------------------------------------------------------
 * Z Clock
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

#define GEN3_PRR			0xFFF00044 /* Product register */
#define PRODUCT_ID_MASK		(0x7f << 8) /* R-Car H3: PRODUCT[14:8] bits */
#define RCAR_H3_PRODUCT_ID		(0x4f << 8) /* 0b1001111 */
#define PRODUCT_VERSION_MASK	0xff	/* R-Car H3: CUT[7:0] bits*/
#define PRODUCT_VERSION_WS1_0	0

int check_product_version(u32 product_bits)
{
	static u32 prr_value;
	int ret;
	void __iomem *prr = ioremap_nocache(GEN3_PRR, 4);

	BUG_ON(!prr);
	prr_value = ioread32(prr);
	prr_value &= PRODUCT_ID_MASK | PRODUCT_VERSION_MASK;
	if (product_bits == prr_value)
		ret = 0;
	else if (product_bits < prr_value)
		ret = -1;
	else
		ret = 1;
	iounmap(prr);

	return ret;
}

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

	val = (clk_readl(zclk->reg) & CPG_FRQCRC_ZFC_MASK)
	    >> CPG_FRQCRC_ZFC_SHIFT;
	mult = 32 - val;

	return div_u64((u64)parent_rate * mult, 32);
}

static long cpg_z_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *parent_rate)
{
	unsigned long prate  = *parent_rate;
	unsigned int mult;

	if (!prate)
		prate = 1;

	mult = div_u64((u64)rate * 32, prate);
	mult = clamp(mult, 1U, 32U);

	return *parent_rate / 32 * mult;
}

static int cpg_z_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct cpg_z_clk *zclk = to_z_clk(hw);
	unsigned int mult;
	u32 val, kick;
	unsigned int i;

	mult = div_u64((u64)rate * 32, parent_rate);
	mult = clamp(mult, 1U, 32U);

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

static const struct clk_ops cpg_z_clk_ops = {
	.recalc_rate = cpg_z_clk_recalc_rate,
	.round_rate = cpg_z_clk_round_rate,
	.set_rate = cpg_z_clk_set_rate,
};

static struct clk * __init cpg_z_clk_register(struct rcar_gen3_cpg *cpg)
{
	static const char *parent_name = "pll0";
	struct clk_init_data init;
	struct cpg_z_clk *zclk;
	struct clk *clk;

	zclk = kzalloc(sizeof(*zclk), GFP_KERNEL);
	if (!zclk)
		return ERR_PTR(-ENOMEM);

	init.name = "z";
	init.ops = &cpg_z_clk_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	zclk->reg = cpg->reg + CPG_FRQCRC;
	zclk->kick_reg = cpg->reg + CPG_FRQCRB;
	zclk->hw.init = &init;

	clk = clk_register(NULL, &zclk->hw);
	if (IS_ERR(clk))
		kfree(zclk);

	return clk;
}
/** End of modifying for Z-clock */

/* -----------------------------------------------------------------------------
 * SDn Clock
 *
 */
#define CPG_SD_STP_N_HCK	BIT(9)
#define CPG_SD_STP_N_CK		BIT(8)
#define CPG_SD_SD_N_SRCFC_MASK	(0x7 << CPG_SD_SD_N_SRCFC_SHIFT)
#define CPG_SD_SD_N_SRCFC_SHIFT	2
#define CPG_SD_SD_N_FC_MASK	(0x3 << CPG_SD_SD_N_FC_SHIFT)
#define CPG_SD_SD_N_FC_SHIFT	0

#define CPG_SD_STP_MASK		(CPG_SD_STP_N_HCK | CPG_SD_STP_N_CK)
#define CPG_SD_FC_MASK		(CPG_SD_SD_N_SRCFC_MASK | CPG_SD_SD_N_FC_MASK)

/* CPG_SD_DIV_TABLE_DATA(stp_n_hck, stp_n_ck, sd_n_srcfc, sd_n_fc, div) */
#define CPG_SD_DIV_TABLE_DATA(_a, _b, _c, _d, _e) \
{ \
	.val = ((_a) ? CPG_SD_STP_N_HCK : 0) | \
	       ((_b) ? CPG_SD_STP_N_CK : 0) | \
	       (((_c) << CPG_SD_SD_N_SRCFC_SHIFT) & CPG_SD_SD_N_SRCFC_MASK) | \
	       (((_d) << CPG_SD_SD_N_FC_SHIFT) & CPG_SD_SD_N_FC_MASK), \
	.div = (_e), \
}

struct sd_div_table {
	u32 val;
	unsigned int div;
};

struct sd_clock {
	struct clk_hw hw;
	void __iomem *reg;
	const struct sd_div_table *div_table;
	int div_num;
	unsigned int div_min;
	unsigned int div_max;
};

/* SDn divider
 *                     sd_n_srcfc sd_n_fc   div
 * stp_n_hck stp_n_ck  (div)      (div)     = sd_n_srcfc x sd_n_fc
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
/*	CPG_SD_DIV_TABLE_DATA(stp_n_hck, stp_n_ck, sd_n_srcfc, sd_n_fc, div) */
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

static int cpg_sd_clock_endisable(struct clk_hw *hw, bool enable)
{
	struct sd_clock *clock = to_sd_clock(hw);
	u32 val, sd_fc;
	int i;

	val = clk_readl(clock->reg);

	if (enable) {
		sd_fc = val & CPG_SD_FC_MASK;
		for (i = 0; i < clock->div_num; i++)
			if (sd_fc == (clock->div_table[i].val & CPG_SD_FC_MASK))
				break;

		if (i >= clock->div_num) {
			pr_err("%s: 0x%4x is not support of division ratio.\n",
				__func__, sd_fc);
			return -ENODATA;
		}

		val &= ~(CPG_SD_STP_MASK);
		val |= clock->div_table[i].val & CPG_SD_STP_MASK;
	} else
		val |= CPG_SD_STP_MASK;

	clk_writel(val, clock->reg);

	return 0;
}

static int cpg_sd_clock_enable(struct clk_hw *hw)
{
	return cpg_sd_clock_endisable(hw, true);
}

static void cpg_sd_clock_disable(struct clk_hw *hw)
{
	cpg_sd_clock_endisable(hw, false);
}

static int cpg_sd_clock_is_enabled(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);

	return !(clk_readl(clock->reg) & CPG_SD_STP_MASK);
}

static unsigned long cpg_sd_clock_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);
	u32 rate = parent_rate;
	u32 val, sd_fc;
	int i;

	val = clk_readl(clock->reg);

	sd_fc = val & CPG_SD_FC_MASK;
	for (i = 0; i < clock->div_num; i++)
		if (sd_fc == (clock->div_table[i].val & CPG_SD_FC_MASK))
			break;

	if (i >= clock->div_num) {
		pr_err("%s: 0x%4x is not support of division ratio.\n",
			__func__, sd_fc);
		return 0;
	}

	do_div(rate, clock->div_table[i].div);

	return rate;
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

	return *parent_rate / div;
}

static int cpg_sd_clock_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);
	unsigned int div = cpg_sd_clock_calc_div(clock, rate, parent_rate);
	u32 val;
	int i;

	for (i = 0; i < clock->div_num; i++)
		if (div == clock->div_table[i].div)
			break;

	if (i >= clock->div_num) {
		pr_err("%s: Not support divider range : div=%d (%lu/%lu).\n",
			__func__, div, parent_rate, rate);
		return -EINVAL;
	}

	val = clk_readl(clock->reg);
	val &= ~(CPG_SD_STP_MASK | CPG_SD_FC_MASK);
	val |= clock->div_table[i].val & (CPG_SD_STP_MASK | CPG_SD_FC_MASK);
	clk_writel(val, clock->reg);

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

static struct clk * __init cpg_sd_clk_register(const char *name,
					       void __iomem *reg,
					       struct device_node *np)
{
	const char *parent_name = of_clk_get_parent_name(np, 1);
	struct clk_init_data init;
	struct sd_clock *clock;
	struct clk *clk;
	int i;

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &cpg_sd_clock_ops;
	init.flags = CLK_IS_BASIC | CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->reg = reg;
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

/* -----------------------------------------------------------------------------
 * RCLK Clock Data
 */
static const struct clk_div_table cpg_rclk_div_table[] = {
			/*                  MD     EXTAL  RCLK  (EXTAL/div)  */
			/* val       div  : 14 13  (MHz)  (KHz)              */
	{ 0x0f, 512  },	/* B'00_1111 512  :  0  0  16.66  32.55 (16666/512)  */
	{ 0x12, 608  },	/* B'01_0010 608  :  0  1  20.00  32.89 (20000/608)  */
	{ 0x17, 768  },	/* B'01_0111 768  :  1  0  25.00  32.55 (25000/768)  */
	{ 0x1f, 1024 },	/* B'01_1111 1024 :  1  1  33.33  32.55 (33333/1024) */
	{ 0, 0 },
};

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
		/* Start clock issue W/A */
		if (!check_product_version(
			RCAR_H3_PRODUCT_ID|PRODUCT_VERSION_WS1_0)) {
			mult *= 2; /* Don't divide PLL0 output for 2 */
		}
		/* End clock issue W/A */
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
	} else if (!strcmp(name, "sd0")) {
		return cpg_sd_clk_register(name, cpg->reg + CPG_SD0CKCR, np);
	} else if (!strcmp(name, "sd1")) {
		return cpg_sd_clk_register(name, cpg->reg + CPG_SD1CKCR, np);
	} else if (!strcmp(name, "sd2")) {
		return cpg_sd_clk_register(name, cpg->reg + CPG_SD2CKCR, np);
	} else if (!strcmp(name, "sd3")) {
		return cpg_sd_clk_register(name, cpg->reg + CPG_SD3CKCR, np);
	} else if (!strcmp(name, "rclk")) {
		parent_name = of_clk_get_parent_name(np, 0);
		return clk_register_divider_table(NULL, name, parent_name, 0,
						  cpg->reg + CPG_RCKCR, 0, 6, 0,
						  cpg_rclk_div_table, NULL);
	} else if (!strcmp(name, "z")) {
		return cpg_z_clk_register(cpg);
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

	cpg_mstp_add_clk_domain(np);
}
CLK_OF_DECLARE(rcar_gen3_cpg_clks, "renesas,rcar-gen3-cpg-clocks",
	       rcar_gen3_cpg_clocks_init);
