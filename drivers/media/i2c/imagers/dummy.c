/*
 * Dummy sensor camera driver
 *
 * Copyright (C) 2019 Cogent Embedded, Inc.
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

struct dummy_priv {
	struct v4l2_subdev		sd;
	struct v4l2_ctrl_handler	hdl;
	struct media_pad		pad;
	struct v4l2_rect		rect;
	u8				id[6];
	int				max_width;
	int				max_height;
	const char *			media_bus_format;
	int				mbus_format;
};

static int width = 1920;
module_param(width, int, 0644);
MODULE_PARM_DESC(width, " width (default: 1920)");

static int height = 1080;
module_param(height, int, 0644);
MODULE_PARM_DESC(height, " height (default: 1080)");

static char *mbus = "uyvy";
module_param(mbus, charp, 0644);
MODULE_PARM_DESC(mbus, " MEDIA_BUS_FORMAT (default: UYVY)");

static inline struct dummy_priv *to_dummy(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct dummy_priv, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct dummy_priv, hdl)->sd;
}

static int dummy_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int dummy_get_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct dummy_priv *priv = to_dummy(client);

	if (format->pad)
		return -EINVAL;

	mf->width = priv->rect.width;
	mf->height = priv->rect.height;
	mf->code = priv->mbus_format;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int dummy_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct dummy_priv *priv = to_dummy(client);

	mf->code = priv->mbus_format;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		cfg->try_fmt = *mf;

	return 0;
}

static int dummy_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct dummy_priv *priv = to_dummy(client);

	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = priv->mbus_format;

	return 0;
}

static int dummy_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct dummy_priv *priv = to_dummy(client);

	memcpy(edid->edid, priv->id, 6);

	edid->edid[6] = 0xff;
	edid->edid[7] = client->addr;
	edid->edid[8] = 'D' >> 8;
	edid->edid[9] = 'Y' & 0xff;

	return 0;
}

static int dummy_set_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	struct v4l2_rect *rect = &sel->r;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct dummy_priv *priv = to_dummy(client);

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

static int dummy_get_selection(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct dummy_priv *priv = to_dummy(client);

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
static int dummy_g_register(struct v4l2_subdev *sd,
			    struct v4l2_dbg_register *reg)
{
	reg->val = 0;
	reg->size = sizeof(u16);

	return 0;
}

static int dummy_s_register(struct v4l2_subdev *sd,
			    const struct v4l2_dbg_register *reg)
{
	return 0;
}
#endif

static struct v4l2_subdev_core_ops dummy_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = dummy_g_register,
	.s_register = dummy_s_register,
#endif
};

static int dummy_s_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_GAMMA:
	case V4L2_CID_SHARPNESS:
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_GAIN:
	case V4L2_CID_ANALOGUE_GAIN:
	case V4L2_CID_EXPOSURE:
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops dummy_ctrl_ops = {
	.s_ctrl = dummy_s_ctrl,
};

static struct v4l2_subdev_video_ops dummy_video_ops = {
	.s_stream	= dummy_s_stream,
};

static const struct v4l2_subdev_pad_ops dummy_subdev_pad_ops = {
	.get_edid	= dummy_get_edid,
	.enum_mbus_code	= dummy_enum_mbus_code,
	.get_selection	= dummy_get_selection,
	.set_selection	= dummy_set_selection,
	.get_fmt	= dummy_get_fmt,
	.set_fmt	= dummy_set_fmt,
};

static struct v4l2_subdev_ops dummy_subdev_ops = {
	.core	= &dummy_core_ops,
	.video	= &dummy_video_ops,
	.pad	= &dummy_subdev_pad_ops,
};

static void dummy_otp_id_read(struct i2c_client *client)
{
	struct dummy_priv *priv = to_dummy(client);

	/* dummy camera id */
	priv->id[0] = 'd';
	priv->id[1] = 'u';
	priv->id[2] = 'm';
	priv->id[3] = 'm';
	priv->id[4] = 'y';
	priv->id[5] = '.';
}

static ssize_t dummy_otp_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(to_i2c_client(dev));
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct dummy_priv *priv = to_dummy(client);

	dummy_otp_id_read(client);

	return snprintf(buf, 32, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);
}

static DEVICE_ATTR(otp_id_dummy, S_IRUGO, dummy_otp_id_show, NULL);

