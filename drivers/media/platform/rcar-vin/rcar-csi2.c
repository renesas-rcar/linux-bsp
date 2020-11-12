// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Renesas R-Car MIPI CSI-2 Receiver
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/sys_soc.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

struct rcar_csi2;

/* Register offsets and bits */

/* Control Timing Select */
#define TREF_REG			0x00
#define TREF_TREF			BIT(0)

/* Software Reset */
#define SRST_REG			0x04
#define SRST_SRST			BIT(0)

/* PHY Operation Control */
#define PHYCNT_REG			0x08
#define PHYCNT_SHUTDOWNZ		BIT(17)
#define PHYCNT_RSTZ			BIT(16)
#define PHYCNT_ENABLECLK		BIT(4)
#define PHYCNT_ENABLE_3			BIT(3)
#define PHYCNT_ENABLE_2			BIT(2)
#define PHYCNT_ENABLE_1			BIT(1)
#define PHYCNT_ENABLE_0			BIT(0)

/* Checksum Control */
#define CHKSUM_REG			0x0c
#define CHKSUM_ECC_EN			BIT(1)
#define CHKSUM_CRC_EN			BIT(0)

/*
 * Channel Data Type Select
 * VCDT[0-15]:  Channel 0 VCDT[16-31]:  Channel 1
 * VCDT2[0-15]: Channel 2 VCDT2[16-31]: Channel 3
 */
#define VCDT_REG			0x10
#define VCDT2_REG			0x14
#define VCDT_VCDTN_EN			BIT(15)
#define VCDT_SEL_VC(n)			(((n) & 0x3) << 8)
#define VCDT_SEL_DTN_ON			BIT(6)
#define VCDT_SEL_DT(n)			(((n) & 0x3f) << 0)

/* Frame Data Type Select */
#define FRDT_REG			0x18

/* Field Detection Control */
#define FLD_REG				0x1c
#define FLD_FLD_NUM(n)			(((n) & 0xff) << 16)
#define FLD_DET_SEL(n)			(((n) & 0x3) << 4)
#define FLD_FLD_EN4			BIT(3)
#define FLD_FLD_EN3			BIT(2)
#define FLD_FLD_EN2			BIT(1)
#define FLD_FLD_EN			BIT(0)

/* Automatic Standby Control */
#define ASTBY_REG			0x20

/* Long Data Type Setting 0 */
#define LNGDT0_REG			0x28

/* Long Data Type Setting 1 */
#define LNGDT1_REG			0x2c

/* Interrupt Enable */
#define INTEN_REG			0x30
#define INTEN_INT_AFIFO_OF		BIT(27)
#define INTEN_INT_ERRSOTHS		BIT(4)
#define INTEN_INT_ERRSOTSYNCHS		BIT(3)

/* Interrupt Source Mask */
#define INTCLOSE_REG			0x34

/* Interrupt Status Monitor */
#define INTSTATE_REG			0x38
#define INTSTATE_INT_ULPS_START		BIT(7)
#define INTSTATE_INT_ULPS_END		BIT(6)

/* Interrupt Error Status Monitor */
#define INTERRSTATE_REG			0x3c

/* Short Packet Data */
#define SHPDAT_REG			0x40

/* Short Packet Count */
#define SHPCNT_REG			0x44

/* LINK Operation Control */
#define LINKCNT_REG			0x48
#define LINKCNT_MONITOR_EN		BIT(31)
#define LINKCNT_REG_MONI_PACT_EN	BIT(25)
#define LINKCNT_ICLK_NONSTOP		BIT(24)

/* Lane Swap */
#define LSWAP_REG			0x4c
#define LSWAP_L3SEL(n)			(((n) & 0x3) << 6)
#define LSWAP_L2SEL(n)			(((n) & 0x3) << 4)
#define LSWAP_L1SEL(n)			(((n) & 0x3) << 2)
#define LSWAP_L0SEL(n)			(((n) & 0x3) << 0)

/* PHY Test Interface Write Register */
#define PHTW_REG			0x50
#define PHTW_DWEN			BIT(24)
#define PHTW_TESTDIN_DATA(n)		(((n & 0xff)) << 16)
#define PHTW_CWEN			BIT(8)
#define PHTW_TESTDIN_CODE(n)		((n & 0xff))

/* V4H Registers */
#define N_LANES			0x0004

#define CSI2_RESETN		0x0008

#define PHY_SHUTDOWNZ	0x0040

#define PHY_MODE		0x001c

#define DPHY_RSTZ		0x0044

#define FLDC			0x0804

#define FLDD			0x0808

#define IDIC			0x0810

#define PHY_EN			0x2000

#define ST_PHYST		0x2814
#define ST_PHY_READY	BIT(31)
#define ST_STOPSTATE_DCK	BIT(7)
#define ST_STOPSTATE_3	BIT(3)
#define ST_STOPSTATE_2	BIT(2)
#define ST_STOPSTATE_1	BIT(1)
#define ST_STOPSTATE_0	BIT(0)

/* V4H PPI registers */
#define PPI_STARTUP_RW_COMMON_DPHY(n)		(0x21800 + (n *2 ))	/* n = 0 - 9 */
#define PPI_STARTUP_RW_COMMON_STARTUP_1_1	0x21822
#define PPI_CALIBCTRL_RW_COMMON_BG_0		0x2184C
#define PPI_RW_LPDCOCAL_TIMEBASE			0x21C02
#define PPI_RW_LPDCOCAL_NREF				0x21C04
#define PPI_RW_LPDCOCAL_NREF_RANGE			0x21C06
#define PPI_RW_LPDCOCAL_TWAIT_CONFIG		0x21C0A
#define PPI_RW_LPDCOCAL_VT_CONFIG			0x21C0C
#define PPI_RW_LPDCOCAL_COARSE_CFG			0x21C10
#define PPI_RW_DDLCAL_CFG(n)				(0x21C40 + (n * 2))	/* n = 0 - 7 */
#define PPI_RW_COMMON_CFG					0x21C6C
#define PPI_RW_TERMCAL_CFG_0				0x21C80
#define PPI_RW_OFFSETCAL_CFG_0				0x21CA0

/* V4H CORE registers */
#define CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2(n)	(0x22040 + (n * 2))	/* n = 0 - 15 */
#define CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2(n)	(0x22440 + (n * 2))	/* n = 0 - 15 */
#define CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2(n)	(0x22840 + (n * 2))	/* n = 0 - 15 */
#define CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2(n)	(0x22C40 + (n * 2))	/* n = 0 - 15 */
#define CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2(n)	(0x23040 + (n * 2))	/* n = 0 - 15 */

#define CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2(n)		(0x23840 + (n * 2))	/* n = 0 - 11 */

#define CORE_DIG_RW_COMMON(n)					(0x23880 + (n * 2))	/* n = 0 - 15 */

#define CORE_DIG_ANACTRL_RW_COMMON_ANACTRL(n)	(0x239E0 + (n * 2))	/* n = 0 - 3 */

#define CORE_DIG_COMMON_RW_DESKEW_FINE_MEM		0x23FE0

#define CORE_DIG_CLANE_1_RW_CFG_0				0x2A400

#define CORE_DIG_CLANE_1_RW_HS_TX_6				0x2A60C

#define CORE_DIG_DLANE_0_RW_CFG(n)				(0x26000 + (n * 2))	/* n = 0 - 2 */

#define CORE_DIG_DLANE_0_RW_LP(n)				(0x26080 + (n * 2))	/* n = 0 - 1 */

#define CORE_DIG_DLANE_0_RW_HS_RX(n)			(0x26100 + (n * 2))	/* n = 0 - 8 */

#define CORE_DIG_DLANE_1_RW_CFG(n)				(0x26400 + (n * 2))	/* n = 0 - 2 */

#define CORE_DIG_DLANE_1_RW_LP(n)				(0x26480 + (n * 2))	/* n = 0 - 1 */

#define CORE_DIG_DLANE_1_RW_HS_RX(n)			(0x26500 + (n * 2))	/* n = 0 - 8 */

#define CORE_DIG_DLANE_2_RW_CFG(n)				(0x26800 + (n * 2))	/* n = 0 - 2 */

#define CORE_DIG_DLANE_2_RW_LP(n)				(0x268A0 + (n * 2))	/* n = 0 - 1 */

#define CORE_DIG_DLANE_2_RW_HS_RX(n)			(0x26900 + (n * 2))	/* n = 0 - 8 */

#define CORE_DIG_DLANE_3_RW_CFG(n)				(0x26C00 + (n * 2))	/* n = 0 - 2 */

#define CORE_DIG_DLANE_3_RW_LP(n)				(0x26CA0 + (n * 2))	/* n = 0 - 1 */

#define CORE_DIG_DLANE_3_RW_HS_RX(n)			(0x26D00 + (n * 2))	/* n = 0 - 8 */

#define CORE_DIG_DLANE_CLK_RW_CFG(n)			(0x27000 + (n * 2))	/* n = 0 - 2 */

#define CORE_DIG_DLANE_CLK_RW_LP(n)				(0x27080 + (n * 2))	/* n = 0 - 1 */

#define CORE_DIG_DLANE_CLK_RW_HS_RX(n)			(0x27100 + (n * 2))	/* n = 0 - 8 */

