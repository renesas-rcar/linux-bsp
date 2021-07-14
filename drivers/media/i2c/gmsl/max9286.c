/*
 * MAXIM max9286 GMSL driver
 *
 * Copyright (C) 2015-2020 Cogent Embedded, Inc.
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
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "common.h"

#define MAX9286_N_LINKS		4

enum max9286_pads {
	MAX9286_SINK_LINK0,
	MAX9286_SINK_LINK1,
	MAX9286_SINK_LINK2,
	MAX9286_SINK_LINK3,
	MAX9286_SOURCE,
	MAX9286_N_PADS,
};

struct max9286_sink {
	struct v4l2_async_subdev asd;
	struct v4l2_subdev	*sd;
	struct fwnode_handle	*fwnode;
};

#define asd_to_max9286_sink(_asd) \
	container_of(_asd, struct max9286_sink, asd)

struct max9286_priv {
	struct i2c_client	*client;
	struct v4l2_subdev	sd;
	struct media_pad	pads[MAX9286_N_PADS];

	struct i2c_mux_core	*mux;

	struct max9286_sink	sinks[MAX9286_N_LINKS];
	struct v4l2_async_subdev *subdevs[MAX9286_N_LINKS];
	struct v4l2_async_notifier notifier;
	struct v4l2_ctrl_handler ctrls;

	int			des_addr;
	int			des_quirk_addr; /* second MAX9286 on the same I2C bus */
	int			n_links;
	int			links_mask;
	int			lanes;
	long			pixel_rate;
	const char		*fsync_mode;
	int			fsync_period;
	int			pclk;
	char			pclk_rising_edge;
	int			gpio_resetb;
	int			active_low_resetb;
	int			him;
	int			hsync;
	int			vsync;
	int			timeout;
	int			poc_delay;
	int			bws;
	int			dbl;
	int			dt;
	int			hsgen;
	u64			crossbar;
	char			cb[16];
	int			hts;
	int			vts;
	int			hts_delay;
	atomic_t		use_count;
	u32			csi2_outord;
	u32			switchin;
	int			ser_addr[4];
	int			ser_id;
	struct regulator	*poc_reg[4]; /* PoC power supply */
	struct notifier_block	reboot_notifier;
	/* link statistic */
	int			prbserr[4];
	int			deterr[4];
	int			correrr[4];
};

#include "max9286_debug.h"

static char fsync_mode_default[20] = "automatic"; /* manual, automatic, semi-automatic, external */

static int conf_link;
module_param(conf_link, int, 0644);
MODULE_PARM_DESC(conf_link, " Force configuration link. Used only if robust firmware flashing required (f.e. recovery)");

static int poc_trig;
module_param(poc_trig, int, 0644);
MODULE_PARM_DESC(poc_trig, " Use PoC triggering during reverse channel setup. Useful on systems with dedicated PoC and unstable ser-des lock");

static int him;
module_param(him, int, 0644);
MODULE_PARM_DESC(him, " Use High-Immunity mode (default: leagacy mode)");

static int fsync_period;
module_param(fsync_period, int, 0644);
MODULE_PARM_DESC(fsync_period, " Frame sync period (default: 3.2MHz)");

static int hsync;
module_param(hsync, int, 0644);
MODULE_PARM_DESC(hsync, " HSYNC invertion (default: 0 - not inverted)");

static int vsync = 1;
module_param(vsync, int, 0644);
MODULE_PARM_DESC(vsync, " VSYNC invertion (default: 1 - inverted)");

static int gpio_resetb;
module_param(gpio_resetb, int, 0644);
MODULE_PARM_DESC(gpio_resetb, " Serializer GPIO reset (default: 0 - not used)");

static int active_low_resetb;
module_param(active_low_resetb, int, 0644);
MODULE_PARM_DESC(active_low_resetb, " Serializer GPIO reset level (default: 0 - active high)");

static int timeout_n = 100;
module_param(timeout_n, int, 0644);
MODULE_PARM_DESC(timeout_n, " Timeout of link detection (default: 100 retries)");

static int poc_delay = 50;
module_param(poc_delay, int, 0644);
MODULE_PARM_DESC(poc_delay, " Delay in ms after POC enable (default: 50 ms)");

static int bws;
module_param(bws, int, 0644);
MODULE_PARM_DESC(bws, " BWS mode (default: 0 - 24-bit gmsl packets)");

static int dbl = 1;
module_param(dbl, int, 0644);
MODULE_PARM_DESC(dbl, " DBL mode (default: 1 - DBL mode enabled)");

static int dt = 3;
module_param(dt, int, 0644);
MODULE_PARM_DESC(dt, " DataType (default: 3 - YUV8), 0 - RGB888, 5 - RAW8, 6 - RAW10, 7 - RAW12, 8 - RAW14");

static int hsgen;
module_param(hsgen, int, 0644);
MODULE_PARM_DESC(hsgen, " Enable HS embedded generator (default: 0 - disabled)");

