/*
 * Sony ISX016 (isp) camera driver
 *
 * Copyright (C) 2019-2020 Cogent Embedded, Inc.
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
#include "isx016.h"

static const int isx016_i2c_addr[] = {0x1a};

#define ISX016_PID_REG		0x0000
#define ISX016_PID		0x0D20

#define ISX016_MEDIA_BUS_FMT	MEDIA_BUS_FMT_UYVY8_2X8

static void isx016_otp_id_read(struct i2c_client *client);

struct isx016_priv {
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

static inline struct isx016_priv *to_isx016(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct isx016_priv, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct isx016_priv, hdl)->sd;
}

static int isx016_read16(struct i2c_client *client, u8 category, u16 reg, u16 *val)
{
	reg16_write(client, 0xFFFF, category);
	reg16_read16(client, reg, val);

	return 0;
}

static int isx016_write16(struct i2c_client *client, u8 category, u16 reg, u16 val)
{
	reg16_write(client, 0xFFFF, category);
	reg16_write16(client, reg, val);

	return 0;
}

static int isx016_set_regs(struct i2c_client *client,
			  const struct isx016_reg *regs, int nr_regs)
{
	int i;

	for (i = 0; i < nr_regs; i++) {
		if (regs[i].reg == ISX016_DELAY) {
			mdelay(regs[i].val);
			continue;
		}

		isx016_write16(client, regs[i].reg >> 8, regs[i].reg & 0xff, regs[i].val);
	}

	return 0;
}

static void isx016_otp_id_read(struct i2c_client *client)
{
	struct isx016_priv *priv = to_isx016(client);
	int i;
	u16 val = 0;

	/* read camera id from isx016 FUSEs */
	for (i = 0; i < 6; i+=2) {
		isx016_read16(client, 92, 0x0a + i, &val);
		priv->id[i] = val >> 8;
		priv->id[i+1] = val & 0xff;
	}
}

static int isx016_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int isx016_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx016_priv *priv = to_isx016(client);

	if (format->pad)
		return -EINVAL;

	mf->width = priv->rect.width;
	mf->height = priv->rect.height;
	mf->code = ISX016_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int isx016_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	mf->code = ISX016_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		cfg->try_fmt = *mf;

	return 0;
}

static int isx016_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = ISX016_MEDIA_BUS_FMT;

	return 0;
}

static int isx016_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx016_priv *priv = to_isx016(client);

	isx016_otp_id_read(client);

	memcpy(edid->edid, priv->id, 6);

	edid->edid[6] = 0xff;
	edid->edid[7] = client->addr;
	edid->edid[8] = ISX016_PID >> 8;
	edid->edid[9] = ISX016_PID & 0xff;

	return 0;
}

static int isx016_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct v4l2_rect *rect = &sel->r;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx016_priv *priv = to_isx016(client);

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

static int isx016_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx016_priv *priv = to_isx016(client);

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
static int isx016_g_register(struct v4l2_subdev *sd,
			    struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u16 val = 0;

	ret = isx016_read16(client, (u16)reg->reg >> 8, (u16)reg->reg & 0xff, &val);
	if (ret < 0)
		return ret;

	reg->val = val;
	reg->size = sizeof(u16);

	return 0;
}

static int isx016_s_register(struct v4l2_subdev *sd,
			    const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return isx016_write16(client, (u16)reg->reg >> 8, (u16)reg->reg & 0xff, (u16)reg->val);
}
#endif

static struct v4l2_subdev_core_ops isx016_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = isx016_g_register,
	.s_register = isx016_s_register,
#endif
};

static int isx016_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx016_priv *priv = to_isx016(client);
	int ret = -EINVAL;

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
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ret = 0;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops isx016_ctrl_ops = {
	.s_ctrl = isx016_s_ctrl,
};

static struct v4l2_subdev_video_ops isx016_video_ops = {
	.s_stream	= isx016_s_stream,
};

static const struct v4l2_subdev_pad_ops isx016_subdev_pad_ops = {
	.get_edid	= isx016_get_edid,
	.enum_mbus_code	= isx016_enum_mbus_code,
	.get_selection	= isx016_get_selection,
	.set_selection	= isx016_set_selection,
	.get_fmt	= isx016_get_fmt,
	.set_fmt	= isx016_set_fmt,
};

