/*
 * TI DS90UB954/960/964 FPDLinkIII driver
 *
 * Copyright (C) 2017-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>

#include <media/v4l2-async.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "ub9x4.h"
#include "../gmsl/common.h"

#define UB9X4_N_LINKS		4

enum ub9x4_pads {
	UB9X4_SINK_LINK0,
	UB9X4_SINK_LINK1,
	UB9X4_SINK_LINK2,
	UB9X4_SINK_LINK3,
	UB9X4_SOURCE,
	UB9X4_N_PADS,
};

struct ub9x4_sink {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev	*sd;
	struct fwnode_handle	*fwnode;
};

#define asd_to_ub9x4_sink(_asd) \
	container_of(_asd, struct ub9x4_sink, asd)

struct ub9x4_priv {
	struct i2c_client	*client;
	struct v4l2_subdev	sd;
	struct media_pad	pads[UB9X4_N_PADS];

	struct i2c_mux_core	*mux;

	struct ub9x4_sink	sinks[UB9X4_N_LINKS];
	struct v4l2_async_subdev *subdevs[UB9X4_N_LINKS];
	struct v4l2_async_notifier notifier;
	struct v4l2_ctrl_handler ctrls;

	int			des_addr;
	int			n_links;
	int			links_mask;
	int			lanes;
	int			csi_rate;
	char			forwarding_mode[16];
	int			fs_time;
	int			fps_numerator;
	int			fps_denominator;
	int			is_coax;
	int			dvp_bus;
	int			dvp_lsb;
	int			hsync;
	int			vsync;
	int			poc_delay;
	atomic_t		use_count;
	int			ser_addr[4];
	char			chip_id[6];
	int			ser_id;
	int			vc_map;
	int			csi_map;
	int			gpio[4];
	struct regulator	*poc_reg[4]; /* PoC power supply */
	struct v4l2_clk		*ref_clk; /* ref clock */
	struct notifier_block	reboot_notifier;
};

static int ser_id;
module_param(ser_id, int, 0644);
MODULE_PARM_DESC(ser_id, "  Serializer ID (default: UB913)");

static int is_stp;
module_param(is_stp, int, 0644);
MODULE_PARM_DESC(is_stp, "  STP cable (default: Coax cable)");

static int dvp_bus = 8;
module_param(dvp_bus, int, 0644);
MODULE_PARM_DESC(dvp_bus, "  DVP/CSI over FPDLink (default: DVP 8-bit)");

static int dvp_lsb = 0;
module_param(dvp_lsb, int, 0644);
MODULE_PARM_DESC(dvp_lsb, "  DVP 8-bit LSB/MSB selection (default: DVP 8-bit MSB)");

static int hsync;
module_param(hsync, int, 0644);
MODULE_PARM_DESC(hsync, " HSYNC invertion (default: 0 - not inverted)");

static int vsync = 1;
module_param(vsync, int, 0644);
MODULE_PARM_DESC(vsync, " VSYNC invertion (default: 1 - inverted)");

static int poc_delay;
module_param(poc_delay, int, 0644);
MODULE_PARM_DESC(poc_delay, " Delay in ms after POC enable (default: 0 ms)");

static int vc_map = 0x3210;
module_param(vc_map, int, 0644);
MODULE_PARM_DESC(vc_map, " CSI VC MAP (default: 0xe4 - linear map VCx=LINKx)");

static int csi_map = 0;
module_param(csi_map, int, 0644);
MODULE_PARM_DESC(csi_map, " CSI TX MAP (default: 0 - forwarding of all links to CSI0)");

static int gpio0 = 0, gpio1 = 0, gpio2 = 0, gpio3 = 0;
module_param(gpio0, int, 0644);
MODULE_PARM_DESC(gpio0, "  GPIO0 function select (default: GPIO0 low level)");
module_param(gpio1, int, 0644);
MODULE_PARM_DESC(gpio1, "  GPIO1 function select (default: GPIO1 low level)");
module_param(gpio2, int, 0644);
MODULE_PARM_DESC(gpio2, "  GPIO2 function select (default: GPIO2 low level)");
module_param(gpio3, int, 0644);
MODULE_PARM_DESC(gpio3, "  GPIO3 function select (default: GPIO3 low level)");

