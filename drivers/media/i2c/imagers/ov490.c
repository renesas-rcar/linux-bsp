/*
 * OmniVision ov490-ov10640 sensor camera driver
 *
 * Copyright (C) 2016-2020 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/videodev2.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "../gmsl/common.h"
#include "ov490.h"

#define OV490_I2C_ADDR		0x24

#define OV490_PID_REGA		0x300a
#define OV490_PID_REGB		0x300b
#define OV490_PID		0x0490

#define OV490_ISP_HSIZE_LOW	0x60
#define OV490_ISP_HSIZE_HIGH	0x61
#define OV490_ISP_VSIZE_LOW	0x62
#define OV490_ISP_VSIZE_HIGH	0x63

#define OV490_MEDIA_BUS_FMT		MEDIA_BUS_FMT_UYVY8_2X8

struct ov490_priv {
	struct v4l2_subdev		sd;
	struct v4l2_ctrl_handler	hdl;
	struct media_pad		pad;
	struct v4l2_rect		rect;
	int				max_width;
	int				max_height;
	int				init_complete;
	u8				id[6];
	int				exposure;
	int				gain;
	int				autogain;
	int				red;
	int				green_r;
	int				green_b;
	int				blue;
	int				awb;
	int				dvp_order;
	int				group;
	int				vsync;
	/* serializers */
	int				ser_addr;
	int				des_addr;
	int				reset_gpio;
	int				active_low_resetb;
};

static int conf_link;
module_param(conf_link, int, 0644);
MODULE_PARM_DESC(conf_link, " Force configuration link. Used only if robust firmware flashing required (f.e. recovery)");

static int group = 0;
module_param(group, int, 0644);
MODULE_PARM_DESC(group, " group number (0 - does not apply)");

static int dvp_order = 0;
module_param(dvp_order, int, 0644);
MODULE_PARM_DESC(dvp_order, " DVP bus bits order");

static int reset_gpio = 0;
module_param(reset_gpio, int, 0644);
MODULE_PARM_DESC(reset_gpio, " serializer gpio number on imager RESETB");

static int vsync = 0;
module_param(vsync, int, 0644);
MODULE_PARM_DESC(vsync, " VSYNC invertion (default: 0 - not inverted)");

static inline struct ov490_priv *to_ov490(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov490_priv, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov490_priv, hdl)->sd;
}

static void ov490_reset(struct i2c_client *client)
{
	struct ov490_priv *priv = to_ov490(client);

	switch (get_des_id(client)) {
	case MAX9286_ID:
	case MAX9288_ID:
	case MAX9296A_ID:
	case MAX96712_ID:
		reg8_write_addr(client, priv->ser_addr, 0x0f, (0xfe & ~BIT(priv->reset_gpio))); /* set GPIOn value to reset */
		usleep_range(2000, 2500);
		reg8_write_addr(client, priv->ser_addr, 0x0f, 0xfe | BIT(priv->reset_gpio)); /* set GPIOn value to un-reset */
		usleep_range(2000, 2500);
		break;
	case UB960_ID:
		reg8_write_addr(client, get_des_addr(client), 0x6e, 0x8a);	/* set GPIO1 value to reset */
		usleep_range(2000, 2500);
		reg8_write_addr(client, get_des_addr(client), 0x6e, 0x9a);	/* set GPIO1 value to un-reset */
		usleep_range(2000, 2500);
		break;
	}
}

static int ov490_set_regs(struct i2c_client *client,
			  const struct ov490_reg *regs, int nr_regs)
{
	int i;

	for (i = 0; i < nr_regs; i++) {
		if (reg16_write(client, regs[i].reg, regs[i].val)) {
			usleep_range(100, 150); /* wait 100 us */
			reg16_write(client, regs[i].reg, regs[i].val);
		}

		if (regs[i].reg == 0xFFFE)
			usleep_range(100, 150); /* wait 100 us */
	}

	return 0;
}

