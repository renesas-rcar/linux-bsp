// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Sony CXD4960 cameras.
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
#include <asm/io.h>

/* Page Size */
#define MSIOF_PAGE_SIZE	0x1000
#define PWM_PAGE_SIZE	0x4000
#define GPIO_PAGE_SIZE	0x1000

/* DES_REFCLK_1R8V */
#define MSIOF3_BASE			0xE6C10000
#define MSIOF_REG_SITMDR1	0x0000
#define MSIOF_REG_SITSCR	0x0020
#define MSIOF_REG_SICTR		0x0028
#define MSIOF_BRPS			0x0000
#define MSIOF_BRDV			0x0000
#define MSIOF_TRMD			0x80000000
#define MSIOF_TSCKIZ		0x00000000
#define MSIOF_TSCKE			0x00008000

/* FSYNC_1R8V */
#define PWM_BASE		0xE6E30000
#define PWM_REG_PWMCR	0x3000
#define PWM_REG_PWMCNT	0x3004
#define PWM_CC0			0x00060000
#define PWM_CCMD		0x00008000
#define PWM_SYNC		0x00000800
#define PWM_SS0			0x00000010
#define PWM_CYC0		0x032E0000
#define PWM_PH0			0x00000197

/* DES_CE_1R8V */
#define GPIO01_BASE		0xE6050000
#define GPIO1_REG_PMMR	0x0800
#define GPIO1_REG_POC	0x08A0
#define GPIO1_REG_PUEN	0x08C0
#define GPIO1_REG_OUTDT	0x0988
#define GPIO1_POC_CE	0x00000001
#define GPIO1_PUEN_CE	0x00000001
#define GPIO1_OUTDT_CE	0x00000001

#define CXD4960_REG_VALUE_08BIT	1
#define CXD4960_REG_VALUE_16BIT	2

#define CXD4960_REG_SERDES_LINK		0x01
#define CXD4960_REG_REMOTE_COMPLETE	0xB5

#define CXD4960_VALUE_SERDES_LINK		1
#define CXD4960_VALUE_REMOTE_COMPLETE	1

#define CXD4960_REG_VIDEO_OUTPUT_ENABLE		0x76
#define CXD4960_VALUE_VIDEO_OUTPUT_ENABLE	1
#define CXD4960_VALUE_VIDEO_OUTPUT_DISABLE	0

#define CXD4960_REG_ERROR_CLEAR			0x1F
#define CXD4960_VALUE_ERROR_NOTCLEAR	0x00
#define CXD4960_VALUE_ERROR_CLEAR		0x01

#define CXD4960_REG_SSCG_CONTROL		0x80

struct cxd4960_reg {
	u16 address;
	u8 val;
};

static const struct cxd4960_reg init_des_set_regs_step1[] = {
	{0x29, 0x06},
	{0x20, 0x01},
};/* init_des_set_regs_step1 */

static const struct cxd4960_reg init_des_set_regs_step2[] = {
	{0xB0, 0x42},
	{0xB1, 0xC0},
	{0xB2, 0x41},
	{0xB4, 0x41},
};/* init_des_set_regs_step2 */

static const struct cxd4960_reg init_des_set_regs_step3[] = {
	{0xB4, 0x00},
	{0xC0, 0x41},
};/* init_des_set_regs_step3 */

static const struct cxd4960_reg init_des_set_regs_step4[] = {
	{0xC2, 0x02},
	{0xA1, 0x03},
	{0xA0, 0x01},
	{0x76, 0x00},
	{0xFF, 0x01},
	{0xFE, 0x73},
	{0xC0, 0x60},
	{0xC1, 0x65},
	{0xC6, 0x29},
	{0xFE, 0x00},
	{0xFF, 0x00},
	{0x90, 0x20},
};/* init_des_set_regs_step4 */

enum cxd4960_pad {
	CXD4960_PAD_SINK,
	CXD4960_PAD_SOURCE,
	CXD4960_PAD_MAX,
};

struct cxd4960 {
	struct v4l2_subdev sd;
	struct media_pad pad[CXD4960_PAD_MAX];

	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *remote;
	unsigned int remote_pad;

	struct v4l2_mbus_framefmt fmt;

	struct gpio_desc *reset_gpio;

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

static inline struct cxd4960 *to_cxd4960(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct cxd4960, sd);
}

static inline struct cxd4960 *notifier_to_cxd4960(struct v4l2_async_notifier *n)
{
	return container_of(n, struct cxd4960, notifier);
}

