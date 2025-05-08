/*
 * Sony ISX019 (isp) camera driver
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
#include "isx019.h"

static const int isx019_i2c_addr[] = {0x1a};

#define ISX019_PID_REG		0x0000
#define ISX019_PID		0x4000

#define ISX019_MEDIA_BUS_FMT	MEDIA_BUS_FMT_UYVY8_2X8

static void isx019_otp_id_read(struct i2c_client *client);

struct isx019_priv {
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

static char *intf = "command";
module_param(intf, charp, 0644);
MODULE_PARM_DESC(intf, " Registers access interface command,address (default: command)");

static inline struct isx019_priv *to_isx019(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct isx019_priv, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct isx019_priv, hdl)->sd;
}

static int isx019_read16(struct i2c_client *client, u8 category, u16 reg, u16 *val)
{
	int ret = -1;

	if (strcmp(intf, "command") == 0) {
 #define R_NUM_BYTES		9
 #define R_NUM_CMDS		1
 #define R_NUM_CMD_BYTES	6
 #define R_CMD			1
 #define R_BYTES		2
		u8 buf[R_NUM_BYTES] = {R_NUM_BYTES, R_NUM_CMDS,
				       R_NUM_CMD_BYTES, R_CMD,
				       category, reg >> 8, reg & 0xff,
				       R_BYTES, 0x00};

		/* calculate checksum */
		buf[8] = R_NUM_BYTES + R_NUM_CMDS + R_NUM_CMD_BYTES + R_CMD +
			 category + (reg >> 8) + (reg & 0xff) + R_BYTES;

		ret = i2c_master_send(client, buf, R_NUM_BYTES);
		if (ret == R_NUM_BYTES)
			ret = i2c_master_recv(client, buf, R_NUM_BYTES - 2);

		if (ret < 0) {
			dev_err(&client->dev,
				"read fail: chip 0x%x register 0x%x: %d\n",
				client->addr, reg, ret);
		} else {
			*val = buf[4] | ((u16)buf[5] << 8);
		}
	} else if (strcmp(intf, "address") == 0) {
		reg16_write(client, 0xFFFF, category);
		ret = reg16_read16(client, reg, val);
		*val = swab16p(val);
	} else {
		dev_err(&client->dev, "invalid register access interface %s\n", intf);
	}

	return ret < 0 ? ret : 0;
}

static int isx019_write16(struct i2c_client *client, u8 category, u16 reg, u16 val)
{
	int ret = -1;

	if (strcmp(intf, "command") == 0) {
 #define W_NUM_BYTES		10
 #define W_NUM_CMDS		1
 #define W_NUM_CMD_BYTES	7
 #define W_CMD			2
		u8 buf[W_NUM_BYTES] = {W_NUM_BYTES, W_NUM_CMDS,
				       W_NUM_CMD_BYTES, W_CMD,
				       category, reg >> 8, reg & 0xff,
				       val & 0xff, val >> 8};

		/* calculate checksum */
		buf[9] = W_NUM_BYTES + W_NUM_CMDS + W_NUM_CMD_BYTES + W_CMD +
			 category + (reg >> 8) + (reg & 0xff) + (val >> 8) + (val & 0xff);

		ret = i2c_master_send(client, buf, W_NUM_BYTES);

		if (ret < 0) {
			dev_err(&client->dev,
				"write fail: chip 0x%x register 0x%x: %d\n",
				client->addr, reg, ret);
		}
	} else if (strcmp(intf, "address") == 0) {
		val = swab16(val);
		reg16_write(client, 0xFFFF, category);
		ret = reg16_write16(client, reg, val);
	} else {
		dev_err(&client->dev, "invalid register acces interface %s\n", intf);
	}

	return ret < 0 ? ret : 0;
}

static int isx019_set_regs(struct i2c_client *client,
			   const struct isx019_reg *regs, int nr_regs)
{
	int i;

	for (i = 0; i < nr_regs; i++) {
		if (regs[i].reg == ISX019_DELAY) {
			mdelay(regs[i].val);
			continue;
		}

		isx019_write16(client, regs[i].reg >> 8, regs[i].reg & 0xff, regs[i].val);
	}

	return 0;
}

static void isx019_otp_id_read(struct i2c_client *client)
{
	struct isx019_priv *priv = to_isx019(client);
	int i;
	u16 val = 0;

	/* read camera id from isx019 OTP memory */
	for (i = 0; i < 6; i+=2) {
		isx019_read16(client, 8, 0x60 + i, &val);
		priv->id[i] = val >> 8;
		priv->id[i+1] = val & 0xff;
	}
}

static int isx019_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int isx019_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx019_priv *priv = to_isx019(client);

	if (format->pad)
		return -EINVAL;

	mf->width = priv->rect.width;
	mf->height = priv->rect.height;
	mf->code = ISX019_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int isx019_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	mf->code = ISX019_MEDIA_BUS_FMT;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		cfg->try_fmt = *mf;

	return 0;
}

static int isx019_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = ISX019_MEDIA_BUS_FMT;

	return 0;
}

static int isx019_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx019_priv *priv = to_isx019(client);

	isx019_otp_id_read(client);

	memcpy(edid->edid, priv->id, 6);

	edid->edid[6] = 0xff;
	edid->edid[7] = client->addr;
	edid->edid[8] = ISX019_PID >> 8;
	edid->edid[9] = ISX019_PID & 0xff;

	return 0;
}