/* C-PHY */
#define CORE_DIG_RW_TRIO0(n)					(0x22100 + (n * 2))
#define CORE_DIG_RW_TRIO1(n)					(0x22500 + (n * 2))
#define CORE_DIG_RW_TRIO2(n)					(0x22900 + (n * 2))

#define CORE_DIG_CLANE_0_RW_LP_0				0x2A080
#define CORE_DIG_CLANE_0_RW_HS_RX(n)			(0x2A100 + (n * 2)) /* n = 0 ~ 6 */

#define CORE_DIG_CLANE_1_RW_LP_0				0x2A480
#define CORE_DIG_CLANE_1_RW_HS_RX(n)			(0x2A500 + (n * 2)) /* n = 0 ~ 6 */

#define CORE_DIG_CLANE_2_RW_LP_0				0x2A880
#define CORE_DIG_CLANE_2_RW_HS_RX(n)			(0x2A900 + (n * 2)) /* n = 0 ~ 6 */

#define RCAR_CSI2_R8A779G0_FEATURE	BIT(0)

#define CSI1300		1

#define CSI2_CPHY_SETTING(ms, rx2, t0, t1, t2, a29, a27) \
	.msps = (ms), \
	.rw_hs_rx_2 = (rx2), \
	.rw_trio_0 = (t0), \
	.rw_trio_1 = (t1), \
	.rw_trio_2 = (t2), \
	.afe_lane0_29 = (a29), \
	.afe_lane0_27 = (a27)

struct rcsi2_cphy_setting {
	u16 msps;
	u16 rw_hs_rx_2;
	u16 rw_trio_0;
	u16 rw_trio_1;
	u16 rw_trio_2;
	u16 afe_lane0_29;
	u16 afe_lane0_27;
};

static const struct rcsi2_cphy_setting cphy_setting_table_r8a779g0[] = {
	{ CSI2_CPHY_SETTING(  80,0x38, 0x024a, 0x0134, 0x6a, 0x0a24, 0x0000) },
	{ CSI2_CPHY_SETTING( 100,0x38, 0x024a, 0x00f5, 0x55, 0x0a24, 0x0000) },
	{ CSI2_CPHY_SETTING( 200,0x38, 0x024a, 0x0077, 0x2b, 0x0a44, 0x0000) },
	{ CSI2_CPHY_SETTING( 300,0x38, 0x024a, 0x004d, 0x1d, 0x0a44, 0x0000) },
	{ CSI2_CPHY_SETTING( 400,0x38, 0x024a, 0x0038, 0x16, 0x0a64, 0x0000) },
	{ CSI2_CPHY_SETTING( 500,0x38, 0x024a, 0x002b, 0x12, 0x0a64, 0x0000) },
	{ CSI2_CPHY_SETTING( 600,0x38, 0x024a, 0x0023, 0x0f, 0x0a64, 0x0000) },
	{ CSI2_CPHY_SETTING( 700,0x38, 0x024a, 0x001d, 0x0d, 0x0a84, 0x0000) },
	{ CSI2_CPHY_SETTING( 800,0x38, 0x024a, 0x0018, 0x0c, 0x0a84, 0x0000) },
	{ CSI2_CPHY_SETTING( 900,0x38, 0x024a, 0x0015, 0x0b, 0x0a84, 0x0000) },
	{ CSI2_CPHY_SETTING(1000,0x3e, 0x024a, 0x0012, 0x0a, 0x0a84, 0x0400) },
	{ CSI2_CPHY_SETTING(1100,0x44, 0x024a, 0x000f, 0x09, 0x0a84, 0x0800) },
	{ CSI2_CPHY_SETTING(1200,0x4a, 0x024a, 0x000e, 0x08, 0x0a84, 0x0c00) },
	{ CSI2_CPHY_SETTING(1300,0x51, 0x024a, 0x000c, 0x08, 0x0aa4, 0x0c00) },
	{ CSI2_CPHY_SETTING(1400,0x57, 0x024a, 0x000b, 0x07, 0x0aa4, 0x1000) },
	{ CSI2_CPHY_SETTING(1500,0x5d, 0x044a, 0x0009, 0x07, 0x0aa4, 0x1000) },
	{ CSI2_CPHY_SETTING(1600,0x63, 0x044a, 0x0008, 0x07, 0x0aa4, 0x1400) },
	{ CSI2_CPHY_SETTING(1700,0x6a, 0x044a, 0x0007, 0x06, 0x0aa4, 0x1400) },
	{ CSI2_CPHY_SETTING(1800,0x70, 0x044a, 0x0007, 0x06, 0x0aa4, 0x1400) },
	{ CSI2_CPHY_SETTING(1900,0x76, 0x044a, 0x0006, 0x06, 0x0aa4, 0x1400) },
	{ CSI2_CPHY_SETTING(2000,0x7c, 0x044a, 0x0005, 0x06, 0x0aa4, 0x1800) },
	{ CSI2_CPHY_SETTING(2100,0x83, 0x044a, 0x0005, 0x05, 0x0aa4, 0x1800) },
	{ CSI2_CPHY_SETTING(2200,0x89, 0x064a, 0x0004, 0x05, 0x0aa4, 0x1800) },
	{ CSI2_CPHY_SETTING(2300,0x8f, 0x064a, 0x0003, 0x05, 0x0aa4, 0x1800) },
	{ CSI2_CPHY_SETTING(2400,0x95, 0x064a, 0x0003, 0x05, 0x0aa4, 0x1800) },
	{ CSI2_CPHY_SETTING(2500,0x9c, 0x064a, 0x0003, 0x05, 0x0aa4, 0x1c00) },
	{ CSI2_CPHY_SETTING(2600,0xa2, 0x064a, 0x0002, 0x05, 0x0ad4, 0x1c00) },
	{ CSI2_CPHY_SETTING(2700,0xa8, 0x064a, 0x0002, 0x05, 0x0ad4, 0x1c00) },
	{ CSI2_CPHY_SETTING(2800,0xae, 0x064a, 0x0002, 0x04, 0x0ad4, 0x1c00) },
	{ CSI2_CPHY_SETTING(2900,0xb5, 0x084a, 0x0001, 0x04, 0x0ad4, 0x1c00) },
	{ CSI2_CPHY_SETTING(3000,0xbb, 0x084a, 0x0001, 0x04, 0x0ad4, 0x1c00) },
	{ CSI2_CPHY_SETTING(3100,0xc1, 0x084a, 0x0001, 0x04, 0x0ad4, 0x1c00) },
	{ CSI2_CPHY_SETTING(3200,0xc7, 0x084a, 0x0001, 0x04, 0x0ad4, 0x1c00) },
	{ CSI2_CPHY_SETTING(3300,0xce, 0x084a, 0x0001, 0x04, 0x0ad4, 0x1c00) },
	{ CSI2_CPHY_SETTING(3400,0xd4, 0x084a, 0x0001, 0x04, 0x0ad4, 0x1c00) },
	{ CSI2_CPHY_SETTING(3500,0xda, 0x084a, 0x0001, 0x04, 0x0ad4, 0x1c00) },
	{ /* sentinel */ },
};

#define PHYFRX_REG			0x64
#define PHYFRX_FORCERX_MODE_3		BIT(3)
#define PHYFRX_FORCERX_MODE_2		BIT(2)
#define PHYFRX_FORCERX_MODE_1		BIT(1)
#define PHYFRX_FORCERX_MODE_0		BIT(0)

struct phtw_value {
	u16 data;
	u16 code;
};

struct rcsi2_mbps_reg {
	u16 mbps;
	u16 reg;
};

