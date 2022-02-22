/*
 * The camera is mode of an dummy sensor connected to a Maxim
 * MAX9295A GMSL2 serializer.
 */
#include <linux/delay.h>
#include <linux/fwnode.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "dummy-camera.h"

#define DUMMY_CAMERA_I2C_ADDRESS	0x30

#define DUMMY_CAMERA_WIDTH		1920
#define DUMMY_CAMERA_HEIGHT		1020
#define DUMMY_CAMERA_FORMAT		MEDIA_BUS_FMT_Y10_1X10

struct dummy_camera_device {
	struct device			*dev;
	struct max9295a_device		*serializer;
	struct i2c_client		*sensor;
	struct v4l2_subdev		sd;
	struct media_pad		pad;
	struct v4l2_ctrl_handler	ctrls;
};

static inline struct dummy_camera_device
*sd_to_dummy_camera(struct v4l2_subdev *sd)
{
	return container_of(sd, struct dummy_camera_device, sd);
}

static inline struct dummy_camera_device
*i2c_to_dummy_camera(struct i2c_client *client)
{
	return sd_to_dummy_camera(i2c_get_clientdata(client));
}
#if 0
static int dummy_camera_read16(struct dummy_camera_device *dev, u16 reg)
{
	u8 buf[2] = { reg >> 8, reg & 0xff };
	int ret;

	ret = i2c_master_send(dev->sensor, buf, 2);
	if (ret == 2)
		ret = i2c_master_recv(dev->sensor, buf, 2);

	if (ret < 0) {
		dev_dbg(dev->dev, "%s: register 0x%04x read failed (%d)\n",
			__func__, reg, ret);
		return ret;
	}

	return (buf[0] << 8) | buf[1];
}
#endif
static int __dummy_camera_write(struct dummy_camera_device *dev,
				u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;

	dev_dbg(dev->dev, "%s(0x%04x, 0x%02x)\n", __func__, reg, val);

	ret = i2c_master_send(dev->sensor, buf, 3);
	return ret < 0 ? ret : 0;
}
#if 0
static int dummy_camera_write(struct dummy_camera_device *dev, u16 reg, u8 val)
{
	int ret;

	ret = __dummy_camera_write(dev, reg, val);
	if (ret < 0)
		dev_err(dev->dev, "%s: register 0x%04x write failed (%d)\n",
			__func__, reg, ret);

	return ret;
}
#endif
static int dummy_camera_set_regs(struct dummy_camera_device *dev,
			    const struct dummy_camera_reg *regs,
			    unsigned int nr_regs)
{
	unsigned int i;
	int ret;

	for (i = 0; i < nr_regs; i++) {
		ret = __dummy_camera_write(dev, regs[i].reg, regs[i].val);
		if (ret) {
			dev_err(dev->dev,
				"%s: register %u (0x%04x) write failed (%d)\n",
				__func__, i, regs[i].reg, ret);
			return ret;
		}
	}

	return 0;
}

static int dummy_camera_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct dummy_camera_device *dev = sd_to_dummy_camera(sd);

	/* Implement as needed */
	dummy_camera_set_regs(dev, NULL, 0);

	return 0;
}

static int dummy_camera_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = DUMMY_CAMERA_FORMAT;

	return 0;
}

static int dummy_camera_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	if (format->pad)
		return -EINVAL;

	mf->width		= DUMMY_CAMERA_WIDTH;
	mf->height		= DUMMY_CAMERA_HEIGHT;
	mf->code		= DUMMY_CAMERA_FORMAT;
	mf->colorspace		= V4L2_COLORSPACE_RAW;
	mf->field		= V4L2_FIELD_NONE;
	mf->ycbcr_enc		= V4L2_YCBCR_ENC_601;
	mf->quantization	= V4L2_QUANTIZATION_FULL_RANGE;
	mf->xfer_func		= V4L2_XFER_FUNC_NONE;

	return 0;
}

static struct v4l2_subdev_video_ops dummy_camera_video_ops = {
	.s_stream	= dummy_camera_s_stream,
};

static const struct v4l2_subdev_pad_ops dummy_camera_subdev_pad_ops = {
	.enum_mbus_code = dummy_camera_enum_mbus_code,
	.get_fmt	= dummy_camera_get_fmt,
	.set_fmt	= dummy_camera_get_fmt,
};

