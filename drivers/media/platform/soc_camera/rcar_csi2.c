/*
 * drivers/media/platform/soc_camera/rcar_csi2.c
 *     This file is the driver for the R-Car MIPI CSI-2 unit.
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * This file is based on the drivers/media/platform/soc_camera/sh_mobile_csi2.c
 *
 * Driver for the SH-Mobile MIPI CSI-2 unit
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/module.h>

#include <media/sh_mobile_ceu.h>
#include <media/rcar_csi2.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#include <media/v4l2-of.h>

#define DRV_NAME "rcar_csi2"

#define RCAR_CSI2_TREF		0x00
#define RCAR_CSI2_SRST		0x04
#define RCAR_CSI2_PHYCNT	0x08
#define RCAR_CSI2_CHKSUM	0x0C
#define RCAR_CSI2_VCDT		0x10

#define RCAR_CSI2_VCDT2			0x14 /* Channel Data Type Select */
#define RCAR_CSI2_FRDT			0x18 /* Frame Data Type Select */
#define RCAR_CSI2_FLD			0x1C /* Field Detection Control */
#define RCAR_CSI2_ASTBY			0x20 /* Automatic standby control */
#define RCAR_CSI2_LNGDT0		0x28
#define RCAR_CSI2_LNGDT1		0x2C
#define RCAR_CSI2_INTEN			0x30
#define RCAR_CSI2_INTCLOSE		0x34
#define RCAR_CSI2_INTSTATE		0x38
#define RCAR_CSI2_INTERRSTATE	0x3C

#define RCAR_CSI2_SHPDAT		0x40
#define RCAR_CSI2_SHPCNT		0x44

#define RCAR_CSI2_LINKCNT		0x48
#define RCAR_CSI2_LSWAP			0x4C
#define RCAR_CSI2_PHTC			0x58
#define RCAR_CSI2_PHYPLL		0x68

#define RCAR_CSI2_PHEERM		0x74
#define RCAR_CSI2_PHCLM			0x78
#define RCAR_CSI2_PHDLM			0x7C

#define RCAR_CSI2_PHYCNT_SHUTDOWNZ	(1 << 17)
#define RCAR_CSI2_PHYCNT_RSTZ		(1 << 16)
#define RCAR_CSI2_PHYCNT_ENABLECLK	(1 << 4)
#define RCAR_CSI2_PHYCNT_ENABLE_3	(1 << 3)
#define RCAR_CSI2_PHYCNT_ENABLE_2	(1 << 2)
#define RCAR_CSI2_PHYCNT_ENABLE_1	(1 << 1)
#define RCAR_CSI2_PHYCNT_ENABLE_0	(1 << 0)

#define RCAR_CSI2_VCDT_VCDTN_EN		(1 << 15)
#define RCAR_CSI2_VCDT_SEL_VCN		(1 << 8)
#define RCAR_CSI2_VCDT_SEL_DTN_ON	(1 << 6)
#define RCAR_CSI2_VCDT_SEL_DTN		(1 << 0)

#define RCAR_CSI2_LINKCNT_MONITOR_EN		(1 << 31)
#define RCAR_CSI2_LINKCNT_REG_MONI_PACT_EN	(1 << 25)

#define RCAR_CSI2_LSWAP_L3SEL_PLANE0		(0 << 6)
#define RCAR_CSI2_LSWAP_L3SEL_PLANE1		(1 << 6)
#define RCAR_CSI2_LSWAP_L3SEL_PLANE2		(2 << 6)
#define RCAR_CSI2_LSWAP_L3SEL_PLANE3		(3 << 6)

#define RCAR_CSI2_LSWAP_L2SEL_PLANE0		(0 << 4)
#define RCAR_CSI2_LSWAP_L2SEL_PLANE1		(1 << 4)
#define RCAR_CSI2_LSWAP_L2SEL_PLANE2		(2 << 4)
#define RCAR_CSI2_LSWAP_L2SEL_PLANE3		(3 << 4)

#define RCAR_CSI2_LSWAP_L1SEL_PLANE0		(0 << 2)
#define RCAR_CSI2_LSWAP_L1SEL_PLANE1		(1 << 2)
#define RCAR_CSI2_LSWAP_L1SEL_PLANE2		(2 << 2)
#define RCAR_CSI2_LSWAP_L1SEL_PLANE3		(3 << 2)

#define RCAR_CSI2_LSWAP_L0SEL_PLANE0		(0 << 0)
#define RCAR_CSI2_LSWAP_L0SEL_PLANE1		(1 << 0)
#define RCAR_CSI2_LSWAP_L0SEL_PLANE2		(2 << 0)
#define RCAR_CSI2_LSWAP_L0SEL_PLANE3		(3 << 0)