/* Read registers up to 2 at a time */
static int cxd4960_read_reg(struct cxd4960 *cxd4960, u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
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
static int cxd4960_write_reg(struct cxd4960 *cxd4960, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
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
static int cxd4960_write_regs(struct cxd4960 *cxd4960,
			     const struct cxd4960_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = cxd4960_write_reg(cxd4960, regs[i].address, 1, regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

static int cxd4960_strobe_led_control(struct cxd4960 *cxd4960, u32 input, u32 output, u32 config)
{
	int ret;

	ret = v4l2_subdev_call(cxd4960->remote, video, s_routing, input, output, config);

	return ret;
}

static int cxd4960_sscg_control(struct cxd4960 *cxd4960, u32 control)
{
	int ret;

	/* Desrializa SSCG ON/OFF Control */
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_DISABLE);
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_SSCG_CONTROL, CXD4960_REG_VALUE_08BIT, control);
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_ENABLE);
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_ERROR_CLEAR, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_ERROR_CLEAR);
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_ERROR_CLEAR, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_ERROR_NOTCLEAR);

	return ret;
}

static int cxd4960_s_routing(struct v4l2_subdev *sd, u32 input, u32 output, u32 config)
{
	struct cxd4960 *cxd4960 = to_cxd4960(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);
	int ret = 0;

	mutex_lock(&cxd4960->mutex);
	if (!cxd4960->streaming) {
		mutex_unlock(&cxd4960->mutex);
		return 0;
	}

	switch (config) {
	case 1:
		/* Strobe LED Control */
		ret = cxd4960_strobe_led_control(cxd4960, input, output, config);
		break;
	case 2:
		/* SSCG ON/OFF Control */
		ret = cxd4960_sscg_control(cxd4960, input);
		break;
	default:
		dev_err(&client->dev, "Not supported command[%d]\n", config);
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&cxd4960->mutex);

	return ret;
}

static int cxd4960_start_streaming(struct cxd4960 *cxd4960)
{
	u32 val;
	int ret;

	void *mapped;

	/* FSYNC_1R8V */
	/* set parameter (addr should be aligned by PWM_PAGE_SIZE) */
	mapped = ioremap(PWM_BASE, PWM_PAGE_SIZE);

	iowrite32(PWM_CC0 | PWM_CCMD | PWM_CCMD | PWM_SYNC | PWM_SS0, mapped + PWM_REG_PWMCR);
	iowrite32(PWM_CYC0 | PWM_PH0, mapped + PWM_REG_PWMCNT);

	iounmap(mapped);

	/* Deserializer Initialize */
	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step1, ARRAY_SIZE(init_des_set_regs_step1));
	if (ret) return ret;

	while(1){
		ret = cxd4960_read_reg(cxd4960, CXD4960_REG_SERDES_LINK, CXD4960_REG_VALUE_08BIT, &val);
		if (val == CXD4960_VALUE_SERDES_LINK) break;
		if (ret) return ret;
	}

	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step2, ARRAY_SIZE(init_des_set_regs_step2));
	if (ret) return ret;

	while(1){
		ret = cxd4960_read_reg(cxd4960, CXD4960_REG_REMOTE_COMPLETE, CXD4960_REG_VALUE_08BIT, &val);
		if (val == CXD4960_VALUE_REMOTE_COMPLETE) break;
		if (ret) return ret;
	}

	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step3, ARRAY_SIZE(init_des_set_regs_step3));
	if (ret) return ret;

	usleep_range(30, 40);

	ret = cxd4960_write_regs(cxd4960, init_des_set_regs_step4, ARRAY_SIZE(init_des_set_regs_step4));
	if (ret) return ret;

	ret = v4l2_subdev_call(cxd4960->remote, video, s_stream, 1);
	if (ret) return ret;

	/* Desirializa Video Output Enable */
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_VIDEO_OUTPUT_ENABLE, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_VIDEO_OUTPUT_ENABLE);
	if (ret) return ret;

	/* Desrializa Error Status Clear */
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_ERROR_CLEAR, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_ERROR_CLEAR);
	if (ret) return ret;
	ret = cxd4960_write_reg(cxd4960, CXD4960_REG_ERROR_CLEAR, CXD4960_REG_VALUE_08BIT, CXD4960_VALUE_ERROR_NOTCLEAR);

	return ret;
}

static void cxd4960_stop_streaming(struct cxd4960 *cxd4960)
{
	struct i2c_client *client = v4l2_get_subdevdata(&cxd4960->sd);

	__v4l2_ctrl_grab(cxd4960->vflip, false);
	__v4l2_ctrl_grab(cxd4960->hflip, false);

	pm_runtime_put(&client->dev);
}

