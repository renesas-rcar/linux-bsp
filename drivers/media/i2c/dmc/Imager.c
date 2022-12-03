// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Sony IMAGER cameras.
 * Copyright (C) 2019, Raspberry Pi (Trading) Ltd
 *
 * Based on Sony imx258 camera driver
 * Copyright (C) 2018 Intel Corporation
 *
 * DT / fwnode changes, and regulator / GPIO control taken from imx214 driver
 * Copyright 2018 Qtechnology A/S
 *
 * Flip handling taken from the Sony IMX319 driver.
 * Copyright (C) 2018 Intel Corporation
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/of_graph.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <asm/unaligned.h>

#define IMAGER_REG_VALUE_08BIT	1
#define IMAGER_REG_VALUE_16BIT	2
#define IMAGER_REG_VALUE_08BIT	1
#define IMAGER_REG_VALUE_16BIT	2

struct imager_reg {
	u16 address;
	u8 val;
};

static const struct imager_reg init_dmc_imeger_set_regs_step1[] = {
	{0x0103,0x01},
	{0x0100,0x00},
	{0x010c,0x02},
	{0x010b,0x01},
	{0x0300,0x01},
	{0x0302,0x32},
	{0x0303,0x00},
	{0x0304,0x03},
	{0x0305,0x02},
	{0x0306,0x01},
	{0x030d,0x5a},
	{0x030e,0x04},
	{0x3001,0x02},
	{0x3005,0x00},
	{0x3006,0x08},
	{0x3011,0x0d},
	{0x3014,0x04},
	{0x301c,0xf0},
	{0x3020,0x20},
	{0x302c,0x00},
	{0x302d,0x00},
	{0x302e,0x00},
	{0x302f,0x03},
	{0x3103,0x00},
	{0x3106,0x08},
	{0x31ff,0x01},
	{0x3506,0x00},
	{0x3507,0x00},
	{0x3620,0x67},
	{0x3633,0x78},
	{0x3662,0x65},
	{0x3664,0xb0},
	{0x3666,0x70},
	{0x3670,0x68},
	{0x367e,0x90},
	{0x3680,0x84},
	{0x3683,0x96},
	{0x36a2,0x04},
	{0x36a3,0x80},
	{0x36b0,0x00},
	{0x3700,0x35},
	{0x3704,0x39},
	{0x370a,0x50},
	{0x3712,0x00},
	{0x3713,0x02},
	{0x3778,0x00},
	{0x379b,0x01},
	{0x379c,0x10},
	{0x3800,0x00},
	{0x3801,0x90},
	{0x3802,0x00},
	{0x3803,0x96},
	{0x3804,0x05},
	{0x3805,0xbf},
	{0x3806,0x04},
	{0x3807,0x8d},
	{0x3808,0x05},
	{0x3809,0x20},
	{0x380a,0x03},
	{0x380b,0xe8},
	{0x380c,0x03},
	{0x380d,0xbe},
	{0x380e,0x0a},
	{0x380f,0xde},
	{0x3810,0x00},
	{0x3811,0x08},
	{0x3812,0x00},
	{0x3813,0x08},
	{0x3814,0x11},
	{0x3815,0x11},
	{0x3816,0x00},
	{0x3817,0x01},
	{0x3818,0x00},
	{0x3819,0x05},
	{0x3820,0x00},
	{0x3821,0x00},
	{0x382b,0x32},
	{0x382c,0x0b},
	{0x382d,0x3a},
	{0x3881,0x44},
	{0x3882,0x02},
	{0x3883,0x8c},
	{0x3885,0x07},
	{0x389d,0x03},
	{0x38a6,0x00},
	{0x38a7,0x01},
	{0x38b3,0x07},
	{0x38b1,0x03},
	{0x38e5,0x02},
	{0x38e7,0x00},
	{0x38e8,0x00},
	{0x3910,0xff},
	{0x3911,0xff},
	{0x3912,0x08},
	{0x3913,0x00},
	{0x3914,0x00},
	{0x3915,0x00},
	{0x391c,0x00},
	{0x4001,0x00},
	{0x4003,0x40},
	{0x4008,0x04},
	{0x4009,0x1b},
	{0x400c,0x04},
	{0x400d,0x1b},
	{0x4010,0xf4},
	{0x4011,0x00},
	{0x4016,0x00},
	{0x4017,0x04},
	{0x4042,0x11},
	{0x4043,0x70},
	{0x4045,0x00},
	{0x4409,0x5f},
	{0x4509,0x00},
	{0x450b,0x00},
	{0x4600,0x00},
	{0x4601,0x82},
	{0x4708,0x09},
	{0x470c,0x81},
	{0x4710,0x06},
	{0x4711,0x00},
	{0x4800,0x00},
	{0x481f,0x30},
	{0x4837,0x14},
	{0x4f00,0x00},
	{0x4f07,0x00},
	{0x4f08,0x03},
	{0x4f09,0x08},
	{0x4f0c,0x04},
	{0x4f0d,0x88},
	{0x4f10,0x00},
	{0x4f11,0x00},
	{0x4f12,0x07},
	{0x4f13,0xe2},
	{0x5000,0x1f},
	{0x5001,0x20},
	{0x5c00,0x00},
	{0x5c01,0x2c},
	{0x5c02,0x00},
	{0x5c03,0x7f},
	{0x5e00,0x00},
	{0x5e01,0x41},
	{0x3004,0x02},
	{0x3007,0x02},
	{0x3025,0x02},
	{0x3920,0xff},
	{0x3921,0x00},
	{0x3922,0x00},
	{0x3923,0x00},
	{0x3924,0x00},
	{0x3925,0x00},
	{0x3926,0x00},
	{0x3927,0x00},
	{0x3928,0xa7},
	{0x3929,0x0a},
	{0x392a,0x30},
	{0x392b,0x00},
	{0x392c,0x00},
	{0x392d,0x03},
	{0x392e,0xbe},
	{0x392f,0x0b},
	{0x3823,0x30},
	{0x3824,0x00},
	{0x3825,0x08},
	{0x3826,0x0a},
	{0x3827,0xda},
	{0x3501,0x00},
	{0x3502,0xa7},
	{0x3508,0x01},
	{0x3509,0x00},
	{0x36c0,0x30},
	{0x36c1,0x0c},
	{0x37cc,0x07},
	{0x37cd,0x03},
	{0x38f0,0x03},
	{0x38f1,0xe8},
	{0x38f2,0x03},
	{0x38f3,0xe8},
	{0x38f5,0x8c},
	{0x38fc,0x01},
	{0x37d7,0x80},
	{0x3679,0x09},
	{0x5025,0x01},
	{0x5026,0x00},
	{0x5f6b,0x0b},
	{0x4442,0x18},
	{0x4443,0x1d},
	{0x4444,0x0a},
	{0x4445,0xda},
	{0x5f60,0x80},
	{0x5f06,0xff},
	{0x3882,0x01},
	{0x37d6,0x1c},
	{0x5f10,0x01},
	{0x5f11,0x01},
	{0x5f12,0x01},
	{0x5f13,0x01},
	{0x5f14,0x01},
	{0x5f15,0x01},
	{0x5f16,0x01},
	{0x5f17,0x01},
	{0x5f18,0x20},
	{0x5f19,0x20},
	{0x5f1a,0x20},
	{0x5f1b,0x20},
	{0x5f1c,0x20},
	{0x5f1d,0x20},
	{0x5f1e,0x20},
	{0x5f1f,0x20},
	{0x3016,0xf1},
	{0x0100,0x01},
};/* init_dmc_imeger_set_regs_step1 */