static const struct rcsi2_mbps_reg phtw_mbps_v3u[] = {
	{ .mbps = 1500, .reg = 0xcc },
	{ .mbps = 1550, .reg = 0x1d },
	{ .mbps = 1600, .reg = 0x27 },
	{ .mbps = 1650, .reg = 0x30 },
	{ .mbps = 1700, .reg = 0x39 },
	{ .mbps = 1750, .reg = 0x42 },
	{ .mbps = 1800, .reg = 0x4b },
	{ .mbps = 1850, .reg = 0x55 },
	{ .mbps = 1900, .reg = 0x5e },
	{ .mbps = 1950, .reg = 0x67 },
	{ .mbps = 2000, .reg = 0x71 },
	{ .mbps = 2050, .reg = 0x79 },
	{ .mbps = 2100, .reg = 0x83 },
	{ .mbps = 2150, .reg = 0x8c },
	{ .mbps = 2200, .reg = 0x95 },
	{ .mbps = 2250, .reg = 0x9e },
	{ .mbps = 2300, .reg = 0xa7 },
	{ .mbps = 2350, .reg = 0xb0 },
	{ .mbps = 2400, .reg = 0xba },
	{ .mbps = 2450, .reg = 0xc3 },
	{ .mbps = 2500, .reg = 0xcc },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_reg phtw_mbps_h3_v3h_m3n[] = {
	{ .mbps =   80, .reg = 0x86 },
	{ .mbps =   90, .reg = 0x86 },
	{ .mbps =  100, .reg = 0x87 },
	{ .mbps =  110, .reg = 0x87 },
	{ .mbps =  120, .reg = 0x88 },
	{ .mbps =  130, .reg = 0x88 },
	{ .mbps =  140, .reg = 0x89 },
	{ .mbps =  150, .reg = 0x89 },
	{ .mbps =  160, .reg = 0x8a },
	{ .mbps =  170, .reg = 0x8a },
	{ .mbps =  180, .reg = 0x8b },
	{ .mbps =  190, .reg = 0x8b },
	{ .mbps =  205, .reg = 0x8c },
	{ .mbps =  220, .reg = 0x8d },
	{ .mbps =  235, .reg = 0x8e },
	{ .mbps =  250, .reg = 0x8e },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_reg phtw_mbps_v3m_e3[] = {
	{ .mbps =   80, .reg = 0x00 },
	{ .mbps =   90, .reg = 0x20 },
	{ .mbps =  100, .reg = 0x40 },
	{ .mbps =  110, .reg = 0x02 },
	{ .mbps =  130, .reg = 0x22 },
	{ .mbps =  140, .reg = 0x42 },
	{ .mbps =  150, .reg = 0x04 },
	{ .mbps =  170, .reg = 0x24 },
	{ .mbps =  180, .reg = 0x44 },
	{ .mbps =  200, .reg = 0x06 },
	{ .mbps =  220, .reg = 0x26 },
	{ .mbps =  240, .reg = 0x46 },
	{ .mbps =  250, .reg = 0x08 },
	{ .mbps =  270, .reg = 0x28 },
	{ .mbps =  300, .reg = 0x0a },
	{ .mbps =  330, .reg = 0x2a },
	{ .mbps =  360, .reg = 0x4a },
	{ .mbps =  400, .reg = 0x0c },
	{ .mbps =  450, .reg = 0x2c },
	{ .mbps =  500, .reg = 0x0e },
	{ .mbps =  550, .reg = 0x2e },
	{ .mbps =  600, .reg = 0x10 },
	{ .mbps =  650, .reg = 0x30 },
	{ .mbps =  700, .reg = 0x12 },
	{ .mbps =  750, .reg = 0x32 },
	{ .mbps =  800, .reg = 0x52 },
	{ .mbps =  850, .reg = 0x72 },
	{ .mbps =  900, .reg = 0x14 },
	{ .mbps =  950, .reg = 0x34 },
	{ .mbps = 1000, .reg = 0x54 },
	{ .mbps = 1050, .reg = 0x74 },
	{ .mbps = 1125, .reg = 0x16 },
	{ /* sentinel */ },
};

/* PHY Test Interface Clear */
#define PHTC_REG			0x58
#define PHTC_TESTCLR			BIT(0)

/* PHY Frequency Control */
#define PHYPLL_REG			0x68
#define PHYPLL_HSFREQRANGE(n)		((n) << 16)

static const struct rcsi2_mbps_reg hsfreqrange_v3u[] = {
	{ .mbps =   80, .reg = 0x00 },
	{ .mbps =   90, .reg = 0x10 },
	{ .mbps =  100, .reg = 0x20 },
	{ .mbps =  110, .reg = 0x30 },
	{ .mbps =  120, .reg = 0x01 },
	{ .mbps =  130, .reg = 0x11 },
	{ .mbps =  140, .reg = 0x21 },
	{ .mbps =  150, .reg = 0x31 },
	{ .mbps =  160, .reg = 0x02 },
	{ .mbps =  170, .reg = 0x12 },
	{ .mbps =  180, .reg = 0x22 },
	{ .mbps =  190, .reg = 0x32 },
	{ .mbps =  205, .reg = 0x03 },
	{ .mbps =  220, .reg = 0x13 },
	{ .mbps =  235, .reg = 0x23 },
	{ .mbps =  250, .reg = 0x33 },
	{ .mbps =  275, .reg = 0x04 },
	{ .mbps =  300, .reg = 0x14 },
	{ .mbps =  325, .reg = 0x25 },
	{ .mbps =  350, .reg = 0x35 },
	{ .mbps =  400, .reg = 0x05 },
	{ .mbps =  450, .reg = 0x16 },
	{ .mbps =  500, .reg = 0x26 },
	{ .mbps =  550, .reg = 0x37 },
	{ .mbps =  600, .reg = 0x07 },
	{ .mbps =  650, .reg = 0x18 },
	{ .mbps =  700, .reg = 0x28 },
	{ .mbps =  750, .reg = 0x39 },
	{ .mbps =  800, .reg = 0x09 },
	{ .mbps =  850, .reg = 0x19 },
	{ .mbps =  900, .reg = 0x29 },
	{ .mbps =  950, .reg = 0x3a },
	{ .mbps = 1000, .reg = 0x0a },
	{ .mbps = 1050, .reg = 0x1a },
	{ .mbps = 1100, .reg = 0x2a },
	{ .mbps = 1150, .reg = 0x3b },
	{ .mbps = 1200, .reg = 0x0b },
	{ .mbps = 1250, .reg = 0x1b },
	{ .mbps = 1300, .reg = 0x2b },
	{ .mbps = 1350, .reg = 0x3c },
	{ .mbps = 1400, .reg = 0x0c },
	{ .mbps = 1450, .reg = 0x1c },
	{ .mbps = 1500, .reg = 0x2c },
	{ .mbps = 1550, .reg = 0x3d },
	{ .mbps = 1600, .reg = 0x0d },
	{ .mbps = 1650, .reg = 0x1d },
	{ .mbps = 1700, .reg = 0x2e },
	{ .mbps = 1750, .reg = 0x3e },
	{ .mbps = 1800, .reg = 0x0e },
	{ .mbps = 1850, .reg = 0x1e },
	{ .mbps = 1900, .reg = 0x2f },
	{ .mbps = 1950, .reg = 0x3f },
	{ .mbps = 2000, .reg = 0x0f },
	{ .mbps = 2050, .reg = 0x40 },
	{ .mbps = 2100, .reg = 0x41 },
	{ .mbps = 2150, .reg = 0x42 },
	{ .mbps = 2200, .reg = 0x43 },
	{ .mbps = 2300, .reg = 0x45 },
	{ .mbps = 2350, .reg = 0x46 },
	{ .mbps = 2400, .reg = 0x47 },
	{ .mbps = 2450, .reg = 0x48 },
	{ .mbps = 2500, .reg = 0x49 },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_reg hsfreqrange_h3_v3h_m3n[] = {
	{ .mbps =   80, .reg = 0x00 },
	{ .mbps =   90, .reg = 0x10 },
	{ .mbps =  100, .reg = 0x20 },
	{ .mbps =  110, .reg = 0x30 },
	{ .mbps =  120, .reg = 0x01 },
	{ .mbps =  130, .reg = 0x11 },
	{ .mbps =  140, .reg = 0x21 },
	{ .mbps =  150, .reg = 0x31 },
	{ .mbps =  160, .reg = 0x02 },
	{ .mbps =  170, .reg = 0x12 },
	{ .mbps =  180, .reg = 0x22 },
	{ .mbps =  190, .reg = 0x32 },
	{ .mbps =  205, .reg = 0x03 },
	{ .mbps =  220, .reg = 0x13 },
	{ .mbps =  235, .reg = 0x23 },
	{ .mbps =  250, .reg = 0x33 },
	{ .mbps =  275, .reg = 0x04 },
	{ .mbps =  300, .reg = 0x14 },
	{ .mbps =  325, .reg = 0x25 },
	{ .mbps =  350, .reg = 0x35 },
	{ .mbps =  400, .reg = 0x05 },
	{ .mbps =  450, .reg = 0x16 },
	{ .mbps =  500, .reg = 0x26 },
	{ .mbps =  550, .reg = 0x37 },
	{ .mbps =  600, .reg = 0x07 },
	{ .mbps =  650, .reg = 0x18 },
	{ .mbps =  700, .reg = 0x28 },
	{ .mbps =  750, .reg = 0x39 },
	{ .mbps =  800, .reg = 0x09 },
	{ .mbps =  850, .reg = 0x19 },
	{ .mbps =  900, .reg = 0x29 },
	{ .mbps =  950, .reg = 0x3a },
	{ .mbps = 1000, .reg = 0x0a },
	{ .mbps = 1050, .reg = 0x1a },
	{ .mbps = 1100, .reg = 0x2a },
	{ .mbps = 1150, .reg = 0x3b },
	{ .mbps = 1200, .reg = 0x0b },
	{ .mbps = 1250, .reg = 0x1b },
	{ .mbps = 1300, .reg = 0x2b },
	{ .mbps = 1350, .reg = 0x3c },
	{ .mbps = 1400, .reg = 0x0c },
	{ .mbps = 1450, .reg = 0x1c },
	{ .mbps = 1500, .reg = 0x2c },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_reg hsfreqrange_m3w_h3es1[] = {
	{ .mbps =   80,	.reg = 0x00 },
	{ .mbps =   90,	.reg = 0x10 },
	{ .mbps =  100,	.reg = 0x20 },
	{ .mbps =  110,	.reg = 0x30 },
	{ .mbps =  120,	.reg = 0x01 },
	{ .mbps =  130,	.reg = 0x11 },
	{ .mbps =  140,	.reg = 0x21 },
	{ .mbps =  150,	.reg = 0x31 },
	{ .mbps =  160,	.reg = 0x02 },
	{ .mbps =  170,	.reg = 0x12 },
	{ .mbps =  180,	.reg = 0x22 },
	{ .mbps =  190,	.reg = 0x32 },
	{ .mbps =  205,	.reg = 0x03 },
	{ .mbps =  220,	.reg = 0x13 },
	{ .mbps =  235,	.reg = 0x23 },
	{ .mbps =  250,	.reg = 0x33 },
	{ .mbps =  275,	.reg = 0x04 },
	{ .mbps =  300,	.reg = 0x14 },
	{ .mbps =  325,	.reg = 0x05 },
	{ .mbps =  350,	.reg = 0x15 },
	{ .mbps =  400,	.reg = 0x25 },
	{ .mbps =  450,	.reg = 0x06 },
	{ .mbps =  500,	.reg = 0x16 },
	{ .mbps =  550,	.reg = 0x07 },
	{ .mbps =  600,	.reg = 0x17 },
	{ .mbps =  650,	.reg = 0x08 },
	{ .mbps =  700,	.reg = 0x18 },
	{ .mbps =  750,	.reg = 0x09 },
	{ .mbps =  800,	.reg = 0x19 },
	{ .mbps =  850,	.reg = 0x29 },
	{ .mbps =  900,	.reg = 0x39 },
	{ .mbps =  950,	.reg = 0x0a },
	{ .mbps = 1000,	.reg = 0x1a },
	{ .mbps = 1050,	.reg = 0x2a },
	{ .mbps = 1100,	.reg = 0x3a },
	{ .mbps = 1150,	.reg = 0x0b },
	{ .mbps = 1200,	.reg = 0x1b },
	{ .mbps = 1250,	.reg = 0x2b },
	{ .mbps = 1300,	.reg = 0x3b },
	{ .mbps = 1350,	.reg = 0x0c },
	{ .mbps = 1400,	.reg = 0x1c },
	{ .mbps = 1450,	.reg = 0x2c },
	{ .mbps = 1500,	.reg = 0x3c },
	{ /* sentinel */ },
};

/* PHY ESC Error Monitor */
#define PHEERM_REG			0x74

/* PHY Clock Lane Monitor */
#define PHCLM_REG			0x78
#define PHCLM_STOPSTATECKL		BIT(0)

/* PHY Data Lane Monitor */
#define PHDLM_REG			0x7c

/* CSI0CLK Frequency Configuration Preset Register */
#define CSI0CLKFCPR_REG			0x260
#define CSI0CLKFREQRANGE(n)		((n & 0x3f) << 16)

struct rcar_csi2_format {
	u32 code;
	unsigned int datatype;
	unsigned int bpp;
};

static const struct rcar_csi2_format rcar_csi2_formats[] = {
	{ .code = MEDIA_BUS_FMT_RGB888_1X24,	.datatype = 0x24, .bpp = 24 },
	{ .code = MEDIA_BUS_FMT_UYVY8_1X16,	.datatype = 0x1e, .bpp = 16 },
	{ .code = MEDIA_BUS_FMT_YUYV8_1X16,	.datatype = 0x1e, .bpp = 16 },
	{ .code = MEDIA_BUS_FMT_UYVY8_2X8,	.datatype = 0x1e, .bpp = 16 },
	{ .code = MEDIA_BUS_FMT_YUYV10_2X10,	.datatype = 0x1e, .bpp = 20 },
	{ .code = MEDIA_BUS_FMT_Y10_1X10,	.datatype = 0x2b, .bpp = 10 },
	{ .code = MEDIA_BUS_FMT_SBGGR8_1X8,     .datatype = 0x2a, .bpp = 8 },
	{ .code = MEDIA_BUS_FMT_SGBRG8_1X8,     .datatype = 0x2a, .bpp = 8 },
	{ .code = MEDIA_BUS_FMT_SGRBG8_1X8,     .datatype = 0x2a, .bpp = 8 },
	{ .code = MEDIA_BUS_FMT_SRGGB8_1X8,     .datatype = 0x2a, .bpp = 8 },
	{ .code = MEDIA_BUS_FMT_Y8_1X8,		.datatype = 0x2a, .bpp = 8 },
};

static const struct rcar_csi2_format *rcsi2_code_to_fmt(unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rcar_csi2_formats); i++)
		if (rcar_csi2_formats[i].code == code)
			return &rcar_csi2_formats[i];

	return NULL;
}

enum rcar_csi2_pads {
	RCAR_CSI2_SINK,
	RCAR_CSI2_SOURCE_VC0,
	RCAR_CSI2_SOURCE_VC1,
	RCAR_CSI2_SOURCE_VC2,
	RCAR_CSI2_SOURCE_VC3,
	NR_OF_RCAR_CSI2_PAD,
};

struct rcar_csi2_info {
	int (*init_phtw)(struct rcar_csi2 *priv, unsigned int mbps);
	int (*phy_post_init)(struct rcar_csi2 *priv);
	const struct rcsi2_mbps_reg *hsfreqrange;
	unsigned int csi0clkfreqrange;
	unsigned int num_channels;
	unsigned int features;
	bool clear_ulps;
	bool no_use_vdt;
	bool has_phyfrx_reg;
};

struct rcar_csi2 {
	struct device *dev;
	void __iomem *base;
	const struct rcar_csi2_info *info;
	struct reset_control *rstc;

	struct v4l2_subdev subdev;
	struct media_pad pads[NR_OF_RCAR_CSI2_PAD];

	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *remote;
	unsigned int remote_pad;

	struct v4l2_mbus_framefmt mf;

	struct mutex lock;
	int stream_count;

	unsigned short lanes;
	unsigned char lane_swap[4];

	bool cphy_connection;
	bool pin_swap;
};

static inline struct rcar_csi2 *sd_to_csi2(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rcar_csi2, subdev);
}

static inline struct rcar_csi2 *notifier_to_csi2(struct v4l2_async_notifier *n)
{
	return container_of(n, struct rcar_csi2, notifier);
}

static u32 rcsi2_read(struct rcar_csi2 *priv, unsigned int reg)
{
	return ioread32(priv->base + reg);
}

static void rcsi2_write(struct rcar_csi2 *priv, unsigned int reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

static void rcsi2_write16(struct rcar_csi2 *priv, unsigned int reg, u16 data)
{
	iowrite16(data, priv->base + reg);
}

static void rcsi2_enter_standby(struct rcar_csi2 *priv)
{
	if (!(priv->info->features & RCAR_CSI2_R8A779G0_FEATURE)) {
		rcsi2_write(priv, PHYCNT_REG, 0);
		rcsi2_write(priv, PHTC_REG, PHTC_TESTCLR);
	}

	reset_control_assert(priv->rstc);
	usleep_range(100, 150);
	pm_runtime_put(priv->dev);
}

static void rcsi2_exit_standby(struct rcar_csi2 *priv)
{
	pm_runtime_get_sync(priv->dev);
	reset_control_deassert(priv->rstc);
}

static int rcsi2_wait_phy_start(struct rcar_csi2 *priv,
				unsigned int lanes)
{
	unsigned int timeout;

	/* Wait for the clock and data lanes to enter LP-11 state. */
	for (timeout = 0; timeout <= 20; timeout++) {
		const u32 lane_mask = (1 << lanes) - 1;

		if ((rcsi2_read(priv, PHCLM_REG) & PHCLM_STOPSTATECKL)  &&
		    (rcsi2_read(priv, PHDLM_REG) & lane_mask) == lane_mask)
			return 0;

		usleep_range(1000, 2000);
	}

	dev_err(priv->dev, "Timeout waiting for LP-11 state\n");

	return -ETIMEDOUT;
}

static int rcsi2_set_phypll(struct rcar_csi2 *priv, unsigned int mbps)
{
	const struct rcsi2_mbps_reg *hsfreq;
	const struct rcsi2_mbps_reg *hsfreq_prev = NULL;

	if (mbps < priv->info->hsfreqrange->mbps)
		dev_warn(priv->dev, "%u Mbps less than min PHY speed %u Mbps",
			 mbps, priv->info->hsfreqrange->mbps);

	for (hsfreq = priv->info->hsfreqrange; hsfreq->mbps != 0; hsfreq++) {
		if (hsfreq->mbps >= mbps)
			break;
		hsfreq_prev = hsfreq;
	}

	if (!hsfreq->mbps) {
		dev_err(priv->dev, "Unsupported PHY speed (%u Mbps)", mbps);
		return -ERANGE;
	}

	if (hsfreq_prev &&
	    ((mbps - hsfreq_prev->mbps) <= (hsfreq->mbps - mbps)))
		hsfreq = hsfreq_prev;

	rcsi2_write(priv, PHYPLL_REG, PHYPLL_HSFREQRANGE(hsfreq->reg));

	return 0;
}

static int rcsi2_calc_mbps(struct rcar_csi2 *priv, unsigned int bpp,
			   unsigned int lanes)
{
	struct v4l2_subdev *source;
	struct v4l2_ctrl *ctrl;
	u64 mbps;

	if (!priv->remote)
		return -ENODEV;

	source = priv->remote;

	/* Read the pixel rate control from remote. */
	ctrl = v4l2_ctrl_find(source->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		dev_err(priv->dev, "no pixel rate control in subdev %s\n",
			source->name);
		return -EINVAL;
	}

	/*
	 * Calculate the phypll in mbps.
	 * link_freq = (pixel_rate * bits_per_sample) / (2 * nr_of_lanes)
	 * bps = link_freq * 2
	 */
	mbps = v4l2_ctrl_g_ctrl_int64(ctrl) * bpp;
	do_div(mbps, lanes * 1000000);

	return mbps;
}

static int rcsi2_get_active_lanes(struct rcar_csi2 *priv,
				  unsigned int *lanes)
{
	struct v4l2_mbus_config mbus_config = { 0 };
	unsigned int num_lanes = UINT_MAX;
	int ret;

	*lanes = priv->lanes;

	ret = v4l2_subdev_call(priv->remote, pad, get_mbus_config,
			       priv->remote_pad, &mbus_config);
	if (ret == -ENOIOCTLCMD) {
		dev_dbg(priv->dev, "No remote mbus configuration available\n");
		return 0;
	}

	if (ret) {
		dev_err(priv->dev, "Failed to get remote mbus configuration\n");
		return ret;
	}

	if (mbus_config.type != V4L2_MBUS_CSI2_DPHY &&
		mbus_config.type != V4L2_MBUS_CSI2_CPHY) {
		dev_err(priv->dev, "Unsupported media bus type %u\n",
			mbus_config.type);
		return -EINVAL;
	}

	if (mbus_config.flags & V4L2_MBUS_CSI2_1_LANE)
		num_lanes = 1;
	else if (mbus_config.flags & V4L2_MBUS_CSI2_2_LANE)
		num_lanes = 2;
	else if (mbus_config.flags & V4L2_MBUS_CSI2_3_LANE)
		num_lanes = 3;
	else if (mbus_config.flags & V4L2_MBUS_CSI2_4_LANE)
		num_lanes = 4;

	if (num_lanes > priv->lanes) {
		dev_err(priv->dev,
			"Unsupported mbus config: too many data lanes %u\n",
			num_lanes);
		return -EINVAL;
	}

	*lanes = num_lanes;

	return 0;
}

static int rcsi2_start_receiver(struct rcar_csi2 *priv)
{
	const struct rcar_csi2_format *format;
	u32 phycnt, vcdt = 0, vcdt2 = 0, fld = 0;
	unsigned int lanes;
	unsigned int i;
	int mbps, ret;

	dev_dbg(priv->dev, "Input size (%ux%u%c)\n",
		priv->mf.width, priv->mf.height,
		priv->mf.field == V4L2_FIELD_NONE ? 'p' : 'i');

	/* Code is validated in set_fmt. */
	format = rcsi2_code_to_fmt(priv->mf.code);

	/*
	 * Enable all supported CSI-2 channels with virtual channel and
	 * data type matching.
	 *
	 * NOTE: It's not possible to get individual datatype for each
	 *       source virtual channel. Once this is possible in V4L2
	 *       it should be used here.
	 */
	for (i = 0; i < priv->info->num_channels; i++) {
		u32 vcdt_part;

		vcdt_part = VCDT_SEL_VC(i) | VCDT_VCDTN_EN | VCDT_SEL_DTN_ON |
			VCDT_SEL_DT(format->datatype);

		/* Store in correct reg and offset. */
		if (i < 2)
			vcdt |= vcdt_part << ((i % 2) * 16);
		else
			vcdt2 |= vcdt_part << ((i % 2) * 16);
	}

	if (priv->mf.field == V4L2_FIELD_ALTERNATE) {
		fld = FLD_DET_SEL(1) | FLD_FLD_EN4 | FLD_FLD_EN3 | FLD_FLD_EN2
			| FLD_FLD_EN;

		if (priv->mf.height == 240)
			fld |= FLD_FLD_NUM(0);
		else
			fld |= FLD_FLD_NUM(1);
	}

	/*
	 * Get the number of active data lanes inspecting the remote mbus
	 * configuration.
	 */
	ret = rcsi2_get_active_lanes(priv, &lanes);
	if (ret)
		return ret;

	phycnt = PHYCNT_ENABLECLK;
	phycnt |= (1 << lanes) - 1;

	mbps = rcsi2_calc_mbps(priv, format->bpp, lanes);
	if (mbps < 0)
		return mbps;

	/* Enable interrupts. */
	rcsi2_write(priv, INTEN_REG, INTEN_INT_AFIFO_OF | INTEN_INT_ERRSOTHS
		    | INTEN_INT_ERRSOTSYNCHS);

	/* Init */
	rcsi2_write(priv, TREF_REG, TREF_TREF);
	rcsi2_write(priv, PHTC_REG, 0);

	/* Configure */
	if (!priv->info->no_use_vdt) {
		rcsi2_write(priv, VCDT_REG, vcdt);
		if (vcdt2)
			rcsi2_write(priv, VCDT2_REG, vcdt2);
	}

	/* Lanes are zero indexed. */
	rcsi2_write(priv, LSWAP_REG,
		    LSWAP_L0SEL(priv->lane_swap[0] - 1) |
		    LSWAP_L1SEL(priv->lane_swap[1] - 1) |
		    LSWAP_L2SEL(priv->lane_swap[2] - 1) |
		    LSWAP_L3SEL(priv->lane_swap[3] - 1));

	/* Start */
	if (priv->info->init_phtw) {
		ret = priv->info->init_phtw(priv, mbps);
		if (ret)
			return ret;
	}

	if (priv->info->hsfreqrange) {
		ret = rcsi2_set_phypll(priv, mbps);
		if (ret)
			return ret;
	}

	if (priv->info->csi0clkfreqrange)
		rcsi2_write(priv, CSI0CLKFCPR_REG,
			    CSI0CLKFREQRANGE(priv->info->csi0clkfreqrange));

	if (priv->info->has_phyfrx_reg)
		rcsi2_write(priv, PHYFRX_REG, PHYFRX_FORCERX_MODE_3 |
					      PHYFRX_FORCERX_MODE_2 |
					      PHYFRX_FORCERX_MODE_1 |
					      PHYFRX_FORCERX_MODE_0);

	rcsi2_write(priv, PHYCNT_REG, phycnt);
	rcsi2_write(priv, LINKCNT_REG, LINKCNT_MONITOR_EN |
		    LINKCNT_REG_MONI_PACT_EN | LINKCNT_ICLK_NONSTOP);
	rcsi2_write(priv, FLD_REG, fld);
	rcsi2_write(priv, PHYCNT_REG, phycnt | PHYCNT_SHUTDOWNZ);
	rcsi2_write(priv, PHYCNT_REG, phycnt | PHYCNT_SHUTDOWNZ | PHYCNT_RSTZ);

	ret = rcsi2_wait_phy_start(priv, lanes);
	if (ret)
		return ret;

	if (priv->info->has_phyfrx_reg)
		rcsi2_write(priv, PHYFRX_REG, 0);

	/* Run post PHY start initialization, if needed. */
	if (priv->info->phy_post_init) {
		ret = priv->info->phy_post_init(priv);
		if (ret)
			return ret;
	}

	/* Clear Ultra Low Power interrupt. */
	if (priv->info->clear_ulps)
		rcsi2_write(priv, INTSTATE_REG,
			    INTSTATE_INT_ULPS_START |
			    INTSTATE_INT_ULPS_END);
	return 0;
}

static int rcsi2_c_phy_setting(struct rcar_csi2 *priv, int data_rate)
{
	const struct rcsi2_cphy_setting *cphy_setting_value;
	unsigned int timeout;
	u32 status;

	for ( cphy_setting_value = cphy_setting_table_r8a779g0;
			cphy_setting_value->msps != 0; cphy_setting_value++ ) {
		if (cphy_setting_value->msps > data_rate)
			break;
	}

	if (!cphy_setting_value->msps) {
		dev_err(priv->dev, "Unsupported PHY speed for mpsp setting (%u Msps)", data_rate);
		return -ERANGE;
	}

	/* C-PHY specific */
	rcsi2_write16(priv, CORE_DIG_RW_COMMON(7), 0x0155);
	rcsi2_write16(priv, PPI_STARTUP_RW_COMMON_DPHY(7), 0x0068);
	rcsi2_write16(priv, PPI_STARTUP_RW_COMMON_DPHY(8), 0x0010);

	rcsi2_write16(priv, CORE_DIG_CLANE_0_RW_LP_0, 0x463C);
	rcsi2_write16(priv, CORE_DIG_CLANE_1_RW_LP_0, 0x463C);
	rcsi2_write16(priv, CORE_DIG_CLANE_2_RW_LP_0, 0x463C);

	rcsi2_write16(priv, CORE_DIG_CLANE_0_RW_HS_RX(0), 0x00D5);
	rcsi2_write16(priv, CORE_DIG_CLANE_1_RW_HS_RX(0), 0x00D5);
	rcsi2_write16(priv, CORE_DIG_CLANE_2_RW_HS_RX(0), 0x00D5);

	rcsi2_write16(priv, CORE_DIG_CLANE_0_RW_HS_RX(1), 0x0013);
	rcsi2_write16(priv, CORE_DIG_CLANE_1_RW_HS_RX(1), 0x0013);
	rcsi2_write16(priv, CORE_DIG_CLANE_2_RW_HS_RX(1), 0x0013);

	rcsi2_write16(priv, CORE_DIG_CLANE_0_RW_HS_RX(5), 0x0013);
	rcsi2_write16(priv, CORE_DIG_CLANE_1_RW_HS_RX(5), 0x0013);
	rcsi2_write16(priv, CORE_DIG_CLANE_2_RW_HS_RX(5), 0x0013);

	rcsi2_write16(priv, CORE_DIG_CLANE_0_RW_HS_RX(6), 0x000A);
	rcsi2_write16(priv, CORE_DIG_CLANE_1_RW_HS_RX(6), 0x000A);
	rcsi2_write16(priv, CORE_DIG_CLANE_2_RW_HS_RX(6), 0x000A);

	rcsi2_write16(priv, CORE_DIG_CLANE_0_RW_HS_RX(2), cphy_setting_value->rw_hs_rx_2);
	rcsi2_write16(priv, CORE_DIG_CLANE_1_RW_HS_RX(2), cphy_setting_value->rw_hs_rx_2);
	rcsi2_write16(priv, CORE_DIG_CLANE_2_RW_HS_RX(2), cphy_setting_value->rw_hs_rx_2);

	rcsi2_write16(priv, CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2(2), 0x0001);
	rcsi2_write16(priv, CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2(2), 0);
	rcsi2_write16(priv, CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2(2), 0x0001);
	rcsi2_write16(priv, CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2(2), 0x0001);
	rcsi2_write16(priv, CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2(2), 0);

	rcsi2_write16(priv, CORE_DIG_RW_TRIO0(0), cphy_setting_value->rw_trio_0);
	rcsi2_write16(priv, CORE_DIG_RW_TRIO1(0), cphy_setting_value->rw_trio_0);
	rcsi2_write16(priv, CORE_DIG_RW_TRIO2(0), cphy_setting_value->rw_trio_0);

	rcsi2_write16(priv, CORE_DIG_RW_TRIO0(2), cphy_setting_value->rw_trio_2);
	rcsi2_write16(priv, CORE_DIG_RW_TRIO1(2), cphy_setting_value->rw_trio_2);
	rcsi2_write16(priv, CORE_DIG_RW_TRIO2(2), cphy_setting_value->rw_trio_2);

	rcsi2_write16(priv, CORE_DIG_RW_TRIO0(1), cphy_setting_value->rw_trio_1);
	rcsi2_write16(priv, CORE_DIG_RW_TRIO1(1), cphy_setting_value->rw_trio_1);
	rcsi2_write16(priv, CORE_DIG_RW_TRIO2(1), cphy_setting_value->rw_trio_1);

	if (priv->pin_swap) {
		/* For WhiteHawk board */
		rcsi2_write16(priv, CORE_DIG_CLANE_1_RW_CFG_0, 0xf5);
		rcsi2_write16(priv, CORE_DIG_CLANE_1_RW_HS_TX_6, 0x5000);
	}

	/* Step T4: Leave Shutdown mode */
	rcsi2_write(priv, DPHY_RSTZ, BIT(0));
	rcsi2_write(priv, PHY_SHUTDOWNZ, BIT(0));

	/* Step T5: wating for calibration */
	for (timeout = 10; timeout > 0; --timeout) {
		status = rcsi2_read(priv, ST_PHYST);
		if (status & ST_PHY_READY)
			break;
		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(priv->dev, "PHY calibration failed\n");
		return -ETIMEDOUT;
	}

	/* Step T6: C-PHY setting - analog programing*/
	rcsi2_write16(priv, CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2(9),
					cphy_setting_value->afe_lane0_29);
	rcsi2_write16(priv, CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2(7),
					cphy_setting_value->afe_lane0_27);

	return 0;
}

static int rcsi2_d_phy_setting(struct rcar_csi2 *priv, int data_rate)
{
	/* T.B.D. */
	return 0;
}

static int rcsi2_start_receiver_v4h(struct rcar_csi2 *priv)
{
	const struct rcar_csi2_format *format;
	int data_rate, ret;
	unsigned int lanes;

	/* Calculate paramters */
	format = rcsi2_code_to_fmt(priv->mf.code);

	ret = rcsi2_get_active_lanes(priv, &lanes);
	if (ret)
		return ret;

	data_rate = rcsi2_calc_mbps(priv, format->bpp, lanes);
	if (data_rate < 0)
		return data_rate;

	if (priv->cphy_connection)
		do_div(data_rate, 2.8);

	/* Step T0: Reset LINK and PHY*/
	rcsi2_write(priv,CSI2_RESETN, 0);
	rcsi2_write(priv,DPHY_RSTZ, 0);
	rcsi2_write(priv,PHY_SHUTDOWNZ, 0);

	/* Step T1: PHY static setting */
	rcsi2_write(priv, PHY_EN, BIT(0));
	rcsi2_write(priv, FLDC, 0);
	rcsi2_write(priv, FLDD, 0);
	rcsi2_write(priv, IDIC, 0);
	rcsi2_write(priv, PHY_MODE, BIT(0));
	rcsi2_write(priv,N_LANES, lanes - 1);

	/* Step T2: Reset CSI2 */
	rcsi2_write(priv, CSI2_RESETN, BIT(0));

	/* Step T3: Registers static setting through APB */
	/* Common setting */
	rcsi2_write16(priv, CORE_DIG_ANACTRL_RW_COMMON_ANACTRL(0), 0x1BFD);
	rcsi2_write16(priv, PPI_STARTUP_RW_COMMON_STARTUP_1_1, 0x0233);
	rcsi2_write16(priv, PPI_STARTUP_RW_COMMON_DPHY(6), 0x0027);
	rcsi2_write16(priv, PPI_CALIBCTRL_RW_COMMON_BG_0, 0x01F4);
	rcsi2_write16(priv, PPI_RW_TERMCAL_CFG_0, 0x0013);
	rcsi2_write16(priv, PPI_RW_OFFSETCAL_CFG_0, 0x0003);
	rcsi2_write16(priv, PPI_RW_LPDCOCAL_TIMEBASE, 0x004F);
	rcsi2_write16(priv, PPI_RW_LPDCOCAL_NREF, 0x0320);
	rcsi2_write16(priv, PPI_RW_LPDCOCAL_NREF_RANGE, 0x000F);
	rcsi2_write16(priv, PPI_RW_LPDCOCAL_TWAIT_CONFIG, 0xFE18);
	rcsi2_write16(priv, PPI_RW_LPDCOCAL_VT_CONFIG, 0x0C3C);
	rcsi2_write16(priv, PPI_RW_LPDCOCAL_COARSE_CFG, 0x0105);
	rcsi2_write16(priv, CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2(6), 0x1000);
	rcsi2_write16(priv, PPI_RW_COMMON_CFG, 0x0003);

	if (priv->cphy_connection) {
		ret = rcsi2_c_phy_setting(priv, data_rate);
		if (ret) {
			dev_err(priv->dev, "Setting C-PHY failed\n");
		}
	} else {
		ret = rcsi2_d_phy_setting(priv, data_rate);
		if (ret) {
			dev_err(priv->dev, "Setting D-PHY failed\n");
		}
	}

	return 0;
}

static int rcsi2_wait_phy_start_v4h(struct rcar_csi2 *priv)
{
	unsigned int timeout;
	u32 status;

	/* Step T7: wait for stopstate_N */
	for (timeout = 10; timeout > 0; --timeout) {
		status = rcsi2_read(priv, ST_PHYST);
		if (status & ST_STOPSTATE_0 &&
			status & ST_STOPSTATE_1 &&
			status & ST_STOPSTATE_2)
			return 0;
		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static int rcsi2_start(struct rcar_csi2 *priv)
{
	int ret;

	/* Start CSI PHY */
	rcsi2_exit_standby(priv);

	if (!(priv->info->features & RCAR_CSI2_R8A779G0_FEATURE)) {
		ret = rcsi2_start_receiver(priv);
	} else {
		/* init V4H PHY */
		ret = rcsi2_start_receiver_v4h(priv);
	}

	if (ret) {
		rcsi2_enter_standby(priv);
			return ret;
	}

	/* Start camera side device */
	ret = v4l2_subdev_call(priv->remote, video, s_stream, 1);
	if (ret) {
		rcsi2_enter_standby(priv);
		return ret;
	}

	/* Confirmation of CSI PHY */
	if (priv->info->features & RCAR_CSI2_R8A779G0_FEATURE)
		rcsi2_wait_phy_start_v4h(priv);
	return 0;
}

static void rcsi2_stop(struct rcar_csi2 *priv)
{
	rcsi2_enter_standby(priv);
	v4l2_subdev_call(priv->remote, video, s_stream, 0);
}

static int rcsi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);
	int ret = 0;

	mutex_lock(&priv->lock);

	if (!priv->remote) {
		ret = -ENODEV;
		goto out;
	}

	if (enable && priv->stream_count == 0) {
		ret = rcsi2_start(priv);
		if (ret)
			goto out;
	} else if (!enable && priv->stream_count == 1) {
		rcsi2_stop(priv);
	}

	priv->stream_count += enable ? 1 : -1;
out:
	mutex_unlock(&priv->lock);

	return ret;
}

static int rcsi2_set_pad_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *format)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);
	struct v4l2_mbus_framefmt *framefmt;

	if (!rcsi2_code_to_fmt(format->format.code))
		format->format.code = rcar_csi2_formats[0].code;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		priv->mf = format->format;
	} else {
		framefmt = v4l2_subdev_get_try_format(sd, cfg, 0);
		*framefmt = format->format;
	}

	return 0;
}

