/*
 * Driver for Renesas R-Car MIPI CSI-2 Receiver
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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
#include <linux/sys_soc.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

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
 * VCDT[0-15]:  Channel 1 VCDT[16-31]:  Channel 2
 * VCDT2[0-15]: Channel 3 VCDT2[16-31]: Channel 4
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
#define PHTW_CWEN			BIT(8)
#define PHTW_TESTDIN_CODE		0x44	/* for r8a77990 */
#define PHTW_TESTDIN_DATA(n)		((n & 0xff) << 16)

/* PHY Test Interface Clear */
#define PHTC_REG			0x58
#define PHTC_TESTCLR			BIT(0)

/* PHY Frequency Control */
#define PHYPLL_REG			0x68
#define PHYPLL_HSFREQRANGE(n)		((n) << 16)

struct phypll_hsfreqrange {
	u16 mbps;
	u16 reg;
};

struct phtw_freqrange {
	unsigned int	mbps;
	u32		phtw_reg;
};

static const struct phypll_hsfreqrange hsfreqrange_h3_v3h_m3n[] = {
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
	/* guard */
	{ .mbps =   0,	.reg = 0x00 },
};

static const struct phypll_hsfreqrange hsfreqrange_m3w_h3es1[] = {
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
	{ .mbps =  950,	.reg = 0x0A },
	{ .mbps = 1000,	.reg = 0x1A },
	{ .mbps = 1050,	.reg = 0x2A },
	{ .mbps = 1100,	.reg = 0x3A },
	{ .mbps = 1150,	.reg = 0x0B },
	{ .mbps = 1200,	.reg = 0x1B },
	{ .mbps = 1250,	.reg = 0x2B },
	{ .mbps = 1300,	.reg = 0x3B },
	{ .mbps = 1350,	.reg = 0x0C },
	{ .mbps = 1400,	.reg = 0x1C },
	{ .mbps = 1450,	.reg = 0x2C },
	{ .mbps = 1500,	.reg = 0x3C },
	/* guard */
	{ .mbps =   0,	.reg = 0x00 },
};

static const struct phtw_freqrange phtw_e3_v3m[] = {
	{ .mbps = 89,	.phtw_reg =  0x00 },
	{ .mbps = 99,	.phtw_reg =  0x20 },
	{ .mbps = 109,	.phtw_reg =  0x40 },
	{ .mbps = 129,	.phtw_reg =  0x02 },
	{ .mbps = 139,	.phtw_reg =  0x22 },
	{ .mbps = 149,	.phtw_reg =  0x42 },
	{ .mbps = 169,	.phtw_reg =  0x04 },
	{ .mbps = 179,	.phtw_reg =  0x24 },
	{ .mbps = 199,	.phtw_reg =  0x44 },
	{ .mbps = 219,	.phtw_reg =  0x06 },
	{ .mbps = 239,	.phtw_reg =  0x26 },
	{ .mbps = 249,	.phtw_reg =  0x46 },
	{ .mbps = 269,	.phtw_reg =  0x08 },
	{ .mbps = 299,	.phtw_reg =  0x28 },
	{ .mbps = 329,	.phtw_reg =  0x0a },
	{ .mbps = 359,	.phtw_reg =  0x2a },
	{ .mbps = 399,	.phtw_reg =  0x4a },
	{ .mbps = 449,	.phtw_reg =  0x0c },
	{ .mbps = 499,	.phtw_reg =  0x2c },
	{ .mbps = 549,	.phtw_reg =  0x0e },
	{ .mbps = 599,	.phtw_reg =  0x2e },
	{ .mbps = 649,	.phtw_reg =  0x10 },
	{ .mbps = 699,	.phtw_reg =  0x30 },
	{ .mbps = 749,	.phtw_reg =  0x12 },
	{ .mbps = 799,	.phtw_reg =  0x32 },
	{ .mbps = 849,	.phtw_reg =  0x52 },
	{ .mbps = 899,	.phtw_reg =  0x72 },
	{ .mbps = 949,	.phtw_reg =  0x14 },
	{ .mbps = 999,	.phtw_reg =  0x34 },
	{ .mbps = 1049,	.phtw_reg =  0x54 },
	{ .mbps = 1099,	.phtw_reg =  0x74 },
	{ .mbps = 1149,	.phtw_reg =  0x16 },
	{ .mbps = 1199,	.phtw_reg =  0x36 },
	{ .mbps = 1249,	.phtw_reg =  0x56 },
	{ .mbps = 1299,	.phtw_reg =  0x76 },
	{ .mbps = 1349,	.phtw_reg =  0x18 },
	{ .mbps = 1399,	.phtw_reg =  0x38 },
	{ .mbps = 1449,	.phtw_reg =  0x58 },
	{ .mbps = 1500,	.phtw_reg =  0x78 },
	/* guard */
	{ .mbps =   0,	.phtw_reg = 0x00 },
};

