/*
 * OmniVision ov2311 sensor camera driver
 *
 * Copyright (C) 2015-2020 Cogent Embedded, Inc.
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
#include "ov2311.h"

#define OV2311_I2C_ADDR			0x60

#define OV2311_PIDA_REG			0x300a
#define OV2311_PIDB_REG			0x300b
#define OV2311_REV_REG			0x300c
#define OV2311_PID			0x2311

#define OV2311_MEDIA_BUS_FMT		MEDIA_BUS_FMT_Y8_1X8

struct ov2311_priv {
	struct v4l2_subdev		sd;
	struct v4l2_ctrl_handler	hdl;
	struct media_pad		pad;
	struct v4l2_rect		rect;
	int				subsampling;
	int				fps_denominator;
	int				init_complete;
	u8				id[6];
	int				dvp_order;
	/* serializers */
	int				ser_addr;
};

static inline struct ov2311_priv *to_ov2311(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov2311_priv, sd);
}

static inline struct v4l2_subdev *ov2311_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov2311_priv, hdl)->sd;
}

static int ov2311_set_regs(struct i2c_client *client,
			   const struct ov2311_reg *regs, int nr_regs)
{
	int i;

	for (i = 0; i < nr_regs; i++) {
		if (regs[i].reg == OV2311_DELAY) {
			mdelay(regs[i].val);
			continue;
		}

		if (reg16_write(client, regs[i].reg, regs[i].val)) {
			usleep_range(100, 150); /* wait 100ns */
			reg16_write(client, regs[i].reg, regs[i].val);
		}
	}

	return 0;
}

static void ov2311_otp_id_read(struct i2c_client *client)
{
	struct ov2311_priv *priv = to_ov2311(client);
	int i;

	reg16_write(client, 0x3d81, 1);
	usleep_range(25000, 25500); /* wait 25 ms */

	for (i = 0; i < 6; i++) {
		/* first 6 bytes are equal on all ov2311 */
		reg16_read(client, 0x7000 + i + 6, &priv->id[i]);
	}
}

static int ov2311_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int ov2311_set_window(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2311_priv *priv = to_ov2311(client);

	dev_dbg(&client->dev, "L=%d T=%d %dx%d\n", priv->rect.left, priv->rect.top, priv->rect.width, priv->rect.height);
#if 0
	/* setup resolution */
	reg16_write(client, 0x3808, priv->rect.width >> 8);
	reg16_write(client, 0x3809, priv->rect.width & 0xff);
	reg16_write(client, 0x380a, priv->rect.height >> 8);
	reg16_write(client, 0x380b, priv->rect.height & 0xff);

	/* horiz isp windowstart */
	reg16_write(client, 0x3810, priv->rect.left >> 8);
	reg16_write(client, 0x3811, priv->rect.left & 0xff);
	reg16_write(client, 0x3812, priv->rect.top >> 8);
	reg16_write(client, 0x3813, priv->rect.top & 0xff);
#endif
	return 0;
};

static int ov2311_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2311_priv *priv = to_ov2311(client);

	if (format->pad)
		return -EINVAL;

	mf->width = priv->rect.width;
	mf->height = priv->rect.height;
	mf->code = OV2311_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int ov2311_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	mf->code = OV2311_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		cfg->try_fmt = *mf;

	return 0;
}

static int ov2311_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = OV2311_MEDIA_BUS_FMT;

	return 0;
}

static int ov2311_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2311_priv *priv = to_ov2311(client);

	memcpy(edid->edid, priv->id, 6);

	edid->edid[6] = 0xff;
	edid->edid[7] = client->addr;
	edid->edid[8] = OV2311_PID >> 8;
	edid->edid[9] = OV2311_PID & 0xff;

	return 0;
}

static int ov2311_set_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	struct v4l2_rect *rect = &sel->r;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2311_priv *priv = to_ov2311(client);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE ||
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	rect->left = ALIGN(rect->left, 2);
	rect->top = ALIGN(rect->top, 2);
	rect->width = ALIGN(rect->width, 2);
	rect->height = ALIGN(rect->height, 2);

	if ((rect->left + rect->width > OV2311_MAX_WIDTH) ||
	    (rect->top + rect->height > OV2311_MAX_HEIGHT))
		*rect = priv->rect;

	priv->rect.left = rect->left;
	priv->rect.top = rect->top;
	priv->rect.width = rect->width;
	priv->rect.height = rect->height;

	ov2311_set_window(sd);

	return 0;
}

