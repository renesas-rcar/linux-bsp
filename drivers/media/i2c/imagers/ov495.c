/*
 * OmniVision ov495-2775 sensor camera glue
 *
 * Copyright (C) 2017-2020 Cogent Embedded, Inc.
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
#include "ov495.h"

#define OV495_I2C_ADDR		0x24

#define OV495_PID_REGA		0x300a
#define OV495_PID_REGB		0x300b
#define OV495_PID		0x0495

#define OV495_ISP_HSIZE_LOW	0x60
#define OV495_ISP_HSIZE_HIGH	0x61
#define OV495_ISP_VSIZE_LOW	0x62
#define OV495_ISP_VSIZE_HIGH	0x63

#define OV495_MEDIA_BUS_FMT	MEDIA_BUS_FMT_UYVY8_2X8

struct ov495_priv {
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
	int				des_addr;
};

static int conf_link;
module_param(conf_link, int, 0644);
MODULE_PARM_DESC(conf_link, " Force configuration link. Used only if robust firmware flashing required (f.e. recovery)");

static inline struct ov495_priv *to_ov495(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov495_priv, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov495_priv, hdl)->sd;
}

static int ov495_set_regs(struct i2c_client *client,
			  const struct ov495_reg *regs, int nr_regs)
{
	int i;

	for (i = 0; i < nr_regs; i++) {
		if (reg16_write(client, regs[i].reg, regs[i].val)) {
			usleep_range(100, 150); /* wait 100 us */
			reg16_write(client, regs[i].reg, regs[i].val);
		}
	}

	return 0;
}

static void ov495_otp_id_read(struct i2c_client *client)
{
	struct ov495_priv *priv = to_ov495(client);
	int i;

#if 0
	/* read camera id from ov495 OTP memory */
	reg16_write(client, 0xFFFD, 0x80);
	reg16_write(client, 0xFFFE, 0x20);
	usleep_range(100, 150); /* wait 100 us */
	reg16_write(client, 0x7384, 0x40); /* manual mode, bank#0 */
	reg16_write(client, 0x7381, 1); /* start OTP read */

	usleep_range(25000, 26000); /* wait 25 ms */

	for (i = 0; i < 6; i++)
		reg16_read(client, 0x7300 + i + 4, &priv->id[i]);
#else
	/* read camera id from ov2775 OTP memory */
	reg16_write(client, 0x3516, 0x00); /* unlock write */
	reg16_write(client, 0x0FFC, 0);
	reg16_write(client, 0x0500, 0x00); /* write 0x34a1 -> 1 */
	reg16_write(client, 0x0501, 0x34);
	reg16_write(client, 0x0502, 0xa1);
	reg16_write(client, 0x0503, 1);
	reg16_write(client, 0x30C0, 0xc1);

	usleep_range(25000, 25500); /* wait 25 ms */

	for (i = 0; i < 6; i++) {
		reg16_write(client, 0x3516, 0x00); /* unlock write */
		reg16_write(client, 0x0500, 0x01); /* read (0x7a00 + i) */
		reg16_write(client, 0x0501, 0x7a);
		reg16_write(client, 0x0502, 0x00 + i + (i < 3 ? 11 : 3)); /* take bytes 11,12,13,6,7,8 */
		reg16_write(client, 0x30C0, 0xc1);
		usleep_range(1000, 1500); /* wait 1 ms */
		reg16_read(client, 0x0500, &priv->id[i]);
	}
#endif
}

static int ov495_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int ov495_get_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov495_priv *priv = to_ov495(client);

	if (format->pad)
		return -EINVAL;

	mf->width = priv->rect.width;
	mf->height = priv->rect.height;
	mf->code = OV495_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int ov495_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	mf->code = OV495_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		cfg->try_fmt = *mf;

	return 0;
}

static int ov495_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = OV495_MEDIA_BUS_FMT;

	return 0;
}

static int ov495_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov495_priv *priv = to_ov495(client);

	memcpy(edid->edid, priv->id, 6);

	edid->edid[6] = 0xff;
	edid->edid[7] = client->addr;
	edid->edid[8] = OV495_PID >> 8;
	edid->edid[9] = OV495_PID & 0xff;

	return 0;
}