static void ub9x4_read_chipid(struct i2c_client *client)
{
	struct ub9x4_priv *priv = i2c_get_clientdata(client);

	/* Chip ID */
	reg8_read(client, 0xf1, &priv->chip_id[0]);
	reg8_read(client, 0xf2, &priv->chip_id[1]);
	reg8_read(client, 0xf3, &priv->chip_id[2]);
	reg8_read(client, 0xf4, &priv->chip_id[3]);
	reg8_read(client, 0xf5, &priv->chip_id[4]);
	priv->chip_id[5] = '\0';
}

static void ub9x4_initial_setup(struct i2c_client *client)
{
	struct ub9x4_priv *priv = i2c_get_clientdata(client);

	/* Initial setup */
	client->addr = priv->des_addr;				/* ub9x4 I2C */
	reg8_write(client, 0x0d, 0xb9);				/* VDDIO 3.3V */
	switch (priv->csi_rate) {
	case 1600: /* REFCLK = 25MHZ */
	case 1500: /* REFCLK = 23MHZ */
	case 1450: /* REFCLK = 22.5MHZ */
		reg8_write(client, 0x1f, 0x00);			/* CSI rate 1.5/1.6Gbps */
		break;
	case 1200: /* REFCLK = 25MHZ */
	case 1100: /* REFCLK = 22.5MHZ */
		reg8_write(client, 0x1f, 0x01);			/* CSI rate 1.1/1.2Gbps */
		break;
	case 800: /* REFCLK = 25MHZ */
	case 700: /* REFCLK = 22.5MHZ */
		reg8_write(client, 0x1f, 0x02);			/* CSI rate 700/800Mbps */
		break;
	case 400: /* REFCLK = 25MHZ */
	case 350: /* REFCLK = 22.5MHZ */
		reg8_write(client, 0x1f, 0x03);			/* CSI rate 350/400Mbps */
		break;
	default:
		dev_err(&client->dev, "unsupported CSI rate %d\n", priv->csi_rate);
	}

	switch (priv->csi_rate) {
	case 1600:
	case 1200:
	case 800:
	case 400:
		/* FrameSync setup for REFCLK=25MHz,   FPS=30: period_counts=1/FPS/12mks=1/30/12e-6=2777 -> HI=2, LO=2775 */
		priv->fs_time = 2790;
		break;
	case 1500:
		/* FrameSync setup for REFCLK=23MHz, FPS=30: period_counts=1/FPS/13.043mks=1/30/13.043e-6=2556 -> HI=2, LO=2554 */
		priv->fs_time = 2570;
		break;
	case 1450:
	case 1100:
	case 700:
	case 350:
		/* FrameSync setup for REFCLK=22.5MHz, FPS=30: period_counts=1/FPS/13.333mks=1/30/13.333e-6=2500 -> HI=2, LO=2498 */
		priv->fs_time = 2513;
		break;
	default:
		priv->fs_time = 0;
		dev_err(&client->dev, "unsupported CSI rate %d\n", priv->csi_rate);
	}

	if (strcmp(priv->forwarding_mode, "round-robin") == 0) {
		reg8_write(client, 0x21, 0x03);			/* Round Robin forwarding enable for CSI0/CSI1 */
	} else if (strcmp(priv->forwarding_mode, "synchronized") == 0) {
		reg8_write(client, 0x21, 0x54);			/* Basic Syncronized forwarding enable (FrameSync must be enabled!!) for CSI0/CSI1 */
	}

	reg8_write(client, 0x32, 0x03);				/* Select TX for CSI0/CSI1, RX for CSI0 */
	reg8_write(client, 0x33, ((priv->lanes - 1) ^ 0x3) << 4); /* disable CSI output, set CSI lane count, non-continuous CSI mode */
	reg8_write(client, 0x20, 0xf0 | priv->csi_map);		/* disable port forwarding */
#if 0
	/* FrameSync setup for REFCLK=25MHz,   FPS=30: period_counts=1/2/FPS*25MHz  =1/2/30*25Mhz  =416666 -> FS_TIME=416666 */
	/* FrameSync setup for REFCLK=22.5MHz, FPS=30: period_counts=1/2/FPS*22.5Mhz=1/2/30*22.5Mhz=375000 -> FS_TIME=375000 */
// #define FS_TIME (priv->csi_rate == 1450 ? 376000 : 417666)
 #define FS_TIME (priv->csi_rate == 1450 ? 385000 : 428000) // FPS=29.2 (new vendor's firmware AWB restriction?)
	reg8_write(client, 0x1a, FS_TIME >> 16);		/* FrameSync time 24bit */
	reg8_write(client, 0x1b, (FS_TIME >> 8) & 0xff);
	reg8_write(client, 0x1c, FS_TIME & 0xff);
	reg8_write(client, 0x18, 0x43);				/* Enable FrameSync, 50/50 mode, Frame clock from 25MHz */
#else
	reg8_write(client, 0x19, 2 >> 8);			/* FrameSync high time MSB */
	reg8_write(client, 0x1a, 2 & 0xff);			/* FrameSync high time LSB */
	reg8_write(client, 0x1b, priv->fs_time >> 8);		/* FrameSync low time MSB */
	reg8_write(client, 0x1c, priv->fs_time & 0xff);		/* FrameSync low time LSB */
	reg8_write(client, 0x18, 0x00);				/* Disable FrameSync - must be enabled after all cameras are set up */
#endif
}