static int ov2311_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2311_priv *priv = to_ov2311(client);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = OV2311_MAX_WIDTH;
		sel->r.height = OV2311_MAX_HEIGHT;
		return 0;
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = OV2311_MAX_WIDTH;
		sel->r.height = OV2311_MAX_HEIGHT;
		return 0;
	case V4L2_SEL_TGT_CROP:
		sel->r = priv->rect;
		return 0;
	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov2311_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	__be64 be_val;

	if (!reg->size)
		reg->size = sizeof(u8);
	if (reg->size > sizeof(reg->val))
		reg->size = sizeof(reg->val);

	ret = reg16_read_n(client, (u16)reg->reg, (u8*)&be_val, reg->size);
	be_val = be_val << ((sizeof(be_val) - reg->size) * 8);
	reg->val = be64_to_cpu(be_val);

	return ret;
}

static int ov2311_s_register(struct v4l2_subdev *sd,
			      const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u32 size = reg->size;
	int ret;
	__be64 be_val;

	if (!size)
		size = sizeof(u8);
	if (size > sizeof(reg->val))
		size = sizeof(reg->val);

	be_val = cpu_to_be64(reg->val);
	be_val = be_val >> ((sizeof(be_val) - size) * 8);
	ret = reg16_write_n(client, (u16)reg->reg, (u8*)&be_val, size);

	return ret;
}
#endif

static struct v4l2_subdev_core_ops ov2311_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ov2311_g_register,
	.s_register = ov2311_s_register,
#endif
};