static int pclk = 100;
module_param(pclk, int, 0644);
MODULE_PARM_DESC(pclk, " PCLK rate (default: 100MHz)");

static int switchin = 0;
module_param(switchin, int, 0644);
MODULE_PARM_DESC(switchin, " COAX SWITCH IN+ and IN- (default: 0 - not switched)");

static unsigned long crossbar = 0xba9876543210;
module_param(crossbar, ulong, 0644);
MODULE_PARM_DESC(crossbar, " Crossbar setup (default: ba9876543210 - reversed)");

enum {
	RGB888_DT = 0,
	RGB565_DT,
	RGB666_DT,
	YUV8_DT, /* default */
	YUV10_DT,
	RAW8_DT,
	RAW10_DT,
	RAW12_DT,
	RAW14_DT,
};

static int dt2bpp[9] = {
	24,	/* RGB888 */
	16,	/* RGB565 */
	18,	/* RGB666 */
	8,	/* YUV8 - default */
	10,	/* YUV10 */
	8,	/* RAW8/RAW16 */
	10,	/* RAW10 */
	12,	/* RAW12 */
	14,	/* RAW14 */
};

static void max9286_write_remote_verify(struct i2c_client *client, int idx, u8 reg, u8 val)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);
	int timeout;

	for (timeout = 0; timeout < 10; timeout++) {
		int tmp_addr;
		u8 sts = 0;
		u8 val2 = 0;

		reg8_write(client, reg, val);

		tmp_addr = client->addr;
		client->addr = priv->des_addr;
		reg8_read(client, 0x70, &sts);
		client->addr = tmp_addr;
		if (sts & BIT(idx)) /* if ACKed */ {
			reg8_read(client, reg, &val2);
			if (val2 == val)
				break;
		}

		usleep_range(1000, 1500);
	}

	if (timeout >= 10)
		dev_err(&client->dev, "timeout remote write acked\n");
}

static void max9286_preinit(struct i2c_client *client, int addr)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);

	client->addr = addr;
	reg8_write(client, 0x0a, 0x00);		/* disable reverse control for all cams */
	reg8_write(client, 0x00, 0x00);		/* disable all GMSL links [0:3] */
//	usleep_range(2000, 2500);
	reg8_write(client, 0x1b, priv->switchin); /* coax polarity (default - normal) */
	reg8_write(client, 0x1c, (priv->him ? 0xf0 : 0x00) |
				 (priv->bws ? 0x05 : 0x04)); /* high-immunity/legacy mode, BWS 24bit */
}

static void max9286_sensor_reset(struct i2c_client *client, int addr, int reset_on)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);

	if (priv->ser_id == MAX96707_ID)
		return;

	if (priv->gpio_resetb < 1 || priv->gpio_resetb > 5)
		return;

	if (priv->active_low_resetb)
		reset_on = !reset_on;

	/* sensor reset/unreset using serializer gpio */
	client->addr = addr;
	reg8_write(client, 0x0f, (0xfe & ~BIT(priv->gpio_resetb)) | (reset_on ? BIT(priv->gpio_resetb) : 0)); /* set GPIOn value */
	reg8_write(client, 0x0e, 0x42 | BIT(priv->gpio_resetb)); /* set GPIOn direction output */
}

static void max9286_postinit(struct i2c_client *client, int addr)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);
	int idx;

	for (idx = 0; idx < priv->n_links; idx++) {
		if (priv->ser_id == MAX96705_ID || priv->ser_id == MAX96707_ID)
			continue;

		client->addr = priv->des_addr;
		reg8_write(client, 0x00, 0xe0 | BIT(idx));	/* enable GMSL link for CAMx */
		reg8_write(client, 0x0a, 0x11 << idx);		/* enable reverse/forward control for CAMx */
		usleep_range(5000, 5500);

		client->addr = priv->ser_addr[idx];
		max9286_sensor_reset(client, client->addr, 0);	/* sensor unreset using gpios. TODO: should be in imager driver */
	}

	client->addr = addr;
	reg8_write(client, 0x0a, 0x00);				/* disable reverse control for all cams */
	reg8_write(client, 0x00, 0xe0 | priv->links_mask);	/* enable GMSL link for CAMs */
	reg8_write(client, 0x0b, priv->csi2_outord);		/* CSI2 output order */
	reg8_write(client, 0x15, 0x9b);				/* enable CSI output, VC is set accordingly to Link number, BIT7 magic must be set */
	reg8_write(client, 0x1b, priv->switchin | priv->links_mask); /* coax polarity, enable equalizer for CAMs */
	reg8_write(client, 0x34, 0x22 | MAXIM_I2C_I2C_SPEED);	/* disable artificial ACK, I2C speed set */
	usleep_range(5000, 5500);

	if (strcmp(priv->fsync_mode, "manual") == 0) {
		reg8_write(client, 0x01, 0x00);			/* manual: FRAMESYNC set manually via [0x06:0x08] regs */
	} else if (strcmp(priv->fsync_mode, "automatic") == 0) {
		reg8_write(client, 0x01, 0x02);			/* automatic: FRAMESYNC taken from the slowest Link */
	} else if (strcmp(priv->fsync_mode, "semi-automatic") == 0) {
		reg8_write(client, 0x01, 0x01);			/* semi-automatic: FRAMESYNC taken from the slowest Link */
	} else if (strcmp(priv->fsync_mode, "external") == 0) {
		reg8_write(client, 0x01, 0xc0);			/* ECU (aka MCU) based FrameSync using GPI-to-GPO */
	}
}

