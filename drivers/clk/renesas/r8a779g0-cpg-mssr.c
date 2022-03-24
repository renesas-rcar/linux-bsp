// SPDX-License-Identifier: GPL-2.0
/*
 * r8a779g0 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 * Based on r8a7795-cpg-mssr.c
 */

#include <linux/bug.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/r8a779g0-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen4-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A779G0_CLK_RCLK,

	/* External Input Clocks */
	CLK_EXTAL,
	CLK_EXTALR,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL3,
	CLK_PLL4,
	CLK_PLL5,
	CLK_PLL6,
	CLK_PLL1_DIV2,
	CLK_PLL2_DIV2,
	CLK_PLL3_DIV2,
	CLK_PLL4_DIV2,
	CLK_PLL5_DIV2,
	CLK_PLL5_DIV4,
	CLK_PLL6_DIV2,
	CLK_S0,
	CLK_S0_VIO,
	CLK_S0_VC,
	CLK_S0_HSC,
	CLK_SV_VIP,
	CLK_SV_IR,
	CLK_SDSRC,
	CLK_RPCSRC,
	CLK_OCO,

	/* Module Clocks */
	MOD_CLK_BASE
};

static const struct cpg_core_clk r8a779g0_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",	CLK_EXTAL),
	DEF_INPUT("extalr",	CLK_EXTALR),

	/* Internal Core Clocks */
	DEF_BASE(".main",	CLK_MAIN,		CLK_TYPE_GEN4_MAIN, CLK_EXTAL),
	DEF_BASE(".pll1",	CLK_PLL1,		CLK_TYPE_GEN4_PLL1, CLK_MAIN),
	DEF_BASE(".pll3",	CLK_PLL3,		CLK_TYPE_GEN4_PLL3, CLK_MAIN),
	DEF_BASE(".pll2",	CLK_PLL2,		CLK_TYPE_GEN4_PLL2, CLK_MAIN),
	DEF_BASE(".pll6",	CLK_PLL6,		CLK_TYPE_GEN4_PLL6, CLK_MAIN),
	DEF_BASE(".pll5",	CLK_PLL5,		CLK_TYPE_GEN4_PLL5, CLK_MAIN),
	DEF_BASE(".pll4",	CLK_PLL4,		CLK_TYPE_GEN4_PLL4, CLK_MAIN),
	DEF_FIXED(".pll1_div2",	CLK_PLL1_DIV2,		CLK_PLL1,	2, 1),
	DEF_FIXED(".pll2_div2",	CLK_PLL2_DIV2,		CLK_PLL2,	2, 1),
	DEF_FIXED(".pll3_div2",	CLK_PLL3_DIV2,		CLK_PLL3,	2, 1),
	DEF_FIXED(".pll4_div2",	CLK_PLL4_DIV2,		CLK_PLL4,	2, 1),
	DEF_FIXED(".pll5_div2",	CLK_PLL5_DIV2,		CLK_PLL5,	2, 1),
	DEF_FIXED(".pll5_div4",	CLK_PLL5_DIV4,		CLK_PLL5_DIV2,	2, 1),
	DEF_FIXED(".pll6_div2",	CLK_PLL6_DIV2,		CLK_PLL6,	2, 1),
	DEF_FIXED("zt",		R8A779G0_CLK_ZT,	CLK_PLL1_DIV2,	3, 1),
	DEF_FIXED(".s0",	CLK_S0,			CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED(".s0_vio",	CLK_S0_VIO,		CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED(".s0_vc",	CLK_S0_VC,		CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED(".s0_hsc",	CLK_S0_HSC,		CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED(".s0_vip",	CLK_SV_VIP,		CLK_PLL1,	5, 1),
	DEF_FIXED(".s0_ir",	CLK_SV_IR,		CLK_PLL1,	5, 1),
	DEF_FIXED(".sdsrc",	CLK_SDSRC,		CLK_PLL5_DIV2,	2, 1),
	DEF_RATE(".oco",	CLK_OCO,		32768),
	DEF_BASE(".rpcsrc",	CLK_RPCSRC,		CLK_TYPE_GEN4_RPCSRC, CLK_PLL5),
	DEF_BASE(".rpc",	R8A779G0_CLK_RPC,	CLK_TYPE_GEN4_RPC,    CLK_RPCSRC),
	DEF_BASE("rpcd2",	R8A779G0_CLK_RPCD2,	CLK_TYPE_GEN4_RPCD2,  R8A779G0_CLK_RPC),

	/* Core Clock Outputs */
	DEF_FIXED("s0d2",	R8A779G0_CLK_S0D2,	CLK_S0,		2, 1),
	DEF_FIXED("s0d3",	R8A779G0_CLK_S0D3,	CLK_S0,		3, 1),
	DEF_FIXED("s0d4",	R8A779G0_CLK_S0D4,	CLK_S0,		4, 1),
	DEF_FIXED("cl16m",	R8A779G0_CLK_CL16M,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_rt",	R8A779G0_CLK_S0D2_RT,	CLK_S0,		2, 1),
	DEF_FIXED("s0d3_rt",	R8A779G0_CLK_S0D3_RT,	CLK_S0,		3, 1),
	DEF_FIXED("s0d4_rt",	R8A779G0_CLK_S0D4_RT,	CLK_S0,		4, 1),
	DEF_FIXED("s0d6_rt",	R8A779G0_CLK_S0D6_RT,	CLK_S0,		6, 1),
	DEF_FIXED("s0d24_rt",	R8A779G0_CLK_S0D24_RT,	CLK_S0,		24, 1),
	DEF_FIXED("cl16m_rt",	R8A779G0_CLK_CL16M_RT,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_per",	R8A779G0_CLK_S0D2_PER,	CLK_S0,		2, 1),
	DEF_FIXED("s0d3_per",	R8A779G0_CLK_S0D3_PER,	CLK_S0,		3, 1),
	DEF_FIXED("s0d4_per",	R8A779G0_CLK_S0D4_PER,	CLK_S0,		4, 1),
	DEF_FIXED("s0d6_per",	R8A779G0_CLK_S0D6_PER,	CLK_S0,		6, 1),
	DEF_FIXED("s0d24_per",	R8A779G0_CLK_S0D24_PER,	CLK_S0,		24, 1),
	DEF_FIXED("cl16m_per",	R8A779G0_CLK_CL16M_PER,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_mm",	R8A779G0_CLK_S0D2_MM,	CLK_S0,		2, 1),
	DEF_FIXED("s0d4_mm",	R8A779G0_CLK_S0D4_MM,	CLK_S0,		4, 1),
	DEF_FIXED("cl16m_mm",	R8A779G0_CLK_CL16M_MM,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_cc",	R8A779G0_CLK_S0D2_CC,	CLK_S0,		2, 1),
	DEF_FIXED("s0d2_u3dg",	R8A779G0_CLK_S0D2_U3DG,	CLK_S0,		2, 1),
	DEF_FIXED("s0d4_u3dg",	R8A779G0_CLK_S0D4_U3DG,	CLK_S0,		4, 1),
	DEF_FIXED("s0d1_vio",	R8A779G0_CLK_S0D1_VIO,	CLK_S0_VIO,	1, 1),
	DEF_FIXED("s0d2_vio",	R8A779G0_CLK_S0D2_VIO,	CLK_S0_VIO,	2, 1),
	DEF_FIXED("s0d4_vio",	R8A779G0_CLK_S0D4_VIO,	CLK_S0_VIO,	4, 1),
	DEF_FIXED("s0d1_vc",	R8A779G0_CLK_S0D1_VC,	CLK_S0_VC,	1, 1),
	DEF_FIXED("s0d2_vc",	R8A779G0_CLK_S0D2_VC,	CLK_S0_VC,	2, 1),
	DEF_FIXED("s0d4_vc",	R8A779G0_CLK_S0D4_VC,	CLK_S0_VC,	4, 1),
	DEF_FIXED("s0d1_hsc",	R8A779G0_CLK_S0D1_HSC,	CLK_S0_HSC,	1, 1),
	DEF_FIXED("s0d2_hsc",	R8A779G0_CLK_S0D2_HSC,	CLK_S0_HSC,	2, 1),
	DEF_FIXED("s0d4_hsc",	R8A779G0_CLK_S0D4_HSC,	CLK_S0_HSC,	4, 1),
	DEF_FIXED("s0d8_hsc",	R8A779G0_CLK_S0D8_HSC,	CLK_S0_HSC,	8, 1),
	DEF_FIXED("cl16m_hsc",	R8A779G0_CLK_CL16M_HSC,	CLK_S0_HSC,	48, 1),
	DEF_FIXED("svd1_vip",	R8A779G0_CLK_SVD1_VIP,	CLK_SV_VIP,	1, 1),
	DEF_FIXED("svd2_vip",	R8A779G0_CLK_SVD2_VIP,	CLK_SV_VIP,	2, 1),
	DEF_FIXED("svd1_ir",	R8A779G0_CLK_SVD1_IR,	CLK_SV_IR,	1, 1),
	DEF_FIXED("svd2_ir",	R8A779G0_CLK_SVD2_IR,	CLK_SV_IR,	2, 1),
	DEF_FIXED("cbfusa",	R8A779G0_CLK_CBFUSA,	CLK_EXTAL,	2, 1),
	DEF_FIXED("dsiref",	R8A779G0_CLK_DSIREF,	CLK_PLL5_DIV4,	48, 1),
	DEF_FIXED("zg",	R8A779G0_CLK_ZG,	CLK_PLL4_DIV2,	2, 1),
	DEF_GEN4_SD("sd0",	R8A779G0_CLK_SD0,	CLK_SDSRC,	0x870),
	DEF_DIV6P1("mso",	R8A779G0_CLK_MSO,	CLK_PLL5_DIV4,	0x087C),
	DEF_DIV6P1("canfd",	R8A779G0_CLK_CANFD,	CLK_PLL5_DIV4,	0x878),
	DEF_DIV6P1("csi",	R8A779G0_CLK_CSI,	CLK_PLL5_DIV4,	0x0880),
	DEF_DIV6P1("dsiext",	R8A779G0_CLK_DSIEXT,	CLK_PLL5_DIV4,	0x0884),
	DEF_DIV6P1("post",	R8A779G0_CLK_POST,	CLK_PLL5_DIV4,	0x890),
	DEF_DIV6P1("post2",	R8A779G0_CLK_POST2,	CLK_PLL5_DIV4,	0x0894),
	DEF_DIV6P1("post3",	R8A779G0_CLK_POST3,	CLK_PLL5_DIV4,	0x898),
	DEF_DIV6P1("post4",	R8A779G0_CLK_POST4,	CLK_PLL5_DIV4,	0x089C),
	DEF_FIXED("sasyncrt",	R8A779G0_CLK_SASYNCRT,	CLK_PLL5_DIV4,	48, 1),
	DEF_FIXED("sasyncper", R8A779G0_CLK_SASYNCPER, CLK_PLL5_DIV4, 3, 1),
	DEF_FIXED("sasyncperd1", R8A779G0_CLK_SASYNCPERD1, R8A779G0_CLK_SASYNCPER, 1, 1),
	DEF_FIXED("sasyncperd2", R8A779G0_CLK_SASYNCPERD2, R8A779G0_CLK_SASYNCPER, 2, 1),
	DEF_FIXED("sasyncperd4", R8A779G0_CLK_SASYNCPERD4, R8A779G0_CLK_SASYNCPER, 4, 1),
	DEF_FIXED("viobus",	R8A779G0_CLK_VIOBUS,	CLK_PLL5_DIV2,	1, 1),
	DEF_FIXED("viobusd2",	R8A779G0_CLK_VIOBUSD2,	CLK_PLL5_DIV2,	2, 1),
	DEF_FIXED("cpex",	R8A779G0_CLK_CPEX,	CLK_EXTAL,	2, 1),
	DEF_GEN4_OSC("osc",	R8A779G0_CLK_OSCCLK,	CLK_EXTAL,	8),
	DEF_GEN4_MDSEL("r",	R8A779G0_CLK_RCLK, 29, CLK_EXTALR, 1, CLK_OCO, 1),
};

static const struct mssr_mod_clk r8a779g0_mod_clks[] __initconst = {
	DEF_MOD("rgx",			0,	R8A779G0_CLK_ZG),
	DEF_MOD("smpo0",		5,	R8A779G0_CLK_S0D3),
	DEF_MOD("smps0",		7,	R8A779G0_CLK_S0D3),
	DEF_MOD("umfl0",		9,	R8A779G0_CLK_S0D3),
	DEF_MOD("isp0",			16,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("isp1",			17,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("impcnn",		22,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("spmc",			23,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("imp0",			24,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("imp1",			25,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("impdmac0",		26,	R8A779G0_CLK_MSO),
	DEF_MOD("imppsc",		27,	R8A779G0_CLK_MSO),

	DEF_MOD("imp2",			103,	R8A779G0_CLK_S0D3),
	DEF_MOD("imp3",			104,	R8A779G0_CLK_S0D3),
	DEF_MOD("impdmac1",		105,	R8A779G0_CLK_S0D3),
	DEF_MOD("impdta",		116,	R8A779G0_CLK_S0D3),
	DEF_MOD("impslv",		118,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("spmi",			120,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("adg",			122,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("advfs",		123,	R8A779G0_CLK_S0D6_PER),

	DEF_MOD("avb0",			211,	R8A779G0_CLK_S0D8_HSC),
	DEF_MOD("avb1",			212,	R8A779G0_CLK_S0D8_HSC),
	DEF_MOD("avb2",			213,	R8A779G0_CLK_S0D8_HSC),

	DEF_MOD("can-fd",		328,	R8A779G0_CLK_CANFD),
	DEF_MOD("cr0",			329,	R8A779G0_CLK_S0D3),
	DEF_MOD("csdbgpap",		330,	R8A779G0_CLK_S0D3),
	DEF_MOD("csitop0",		331,	R8A779G0_CLK_CSI),

	DEF_MOD("csitop1",		400,	R8A779G0_CLK_CSI),
	DEF_MOD("dis0",			411,	R8A779G0_CLK_S0D3),
	DEF_MOD("doc2ch",		414,	R8A779G0_CLK_S0D3),
	DEF_MOD("dsitxlink0",		415,	R8A779G0_CLK_DSIREF),
	DEF_MOD("dsitxlink1",		416,	R8A779G0_CLK_DSIREF),

	DEF_MOD("fcpcs",		507,	R8A779G0_CLK_S0D3),
	DEF_MOD("fcpvd0",		508,	R8A779G0_CLK_S0D3),
	DEF_MOD("fcpvd1",		509,	R8A779G0_CLK_S0D3),
	DEF_MOD("fray00",		513,	R8A779G0_CLK_S0D3),
	DEF_MOD("hscif0",		514,	R8A779G0_CLK_RCLK),
	DEF_MOD("hscif1",		515,	R8A779G0_CLK_RCLK),
	DEF_MOD("hscif2",		516,	R8A779G0_CLK_RCLK),
	DEF_MOD("hscif3",		517,	R8A779G0_CLK_RCLK),
	DEF_MOD("i2c0",			518,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("i2c1",			519,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("i2c2",			520,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("i2c3",			521,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("i2c4",			522,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("i2c5",			523,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("imr0",			525,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("imr1",			526,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("imr2",			527,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("ims0",			529,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("ims1",			530,	R8A779G0_CLK_S0D6_PER),

	DEF_MOD("ipc",			601,	R8A779G0_CLK_MSO),
	DEF_MOD("ispcs0",		612,	R8A779G0_CLK_MSO),
	DEF_MOD("ispcs1",		613,	R8A779G0_CLK_MSO),
	DEF_MOD("ivcp1e",		616,	R8A779G0_CLK_MSO),
	DEF_MOD("msi0",			618,	R8A779G0_CLK_MSO),
	DEF_MOD("msi1",			619,	R8A779G0_CLK_MSO),
	DEF_MOD("msi2",			620,	R8A779G0_CLK_MSO),
	DEF_MOD("msi3",			621,	R8A779G0_CLK_MSO),
	DEF_MOD("msi4",			622,	R8A779G0_CLK_MSO),
	DEF_MOD("msi5",			623,	R8A779G0_CLK_MSO),
	DEF_MOD("pcie0",		624,	R8A779G0_CLK_S0D2),
	DEF_MOD("pcie1",		625,	R8A779G0_CLK_S0D2),
	DEF_MOD("pwm",			628,	R8A779G0_CLK_RPCD2),
	DEF_MOD("rpc",			629,	R8A779G0_CLK_RPCD2),
	DEF_MOD("rtdm0",		630,	R8A779G0_CLK_S0D4_RT),
	DEF_MOD("rtdm1",		631,	R8A779G0_CLK_S0D4_RT),

	DEF_MOD("rtdm2",		700,	R8A779G0_CLK_S0D4_RT),
	DEF_MOD("rtdm3",		701,	R8A779G0_CLK_S0D4_RT),
	DEF_MOD("scif0",		702,	R8A779G0_CLK_RCLK),
	DEF_MOD("scif1",		703,	R8A779G0_CLK_RCLK),
	DEF_MOD("scif3",		704,	R8A779G0_CLK_RCLK),
	DEF_MOD("scif4",		705,	R8A779G0_CLK_RCLK),
	DEF_MOD("sdhi0",		706,	R8A779G0_CLK_SD0),
	DEF_MOD("secrom",		707,	R8A779G0_CLK_S0D4),
	DEF_MOD("sydm1",		709,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("sydm2",		710,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("system_ram",		711,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("tmu0",			713,	R8A779G0_CLK_SASYNCRT),
	DEF_MOD("tmu1",			714,	R8A779G0_CLK_SASYNCPERD2),
	DEF_MOD("tmu2",			715,	R8A779G0_CLK_SASYNCPERD2),
	DEF_MOD("tmu3",			716,	R8A779G0_CLK_SASYNCPERD2),
	DEF_MOD("tmu4",			717,	R8A779G0_CLK_SASYNCPERD2),
	DEF_MOD("tpu",			718,	R8A779G0_CLK_S0D6_RT),
	DEF_MOD("caiplite_wrapper0",    720,    R8A779G0_CLK_S0D2),
	DEF_MOD("caiplite0",            721,    R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("caiplite1",            722,    R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("caiplite2",            723,    R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("caiplite3",            724,    R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("caiplite4",            725,    R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("caiplite5",            726,    R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("caiplite6",            727,    R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("caiplite7",            728,    R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vcp4l",		729,	R8A779G0_CLK_S0D6_RT),
	DEF_MOD("vin0",			730,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin1",			731,	R8A779G0_CLK_S0D1_VIO),

	DEF_MOD("vin2",			800,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin3",			801,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin4",			802,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin5",			803,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin6",			804,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin7",			805,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin10",		806,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin11",		807,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin12",		808,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin13",		809,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin14",		810,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin15",		811,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin16",		812,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vin17",		813,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vspd0",		830,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vspd1",		831,	R8A779G0_CLK_S0D1_VIO),

	DEF_MOD("wcrc0",		903,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("wcrc1",		904,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("wcrc2",		905,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("wcrc3",		906,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("wdt1:wdt0",		907,	R8A779G0_CLK_RCLK),
	DEF_MOD("cmt0",			910,	R8A779G0_CLK_RCLK),
	DEF_MOD("cmt1",			911,	R8A779G0_CLK_RCLK),
	DEF_MOD("cmt2",			912,	R8A779G0_CLK_RCLK),
	DEF_MOD("cmt3",			913,	R8A779G0_CLK_RCLK),
	DEF_MOD("pfc0",			915,	R8A779G0_CLK_CL16M),
	DEF_MOD("pfc1",			916,	R8A779G0_CLK_CL16M),
	DEF_MOD("pfc2",			917,	R8A779G0_CLK_CL16M),
	DEF_MOD("pfc3",			918,	R8A779G0_CLK_CL16M),
	DEF_MOD("tsc4:tsc3:tsc2:tsc1",	919,	R8A779G0_CLK_CL16M),
	DEF_MOD("ucmt",			920,	R8A779G0_CLK_CL16M),

	DEF_MOD("vspx0",		1028,	R8A779G0_CLK_S0D1_VIO),
	DEF_MOD("vspx1",		1029,	R8A779G0_CLK_S0D1_VIO),

	DEF_MOD("fcpvx0",		1100,	R8A779G0_CLK_S0D2_VC),
	DEF_MOD("fcpvx1",		1101,	R8A779G0_CLK_S0D2_VC),
	DEF_MOD("aurora2",		1106,	R8A779G0_CLK_S0D2_HSC),
	DEF_MOD("aurora4",		1107,	R8A779G0_CLK_S0D2_HSC),

	DEF_MOD("advfsc",		1223,	R8A779G0_CLK_CL16M),
	DEF_MOD("crc0",			1225,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("crc1",			1226,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("crc2",			1227,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("crc3",			1228,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("fso",			1230,	R8A779G0_CLK_S0D2),
	DEF_MOD("kcrc4",		1231,	R8A779G0_CLK_S0D2_RT),

	DEF_MOD("kcrc5",		1300,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("kcrc6",		1301,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("kcrc7",		1302,	R8A779G0_CLK_S0D2_RT),
	DEF_MOD("mti",			1304,	R8A779G0_CLK_S0D1_VIO),

	DEF_MOD("cve0",			2706,	R8A779G0_CLK_SVD1_IR),
	DEF_MOD("cve1",			2707,	R8A779G0_CLK_SVD1_IR),
	DEF_MOD("cve2",			2708,	R8A779G0_CLK_SVD1_IR),
	DEF_MOD("cve3",			2709,	R8A779G0_CLK_SVD1_IR),
	DEF_MOD("impsdmac0",		2712,	R8A779G0_CLK_SVD1_VIP),
	DEF_MOD("impsdmac1",		2713,	R8A779G0_CLK_SVD1_VIP),
	DEF_MOD("tsn",			2723,	R8A779G0_CLK_S0D8_HSC),
	DEF_MOD("csbrg_ir_a3",		2728,	R8A779G0_CLK_ZT),
	DEF_MOD("csbrg_ir_a2",		2729,	R8A779G0_CLK_ZT),

	DEF_MOD("vdsp0_bus",		2801,	R8A779G0_CLK_SVD1_IR), /* T.B.D. */
	DEF_MOD("vdsp1_bus",		2802,	R8A779G0_CLK_SVD1_IR), /* T.B.D. */
	DEF_MOD("vdsp2_bus",		2803,	R8A779G0_CLK_SVD1_IR), /* T.B.D. */
	DEF_MOD("vdsp3_bus",		2804,	R8A779G0_CLK_SVD1_IR), /* T.B.D. */
	DEF_MOD("paptop",               2806,    R8A779G0_CLK_S0D6_PER),
	DEF_MOD("papsdma",              2807,    R8A779G0_CLK_S0D6_PER),
	DEF_MOD("fcprc",		2817,	R8A779G0_CLK_S0D2_MM),
	DEF_MOD("dsc",			2819,	R8A779G0_CLK_VIOBUSD2),
	DEF_MOD("vdsp0_csb",		2821,	R8A779G0_CLK_SVD1_IR), /* T.B.D. */
	DEF_MOD("vdsp1_csb",		2830,	R8A779G0_CLK_SVD1_IR), /* T.B.D. */

	DEF_MOD("vdsp2_csb",		2907,	R8A779G0_CLK_SVD1_IR), /* T.B.D. */
	DEF_MOD("vdsp3_csb",		2916,	R8A779G0_CLK_SVD1_IR), /* T.B.D. */
	DEF_MOD("ssiu",			2926,	R8A779G0_CLK_S0D6_PER),
	DEF_MOD("ssi",			2927,	R8A779G0_CLK_S0D6_PER),

};

/*
 * CPG Clock Data
 */
/*
 *   MD	 EXTAL		PLL1	PLL2	PLL3	PLL4	PLL5	PLL6	OSC
 * 14 13 (MHz)
 * ------------------------------------------------------------------------
 * 0  0	 16.66 / 1	x192	x204	x192	x144	x192	168	/16
 * 0  1	 20.00 / 1	x160	x170	x160	x120	x160	140	/19
 * 1  0	 Prohibited setting
 * 1  1	 33.33 / 2	x192	x204	x192	x144	x192	168	/32
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 13) | \
					 (((md) & BIT(13)) >> 13))

static const struct rcar_gen4_cpg_pll_config cpg_pll_configs[4] = {
	/* EXTAL div	PLL1 mult/div	PLL2 mult/div	PLL3 mult/div	PLL4 mult/div	PLL5 mult/div	PLL6 mult/div	OSC prediv */
	{ 1,		192,	1,	204,	1,	192,	1,	144,	1,	192,	1,	168,	1,	16,	},
	{ 1,		160,	1,	170,	1,	160,	1,	120,	1,	160,	1,	140,	1,	19,	},
	{ 0,		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	},
	{ 2,		192,	1,	204,	1,	192,	1,	144,	1,	192,	1,	168,	1,	32,	},
};

static int __init r8a779g0_cpg_mssr_init(struct device *dev)
{
	const struct rcar_gen4_cpg_pll_config *cpg_pll_config;
	u32 cpg_mode;
	int error;

	error = rcar_rst_read_mode_pins(&cpg_mode);
	if (error)
		return error;

	cpg_pll_config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];
	if (!cpg_pll_config->extal_div) {
		dev_err(dev, "Prohibited setting (cpg_mode=0x%x)\n", cpg_mode);
		return -EINVAL;
	}

	return rcar_gen4_cpg_init(cpg_pll_config, CLK_EXTALR, cpg_mode);
}

const struct cpg_mssr_info r8a779g0_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a779g0_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a779g0_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a779g0_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a779g0_mod_clks),
	.num_hw_mod_clks = 30 * 32,

	/* Callbacks */
	.init = r8a779g0_cpg_mssr_init,
	.cpg_clk_register = rcar_gen4_cpg_clk_register,

	.reg_layout = CLK_REG_LAYOUT_RCAR_GEN4,
};