static const struct phtw_freqrange phtw_h3_m3n[] = {
	{ .mbps =   80,	.phtw_reg = 0x018601f1 },
	{ .mbps =   90,	.phtw_reg = 0x018601f1 },
	{ .mbps =  100,	.phtw_reg = 0x018701f1 },
	{ .mbps =  110,	.phtw_reg = 0x018701f1 },
	{ .mbps =  120,	.phtw_reg = 0x018801f1 },
	{ .mbps =  130,	.phtw_reg = 0x018801f1 },
	{ .mbps =  140,	.phtw_reg = 0x018901f1 },
	{ .mbps =  150,	.phtw_reg = 0x018901f1 },
	{ .mbps =  160,	.phtw_reg = 0x018a01f1 },
	{ .mbps =  170,	.phtw_reg = 0x018a01f1 },
	{ .mbps =  180,	.phtw_reg = 0x018b01f1 },
	{ .mbps =  190,	.phtw_reg = 0x018b01f1 },
	{ .mbps =  205,	.phtw_reg = 0x018c01f1 },
	{ .mbps =  220,	.phtw_reg = 0x018d01f1 },
	{ .mbps =  235,	.phtw_reg = 0x018e01f1 },
	{ .mbps =  250,	.phtw_reg = 0x018e01f1 },
	/* guard */
	{ .mbps =   0,	.phtw_reg = 0x00 },
};

/* PHY ESC Error Monitor */
#define PHEERM_REG			0x74

/* PHY Clock Lane Monitor */
#define PHCLM_REG			0x78

/* PHY Data Lane Monitor */
#define PHDLM_REG			0x7c

/* CSI0CLK Frequency Configuration Preset Register */
#define CSI0CLKFCPR_REG			0x260
#define CSI0CLKFREQRANGE(n)		((n & 0x3f) << 16)

/* device information */
#define R8A77990			BIT(0)

struct rcar_csi2_format {
	unsigned int code;
	unsigned int datatype;
	unsigned int bpp;
};

static const struct rcar_csi2_format rcar_csi2_formats[] = {
	{ .code = MEDIA_BUS_FMT_RGB888_1X24,	.datatype = 0x24, .bpp = 24 },
	{ .code = MEDIA_BUS_FMT_UYVY8_1X16,	.datatype = 0x1e, .bpp = 16 },
	{ .code = MEDIA_BUS_FMT_UYVY8_2X8,	.datatype = 0x1e, .bpp = 16 },
	{ .code = MEDIA_BUS_FMT_YUYV10_2X10,	.datatype = 0x1e, .bpp = 16 },
};

static const struct rcar_csi2_format *rcar_csi2_code_to_fmt(unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rcar_csi2_formats); i++)
		if (rcar_csi2_formats[i].code == code)
			return rcar_csi2_formats + i;

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
	const struct phypll_hsfreqrange *hsfreqrange;
	const struct phtw_freqrange *phtw;
	unsigned int csi0clkfreqrange;
	bool clear_ulps;
	bool have_phtw;
	bool phtw_testin;
	u32 device;
};

struct rcar_csi2 {
	struct device *dev;
	void __iomem *base;
	const struct rcar_csi2_info *info;

	struct v4l2_subdev subdev;
	struct media_pad pads[NR_OF_RCAR_CSI2_PAD];

	struct v4l2_async_notifier notifier;
	struct v4l2_async_subdev remote;

	struct v4l2_mbus_framefmt mf;

