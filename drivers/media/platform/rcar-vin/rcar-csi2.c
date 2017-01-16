/*
 * Driver for Renesas R-Car MIPI CSI-2
 *
 * Copyright (C) 2016-2017 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sys_soc.h>

#include <media/v4l2-of.h>
#include <media/v4l2-subdev.h>

/* Register offsets */
#define TREF_REG		0x00 /* Control Timing Select */
#define SRST_REG		0x04 /* Software Reset */
#define PHYCNT_REG		0x08 /* PHY Operation Control */
#define CHKSUM_REG		0x0C /* Checksum Control */
#define VCDT_REG		0x10 /* Channel Data Type Select */
#define VCDT2_REG		0x14 /* Channel Data Type Select 2 */
#define FRDT_REG		0x18 /* Frame Data Type Select */
#define FLD_REG			0x1C /* Field Detection Control */
#define ASTBY_REG		0x20 /* Automatic Standby Control */
#define LNGDT0_REG		0x28 /* Long Data Type Setting 0 */
#define LNGDT1_REG		0x2C /* Long Data Type Setting 1 */
#define INTEN_REG		0x30 /* Interrupt Enable */
#define INTCLOSE_REG		0x34 /* Interrupt Source Mask */
#define INTSTATE_REG		0x38 /* Interrupt Status Monitor */
#define INTERRSTATE_REG		0x3C /* Interrupt Error Status Monitor */
#define SHPDAT_REG		0x40 /* Short Packet Data */
#define SHPCNT_REG		0x44 /* Short Packet Count */
#define LINKCNT_REG		0x48 /* LINK Operation Control */
#define LSWAP_REG		0x4C /* Lane Swap */
#define PHTW_REG		0x50 /* PHY Test Interface Write */
#define PHTC_REG		0x58 /* PHY Test Interface Clear */
#define PHYPLL_REG		0x68 /* PHY Frequency Control */
#define PHEERM_REG		0x74 /* PHY ESC Error Monitor */
#define PHCLM_REG		0x78 /* PHY Clock Lane Monitor */
#define PHDLM_REG		0x7C /* PHY Data Lane Monitor */
#define CSI0CLKFCPR_REG		0x254/* CSI0CLK Frequency Configuration Preset */

/* Control Timing Select bits */
#define TREF_TREF			(1 << 0)

/* Software Reset bits */
#define SRST_SRST			(1 << 0)

/* PHY Operation Control bits */
#define PHYCNT_SHUTDOWNZ		(1 << 17)
#define PHYCNT_RSTZ			(1 << 16)
#define PHYCNT_ENABLECLK		(1 << 4)
#define PHYCNT_ENABLE_3			(1 << 3)
#define PHYCNT_ENABLE_2			(1 << 2)
#define PHYCNT_ENABLE_1			(1 << 1)
#define PHYCNT_ENABLE_0			(1 << 0)

/* Checksum Control bits */
#define CHKSUM_ECC_EN			(1 << 1)
#define CHKSUM_CRC_EN			(1 << 0)

/*
 * Channel Data Type Select bits
 * VCDT[0-15]:  Channel 1 VCDT[16-31]:  Channel 2
 * VCDT2[0-15]: Channel 3 VCDT2[16-31]: Channel 4
 */
#define VCDT_VCDTN_EN			(1 << 15)
#define VCDT_SEL_VC(n)			((n & 0x3) << 8)
#define VCDT_SEL_DTN_ON			(1 << 6)
#define VCDT_SEL_DT(n)			((n & 0x1f) << 0)

/* Field Detection Control bits */
#define FLD_FLD_NUM(n)			((n & 0xff) << 16)
#define FLD_FLD_EN4			(1 << 3)
#define FLD_FLD_EN3			(1 << 2)
#define FLD_FLD_EN2			(1 << 1)
#define FLD_FLD_EN			(1 << 0)

/* LINK Operation Control bits */
#define LINKCNT_MONITOR_EN		(1 << 31)
#define LINKCNT_REG_MONI_PACT_EN	(1 << 25)
#define LINKCNT_ICLK_NONSTOP		(1 << 24)