static int ov2311_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ov2311_to_sd(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2311_priv *priv = to_ov2311(client);
	int ret = 0;
	u8 val = 0;

	if (!priv->init_complete)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_GAMMA:
		break;
	case V4L2_CID_GAIN:
		reg16_write(client, 0x350A, ctrl->val / 0x3ff); // COARSE: 4.10 format
		reg16_write(client, 0x350B, (ctrl->val % 0x3ff) >> 2); // FINE: 4.10 format
		reg16_write(client, 0x350C, (ctrl->val % 0x3ff) << 6); // FINE: 4.10 format
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		reg16_write(client, 0x3508, ctrl->val / 0xf); // COARSE: 5.4 format
		reg16_write(client, 0x3509, (ctrl->val % 0xf) << 4); // FINE: 5.4 format
		break;
	case V4L2_CID_EXPOSURE:
		reg16_write(client, 0x3501, ctrl->val >> 8);
		reg16_write(client, 0x3502, ctrl->val & 0xff);
		break;
	case V4L2_CID_HFLIP:
		reg16_read(client, 0x3821, &val);
		val &= ~0x04;
		val |= (ctrl->val ? 0x04 : 0);
		reg16_write(client, 0x3821, val);
		break;
	case V4L2_CID_VFLIP:
		reg16_read(client, 0x3820, &val);
		val &= ~0x44;
		val |= (ctrl->val ? 0x44 : 0);
		reg16_write(client, 0x3820, val);
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ret = 0;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ov2311_ctrl_ops = {
	.s_ctrl = ov2311_s_ctrl,
};

static struct v4l2_subdev_video_ops ov2311_video_ops = {
	.s_stream	= ov2311_s_stream,
};

static const struct v4l2_subdev_pad_ops ov2311_subdev_pad_ops = {
	.get_edid	= ov2311_get_edid,
	.enum_mbus_code	= ov2311_enum_mbus_code,
	.get_selection	= ov2311_get_selection,
	.set_selection	= ov2311_set_selection,
	.get_fmt	= ov2311_get_fmt,
	.set_fmt	= ov2311_set_fmt,
};

static struct v4l2_subdev_ops ov2311_subdev_ops = {
	.core	= &ov2311_core_ops,
	.video	= &ov2311_video_ops,
	.pad	= &ov2311_subdev_pad_ops,
};

static ssize_t ov2311_otp_id_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2311_priv *priv = to_ov2311(client);

	return snprintf(buf, 32, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
}

static DEVICE_ATTR(otp_id_ov2311, S_IRUGO, ov2311_otp_id_show, NULL);

static int ov2311_initialize(struct i2c_client *client)
{
	struct ov2311_priv *priv = to_ov2311(client);
	u16 pid;
	u8 val = 0, rev = 0;

	setup_i2c_translator(client, priv->ser_addr, OV2311_I2C_ADDR);

	reg16_read(client, OV2311_PIDA_REG, &val);
	pid = val;
	reg16_read(client, OV2311_PIDB_REG, &val);
	pid = (pid << 8) | val;

	if (pid != OV2311_PID) {
		dev_dbg(&client->dev, "Product ID error %x\n", pid);
		return -ENODEV;
	}

	switch (get_des_id(client)) {
	case UB960_ID:
		reg8_write_addr(client, priv->ser_addr, 0x02, 0x13); /* MIPI 2-lanes */
		break;
	case MAX9296A_ID:
	case MAX96712_ID:
		reg16_write_addr(client, priv->ser_addr, MAX9295_MIPI_RX1, 0x11); /* MIPI 2-lanes */
		break;
	}

	/* check revision */
	reg16_read(client, OV2311_REV_REG, &rev);
	/* Program wizard registers */
	ov2311_set_regs(client, ov2311_regs_r1c, ARRAY_SIZE(ov2311_regs_r1c));
	/* Read OTP IDs */
	ov2311_otp_id_read(client);

	dev_info(&client->dev, "PID %x (rev %x), res %dx%d, OTP_ID %02x:%02x:%02x:%02x:%02x:%02x\n",
		 pid, rev, OV2311_MAX_WIDTH, OV2311_MAX_HEIGHT, priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
	return 0;
}

static int ov2311_parse_dt(struct device_node *np, struct ov2311_priv *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);
	u32 addrs[2], naddrs;
	struct fwnode_handle *ep;

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

static int ov2311_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct ov2311_priv *priv;
	struct v4l2_ctrl *ctrl;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->sd, client, &ov2311_subdev_ops);
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	priv->rect.left = 0;
	priv->rect.top = 0;
	priv->rect.width = OV2311_MAX_WIDTH;
	priv->rect.height = OV2311_MAX_HEIGHT;
	priv->fps_denominator = 30;

	v4l2_ctrl_handler_init(&priv->hdl, 4);
	v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 0xff, 1, 0x30);
	v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 4, 1, 2);
	v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 0xff, 1, 0xff);
	v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
			  V4L2_CID_HUE, 0, 255, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
			  V4L2_CID_GAMMA, 0, 0xffff, 1, 0x233);
	v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
			  V4L2_CID_GAIN, 0, 0x3ff*4, 1, 0x3ff);
	v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, 0, 0xf*5, 1, 0xf);
	v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
			  V4L2_CID_EXPOSURE, 0, 0x580, 1, 0x57c);
	v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
	ctrl = v4l2_ctrl_new_std(&priv->hdl, &ov2311_ctrl_ops,
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

	ret = ov2311_parse_dt(client->dev.of_node, priv);
	if (ret)
		goto cleanup;

	ret = ov2311_initialize(client);
	if (ret < 0)
		goto cleanup;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret)
		goto cleanup;

	if (device_create_file(&client->dev, &dev_attr_otp_id_ov2311) != 0) {
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

static int ov2311_remove(struct i2c_client *client)
{
	struct ov2311_priv *priv = i2c_get_clientdata(client);

	device_remove_file(&client->dev, &dev_attr_otp_id_ov2311);
	v4l2_async_unregister_subdev(&priv->sd);
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->sd);

	return 0;
}

static const struct i2c_device_id ov2311_id[] = {
	{ "ov2311", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov2311_id);

static const struct of_device_id ov2311_of_ids[] = {
	{ .compatible = "ovti,ov2311", },
	{ }
};
MODULE_DEVICE_TABLE(of, ov2311_of_ids);

static struct i2c_driver ov2311_i2c_driver = {
	.driver	= {
		.name		= "ov2311",
		.of_match_table	= ov2311_of_ids,
	},
	.probe		= ov2311_probe,
	.remove		= ov2311_remove,
	.id_table	= ov2311_id,
};

module_i2c_driver(ov2311_i2c_driver);

MODULE_DESCRIPTION("Camera glue driver for OV2311");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