static u8 ov490_ov10640_read(struct i2c_client *client, u16 addr)
{
	u8 reg_val = 0;

	reg16_write(client, 0xFFFD, 0x80);
	usleep_range(100, 150); /* wait 100 us */
	reg16_write(client, 0xFFFE, 0x19);
	usleep_range(100, 150); /* wait 100 us */
	reg16_write(client, 0x5000, 0x01); /* read operation */
	reg16_write(client, 0x5001, addr >> 8);
	reg16_write(client, 0x5002, addr & 0xff);
	reg16_write(client, 0xFFFE, 0x80);
	usleep_range(100, 150); /* wait 100 us */
	reg16_write(client, 0x00C0, 0xc1);
	reg16_write(client, 0xFFFE, 0x19);
	usleep_range(1000, 1500); /* wait 1 ms */
	reg16_read(client, 0x5000, &reg_val);

	return reg_val;
}

static void ov490_ov10640_write(struct i2c_client *client, u16 addr, u8 val)
{
	reg16_write(client, 0xFFFD, 0x80);
	usleep_range(100, 150); /* wait 100 us */
	reg16_write(client, 0xFFFE, 0x19);
	usleep_range(100, 150); /* wait 100 us */
	reg16_write(client, 0x5000, 0x00); /* write operation */
	reg16_write(client, 0x5001, addr >> 8);
	reg16_write(client, 0x5002, addr & 0xff);
	reg16_write(client, 0x5003, val);
	reg16_write(client, 0xFFFE, 0x80);
	usleep_range(100, 150); /* wait 100 us */
	reg16_write(client, 0x00C0, 0xc1);
}

static void ov490_otp_id_read(struct i2c_client *client)
{
	struct ov490_priv *priv = to_ov490(client);
	int i;
	int otp_bank0_allzero = 1;
#if 0
	/* read camera id from ov490 OTP memory */
	reg16_write(client, 0xFFFD, 0x80);
	reg16_write(client, 0xFFFE, 0x28);
	usleep_range(100, 150); /* wait 100 us */
	reg16_write(client, 0xE084, 0x40); /* manual mode, bank#0 */
	reg16_write(client, 0xE081, 1); /* start OTP read */

	usleep_range(25000, 26000); /* wait 25 ms */

	for (i = 0; i < 6; i++)
		reg16_read(client, 0xe000 + i + 4, &priv->id[i]);
#else
	/* read camera id from ov10640 OTP memory */
	ov490_ov10640_write(client, 0x349C, 1);
	usleep_range(25000, 25500); /* wait 25 ms */

	for (i = 0; i < 6; i++) {
		/* first 6 bytes are equal on all ov10640 */
		priv->id[i] = ov490_ov10640_read(client, 0x349e + i + 6);
		if (priv->id[i])
			otp_bank0_allzero = 0;
	}

	if (otp_bank0_allzero) {
		ov490_ov10640_write(client, 0x3495, 0x41); /* bank#1 */
		ov490_ov10640_write(client, 0x349C, 1);
		usleep_range(25000, 25500); /* wait 25 ms */

		for (i = 0; i < 6; i++)
			priv->id[i] = ov490_ov10640_read(client, 0x34ae + i);
	}
#endif
}

static int ov490_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int ov490_get_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov490_priv *priv = to_ov490(client);

	if (format->pad)
		return -EINVAL;

	mf->width = priv->rect.width;
	mf->height = priv->rect.height;
	mf->code = OV490_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int ov490_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	mf->code = OV490_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		cfg->try_fmt = *mf;

	return 0;
}

static int ov490_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = OV490_MEDIA_BUS_FMT;

	return 0;
}

static int ov490_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov490_priv *priv = to_ov490(client);

	memcpy(edid->edid, priv->id, 6);

	edid->edid[6] = 0xff;
	edid->edid[7] = client->addr;
	edid->edid[8] = OV490_PID >> 8;
	edid->edid[9] = OV490_PID & 0xff;

	return 0;
}

static int ov490_set_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	struct v4l2_rect *rect = &sel->r;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov490_priv *priv = to_ov490(client);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE ||
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	rect->left = ALIGN(rect->left, 2);
	rect->top = ALIGN(rect->top, 2);
	rect->width = ALIGN(rect->width, 2);
	rect->height = ALIGN(rect->height, 2);

	if ((rect->left + rect->width > priv->max_width) ||
	    (rect->top + rect->height > priv->max_height))
		*rect = priv->rect;

	priv->rect.left = rect->left;
	priv->rect.top = rect->top;
	priv->rect.width = rect->width;
	priv->rect.height = rect->height;

	return 0;
}

