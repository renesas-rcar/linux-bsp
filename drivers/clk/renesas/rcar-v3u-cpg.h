/* SPDX-License-Identifier: GPL-2.0 */
/*
 * R-Car r8a779a0 Clock Pulse Generator
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 */

#ifndef __CLK_RENESAS_RCAR_R8A779A0_CPG_H__
#define __CLK_RENESAS_RCAR_R8A779A0_CPG_H__

enum rcar_R8A779A0_clk_types {
	CLK_TYPE_R8A779A0_MAIN = CLK_TYPE_CUSTOM,
	CLK_TYPE_R8A779A0_PLL1,
	CLK_TYPE_R8A779A0_PLL20,
	CLK_TYPE_R8A779A0_PLL21,
	CLK_TYPE_R8A779A0_PLL30,
	CLK_TYPE_R8A779A0_PLL31,
	CLK_TYPE_R8A779A0_PLL4,
	CLK_TYPE_R8A779A0_PLL5,
	CLK_TYPE_R8A779A0_SD,
	CLK_TYPE_R8A779A0_R,
	CLK_TYPE_R8A779A0_MDSEL,	/* Select parent/divider using mode pin */
	CLK_TYPE_R8A779A0_Z,
	CLK_TYPE_R8A779A0_ZG,
	CLK_TYPE_R8A779A0_OSC,	/* OSC EXTAL predivider and fixed divider */
	CLK_TYPE_R8A779A0_RCKSEL,	/* Select parent/divider using RCKCR.CKSEL */
	CLK_TYPE_R8A779A0_RPCSRC,
	CLK_TYPE_R8A779A0_RPC,
	CLK_TYPE_R8A779A0_RPCD2,

	/* SoC specific definitions start here */
	CLK_TYPE_R8A779A0_SOC_BASE,
};

#define DEF_R8A779A0_SD(_name, _id, _parent, _offset)	\
	DEF_BASE(_name, _id, CLK_TYPE_R8A779A0_SD, _parent, .offset = _offset)

#define DEF_R8A779A0_MDSEL(_name, _id, _md, _parent0, _div0, _parent1, _div1) \
	DEF_BASE(_name, _id, CLK_TYPE_R8A779A0_MDSEL,	\
		 (_parent0) << 16 | (_parent1),		\
		 .div = (_div0) << 16 | (_div1), .offset = _md)

#define DEF_R8A779A0_PE(_name, _id, _parent_clean, _div_clean, _parent_sscg, \
		    _div_sscg) \
	DEF_R8A779A0_MDSEL(_name, _id, 12, _parent_clean, _div_clean,	\
		       _parent_sscg, _div_sscg)

#define DEF_R8A779A0_OSC(_name, _id, _parent, _div)		\
	DEF_BASE(_name, _id, CLK_TYPE_R8A779A0_OSC, _parent, .div = _div)

#define DEF_R8A779A0_RCKSEL(_name, _id, _parent0, _div0, _parent1, _div1) \
	DEF_BASE(_name, _id, CLK_TYPE_R8A779A0_RCKSEL,	\
		 (_parent0) << 16 | (_parent1),	.div = (_div0) << 16 | (_div1))

#define DEF_R8A779A0_Z(_name, _id, _type, _parent, _div, _offset)	\
	DEF_BASE(_name, _id, _type, _parent, .div = _div, .offset = _offset)

struct rcar_r8a779a0_cpg_pll_config {
	u8 extal_div;
	u8 pll1_mult;
	u8 pll1_div;
	u8 pll5_mult;
	u8 pll5_div;
	u8 osc_prediv;
};

#define CPG_RPCCKCR	0x874

struct clk *rcar_r8a779a0_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct clk **clks, void __iomem *base,
	struct raw_notifier_head *notifiers);
int rcar_r8a779a0_cpg_init(const struct rcar_r8a779a0_cpg_pll_config *config,
		       unsigned int clk_extalr, u32 mode);

#endif