	struct mutex lock;
	int stream_count;

	unsigned short lanes;
	unsigned char lane_swap[4];
};

static inline struct rcar_csi2 *sd_to_csi2(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rcar_csi2, subdev);
}

static inline struct rcar_csi2 *notifier_to_csi2(struct v4l2_async_notifier *n)
{
	return container_of(n, struct rcar_csi2, notifier);
}

static u32 rcar_csi2_read(struct rcar_csi2 *priv, unsigned int reg)
{
	return ioread32(priv->base + reg);
}

static void rcar_csi2_write(struct rcar_csi2 *priv, unsigned int reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

static void rcar_csi2_reset(struct rcar_csi2 *priv)
{
	rcar_csi2_write(priv, SRST_REG, SRST_SRST);
	usleep_range(100, 150);
	rcar_csi2_write(priv, SRST_REG, 0);
}

static int rcar_csi2_wait_phy_start(struct rcar_csi2 *priv)
{
	int timeout;

	/* Wait for the clock and data lanes to enter LP-11 state. */
	for (timeout = 100; timeout > 0; timeout--) {
		const u32 lane_mask = (1 << priv->lanes) - 1;

		if ((rcar_csi2_read(priv, PHCLM_REG) & 1) == 1 &&
		    (rcar_csi2_read(priv, PHDLM_REG) & lane_mask) == lane_mask)
			return 0;

		msleep(20);
	}

	dev_err(priv->dev, "Timeout waiting for LP-11 state\n");

	return -ETIMEDOUT;
}

static int rcar_csi2_calc_phypll(struct rcar_csi2 *priv, unsigned int bpp,
				 u32 *phypll,
				 u32 *phtw,
				 u32 *testin)
{

	const struct phypll_hsfreqrange *hsfreq;
	const struct phtw_freqrange *phtwfreq;
	struct media_pad *pad, *source_pad;
	struct v4l2_subdev *source = NULL;
	struct v4l2_ctrl *ctrl;
	u64 mbps;

	/* Get remote subdevice */
	pad = &priv->subdev.entity.pads[RCAR_CSI2_SINK];
	source_pad = media_entity_remote_pad(pad);
	if (!source_pad) {
		dev_err(priv->dev, "Could not find remote source pad\n");
		return -ENODEV;
	}

	source = media_entity_to_v4l2_subdev(source_pad->entity);

	dev_dbg(priv->dev, "Using source %s pad: %u\n", source->name,
		source_pad->index);

	/* Read the pixel rate control from remote */
	ctrl = v4l2_ctrl_find(source->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		dev_err(priv->dev, "no link freq control in subdev %s\n",
			source->name);
		return -EINVAL;
	}

	/* Calculate the phypll */
	mbps = v4l2_ctrl_g_ctrl_int64(ctrl) * bpp;

	/* Vblank's margin is 1.05 times of the horizontal size */
	mbps = div_u64(mbps * 105, 100);

	/* Hblank's margin is 1.13 times of the vertical size */
	mbps = div_u64(mbps * 113, 100);

	do_div(mbps, priv->lanes * 1000000);

	if (priv->info->phtw_testin) {
		for (phtwfreq = priv->info->phtw; phtwfreq->mbps != 0;
		     phtwfreq++)
			if (phtwfreq->mbps >= mbps)
				break;

		if (!phtwfreq->mbps) {
			dev_err(priv->dev, "Unsupported PHY speed (%llu Mbps)",
				mbps);
			return -ERANGE;
		}
		*testin = PHTW_TESTDIN_DATA(phtwfreq->phtw_reg);

		dev_dbg(priv->dev, "TESTDIN_DATA requested %llu got %u Mbps\n",
			mbps, phtwfreq->mbps);

		return 0;
	}

	for (hsfreq = priv->info->hsfreqrange; hsfreq->mbps != 0; hsfreq++)
		if (hsfreq->mbps >= mbps)
			break;

	if (!hsfreq->mbps) {
		dev_err(priv->dev, "Unsupported PHY speed (%llu Mbps)", mbps);
		return -ERANGE;
	}

	dev_dbg(priv->dev, "PHY HSFREQRANGE requested %llu got %u Mbps\n", mbps,
		hsfreq->mbps);

	if (!priv->info->phtw) {
		*phypll = PHYPLL_HSFREQRANGE(hsfreq->reg);
		return 0;
	}

	for (phtwfreq = priv->info->phtw; phtwfreq->mbps != 0; phtwfreq++)
		if (phtwfreq->mbps >= mbps)
			break;

	*phtw = phtwfreq->phtw_reg;
	*phypll = PHYPLL_HSFREQRANGE(hsfreq->reg);

	return 0;
}

static int rcar_csi2_start(struct rcar_csi2 *priv, struct v4l2_subdev *nextsd)
{
	const struct rcar_csi2_format *format;
	u32 phycnt, phypll = 0, tmp, phtw = 0, fld = 0, testin = 0;
	u32 vcdt = 0, vcdt2 = 0;
	unsigned int i;
	int ret;
	v4l2_std_id std = 0;

	dev_dbg(priv->dev, "Input size (%ux%u%c)\n",
		priv->mf.width, priv->mf.height,
		priv->mf.field == V4L2_FIELD_NONE ? 'p' : 'i');

	/* Code is validated in set_ftm */
	format = rcar_csi2_code_to_fmt(priv->mf.code);

	ret = v4l2_subdev_call(nextsd, video, g_std, &std);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	if (priv->mf.field != V4L2_FIELD_NONE) {
		if (std & V4L2_STD_525_60)
			fld = FLD_FLD_NUM(2);
		else
			fld = FLD_FLD_NUM(1);
	}

	/*
	 * Enable all Virtual Channels
	 *
	 * NOTE: It's not possible to get individual datatype for each
	 *       source virtual channel. Once this is possible in V4L2
	 *       it should be used here.
	 */
	for (i = 0; i < 4; i++) {
		tmp = VCDT_SEL_VC(i) | VCDT_VCDTN_EN | VCDT_SEL_DTN_ON |
			VCDT_SEL_DT(format->datatype);

		/* Store in correct reg and offset */
		if (i < 2)
			vcdt |= tmp << ((i % 2) * 16);
		else
			vcdt2 |= tmp << ((i % 2) * 16);
	}

	switch (priv->lanes) {
	case 1:
		phycnt = PHYCNT_ENABLECLK | PHYCNT_ENABLE_0;
		break;
	case 2:
		phycnt = PHYCNT_ENABLECLK | PHYCNT_ENABLE_1 | PHYCNT_ENABLE_0;
		break;
	case 4:
		phycnt = PHYCNT_ENABLECLK | PHYCNT_ENABLE_3 | PHYCNT_ENABLE_2 |
			PHYCNT_ENABLE_1 | PHYCNT_ENABLE_0;
		break;
	default:
		return -EINVAL;
	}

	ret = rcar_csi2_calc_phypll(priv, format->bpp, &phypll, &phtw,
				    &testin);
	if (ret)
		return ret;

	/* Init */
	rcar_csi2_write(priv, TREF_REG, TREF_TREF);
	rcar_csi2_reset(priv);
	rcar_csi2_write(priv, PHTC_REG, 0);

	/* Configure */
	rcar_csi2_write(priv, VCDT_REG, vcdt);
	if (priv->info->device != R8A77990)
		rcar_csi2_write(priv, VCDT2_REG, vcdt2);
	/* Lanes are zero indexed */
	rcar_csi2_write(priv, LSWAP_REG,
			LSWAP_L0SEL(priv->lane_swap[0] - 1) |
			LSWAP_L1SEL(priv->lane_swap[1] - 1) |
			LSWAP_L2SEL(priv->lane_swap[2] - 1) |
			LSWAP_L3SEL(priv->lane_swap[3] - 1));

	if (priv->info->have_phtw) {
		/*
		 * This is for H3 ES2.0 and M3N ES1.0
		 */
		rcar_csi2_write(priv, PHTW_REG, 0x01cc01e2);
		rcar_csi2_write(priv, PHTW_REG, 0x010101e3);
		rcar_csi2_write(priv, PHTW_REG, 0x011101e4);
		rcar_csi2_write(priv, PHTW_REG, 0x010101e5);
		rcar_csi2_write(priv, PHTW_REG, 0x01100104);

		if (phtw) {
			rcar_csi2_write(priv, PHTW_REG, 0x01390105);
			rcar_csi2_write(priv, PHTW_REG, phtw);
		}
		rcar_csi2_write(priv, PHTW_REG, 0x01380108);
		rcar_csi2_write(priv, PHTW_REG, 0x01010100);
		rcar_csi2_write(priv, PHTW_REG, 0x014b01ac);
		rcar_csi2_write(priv, PHTW_REG, 0x01030100);
		rcar_csi2_write(priv, PHTW_REG, 0x01800107);
	}

	/* Start */
	if (priv->info->phtw_testin)
		rcar_csi2_write(priv, PHTW_REG, PHTW_DWEN | PHTW_CWEN |
				PHTW_TESTDIN_CODE | testin);
	else
		rcar_csi2_write(priv, PHYPLL_REG, phypll);

	/* Set frequency range if we have it */
	if (priv->info->csi0clkfreqrange)
		rcar_csi2_write(priv, CSI0CLKFCPR_REG,
				CSI0CLKFREQRANGE(priv->info->csi0clkfreqrange));

	rcar_csi2_write(priv, PHYCNT_REG, phycnt);
	rcar_csi2_write(priv, LINKCNT_REG, LINKCNT_MONITOR_EN |
			LINKCNT_REG_MONI_PACT_EN | LINKCNT_ICLK_NONSTOP);

	if (priv->mf.field != V4L2_FIELD_NONE)
		rcar_csi2_write(priv, FLD_REG, fld | FLD_FLD_EN4 |
				FLD_FLD_EN3 | FLD_FLD_EN2 | FLD_FLD_EN);
	else
		rcar_csi2_write(priv, FLD_REG, 0);

	rcar_csi2_write(priv, PHYCNT_REG, phycnt | PHYCNT_SHUTDOWNZ);
	rcar_csi2_write(priv, PHYCNT_REG, phycnt | PHYCNT_SHUTDOWNZ |
			PHYCNT_RSTZ);

	ret = rcar_csi2_wait_phy_start(priv);
	if (ret)
		return ret;

	/* Clear Ultra Low Power interrupt */
	if (priv->info->clear_ulps)
		rcar_csi2_write(priv, INTSTATE_REG,
				INTSTATE_INT_ULPS_START |
				INTSTATE_INT_ULPS_END);
	return ret;
}

static void rcar_csi2_stop(struct rcar_csi2 *priv)
{
	rcar_csi2_write(priv, PHYCNT_REG, 0);
	iowrite32(PHTC_TESTCLR, priv->base + PHTC_REG);

	rcar_csi2_reset(priv);
}

static int rcar_csi2_sd_info(struct rcar_csi2 *priv, struct v4l2_subdev **sd)
{
	struct media_pad *pad;

	pad = media_entity_remote_pad(&priv->pads[RCAR_CSI2_SINK]);
	if (!pad) {
		dev_err(priv->dev, "Could not find remote pad\n");
		return -ENODEV;
	}

	*sd = media_entity_to_v4l2_subdev(pad->entity);
	if (!*sd) {
		dev_err(priv->dev, "Could not find remote subdevice\n");
		return -ENODEV;
	}

	return 0;
}

static int rcar_csi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);
	struct v4l2_subdev *nextsd;
	int ret;

	mutex_lock(&priv->lock);

	ret = rcar_csi2_sd_info(priv, &nextsd);
	if (ret)
		goto out;

	if (enable && priv->stream_count == 0) {
		pm_runtime_get_sync(priv->dev);

		ret =  rcar_csi2_start(priv, nextsd);
		if (ret) {
			pm_runtime_put(priv->dev);
			goto out;
		}

		ret = v4l2_subdev_call(nextsd, video, s_stream, 1);
		if (ret) {
			rcar_csi2_stop(priv);
			pm_runtime_put(priv->dev);
			goto out;
		}
	} else if (!enable && priv->stream_count == 1) {
		rcar_csi2_stop(priv);
		ret = v4l2_subdev_call(nextsd, video, s_stream, 0);
		pm_runtime_put(priv->dev);
	}

	priv->stream_count += enable ? 1 : -1;