/* Lane Swap bits */
#define LSWAP_L3SEL(n)			((n & 0x3) << 6)
#define LSWAP_L2SEL(n)			((n & 0x3) << 4)
#define LSWAP_L1SEL(n)			((n & 0x3) << 2)
#define LSWAP_L0SEL(n)			((n & 0x3) << 0)

/* PHY Test Interface Clear bits */
#define PHTC_TESTCLR			(1 << 0)

/* PHY Frequency Control */
#define CSI2_FRE_NUM	43

enum fre_range {
	BPS_80M,
	BPS_90M,
	BPS_100M,
	BPS_110M,
	BPS_120M,
	BPS_130M,
	BPS_140M,
	BPS_150M,
	BPS_160M,
	BPS_170M,
	BPS_180M,
	BPS_190M,
	BPS_205M,
	BPS_220M,
	BPS_235M,
	BPS_250M,
	BPS_275M,
	BPS_300M,
	BPS_325M,
	BPS_350M,
	BPS_400M,
	BPS_450M,
	BPS_500M,
	BPS_550M,
	BPS_600M,
	BPS_650M,
	BPS_700M,
	BPS_750M,
	BPS_800M,
	BPS_850M,
	BPS_900M,
	BPS_950M,
	BPS_1000M,
	BPS_1050M,
	BPS_1100M,
	BPS_1150M,
	BPS_1200M,
	BPS_1250M,
	BPS_1300M,
	BPS_1350M,
	BPS_1400M,
	BPS_1450M,
	BPS_1500M,
};