static int max9286_reverse_channel_setup(struct i2c_client *client, int idx)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);
	u8 val = 0, lock_sts = 0, link_sts = 0;
	int timeout = priv->timeout;
	char timeout_str[40];
	int ret = 0;

	/* Reverse channel enable */
	client->addr = priv->des_addr;
	reg8_write(client, 0x34, 0xa2 | MAXIM_I2C_I2C_SPEED);	/* enable artificial ACKs, I2C speed set */
	usleep_range(2000, 2500);
	reg8_write(client, 0x00, 0xe0 | BIT(idx));		/* enable GMSL link for CAMx */
	reg8_write(client, 0x0a, 0x11 << idx);			/* enable reverse control for CAMx */
	usleep_range(2000, 2500);

	for (;;) {
		if (priv->him) {
			/* HIM mode setup */
			client->addr = 0x40;
			reg8_write(client, 0x4d, 0xc0);
			usleep_range(2000, 2500);
			reg8_write(client, 0x04, 0x43);		/* wake-up, enable reverse_control/conf_link */
			usleep_range(2000, 2500);
			if (priv->bws) {
				reg8_write(client, 0x07, 0x04 | (priv->pclk_rising_edge ? 0 : 0x10) |
							 (priv->dbl ? 0x80 : 0) |
							 (priv->bws ? 0x20 : 0)); /* RAW/YUV, PCLK edge, HS/VS encoding enabled, DBL mode, BWS 24/32-bit */
				usleep_range(2000, 2500);
			}
		} else {
			/* Legacy mode setup */
			client->addr = priv->des_addr;
			reg8_write(client, 0x3f, 0x4f);		/* enable custom reverse channel & first pulse length */
			reg8_write(client, 0x3b, 0x1e);		/* first pulse length rise time changed from 300ns to 200ns, amplitude 100mV */
			usleep_range(2000, 2500);

			client->addr = 0x40;
			reg8_write(client, 0x04, 0x43);		/* wake-up, enable reverse_control/conf_link */
			usleep_range(2000, 2500);
			reg8_write(client, 0x08, 0x01);		/* reverse channel receiver high threshold enable */
			reg8_write(client, 0x97, 0x5f);		/* enable reverse control channel programming (MAX96705-MAX96711 only) */
			usleep_range(2000, 2500);
			if (priv->bws) {
				reg8_write(client, 0x07, 0x04 | (priv->pclk_rising_edge ? 0 : 0x10) |
							 (priv->dbl ? 0x80 : 0) |
							 (priv->bws ? 0x20 : 0)); /* RAW/YUV, PCLK edge, HS/VS encoding enabled, DBL mode, BWS 24/32-bit */
				usleep_range(2000, 2500);
			}

			client->addr = priv->des_addr;
			reg8_write(client, 0x3b, 0x19);		/* reverse channel increase amplitude 170mV to compensate high threshold enabled */
			usleep_range(2000, 2500);
		}

		client->addr = 0x40;
		reg8_read(client, 0x1e, &val);			/* read max9271 ID */
		if (val == MAX9271_ID || val == MAX96705_ID || val == MAX96707_ID || --timeout == 0) {
			priv->ser_id = val;
			break;
		}

		/* Check if already initialized (after reboot/reset ?) */
		client->addr = priv->ser_addr[idx];
		reg8_read(client, 0x1e, &val);			/* read max9271 ID */
		if (val == MAX9271_ID || val == MAX96705_ID || val == MAX96707_ID) {
			priv->ser_id = val;
			reg8_write(client, 0x04, 0x43);		/* enable reverse_control/conf_link */
			usleep_range(2000, 2500);
			ret = -EADDRINUSE;
			break;
		}

		if (poc_trig) {
			if (!IS_ERR(priv->poc_reg[idx]) && (timeout % poc_trig == 0)) {
				regulator_disable(priv->poc_reg[idx]); /* POC power off */
				mdelay(200);
				ret = regulator_enable(priv->poc_reg[idx]); /* POC power on */
				if (ret)
					dev_err(&client->dev, "failed to enable poc regulator\n");
				mdelay(priv->poc_delay);
			}
		}
	}

	max9286_sensor_reset(client, client->addr, 1);	/* sensor reset */

	client->addr = priv->des_addr;			/* MAX9286-CAMx I2C */
	reg8_read(client, 0x27, &lock_sts);		/* LOCK status */
	reg8_read(client, 0x49, &link_sts);		/* LINK status */

	if (!timeout) {
		ret = -ETIMEDOUT;
		goto out;
	}

	priv->links_mask |= BIT(idx);
	priv->csi2_outord &= ~(0x3 << (idx * 2));
	priv->csi2_outord |= ((hweight8(priv->links_mask) - 1) << (idx * 2));