out:
	mutex_unlock(&priv->lock);

	return ret;
}

static int rcar_csi2_set_pad_format(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_format *format)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);
	struct v4l2_mbus_framefmt *framefmt;

	if (!rcar_csi2_code_to_fmt(format->format.code))
		return -EINVAL;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		priv->mf = format->format;
	} else {
		framefmt = v4l2_subdev_get_try_format(sd, cfg, 0);
		*framefmt = format->format;
	}

	return 0;
}

static int rcar_csi2_get_pad_format(struct v4l2_subdev *sd,
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
	.s_stream = rcar_csi2_s_stream,
};

static const struct v4l2_subdev_pad_ops rcar_csi2_pad_ops = {
	.set_fmt = rcar_csi2_set_pad_format,
	.get_fmt = rcar_csi2_get_pad_format,
};

static const struct v4l2_subdev_ops rcar_csi2_subdev_ops = {
	.video	= &rcar_csi2_video_ops,
	.pad	= &rcar_csi2_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Async and registered of subdevices and links
 */

static int rcar_csi2_notify_bound(struct v4l2_async_notifier *notifier,
				   struct v4l2_subdev *subdev,
				   struct v4l2_async_subdev *asd)
{
	struct rcar_csi2 *priv = notifier_to_csi2(notifier);
	int pad;

	v4l2_set_subdev_hostdata(subdev, priv);

	pad = media_entity_get_fwnode_pad(&subdev->entity,
					  asd->match.fwnode.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		dev_err(priv->dev, "Failed to find pad for %s\n", subdev->name);
		return pad;
	}

	dev_dbg(priv->dev, "Bound %s pad: %d\n", subdev->name, pad);

	return media_create_pad_link(&subdev->entity, pad,
				     &priv->subdev.entity, 0,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static const struct v4l2_async_notifier_operations rcar_csi2_notify_ops = {
	.bound = rcar_csi2_notify_bound,
};

static int rcar_csi2_parse_v4l2(struct rcar_csi2 *priv,
				struct v4l2_fwnode_endpoint *vep)
{
	unsigned int i;

	/* Only port 0 enpoint 0 is valid */
	if (vep->base.port || vep->base.id)
		return -ENOTCONN;

	if (vep->bus_type != V4L2_MBUS_CSI2) {
		dev_err(priv->dev, "Unsupported bus: 0x%x\n", vep->bus_type);
		return -EINVAL;
	}

	priv->lanes = vep->bus.mipi_csi2.num_data_lanes;
	if (priv->lanes != 1 && priv->lanes != 2 && priv->lanes != 4) {
		dev_err(priv->dev, "Unsupported number of data-lanes: %d\n",
			priv->lanes);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(priv->lane_swap); i++) {
		priv->lane_swap[i] = i < priv->lanes ?
			vep->bus.mipi_csi2.data_lanes[i] : i;

		/* Check for valid lane number */
		if (priv->lane_swap[i] < 1 || priv->lane_swap[i] > 4) {
			dev_err(priv->dev, "data-lanes must be in 1-4 range\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int rcar_csi2_parse_dt(struct rcar_csi2 *priv)
{
	struct device_node *ep;
	struct v4l2_fwnode_endpoint v4l2_ep;
	int ret;

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

	ret = rcar_csi2_parse_v4l2(priv, &v4l2_ep);
	if (ret)
		return ret;

	priv->remote.match.fwnode.fwnode =
		fwnode_graph_get_remote_endpoint(of_fwnode_handle(ep));
	priv->remote.match_type = V4L2_ASYNC_MATCH_FWNODE;

	of_node_put(ep);

	priv->notifier.subdevs = devm_kzalloc(priv->dev,
					      sizeof(*priv->notifier.subdevs),
					      GFP_KERNEL);
	if (priv->notifier.subdevs == NULL)
		return -ENOMEM;

	priv->notifier.num_subdevs = 1;
	priv->notifier.subdevs[0] = &priv->remote;
	priv->notifier.ops = &rcar_csi2_notify_ops;

	dev_dbg(priv->dev, "Found '%pOF'\n",
		to_of_node(priv->remote.match.fwnode.fwnode));

	return v4l2_async_subdev_notifier_register(&priv->subdev,
						   &priv->notifier);
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static const struct media_entity_operations rcar_csi2_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int rcar_csi2_probe_resources(struct rcar_csi2 *priv,
				     struct platform_device *pdev)
{
	struct resource *res;
	int irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	return 0;
}

static const struct rcar_csi2_info rcar_csi2_info_r8a7795 = {
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.phtw = phtw_h3_m3n,
	.clear_ulps = true,
	.have_phtw = true,
	.csi0clkfreqrange = 0x20,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7795es2 = {
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.clear_ulps = true,
	.have_phtw = true,
	.csi0clkfreqrange = 0x20,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7795es1 = {
	.hsfreqrange = hsfreqrange_m3w_h3es1,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7796 = {
	.hsfreqrange = hsfreqrange_m3w_h3es1,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77965 = {
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.phtw = phtw_h3_m3n,
	.clear_ulps = true,
	.have_phtw = true,
	.csi0clkfreqrange = 0x20,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77990 = {
	.phtw_testin = true,
	.phtw = phtw_e3_v3m,
	.device = R8A77990,
};

static const struct of_device_id rcar_csi2_of_table[] = {
	{
		.compatible = "renesas,r8a7795-csi2",
		.data = &rcar_csi2_info_r8a7795,
	},
	{
		.compatible = "renesas,r8a7796-csi2",
		.data = &rcar_csi2_info_r8a7796,
	},
	{
		.compatible = "renesas,r8a77965-csi2",
		.data = &rcar_csi2_info_r8a77965,
	},
	{
		.compatible = "renesas,r8a77990-csi2",
		.data = &rcar_csi2_info_r8a77990,
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
	{ /* sentinel */}
};

static int rcar_csi2_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *attr;
	struct rcar_csi2 *priv;
	unsigned int i;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info = of_device_get_match_data(&pdev->dev);

	/* r8a7795 ES1.x behaves different then ES2.0+ but no own compat.
	 * PHTW register setting is different between r8a7795 ES2.0 and ES3.0.
	 */
	attr = soc_device_match(r8a7795);
	if (attr)
		priv->info = attr->data;

	priv->dev = &pdev->dev;

	mutex_init(&priv->lock);
	priv->stream_count = 0;

	ret = rcar_csi2_probe_resources(priv, pdev);
	if (ret) {
		dev_err(priv->dev, "Failed to get resources\n");
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	ret = rcar_csi2_parse_dt(priv);
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

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		goto error;

	pm_runtime_enable(&pdev->dev);

	dev_info(priv->dev, "%d lanes found\n", priv->lanes);

	return 0;

error:
	v4l2_async_notifier_cleanup(&priv->notifier);

	return ret;
}

static int rcar_csi2_remove(struct platform_device *pdev)
{
	struct rcar_csi2 *priv = platform_get_drvdata(pdev);

	v4l2_async_notifier_cleanup(&priv->notifier);
	v4l2_async_notifier_unregister(&priv->notifier);
	v4l2_async_unregister_subdev(&priv->subdev);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rcar_csi2_suspend(struct device *dev)
{
	struct rcar_csi2 *priv = dev_get_drvdata(dev);

	pm_runtime_put(priv->dev);

	return 0;
}

static int rcar_csi2_resume(struct device *dev)
{
	struct rcar_csi2 *priv = dev_get_drvdata(dev);

	pm_runtime_get_sync(priv->dev);

	return 0;
}

static const struct dev_pm_ops rcar_csi2_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(rcar_csi2_suspend, rcar_csi2_resume)
};
#endif

static struct platform_driver __refdata rcar_csi2_pdrv = {
	.remove	= rcar_csi2_remove,
	.probe	= rcar_csi2_probe,
	.driver	= {
		.name	= "rcar-csi2",
#ifdef CONFIG_PM_SLEEP
		.pm = &rcar_csi2_pm_ops,
#endif
		.of_match_table	= of_match_ptr(rcar_csi2_of_table),
	},
};

module_platform_driver(rcar_csi2_pdrv);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Renesas R-Car MIPI CSI-2 receiver");
MODULE_LICENSE("GPL v2");
