/*
 * ON Semiconductor AP0101-AR014X sensor camera driver
 *
 * Copyright (C) 2018-2020 Cogent Embedded, Inc.
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
#include "ap0101.h"

static const int ap0101_i2c_addr[] = {0x5d, 0x48};

#define AP0101_PID_REG		0x0000
#define AP0101_REV_REG		0x0058
#define AP0100_PID		0x0062
#define AP0101_PID		0x0160
#define AP0102_PID		0x0064

#define AP0101_MEDIA_BUS_FMT	MEDIA_BUS_FMT_UYVY8_2X8

struct ap0101_priv {
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
	/* serializers */
	int				ser_addr;
	int				hts;
	int				vts;
	int				frame_preamble;
};

static inline struct ap0101_priv *to_ap0101(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ap0101_priv, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ap0101_priv, hdl)->sd;
}

static int ap0101_set_regs(struct i2c_client *client,
			   const struct ap0101_reg *regs, int nr_regs)
{
	int i;

	for (i = 0; i < nr_regs; i++) {
		if (regs[i].reg == AP0101_DELAY) {
			mdelay(regs[i].val);
			continue;
		}

		reg16_write16(client, regs[i].reg, regs[i].val);
	}

	return 0;
}

static u16 ap0101_ar014x_read(struct i2c_client *client, u16 addr)
{
	u16 reg_val = 0;

	reg16_write16(client, 0x0040, 0x8d00);
	usleep_range(1000, 1500); /* wait 1000 us */
	reg16_write16(client, 0xfc00, addr);
	reg16_write16(client, 0xfc02, 0x0200); /* 2 bytes */
	reg16_write16(client, 0x0040, 0x8d05);
	usleep_range(1000, 1500); /* wait 1000 us */
	reg16_write16(client, 0x0040, 0x8d08);
	usleep_range(1000, 1500); /* wait 1000 us */
	reg16_read16(client, 0xfc00, &reg_val);
	reg16_write16(client, 0x0040, 0x8d02);
	usleep_range(1000, 1500); /* wait 1000 us */

	return reg_val;
}

static void ap0101_ar014x_write(struct i2c_client *client, u16 addr, u16 val)
{
	reg16_write16(client, 0x0040, 0x8d00);
	usleep_range(1000, 1500); /* wait 1000 us */
	reg16_write16(client, 0xfc00, addr);
	reg16_write16(client, 0xfc02, 0x0200 | (val >> 8)); /* 2 bytes */
	reg16_write16(client, 0xfc04, (val & 0xff) << 8);
	reg16_write16(client, 0x0040, 0x8d06);
	usleep_range(1000, 1500); /* wait 1000 us */
	reg16_write16(client, 0x0040, 0x8d08);
	usleep_range(1000, 1500); /* wait 1000 us */
	reg16_write16(client, 0x0040, 0x8d02);
	usleep_range(1000, 1500); /* wait 1000 us */
}

static void ap0101_otp_id_read(struct i2c_client *client)
{
	struct ap0101_priv *priv = to_ap0101(client);
	int i;

	/* read camera id from ar014x OTP memory */
	ap0101_ar014x_write(client, 0x3054, 0x400);
	ap0101_ar014x_write(client, 0x304a, 0x110);
	usleep_range(25000, 25500); /* wait 25 ms */

	for (i = 0; i < 6; i += 2) {
		/* first 4 bytes are equal on all ar014x */
		priv->id[i]     = (ap0101_ar014x_read(client, 0x3800 + i + 4) >> 8)   ^ (ap0101_ar014x_read(client, 0x3800 + i + 16) >> 8);
		priv->id[i + 1] = (ap0101_ar014x_read(client, 0x3800 + i + 4) & 0xff) ^ (ap0101_ar014x_read(client, 0x3800 + i + 16) & 0xff);
	}
}

static int ap0101_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int ap0101_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ap0101_priv *priv = to_ap0101(client);

	if (format->pad)
		return -EINVAL;

	mf->width = priv->rect.width;
	mf->height = priv->rect.height;
	mf->code = AP0101_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int ap0101_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	mf->code = AP0101_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		cfg->try_fmt = *mf;

	return 0;
}

static int ap0101_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = AP0101_MEDIA_BUS_FMT;

	return 0;
}

static int ap0101_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ap0101_priv *priv = to_ap0101(client);

	ap0101_otp_id_read(client);

	memcpy(edid->edid, priv->id, 6);

	edid->edid[6] = 0xff;
	edid->edid[7] = client->addr;
	edid->edid[8] = AP0101_PID >> 8;
	edid->edid[9] = AP0101_PID & 0xff;

	return 0;
}

