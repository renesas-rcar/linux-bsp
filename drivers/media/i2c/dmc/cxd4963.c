// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Sony CXD4963 cameras.
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

#define CXD4963_REG_VALUE_08BIT	1
#define CXD4963_REG_VALUE_16BIT	2
#define CXD4963_REG_VALUE_08BIT	1
#define CXD4963_REG_VALUE_16BIT	2

#define CXD4963_REG_VIDEO_SETUP 0x53
#define CXD4963_VALUE_INPUT_ENABLE 0x01

#define CXD4963_REG_ERROR_CLEAR			0x1F
#define CXD4963_VALUE_ERROR_NOTCLEAR	0x00
#define CXD4963_VALUE_ERROR_CLEAR		0x01


struct cxd4963_reg {
	u16 address;
	u8 val;
};

static const struct cxd4963_reg init_dmc_ser_set_regs[] = {
	{0x50, 0x08},
	{0x53, 0x00},
	{0xA1, 0x03},
	{0xA3, 0x06},
	{0xCC, 0x01},
	{0xCF, 0x50},
	{0xD0, 0x06},
	{0xD1, 0x7A},
	{0xD9, 0x02},
	{0xA0, 0x01},
	{0x69, 0x42},
	{0x6B, 0x00},
	{0x6C, 0x01},
	{0xBA, 0x01},
	{0xBB, 0x1E},
	{0xBC, 0x01},
};/* init_dmc_ser_set_regs */

/* regulator supplies */
static const char * const cxd4963_supply_name[] = {
	/* Supplies can be enabled in any order */
	"VANA",  /* Analog (2.8V) supply */
	"VDIG",  /* Digital Core (1.8V) supply */
	"VDDL",  /* IF (1.2V) supply */
};

#define CXD4963_NUM_SUPPLIES ARRAY_SIZE(cxd4963_supply_name)

struct cxd4963 {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *remote;
	unsigned int remote_pad;

	struct v4l2_mbus_framefmt fmt;

	struct clk *xclk; /* system clock to CXD4963 */
	u32 xclk_freq;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[CXD4963_NUM_SUPPLIES];

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

static inline struct cxd4963 *to_cxd4963(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct cxd4963, sd);
}

static inline struct cxd4963 *notifier_to_cxd4963(struct v4l2_async_notifier *n)
{
	return container_of(n, struct cxd4963, notifier);
}

/* Read registers up to 2 at a time */
#if 0
static int cxd4963_read_reg(struct cxd4963 *cxd4963, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4963->sd);
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
#endif

/* Write registers up to 2 at a time */
static int cxd4963_write_reg(struct cxd4963 *cxd4963, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4963->sd);
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
static int cxd4963_write_regs(struct cxd4963 *cxd4963,
			     const struct cxd4963_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4963->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = cxd4963_write_reg(cxd4963, regs[i].address, 1, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

static int cxd4963_strobe_led_control(struct cxd4963 *cxd4963, u32 input, u32 output, u32 config)
{
	int ret;

	ret = v4l2_subdev_call(cxd4963->remote, video, s_routing, input, output, config);

	return ret;
}

static int cxd4963_s_routing(struct v4l2_subdev *sd, u32 input, u32 output, u32 config)
{
	struct cxd4963 *cxd4963 = to_cxd4963(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4963->sd);
	int ret = 0;

	mutex_lock(&cxd4963->mutex);
	if (!cxd4963->streaming) {
		mutex_unlock(&cxd4963->mutex);
		return 0;
	}

	switch (config) {
	case 1:
		/* Strobe LED Control */
		ret = cxd4963_strobe_led_control(cxd4963, input, output, config);
		break;
	default:
		dev_err(&client->dev, "Not supported command[%d]\n", config);
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&cxd4963->mutex);

	return ret;
}

static int cxd4963_start_streaming(struct cxd4963 *cxd4963)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4963->sd);
	int ret;

	ret = pm_runtime_get_sync(&client->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&client->dev);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(cxd4963->sd.ctrl_handler);
	if (ret)
		goto err_rpm_put;

	/* Serializer Initialize */
	ret = cxd4963_write_regs(cxd4963, init_dmc_ser_set_regs, ARRAY_SIZE(init_dmc_ser_set_regs));

	msleep(13);

	v4l2_subdev_call(cxd4963->remote, video, s_stream, 1);

	/* Sirializa Video Output Enable */
	ret = cxd4963_write_reg(cxd4963, CXD4963_REG_VIDEO_SETUP, CXD4963_REG_VALUE_08BIT, CXD4963_VALUE_INPUT_ENABLE);

	/* Srializa Error Status Clear */
	ret = cxd4963_write_reg(cxd4963, CXD4963_REG_ERROR_CLEAR, CXD4963_REG_VALUE_08BIT, CXD4963_VALUE_ERROR_CLEAR);
	ret = cxd4963_write_reg(cxd4963, CXD4963_REG_ERROR_CLEAR, CXD4963_REG_VALUE_08BIT, CXD4963_VALUE_ERROR_NOTCLEAR);

	if (ret)
		goto err_rpm_put;

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(cxd4963->vflip, true);
	__v4l2_ctrl_grab(cxd4963->hflip, true);

	return 0;

err_rpm_put:
	pm_runtime_put(&client->dev);
	return ret;
}