static const struct imager_reg init_dmc_imeger_set_regs_step2[] = {
	{0x4814,0x6b},
	{0x3218,0x32},
	{0x3216,0x02},
	{0x3208,0x04},
	{0x0103,0x01},
	{0x0100,0x01},
	{0x010c,0x01},
	{0x010b,0x01},
	{0x0300,0x01},
	{0x0302,0x05},
	{0x030d,0x02},
	{0x3001,0x01},
	{0x3005,0x02},
	{0x3011,0x01},
	{0x3014,0x01},
	{0x301c,0x01},
	{0x3020,0x01},
	{0x302c,0x04},
	{0x3506,0x02},
	{0x3620,0x01},
	{0x3633,0x01},
	{0x3662,0x01},
	{0x3664,0x01},
	{0x3666,0x01},
	{0x3670,0x01},
	{0x367e,0x01},
	{0x3680,0x01},
	{0x3683,0x01},
	{0x36a2,0x02},
	{0x36b0,0x01},
	{0x3700,0x01},
	{0x3704,0x01},
	{0x370a,0x01},
	{0x3712,0x02},
	{0x3778,0x01},
	{0x379b,0x02},
	{0x3800,0x1a},
	{0x3820,0x02},
	{0x382b,0x03},
	{0x3881,0x01},
	{0x3883,0x01},
	{0x3885,0x01},
	{0x389d,0x01},
	{0x38a6,0x02},
	{0x38b3,0x01},
	{0x38b1,0x01},
	{0x38e5,0x01},
	{0x38e7,0x02},
	{0x3910,0x06},
	{0x391c,0x01},
	{0x4001,0x01},
	{0x4003,0x01},
	{0x4008,0x02},
	{0x400c,0x02},
	{0x4010,0x02},
	{0x4016,0x02},
	{0x4042,0x02},
	{0x4045,0x01},
	{0x4409,0x01},
	{0x4509,0x01},
	{0x450b,0x01},
	{0x4600,0x02},
	{0x4708,0x01},
	{0x470c,0x01},
	{0x4710,0x02},
	{0x4800,0x01},
	{0x481f,0x01},
	{0x4837,0x01},
	{0x4f00,0x01},
	{0x4f07,0x03},
	{0x4f0c,0x02},
	{0x4f10,0x04},
	{0x5000,0x02},
	{0x5c00,0x04},
	{0x5e00,0x02},
	{0x3004,0x01},
	{0x3007,0x01},
	{0x3025,0x01},
	{0x3920,0x10},
	{0x3823,0x05},
	{0x3501,0x02},
	{0x3508,0x02},
	{0x36c0,0x02},
	{0x37cc,0x01},
	{0x37cd,0x01},
	{0x38f0,0x04},
	{0x38f5,0x01},
	{0x38fc,0x01},
	{0x37d7,0x01},
	{0x3679,0x01},
	{0x5025,0x02},
	{0x5f6b,0x01},
	{0x4442,0x04},
	{0x5f60,0x01},
	{0x5f06,0x01},
	{0x3882,0x01},
	{0x37d6,0x01},
	{0x5f10,0x10},
	{0x3016,0x01},
	{0x4814,0x01},
	{0x3674,0x01},
	{0x4448,0x04},
	{0x350E,0x02},
	{0x3514,0x02},
	{0x4417,0x02},
	{0x5f72,0x03},
	{0x4424,0x01},
	{0x3e1d,0x01},
	{0x4606,0x01},
	{0x7000,0x10},
	{0x7016,0x04},
	{0x7013,0x01},
	{0x3003,0x01},
	{0x3208,0x14},
	{0x3674,0x11},
	{0x3016,0xf0},
};/* init_dmc_imeger_set_regs_step2 */