#define RCAR_CSI2_PHTC_TESTCLR				(1 << 0)

/* interrupt status registers */
#define RCAR_CSI2_INTSTATE_EBD_CH1			(1 << 29)
#define RCAR_CSI2_INTSTATE_LESS_THAN_WC		(1 << 28)
#define RCAR_CSI2_INTSTATE_AFIFO_OF			(1 << 27)
#define RCAR_CSI2_INTSTATE_VD4_START		(1 << 26)
#define RCAR_CSI2_INTSTATE_VD4_END			(1 << 25)
#define RCAR_CSI2_INTSTATE_VD3_START		(1 << 24)
#define RCAR_CSI2_INTSTATE_VD3_END			(1 << 23)
#define RCAR_CSI2_INTSTATE_VD2_START		(1 << 22)
#define RCAR_CSI2_INTSTATE_VD2_END			(1 << 21)
#define RCAR_CSI2_INTSTATE_VD1_START		(1 << 20)
#define RCAR_CSI2_INTSTATE_VD1_END			(1 << 19)
#define RCAR_CSI2_INTSTATE_SHP				(1 << 18)
#define RCAR_CSI2_INTSTATE_FSFE				(1 << 17)
#define RCAR_CSI2_INTSTATE_LNP				(1 << 16)
#define RCAR_CSI2_INTSTATE_CRC_ERR			(1 << 15)
#define RCAR_CSI2_INTSTATE_HD_WC_ZERO		(1 << 14)
#define RCAR_CSI2_INTSTATE_FRM_SEQ_ERR1		(1 << 13)
#define RCAR_CSI2_INTSTATE_FRM_SEQ_ERR2		(1 << 12)
#define RCAR_CSI2_INTSTATE_ECC_ERR			(1 << 11)
#define RCAR_CSI2_INTSTATE_ECC_CRCT_ERR		(1 << 10)
#define RCAR_CSI2_INTSTATE_LPDT_START		(1 << 9)
#define RCAR_CSI2_INTSTATE_LPDT_END			(1 << 8)
#define RCAR_CSI2_INTSTATE_ULPS_START		(1 << 7)
#define RCAR_CSI2_INTSTATE_ULPS_END			(1 << 6)
#define RCAR_CSI2_INTSTATE_RESERVED			(1 << 5)
#define RCAR_CSI2_INTSTATE_ERRSOTHS			(1 << 4)
#define RCAR_CSI2_INTSTATE_ERRSOTSYNCCHS	(1 << 3)
#define RCAR_CSI2_INTSTATE_ERRESC			(1 << 2)
#define RCAR_CSI2_INTSTATE_ERRSYNCESC		(1 << 1)
#define RCAR_CSI2_INTSTATE_ERRCONTROL		(1 << 0)

/* monitoring registers of interrupt error status */
#define RCAR_CSI2_INTSTATE_ECC_ERR			(1 << 11)
#define RCAR_CSI2_INTSTATE_ECC_CRCT_ERR		(1 << 10)
#define RCAR_CSI2_INTSTATE_LPDT_START		(1 << 9)
#define RCAR_CSI2_INTSTATE_LPDT_END			(1 << 8)
#define RCAR_CSI2_INTSTATE_ULPS_START		(1 << 7)
#define RCAR_CSI2_INTSTATE_ULPS_END			(1 << 6)
#define RCAR_CSI2_INTSTATE_RESERVED			(1 << 5)
#define RCAR_CSI2_INTSTATE_ERRSOTHS			(1 << 4)
#define RCAR_CSI2_INTSTATE_ERRSOTSYNCCHS	(1 << 3)
#define RCAR_CSI2_INTSTATE_ERRESC			(1 << 2)
#define RCAR_CSI2_INTSTATE_ERRSYNCESC		(1 << 1)
#define RCAR_CSI2_INTSTATE_ERRCONTROL		(1 << 0)

enum chip_id {
	RCAR_GEN3,
	RCAR_GEN2,
};

enum decoder_input_interface {
	DECODER_INPUT_INTERFACE_RGB888,
	DECODER_INPUT_INTERFACE_YCBCR422,
	DECODER_INPUT_INTERFACE_NONE,
};

/**
 * struct rcar_csi2_link_config - Describes rcar_csi2 hardware configuration
 * @input_colorspace:		The input colorspace (RGB, YUV444, YUV422)
 */