static struct v4l2_subdev_ops dummy_camera_subdev_ops = {
	.video		= &dummy_camera_video_ops,
	.pad		= &dummy_camera_subdev_pad_ops,
};

static int dummy_camera_initialize(struct dummy_camera_device *dev)
{
	/* Implement as needed */
	return 0;
}

static int dummy_camera_probe(struct i2c_client *client)
{
	struct dummy_camera_device *dev;
	struct fwnode_handle *ep;
	int ret;

	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->dev = &client->dev;

	/*
	 * Create the dummy I2C client for the sensor: the right i2c address
	 * will be overwritten later at sensor initialization time.
	 */
	dev->sensor = i2c_new_dummy_device(client->adapter, DUMMY_CAMERA_I2C_ADDRESS);
	if (!dev->sensor) {
		ret = -ENXIO;
		goto error;
	}

	/* Initialize the hardware. */
	ret = dummy_camera_initialize(dev);
	if (ret < 0)
		goto error;

	/* Initialize and register the subdevice. */
	v4l2_i2c_subdev_init(&dev->sd, client, &dummy_camera_subdev_ops);
	dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	v4l2_ctrl_handler_init(&dev->ctrls, 1);
	/*
	 * FIXME: Compute the real pixel rate. The 50 MP/s value comes from the
	 * hardcoded frequency in the BSP CSI-2 receiver driver.
	 */
	v4l2_ctrl_new_std(&dev->ctrls, NULL, V4L2_CID_PIXEL_RATE, 50000000,
			  50000000, 1, 50000000);
	dev->sd.ctrl_handler = &dev->ctrls;

	ret = dev->ctrls.error;
	if (ret)
		goto error_free_ctrls;

	dev->pad.flags = MEDIA_PAD_FL_SOURCE;
	dev->sd.entity.flags |= MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&dev->sd.entity, 1, &dev->pad);
	if (ret < 0)
		goto error_free_ctrls;

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev), NULL);
	if (!ep) {
		dev_err(&client->dev,
			"Unable to get endpoint in node %pOF\n",
			client->dev.of_node);
		ret = -ENOENT;
		goto error_free_ctrls;
	}
	dev->sd.fwnode = ep;

	ret = v4l2_async_register_subdev(&dev->sd);
	if (ret)
		goto error_put_node;

	return 0;

error_put_node:
	fwnode_handle_put(ep);
error_free_ctrls:
	v4l2_ctrl_handler_free(&dev->ctrls);
error:
	media_entity_cleanup(&dev->sd.entity);
	if (dev->sensor)
		i2c_unregister_device(dev->sensor);

	dev_err(&client->dev, "probe failed\n");

	return ret;
}

static int dummy_camera_remove(struct i2c_client *client)
{
	struct dummy_camera_device *dev = i2c_to_dummy_camera(client);

	fwnode_handle_put(dev->sd.fwnode);
	v4l2_async_unregister_subdev(&dev->sd);
	v4l2_ctrl_handler_free(&dev->ctrls);
	media_entity_cleanup(&dev->sd.entity);
	i2c_unregister_device(dev->sensor);

	return 0;
}

static void dummy_camera_shutdown(struct i2c_client *client)
{
	struct dummy_camera_device *dev = i2c_to_dummy_camera(client);

	/* make sure stream off during shutdown (reset/reboot) */
	dummy_camera_s_stream(&dev->sd, 0);
}

static const struct of_device_id dummy_camera_of_ids[] = {
	{ .compatible = "dummy,camera", },
	{ }
};
MODULE_DEVICE_TABLE(of, dummy_camera_of_ids);

static struct i2c_driver dummy_camera_i2c_driver = {
	.driver	= {
		.name	= "dummy_camera",
		.of_match_table = dummy_camera_of_ids,
	},
	.probe_new	= dummy_camera_probe,
	.remove		= dummy_camera_remove,
	.shutdown	= dummy_camera_shutdown,
};

module_i2c_driver(dummy_camera_i2c_driver);

MODULE_ALIAS("dummy_camera");
MODULE_DESCRIPTION("GMSL2 dummy camera driver");
MODULE_LICENSE("GPL");