static void cxd4963_stop_streaming(struct cxd4963 *cxd4963)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4963->sd);

	__v4l2_ctrl_grab(cxd4963->vflip, false);
	__v4l2_ctrl_grab(cxd4963->hflip, false);

	pm_runtime_put(&client->dev);
}

static int cxd4963_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct cxd4963 *cxd4963 = to_cxd4963(sd);
	int ret = 0;

	mutex_lock(&cxd4963->mutex);
	if (cxd4963->streaming == enable) {
		mutex_unlock(&cxd4963->mutex);
		return 0;
	}

	if (enable) {
		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = cxd4963_start_streaming(cxd4963);
		if (ret)
			goto err_unlock;
	} else {
		cxd4963_stop_streaming(cxd4963);
	}

	cxd4963->streaming = enable;

	mutex_unlock(&cxd4963->mutex);

	return ret;

err_unlock:
	mutex_unlock(&cxd4963->mutex);

	return ret;
}

/* Power/clock management functions */
static int cxd4963_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4963 *cxd4963 = to_cxd4963(sd);
	int ret;

	ret = regulator_bulk_enable(CXD4963_NUM_SUPPLIES,
				    cxd4963->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(cxd4963->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	return 0;

reg_off:
	regulator_bulk_disable(CXD4963_NUM_SUPPLIES, cxd4963->supplies);

	return ret;
}

static int cxd4963_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4963 *cxd4963 = to_cxd4963(sd);

	gpiod_set_value_cansleep(cxd4963->reset_gpio, 0);
	regulator_bulk_disable(CXD4963_NUM_SUPPLIES, cxd4963->supplies);
	clk_disable_unprepare(cxd4963->xclk);

	return 0;
}

static int __maybe_unused cxd4963_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4963 *cxd4963 = to_cxd4963(sd);

	if (cxd4963->streaming)
		cxd4963_stop_streaming(cxd4963);

	return 0;
}

static int __maybe_unused cxd4963_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4963 *cxd4963 = to_cxd4963(sd);
	int ret;

	if (cxd4963->streaming) {
		ret = cxd4963_start_streaming(cxd4963);
		if (ret)
			goto error;
	}

	return 0;

error:
	cxd4963_stop_streaming(cxd4963);
	cxd4963->streaming = false;

	return ret;
}

static const struct v4l2_subdev_core_ops cxd4963_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops cxd4963_video_ops = {
	.s_stream = cxd4963_set_stream,
	.s_routing = cxd4963_s_routing,
};

static const struct v4l2_subdev_ops cxd4963_subdev_ops = {
	.core = &cxd4963_core_ops,
	.video = &cxd4963_video_ops,
};

static void cxd4963_free_controls(struct cxd4963 *cxd4963)
{
	v4l2_ctrl_handler_free(cxd4963->sd.ctrl_handler);
	mutex_destroy(&cxd4963->mutex);
}

static int cxd4963_check_hwcfg(struct device *dev)
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

static int cxd4963_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_subdev *asd)
{
	struct cxd4963 *priv = notifier_to_cxd4963(notifier);
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

static void cxd4963_notify_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct cxd4963 *priv = notifier_to_cxd4963(notifier);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);

	priv->remote = NULL;

	dev_dbg(&client->dev, "Unbind %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations cxd4963_notify_ops = {
	.bound = cxd4963_notify_bound,
	.unbind = cxd4963_notify_unbind,
};

static int cxd4963_parse(struct cxd4963 *priv)
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
	priv->notifier.ops = &cxd4963_notify_ops;

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

static int cxd4963_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct cxd4963 *cxd4963;
	int ret;

	cxd4963 = devm_kzalloc(&client->dev, sizeof(*cxd4963), GFP_KERNEL);
	if (!cxd4963)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&cxd4963->sd, client, &cxd4963_subdev_ops);

	/* Check the hardware configuration in device tree */
	if (cxd4963_check_hwcfg(dev))
		return -EINVAL;

	ret = cxd4963_parse(cxd4963);
	if (ret)
		return ret;

	return 0;

};

static int cxd4963_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4963 *cxd4963 = to_cxd4963(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	cxd4963_free_controls(cxd4963);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		cxd4963_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct of_device_id cxd4963_dt_ids[] = {
	{ .compatible = "sony,cxd4963" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cxd4963_dt_ids);

static const struct dev_pm_ops cxd4963_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cxd4963_suspend, cxd4963_resume)
	SET_RUNTIME_PM_OPS(cxd4963_power_off, cxd4963_power_on, NULL)
};

static struct i2c_driver cxd4963_i2c_driver = {
	.driver = {
		.name = "cxd4963",
		.of_match_table	= cxd4963_dt_ids,
		.pm = &cxd4963_pm_ops,
	},
	.probe_new = cxd4963_probe,
	.remove = cxd4963_remove,
};

module_i2c_driver(cxd4963_i2c_driver);

MODULE_AUTHOR("Dave Stevenson <dave.stevenson@raspberrypi.com");
MODULE_DESCRIPTION("Sony CXD4963 sensor driver");
MODULE_LICENSE("GPL v2");