struct rcar_csi2_link_config {
	enum decoder_input_interface input_interface;
	unsigned char lanes;
};

#define INIT_RCAR_CSI2_LINK_CONFIG(m) \
{	\
	m.input_interface = DECODER_INPUT_INTERFACE_NONE; \
	m.lanes = 0; \
}

struct rcar_csi_irq_counter_log {
	unsigned long crc_err;
};

struct rcar_csi2 {
	struct v4l2_subdev		subdev;
	unsigned int			irq;
	unsigned long			mipi_flags;
	void __iomem			*base;
	struct platform_device		*pdev;
	struct rcar_csi2_client_config	*client;

	spinlock_t			lock;
};

static void rcar_csi2_hwinit(struct rcar_csi2 *priv);
#if 1 /* FIXME */
static int rcar_csi2_hwinit_tmp(struct rcar_csi2 *priv,
	struct rcar_csi2_link_config *link_config);
#endif

#define RCAR_CSI_80MBPS		0
#define RCAR_CSI_90MBPS		1
#define RCAR_CSI_100MBPS	2
#define RCAR_CSI_110MBPS	3
#define RCAR_CSI_120MBPS	4
#define RCAR_CSI_130MBPS	5
#define RCAR_CSI_140MBPS	6
#define RCAR_CSI_150MBPS	7
#define RCAR_CSI_160MBPS	8
#define RCAR_CSI_170MBPS	9
#define RCAR_CSI_180MBPS	10
#define RCAR_CSI_190MBPS	11
#define RCAR_CSI_205MBPS	12
#define RCAR_CSI_220MBPS	13
#define RCAR_CSI_235MBPS	14
#define RCAR_CSI_250MBPS	15
#define RCAR_CSI_275MBPS	16
#define RCAR_CSI_300MBPS	17
#define RCAR_CSI_325MBPS	18
#define RCAR_CSI_350MBPS	19
#define RCAR_CSI_400MBPS	20
#define RCAR_CSI_450MBPS	21
#define RCAR_CSI_500MBPS	22
#define RCAR_CSI_550MBPS	23
#define RCAR_CSI_600MBPS	24
#define RCAR_CSI_650MBPS	25
#define RCAR_CSI_700MBPS	26
#define RCAR_CSI_750MBPS	27
#define RCAR_CSI_800MBPS	28
#define RCAR_CSI_850MBPS	29
#define RCAR_CSI_900MBPS	30
#define RCAR_CSI_950MBPS	31
#define RCAR_CSI_1000MBPS	32
#define RCAR_CSI_1050MBPS	33
#define RCAR_CSI_1100MBPS	34
#define RCAR_CSI_1150MBPS	35
#define RCAR_CSI_1200MBPS	36
#define RCAR_CSI_1250MBPS	37
#define RCAR_CSI_1300MBPS	38
#define RCAR_CSI_1350MBPS	39
#define RCAR_CSI_1400MBPS	40
#define RCAR_CSI_1450MBPS	41
#define RCAR_CSI_1500MBPS	42

static int rcar_csi2_set_phy_freq(struct rcar_csi2 *priv, unsigned char lanes)
{
	const uint32_t const HSFREQRANGE[43] = {
		0x00, 0x10, 0x20, 0x30, 0x01,  /* 0-4   */
		0x11, 0x21, 0x31, 0x02, 0x12,  /* 5-9   */
		0x22, 0x32, 0x03, 0x13, 0x23,  /* 10-14 */
		0x33, 0x04, 0x14, 0x05, 0x15,  /* 15-19 */
		0x25, 0x06, 0x16, 0x07, 0x17,  /* 20-24 */
		0x08, 0x18, 0x09, 0x19, 0x29,  /* 25-29 */
		0x39, 0x0A, 0x1A, 0x2A, 0x3A,  /* 30-34 */
		0x0B, 0x1B, 0x2B, 0x3B, 0x0C,  /* 35-39 */
		0x1C, 0x2C, 0x3C               /* 40-42 */
	};

	uint32_t bps_per_lane = 0;

    /* [FIXME] */
	switch (lanes) {
	case 1:
		bps_per_lane = RCAR_CSI_400MBPS;
/*		bps_per_lane = CSI_220MBPS; */
		break;
	case 4:
/*		bps_per_lane = CSI_900MBPS; */
		bps_per_lane = RCAR_CSI_190MBPS;
		break;
	default:
		dev_err(NULL, "ERROR: lanes is invalid (%d)\n", lanes);
		return -1;
	}

	iowrite32((HSFREQRANGE[bps_per_lane] << 16),
				priv->base + RCAR_CSI2_PHYPLL);

	return 0;
}