static int cxd4960_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct cxd4960 *cxd4960 = to_cxd4960(sd);
	int ret = 0;

	mutex_lock(&cxd4960->mutex);
	if (cxd4960->streaming == enable) {
		mutex_unlock(&cxd4960->mutex);
		return 0;
	}

	if (enable) {
		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = cxd4960_start_streaming(cxd4960);
		if (ret)
			goto err_unlock;
	} else {
		cxd4960_stop_streaming(cxd4960);
	}

	cxd4960->streaming = enable;

	mutex_unlock(&cxd4960->mutex);

	return ret;

err_unlock:
	mutex_unlock(&cxd4960->mutex);

	return ret;
}

/* Power/clock management functions */
static void cxd4960_control_ce(u32 io)
{
	u32 gpioreg;
	void *mapped;

	mapped = ioremap(GPIO01_BASE, GPIO_PAGE_SIZE);

	gpioreg = ioread32(mapped + GPIO1_REG_OUTDT);
	if (io)
		gpioreg |= GPIO1_OUTDT_CE;
	else
		gpioreg &= ~GPIO1_OUTDT_CE;
	iowrite32(gpioreg, mapped + GPIO1_REG_OUTDT);

	iounmap(mapped);

	return;
}

static int cxd4960_power_on(struct device *dev)
{
//	struct i2c_client *client = to_i2c_client(dev);
//	struct v4l2_subdev *sd = i2c_get_clientdata(client);
//	struct cxd4960 *cxd4960 = to_cxd4960(sd);

//	gpiod_set_value_cansleep(cxd4960->reset_gpio, 1);
	cxd4960_control_ce(1);
	msleep(10);

	return 0;
}

static int cxd4960_power_off(struct device *dev)
{
//	struct i2c_client *client = to_i2c_client(dev);
//	struct v4l2_subdev *sd = i2c_get_clientdata(client);
//	struct cxd4960 *cxd4960 = to_cxd4960(sd);

//	gpiod_set_value_cansleep(cxd4960->reset_gpio, 0);
	cxd4960_control_ce(0);

	return 0;
}

static int __maybe_unused cxd4960_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4960 *cxd4960 = to_cxd4960(sd);

	if (cxd4960->streaming)
		cxd4960_stop_streaming(cxd4960);

	return 0;
}

static int __maybe_unused cxd4960_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4960 *cxd4960 = to_cxd4960(sd);
	int ret;

	if (cxd4960->streaming) {
		ret = cxd4960_start_streaming(cxd4960);
		if (ret)
			goto error;
	}

	return 0;

error:
	cxd4960_stop_streaming(cxd4960);
	cxd4960->streaming = false;

	return ret;
}

static const struct v4l2_subdev_core_ops cxd4960_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops cxd4960_video_ops = {
	.s_stream = cxd4960_set_stream,
	.s_routing = cxd4960_s_routing,
};

static const struct v4l2_subdev_ops cxd4960_subdev_ops = {
	.core = &cxd4960_core_ops,
	.video = &cxd4960_video_ops,
};

static void cxd4960_free_controls(struct cxd4960 *cxd4960)
{
	mutex_destroy(&cxd4960->mutex);
}

static int cxd4960_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret = -EINVAL;

	for (endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
		endpoint != NULL;
		endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), endpoint)) {
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
			/* check next endpoint */
			continue;
		} else {
			ret = 0;
		}
	}

	if (ret)
		dev_err(dev, "only 2 data lanes are currently supported\n");

error_out:
	v4l2_fwnode_endpoint_free(&ep_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static int cxd4960_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_subdev *asd)
{
	struct cxd4960 *priv = notifier_to_cxd4960(notifier);
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
				     &priv->sd.entity, CXD4960_PAD_SINK,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static void cxd4960_notify_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct cxd4960 *priv = notifier_to_cxd4960(notifier);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);

	priv->remote = NULL;

	dev_dbg(&client->dev, "Unbind %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations cxd4960_notify_ops = {
	.bound = cxd4960_notify_bound,
	.unbind = cxd4960_notify_unbind,
};

static int cxd4960_parse(struct cxd4960 *priv)
{
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *fwnode;
	struct device_node *ep;
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = 0 };
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);

	int ret;

	ep = of_graph_get_endpoint_by_regs(client->dev.of_node, 0, -1);
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
	priv->notifier.ops = &cxd4960_notify_ops;

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