static int ov490_get_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov490_priv *priv = to_ov490(client);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = priv->max_width;
		sel->r.height = priv->max_height;
		return 0;
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = priv->max_width;
		sel->r.height = priv->max_height;
		return 0;
	case V4L2_SEL_TGT_CROP:
		sel->r = priv->rect;
		return 0;
	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov490_g_register(struct v4l2_subdev *sd,
			    struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 val = 0;

	ret = reg16_read(client, (u16)reg->reg, &val);
	if (ret < 0)
		return ret;

	reg->val = val;
	reg->size = sizeof(u16);

	return 0;
}

static int ov490_s_register(struct v4l2_subdev *sd,
			    const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = reg16_write(client, (u16)reg->reg, (u8)reg->val);
	if ((u8)reg->reg == 0xFFFD)
		usleep_range(100, 150); /* wait 100 us */
	if ((u8)reg->reg == 0xFFFE)
		usleep_range(100, 150); /* wait 100 us */
	return ret;
}
#endif

static struct v4l2_subdev_core_ops ov490_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ov490_g_register,
	.s_register = ov490_s_register,
#endif
};

static int ov490_s_gamma(int a, int ref)
{
	if ((a + ref) > 0xff)
		return 0xff;

	if ((a + ref) < 0)
		return 0;

	return a + ref;
}