static int ov495_set_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	struct v4l2_rect *rect = &sel->r;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov495_priv *priv = to_ov495(client);

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

static int ov495_get_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov495_priv *priv = to_ov495(client);

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
static int ov495_g_register(struct v4l2_subdev *sd,
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

static int ov495_s_register(struct v4l2_subdev *sd,
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

static struct v4l2_subdev_core_ops ov495_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ov495_g_register,
	.s_register = ov495_s_register,
#endif
};

static int ov495_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov495_priv *priv = to_ov495(client);
	int ret = -EINVAL;

	if (!priv->init_complete)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		break;
	case V4L2_CID_CONTRAST:
		break;
	case V4L2_CID_SATURATION:
		break;
	case V4L2_CID_HUE:
		break;
	case V4L2_CID_GAMMA:
		break;
	case V4L2_CID_SHARPNESS:
		break;
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_GAIN:
	case V4L2_CID_EXPOSURE:
		break;
	case V4L2_CID_HFLIP:
		ret = reg16_write(client, 0x3516, 0x00);
		ret |= reg16_write(client, 0x0ffc, 0x00);
		ret |= reg16_write(client, 0x0500, ctrl->val);
		ret |= reg16_write(client, 0x0501, 0x00);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x30C0, 0xdc);
		ret |= reg16_write(client, 0x3516, 0x01);
		break;
	case V4L2_CID_VFLIP:
		ret = reg16_write(client, 0x3516, 0x00);
		ret |= reg16_write(client, 0x0ffc, 0x00);
		ret |= reg16_write(client, 0x0500, ctrl->val);
		ret |= reg16_write(client, 0x0501, 0x01);
		usleep_range(100, 150); /* wait 100 us */
		ret |= reg16_write(client, 0x30C0, 0xdc);
		ret |= reg16_write(client, 0x3516, 0x01);
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ret = 0;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ov495_ctrl_ops = {
	.s_ctrl = ov495_s_ctrl,
};

static struct v4l2_subdev_video_ops ov495_video_ops = {
	.s_stream	= ov495_s_stream,
};

static const struct v4l2_subdev_pad_ops ov495_subdev_pad_ops = {
	.get_edid	= ov495_get_edid,
	.enum_mbus_code	= ov495_enum_mbus_code,
	.get_selection	= ov495_get_selection,
	.set_selection	= ov495_set_selection,
	.get_fmt	= ov495_get_fmt,
	.set_fmt	= ov495_set_fmt,
};

static struct v4l2_subdev_ops ov495_subdev_ops = {
	.core	= &ov495_core_ops,
	.video	= &ov495_video_ops,
	.pad	= &ov495_subdev_pad_ops,
};

static ssize_t ov495_otp_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov495_priv *priv = to_ov495(client);

	return snprintf(buf, 32, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
}

static DEVICE_ATTR(otp_id_ov495, S_IRUGO, ov495_otp_id_show, NULL);