static irqreturn_t rcar_csi2_irq(int irq, void *data)
{
	struct rcar_csi2 *priv = data;
	u32 int_status;
	unsigned int handled = 0;

	spin_lock(&priv->lock);

	int_status = ioread32(priv->base + RCAR_CSI2_INTSTATE);
	if (!int_status)
		goto done;
	/* ack interrupts */
	iowrite32(int_status, priv->base + RCAR_CSI2_INTSTATE);
	handled = 1;

done:
	spin_unlock(&priv->lock);

	return IRQ_RETVAL(handled);

}

static int rcar_csi2_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct rcar_csi2 *priv = container_of(sd, struct rcar_csi2, subdev);
	struct rcar_csi2_pdata *pdata = priv->pdev->dev.platform_data;

	/* FIXME */
	dev_dbg(sd->v4l2_dev->dev, "%s\n", __func__);

	switch (pdata->type) {
	case RCAR_CSI2_CSI2X:
		switch (code->code) {
		case MEDIA_BUS_FMT_UYVY8_2X8:		/* YUV422 */
		case MEDIA_BUS_FMT_YUYV8_1_5X8:		/* YUV420 */
		case MEDIA_BUS_FMT_Y8_1X8:		/* RAW8 */
		case MEDIA_BUS_FMT_SBGGR8_1X8:
		case MEDIA_BUS_FMT_SGRBG8_1X8:
			break;
		default:
		/* All MIPI CSI-2 devices must support one of primary formats */
			code->code = MEDIA_BUS_FMT_YUYV8_2X8;
		}
		break;
	case RCAR_CSI2_CSI4X:
		switch (code->code) {
		case MEDIA_BUS_FMT_RGB888_1X24:
		/* FIXME */
		case MEDIA_BUS_FMT_Y8_1X8:		/* RAW8 */
		case MEDIA_BUS_FMT_SBGGR8_1X8:
		case MEDIA_BUS_FMT_SGRBG8_1X8:
			break;
		default:
		/* All MIPI CSI-2 devices must support one of primary formats */
			code->code = MEDIA_BUS_FMT_SBGGR8_1X8;
		}
		break;
	}

	return 0;
}

static int rcar_csi2_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct rcar_csi2 *priv = container_of(sd, struct rcar_csi2, subdev);
	struct rcar_csi2_pdata *pdata = priv->pdev->dev.platform_data;
	struct v4l2_mbus_framefmt *mf = &format->format;

	switch (pdata->type) {
	case RCAR_CSI2_CSI2X:
		switch (mf->code) {
		case MEDIA_BUS_FMT_UYVY8_2X8:		/* YUV422 */
		case MEDIA_BUS_FMT_YUYV8_1_5X8:		/* YUV420 */
		case MEDIA_BUS_FMT_Y8_1X8:		/* RAW8 */
		case MEDIA_BUS_FMT_SBGGR8_1X8:
		case MEDIA_BUS_FMT_SGRBG8_1X8:
			break;
		default:
			/* All MIPI CSI-2 devices must support
		      one of primary formats */
			mf->code = MEDIA_BUS_FMT_YUYV8_2X8;
		}
		break;
	case RCAR_CSI2_CSI4X:
		switch (mf->code) {
		case MEDIA_BUS_FMT_RGB888_1X24:
		/* FIXME */
		case MEDIA_BUS_FMT_Y8_1X8:		/* RAW8 */
		case MEDIA_BUS_FMT_SBGGR8_1X8:
		case MEDIA_BUS_FMT_SGRBG8_1X8:
			break;
		default:
			/* All MIPI CSI-2 devices must support
			  one of primary formats */
			mf->code = MEDIA_BUS_FMT_SBGGR8_1X8;
		}
		break;
	}

	dev_dbg(sd->v4l2_dev->dev, "%s(%u)\n", __func__, mf->code);

	return 0;
}