static int dummy_initialize(struct i2c_client *client)
{
	struct dummy_priv *priv = to_dummy(client);

	if (strcmp(priv->media_bus_format, "yuyv") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_YUYV8_2X8;
	else if (strcmp(priv->media_bus_format, "uyvy") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_UYVY8_2X8;
	else if (strcmp(priv->media_bus_format, "grey") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_Y8_1X8;
	else if (strcmp(priv->media_bus_format, "rggb8") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SRGGB8_1X8;
	else if (strcmp(priv->media_bus_format, "bggr8") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SBGGR8_1X8;
	else if (strcmp(priv->media_bus_format, "grbg8") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SGRBG8_1X8;
	else if (strcmp(priv->media_bus_format, "rggb12") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SRGGB12_1X12;
	else if (strcmp(priv->media_bus_format, "bggr12") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SBGGR12_1X12;
	else if (strcmp(priv->media_bus_format, "grbg12") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SGRBG12_1X12;
	else if (strcmp(priv->media_bus_format, "rggb14") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SRGGB14_1X14;
	else if (strcmp(priv->media_bus_format, "bggr14") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SBGGR14_1X14;
	else if (strcmp(priv->media_bus_format, "grbg14") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SGRBG14_1X14;
	else if (strcmp(priv->media_bus_format, "rggb16") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SRGGB16_1X16;
	else if (strcmp(priv->media_bus_format, "bggr16") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SBGGR16_1X16;
	else if (strcmp(priv->media_bus_format, "grbg16") == 0)
		priv->mbus_format = MEDIA_BUS_FMT_SGRBG16_1X16;
	else {
		v4l_err(client, "failed to parse mbus format (%s)\n", priv->media_bus_format);
		return -EINVAL;
	}

	/* Read OTP IDs */
	dummy_otp_id_read(client);

	dev_info(&client->dev, "res %dx%d, mbus %s, OTP_ID %02x:%02x:%02x:%02x:%02x:%02x\n",
		 priv->max_width, priv->max_height, priv->media_bus_format, priv->id[0], priv->id[1], priv->id[2], priv->id[3], priv->id[4], priv->id[5]);

	return 0;
}

static const struct i2c_device_id dummy_id[] = {
	{ "dummy-camera", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dummy_id);

static const struct of_device_id dummy_of_ids[] = {
	{ .compatible = "dummy,camera", },
	{ }
};
MODULE_DEVICE_TABLE(of, dummy_of_ids);

static int dummy_parse_dt(struct device_node *np, struct dummy_priv *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);
	struct fwnode_handle *ep;

	if (of_property_read_u32(np, "dummy,width", &priv->max_width))
		priv->max_width = width;

	if (of_property_read_u32(np, "dummy,height", &priv->max_height))
		priv->max_height = height;

	if (of_property_read_string(np, "dummy,mbus", &priv->media_bus_format))
		priv->media_bus_format = mbus;

	/* module params override dts */
	if (strcmp(mbus, "uyvy"))
		priv->media_bus_format = mbus;
	if (width != 1920)
		priv->max_width = width;
	if (height != 1080)
		priv->max_height = height;

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev), NULL);
	if (!ep) {
		dev_err(&client->dev, "Unable to get endpoint in node %pOF: %ld\n",
				      client->dev.of_node, PTR_ERR(ep));
		return -ENOENT;
	}
	priv->sd.fwnode = ep;

	return 0;
}

static int dummy_probe(struct i2c_client *client,
		       const struct i2c_device_id *did)
{
	struct dummy_priv *priv;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->sd, client, &dummy_subdev_ops);
	priv->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	v4l2_ctrl_handler_init(&priv->hdl, 4);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 16, 1, 7);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 7, 1, 2);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_HUE, 0, 23, 1, 12);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_GAMMA, -128, 128, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_SHARPNESS, 0, 10, 1, 3);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_AUTOGAIN, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_GAIN, 1, 0x7ff, 1, 0x200);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, 1, 0xe, 1, 0xa);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_EXPOSURE, 1, 0x600, 1, 0x144);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &dummy_ctrl_ops,
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

	ret = dummy_parse_dt(client->dev.of_node, priv);
	if (ret)
		goto cleanup;

	ret = dummy_initialize(client);
	if (ret < 0)
		goto cleanup;

	priv->rect.left = 0;
	priv->rect.top = 0;
	priv->rect.width = priv->max_width;
	priv->rect.height = priv->max_height;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret)
		goto cleanup;

	if (device_create_file(&client->dev, &dev_attr_otp_id_dummy) != 0) {
		dev_err(&client->dev, "sysfs otp_id entry creation failed\n");
		goto cleanup;
	}

	return 0;

cleanup:
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->sd);
	v4l_err(client, "failed to probe @ 0x%02x (%s)\n",
		client->addr, client->adapter->name);
	return ret;
}

static int dummy_remove(struct i2c_client *client)
{
	struct dummy_priv *priv = i2c_get_clientdata(client);

	device_remove_file(&client->dev, &dev_attr_otp_id_dummy);
	v4l2_async_unregister_subdev(&priv->sd);
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->sd);

	return 0;
}

static struct i2c_driver dummy_i2c_driver = {
	.driver	= {
		.name		= "dummy-camera",
		.of_match_table	= dummy_of_ids,
	},
	.probe		= dummy_probe,
	.remove		= dummy_remove,
	.id_table	= dummy_id,
};
module_i2c_driver(dummy_i2c_driver);

MODULE_DESCRIPTION("Dummy camera glue driver");
MODULE_AUTHOR("Vladimir Barinov");
MODULE_LICENSE("GPL");
