/*
 * Driver for OV5642 CMOS Image Sensor from Omnivision
 *
 * Copyright (C) 2011, Bastian Hecht <hechtb@gmail.com>
 *
 * Based on Sony IMX074 Camera Driver
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * Based on Omnivision OV7670 Camera Driver
 * Copyright (C) 2006-7 Jonathan Corbet <corbet@lwn.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/module.h>
#include <linux/v4l2-mediabus.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>

#include <media/v4l2-async.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define USE_PREDEF
#include "ov5642.h"

struct ov5642_datafmt {
	u32	code;
	enum v4l2_colorspace		colorspace;
};

struct ov5642 {
	struct v4l2_subdev		subdev;
	struct media_pad		pad;
	const struct ov5642_datafmt	*fmt;
	struct v4l2_rect                crop_rect;
	struct v4l2_clk			*clk;

	/* blanking information */
	int total_width;
	int total_height;

	struct gpio_desc		*resetb_gpio;
	struct gpio_desc		*pwdn_gpio;
};

static const struct ov5642_datafmt ov5642_colour_fmts[] = {
	{MEDIA_BUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
};

static struct ov5642 *to_ov5642(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov5642, subdev);
}

/* Find a data format by a pixel code in an array */
static const struct ov5642_datafmt
			*ov5642_find_datafmt(u32 code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5642_colour_fmts); i++)
		if (ov5642_colour_fmts[i].code == code)
			return ov5642_colour_fmts + i;

	return NULL;
}

static int reg_read(struct i2c_client *client, u16 reg, u8 *val)
{
	int ret;
	/* We have 16-bit i2c addresses - care for endianness */
	unsigned char data[2] = { reg >> 8, reg & 0xff };

	ret = i2c_master_send(client, data, 2);
	if (ret < 2) {
		dev_err(&client->dev, "%s: i2c read error, reg: %x\n",
			__func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	ret = i2c_master_recv(client, val, 1);
	if (ret < 1) {
		dev_err(&client->dev, "%s: i2c read error, reg: %x\n",
				__func__, reg);
		return ret < 0 ? ret : -EIO;
	}
	return 0;
}

static int reg_write(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	unsigned char data[3] = { reg >> 8, reg & 0xff, val };

	ret = i2c_master_send(client, data, 3);
	if (ret < 3) {
		dev_err(&client->dev, "%s: i2c write error, reg: %x\n",
			__func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

#if 0
static int reg_write16(struct i2c_client *client, u16 reg, u16 val16)
{
	int ret;

	ret = reg_write(client, reg, val16 >> 8);
	if (ret)
		return ret;
	return reg_write(client, reg + 1, val16 & 0x00ff);
}
#endif

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5642_get_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 val;

	if (reg->reg & ~0xffff)
		return -EINVAL;

	reg->size = 1;

	ret = reg_read(client, reg->reg, &val);
	if (!ret)
		reg->val = (__u64)val;

	return ret;
}

static int ov5642_set_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg & ~0xffff || reg->val & ~0xff)
		return -EINVAL;

	return reg_write(client, reg->reg, reg->val);
}
#endif

static int ov5642_write_array(struct i2c_client *client,
				struct regval_list *vals)
{
	while (vals->reg_num != 0xffff || vals->value != 0xff) {
		int ret = reg_write(client, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	return 0;
}

#ifndef USE_PREDEF
static int ov5642_set_resolution(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);
	int width = priv->crop_rect.width;
	int height = priv->crop_rect.height;
	int total_width = priv->total_width;
	int total_height = priv->total_height;
	int start_x = (OV5642_SENSOR_SIZE_X - width) / 2;
	int start_y = (OV5642_SENSOR_SIZE_Y - height) / 2;
	int ret;

	/*
	 * This should set the starting point for cropping.
	 * Doesn't work so far.
	 */
	ret = reg_write16(client, REG_WINDOW_START_X_HIGH, start_x);
	if (!ret)
		ret = reg_write16(client, REG_WINDOW_START_Y_HIGH, start_y);
	if (!ret) {
		priv->crop_rect.left = start_x;
		priv->crop_rect.top = start_y;
	}

	if (!ret)
		ret = reg_write16(client, REG_WINDOW_WIDTH_HIGH, width);
	if (!ret)
		ret = reg_write16(client, REG_WINDOW_HEIGHT_HIGH, height);
	if (ret)
		return ret;
	priv->crop_rect.width = width;
	priv->crop_rect.height = height;

	/* Set the output window size. Only 1:1 scale is supported so far. */
	ret = reg_write16(client, REG_OUT_WIDTH_HIGH, width);
	if (!ret)
		ret = reg_write16(client, REG_OUT_HEIGHT_HIGH, height);

	/* Total width = output size + blanking */
	if (!ret)
		ret = reg_write16(client, REG_OUT_TOTAL_WIDTH_HIGH, total_width);
	if (!ret)
		ret = reg_write16(client, REG_OUT_TOTAL_HEIGHT_HIGH, total_height);

	/* Sets the window for AWB calculations */
	if (!ret)
		ret = reg_write16(client, REG_AVG_WINDOW_END_X_HIGH, width);
	if (!ret)
		ret = reg_write16(client, REG_AVG_WINDOW_END_Y_HIGH, height);

	return ret;
}
#endif

static int ov5642_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);
	const struct ov5642_datafmt *fmt = ov5642_find_datafmt(mf->code);

	if (format->pad)
		return -EINVAL;

	mf->width = priv->crop_rect.width;
	mf->height = priv->crop_rect.height;

	if (!fmt) {
		if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			return -EINVAL;
		mf->code	= ov5642_colour_fmts[0].code;
		mf->colorspace	= ov5642_colour_fmts[0].colorspace;
	}

	mf->field	= V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		priv->fmt = fmt;
	else
		cfg->try_fmt = *mf;
	return 0;
}

static int ov5642_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);

	const struct ov5642_datafmt *fmt = priv->fmt;

	if (format->pad)
		return -EINVAL;

	mf->code	= fmt->code;
	mf->colorspace	= fmt->colorspace;
	mf->width	= priv->crop_rect.width;
	mf->height	= priv->crop_rect.height;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ov5642_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(ov5642_colour_fmts))
		return -EINVAL;

	code->code = ov5642_colour_fmts[code->index].code;
	return 0;
}