static void ub9x4_fpdlink3_setup(struct i2c_client *client, int idx)
{
	struct ub9x4_priv *priv = i2c_get_clientdata(client);
	u8 port_config = 0x78;
	u8 port_config2 = 0;

	/* FPDLinkIII setup */
	client->addr = priv->des_addr;				/* ub9x4 I2C */
	reg8_write(client, 0x4c, (idx << 4) | (1 << idx));	/* Select RX port number */
	usleep_range(2000, 2500);

	switch (priv->ser_id) {
	case UB913_ID:
		reg8_write(client, 0x58, 0x58);			/* Back channel: Freq=2.5Mbps */
		break;
	case UB953_ID:
		reg8_write(client, 0x58, 0x5e);			/* Back channel: Freq=50Mbps */
		break;
	default:
		break;
	}

	reg8_write(client, 0x5c, priv->ser_addr[idx] << 1);	/* UB9X3 I2C addr */
//	reg8_write(client, 0x5d, 0x30 << 1);			/* SENSOR I2C native - must be set by sensor driver */
//	reg8_write(client, 0x65, (0x60 + idx) << 1);		/* SENSOR I2C translated - must be set by sensor driver */

	if (priv->is_coax)
		port_config |= 0x04;				/* Coax */
	else
		port_config |= 0x00;				/* STP */

	switch (priv->dvp_bus) {
	case 8:
		port_config2 |= (priv->dvp_lsb ? 0xC0 : 0x80);	/* RAW10 as 8-bit prosessing using LSB/MSB bits  */
		/* fall through */
	case 10:
		port_config |= 0x03;				/* DVP over FPDLink (UB913 compatible) RAW10/RAW8 */
		break;
	case 12:
		port_config |= 0x02;				/* DVP over FPDLink (UB913 compatible) RAW12 */
		break;
	default:
		port_config |= 0x00;				/* CSI over FPDLink (UB953 compatible) */
	}

	if (priv->vsync)
		port_config2 |= 0x01;				/* VSYNC acive low */
	if (priv->hsync)
		port_config2 |= 0x02;				/* HSYNC acive low */

	reg8_write(client, 0x6d, port_config);
	reg8_write(client, 0x7c, port_config2);
	reg8_write(client, 0x70, ((priv->vc_map >> (idx * 4)) << 6) | 0x1e); /* CSI data type: yuv422 8-bit, assign VC */
	reg8_write(client, 0x71, ((priv->vc_map >> (idx * 4)) << 6) | 0x2c); /* CSI data type: RAW12, assign VC */
	reg8_write(client, 0xbc, 0x00);				/* Setup minimal time between FV and LV to 3 PCLKs */
	reg8_write(client, 0x72, priv->vc_map >> (idx * 4));	/* CSI VC MAP */
}