static int cxd4960_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct cxd4960 *cxd4960;
	int ret;
	u32 gpioreg;
	struct v4l2_subdev *sd;

	void *mapped;

	cxd4960 = devm_kzalloc(&client->dev, sizeof(*cxd4960), GFP_KERNEL);
	if (!cxd4960)
		return -ENOMEM;

	sd = &cxd4960->sd;

	cxd4960->sd.owner = THIS_MODULE;
	cxd4960->sd.dev = dev;
	v4l2_i2c_subdev_init(&cxd4960->sd, client, &cxd4960_subdev_ops);

	/* Check the hardware configuration in device tree */
	if (cxd4960_check_hwcfg(dev))
		return -EINVAL;

	ret = cxd4960_parse(cxd4960);
	if (ret)
		return ret;

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	cxd4960->pad[CXD4960_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	cxd4960->pad[CXD4960_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_ATV_DECODER;
	ret = media_entity_pads_init(&sd->entity, CXD4960_PAD_MAX, cxd4960->pad);
	if (ret)
		return ret;

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0) {
		dev_err(dev, "Failed to register subdevice.\n");
		return ret;
	}

	/* GPIO setting DES_CE */
	/* set parameter (addr should be aligned by GPIO_PAGE_SIZE) */
	mapped = ioremap(GPIO01_BASE, GPIO_PAGE_SIZE);

	gpioreg = ioread32(mapped + GPIO1_REG_POC);
	gpioreg &= ~GPIO1_POC_CE;
	iowrite32(~gpioreg, mapped + GPIO1_REG_PMMR);
	iowrite32(gpioreg, mapped + GPIO1_REG_POC);
	gpioreg = ioread32(mapped + GPIO1_REG_PUEN);
	gpioreg &= ~GPIO1_PUEN_CE;
	iowrite32(~gpioreg, mapped + GPIO1_REG_PMMR);
	iowrite32(gpioreg, mapped + GPIO1_REG_PUEN);

	iounmap(mapped);

	/* Des電源起動の判定はSystemRAMのTPS78412Vout_状態を確認。 */





	/* DSM電源起動の判定はSystemRAMのDSM_POWER_ENABLEを確認。 */





	msleep(25);

	/* Request optional enable pin */
#if 0
	cxd4960->reset_gpio = devm_gpiod_get_optional(dev, NULL,
						     GPIOD_OUT_LOW);
	if (!cxd4960->reset_gpio)
		return -ENOENT;
#endif

	ret = cxd4960_power_on(dev);
	if (ret)
		return ret;

	/* FCMイメージセンサ初期化(Streamingモードへ遷移)完了チェック */





	/* REFCLK */
	/* set by msiof driver */
#if 0
	/* set parameter (addr should be aligned by MSIOF_PAGE_SIZE) */

	mapped = ioremap(MSIOF3_BASE, MSIOF_PAGE_SIZE);

	iowrite32(MSIOF_TRMD, mapped + MSIOF_REG_SITMDR1);
	iowrite16(MSIOF_BRPS | MSIOF_BRDV, mapped + MSIOF_REG_SITSCR);
	iowrite32(MSIOF_TSCKIZ | MSIOF_TSCKE, mapped + MSIOF_REG_SICTR);

	iounmap(mapped);
#endif

	msleep(1);

	dev_info(dev, "probed.\n");

	return 0;

};

static int cxd4960_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cxd4960 *cxd4960 = to_cxd4960(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	cxd4960_free_controls(cxd4960);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		cxd4960_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct of_device_id cxd4960_dt_ids[] = {
	{ .compatible = "sony,cxd4960" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cxd4960_dt_ids);

static const struct dev_pm_ops cxd4960_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cxd4960_suspend, cxd4960_resume)
	SET_RUNTIME_PM_OPS(cxd4960_power_off, cxd4960_power_on, NULL)
};

static struct i2c_driver cxd4960_i2c_driver = {
	.driver = {
		.name = "cxd4960",
		.of_match_table	= cxd4960_dt_ids,
		.pm = &cxd4960_pm_ops,
	},
	.probe_new = cxd4960_probe,
	.remove = cxd4960_remove,
};

module_i2c_driver(cxd4960_i2c_driver);

MODULE_AUTHOR("Dave Stevenson <dave.stevenson@raspberrypi.com");
MODULE_DESCRIPTION("Sony CXD4960 sensor driver");
MODULE_LICENSE("GPL v2");