static int ap0101_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct v4l2_rect *rect = &sel->r;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ap0101_priv *priv = to_ap0101(client);

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

static int ap0101_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ap0101_priv *priv = to_ap0101(client);

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
static int ap0101_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	__be64 be_val;

	if (!reg->size)
		reg->size = sizeof(u16);
	if (reg->size > sizeof(reg->val))
		reg->size = sizeof(reg->val);

	ret = reg16_read_n(client, (u16)reg->reg, (u8*)&be_val, reg->size);
	be_val = be_val << ((sizeof(be_val) - reg->size) * 8);
	reg->val = be64_to_cpu(be_val);

	return ret;
}

static int ap0101_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u32 size = reg->size;
	__be64 be_val;

	if (!size)
		size = sizeof(u16);
	if (size > sizeof(reg->val))
		size = sizeof(reg->val);

	be_val = cpu_to_be64(reg->val);
	be_val = be_val >> ((sizeof(be_val) - size) * 8);
	return reg16_write_n(client, (u16)reg->reg, (u8*)&be_val, size);
}
#endif

static struct v4l2_subdev_core_ops ap0101_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ap0101_g_register,
	.s_register = ap0101_s_register,
#endif
};

static int ap0101_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ap0101_priv *priv = to_ap0101(client);
	int ret = -EINVAL;
	u16 val = 0;

	if (!priv->init_complete)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_GAMMA:
	case V4L2_CID_SHARPNESS:
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		break;
	case V4L2_CID_HFLIP:
		reg16_read16(client, 0xc846, &val);
		if (ctrl->val)
			val |= 0x01;
		else
			val &= ~0x01;
		reg16_write16(client, 0xc846, val);
		reg16_write16(client, 0xfc00, 0x2800);
		ret = reg16_write16(client, 0x0040, 0x8100);
		break;
	case V4L2_CID_VFLIP:
		reg16_read16(client, 0xc846, &val);
		if (ctrl->val)
			val |= 0x02;
		else
			val &= ~0x02;
		reg16_write16(client, 0xc846, val);
		reg16_write16(client, 0xfc00, 0x2800);
		ret = reg16_write16(client, 0x0040, 0x8100);
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ret = 0;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ap0101_ctrl_ops = {
	.s_ctrl = ap0101_s_ctrl,
};

static struct v4l2_subdev_video_ops ap0101_video_ops = {
	.s_stream	= ap0101_s_stream,
};

static const struct v4l2_subdev_pad_ops ap0101_subdev_pad_ops = {
	.get_edid	= ap0101_get_edid,
	.enum_mbus_code	= ap0101_enum_mbus_code,
	.get_selection	= ap0101_get_selection,
	.set_selection	= ap0101_set_selection,
	.get_fmt	= ap0101_get_fmt,
	.set_fmt	= ap0101_set_fmt,
};

static struct v4l2_subdev_ops ap0101_subdev_ops = {
	.core	= &ap0101_core_ops,
	.video	= &ap0101_video_ops,
	.pad	= &ap0101_subdev_pad_ops,
};

static ssize_t ap0101_otp_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ap0101_priv *priv = to_ap0101(client);

	ap0101_otp_id_read(client);

	return snprintf(buf, 32, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
}

static DEVICE_ATTR(otp_id_ap0101, S_IRUGO, ap0101_otp_id_show, NULL);

static int ap0101_initialize(struct i2c_client *client)
{
	struct ap0101_priv *priv = to_ap0101(client);
	u16 pid = 0, rev = 0, val = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(ap0101_i2c_addr); i++) {
		setup_i2c_translator(client, priv->ser_addr, ap0101_i2c_addr[i]);

		/* check model ID */
		reg16_read16(client, AP0101_PID_REG, &pid);
		if (pid == AP0101_PID)
			break;
	}

	if (pid != AP0101_PID) {
		dev_dbg(&client->dev, "Product ID error %x\n", pid);
		return -ENODEV;
	}

	reg16_read16(client, AP0101_REV_REG, &rev);
#if 1
	/* read resolution used by current firmware */
	reg16_read16(client, 0xca90, &val);
	priv->max_width = val;
	reg16_read16(client, 0xca92, &val);
	priv->max_height = val;
#else
	priv->max_width = AP0101_MAX_WIDTH;
	priv->max_height = AP0101_MAX_HEIGHT;