static int rcar_csi2_set_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct rcar_csi2 *priv = container_of(sd, struct rcar_csi2, subdev);
	u32 tmp = (priv->client->channel & 3) << 8;
	struct v4l2_mbus_framefmt *mf = &format->format;

	dev_dbg(sd->v4l2_dev->dev, "%s(%u)\n", __func__, mf->code);

	switch (mf->code) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
		tmp |= 0x1e;	/* YUV422 8 bit */
		break;
	case MEDIA_BUS_FMT_YUYV8_1_5X8:
		tmp |= 0x18;	/* YUV420 8 bit */
		break;
	case MEDIA_BUS_FMT_RGB555_2X8_PADHI_BE:
		tmp |= 0x21;	/* RGB555 */
		break;
	case MEDIA_BUS_FMT_RGB565_2X8_BE:
		tmp |= 0x22;	/* RGB565 */
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
		tmp |= 0x24;	/* RGB888 */
		break;
	case MEDIA_BUS_FMT_Y8_1X8:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		tmp |= 0x2a;	/* RAW8 */
		break;
	default:
		return -EINVAL;
	}

	tmp |= (RCAR_CSI2_VCDT_VCDTN_EN | RCAR_CSI2_VCDT_SEL_DTN_ON);

	if (priv->client->phy == RCAR_CSI2_PHY_CSI20)
		tmp = tmp << 16;

	iowrite32(tmp, priv->base + RCAR_CSI2_VCDT);

	return 0;
}

static int rcar_csi2_g_mbus_config(struct v4l2_subdev *sd,
				 struct v4l2_mbus_config *cfg)
{
	struct rcar_csi2 *priv = container_of(sd, struct rcar_csi2, subdev);

	if (!priv->mipi_flags) {
		struct soc_camera_device *icd = v4l2_get_subdev_hostdata(sd);
		struct v4l2_subdev *client_sd = soc_camera_to_subdev(icd);
		struct rcar_csi2_pdata *pdata = priv->pdev->dev.platform_data;
		unsigned long common_flags, csi2_flags;
		struct v4l2_mbus_config client_cfg = {.type = V4L2_MBUS_CSI2,};
		int ret;

		/* Check if we can support this camera */
		csi2_flags = V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
			V4L2_MBUS_CSI2_1_LANE;

		switch (pdata->type) {
		case RCAR_CSI2_CSI2X:
			if (priv->client->lanes != 1)
				csi2_flags |= V4L2_MBUS_CSI2_2_LANE;
			break;
		case RCAR_CSI2_CSI4X:
			switch (priv->client->lanes) {
			default:
				csi2_flags |= V4L2_MBUS_CSI2_4_LANE;
			case 3:
				csi2_flags |= V4L2_MBUS_CSI2_3_LANE;
			case 2:
				csi2_flags |= V4L2_MBUS_CSI2_2_LANE;
			}
		}

		ret = v4l2_subdev_call(client_sd, video, g_mbus_config,
				&client_cfg);
		if (ret == -ENOIOCTLCMD)
			common_flags = csi2_flags;
		else if (!ret)
			common_flags = soc_mbus_config_compatible(&client_cfg,
				csi2_flags);
		else
			common_flags = 0;

		if (!common_flags)
			return -EINVAL;

		/* All good: camera MIPI configuration supported */
		priv->mipi_flags = common_flags;
	}

	if (cfg) {
		cfg->flags = V4L2_MBUS_PCLK_SAMPLE_RISING |
			V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_HIGH |
			V4L2_MBUS_MASTER |
			V4L2_MBUS_DATA_ACTIVE_HIGH;
		cfg->type = V4L2_MBUS_PARALLEL;
	}

	return 0;
}

static int rcar_csi2_s_mbus_config(struct v4l2_subdev *sd,
				 const struct v4l2_mbus_config *cfg)
{
	struct rcar_csi2 *priv = container_of(sd, struct rcar_csi2, subdev);
	struct soc_camera_device *icd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_subdev *client_sd = soc_camera_to_subdev(icd);
	struct v4l2_mbus_config client_cfg = {.type = V4L2_MBUS_CSI2,};
	int ret = rcar_csi2_g_mbus_config(sd, NULL);

	if (ret < 0)
		return ret;

	pm_runtime_get_sync(&priv->pdev->dev);

	rcar_csi2_hwinit(priv);

	client_cfg.flags = priv->mipi_flags;

	return v4l2_subdev_call(client_sd, video, s_mbus_config,
								&client_cfg);
}

static struct v4l2_subdev_video_ops rcar_csi2_subdev_video_ops = {
#if 0 /* FIXME */
	.s_mbus_fmt	= rcar_csi2_s_fmt,
	.try_mbus_fmt	= rcar_csi2_try_fmt,
#endif
	.g_mbus_config	= rcar_csi2_g_mbus_config,
	.s_mbus_config	= rcar_csi2_s_mbus_config,
};

static const struct v4l2_subdev_pad_ops rcar_csi2_pad_ops = {
	.enum_mbus_code = rcar_csi2_enum_mbus_code,
	.set_fmt = rcar_csi2_set_pad_format,
	.get_fmt = rcar_csi2_get_pad_format,
};