static int ub9x4_initialize(struct i2c_client *client)
{
	struct ub9x4_priv *priv = i2c_get_clientdata(client);
	int i, ret, timeout;
	u8 port_sts1[4] = {0, 0, 0, 0}, port_sts2[4] = {0, 0, 0, 0};

	dev_info(&client->dev, "LINKs=%d, LANES=%d, FORWARDING=%s, CABLE=%s, ID=%s\n",
			       priv->n_links, priv->lanes, priv->forwarding_mode, priv->is_coax ? "coax" : "stp", priv->chip_id);

	ub9x4_initial_setup(client);

	for (i = 0; i < priv->n_links; i++) {
		if (!IS_ERR(priv->poc_reg[i])) {
			ret = regulator_enable(priv->poc_reg[i]); /* POC power on */
			if (ret) {
				dev_err(&client->dev, "failed to enable poc regulator\n");
				continue;
			}
			mdelay(priv->poc_delay);
		}

		ub9x4_fpdlink3_setup(client, i);
	}

	client->addr = priv->des_addr;

	/* check lock status */
	for (timeout = 500 / priv->n_links; timeout > 0; timeout--) {
		for (i = 0; i < priv->n_links; i++) {
			if ((port_sts1[i] & 0x1) && (port_sts2[i] & 0x4))
				continue;

			reg8_write(client, 0x4c, (i << 4) | (1 << i));	/* Select RX port number */
			usleep_range(1000, 1500);
			reg8_read(client, 0x4d, &port_sts1[i]);		/* Lock status */
			reg8_read(client, 0x4e, &port_sts2[i]);		/* Freq stable */
		}
	}

	if (!timeout)
		dev_info(&client->dev, "Receiver lock status [%d,%d,%d,%d]\n",
				       (port_sts1[0] & 0x1) && (port_sts2[0] & 0x4),
				       (port_sts1[1] & 0x1) && (port_sts2[1] & 0x4),
				       (port_sts1[2] & 0x1) && (port_sts2[2] & 0x4),
				       (port_sts1[3] & 0x1) && (port_sts2[3] & 0x4));

	if (priv->poc_delay)
		mdelay(100);

	for (i = 0; i < priv->n_links; i++) {
		if (!((port_sts1[i] & 0x1) && (port_sts2[i] & 0x4)))
			continue;

		reg8_write(client, 0x4c, (i << 4) | (1 << i));			/* Select RX port number */
		usleep_range(1000, 1500);

		/*
		 * Enable only FSIN for remote gpio, all permanent states (0 or 1) setup on serializer side:
		 * this avoids intermittent remote gpio noise (f.e. reset or spuriouse fsin) caused by
		 * unstable/bad link, hence unstable backchannel
		 */
		client->addr = priv->ser_addr[i];				/* UB9X3 I2C addr */
		switch (priv->ser_id) {
		case UB913_ID:
			reg8_write(client, 0x0d, 0x55);				/* Enable remote GPIO0/1 */
			reg8_write(client, 0x11, 0x10);				/* I2C high pulse width */
			reg8_write(client, 0x12, 0x10);				/* I2C low pulse width */
			break;
		case UB953_ID:
			reg8_write(client, 0x0d, (priv->gpio[0] & 0x1) << 0 |
						 (priv->gpio[1] & 0x1) << 1 |
						 (priv->gpio[2] & 0x1) << 2 |
						 (priv->gpio[3] & 0x1) << 3 |
						 (priv->gpio[0] & 0x2) << 3 |
						 (priv->gpio[1] & 0x2) << 4 |
						 (priv->gpio[2] & 0x2) << 5 |
						 (priv->gpio[3] & 0x2) << 6);	/* Enable FSIN remote GPIOs and set local constant gpios */
			reg8_write(client, 0x0e, (!!priv->gpio[0] << 4) |
						 (!!priv->gpio[1] << 5) |
						 (!!priv->gpio[2] << 6) |
						 (!!priv->gpio[3] << 7));	/* Enable serializer GPIOs only for output */
			reg8_write(client, 0x0b, 0x10);				/* I2C high pulse width */
			reg8_write(client, 0x0c, 0x10);				/* I2C low pulse width */
			break;
		}
		client->addr = priv->des_addr;

		reg8_write(client, 0x6e, 0x88 | (priv->gpio[1] << 4) | priv->gpio[0]); /* Remote GPIO1/GPIO0 setup */
		reg8_write(client, 0x6f, 0x88 | (priv->gpio[3] << 4) | priv->gpio[2]); /* Remote GPIO3/GPIO2 setup */

		priv->links_mask |= BIT(i);
		i2c_mux_add_adapter(priv->mux, 0, i, 0);
	}

	return 0;
}

static int ub9x4_post_initialize(struct i2c_client *client)
{
	struct ub9x4_priv *priv = i2c_get_clientdata(client);

	reg8_write(client, 0x33, ((priv->lanes - 1) ^ 0x3) << 4 | 0x1); /* enable CSI output, set CSI lane count, non-continuous CSI mode */
	reg8_write(client, 0x18, 0x01);					/* Enable FrameSync, HI/LO mode, Frame clock from port0 */
//	reg8_write(client, 0x18, 0x80);					/* Enable FrameSync, Frame clock is external */

	return 0;
}