struct rcar_csi2_info {
	int fre_range[CSI2_FRE_NUM];
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7795 = {
	.fre_range = {
		(0x00 << 16), (0x10 << 16), (0x20 << 16), (0x30 << 16),
		(0x01 << 16), (0x11 << 16), (0x21 << 16), (0x31 << 16),
		(0x02 << 16), (0x12 << 16), (0x22 << 16), (0x32 << 16),
		(0x03 << 16), (0x13 << 16), (0x23 << 16), (0x33 << 16),
		(0x04 << 16), (0x14 << 16), (0x25 << 16), (0x35 << 16),
		(0x05 << 16), (0x26 << 16), (0x36 << 16), (0x37 << 16),
		(0x07 << 16), (0x18 << 16), (0x28 << 16), (0x39 << 16),
		(0x09 << 16), (0x19 << 16), (0x29 << 16), (0x3a << 16),
		(0x0a << 16), (0x1a << 16), (0x2a << 16), (0x3b << 16),
		(0x0b << 16), (0x1b << 16), (0x2b << 16), (0x3c << 16),
		(0x0c << 16), (0x1c << 16), (0x2c << 16),
	},
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7796 = {
	.fre_range = {
		(0x00 << 16), (0x10 << 16), (0x20 << 16), (0x30 << 16),
		(0x01 << 16), (0x11 << 16), (0x21 << 16), (0x31 << 16),
		(0x02 << 16), (0x12 << 16), (0x22 << 16), (0x32 << 16),
		(0x03 << 16), (0x13 << 16), (0x23 << 16), (0x33 << 16),
		(0x04 << 16), (0x14 << 16), (0x05 << 16), (0x15 << 16),
		(0x25 << 16), (0x06 << 16), (0x16 << 16), (0x07 << 16),
		(0x17 << 16), (0x08 << 16), (0x18 << 16), (0x09 << 16),
		(0x19 << 16), (0x29 << 16), (0x39 << 16), (0x0a << 16),
		(0x1a << 16), (0x2a << 16), (0x3a << 16), (0x0b << 16),
		(0x1b << 16), (0x2b << 16), (0x3b << 16), (0x0c << 16),
		(0x1c << 16), (0x2c << 16), (0x3c << 16),
	},
};

enum rcar_csi2_pads {
	RCAR_CSI2_SINK,
	RCAR_CSI2_SOURCE_VC0,
	RCAR_CSI2_SOURCE_VC1,
	RCAR_CSI2_SOURCE_VC2,
	RCAR_CSI2_SOURCE_VC3,
	RCAR_CSI2_PAD_MAX,
};

/* CSI0CLK frequency configuration bit */
#define CSI0CLKFREQRANGE(n)		((n & 0x3f) << 16)

struct rcar_csi2 {
	struct device *dev;
	void __iomem *base;
	spinlock_t lock;
	const struct rcar_csi2_info *info;

	unsigned short lanes;
	unsigned char swap[4];

	struct v4l2_subdev subdev;
	struct media_pad pads[RCAR_CSI2_PAD_MAX];
	struct v4l2_mbus_framefmt mf;

	u32 vc_num;
};

#define csi_dbg(p, fmt, arg...)		dev_dbg(p->dev, fmt, ##arg)
#define csi_info(p, fmt, arg...)	dev_info(p->dev, fmt, ##arg)
#define csi_warn(p, fmt, arg...)	dev_warn(p->dev, fmt, ##arg)
#define csi_err(p, fmt, arg...)		dev_err(p->dev, fmt, ##arg)

/* H3 WS1.x  */
static const struct soc_device_attribute r8a7795es1x[] = {
	{ .soc_id = "r8a7795", .revision = "ES1.*" },
	{ },
};

/* M3  */
static const struct soc_device_attribute r8a7796[] = {
	{ .soc_id = "r8a7796" },
	{ },
};

static irqreturn_t rcar_csi2_irq(int irq, void *data)
{
	struct rcar_csi2 *priv = data;
	u32 int_status;
	unsigned int handled = 0;

	spin_lock(&priv->lock);

	int_status = ioread32(priv->base + INTSTATE_REG);
	if (!int_status)
		goto done;

	/* ack interrupts */
	iowrite32(int_status, priv->base + INTSTATE_REG);
	handled = 1;

done:
	spin_unlock(&priv->lock);

	return IRQ_RETVAL(handled);

}

static void rcar_csi2_reset(struct rcar_csi2 *priv)
{
	iowrite32(SRST_SRST, priv->base + SRST_REG);
	udelay(5);
	iowrite32(0, priv->base + SRST_REG);
}

static void rcar_csi2_wait_phy_start(struct rcar_csi2 *priv)
{
	int timeout;

	/* Read the PHY clock lane monitor register (PHCLM). */
	for (timeout = 100; timeout > 0; timeout--) {
		if (ioread32(priv->base + PHCLM_REG) & 0x01) {
			csi_dbg(priv, "Detected the PHY clock lane\n");
			break;
		}
		msleep(20);
	}
	if (!timeout)
		csi_err(priv, "Timeout of reading the PHY clock lane\n");


	/* Read the PHY data lane monitor register (PHDLM). */
	for (timeout = 100; timeout > 0; timeout--) {
		if (ioread32(priv->base + PHDLM_REG) & 0x01) {
			csi_dbg(priv, "Detected the PHY data lane\n");
			break;
		}
		msleep(20);
	}
	if (!timeout)
		csi_err(priv, "Timeout of reading the PHY data lane\n");

}

static int rcar_csi2_start(struct rcar_csi2 *priv)
{
	u32 fld, phycnt, phypll, vcdt, vcdt2, tmp, pixels;
	int i;

	csi_dbg(priv, "Input size (%dx%d%c)\n", priv->mf.width, priv->mf.height,
		priv->mf.field == V4L2_FIELD_NONE ? 'p' : 'i');

	vcdt = vcdt2 = 0;
	for (i = 0; i < priv->vc_num; i++) {
		tmp = VCDT_SEL_VC(i) | VCDT_VCDTN_EN | VCDT_SEL_DTN_ON;

		switch (priv->mf.code) {
		case MEDIA_BUS_FMT_RGB888_1X24:
			/* 24 == RGB888 */
			tmp |= 0x24;
			break;
		case MEDIA_BUS_FMT_UYVY8_1X16:
		case MEDIA_BUS_FMT_UYVY8_2X8:
		case MEDIA_BUS_FMT_YUYV10_2X10:
			/* 1E == YUV422 8-bit */
			tmp |= 0x1e;
			break;
		default:
			csi_warn(priv,
				 "Unknown media bus format, try it anyway\n");
			break;
		}

		/* Store in correct reg and offset */
		if (i < 2)
			vcdt |= tmp << ((i % 2) * 16);
		else
			vcdt2 |= tmp << ((i % 2) * 16);
	}

	switch (priv->lanes) {
	case 1:
		fld = FLD_FLD_NUM(1) | FLD_FLD_EN;
		phycnt = PHYCNT_ENABLECLK | PHYCNT_ENABLE_0;
		phypll = priv->info->fre_range[BPS_205M];
		break;
	case 4:
		fld = FLD_FLD_NUM(2) | FLD_FLD_EN4 | FLD_FLD_EN3 |
			FLD_FLD_EN2 | FLD_FLD_EN;
		phycnt = PHYCNT_ENABLECLK | PHYCNT_ENABLE_3 |
			PHYCNT_ENABLE_2 | PHYCNT_ENABLE_1 | PHYCNT_ENABLE_0;

		/* Calculate MBPS per lane, assume 32 bits per pixel at 60Hz */
		pixels = (priv->mf.width * priv->mf.height);
		if (pixels <= 640 * 480)
			phypll = priv->info->fre_range[BPS_100M];
		else if (pixels <= 720 * 576)
			phypll = priv->info->fre_range[BPS_190M];
		else if (pixels <= 1280 * 720)
			phypll = priv->info->fre_range[BPS_450M];
		else if (pixels <= 1920 * 1080) {
			if (priv->mf.field == V4L2_FIELD_NONE)
				phypll = priv->info->fre_range[BPS_900M];
			else
				phypll = priv->info->fre_range[BPS_450M];
		} else
			goto error;

		break;
	default:
		goto error;
	}

	csi_dbg(priv, "PHYPLL:0x%x\n", phypll);

	/* Init */
	iowrite32(TREF_TREF, priv->base + TREF_REG);
	rcar_csi2_reset(priv);
	iowrite32(0, priv->base + PHTC_REG);

	/* Configure */
	iowrite32(fld, priv->base + FLD_REG);
	iowrite32(vcdt, priv->base + VCDT_REG);
	iowrite32(vcdt2, priv->base + VCDT2_REG);
	iowrite32(LSWAP_L0SEL(priv->swap[0]) | LSWAP_L1SEL(priv->swap[1]) |
		  LSWAP_L2SEL(priv->swap[2]) | LSWAP_L3SEL(priv->swap[3]),
		  priv->base + LSWAP_REG);

	if (!soc_device_match(r8a7795es1x) && !soc_device_match(r8a7796)) {
		/* Set PHY Test Interface Write Register for external
		 * reference resistor is unnecessary in R-Car H3(WS2.0)
		 */
		iowrite32(0x012701e2, priv->base + PHTW_REG);
		iowrite32(0x010101e3, priv->base + PHTW_REG);
		iowrite32(0x010101e4, priv->base + PHTW_REG);
		iowrite32(0x01100104, priv->base + PHTW_REG);
	}

	/* Start */
	iowrite32(phypll, priv->base + PHYPLL_REG);

	/* Set CSI0CLK Frequency Configuration Preset Register for external
	 * reference resistor is unnecessary in R-Car H3(WS2.0)
	 */
	if (!soc_device_match(r8a7795es1x) && !soc_device_match(r8a7796))
		iowrite32(CSI0CLKFREQRANGE(32), priv->base + CSI0CLKFCPR_REG);

	iowrite32(phycnt, priv->base + PHYCNT_REG);
	iowrite32(LINKCNT_MONITOR_EN | LINKCNT_REG_MONI_PACT_EN |
		  LINKCNT_ICLK_NONSTOP, priv->base + LINKCNT_REG);
	iowrite32(phycnt | PHYCNT_SHUTDOWNZ, priv->base + PHYCNT_REG);
	iowrite32(phycnt | PHYCNT_SHUTDOWNZ | PHYCNT_RSTZ,
		  priv->base + PHYCNT_REG);

	rcar_csi2_wait_phy_start(priv);

	return 0;
error:
	csi_err(priv, "Unsupported resolution (%dx%d%c)\n",
		priv->mf.width, priv->mf.height,
		priv->mf.field == V4L2_FIELD_NONE ? 'p' : 'i');

	return -EINVAL;
}

static void rcar_csi2_stop(struct rcar_csi2 *priv)
{
	iowrite32(0, priv->base + PHYCNT_REG);

	rcar_csi2_reset(priv);
}

static int rcar_csi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rcar_csi2 *priv = container_of(sd, struct rcar_csi2, subdev);

	if (enable)
		return rcar_csi2_start(priv);

	rcar_csi2_stop(priv);

	return 0;
}

static int rcar_csi2_set_pad_format(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_format *format)
{
	struct rcar_csi2 *priv = container_of(sd, struct rcar_csi2, subdev);

	if (format->pad != RCAR_CSI2_SINK)
		return -EINVAL;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		priv->mf = format->format;

	return 0;
}

static int rcar_csi2_get_pad_format(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_format *format)
{
	struct rcar_csi2 *priv = container_of(sd, struct rcar_csi2, subdev);

	if (format->pad != RCAR_CSI2_SINK)
		return -EINVAL;

	format->format = priv->mf;

	return 0;
}

static int rcar_csi2_s_power(struct v4l2_subdev *sd, int on)
{
	struct rcar_csi2 *priv = container_of(sd, struct rcar_csi2, subdev);

	if (on)
		pm_runtime_get_sync(priv->dev);
	else
		pm_runtime_put_sync(priv->dev);

	return 0;
}

static const struct v4l2_subdev_video_ops rcar_csi2_video_ops = {
	.s_stream = rcar_csi2_s_stream,
};

static struct v4l2_subdev_core_ops rcar_csi2_subdev_core_ops = {
	.s_power = rcar_csi2_s_power,
};

static const struct v4l2_subdev_pad_ops rcar_csi2_pad_ops = {
	.set_fmt = rcar_csi2_set_pad_format,
	.get_fmt = rcar_csi2_get_pad_format,
};

static struct v4l2_subdev_ops rcar_csi2_subdev_ops = {
	.video	= &rcar_csi2_video_ops,
	.core	= &rcar_csi2_subdev_core_ops,
	.pad	= &rcar_csi2_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static const struct of_device_id rcar_csi2_of_table[] = {
	{
		.compatible = "renesas,r8a7795-csi2",
		.data = &rcar_csi2_info_r8a7795,
	},
	{
		.compatible = "renesas,r8a7796-csi2",
		.data = &rcar_csi2_info_r8a7796,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_csi2_of_table);

static int rcar_csi2_parse_dt(struct rcar_csi2 *priv)
{
	struct v4l2_of_endpoint v4l2_ep;
	struct device_node *ep;
	int i, n, ret;
	u32 vc_num;

	ep = of_graph_get_endpoint_by_regs(priv->dev->of_node, 0, 0);
	if (!ep)
		return -EINVAL;

	ret = v4l2_of_parse_endpoint(ep, &v4l2_ep);
	of_node_put(ep);
	if (ret) {
		csi_err(priv, "Could not parse v4l2 endpoint\n");
		return -EINVAL;
	}

	if (v4l2_ep.bus_type != V4L2_MBUS_CSI2) {
		csi_err(priv, "Unsupported media bus type for %s\n",
			of_node_full_name(ep));
		return -EINVAL;
	}

	switch (v4l2_ep.bus.mipi_csi2.num_data_lanes) {
	case 1:
	case 4:
		priv->lanes = v4l2_ep.bus.mipi_csi2.num_data_lanes;
		break;
	default:
		csi_err(priv, "Unsupported number of lanes\n");
		return -EINVAL;
	}

	for (i = 0; i < 4; i++)
		priv->swap[i] = i;

	for (i = 0; i < priv->lanes; i++) {
		/* Check for valid lane number */
		if (v4l2_ep.bus.mipi_csi2.data_lanes[i] < 1 ||
		    v4l2_ep.bus.mipi_csi2.data_lanes[i] > 4) {
			csi_err(priv, "data lanes must be in 1-4 range\n");
			return -EINVAL;
		}

		/* Use lane numbers 0-3 internally */
		priv->swap[i] = v4l2_ep.bus.mipi_csi2.data_lanes[i] - 1;


	}

	/* Make sure there are no duplicates */
	for (i = 0; i < priv->lanes; i++) {
		for (n = i + 1; n < priv->lanes; n++) {
			if (priv->swap[i] == priv->swap[n]) {
				csi_err(priv,
					"Requested swapping not possible\n");
				return -EINVAL;
			}
		}
	}

	if (!of_property_read_u32(ep, "virtual-channel-number", &vc_num))
		priv->vc_num = vc_num;

	return 0;
}

static int rcar_csi2_probe_resources(struct rcar_csi2 *priv,
				     struct platform_device *pdev)
{
	struct resource *mem;
	int irq;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -ENODEV;

	priv->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	irq = platform_get_irq(pdev, 0);
	if (!irq)
		return -ENODEV;

	return devm_request_irq(&pdev->dev, irq, rcar_csi2_irq, IRQF_SHARED,
				dev_name(&pdev->dev), priv);
}

static int rcar_csi2_probe(struct platform_device *pdev)
{
	struct rcar_csi2 *priv;
	const struct of_device_id *match;
	unsigned int i;
	int ret;
	u32 vc_num;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct rcar_csi2), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	match = of_match_device(of_match_ptr(rcar_csi2_of_table), &pdev->dev);
	if (!match)
		return -ENODEV;
	priv->info = match->data;

	/* HSFREQRANGE bit information of H3(ES1.x) and M3(WS1.0) are same. */
	if (soc_device_match(r8a7795es1x))
		priv->info = &rcar_csi2_info_r8a7796;

	priv->dev = &pdev->dev;
	spin_lock_init(&priv->lock);

	priv->vc_num = 0;

	ret = rcar_csi2_parse_dt(priv);
	if (ret)
		return ret;

	ret = rcar_csi2_probe_resources(priv, pdev);
	if (ret) {
		csi_err(priv, "Failed to get resources\n");
		return ret;
	}

	vc_num = priv->vc_num;
	platform_set_drvdata(pdev, priv);

	priv->subdev.owner = THIS_MODULE;
	priv->subdev.dev = &pdev->dev;
	v4l2_subdev_init(&priv->subdev, &rcar_csi2_subdev_ops);
	v4l2_set_subdevdata(&priv->subdev, &pdev->dev);
	snprintf(priv->subdev.name, V4L2_SUBDEV_NAME_SIZE, "%s.%s",
		 KBUILD_MODNAME, dev_name(&pdev->dev));

	priv->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	priv->subdev.entity.function = MEDIA_ENT_F_ATV_DECODER;
	priv->subdev.entity.flags |= MEDIA_ENT_F_ATV_DECODER;

	priv->pads[RCAR_CSI2_SINK].flags = MEDIA_PAD_FL_SINK;
	for (i = RCAR_CSI2_SOURCE_VC0; i < RCAR_CSI2_PAD_MAX; i++)
		priv->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&priv->subdev.entity, RCAR_CSI2_PAD_MAX,
				     priv->pads);
	if (ret)
		return ret;

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		return ret;

	pm_runtime_enable(&pdev->dev);

	csi_info(priv, "%d lanes found. virtual channel number %d use\n",
		 priv->lanes, priv->vc_num);

	return 0;
}

static int rcar_csi2_remove(struct platform_device *pdev)
{
	struct rcar_csi2 *priv = platform_get_drvdata(pdev);

	v4l2_async_unregister_subdev(&priv->subdev);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver __refdata rcar_csi2_pdrv = {
	.remove	= rcar_csi2_remove,
	.probe	= rcar_csi2_probe,
	.driver	= {
		.name	= "rcar-csi2",
		.of_match_table	= of_match_ptr(rcar_csi2_of_table),
	},
};

module_platform_driver(rcar_csi2_pdrv);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Renesas R-Car MIPI CSI-2 driver");
MODULE_LICENSE("GPL v2");