static int isx019_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct v4l2_rect *rect = &sel->r;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx019_priv *priv = to_isx019(client);

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

static int isx019_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx019_priv *priv = to_isx019(client);

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
static int isx019_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u16 val = 0;

	ret = isx019_read16(client, (u16)reg->reg >> 8, (u16)reg->reg & 0xff, &val);
	if (ret < 0)
		return ret;

	reg->val = val;
	reg->size = sizeof(u16);

	return 0;
}

static int isx019_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return isx019_write16(client, (u16)reg->reg >> 8, (u16)reg->reg & 0xff, (u16)reg->val);
}
#endif

static struct v4l2_subdev_core_ops isx019_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = isx019_g_register,
	.s_register = isx019_s_register,
#endif
};

static int isx019_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx019_priv *priv = to_isx019(client);
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
	}

	return ret;
}

static const struct v4l2_ctrl_ops isx019_ctrl_ops = {
	.s_ctrl = isx019_s_ctrl,
};

static struct v4l2_subdev_video_ops isx019_video_ops = {
	.s_stream	= isx019_s_stream,
};

static const struct v4l2_subdev_pad_ops isx019_subdev_pad_ops = {
	.get_edid	= isx019_get_edid,
	.enum_mbus_code	= isx019_enum_mbus_code,
	.get_selection	= isx019_get_selection,
	.set_selection	= isx019_set_selection,
	.get_fmt	= isx019_get_fmt,
	.set_fmt	= isx019_set_fmt,
};

static struct v4l2_subdev_ops isx019_subdev_ops = {
	.core	= &isx019_core_ops,
	.video	= &isx019_video_ops,
	.pad	= &isx019_subdev_pad_ops,
};

static ssize_t isx019_otp_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct isx019_priv *priv = to_isx019(client);

	isx019_otp_id_read(client);

	return snprintf(buf, 32, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
}

static DEVICE_ATTR(otp_id_isx019, S_IRUGO, isx019_otp_id_show, NULL);

static int isx019_initialize(struct i2c_client *client)
{
	struct isx019_priv *priv = to_isx019(client);
	u16 pid = 0, val = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(isx019_i2c_addr); i++) {
		setup_i2c_translator(client, priv->ser_addr, isx019_i2c_addr[i]);

		/* check model ID */
		isx019_read16(client, 0, ISX019_PID_REG, &pid);

		if ((pid & 0xff00) == ISX019_PID)
			break;
	}

	if ((pid & 0xff00) != ISX019_PID) {
		dev_dbg(&client->dev, "Product ID error %x\n", pid);
		return -ENODEV;
	}

	/* Read OTP IDs */
	isx019_otp_id_read(client);
	/* Program wizard registers */
	isx019_set_regs(client, isx019_regs, ARRAY_SIZE(isx019_regs));
	/* read resolution used by current firmware */
	isx019_read16(client, 86, 0x8, &val);
	priv->max_width = val;
	isx019_read16(client, 86, 0xa, &val);
	priv->max_height = val;

	dev_info(&client->dev, "PID %x (rev %x), res %dx%d, if=%s, OTP_ID %02x:%02x:%02x:%02x:%02x:%02x\n",
		 pid & 0xff00, pid & 0xff, priv->max_width, priv->max_height, intf, priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
	return 0;
}

static int isx019_parse_dt(struct device_node *np, struct isx019_priv *priv)
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

static int isx019_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct isx019_priv *priv;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->sd, client, &isx019_subdev_ops);
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	priv->exposure = 0x100;
	priv->gain = 0x100;
	priv->autogain = 1;
	v4l2_ctrl_handler_init(&priv->hdl, 4);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 7, 1, 2);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_HUE, 0, 23, 1, 12);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_GAMMA, -128, 128, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_SHARPNESS, 0, 10, 1, 3);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_AUTOGAIN, 0, 1, 1, priv->autogain);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_GAIN, 0, 0xffff, 1, priv->gain);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_EXPOSURE, 0, 0xffff, 1, priv->exposure);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&priv->hdl, &isx019_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
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

	ret = isx019_parse_dt(client->dev.of_node, priv);
	if (ret)
		goto cleanup;

	ret = isx019_initialize(client);
	if (ret < 0)
		goto cleanup;

	priv->rect.left = 0;
	priv->rect.top = 0;
	priv->rect.width = priv->max_width;
	priv->rect.height = priv->max_height;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret)
		goto cleanup;

	if (device_create_file(&client->dev, &dev_attr_otp_id_isx019) != 0) {
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

static int isx019_remove(struct i2c_client *client)
{
	struct isx019_priv *priv = i2c_get_clientdata(client);

	device_remove_file(&client->dev, &dev_attr_otp_id_isx019);
	v4l2_async_unregister_subdev(&priv->sd);
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->sd);

	return 0;
}

static const struct i2c_device_id isx019_id[] = {
	{ "isx019", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isx019_id);

static const struct of_device_id isx019_of_ids[] = {
	{ .compatible = "sony,isx019", },
	{ }
};
MODULE_DEVICE_TABLE(of, isx019_of_ids);

static struct i2c_driver isx019_i2c_driver = {
	.driver	= {
		.name		= "isx019",
		.of_match_table	= isx019_of_ids,
	},
	.probe		= isx019_probe,
	.remove		= isx019_remove,
	.id_table	= isx019_id,
};

module_i2c_driver(isx019_i2c_driver);

MODULE_DESCRIPTION("Camera glue driver for ISX019");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