out:
	sprintf(timeout_str, "retries=%d lock_sts=%d link_sts=0x%x", priv->timeout - timeout, !!(lock_sts & 0x80), link_sts & (0x11 << idx));
	dev_info(&client->dev, "link%d %s %sat 0x%x %s %s\n", idx, chip_name(priv->ser_id),
			       ret == -EADDRINUSE ? "already " : "", priv->ser_addr[idx],
			       ret == -ETIMEDOUT ? "not found: timeout GMSL link establish" : "",
			       priv->timeout - timeout ? timeout_str : "");

	return ret;
}

static void max9286_initial_setup(struct i2c_client *client)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);

	/* Initial setup */
	client->addr = priv->des_addr;
	reg8_write(client, 0x15, 0x13);				/* disable CSI output, VC is set accordingly to Link number */
	reg8_write(client, 0x69, 0x0f);				/* mask CSI forwarding from all links */
	reg8_write(client, 0x12, ((priv->lanes - 1) << 6) |
				 (priv->dbl ? 0x30 : 0) |
				 (priv->dt & 0xf));		/* setup lanes, DBL mode, DataType */

	/* Start GMSL initialization with FSYNC disabled. This is required for some odd LVDS cameras */
	reg8_write(client, 0x01, 0xc0);				/* ECU (aka MCU) based FrameSync using GPI-to-GPO */
	reg8_write(client, 0x06, priv->fsync_period & 0xff);
	reg8_write(client, 0x07, (priv->fsync_period >> 8) & 0xff);
	reg8_write(client, 0x08, priv->fsync_period >> 16);

	reg8_write(client, 0x63, 0);				/* disable overlap window */
	reg8_write(client, 0x64, 0);
	reg8_write(client, 0x0c, 0x91 | (priv->vsync ? BIT(3) : 0) | (priv->hsync ? BIT(2) : 0)); /* enable HS/VS encoding, use D14/15 for HS/VS, invert HS/VS */
	reg8_write(client, 0x19, 0x0c);				/* Drive HSTRAIL state for 120ns after the last payload bit */
}