static int ov490_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov490_priv *priv = to_ov490(client);
	int ret = -EINVAL;

	if (!priv->init_complete)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		/* SDE (rough) brightness */
		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x00);
		ret |= reg16_write(client, 0x5001, ctrl->val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xf1);
		break;
	case V4L2_CID_CONTRAST:
		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, ctrl->val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xfd);
		break;
	case V4L2_CID_SATURATION:
		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, ctrl->val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xf3);
		break;
	case V4L2_CID_HUE:
		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, ctrl->val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xf5);
		break;
	case V4L2_CID_GAMMA:
		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, ov490_s_gamma(ctrl->val, 0x12));
		ret |= reg16_write(client, 0x5001, ov490_s_gamma(ctrl->val, 0x20));
		ret |= reg16_write(client, 0x5002, ov490_s_gamma(ctrl->val, 0x3b));
		ret |= reg16_write(client, 0x5003, ov490_s_gamma(ctrl->val, 0x5d));
		ret |= reg16_write(client, 0x5004, ov490_s_gamma(ctrl->val, 0x6a));
		ret |= reg16_write(client, 0x5005, ov490_s_gamma(ctrl->val, 0x76));
		ret |= reg16_write(client, 0x5006, ov490_s_gamma(ctrl->val, 0x81));
		ret |= reg16_write(client, 0x5007, ov490_s_gamma(ctrl->val, 0x8b));
		ret |= reg16_write(client, 0x5008, ov490_s_gamma(ctrl->val, 0x96));
		ret |= reg16_write(client, 0x5009, ov490_s_gamma(ctrl->val, 0x9e));
		ret |= reg16_write(client, 0x500a, ov490_s_gamma(ctrl->val, 0xae));
		ret |= reg16_write(client, 0x500b, ov490_s_gamma(ctrl->val, 0xbc));
		ret |= reg16_write(client, 0x500c, ov490_s_gamma(ctrl->val, 0xcf));
		ret |= reg16_write(client, 0x500d, ov490_s_gamma(ctrl->val, 0xde));
		ret |= reg16_write(client, 0x500e, ov490_s_gamma(ctrl->val, 0xec));
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xf9);
		break;
	case V4L2_CID_SHARPNESS:
		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, ctrl->val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xfb);
		break;
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		if (ctrl->id == V4L2_CID_AUTOGAIN)
			priv->autogain = ctrl->val;
		if (ctrl->id == V4L2_CID_GAIN)
			priv->gain = ctrl->val;
		if (ctrl->id == V4L2_CID_EXPOSURE)
			priv->exposure = ctrl->val;

		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, !priv->autogain);
		ret |= reg16_write(client, 0x5001, priv->exposure >> 8);
		ret |= reg16_write(client, 0x5002, priv->exposure & 0xff);
		ret |= reg16_write(client, 0x5003, priv->exposure >> 8);
		ret |= reg16_write(client, 0x5004, priv->exposure & 0xff);
		ret |= reg16_write(client, 0x5005, priv->exposure >> 8);
		ret |= reg16_write(client, 0x5006, priv->exposure & 0xff);
		ret |= reg16_write(client, 0x5007, priv->gain >> 8);
		ret |= reg16_write(client, 0x5008, priv->gain & 0xff);
		ret |= reg16_write(client, 0x5009, priv->gain >> 8);
		ret |= reg16_write(client, 0x500a, priv->gain & 0xff);
		ret |= reg16_write(client, 0x500b, priv->gain >> 8);
		ret |= reg16_write(client, 0x500c, priv->gain & 0xff);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xea);
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
	case V4L2_CID_RED_BALANCE:
	case V4L2_CID_BLUE_BALANCE:
		if (ctrl->id == V4L2_CID_AUTO_WHITE_BALANCE)
			priv->awb = ctrl->val;
		if (ctrl->id == V4L2_CID_RED_BALANCE) {
			priv->red = ctrl->val;
			priv->red <<= 8;
			priv->green_r = priv->red / 2;
		}
		if (ctrl->id == V4L2_CID_BLUE_BALANCE) {
			priv->blue = ctrl->val;
			priv->blue <<= 8;
			priv->green_b = priv->blue / 2;
		}

		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, !priv->awb);
		ret |= reg16_write(client, 0x5001, priv->red >> 8);
		ret |= reg16_write(client, 0x5002, priv->red & 0xff);
		ret |= reg16_write(client, 0x5003, priv->green_r >> 8);
		ret |= reg16_write(client, 0x5004, priv->green_r & 0xff);
		ret |= reg16_write(client, 0x5005, priv->green_b >> 8);
		ret |= reg16_write(client, 0x5006, priv->green_b & 0xff);
		ret |= reg16_write(client, 0x5007, priv->blue >> 8);
		ret |= reg16_write(client, 0x5008, priv->blue & 0xff);
		ret |= reg16_write(client, 0x5009, priv->red >> 8);
		ret |= reg16_write(client, 0x500a, priv->red & 0xff);
		ret |= reg16_write(client, 0x500b, priv->green_r >> 8);
		ret |= reg16_write(client, 0x500c, priv->green_r & 0xff);
		ret |= reg16_write(client, 0x500d, priv->green_b >> 8);
		ret |= reg16_write(client, 0x500e, priv->green_b & 0xff);
		ret |= reg16_write(client, 0x500f, priv->blue >> 8);
		ret |= reg16_write(client, 0x5010, priv->blue & 0xff);
		ret |= reg16_write(client, 0x5011, priv->red >> 8);
		ret |= reg16_write(client, 0x5012, priv->red & 0xff);
		ret |= reg16_write(client, 0x5013, priv->green_r >> 8);
		ret |= reg16_write(client, 0x5014, priv->green_r & 0xff);
		ret |= reg16_write(client, 0x5015, priv->green_b >> 8);
		ret |= reg16_write(client, 0x5016, priv->green_b & 0xff);
		ret |= reg16_write(client, 0x5017, priv->blue >> 8);
		ret |= reg16_write(client, 0x5018, priv->blue & 0xff);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xeb);
		break;
	case V4L2_CID_HFLIP:
#if 1
		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, ctrl->val);
		ret |= reg16_write(client, 0x5001, 0x00);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xdc);
#else
		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x01); // read 0x3128
		ret |= reg16_write(client, 0x5001, 0x31);
		ret |= reg16_write(client, 0x5002, 0x28);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_read(client, 0x5000, &val);
		val &= ~(0x1 << 0);
		val |= (ctrl->val << 0);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x00); // write 0x3128
		ret |= reg16_write(client, 0x5001, 0x31);
		ret |= reg16_write(client, 0x5002, 0x28);
		ret |= reg16_write(client, 0x5003, val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);

		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x01); // read 0x3291
		ret |= reg16_write(client, 0x5001, 0x32);
		ret |= reg16_write(client, 0x5002, 0x91);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_read(client, 0x5000, &val);
		val &= ~(0x1 << 1);
		val |= (ctrl->val << 1);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x00); // write 0x3291
		ret |= reg16_write(client, 0x5001, 0x32);
		ret |= reg16_write(client, 0x5002, 0x91);
		ret |= reg16_write(client, 0x5003, val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);

		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x01); // read 0x3090
		ret |= reg16_write(client, 0x5001, 0x30);
		ret |= reg16_write(client, 0x5002, 0x90);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_read(client, 0x5000, &val);
		val &= ~(0x1 << 2);
		val |= (ctrl->val << 2);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x00); // write 0x3090
		ret |= reg16_write(client, 0x5001, 0x30);
		ret |= reg16_write(client, 0x5002, 0x90);
		ret |= reg16_write(client, 0x5003, val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);