static int rcsi2_get_pad_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *format)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		format->format = priv->mf;
	else
		format->format = *v4l2_subdev_get_try_format(sd, cfg, 0);

	return 0;
}

static const struct v4l2_subdev_video_ops rcar_csi2_video_ops = {
	.s_stream = rcsi2_s_stream,
};

static const struct v4l2_subdev_pad_ops rcar_csi2_pad_ops = {
	.set_fmt = rcsi2_set_pad_format,
	.get_fmt = rcsi2_get_pad_format,
};

static const struct v4l2_subdev_ops rcar_csi2_subdev_ops = {
	.video	= &rcar_csi2_video_ops,
	.pad	= &rcar_csi2_pad_ops,
};

static irqreturn_t rcsi2_irq(int irq, void *data)
{
	struct rcar_csi2 *priv = data;
	u32 status, err_status;

	status = rcsi2_read(priv, INTSTATE_REG);
	err_status = rcsi2_read(priv, INTERRSTATE_REG);

	if (!status)
		return IRQ_HANDLED;

	rcsi2_write(priv, INTSTATE_REG, status);

	if (!err_status)
		return IRQ_HANDLED;

	rcsi2_write(priv, INTERRSTATE_REG, err_status);

	dev_info(priv->dev, "Transfer error, restarting CSI-2 receiver\n");

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rcsi2_irq_thread(int irq, void *data)
{
	struct rcar_csi2 *priv = data;

	mutex_lock(&priv->lock);
	rcsi2_stop(priv);
	usleep_range(1000, 2000);
	if (rcsi2_start(priv))
		dev_warn(priv->dev, "Failed to restart CSI-2 receiver\n");
	mutex_unlock(&priv->lock);

	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * Async handling and registration of subdevices and links.
 */

static int rcsi2_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_subdev *asd)
{
	struct rcar_csi2 *priv = notifier_to_csi2(notifier);
	int pad;

	pad = media_entity_get_fwnode_pad(&subdev->entity, asd->match.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		dev_err(priv->dev, "Failed to find pad for %s\n", subdev->name);
		return pad;
	}

	priv->remote = subdev;
	priv->remote_pad = pad;

	dev_dbg(priv->dev, "Bound %s pad: %d\n", subdev->name, pad);

	return media_create_pad_link(&subdev->entity, pad,
				     &priv->subdev.entity, 0,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static void rcsi2_notify_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct rcar_csi2 *priv = notifier_to_csi2(notifier);

	priv->remote = NULL;

	dev_dbg(priv->dev, "Unbind %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations rcar_csi2_notify_ops = {
	.bound = rcsi2_notify_bound,
	.unbind = rcsi2_notify_unbind,
};

static int rcsi2_parse_v4l2(struct rcar_csi2 *priv,
			    struct v4l2_fwnode_endpoint *vep)
{
	unsigned int i;

	/* Only port 0 endpoint 0 is valid. */
	if (vep->base.port || vep->base.id)
		return -ENOTCONN;

	if (vep->bus_type != V4L2_MBUS_CSI2_DPHY &&
		vep->bus_type != V4L2_MBUS_CSI2_CPHY) {
		dev_err(priv->dev, "Unsupported bus: %u\n", vep->bus_type);
		return -EINVAL;
	}

	priv->lanes = vep->bus.mipi_csi2.num_data_lanes;
	if (vep->bus_type == V4L2_MBUS_CSI2_DPHY) {
		if (priv->lanes != 1 && priv->lanes != 2 && priv->lanes != 4) {
			dev_err(priv->dev, "Unsupported number of data-lanes: %u\n",
				priv->lanes);
			return -EINVAL;
		}
		priv->cphy_connection = false;
	} else {
		if (priv->lanes != 3) {
			dev_err(priv->dev, "Unsupported number of data-lanes: %u\n",
				priv->lanes);
			return -EINVAL;
		}
		priv->cphy_connection = true;
	}

	for (i = 0; i < ARRAY_SIZE(priv->lane_swap); i++) {
		priv->lane_swap[i] = i < priv->lanes ?
			vep->bus.mipi_csi2.data_lanes[i] : i;

		/* Check for valid lane number. */
		if (priv->lane_swap[i] < 1 || priv->lane_swap[i] > 4) {
			dev_err(priv->dev, "data-lanes must be in 1-4 range\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int rcsi2_parse_dt(struct rcar_csi2 *priv)
{
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *fwnode;
	struct device_node *ep;
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = 0 };
	int ret;

	if (of_find_property(priv->dev->of_node, "pin-swap", NULL))
		priv->pin_swap = true;
	else
		priv->pin_swap = false;

	ep = of_graph_get_endpoint_by_regs(priv->dev->of_node, 0, 0);
	if (!ep) {
		dev_dbg(priv->dev, "Not connected to subdevice\n");
		return 0;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &v4l2_ep);
	if (ret) {
		dev_err(priv->dev, "Could not parse v4l2 endpoint\n");
		of_node_put(ep);
		return -EINVAL;
	}

	ret = rcsi2_parse_v4l2(priv, &v4l2_ep);
	if (ret) {
		of_node_put(ep);
		return ret;
	}

	fwnode = fwnode_graph_get_remote_endpoint(of_fwnode_handle(ep));
	of_node_put(ep);

	dev_dbg(priv->dev, "Found '%pOF'\n", to_of_node(fwnode));

	v4l2_async_notifier_init(&priv->notifier);
	priv->notifier.ops = &rcar_csi2_notify_ops;

	asd = v4l2_async_notifier_add_fwnode_subdev(&priv->notifier, fwnode,
						    sizeof(*asd));
	fwnode_handle_put(fwnode);
	if (IS_ERR(asd))
		return PTR_ERR(asd);

	ret = v4l2_async_subdev_notifier_register(&priv->subdev,
						  &priv->notifier);
	if (ret)
		v4l2_async_notifier_cleanup(&priv->notifier);

	return ret;
}

/* -----------------------------------------------------------------------------
 * PHTW initialization sequences.
 *
 * NOTE: Magic values are from the datasheet and lack documentation.
 */

static int rcsi2_phtw_write(struct rcar_csi2 *priv, u16 data, u16 code)
{
	unsigned int timeout;

	rcsi2_write(priv, PHTW_REG,
		    PHTW_DWEN | PHTW_TESTDIN_DATA(data) |
		    PHTW_CWEN | PHTW_TESTDIN_CODE(code));

	/* Wait for DWEN and CWEN to be cleared by hardware. */
	for (timeout = 0; timeout <= 20; timeout++) {
		if (!(rcsi2_read(priv, PHTW_REG) & (PHTW_DWEN | PHTW_CWEN)))
			return 0;

		usleep_range(1000, 2000);
	}

	dev_err(priv->dev, "Timeout waiting for PHTW_DWEN and/or PHTW_CWEN\n");

	return -ETIMEDOUT;
}

static int rcsi2_phtw_write_array(struct rcar_csi2 *priv,
				  const struct phtw_value *values)
{
	const struct phtw_value *value;
	int ret;

	for (value = values; value->data || value->code; value++) {
		ret = rcsi2_phtw_write(priv, value->data, value->code);
		if (ret)
			return ret;
	}

	return 0;
}

static int rcsi2_phtw_write_mbps(struct rcar_csi2 *priv, unsigned int mbps,
				 const struct rcsi2_mbps_reg *values, u16 code)
{
	const struct rcsi2_mbps_reg *value;
	const struct rcsi2_mbps_reg *prev_value = NULL;

	for (value = values; value->mbps; value++) {
		if (value->mbps >= mbps)
			break;
		prev_value = value;
	}

	if (prev_value &&
	    ((mbps - prev_value->mbps) <= (value->mbps - mbps)))
		value = prev_value;

	if (!value->mbps) {
		dev_err(priv->dev, "Unsupported PHY speed (%u Mbps)", mbps);
		return -ERANGE;
	}

	return rcsi2_phtw_write(priv, value->reg, code);
}

static int __rcsi2_init_phtw_h3_v3h_m3n(struct rcar_csi2 *priv,
					unsigned int mbps)
{
	static const struct phtw_value step1[] = {
		{ .data = 0xcc, .code = 0xe2 },
		{ .data = 0x01, .code = 0xe3 },
		{ .data = 0x11, .code = 0xe4 },
		{ .data = 0x01, .code = 0xe5 },
		{ .data = 0x10, .code = 0x04 },
		{ /* sentinel */ },
	};

	static const struct phtw_value step2[] = {
		{ .data = 0x38, .code = 0x08 },
		{ .data = 0x01, .code = 0x00 },
		{ .data = 0x4b, .code = 0xac },
		{ .data = 0x03, .code = 0x00 },
		{ .data = 0x80, .code = 0x07 },
		{ /* sentinel */ },
	};

	int ret;

	ret = rcsi2_phtw_write_array(priv, step1);
	if (ret)
		return ret;

	if (mbps != 0 && mbps <= 250) {
		ret = rcsi2_phtw_write(priv, 0x39, 0x05);
		if (ret)
			return ret;

		ret = rcsi2_phtw_write_mbps(priv, mbps, phtw_mbps_h3_v3h_m3n,
					    0xf1);
		if (ret)
			return ret;
	}

	return rcsi2_phtw_write_array(priv, step2);
}

static int rcsi2_init_phtw_h3_v3h_m3n(struct rcar_csi2 *priv, unsigned int mbps)
{
	return __rcsi2_init_phtw_h3_v3h_m3n(priv, mbps);
}

static int rcsi2_init_phtw_h3es2(struct rcar_csi2 *priv, unsigned int mbps)
{
	return __rcsi2_init_phtw_h3_v3h_m3n(priv, 0);
}

static int rcsi2_init_phtw_v3m_e3(struct rcar_csi2 *priv, unsigned int mbps)
{
	return rcsi2_phtw_write_mbps(priv, mbps, phtw_mbps_v3m_e3, 0x44);
}

static int rcsi2_phy_post_init_v3m_e3(struct rcar_csi2 *priv)
{
	static const struct phtw_value step1[] = {
		{ .data = 0xee, .code = 0x34 },
		{ .data = 0xee, .code = 0x44 },
		{ .data = 0xee, .code = 0x54 },
		{ .data = 0xee, .code = 0x84 },
		{ .data = 0xee, .code = 0x94 },
		{ /* sentinel */ },
	};

	return rcsi2_phtw_write_array(priv, step1);
}

static int rcsi2_init_phtw_v3u(struct rcar_csi2 *priv,
			       unsigned int mbps)
{
	/* In case of 1500Mbps or less */
	static const struct phtw_value step1[] = {
		{ .data = 0xcc, .code = 0xe2 },
		{ /* sentinel */ },
	};

	static const struct phtw_value step2[] = {
		{ .data = 0x01, .code = 0xe3 },
		{ .data = 0x11, .code = 0xe4 },
		{ .data = 0x01, .code = 0xe5 },
		{ /* sentinel */ },
	};

	/* In case of 1500Mbps or less */
	static const struct phtw_value step3[] = {
		{ .data = 0x38, .code = 0x08 },
		{ /* sentinel */ },
	};

	static const struct phtw_value step4[] = {
		{ .data = 0x01, .code = 0x00 },
		{ .data = 0x4b, .code = 0xac },
		{ .data = 0x03, .code = 0x00 },
		{ .data = 0x80, .code = 0x07 },
		{ /* sentinel */ },
	};

	int ret;

	if (mbps != 0 && mbps <= 1500)
		ret = rcsi2_phtw_write_array(priv, step1);
	else
		ret = rcsi2_phtw_write_mbps(priv, mbps, phtw_mbps_v3u, 0xe2);
	if (ret)
		return ret;

	ret = rcsi2_phtw_write_array(priv, step2);
	if (ret)
		return ret;

	if (mbps != 0 && mbps <= 1500) {
		ret = rcsi2_phtw_write_array(priv, step3);
		if (ret)
			return ret;
	}

	ret = rcsi2_phtw_write_array(priv, step4);
	if (ret)
		return ret;

	return ret;
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver.
 */

static const struct media_entity_operations rcar_csi2_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int rcsi2_probe_resources(struct rcar_csi2 *priv,
				 struct platform_device *pdev)
{
	struct resource *res;
	int irq, ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(&pdev->dev, irq, rcsi2_irq,
					rcsi2_irq_thread, IRQF_SHARED,
					KBUILD_MODNAME, priv);
	if (ret)
		return ret;

	priv->rstc = devm_reset_control_get(&pdev->dev, NULL);

	return PTR_ERR_OR_ZERO(priv->rstc);
}

static const struct rcar_csi2_info rcar_csi2_info_r8a7795 = {
	.init_phtw = rcsi2_init_phtw_h3_v3h_m3n,
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.csi0clkfreqrange = 0x20,
	.num_channels = 4,
	.clear_ulps = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7795es1 = {
	.hsfreqrange = hsfreqrange_m3w_h3es1,
	.num_channels = 4,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7795es2 = {
	.init_phtw = rcsi2_init_phtw_h3es2,
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.csi0clkfreqrange = 0x20,
	.num_channels = 4,
	.clear_ulps = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7796 = {
	.hsfreqrange = hsfreqrange_m3w_h3es1,
	.num_channels = 4,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77965 = {
	.init_phtw = rcsi2_init_phtw_h3_v3h_m3n,
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.csi0clkfreqrange = 0x20,
	.num_channels = 4,
	.clear_ulps = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77970 = {
	.init_phtw = rcsi2_init_phtw_v3m_e3,
	.phy_post_init = rcsi2_phy_post_init_v3m_e3,
	.num_channels = 4,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77980 = {
	.init_phtw = rcsi2_init_phtw_h3_v3h_m3n,
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.csi0clkfreqrange = 0x20,
	.num_channels = 4,
	.clear_ulps = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77990 = {
	.init_phtw = rcsi2_init_phtw_v3m_e3,
	.phy_post_init = rcsi2_phy_post_init_v3m_e3,
	.num_channels = 2,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a779g0 = {
	.features = RCAR_CSI2_R8A779G0_FEATURE,
	.num_channels = 16,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a779a0 = {
	.init_phtw = rcsi2_init_phtw_v3u,
	.hsfreqrange = hsfreqrange_v3u,
	.csi0clkfreqrange = 0x20,
	.clear_ulps = true,
	.num_channels = 4,
	.no_use_vdt = true,
	.has_phyfrx_reg = true,
};

static const struct of_device_id rcar_csi2_of_table[] = {
	{
		.compatible = "renesas,r8a774a1-csi2",
		.data = &rcar_csi2_info_r8a7796,
	},
	{
		.compatible = "renesas,r8a774b1-csi2",
		.data = &rcar_csi2_info_r8a77965,
	},
	{
		.compatible = "renesas,r8a774c0-csi2",
		.data = &rcar_csi2_info_r8a77990,
	},
	{
		.compatible = "renesas,r8a774e1-csi2",
		.data = &rcar_csi2_info_r8a7795,
	},
	{
		.compatible = "renesas,r8a7795-csi2",
		.data = &rcar_csi2_info_r8a7795,
	},
	{
		.compatible = "renesas,r8a7796-csi2",
		.data = &rcar_csi2_info_r8a7796,
	},
	{
		.compatible = "renesas,r8a77961-csi2",
		.data = &rcar_csi2_info_r8a7796,
	},
	{
		.compatible = "renesas,r8a77965-csi2",
		.data = &rcar_csi2_info_r8a77965,
	},
	{
		.compatible = "renesas,r8a77970-csi2",
		.data = &rcar_csi2_info_r8a77970,
	},
	{
		.compatible = "renesas,r8a77980-csi2",
		.data = &rcar_csi2_info_r8a77980,
	},
	{
		.compatible = "renesas,r8a77990-csi2",
		.data = &rcar_csi2_info_r8a77990,
	},
	{
		.compatible = "renesas,r8a779g0-csi2",
		.data = &rcar_csi2_info_r8a779g0,
	},
	{
		.compatible = "renesas,r8a779a0-csi2",
		.data = &rcar_csi2_info_r8a779a0,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rcar_csi2_of_table);

static const struct soc_device_attribute r8a7795[] = {
	{
		.soc_id = "r8a7795", .revision = "ES1.*",
		.data = &rcar_csi2_info_r8a7795es1,
	},
	{
		.soc_id = "r8a7795", .revision = "ES2.*",
		.data = &rcar_csi2_info_r8a7795es2,
	},
	{ /* sentinel */ },
};

static int rcsi2_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *attr;
	struct rcar_csi2 *priv;
	unsigned int i;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info = of_device_get_match_data(&pdev->dev);

	/*
	 * The different ES versions of r8a7795 (H3) behave differently but
	 * share the same compatible string.
	 */
	attr = soc_device_match(r8a7795);
	if (attr)
		priv->info = attr->data;

	priv->dev = &pdev->dev;

	mutex_init(&priv->lock);
	priv->stream_count = 0;

	ret = rcsi2_probe_resources(priv, pdev);
	if (ret) {
		dev_err(priv->dev, "Failed to get resources\n");
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	ret = rcsi2_parse_dt(priv);
	if (ret)
		return ret;

	priv->subdev.owner = THIS_MODULE;
	priv->subdev.dev = &pdev->dev;
	v4l2_subdev_init(&priv->subdev, &rcar_csi2_subdev_ops);
	v4l2_set_subdevdata(&priv->subdev, &pdev->dev);
	snprintf(priv->subdev.name, V4L2_SUBDEV_NAME_SIZE, "%s %s",
		 KBUILD_MODNAME, dev_name(&pdev->dev));
	priv->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	priv->subdev.entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	priv->subdev.entity.ops = &rcar_csi2_entity_ops;

	priv->pads[RCAR_CSI2_SINK].flags = MEDIA_PAD_FL_SINK;
	for (i = RCAR_CSI2_SOURCE_VC0; i < NR_OF_RCAR_CSI2_PAD; i++)
		priv->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&priv->subdev.entity, NR_OF_RCAR_CSI2_PAD,
				     priv->pads);
	if (ret)
		goto error;

	pm_runtime_enable(&pdev->dev);

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		goto error;

	dev_info(priv->dev, "%d lanes found\n", priv->lanes);

	return 0;

error:
	v4l2_async_notifier_unregister(&priv->notifier);
	v4l2_async_notifier_cleanup(&priv->notifier);

	return ret;
}

static int rcsi2_remove(struct platform_device *pdev)
{
	struct rcar_csi2 *priv = platform_get_drvdata(pdev);

	v4l2_async_notifier_unregister(&priv->notifier);
	v4l2_async_unregister_subdev(&priv->subdev);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver rcar_csi2_pdrv = {
	.remove	= rcsi2_remove,
	.probe	= rcsi2_probe,
	.driver	= {
		.name	= "rcar-csi2",
		.of_match_table	= rcar_csi2_of_table,
	},
};

module_platform_driver(rcar_csi2_pdrv);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Renesas R-Car MIPI CSI-2 receiver driver");
MODULE_LICENSE("GPL");