static int ov495_initialize(struct i2c_client *client)
{
	struct ov495_priv *priv = to_ov495(client);
	u8 val = 0;
	u16 pid = 0;

	setup_i2c_translator(client, priv->ser_addr, OV495_I2C_ADDR);

	reg16_write(client, 0xFFFD, 0x80);
	reg16_write(client, 0xFFFE, 0x80);
	usleep_range(100, 150); /* wait 100 us */
	reg16_read(client, OV495_PID_REGA, &val);
	pid = val;
	reg16_read(client, OV495_PID_REGB, &val);
	pid = (pid << 8) | val;

	if (pid != OV495_PID) {
		dev_dbg(&client->dev, "Product ID error %x\n", pid);
		return -ENODEV;
	}

#if 0
	/* setup XCLK */
	tmp_addr = client->addr;
	if (priv->ti9x4_addr) {
		/* CLK_OUT=22.5792*160*M/N/CLKDIV -> CLK_OUT=25MHz: CLKDIV=4, M=7, N=253: 22.5792*160/4*7/253=24.989MHz=CLK_OUT */
		client->addr = priv->ti9x3_addr;			/* Serializer I2C address */
		reg8_write(client, 0x06, 0x47);				/* Set CLKDIV and M */
		reg8_write(client, 0x07, 0xfd);				/* Set N */
	}
	client->addr = tmp_addr;
#endif

	if (unlikely(conf_link))
		goto out;

#if 0
	/* read resolution used by current firmware */
	reg16_write(client, 0xFFFD, 0x80);
	reg16_write(client, 0xFFFE, 0x82);
	usleep_range(100, 150); /* wait 100 us */
	reg16_read(client, OV495_ISP_HSIZE_HIGH, &val);
	priv->max_width = val;
	reg16_read(client, OV495_ISP_HSIZE_LOW, &val);
	priv->max_width = (priv->max_width << 8) | val;
	reg16_read(client, OV495_ISP_VSIZE_HIGH, &val);
	priv->max_height = val;
	reg16_read(client, OV495_ISP_VSIZE_LOW, &val);
	priv->max_height = (priv->max_height << 8) | val;
#else
	priv->max_width = 1920;
	priv->max_height = 1080;
#endif
	/* set virtual channel */
//	ov495_regs[3].val = 0x1e | (priv->port << 6);
	/* Program wizard registers */
	ov495_set_regs(client, ov495_regs, ARRAY_SIZE(ov495_regs));
	/* Read OTP IDs */
	ov495_otp_id_read(client);

out:
	dev_info(&client->dev, "PID %x, res %dx%d, OTP_ID %02x:%02x:%02x:%02x:%02x:%02x\n",
		 pid, priv->max_width, priv->max_height, priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
	return 0;
}

static int ov495_parse_dt(struct device_node *np, struct ov495_priv *priv)
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

static int ov495_probe(struct i2c_client *client,
		       const struct i2c_device_id *did)
{
	struct ov495_priv *priv;
	struct v4l2_ctrl *ctrl;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->sd, client, &ov495_subdev_ops);
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	priv->exposure = 0x100;
	priv->gain = 0x100;
	priv->autogain = 1;
	v4l2_ctrl_handler_init(&priv->hdl, 4);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 7, 1, 2);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_HUE, 0, 23, 1, 12);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_GAMMA, -128, 128, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_SHARPNESS, 0, 10, 1, 3);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_AUTOGAIN, 0, 1, 1, priv->autogain);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_GAIN, 0, 0xffff, 1, priv->gain);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_EXPOSURE, 0, 0xffff, 1, priv->exposure);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
	ctrl = v4l2_ctrl_new_std(&priv->hdl, &ov495_ctrl_ops,
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

	ret = ov495_parse_dt(client->dev.of_node, priv);
	if (ret)
		goto cleanup;

	ret = ov495_initialize(client);
	if (ret < 0)
		goto cleanup;

	priv->rect.left = 0;
	priv->rect.top = 0;
	priv->rect.width = priv->max_width;
	priv->rect.height = priv->max_height;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret)
		goto cleanup;

	if (device_create_file(&client->dev, &dev_attr_otp_id_ov495) != 0) {
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

static int ov495_remove(struct i2c_client *client)
{
	struct ov495_priv *priv = i2c_get_clientdata(client);

	device_remove_file(&client->dev, &dev_attr_otp_id_ov495);
	v4l2_async_unregister_subdev(&priv->sd);
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->sd);

	return 0;
}

static const struct i2c_device_id ov495_id[] = {
	{ "ov495", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov495_id);

static const struct of_device_id ov495_of_ids[] = {
	{ .compatible = "ovti,ov495", },
	{ }
};
MODULE_DEVICE_TABLE(of, ov495_of_ids);

static struct i2c_driver ov495_i2c_driver = {
	.driver	= {
		.name		= "ov495",
		.of_match_table	= ov495_of_ids,
	},
	.probe		= ov495_probe,
	.remove		= ov495_remove,
	.id_table	= ov495_id,
};

module_i2c_driver(ov495_i2c_driver);

MODULE_DESCRIPTION("Camera glue driver for OV495-2775");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