#endif
	/* Program wizard registers */
	switch (pid) {
	case AP0100_PID:
	case AP0101_PID:
		ap0101_set_regs(client, ap0101_regs, ARRAY_SIZE(ap0101_regs));
		break;
	case AP0102_PID:
		ap0101_set_regs(client, ap0102_regs, ARRAY_SIZE(ap0102_regs));
		break;
	}
	/* Read OTP IDs */
	ap0101_otp_id_read(client);

	switch (get_des_id(client)) {
	case MAX9286_ID:
	case MAX9288_ID:
	case MAX9296A_ID:
	case MAX96712_ID:
		/* setup serializer HS generator */
		priv->frame_preamble = 5;
		priv->hts = 1280 * 2 + 548;
		priv->vts = 960;
		reg8_write_addr(client, priv->ser_addr, 0x4e, priv->frame_preamble >> 16);	/* HS delay */
		reg8_write_addr(client, priv->ser_addr, 0x4f, (priv->frame_preamble >> 8) & 0xff);
		reg8_write_addr(client, priv->ser_addr, 0x50, priv->frame_preamble & 0xff);
		reg8_write_addr(client, priv->ser_addr, 0x54, (priv->max_width * 2) >> 8);	/* HS high period */
		reg8_write_addr(client, priv->ser_addr, 0x55, (priv->max_width * 2) & 0xff);
		reg8_write_addr(client, priv->ser_addr, 0x56, (priv->hts - priv->max_width * 2) >> 8); /* HS low period */
		reg8_write_addr(client, priv->ser_addr, 0x57, (priv->hts - priv->max_width * 2) & 0xff);
		reg8_write_addr(client, priv->ser_addr, 0x58, priv->vts >> 8);			/* HS count */
		reg8_write_addr(client, priv->ser_addr, 0x59, priv->vts & 0xff);
		break;
	}

	dev_info(&client->dev, "PID %x (%x), res %dx%d, OTP_ID %02x:%02x:%02x:%02x:%02x:%02x\n",
		 pid, rev, priv->max_width, priv->max_height, priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
	return 0;
}

static int ap0101_parse_dt(struct device_node *np, struct ap0101_priv *priv)
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

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev), NULL);
	if (!ep) {
		dev_err(&client->dev, "Unable to get endpoint in node %pOF: %ld\n",
				      client->dev.of_node, PTR_ERR(ep));
		return -ENOENT;
	}
	priv->sd.fwnode = ep;

	return 0;
}

static int ap0101_probe(struct i2c_client *client,
		        const struct i2c_device_id *did)
{
	struct ap0101_priv *priv;
	struct v4l2_ctrl *ctrl;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->sd, client, &ap0101_subdev_ops);
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	priv->exposure = 0x100;
	priv->gain = 0x100;
	priv->autogain = 1;
	v4l2_ctrl_handler_init(&priv->hdl, 4);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 7, 1, 2);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_HUE, 0, 23, 1, 12);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_GAMMA, -128, 128, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_SHARPNESS, 0, 10, 1, 3);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_AUTOGAIN, 0, 1, 1, priv->autogain);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_GAIN, 0, 0xffff, 1, priv->gain);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_EXPOSURE, 0, 0xffff, 1, priv->exposure);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 1);
	ctrl = v4l2_ctrl_new_std(&priv->hdl, &ap0101_ctrl_ops,
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

	ret = ap0101_parse_dt(client->dev.of_node, priv);
	if (ret)
		goto cleanup;

	ret = ap0101_initialize(client);
	if (ret < 0)
		goto cleanup;

	priv->rect.left = 0;
	priv->rect.top = 0;
	priv->rect.width = priv->max_width;
	priv->rect.height = priv->max_height;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret)
		goto cleanup;

	if (device_create_file(&client->dev, &dev_attr_otp_id_ap0101) != 0) {
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

static int ap0101_remove(struct i2c_client *client)
{
	struct ap0101_priv *priv = i2c_get_clientdata(client);

	device_remove_file(&client->dev, &dev_attr_otp_id_ap0101);
	v4l2_async_unregister_subdev(&priv->sd);
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->sd);

	return 0;
}

static const struct i2c_device_id ap0101_id[] = {
	{ "ap0101", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ap0101_id);

static const struct of_device_id ap0101_of_ids[] = {
	{ .compatible = "onnn,ap0101", },
	{ }
};
MODULE_DEVICE_TABLE(of, ap0101_of_ids);

static struct i2c_driver ap0101_i2c_driver = {
	.driver	= {
		.name		= "ap0101",
		.of_match_table	= ap0101_of_ids,
	},
	.probe		= ap0101_probe,
	.remove		= ap0101_remove,
	.id_table	= ap0101_id,
};

module_i2c_driver(ap0101_i2c_driver);

MODULE_DESCRIPTION("Camera glue driver for AP0101");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