static void max9286_gmsl_link_setup(struct i2c_client *client, int idx)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);

	/* GMSL setup */
	client->addr = 0x40;
	reg8_write(client, 0x0d, 0x22 | MAXIM_I2C_I2C_SPEED);	/* disable artificial ACK, I2C speed set */
	reg8_write(client, 0x07, 0x04 | (priv->pclk_rising_edge ? 0 : 0x10) |
					(priv->dbl ? 0x80 : 0) |
					(priv->bws ? 0x20 : 0)); /* RAW/YUV, PCLK edge, HS/VS encoding enabled, DBL mode, BWS 24/32-bit */
	usleep_range(2000, 2500);
	reg8_write(client, 0x02, 0xff);				/* spread spectrum +-4%, pclk range automatic, Gbps automatic  */
	usleep_range(2000, 2500);

	if (priv->ser_id == MAX96705_ID || priv->ser_id == MAX96707_ID) {
		switch (priv->dt) {
		case YUV8_DT:
			/* setup crossbar for YUV8/RAW8: reverse DVP bus */
			reg8_write(client, 0x20, priv->cb[7]);
			reg8_write(client, 0x21, priv->cb[6]);
			reg8_write(client, 0x22, priv->cb[5]);
			reg8_write(client, 0x23, priv->cb[4]);
			reg8_write(client, 0x24, priv->cb[3]);
			reg8_write(client, 0x25, priv->cb[2]);
			reg8_write(client, 0x26, priv->cb[1]);
			reg8_write(client, 0x27, priv->cb[0]);

			/* this is second byte if DBL=1 */
			reg8_write(client, 0x30, priv->cb[7] + 16);
			reg8_write(client, 0x31, priv->cb[6] + 16);
			reg8_write(client, 0x32, priv->cb[5] + 16);
			reg8_write(client, 0x33, priv->cb[4] + 16);
			reg8_write(client, 0x34, priv->cb[3] + 16);
			reg8_write(client, 0x35, priv->cb[2] + 16);
			reg8_write(client, 0x36, priv->cb[1] + 16);
			reg8_write(client, 0x37, priv->cb[0] + 16);
			break;
		case RAW12_DT:
			/* setup crossbar for RAW12: reverse DVP bus */
			reg8_write(client, 0x20, priv->cb[11]);
			reg8_write(client, 0x21, priv->cb[10]);
			reg8_write(client, 0x22, priv->cb[9]);
			reg8_write(client, 0x23, priv->cb[8]);
			reg8_write(client, 0x24, priv->cb[7]);
			reg8_write(client, 0x25, priv->cb[6]);
			reg8_write(client, 0x26, priv->cb[5]);
			reg8_write(client, 0x27, priv->cb[4]);
			reg8_write(client, 0x28, priv->cb[3]);
			reg8_write(client, 0x29, priv->cb[2]);
			reg8_write(client, 0x2a, priv->cb[1]);
			reg8_write(client, 0x2b, priv->cb[0]);

			/* this is second byte if DBL=1 */
			reg8_write(client, 0x30, priv->cb[11] + 16);
			reg8_write(client, 0x31, priv->cb[10] + 16);
			reg8_write(client, 0x32, priv->cb[9] + 16);
			reg8_write(client, 0x33, priv->cb[8] + 16);
			reg8_write(client, 0x34, priv->cb[7] + 16);
			reg8_write(client, 0x35, priv->cb[6] + 16);
			reg8_write(client, 0x36, priv->cb[5] + 16);
			reg8_write(client, 0x37, priv->cb[4] + 16);
			reg8_write(client, 0x38, priv->cb[3] + 16);
			reg8_write(client, 0x39, priv->cb[2] + 16);
			reg8_write(client, 0x3a, priv->cb[1] + 16);
			reg8_write(client, 0x3b, priv->cb[0] + 16);

			if (!priv->bws && priv->dbl)
				dev_err(&client->dev, " BWS must be 27/32-bit for RAW12 in DBL mode\n");

			break;
		}

		if (priv->hsgen) {
			/* HS/VS pins map */
			reg8_write(client, 0x3f, 0x10);			/* HS (NC) */
			reg8_write(client, 0x41, 0x10);			/* DE (NC) */
			if (priv->ser_id == MAX96705_ID)
				reg8_write(client, 0x40, 15);		/* VS (DIN13) */
			if (priv->ser_id == MAX96707_ID)
				reg8_write(client, 0x40, 13);		/* VS (DIN13) */
#if 0
			/* following must come from imager */
#define SENSOR_WIDTH	(1280*2)
#define HTS		(1288*2)
#define VTS		960
#define HTS_DELAY	0x9
			reg8_write(client, 0x4e, HTS_DELAY >> 16);	/* HS delay */
			reg8_write(client, 0x4f, (HTS_DELAY >> 8) & 0xff);
			reg8_write(client, 0x50, HTS_DELAY & 0xff);
			reg8_write(client, 0x54, SENSOR_WIDTH >> 8);	/* HS high period */
			reg8_write(client, 0x55, SENSOR_WIDTH & 0xff);
			reg8_write(client, 0x56, (HTS - SENSOR_WIDTH) >> 8); /* HS low period */
			reg8_write(client, 0x57, (HTS - SENSOR_WIDTH) & 0xff);
			reg8_write(client, 0x58, VTS >> 8);		/* HS count */
			reg8_write(client, 0x59, VTS & 0xff );
#endif
			reg8_write(client, 0x43, 0x15);			/* enable HS generator */
		}
	}

	client->addr = priv->des_addr;
	reg8_write(client, 0x34, 0x22 | MAXIM_I2C_I2C_SPEED);	/* disable artificial ACK, I2C speed set */
	usleep_range(2000, 2500);

	/* I2C translator setup */
	client->addr = 0x40;
//	reg8_write(client, 0x09, maxim_map[2][idx] << 1);	/* SENSOR I2C translated - must be set by sensor driver */
//	reg8_write(client, 0x0A, 0x30 << 1);			/* SENSOR I2C native - must be set by sensor driver */
	reg8_write(client, 0x0B, BROADCAST << 1);		/* broadcast I2C */
	reg8_write(client, 0x0C, priv->ser_addr[idx] << 1);
	/* I2C addresse change */
	reg8_write(client, 0x01, priv->des_addr << 1);
	reg8_write(client, 0x00, priv->ser_addr[idx] << 1);
	usleep_range(2000, 2500);
	/* put MAX9271 in configuration link state  */
	client->addr = priv->ser_addr[idx];
	reg8_write(client, 0x04, 0x43);				/* enable reverse_control/conf_link */
	usleep_range(2000, 2500);
}