/* FIXME */
static void rcar_csi2_clock_init(struct rcar_csi2 *priv)
{
	void __iomem *cpgwrp_reg; /* CPG Write Protect Register */
	void __iomem *cpg_reg;
	uint32_t dataL;

	cpgwrp_reg = ioremap_nocache(0xE6150900, 0x04);

	/* Set CSI0CKCR */
	dataL = 0x0000011F;
/*	dataL = 0x0000010F; */
	writel(~dataL, cpgwrp_reg);
	cpg_reg = ioremap_nocache(0xe615000C, 0x04);
	writel((readl(cpg_reg) & 0xFFFFFE00) | dataL, cpg_reg);
	dataL &= 0xFFFFFEFF;
	writel(~dataL, cpgwrp_reg);
	writel((readl(cpg_reg) & 0xFFFFFE00) | dataL, cpg_reg);

	/* Set CSIREFCKCR */
	dataL = 0x00000107;
/*	dataL = 0x00000103; */
	writel(~dataL, cpgwrp_reg);
	cpg_reg = ioremap_nocache(0xe6150034, 0x04);
	writel((readl(cpg_reg) & 0xFFFFFE00) | dataL, cpg_reg);
	dataL &= 0xFFFFFEFF;
	writel(~dataL, cpgwrp_reg);
	writel(((readl(cpg_reg) & 0xFFFFFE00) | dataL), cpg_reg);
}

#if 1 /* FIXME */
static int rcar_csi2_hwinit_tmp(struct rcar_csi2 *priv,
			struct rcar_csi2_link_config *link_config)
{
	int ret;
	__u32 tmp = 0x10; /* Enable MIPI CSI clock lane */
	__u32 vcdt = 0;
	__u32 vcdt2 = 0;

	pm_runtime_get_sync(&priv->pdev->dev);

	/* initialize the clock of csi2 */
	rcar_csi2_clock_init(priv);

	/* Reflect registers immediately */
	iowrite32(0x00000001, priv->base + RCAR_CSI2_TREF);
	/* reset CSI2 hardware */
	iowrite32(0x00000001, priv->base + RCAR_CSI2_SRST);
	udelay(5);
	iowrite32(0x00000000, priv->base + RCAR_CSI2_SRST);

	iowrite32(0x00000000, priv->base + RCAR_CSI2_PHTC);

	/* setting HS reception frequency */
	{
		switch (link_config->lanes) {
		case 1:
			tmp |= 0x1;
			vcdt |= (0x1e | RCAR_CSI2_VCDT_VCDTN_EN);
				/* YUV422 8 bit */
			break;
		case 4:
			tmp |= 0xF;
			vcdt |= (0x24 | RCAR_CSI2_VCDT_VCDTN_EN);
				/* RGB888 */
			break;
		default:
			dev_err(&priv->pdev->dev,
				"ERROR: link_config->lanes is invalid (%d)\n",
				link_config->lanes);
			return -1;
		}

		/* set PHY frequency */
		ret = rcar_csi2_set_phy_freq(priv, link_config->lanes);
		if (ret < 0)
			return ret;

		/* Enable lanes */
		iowrite32(tmp, priv->base + RCAR_CSI2_PHYCNT);

		/* */
		iowrite32(tmp | RCAR_CSI2_PHYCNT_SHUTDOWNZ,
					priv->base + RCAR_CSI2_PHYCNT);
		iowrite32(tmp | (RCAR_CSI2_PHYCNT_SHUTDOWNZ |
					RCAR_CSI2_PHYCNT_RSTZ),
					priv->base + RCAR_CSI2_PHYCNT);

	}

	tmp = 0x3;
	iowrite32(tmp, priv->base + RCAR_CSI2_CHKSUM);

	iowrite32(vcdt, priv->base + RCAR_CSI2_VCDT);
	iowrite32(vcdt2, priv->base + RCAR_CSI2_VCDT2);

	iowrite32(0x00010000, priv->base + RCAR_CSI2_FRDT);

	udelay(10);

	iowrite32(0x82000000, priv->base + RCAR_CSI2_LINKCNT);

	iowrite32(0x000000e4, priv->base + RCAR_CSI2_LSWAP);

	/* FIXME : wait until video decoder power off */
	msleep(10);