#endif
		break;
	case V4L2_CID_VFLIP:
#if 1
		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, ctrl->val);
		ret |= reg16_write(client, 0x5001, 0x01);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xdc);
#else
		ret = reg16_write(client, 0xFFFD, 0x80);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x01); // read 0x3128
		ret |= reg16_write(client, 0x5001, 0x31);
		ret |= reg16_write(client, 0x5002, 0x28);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_read(client, 0x5000, &val);
		val &= ~(0x1 << 1);
		val |= (ctrl->val << 1);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x00); // write 0x3128
		ret |= reg16_write(client, 0x5001, 0x31);
		ret |= reg16_write(client, 0x5002, 0x28);
		ret |= reg16_write(client, 0x5003, val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);

		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x01); // read 0x3291
		ret |= reg16_write(client, 0x5001, 0x32);
		ret |= reg16_write(client, 0x5002, 0x91);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_read(client, 0x5000, &val);
		val &= ~(0x1 << 2);
		val |= (ctrl->val << 2);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x00); // write 0x3291
		ret |= reg16_write(client, 0x5001, 0x32);
		ret |= reg16_write(client, 0x5002, 0x91);
		ret |= reg16_write(client, 0x5003, val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);

		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x01); // read 0x3090
		ret |= reg16_write(client, 0x5001, 0x30);
		ret |= reg16_write(client, 0x5002, 0x90);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_read(client, 0x5000, &val);
		val &= ~(0x1 << 3);
		val |= (ctrl->val << 3);
		ret |= reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x5000, 0x00); // write 0x3090
		ret |= reg16_write(client, 0x5001, 0x30);
		ret |= reg16_write(client, 0x5002, 0x90);
		ret |= reg16_write(client, 0x5003, val);
		ret |= reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x00C0, 0xc1);
#endif
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ret = 0;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ov490_ctrl_ops = {
	.s_ctrl = ov490_s_ctrl,
};

static struct v4l2_subdev_video_ops ov490_video_ops = {
	.s_stream	= ov490_s_stream,
};

static const struct v4l2_subdev_pad_ops ov490_subdev_pad_ops = {
	.get_edid	= ov490_get_edid,
	.enum_mbus_code	= ov490_enum_mbus_code,
	.get_selection	= ov490_get_selection,
	.set_selection	= ov490_set_selection,
	.get_fmt	= ov490_get_fmt,
	.set_fmt	= ov490_set_fmt,
};

static struct v4l2_subdev_ops ov490_subdev_ops = {
	.core	= &ov490_core_ops,
	.video	= &ov490_video_ops,
	.pad	= &ov490_subdev_pad_ops,
};

static ssize_t ov490_otp_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov490_priv *priv = to_ov490(client);

	return snprintf(buf, 32, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
}

static DEVICE_ATTR(otp_id_ov490, S_IRUGO, ov490_otp_id_show, NULL);