static int ub9x4_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ub9x4_priv *priv = v4l2_get_subdevdata(sd);
	struct i2c_client *client = priv->client;

	if (enable) {
		if (atomic_inc_return(&priv->use_count) == 1)
			reg8_write(client, 0x20, 0x00 | priv->csi_map); /* enable port forwarding to CSI */
	} else {
		if (atomic_dec_return(&priv->use_count) == 0)
			reg8_write(client, 0x20, 0xf0 | priv->csi_map); /* disable port forwarding to CSI */
	}

	return 0;
}

static int ub9x4_g_frame_interval(struct v4l2_subdev *sd,
				  struct v4l2_subdev_frame_interval *ival)
{
	return 0;
}

static int ub9x4_s_frame_interval(struct v4l2_subdev *sd,
				  struct v4l2_subdev_frame_interval *ival)
{
	struct ub9x4_priv *priv = v4l2_get_subdevdata(sd);
	struct i2c_client *client = priv->client;
	struct v4l2_fract *tpf = &ival->interval;

	if (priv->fps_denominator != tpf->denominator ||
	    priv->fps_numerator != tpf->numerator) {
		int f_time;

		f_time = priv->fs_time * 30 * tpf->numerator / tpf->denominator;
		reg8_write(client, 0x1b, f_time >> 8);			/* FrameSync low time MSB */
		reg8_write(client, 0x1c, f_time & 0xff);		/* FrameSync low time LSB */

		priv->fps_denominator = tpf->denominator;
		priv->fps_numerator = tpf->numerator;
	}

	return 0;
}

static const struct v4l2_subdev_video_ops ub9x4_video_ops = {
	.s_stream = ub9x4_s_stream,
	.g_frame_interval = ub9x4_g_frame_interval,
	.s_frame_interval = ub9x4_s_frame_interval,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ub9x4_g_register(struct v4l2_subdev *sd,
				      struct v4l2_dbg_register *reg)
{
	struct ub9x4_priv *priv = v4l2_get_subdevdata(sd);
	struct i2c_client *client = priv->client;
	int ret;
	u8 val = 0;

	ret = reg8_read(client, (u8)reg->reg, &val);
	if (ret < 0)
		return ret;

	reg->val = val;
	reg->size = sizeof(u8);

	return 0;
}

static int ub9x4_s_register(struct v4l2_subdev *sd,
				      const struct v4l2_dbg_register *reg)
{
	struct ub9x4_priv *priv = v4l2_get_subdevdata(sd);
	struct i2c_client *client = priv->client;

	return reg8_write(client, (u8)reg->reg, (u8)reg->val);
}
#endif

static int ub9x4_reboot_notifier(struct notifier_block *nb, unsigned long event, void *buf)
{
	struct ub9x4_priv *priv = container_of(nb, struct ub9x4_priv, reboot_notifier);
	int i;

	for (i = 0; i < priv->n_links; i++) {
		if (!IS_ERR(priv->poc_reg[i]))
			regulator_disable(priv->poc_reg[i]); /* POC power off */
	}

	return NOTIFY_DONE;
}

static struct v4l2_subdev_core_ops ub9x4_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ub9x4_g_register,
	.s_register = ub9x4_s_register,
#endif
};

static struct v4l2_subdev_ops ub9x4_subdev_ops = {
	.core = &ub9x4_subdev_core_ops,
	.video = &ub9x4_video_ops,
};

/* -----------------------------------------------------------------------------
 * I2C Multiplexer
 */

static int ub9x4_i2c_mux_select(struct i2c_mux_core *muxc, u32 chan)
{
	/* Do nothing! */
	return 0;
}