	{
		int timeout = 100;

		/* Read the PHY clock lane monitor register (PHCLM). */
		while (!(ioread32(priv->base + RCAR_CSI2_PHCLM) & 0x01)
			&& timeout){
			timeout--;
		}
		if (timeout == 0)
			dev_err(&priv->pdev->dev,
				"Error: Timeout of reading the PHY clock lane\n");
		else
			dev_err(&priv->pdev->dev, "Detected the PHY clock lane\n");

		timeout = 100;
		/* Read the PHY data lane monitor register (PHDLM). */
		while (!(ioread32(priv->base + RCAR_CSI2_PHDLM) & 0x01)
			&& timeout){
			timeout--;
		}
		if (timeout == 0)
			dev_err(&priv->pdev->dev,
				"Error: Timeout of reading the PHY data lane\n");
		else
			dev_err(&priv->pdev->dev, "Detected the PHY data lane\n");
	}

	return 0;
}
#endif
static void rcar_csi2_hwinit(struct rcar_csi2 *priv)
{
	struct rcar_csi2_pdata *pdata = priv->pdev->dev.platform_data;
	__u32 tmp = 0x10; /* Enable MIPI CSI clock lane */

	/* Reflect registers immediately */
	iowrite32(0x00000001, priv->base + RCAR_CSI2_TREF);
	/* reset CSI2 hardware */
	iowrite32(0x00000001, priv->base + RCAR_CSI2_SRST);
	udelay(5);
	iowrite32(0x00000000, priv->base + RCAR_CSI2_SRST);

	switch (pdata->type) {
	case RCAR_CSI2_CSI2X:
		if (priv->client->lanes == 1)
			tmp |= 1;
		else
			/* Default - both lanes */
			tmp |= 3;
		break;
	case RCAR_CSI2_CSI4X:
		if (!priv->client->lanes || priv->client->lanes > 4)
			/* Default - all 4 lanes */
			tmp |= 0xf;
		else
			tmp |= (1 << priv->client->lanes) - 1;
	}

	iowrite32(tmp, priv->base + RCAR_CSI2_PHYCNT);

    /* comfirmation of PHY start */
    /* FIXME */

	tmp = 0;
	if (pdata->flags & RCAR_CSI2_ECC)
		tmp |= 2;
	if (pdata->flags & RCAR_CSI2_CRC)
		tmp |= 1;
	iowrite32(tmp, priv->base + RCAR_CSI2_CHKSUM);
}

static int rcar_csi2_client_connect(struct rcar_csi2 *priv)
{
	struct device *dev = v4l2_get_subdevdata(&priv->subdev);
	struct rcar_csi2_pdata *pdata = dev->platform_data;
	struct soc_camera_device *icd =
					v4l2_get_subdev_hostdata(&priv->subdev);
	int i;

	if (priv->client)
		return -EBUSY;

	for (i = 0; i < pdata->num_clients; i++)
		if ((pdata->clients[i].pdev &&
		     &pdata->clients[i].pdev->dev == icd->pdev) ||
		    (icd->control &&
		     strcmp(pdata->clients[i].name, dev_name(icd->control))))
			break;

	dev_dbg(dev, "%s(%p): found #%d\n", __func__, dev, i);

	if (i == pdata->num_clients)
		return -ENODEV;

	priv->client = pdata->clients + i;

	return 0;
}

static void rcar_csi2_client_disconnect(struct rcar_csi2 *priv)
{

	if (!priv->client)
		return;

	priv->client = NULL;

	pm_runtime_put(v4l2_get_subdevdata(&priv->subdev));
}

static int rcar_csi2_s_power(struct v4l2_subdev *sd, int on)
{
	struct rcar_csi2 *priv = container_of(sd, struct rcar_csi2, subdev);

	if (on)
		return rcar_csi2_client_connect(priv);

	rcar_csi2_client_disconnect(priv);
	return 0;
}

static struct v4l2_subdev_core_ops rcar_csi2_subdev_core_ops = {
	.s_power	= rcar_csi2_s_power,
};

static struct v4l2_subdev_ops rcar_csi2_subdev_ops = {
	.core	= &rcar_csi2_subdev_core_ops,
	.video	= &rcar_csi2_subdev_video_ops,
	.pad = &rcar_csi2_pad_ops,
};

#ifdef CONFIG_OF
static const struct of_device_id rcar_csi2_of_table[] = {
	{ .compatible = "renesas,csi2-r8a7795", .data = (void *)RCAR_GEN3 },
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_vin_of_table);
#endif

static struct platform_device_id rcar_csi2_id_table[] = {
	{ "r8a7795-csi2",  RCAR_GEN3 },
	{},
};
MODULE_DEVICE_TABLE(platform, rcar_vin_id_table);