static int ov490_initialize(struct i2c_client *client)
{
	struct ov490_priv *priv = to_ov490(client);
	u8 val = 0;
	u16 pid = 0;
	int timeout, retry_timeout = 3;

	setup_i2c_translator(client, priv->ser_addr, OV490_I2C_ADDR);

	reg16_write(client, 0xFFFD, 0x80);
	reg16_write(client, 0xFFFE, 0x80);
	usleep_range(100, 150); /* wait 100 us */
	reg16_read(client, OV490_PID_REGA, &val);
	pid = val;
	reg16_read(client, OV490_PID_REGB, &val);
	pid = (pid << 8) | val;

	if (pid != OV490_PID) {
		dev_dbg(&client->dev, "Product ID error %x\n", pid);
		return -ENODEV;
	}

	if (unlikely(conf_link))
		goto out;
again:
	/* Check if firmware booted by reading stream-on status */
	reg16_write(client, 0xFFFD, 0x80);
	reg16_write(client, 0xFFFE, 0x29);
	usleep_range(100, 150); /* wait 100 us */
	for (timeout = 300; timeout > 0; timeout--) {
		reg16_read(client, 0xd000, &val);
		if (val == 0x0c)
			break;
		mdelay(1);
	}

	/* wait firmware apps started by reading OV10640 ID */
	for (;timeout > 0; timeout--) {
		reg16_write(client, 0xFFFD, 0x80);
		reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		reg16_write(client, 0x5000, 0x01);
		reg16_write(client, 0x5001, 0x30);
		reg16_write(client, 0x5002, 0x0a);
		reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		reg16_write(client, 0xC0, 0xc1);
		reg16_write(client, 0xFFFE, 0x19);
		usleep_range(1000, 1500); /* wait 1 ms */
		reg16_read(client, 0x5000, &val);
		if (val == 0xa6)
			break;
		mdelay(1);
	}

	if (!timeout) {
		dev_err(&client->dev, "Timeout firmware boot wait, retrying\n");
		/* reset OV10640 using RESETB pin controlled by OV490 GPIO0 */
		reg16_write(client, 0xFFFD, 0x80);
		reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		reg16_write(client, 0x0050, 0x01);
		reg16_write(client, 0x0054, 0x01);
		reg16_write(client, 0x0058, 0x00);
		mdelay(10);
		reg16_write(client, 0x0058, 0x01);
		/* reset OV490 using RESETB pin controlled by serializer */
		ov490_reset(client);
		if (retry_timeout--)
			goto again;
	}

	if (priv->group) {
		/* switch to group# */
		reg16_write(client, 0xFFFD, 0x80);
		reg16_write(client, 0xFFFE, 0x19);
		usleep_range(100, 150); /* wait 100 us */
		reg16_write(client, 0x5000, priv->group);
		reg16_write(client, 0xFFFE, 0x80);
		usleep_range(100, 150); /* wait 100 us */
		reg16_write(client, 0xc0, 0x3f);

		mdelay(30);
	}

	/* read resolution used by current firmware */
	reg16_write(client, 0xFFFD, 0x80);
	reg16_write(client, 0xFFFE, 0x82);
	usleep_range(100, 150); /* wait 100 us */
	reg16_read(client, OV490_ISP_HSIZE_HIGH, &val);
	priv->max_width = val;
	reg16_read(client, OV490_ISP_HSIZE_LOW, &val);
	priv->max_width = (priv->max_width << 8) | val;
	reg16_read(client, OV490_ISP_VSIZE_HIGH, &val);
	priv->max_height = val;
	reg16_read(client, OV490_ISP_VSIZE_LOW, &val);
	priv->max_height = (priv->max_height << 8) | val;
	/* Program wizard registers */
	ov490_set_regs(client, ov490_regs, ARRAY_SIZE(ov490_regs));
	/* Set DVP bit swap */
	reg16_write(client, 0xFFFD, 0x80);
	reg16_write(client, 0xFFFE, 0x28);
	usleep_range(100, 150); /* wait 100 us */
	reg16_write(client, 0x6009, priv->dvp_order << 4);
	/* Set VSYNC inversion */
	reg16_write(client, 0x6008, priv->vsync ? 0x2 : 0x0);
	/* Read OTP IDs */
	ov490_otp_id_read(client);

out:
	dev_info(&client->dev, "PID %x, res %dx%d, OTP_ID %02x:%02x:%02x:%02x:%02x:%02x\n",
		 pid, priv->max_width, priv->max_height, priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
	return 0;
}

static int ov490_parse_dt(struct device_node *np, struct ov490_priv *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);
	struct fwnode_handle *ep;
	u32 addrs[2], naddrs;

	naddrs = of_property_count_elems_of_size(np, "reg", sizeof(u32));
	if (naddrs != 2) {
		dev_err(&client->dev, "Invalid DT reg property\n");
		return -EINVAL;
	}

	if (of_property_read_u32_array(client->dev.of_node, "reg", addrs, naddrs) < 0) {
		dev_err(&client->dev, "Invalid DT reg property\n");
		return -EINVAL;
	}

	priv->ser_addr = addrs[1];

	if (of_property_read_u32(np, "dvp-order", &priv->dvp_order))
		priv->dvp_order = 0;
	if (of_property_read_u32(np, "reset-gpio", &priv->reset_gpio))
		priv->reset_gpio = 1;
	if (of_property_read_u32(np, "group", &priv->group))
		priv->group = 0;
	if (of_property_read_u32(np, "vsync", &priv->group))
		priv->vsync = 0;

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev), NULL);
	if (!ep) {
		dev_err(&client->dev, "Unable to get endpoint in node %pOF: %ld\n",
				      client->dev.of_node, PTR_ERR(ep));
		return -ENOENT;
	}
	priv->sd.fwnode = ep;

	/* module params override dts */
	if (dvp_order)
		priv->dvp_order = dvp_order;
	if (group)
		priv->group = group;
	if (vsync)
		priv->vsync = vsync;

	return 0;
}