static const struct imager_reg strobe_led_on_set_regs[] = {
	{0x3501,0x00},
	{0x3502,0xAA},
	{0x3927,0x00},
	{0x3928,0xAA},
	{0x3929,0x09},
	{0x392A,0x53},
};/* strobe_led_on_set_regs */

static const struct imager_reg strobe_led_off_set_regs[] = {
	{0x3501,0x00},
	{0x3502,0x00},
	{0x3927,0x00},
	{0x3928,0x00},
	{0x3929,0x09},
	{0x392A,0xFD},
};/* strobe_led_off_set_regs */

#define IMAGER_REG_STROBE_CONTROL	(0x3006)
#define IMAGER_STROBE_ONOFF			(0x08)

/* regulator supplies */
static const char * const imager_supply_name[] = {
	/* Supplies can be enabled in any order */
	"VANA",  /* Analog (2.8V) supply */
	"VDIG",  /* Digital Core (1.8V) supply */
	"VDDL",  /* IF (1.2V) supply */
};

#define IMAGER_NUM_SUPPLIES ARRAY_SIZE(imager_supply_name)

struct imager {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *remote;
	unsigned int remote_pad;

	struct v4l2_mbus_framefmt fmt;

	struct clk *xclk; /* system clock to IMAGER */
	u32 xclk_freq;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMAGER_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;

	/*
	 * Mutex for serialized access:
	 * Protect sensor module set pad format and start/stop streaming safely.
	 */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;
};

static inline struct imager *to_imager(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imager, sd);
}

static inline struct imager *notifier_to_imager(struct v4l2_async_notifier *n)
{
	return container_of(n, struct imager, notifier);
}

/* Read registers up to 2 at a time */
static int imager_read_reg(struct imager *imager, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imager->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2] = { reg >> 8, reg & 0xff };
	u8 data_buf[4] = { 0, };
	int ret;

	if (len > 4)
		return -EINVAL;

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