static int rcar_csi2_parse_dt(struct device_node *np,
			struct rcar_csi2_link_config *config)
{
	struct v4l2_of_endpoint bus_cfg;
	struct device_node *endpoint;
	const char *str;
	int ret;

	/* Parse the endpoint. */
	endpoint = of_graph_get_next_endpoint(np, NULL);
	if (!endpoint)
		return -EINVAL;

	v4l2_of_parse_endpoint(endpoint, &bus_cfg);
	of_node_put(endpoint);

	config->lanes = bus_cfg.bus.mipi_csi2.num_data_lanes;

	ret = of_property_read_string(np, "adi,input-interface", &str);
	if (ret < 0)
		return ret;

	return 0;
}

static int rcar_csi2_probe(struct platform_device *pdev)
{
	struct resource *res;
	unsigned int irq;
	int ret;
	struct rcar_csi2 *priv;
	/* Platform data specify the PHY, lanes, ECC, CRC */
	struct rcar_csi2_pdata *pdata;
	struct rcar_csi2_link_config link_config;

	dev_dbg(&pdev->dev, "CSI2 probed.\n");

	INIT_RCAR_CSI2_LINK_CONFIG(link_config);

	if (pdev->dev.of_node) {
		/* FIXME */
		ret = rcar_csi2_parse_dt(pdev->dev.of_node, &link_config);
		if (ret)
			return ret;

		if (link_config.lanes == 4)
			dev_err(&pdev->dev,
				"Detected rgb888 in rcar_csi2_parse_dt\n");
		else
			dev_err(&pdev->dev,
				"Detected YCbCr422 in rcar_csi2_parse_dt\n");
	} else {
		pdata = pdev->dev.platform_data;
		if (!pdata)
			return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(struct rcar_csi2), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* Interrupt unused so far */
	irq = platform_get_irq(pdev, 0);

	if (!res || (int)irq <= 0) {
		dev_err(&pdev->dev, "Not enough CSI2 platform resources.\n");
		return -ENODEV;
	}

	priv->irq = irq;

	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	ret = devm_request_irq(&pdev->dev, irq, rcar_csi2_irq, IRQF_SHARED,
			       dev_name(&pdev->dev), priv);
	if (ret)
		return ret;

	priv->pdev = pdev;
	priv->subdev.owner = THIS_MODULE;
	priv->subdev.dev = &pdev->dev;

	platform_set_drvdata(pdev, &priv->subdev);

	v4l2_subdev_init(&priv->subdev, &rcar_csi2_subdev_ops);
	v4l2_set_subdevdata(&priv->subdev, &pdev->dev);

	snprintf(priv->subdev.name, V4L2_SUBDEV_NAME_SIZE, "%s.mipi-csi",
		 dev_name(&pdev->dev));

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		return ret;

	spin_lock_init(&priv->lock);

	pm_runtime_enable(&pdev->dev);

	dev_dbg(&pdev->dev, "CSI2 probed.\n");

    /* [FIXME] The following codes is trial */
	{
#if 0
		/* FIXME: The following is a original code */
		struct v4l2_subdev_format format;

		rcar_csi2_hwinit(priv);
		format.format.code = MEDIA_BUS_FMT_UYVY8_2X8;
		rcar_csi2_set_pad_format(&priv->subdev, NULL, &format);
#else
		ret = rcar_csi2_hwinit_tmp(priv, &link_config);
		if (ret < 0)
			return ret;
#endif
	}

	return 0;
}

static int rcar_csi2_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *subdev = platform_get_drvdata(pdev);
	struct rcar_csi2 *priv = container_of(subdev, struct rcar_csi2, subdev);

	v4l2_async_unregister_subdev(&priv->subdev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver __refdata rcar_csi2_pdrv = {
	.remove	= rcar_csi2_remove,
	.probe	= rcar_csi2_probe,
	.driver	= {
		.name	= DRV_NAME,
		.of_match_table	= of_match_ptr(rcar_csi2_of_table),
	},
	.id_table	= rcar_csi2_id_table,
};

#if 1/* FIXME */
module_platform_driver(rcar_csi2_pdrv);
#else
static int __init rcar_csi2_adap_init(void)
{
	return platform_driver_register(&rcar_csi2_pdrv);
}

static void __exit rcar_csi2_adap_exit(void)
{
	platform_driver_unregister(&rcar_csi2_pdrv);
}

subsys_initcall(rcar_csi2_adap_init);
module_exit(rcar_csi2_adap_exit);
#endif

MODULE_DESCRIPTION("Renesas R-Car MIPI CSI-2 driver");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rcar-csi2");