static int max9286_initialize(struct i2c_client *client)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);
	int i, ret;

	dev_info(&client->dev, "LINKs=%d, LANES=%d, FSYNC mode=%s, FSYNC period=%d, PCLK edge=%s\n",
			       priv->n_links, priv->lanes, priv->fsync_mode, priv->fsync_period,
			       priv->pclk_rising_edge ? "rising" : "falling");

	if (priv->des_quirk_addr)
		max9286_preinit(client, priv->des_quirk_addr);

	max9286_preinit(client, priv->des_addr);
	max9286_initial_setup(client);

	for (i = 0; i < priv->n_links; i++) {
		if (!IS_ERR(priv->poc_reg[i])) {
			ret = regulator_enable(priv->poc_reg[i]); /* POC power on */
			if (ret) {
				dev_err(&client->dev, "failed to enable poc regulator\n");
				continue;
			}
			mdelay(priv->poc_delay);
		}

		ret = max9286_reverse_channel_setup(client, i);
		if (ret == -ETIMEDOUT)
			continue;
		if (!ret)
			max9286_gmsl_link_setup(client, i);

		i2c_mux_add_adapter(priv->mux, 0, i, 0);
	}

	max9286_postinit(client, priv->des_addr);

	client->addr = priv->des_addr;

	return 0;
}

static int max9286_post_initialize(struct i2c_client *client)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);
	int idx;

	for (idx = 0; idx < priv->n_links; idx++) {
		if (!(priv->links_mask & (1 << idx)))
			continue;

		client->addr = priv->des_addr;
		reg8_write(client, 0x0a, 0x11 << idx); /* enable reverse/forward control for CAMx */
//		usleep_range(5000, 5500);

		/* switch to GMSL serial_link for streaming video */
		client->addr = priv->ser_addr[idx];
		max9286_write_remote_verify(client, idx, 0x04, conf_link ? 0x43 : 0x83);
	}

	client->addr = priv->des_addr;
	reg8_write(client, 0x0a, (priv->links_mask << 4) | priv->links_mask); /* enable reverse/forward control for all detected CAMs */

	return 0;
}

static int max9286_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct max9286_priv *priv = v4l2_get_subdevdata(sd);
	struct i2c_client *client = priv->client;

	if (enable) {
		if (atomic_inc_return(&priv->use_count) == 1)
			reg8_write(client, 0x69, priv->links_mask ^ 0x0f); /* unmask CSI forwarding from detected links */
	} else {
		if (atomic_dec_return(&priv->use_count) == 0)
			reg8_write(client, 0x69, 0x0f); /* mask CSI forwarding from all links */
	}

	return 0;
}