/* Write registers up to 2 at a time */
static int imager_write_reg(struct imager *imager, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imager->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/* Write a list of registers */
static int imager_write_regs(struct imager *imager,
			     const struct imager_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imager->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = imager_write_reg(imager, regs[i].address, 1, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

static int imager_strobe_led_control(struct imager *imager, u32 enable)
{
	int ret;
	u32 val;

	if (enable) {
		/* Strobe LED ON setting */
		ret = imager_write_regs(imager, strobe_led_on_set_regs, ARRAY_SIZE(strobe_led_on_set_regs));

		/* Stroe Enable */
		ret = imager_read_reg(imager, IMAGER_REG_STROBE_CONTROL, 1, &val);
		ret = imager_write_reg(imager, IMAGER_REG_STROBE_CONTROL, 1, val | IMAGER_STROBE_ONOFF);
	} else {
		/* Strobe LED OFF setting */
		ret = imager_write_regs(imager, strobe_led_off_set_regs, ARRAY_SIZE(strobe_led_off_set_regs));

		/* Strobe disable */
		ret = imager_read_reg(imager, IMAGER_REG_STROBE_CONTROL, 1, &val);
		ret = imager_write_reg(imager, IMAGER_REG_STROBE_CONTROL, 1, val & ~IMAGER_STROBE_ONOFF);
	}

	return ret;
}

static int imager_s_routing(struct v4l2_subdev *sd, u32 input, u32 output, u32 config)
{
	struct imager *imager = to_imager(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&imager->sd);
	int ret = 0;

	mutex_lock(&imager->mutex);
	if (!imager->streaming) {
		mutex_unlock(&imager->mutex);
		return 0;
	}

	switch (config) {
	case 1:
		/* Strobe LED Control */
		ret = imager_strobe_led_control(imager, input);
		break;
	default:
		dev_err(&client->dev, "Not supported command[%d]\n", config);
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&imager->mutex);

	return ret;
}

static int imager_start_streaming(struct imager *imager)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imager->sd);
	int ret;

	ret = pm_runtime_get_sync(&client->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&client->dev);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(imager->sd.ctrl_handler);
	if (ret)
		goto err_rpm_put;

	/* Imager Initialize */
	ret = imager_write_regs(imager, init_dmc_imeger_set_regs_step1, ARRAY_SIZE(init_dmc_imeger_set_regs_step1));

	msleep(5);

	ret = imager_write_regs(imager, init_dmc_imeger_set_regs_step2, ARRAY_SIZE(init_dmc_imeger_set_regs_step2));

	msleep(66);

	if (ret)
		goto err_rpm_put;

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(imager->vflip, true);
	__v4l2_ctrl_grab(imager->hflip, true);

	return 0;

err_rpm_put:
	pm_runtime_put(&client->dev);
	return ret;
}

static void imager_stop_streaming(struct imager *imager)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imager->sd);

	__v4l2_ctrl_grab(imager->vflip, false);
	__v4l2_ctrl_grab(imager->hflip, false);

	pm_runtime_put(&client->dev);
}

static int imager_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imager *imager = to_imager(sd);
	int ret = 0;

	mutex_lock(&imager->mutex);
	if (imager->streaming == enable) {
		mutex_unlock(&imager->mutex);
		return 0;
	}

	if (enable) {
		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = imager_start_streaming(imager);
		if (ret)
			goto err_unlock;
	} else {
		imager_stop_streaming(imager);
	}

	imager->streaming = enable;

	mutex_unlock(&imager->mutex);

	return ret;

err_unlock:
	mutex_unlock(&imager->mutex);

	return ret;
}

/* Power/clock management functions */
static int imager_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imager *imager = to_imager(sd);
	int ret;

	ret = regulator_bulk_enable(IMAGER_NUM_SUPPLIES,
				    imager->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(imager->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	return 0;

reg_off:
	regulator_bulk_disable(IMAGER_NUM_SUPPLIES, imager->supplies);

	return ret;
}

static int imager_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imager *imager = to_imager(sd);

	gpiod_set_value_cansleep(imager->reset_gpio, 0);
	regulator_bulk_disable(IMAGER_NUM_SUPPLIES, imager->supplies);
	clk_disable_unprepare(imager->xclk);

	return 0;
}

static int __maybe_unused imager_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imager *imager = to_imager(sd);

	if (imager->streaming)
		imager_stop_streaming(imager);

	return 0;
}

static int __maybe_unused imager_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imager *imager = to_imager(sd);
	int ret;

	if (imager->streaming) {
		ret = imager_start_streaming(imager);
		if (ret)
			goto error;
	}

	return 0;

error:
	imager_stop_streaming(imager);
	imager->streaming = false;

	return ret;
}