static int ub9x4_i2c_mux_init(struct ub9x4_priv *priv)
{
	struct i2c_client *client = priv->client;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	priv->mux = i2c_mux_alloc(client->adapter, &client->dev,
				  priv->n_links, 0, I2C_MUX_LOCKED,
				  ub9x4_i2c_mux_select, NULL);
	if (!priv->mux)
		return -ENOMEM;

	priv->mux->priv = priv;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Async handling and registration of subdevices and links.
 */

static int ub9x4_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_subdev *asd)
{
	struct ub9x4_priv *priv = v4l2_get_subdevdata(notifier->sd);
	struct ub9x4_sink *sink = asd_to_ub9x4_sink(asd);
	int sink_pad = sink - &priv->sinks[0];
	int src_pad;

	src_pad = media_entity_get_fwnode_pad(&subdev->entity, sink->fwnode, MEDIA_PAD_FL_SOURCE);
	if (src_pad < 0) {
		dev_err(&priv->client->dev, "Failed to find pad for %s\n", subdev->name);
		return src_pad;
	}

	sink->sd = subdev;

	dev_dbg(&priv->client->dev, "Bound %s:%u -> %s:%u\n",
		subdev->name, src_pad, priv->sd.name, sink_pad);

	return media_create_pad_link(&subdev->entity, src_pad,
				     &priv->sd.entity, sink_pad,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static void ub9x4_notify_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct ub9x4_priv *priv = v4l2_get_subdevdata(notifier->sd);
	struct ub9x4_sink *sink = asd_to_ub9x4_sink(asd);

	sink->sd = NULL;

	dev_dbg(&priv->client->dev, "Unbind %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations ub9x4_notify_ops = {
	.bound = ub9x4_notify_bound,
	.unbind = ub9x4_notify_unbind,
};

static int ub9x4_v4l2_init(struct i2c_client *client)
{
	struct ub9x4_priv *priv = i2c_get_clientdata(client);
	struct device_node *ep;
	unsigned int i;
	int err;
	long pixel_rate;

	v4l2_async_notifier_init(&priv->notifier);

	for (i = 0; i < priv->n_links; i++) {
		if (!(priv->links_mask & (1 << i)))
			continue;
		err = v4l2_async_notifier_add_subdev(&priv->notifier, priv->subdevs[i]);
		if (err < 0)
			return err;
	}

	priv->notifier.ops = &ub9x4_notify_ops;
	err = v4l2_async_subdev_notifier_register(&priv->sd, &priv->notifier);
	if (err < 0)
		return err;

	v4l2_i2c_subdev_init(&priv->sd, client, &ub9x4_subdev_ops);
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Only YUV422 bpp=16 supported atm, decode to pixel_rate from fixed csi_rate */
	pixel_rate = priv->csi_rate / priv->lanes * 1000000;
	v4l2_ctrl_handler_init(&priv->ctrls, 1);
	v4l2_ctrl_new_std(&priv->ctrls, NULL, V4L2_CID_PIXEL_RATE,
			  pixel_rate, pixel_rate, 1, pixel_rate);
	priv->sd.ctrl_handler = &priv->ctrls;
	err = priv->ctrls.error;
	if (err)
		return err;

	/* Pads init */
	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	priv->pads[UB9X4_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	priv->pads[UB9X4_SINK_LINK0].flags = MEDIA_PAD_FL_SINK;
	priv->pads[UB9X4_SINK_LINK1].flags = MEDIA_PAD_FL_SINK;
	priv->pads[UB9X4_SINK_LINK2].flags = MEDIA_PAD_FL_SINK;
	priv->pads[UB9X4_SINK_LINK3].flags = MEDIA_PAD_FL_SINK;
	err = media_entity_pads_init(&priv->sd.entity, UB9X4_N_PADS, priv->pads);
	if (err)
		return err;

	/* Subdevice register */
	ep = of_graph_get_endpoint_by_regs(client->dev.of_node, UB9X4_SOURCE, -1);
	if (!ep) {
		dev_err(&client->dev, "Unable to retrieve endpoint on \"port@4\"\n");
		return -ENOENT;
	}
	priv->sd.fwnode = of_fwnode_handle(ep);
	v4l2_set_subdevdata(&priv->sd, priv);

	of_node_put(ep);

	err = v4l2_async_register_subdev(&priv->sd);
	if (err < 0) {
		dev_err(&client->dev, "Unable to register subdevice\n");
		goto err_put_node;
	}

	return 0;

err_put_node:
	of_node_put(ep);
	return err;
}

static int ub9x4_parse_dt(struct i2c_client *client)
{
	struct ub9x4_priv *priv = i2c_get_clientdata(client);
	struct device_node *np = client->dev.of_node;
	struct device_node *endpoint = NULL;
	int err, i;
	int sensor_delay;
	struct property *prop;
	u8 val = 0;
	struct gpio_desc *pwdn_gpio;
	u32 addrs[4], naddrs;

	i = of_property_match_string(np, "reg-names", "ub9x4");
	if (i >= 0)
		of_property_read_u32_index(np, "reg", i, (unsigned int *)&client->addr);
	priv->des_addr = client->addr;

	naddrs = of_property_count_elems_of_size(np, "regs", sizeof(u32));
	err = of_property_read_u32_array(client->dev.of_node, "regs", addrs,
					 naddrs);
	if (err < 0) {
		dev_err(&client->dev, "Invalid DT regs property\n");
		return -EINVAL;
	}
	priv->n_links = naddrs;
	memcpy(priv->ser_addr, addrs, naddrs * sizeof(u32));

	priv->ref_clk = v4l2_clk_get(&client->dev, "ref_clk");
	if (!IS_ERR(priv->ref_clk)) {
		dev_info(&client->dev, "ref_clk = %luKHz", v4l2_clk_get_rate(priv->ref_clk) / 1000);
		v4l2_clk_enable(priv->ref_clk);
	}

	pwdn_gpio = devm_gpiod_get(&client->dev, "shutdown", GPIOD_OUT_HIGH);
	if (!IS_ERR(pwdn_gpio)) {
		mdelay(5);
		gpiod_direction_output(pwdn_gpio, 0);
		mdelay(5);
	}

	for (i = 0; i < priv->n_links; i++) {
		char poc_name[10];

		sprintf(poc_name, "poc%d", i);
		priv->poc_reg[i] = devm_regulator_get(&client->dev, poc_name);
		if (PTR_ERR(priv->poc_reg[i]) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	}

	reg8_read(client, 0x00, &val); /* read ub9x4 ID: self i2c address */
	if (val != (priv->des_addr << 1))
		return -ENODEV;

	ub9x4_read_chipid(client);

	if (!of_property_read_u32(np, "ti,sensor_delay", &sensor_delay))
		mdelay(sensor_delay);
	if (of_property_read_string(np, "ti,forwarding-mode", (const char **)&priv->forwarding_mode))
		strncpy(priv->forwarding_mode, "round-robin", 16);
	if (of_property_read_bool(np, "ti,stp"))
		priv->is_coax = 0;
	else
		priv->is_coax = 1;
	if (of_property_read_u32(np, "ti,dvp_bus", &priv->dvp_bus))
		priv->dvp_bus = 8;
	if (of_property_read_bool(np, "ti,dvp_lsb"))
		priv->dvp_lsb = 1;
	else
		priv->dvp_lsb = 0;
	if (of_property_read_u32(np, "ti,hsync", &priv->hsync))
		priv->hsync = 0;
	if (of_property_read_u32(np, "ti,vsync", &priv->vsync))
		priv->vsync = 1;
	if (of_property_read_u32(np, "ti,ser_id", &priv->ser_id))
		priv->ser_id = UB913_ID;
	if (of_property_read_u32(np, "ti,poc-delay", &priv->poc_delay))
		priv->poc_delay = 10;
	if (of_property_read_u32(np, "ti,csi-rate", &priv->csi_rate))
		priv->csi_rate = 1450;
	if (of_property_read_u32(np, "ti,vc-map", &priv->vc_map))
		priv->vc_map = 0x3210;
	for (i = 0; i < 4; i++) {
		char name[10];

		sprintf(name, "ti,gpio%d", i);
		if (of_property_read_u32(np, name, &priv->gpio[i]))
			priv->gpio[i] = 0;
	}

	/*
	 * CSI forwarding of all links is to CSI0 by default.
	 * Decide if any link will be forwarded to CSI1 instead CSI0
	 */
	prop = of_find_property(np, "ti,csi1-links", NULL);
	if (prop) {
		const __be32 *link = NULL;
		u32 v;

		for (i = 0; i < 4; i++) {
			link = of_prop_next_u32(prop, link, &v);
			if (!link)
				break;
			priv->csi_map |= BIT(v);
		}
	} else {
		priv->csi_map = 0;
	}

	/* module params override dts */
	if (is_stp)
		priv->is_coax = 0;
	if (dvp_bus != 8)
		priv->dvp_bus = dvp_bus;
	if (dvp_lsb)
		priv->dvp_lsb = dvp_lsb;
	if (hsync)
		priv->hsync = hsync;
	if (!vsync)
		priv->vsync = vsync;
	if (ser_id)
		priv->ser_id = ser_id;
	if (poc_delay)
		priv->poc_delay = poc_delay;
	if (vc_map != 0x3210)
		priv->vc_map = vc_map;
	if (csi_map)
		priv->csi_map = csi_map;
	if (gpio0)
		priv->gpio[0] = gpio0;
	if (gpio1)
		priv->gpio[1] = gpio1;
	if (gpio2)
		priv->gpio[2] = gpio2;
	if (gpio3)
		priv->gpio[3] = gpio3;

	for_each_endpoint_of_node(np, endpoint) {
		struct ub9x4_sink *sink;
		struct of_endpoint ep;

		of_graph_parse_endpoint(endpoint, &ep);
		dev_dbg(&client->dev, "Endpoint %pOF on port %d", ep.local_node, ep.port);

		if (ep.port > UB9X4_N_LINKS) {
			dev_err(&client->dev, "Invalid endpoint %s on port %d",
				of_node_full_name(ep.local_node), ep.port);
			continue;
		}

		if (ep.port == UB9X4_SOURCE) {
			struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = 0 };

			err = v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint), &v4l2_ep);
			if (err) {
				of_node_put(endpoint);
				return err;
			}

			if (v4l2_ep.bus_type != V4L2_MBUS_CSI2_DPHY) {
				dev_err(&client->dev, "Unsupported bus: %u\n", v4l2_ep.bus_type);
				of_node_put(endpoint);
				return -EINVAL;
			}

			priv->lanes = v4l2_ep.bus.mipi_csi2.num_data_lanes;

			continue;
		}

		sink = &priv->sinks[ep.port];
		sink->fwnode = fwnode_graph_get_remote_endpoint(of_fwnode_handle(endpoint));
		if (!sink->fwnode) {
			dev_err(&client->dev, "Endpoint %pOF has no remote endpoint connection\n", ep.local_node);
			continue;
		}

		sink->asd.match_type = V4L2_ASYNC_MATCH_FWNODE;
		sink->asd.match.fwnode = sink->fwnode;

		priv->subdevs[ep.port] = &sink->asd;
	}

	of_node_put(endpoint);

	return 0;
}

static int ub9x4_probe(struct i2c_client *client,
		       const struct i2c_device_id *did)
{
	struct ub9x4_priv *priv;
	int err;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_clientdata(client, priv);
	priv->client = client;
	atomic_set(&priv->use_count, 0);
	priv->fps_numerator = 1;
	priv->fps_denominator = 30;

	err = ub9x4_parse_dt(client);
	if (err)
		goto out;

	err = ub9x4_i2c_mux_init(priv);
	if (err) {
		dev_err(&client->dev, "Unable to initialize I2C multiplexer\n");
		goto out;
	}

	err = ub9x4_initialize(client);
	if (err < 0)
		goto out;

	err = ub9x4_v4l2_init(client);
	if (err < 0)
		goto out;

	/* FIXIT: v4l2_i2c_subdev_init re-assigned clientdata */
	i2c_set_clientdata(client, priv);
	ub9x4_post_initialize(client);

	priv->reboot_notifier.notifier_call = ub9x4_reboot_notifier;
	err = register_reboot_notifier(&priv->reboot_notifier);
	if (err)
		dev_err(&client->dev, "failed to register reboot notifier\n");

out:
	return err;
}

static int ub9x4_remove(struct i2c_client *client)
{
	struct ub9x4_priv *priv = i2c_get_clientdata(client);
	int i;

	unregister_reboot_notifier(&priv->reboot_notifier);

	i2c_mux_del_adapters(priv->mux);
	v4l2_async_notifier_unregister(&priv->notifier);
	v4l2_async_notifier_cleanup(&priv->notifier);
	v4l2_async_unregister_subdev(&priv->sd);

	for (i = 0; i < priv->n_links; i++) {
		if (!IS_ERR(priv->poc_reg[i]))
			regulator_disable(priv->poc_reg[i]); /* POC power off */
	}

	return 0;
}

static const struct of_device_id ub9x4_dt_ids[] = {
	{ .compatible = "ti,ub9x4" },
	{},
};
MODULE_DEVICE_TABLE(of, ub9x4_dt_ids);

static const struct i2c_device_id ub9x4_id[] = {
	{ "ub9x4", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ub9x4_id);

static struct i2c_driver ub9x4_i2c_driver = {
	.driver = {
		.name = "ub9x4",
		.of_match_table = of_match_ptr(ub9x4_dt_ids),
	},
	.probe = ub9x4_probe,
	.remove = ub9x4_remove,
	.id_table = ub9x4_id,
};

module_i2c_driver(ub9x4_i2c_driver);

MODULE_DESCRIPTION("FPDLinkIII driver for ds90ub9x4");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