static int ov490_probe(struct i2c_client *client,
		       const struct i2c_device_id *did)
{
	struct ov490_priv *priv;
	struct v4l2_ctrl *ctrl;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->sd, client, &ov490_subdev_ops);
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	priv->exposure = 0x100;
	priv->gain = 0x100;
	priv->autogain = 1;
	priv->red = 0x400;
	priv->blue = 0x400;
	priv->green_r = priv->red / 2;
	priv->green_b = priv->blue / 2;
	priv->awb = 1;
	v4l2_ctrl_handler_init(&priv->hdl, 4);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 7, 1, 2);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_HUE, 0, 23, 1, 12);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_GAMMA, -128, 128, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_SHARPNESS, 0, 10, 1, 3);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_AUTOGAIN, 0, 1, 1, priv->autogain);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_GAIN, 0, 0xffff, 1, priv->gain);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_EXPOSURE, 0, 0xffff, 1, priv->exposure);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, priv->autogain);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_RED_BALANCE, 2, 0xf, 1, priv->red >> 8);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_BLUE_BALANCE, 2, 0xf, 1, priv->blue >> 8);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
	ctrl = v4l2_ctrl_new_std(&priv->hdl, &ov490_ctrl_ops,
			  V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1, 9);
	if (ctrl)
		ctrl->flags &= ~V4L2_CTRL_FLAG_READ_ONLY;
	priv->sd.ctrl_handler = &priv->hdl;

	ret = priv->hdl.error;
	if (ret)
		goto cleanup;

	v4l2_ctrl_handler_setup(&priv->hdl);

	priv->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&priv->sd.entity, 1, &priv->pad);
	if (ret < 0)
		goto cleanup;

	ret = ov490_parse_dt(client->dev.of_node, priv);
	if (ret)
		goto cleanup;

	ret = ov490_initialize(client);
	if (ret < 0)
		goto cleanup;

	priv->rect.left = 0;
	priv->rect.top = 0;
	priv->rect.width = priv->max_width;
	priv->rect.height = priv->max_height;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret)
		goto cleanup;

	if (device_create_file(&client->dev, &dev_attr_otp_id_ov490) != 0) {
		dev_err(&client->dev, "sysfs otp_id entry creation failed\n");
		goto cleanup;
	}

	priv->init_complete = 1;

	return 0;

cleanup:
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->sd);
	return ret;
}

static int ov490_remove(struct i2c_client *client)
{
	struct ov490_priv *priv = i2c_get_clientdata(client);

	device_remove_file(&client->dev, &dev_attr_otp_id_ov490);
	v4l2_async_unregister_subdev(&priv->sd);
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->sd);

	return 0;
}

static const struct i2c_device_id ov490_id[] = {
	{ "ov490", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov490_id);

static const struct of_device_id ov490_of_ids[] = {
	{ .compatible = "ovti,ov490", },
	{ }
};
MODULE_DEVICE_TABLE(of, ov490_of_ids);

static struct i2c_driver ov490_i2c_driver = {
	.driver	= {
		.name		= "ov490",
		.of_match_table	= ov490_of_ids,
	},
	.probe		= ov490_probe,
	.remove		= ov490_remove,
	.id_table	= ov490_id,
};

module_i2c_driver(ov490_i2c_driver);

MODULE_DESCRIPTION("Camera glue driver for OV490-10640");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