static int ov5642_set_selection(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);
	struct v4l2_rect rect = sel->r;
	int ret;

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE ||
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	v4l_bound_align_image(&rect.width, 48, OV5642_MAX_WIDTH, 1,
			      &rect.height, 32, OV5642_MAX_HEIGHT, 1, 0);

	priv->crop_rect.width	= rect.width;
	priv->crop_rect.height	= rect.height;
	priv->total_width	= rect.width + BLANKING_EXTRA_WIDTH;
	priv->total_height	= max_t(int, rect.height +
							BLANKING_EXTRA_HEIGHT,
							BLANKING_MIN_HEIGHT);
#ifdef USE_PREDEF
	ret = ov5642_write_array(client, OV5642_720P_30FPS);
#else
	ret = ov5642_write_array(client, ov5642_default_regs_init);
	if (!ret)
		ret = ov5642_set_resolution(sd);
	if (!ret)
		ret = ov5642_write_array(client, ov5642_default_regs_finalise);
#endif
	return ret;
}

static int ov5642_get_selection(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_selection *sel)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5642 *priv = to_ov5642(client);

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = OV5642_MAX_WIDTH;
		sel->r.height = OV5642_MAX_HEIGHT;
		return 0;
	case V4L2_SEL_TGT_CROP:
		sel->r = priv->crop_rect;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ov5642_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

#ifdef USE_PREDEF
	ret = ov5642_write_array(client, OV5642_720P_30FPS);
#else
	ret = ov5642_write_array(client, ov5642_default_regs_init);
	if (!ret)
		ret = ov5642_set_resolution(sd);
	if (!ret)
		ret = ov5642_write_array(client, ov5642_default_regs_finalise);
#endif
	return ret;
}

static const struct v4l2_subdev_pad_ops ov5642_subdev_pad_ops = {
	.enum_mbus_code = ov5642_enum_mbus_code,
	.get_selection	= ov5642_get_selection,
	.set_selection	= ov5642_set_selection,
	.get_fmt	= ov5642_get_fmt,
	.set_fmt	= ov5642_set_fmt,
};

static const struct v4l2_subdev_core_ops ov5642_subdev_core_ops = {
	.s_power	= ov5642_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= ov5642_get_register,
	.s_register	= ov5642_set_register,
#endif
};

static const struct v4l2_subdev_ops ov5642_subdev_ops = {
	.core	= &ov5642_subdev_core_ops,
	.pad	= &ov5642_subdev_pad_ops,
};

/* OF probe functions */
static int ov5642_hw_power(struct device *dev, int on)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov5642 *priv = to_ov5642(client);

	if (priv->pwdn_gpio)
		gpiod_direction_output(priv->pwdn_gpio, !on);

	return 0;
}