static const struct v4l2_subdev_video_ops max9286_video_ops = {
	.s_stream	= max9286_s_stream,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int max9286_g_register(struct v4l2_subdev *sd,
				      struct v4l2_dbg_register *reg)
{
	struct max9286_priv *priv = v4l2_get_subdevdata(sd);
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

static int max9286_s_register(struct v4l2_subdev *sd,
				      const struct v4l2_dbg_register *reg)
{
	struct max9286_priv *priv = v4l2_get_subdevdata(sd);
	struct i2c_client *client = priv->client;

	return reg8_write(client, (u8)reg->reg, (u8)reg->val);
}
#endif

static int max9286_reboot_notifier(struct notifier_block *nb, unsigned long event, void *buf)
{
	struct max9286_priv *priv = container_of(nb, struct max9286_priv, reboot_notifier);
	int i;

	for (i = 0; i < priv->n_links; i++) {
		if (!IS_ERR(priv->poc_reg[i]))
			regulator_disable(priv->poc_reg[i]); /* POC power off */
	}

	return NOTIFY_DONE;
}

static struct v4l2_subdev_core_ops max9286_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= max9286_g_register,
	.s_register	= max9286_s_register,
#endif
};

static const struct v4l2_subdev_ops max9286_subdev_ops = {
	.core		= &max9286_subdev_core_ops,
	.video		= &max9286_video_ops,
};

/* -----------------------------------------------------------------------------
 * I2C Multiplexer
 */

static int max9286_i2c_mux_select(struct i2c_mux_core *muxc, u32 chan)
{
	/* Do nothing! */
	return 0;
}

static int max9286_i2c_mux_init(struct max9286_priv *priv)
{
	struct i2c_client *client = priv->client;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	priv->mux = i2c_mux_alloc(client->adapter, &client->dev,
				  priv->n_links, 0, I2C_MUX_LOCKED,
				  max9286_i2c_mux_select, NULL);
	if (!priv->mux)
		return -ENOMEM;

	priv->mux->priv = priv;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Async handling and registration of subdevices and links.
 */

static int max9286_notify_bound(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct max9286_priv *priv = v4l2_get_subdevdata(notifier->sd);
	struct max9286_sink *sink = asd_to_max9286_sink(asd);
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

static void max9286_notify_unbind(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *subdev,
				  struct v4l2_async_subdev *asd)
{
	struct max9286_priv *priv = v4l2_get_subdevdata(notifier->sd);
	struct max9286_sink *sink = asd_to_max9286_sink(asd);

	sink->sd = NULL;

	dev_dbg(&priv->client->dev, "Unbind %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations max9286_notify_ops = {
	.bound = max9286_notify_bound,
	.unbind = max9286_notify_unbind,
};

static int max9286_v4l2_init(struct i2c_client *client)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);
	struct device_node *ep;
	unsigned int i;
	int err;

	v4l2_async_notifier_init(&priv->notifier);

	for (i = 0; i < priv->n_links; i++) {
		if (!(priv->links_mask & (1 << i)))
			continue;
		err = v4l2_async_notifier_add_subdev(&priv->notifier, priv->subdevs[i]);
		if (err < 0)
			return err;
	}

	priv->notifier.ops = &max9286_notify_ops;
	err = v4l2_async_subdev_notifier_register(&priv->sd, &priv->notifier);
	if (err < 0)
		return err;

	v4l2_i2c_subdev_init(&priv->sd, client, &max9286_subdev_ops);
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* CSI2_RATE = PCLK*bpp*n_links/lanes*/
	priv->pixel_rate = priv->pclk * 2 * dt2bpp[priv->dt] / 8 * hweight8(priv->links_mask) / priv->lanes * 1000000;
	v4l2_ctrl_handler_init(&priv->ctrls, 1);
	v4l2_ctrl_new_std(&priv->ctrls, NULL, V4L2_CID_PIXEL_RATE,
			  priv->pixel_rate, priv->pixel_rate, 1, priv->pixel_rate);
	priv->sd.ctrl_handler = &priv->ctrls;
	err = priv->ctrls.error;
	if (err)
		return err;

	/* Pads init */
	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	priv->pads[MAX9286_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	priv->pads[MAX9286_SINK_LINK0].flags = MEDIA_PAD_FL_SINK;
	priv->pads[MAX9286_SINK_LINK1].flags = MEDIA_PAD_FL_SINK;
	priv->pads[MAX9286_SINK_LINK2].flags = MEDIA_PAD_FL_SINK;
	priv->pads[MAX9286_SINK_LINK3].flags = MEDIA_PAD_FL_SINK;
	err = media_entity_pads_init(&priv->sd.entity, MAX9286_N_PADS, priv->pads);
	if (err)
		return err;

	/* Subdevice register */
	ep = of_graph_get_endpoint_by_regs(client->dev.of_node, MAX9286_SOURCE, -1);
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

static int max9286_parse_dt(struct i2c_client *client)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);
	struct device_node *np = client->dev.of_node;
	struct device_node *endpoint = NULL;
	int err, i;
	int sensor_delay, gpio0 = 1, gpio1 = 1;
	u8 val = 0;
	struct gpio_desc *pwdn_gpio;
	u32 addrs[4], naddrs;

	i = of_property_match_string(np, "reg-names", "max9286");
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

	pwdn_gpio = devm_gpiod_get_optional(&client->dev, "shutdown", GPIOD_OUT_HIGH);
	if (!IS_ERR(pwdn_gpio)) {
		udelay(5);
		gpiod_set_value_cansleep(pwdn_gpio, 0);
		mdelay(10);
	}

	for (i = 0; i < priv->n_links; i++) {
		char poc_name[10];

		sprintf(poc_name, "poc%d", i);
		priv->poc_reg[i] = devm_regulator_get(&client->dev, poc_name);
		if (PTR_ERR(priv->poc_reg[i]) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	}

	reg8_read(client, 0x1e, &val); /* read max9286 ID */
	if (val != MAX9286_ID)
		return -ENODEV;

	if (!of_property_read_u32(np, "maxim,gpio0", &gpio0) ||
	    !of_property_read_u32(np, "maxim,gpio1", &gpio1))
		reg8_write(client, 0x0f, 0x08 | (gpio1 << 1) | gpio0);

	if (of_property_read_u32(np, "maxim,resetb-gpio", &priv->gpio_resetb)) {
		priv->gpio_resetb = -1;
	} else {
		if (of_property_read_bool(np, "maxim,resetb-active-high"))
			priv->active_low_resetb = 0;
		else
			priv->active_low_resetb = 1;
	}

	if (!of_property_read_u32(np, "maxim,sensor_delay", &sensor_delay))
		mdelay(sensor_delay);
	if (of_property_read_string(np, "maxim,fsync-mode", &priv->fsync_mode))
		priv->fsync_mode = fsync_mode_default;
	if (of_property_read_u32(np, "maxim,fsync-period", &priv->fsync_period))
		priv->fsync_period = 3200000;			/* 96MHz/30fps */
	priv->pclk_rising_edge = true;
	if (of_property_read_bool(np, "maxim,pclk-falling-edge"))
		priv->pclk_rising_edge = false;
	if (of_property_read_u32(np, "maxim,timeout", &priv->timeout))
		priv->timeout = 100;
	if (of_property_read_u32(np, "maxim,i2c-quirk", &priv->des_quirk_addr))
		priv->des_quirk_addr = 0;
	if (of_property_read_u32(np, "maxim,him", &priv->him))
		priv->him = 0;
	if (of_property_read_u32(np, "maxim,hsync", &priv->hsync))
		priv->hsync = 0;
	if (of_property_read_u32(np, "maxim,vsync", &priv->vsync))
		priv->vsync = 1;
	if (of_property_read_u32(np, "maxim,poc-delay", &priv->poc_delay))
		priv->poc_delay = 50;
	if (of_property_read_u32(np, "maxim,bws", &priv->bws))
		priv->bws = 0;
	if (of_property_read_u32(np, "maxim,dbl", &priv->dbl))
		priv->dbl = 1;
	if (of_property_read_u32(np, "maxim,dt", &priv->dt))
		priv->dt = 3;
	if (of_property_read_u32(np, "maxim,hsgen", &priv->hsgen))
		priv->hsgen = 0;
	if (of_property_read_u32(np, "maxim,pclk", &priv->pclk))
		priv->pclk = pclk;
	if (of_property_read_u32(np, "maxim,switchin", &priv->switchin))
		priv->switchin = 0;
	if (of_property_read_u64(np, "maxim,crossbar", &priv->crossbar))
		priv->crossbar = crossbar;

	/* module params override dts */
	if (him)
		priv->him = him;
	if (fsync_period) {
		priv->fsync_period = fsync_period;
		strncpy(fsync_mode_default, "manual", 8);
		priv->fsync_mode = fsync_mode_default;
	}
	if (hsync)
		priv->hsync = hsync;
	if (!vsync)
		priv->vsync = vsync;
	if (gpio_resetb)
		priv->gpio_resetb = gpio_resetb;
	if (active_low_resetb)
		priv->active_low_resetb = active_low_resetb;
	if (timeout_n)
		priv->timeout = timeout_n;
	if (poc_delay)
		priv->poc_delay = poc_delay;
	if (bws)
		priv->bws = bws;
	if (!dbl)
		priv->dbl = dbl;
	if (dt != 3)
		priv->dt = dt;
	if (hsgen)
		priv->hsgen = hsgen;
	if (pclk != 100)
		priv->pclk = pclk;
	if (switchin)
		priv->switchin = switchin;

	/* parse crossbar setup */
	for (i = 0; i < 16; i++) {
		priv->cb[i] = priv->crossbar % 16;
		priv->crossbar /= 16;
	}

	for_each_endpoint_of_node(np, endpoint) {
		struct max9286_sink *sink;
		struct of_endpoint ep;

		of_graph_parse_endpoint(endpoint, &ep);
		dev_dbg(&client->dev, "Endpoint %pOF on port %d", ep.local_node, ep.port);

		if (ep.port > MAX9286_N_LINKS) {
			dev_err(&client->dev, "Invalid endpoint %s on port %d",
				of_node_full_name(ep.local_node), ep.port);
			continue;
		}

		if (ep.port == MAX9286_SOURCE) {
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

static int max9286_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct max9286_priv *priv;
	int err;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_clientdata(client, priv);
	priv->client = client;
	atomic_set(&priv->use_count, 0);
	priv->csi2_outord = 0xff;

	err = max9286_parse_dt(client);
	if (err)
		goto out;

	err = max9286_i2c_mux_init(priv);
	if (err) {
		dev_err(&client->dev, "Unable to initialize I2C multiplexer\n");
		goto out;
	}

	err = max9286_initialize(client);
	if (err < 0)
		goto out;

	err = max9286_v4l2_init(client);
	if (err < 0)
		goto out;

	/* FIXIT: v4l2_i2c_subdev_init re-assigned clientdata */
	i2c_set_clientdata(client, priv);
	max9286_post_initialize(client);

	priv->reboot_notifier.notifier_call = max9286_reboot_notifier;
	err = register_reboot_notifier(&priv->reboot_notifier);
	if (err) {
		dev_err(&client->dev, "failed to register reboot notifier\n");
		goto out;
	}

	err = sysfs_create_group(&client->dev.kobj, &max9286_group);
	if (err < 0)
		dev_err(&client->dev, "Sysfs registration failed\n");

out:
	return err;
}

static int max9286_remove(struct i2c_client *client)
{
	struct max9286_priv *priv = i2c_get_clientdata(client);
	int i;

	sysfs_remove_group(&client->dev.kobj,  &max9286_group);
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

static const struct of_device_id max9286_dt_ids[] = {
	{ .compatible = "maxim,max9286" },
	{},
};
MODULE_DEVICE_TABLE(of, max9286_dt_ids);

static const struct i2c_device_id max9286_id[] = {
	{ "max9286", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max9286_id);

static struct i2c_driver max9286_i2c_driver = {
	.driver	= {
		.name		= "max9286",
		.of_match_table	= of_match_ptr(max9286_dt_ids),
	},
	.probe		= max9286_probe,
	.remove		= max9286_remove,
	.id_table	= max9286_id,
};

static int __init max9286_init(void)
{
	return i2c_add_driver(&max9286_i2c_driver);
}
late_initcall(max9286_init);

static void __exit max9286_exit(void)
{
	i2c_del_driver(&max9286_i2c_driver);
}
module_exit(max9286_exit);
MODULE_DESCRIPTION("GMSL driver for MAX9286");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