static struct v4l2_subdev_ops isx016_subdev_ops = {
	.core	= &isx016_core_ops,
	.video	= &isx016_video_ops,
	.pad	= &isx016_subdev_pad_ops,
};

static ssize_t isx016_otp_id_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx016_priv *priv = to_isx016(client);

	isx016_otp_id_read(client);

	return snprintf(buf, 32, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
}

static DEVICE_ATTR(otp_id_isx016, S_IRUGO, isx016_otp_id_show, NULL);

static int isx016_initialize(struct i2c_client *client)
{
	struct isx016_priv *priv = to_isx016(client);
	u16 pid = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(isx016_i2c_addr); i++) {
		setup_i2c_translator(client, priv->ser_addr, isx016_i2c_addr[i]);

		/* check model ID */
		isx016_read16(client, 0, ISX016_PID_REG, &pid);

		if (pid == ISX016_PID)
			break;
	}

	if (pid != ISX016_PID) {
		dev_dbg(&client->dev, "Product ID error %x\n", pid);
		return -ENODEV;
	}

	priv->max_width = ISX016_MAX_WIDTH;
	priv->max_height = ISX016_MAX_HEIGHT;

	/* Read OTP IDs */
	isx016_otp_id_read(client);
	/* Program wizard registers */
	isx016_set_regs(client, isx016_regs, ARRAY_SIZE(isx016_regs));

	dev_info(&client->dev, "PID %x, res %dx%d, OTP_ID %02x:%02x:%02x:%02x:%02x:%02x\n",
		 pid, priv->max_width, priv->max_height, priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
	return 0;
}

static int isx016_parse_dt(struct device_node *np, struct isx016_priv *priv)
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

static int isx016_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct isx016_priv *priv;
	struct v4l2_ctrl *ctrl;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->sd, client, &isx016_subdev_ops);
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	priv->exposure = 0x100;
	priv->gain = 0x100;
	priv->autogain = 1;
	v4l2_ctrl_handler_init(&priv->hdl, 4);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 7, 1, 2);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_HUE, 0, 23, 1, 12);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_GAMMA, -128, 128, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_SHARPNESS, 0, 10, 1, 3);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_AUTOGAIN, 0, 1, 1, priv->autogain);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_GAIN, 0, 0xffff, 1, priv->gain);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_EXPOSURE, 0, 0xffff, 1, priv->exposure);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
	ctrl = v4l2_ctrl_new_std(&priv->hdl, &isx016_ctrl_ops,
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
	priv->sd.entity.flags |= MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&priv->sd.entity, 1, &priv->pad);
	if (ret < 0)
		goto cleanup;

	ret = isx016_parse_dt(client->dev.of_node, priv);
	if (ret)
		goto cleanup;

	ret = isx016_initialize(client);
	if (ret < 0)
		goto cleanup;

	priv->rect.left = 0;
	priv->rect.top = 0;
	priv->rect.width = priv->max_width;
	priv->rect.height = priv->max_height;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret)
		goto cleanup;

	if (device_create_file(&client->dev, &dev_attr_otp_id_isx016) != 0) {
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

static int isx016_remove(struct i2c_client *client)
{
	struct isx016_priv *priv = i2c_get_clientdata(client);

	device_remove_file(&client->dev, &dev_attr_otp_id_isx016);
	v4l2_async_unregister_subdev(&priv->sd);
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->sd);

	return 0;
}

static const struct i2c_device_id isx016_id[] = {
	{ "isx016", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isx016_id);

static const struct of_device_id isx016_of_ids[] = {
	{ .compatible = "sony,isx016", },
	{ }
};
MODULE_DEVICE_TABLE(of, isx016_of_ids);

static struct i2c_driver isx016_i2c_driver = {
	.driver = {
		.name = "isx016",
		.of_match_table = isx016_of_ids,
	},
	.probe = isx016_probe,
	.remove = isx016_remove,
	.id_table = isx016_id,
};

module_i2c_driver(isx016_i2c_driver);

MODULE_DESCRIPTION("Camera glue driver for ISX016");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