static int ov5642_hw_reset(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ov5642 *priv = to_ov5642(client);

	if (priv->resetb_gpio) {
		/* Active the resetb pin to perform a reset pulse */
		gpiod_direction_output(priv->resetb_gpio, 1);
		usleep_range(3000, 5000);
		gpiod_direction_output(priv->resetb_gpio, 0);
	}

	return 0;
}

static int ov5642_probe_dt(struct i2c_client *client,
		struct ov5642 *priv)
{
	/* Request the reset GPIO deasserted */
	priv->resetb_gpio = devm_gpiod_get_optional(&client->dev, "resetb",
			GPIOD_OUT_LOW);
	if (!priv->resetb_gpio)
		dev_err(&client->dev, "resetb gpio is not assigned!\n");
	else if (IS_ERR(priv->resetb_gpio))
		return PTR_ERR(priv->resetb_gpio);

	/* Request the power down GPIO asserted */
	priv->pwdn_gpio = devm_gpiod_get_optional(&client->dev, "pwdn",
			GPIOD_OUT_HIGH);
	if (!priv->pwdn_gpio)
		dev_err(&client->dev, "pwdn gpio is not assigned!\n");
	else if (IS_ERR(priv->pwdn_gpio))
		return PTR_ERR(priv->pwdn_gpio);

	return 0;
}

static int ov5642_video_probe(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	int ret;
	u8 id_high, id_low;
	u16 id;

	ret = ov5642_s_power(subdev, 1);
	if (ret < 0)
		return ret;

	/* Read sensor Model ID */
	ret = reg_read(client, REG_CHIP_ID_HIGH, &id_high);
	if (ret < 0)
		goto done;

	id = id_high << 8;

	ret = reg_read(client, REG_CHIP_ID_LOW, &id_low);
	if (ret < 0)
		goto done;

	id |= id_low;

	dev_info(&client->dev, "Chip ID 0x%04x detected\n", id);

	if (id != 0x5642) {
		ret = -ENODEV;
		goto done;
	}

	ret = 0;

done:
	ov5642_s_power(subdev, 0);
	return ret;
}

static int ov5642_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct ov5642 *priv;
	struct v4l2_subdev *sd;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(struct ov5642), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = ov5642_probe_dt(client, priv);
	if (ret)
		return ret;

	sd = &priv->subdev;
	v4l2_i2c_subdev_init(&priv->subdev, client, &ov5642_subdev_ops);
	priv->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	priv->fmt		= &ov5642_colour_fmts[0];

	priv->crop_rect.width	= OV5642_DEFAULT_WIDTH;
	priv->crop_rect.height	= OV5642_DEFAULT_HEIGHT;
	priv->crop_rect.left	= (OV5642_MAX_WIDTH - OV5642_DEFAULT_WIDTH) / 2;
	priv->crop_rect.top	= (OV5642_MAX_HEIGHT - OV5642_DEFAULT_HEIGHT) / 2;
	priv->total_width = OV5642_DEFAULT_WIDTH + BLANKING_EXTRA_WIDTH;
	priv->total_height = BLANKING_MIN_HEIGHT;

	priv->clk = v4l2_clk_get(&client->dev, "mclk");
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	ov5642_hw_power(&client->dev, 1);
	mdelay(100);
	ov5642_hw_reset(&client->dev);
	mdelay(100);

	ret = ov5642_video_probe(client);
	if (ret < 0) {
		v4l2_clk_put(priv->clk);
		return ret;
	}

	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &priv->pad);
	if (ret < 0)
		return ret;

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0)
		media_entity_cleanup(&sd->entity);

	return ret;
}

static int ov5642_remove(struct i2c_client *client)
{
	struct ov5642 *priv = to_ov5642(client);

	v4l2_clk_put(priv->clk);

	return 0;
}

static const struct i2c_device_id ov5642_id[] = {
	{ "ov5642", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov5642_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov5642_of_match[] = {
	{ .compatible = "ovti,ov5642" },
	{ },
};
MODULE_DEVICE_TABLE(of, ov5642_of_match);
#endif

static struct i2c_driver ov5642_i2c_driver = {
	.driver = {
		.name = "ov5642",
		.of_match_table = of_match_ptr(ov5642_of_match),
	},
	.probe		= ov5642_probe,
	.remove		= ov5642_remove,
	.id_table	= ov5642_id,
};

module_i2c_driver(ov5642_i2c_driver);

MODULE_DESCRIPTION("Omnivision OV5642 Camera driver");
MODULE_AUTHOR("Bastian Hecht <hechtb@gmail.com>");
MODULE_LICENSE("GPL v2");