static const struct v4l2_subdev_core_ops imager_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imager_video_ops = {
	.s_stream = imager_set_stream,
	.s_routing = imager_s_routing,
};

static const struct v4l2_subdev_ops imager_subdev_ops = {
	.core = &imager_core_ops,
	.video = &imager_video_ops,
};

static void imager_free_controls(struct imager *imager)
{
	v4l2_ctrl_handler_free(imager->sd.ctrl_handler);
	mutex_destroy(&imager->mutex);
}

static int imager_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret = -EINVAL;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	if (v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep_cfg)) {
		dev_err(dev, "could not parse endpoint\n");
		goto error_out;
	}

	/* Check the number of MIPI CSI2 data lanes */
	if (ep_cfg.bus.mipi_csi2.num_data_lanes != 2) {
		dev_err(dev, "only 2 data lanes are currently supported\n");
		goto error_out;
	}

	/* Check the link frequency set in device tree */
	if (!ep_cfg.nr_of_link_frequencies) {
		dev_err(dev, "link-frequency property not found in DT\n");
		goto error_out;
	}

	ret = 0;

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static int imager_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_subdev *asd)
{
	struct imager *priv = notifier_to_imager(notifier);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);

	int pad;

	pad = media_entity_get_fwnode_pad(&subdev->entity, asd->match.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		dev_err(&client->dev, "Failed to find pad for %s\n", subdev->name);
		return pad;
	}

	priv->remote = subdev;
	priv->remote_pad = pad;

	dev_dbg(&client->dev, "Bound %s pad: %d\n", subdev->name, pad);

	return media_create_pad_link(&subdev->entity, pad,
				     &priv->sd.entity, 0,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static void imager_notify_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct imager *priv = notifier_to_imager(notifier);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);

	priv->remote = NULL;

	dev_dbg(&client->dev, "Unbind %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations imager_notify_ops = {
	.bound = imager_notify_bound,
	.unbind = imager_notify_unbind,
};

static int imager_parse(struct imager *priv)
{
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *fwnode;
	struct device_node *ep;
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = 0 };
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);

	int ret;

	ep = of_graph_get_endpoint_by_regs(client->dev.of_node, 0, 0);
	if (!ep) {
		dev_dbg(&client->dev, "Not connected to subdevice\n");
		return 0;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &v4l2_ep);
	if (ret) {
		dev_err(&client->dev, "Could not parse v4l2 endpoint\n");
		of_node_put(ep);
		return -EINVAL;
	}

	fwnode = fwnode_graph_get_remote_endpoint(of_fwnode_handle(ep));
	of_node_put(ep);

	dev_dbg(&client->dev, "Found '%pOF'\n", to_of_node(fwnode));

	v4l2_async_notifier_init(&priv->notifier);
	priv->notifier.ops = &imager_notify_ops;

	asd = v4l2_async_notifier_add_fwnode_subdev(&priv->notifier, fwnode,
						    sizeof(*asd));
	fwnode_handle_put(fwnode);
	if (IS_ERR(asd))
		return PTR_ERR(asd);

	ret = v4l2_async_subdev_notifier_register(&priv->sd,
						  &priv->notifier);
	if (ret)
		v4l2_async_notifier_cleanup(&priv->notifier);

	return ret;
}

static int imager_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imager *imager;
	int ret;

	imager = devm_kzalloc(&client->dev, sizeof(*imager), GFP_KERNEL);
	if (!imager)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&imager->sd, client, &imager_subdev_ops);

	/* Check the hardware configuration in device tree */
	if (imager_check_hwcfg(dev))
		return -EINVAL;

	ret = imager_parse(imager);
	if (ret)
		return ret;

	return 0;

};

static int imager_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imager *imager = to_imager(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imager_free_controls(imager);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imager_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct of_device_id imager_dt_ids[] = {
	{ .compatible = "sony,imager" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imager_dt_ids);

static const struct dev_pm_ops imager_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(imager_suspend, imager_resume)
	SET_RUNTIME_PM_OPS(imager_power_off, imager_power_on, NULL)
};

static struct i2c_driver imager_i2c_driver = {
	.driver = {
		.name = "imager",
		.of_match_table	= imager_dt_ids,
		.pm = &imager_pm_ops,
	},
	.probe_new = imager_probe,
	.remove = imager_remove,
};

module_i2c_driver(imager_i2c_driver);

MODULE_AUTHOR("Dave Stevenson <dave.stevenson@raspberrypi.com");
MODULE_DESCRIPTION("Sony Imager sensor driver");
MODULE_LICENSE("GPL v2");
