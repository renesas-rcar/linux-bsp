// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 * Driver for Renesas R-Car ISP Channel Selector
 *
 * The ISP hardware is capable of more than just channel selection, features
 * such as demosaicing, white balance control and color space conversion are
 * also possible. These more advanced features are not supported by the driver
 * due to lack of documentation.
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-subdev.h>

#define ISPFIFOCTL						0x0004
#define ISPFIFOCTL_FIFO_PUSH			BIT(2)

#define ISPINPUTSEL0_REG				0x0008
#define ISPINPUTSEL0_SEL_CSI0				BIT(31)

#define ISPSTART_REG					0x0014
#define ISPSTART_START					0xffff
#define ISPSTART_STOP					0x0000

#define ISPPROCMODE_DT_REG(n)				(0x1100 + (0x4 * (n)))
#define ISPPROCMODE_DT_PROC_MODE_VC3(pm)		(((pm) & 0x3f) << 24)
#define ISPPROCMODE_DT_PROC_MODE_VC2(pm)		(((pm) & 0x3f) << 16)
#define ISPPROCMODE_DT_PROC_MODE_VC1(pm)		(((pm) & 0x3f) << 8)
#define ISPPROCMODE_DT_PROC_MODE_VC0(pm)		((pm) & 0x3f)

#define ISPCS_FILTER_ID_CH_REG(n)			(0x3000 + (0x0100 * (n)))

#define ISPCS_DT_CODE03_CH_REG(n)			(0x3008 + (0x100 * (n)))
#define ISPCS_DT_CODE03_EN3				BIT(31)
#define ISPCS_DT_CODE03_DT3(dt)				(((dt) & 0x3f) << 24)
#define ISPCS_DT_CODE03_EN2				BIT(23)
#define ISPCS_DT_CODE03_DT2(dt)				(((dt) & 0x3f) << 16)
#define ISPCS_DT_CODE03_EN1				BIT(15)
#define ISPCS_DT_CODE03_DT1(dt)				(((dt) & 0x3f) << 8)
#define ISPCS_DT_CODE03_EN0				BIT(7)
#define ISPCS_DT_CODE03_DT0(dt)				((dt) & 0x3f)

#define ISPCS_FILTER_VC_EN_CH(n)			(0x3014 + (0x100 * n))

#define ISPCS_LUT_FILTER_CTRL_CH(n)			(0x3040+(0x100 * n))
#define CPLX								BIT(31)
#define LINE_FILTER_LUT_LENGTH_MINUS1		GENMASK(22,16)
#define FRAME_FILTER_LUT_LENGTH_MINUS1		GENMASK(30,24)
#define ENABLE_FRAME_FILTER					BIT(13)
#define ENABLE_LINE_FILTER					BIT(12)
#define PIXEL_FILTER_LUT_LENGTH_MINUS1		GENMASK(8,0)

#define MAX_NUM_PAD 25

enum rcar_soc_type {
        R8A779A0,
        R8A779G0,
        R8A78000,
};

struct rcar_isp_format {
	u32 code;
	unsigned int datatype;
	unsigned int procmode;
};

static const struct rcar_isp_format rcar_isp_formats[] = {
	{
		.code = MEDIA_BUS_FMT_RGB888_1X24,
		.datatype = MIPI_CSI2_DT_RGB888,
		.procmode = 0x15
	}, {
		.code = MEDIA_BUS_FMT_Y10_1X10,
		.datatype = MIPI_CSI2_DT_RAW10,
		.procmode = 0x10,
	}, {
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.datatype = MIPI_CSI2_DT_YUV422_8B,
		.procmode = 0x0c,
	}, {
		.code = MEDIA_BUS_FMT_YUYV8_1X16,
		.datatype = MIPI_CSI2_DT_YUV422_8B,
		.procmode = 0x0c,
	}, {
		.code = MEDIA_BUS_FMT_UYVY8_2X8,
		.datatype = MIPI_CSI2_DT_YUV422_8B,
		.procmode = 0x0c,
	}, {
		.code = MEDIA_BUS_FMT_YUYV10_2X10,
		.datatype = MIPI_CSI2_DT_YUV422_8B,
		.procmode = 0x0c,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.procmode = 0x00,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.procmode = 0x00,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.procmode = 0x00,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.datatype = MIPI_CSI2_DT_RAW8,
		.procmode = 0x00,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.datatype = MIPI_CSI2_DT_RAW10,
		.procmode = 0x01,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.datatype = MIPI_CSI2_DT_RAW10,
		.procmode = 0x01,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.datatype = MIPI_CSI2_DT_RAW10,
		.procmode = 0x01,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.datatype = MIPI_CSI2_DT_RAW10,
		.procmode = 0x01,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.datatype = MIPI_CSI2_DT_RAW12,
		.procmode = 0x02,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.datatype = MIPI_CSI2_DT_RAW12,
		.procmode = 0x02,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.datatype = MIPI_CSI2_DT_RAW12,
		.procmode = 0x02,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.datatype = MIPI_CSI2_DT_RAW12,
		.procmode = 0x02,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR14_1X14,
		.datatype = MIPI_CSI2_DT_RAW14,
		.procmode = 0x03,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG14_1X14,
		.datatype = MIPI_CSI2_DT_RAW14,
		.procmode = 0x03,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG14_1X14,
		.datatype = MIPI_CSI2_DT_RAW14,
		.procmode = 0x03,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB14_1X14,
		.datatype = MIPI_CSI2_DT_RAW14,
		.procmode = 0x03,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR16_1X16,
		.datatype = MIPI_CSI2_DT_RAW16,
		.procmode = 0x04,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG16_1X16,
		.datatype = MIPI_CSI2_DT_RAW16,
		.procmode = 0x04,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG16_1X16,
		.datatype = MIPI_CSI2_DT_RAW16,
		.procmode = 0x04,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB16_1X16,
		.datatype = MIPI_CSI2_DT_RAW16,
		.procmode = 0x04,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR20_1X20,
		.datatype = MIPI_CSI2_DT_RAW20,
		.procmode = 0x05,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG20_1X20,
		.datatype = MIPI_CSI2_DT_RAW20,
		.procmode = 0x05,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG20_1X20,
		.datatype = MIPI_CSI2_DT_RAW20,
		.procmode = 0x05,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB20_1X20,
		.datatype = MIPI_CSI2_DT_RAW20,
		.procmode = 0x05,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR24_1X24,
		.datatype = MIPI_CSI2_DT_RAW24,
		.procmode = 0x06,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG24_1X24,
		.datatype = MIPI_CSI2_DT_RAW24,
		.procmode = 0x06,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG24_1X24,
		.datatype = MIPI_CSI2_DT_RAW24,
		.procmode = 0x06,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB24_1X24,
		.datatype = MIPI_CSI2_DT_RAW24,
		.procmode = 0x06,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR28_1X28,
		.datatype = MIPI_CSI2_DT_RAW28,
		.procmode = 0x08,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG28_1X28,
		.datatype = MIPI_CSI2_DT_RAW28,
		.procmode = 0x08,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG28_1X28,
		.datatype = MIPI_CSI2_DT_RAW28,
		.procmode = 0x08,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB28_1X28,
		.datatype = MIPI_CSI2_DT_RAW28,
		.procmode = 0x08,
	},
};

static const struct rcar_isp_format *risp_code_to_fmt(unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rcar_isp_formats); i++) {
		if (rcar_isp_formats[i].code == code)
			return &rcar_isp_formats[i];
	}

	return NULL;
}

enum rcar_isp_input {
	RISP_CSI_INPUT0,
	RISP_CSI_INPUT1,
	RISP_CSI_INPUT2,
	RISP_CSI_INPUT3,
};

enum rcar_isp_pads {
	RCAR_ISP_SINK,
	RCAR_ISP_PORT0,
	RCAR_ISP_PORT1,
	RCAR_ISP_PORT2,
	RCAR_ISP_PORT3,
	RCAR_ISP_PORT4,
	RCAR_ISP_PORT5,
	RCAR_ISP_PORT6,
	RCAR_ISP_PORT7,
	RCAR_ISP_PORT8,
	RCAR_ISP_PORT9,
	RCAR_ISP_PORT10,
	RCAR_ISP_PORT11,
	RCAR_ISP_PORT12,
	RCAR_ISP_PORT13,
	RCAR_ISP_PORT14,
	RCAR_ISP_PORT15,
	RCAR_ISP_PORT16,
	RCAR_ISP_PORT17,
	RCAR_ISP_PORT18,
	RCAR_ISP_PORT19,
	RCAR_ISP_PORT20,
	RCAR_ISP_PORT21,
	RCAR_ISP_PORT22,
	RCAR_ISP_PORT23,
};

struct rcar_isp;

struct rcar_isp_info {
	int soc_id;
	void (*risp_start)(struct rcar_isp *isp, const struct rcar_isp_format *format);
	int max_csi_input;
	int num_vin_conn_bridge;
};

struct rcar_isp {
	struct device *dev;
	void __iomem *base;
	struct reset_control *rstc;

	enum rcar_isp_input csi_input;

	struct v4l2_subdev subdev;
	struct media_pad pads[MAX_NUM_PAD];

	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *remote;

	struct mutex lock; /* Protects mf and stream_count. */
	struct v4l2_mbus_framefmt mf;
	int stream_count;
	const struct rcar_isp_info *info;
};

static inline struct rcar_isp *sd_to_isp(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rcar_isp, subdev);
}

static inline struct rcar_isp *notifier_to_isp(struct v4l2_async_notifier *n)
{
	return container_of(n, struct rcar_isp, notifier);
}

static void risp_write(struct rcar_isp *isp, u32 offset, u32 value)
{
	iowrite32(value, isp->base + offset);
}

static u32 risp_read(struct rcar_isp *isp, u32 offset)
{
	return ioread32(isp->base + offset);
}

static int risp_power_on(struct rcar_isp *isp)
{
	int ret;

	ret = pm_runtime_resume_and_get(isp->dev);
	if (ret < 0)
		return ret;

#ifndef CONFIG_VIDEO_RCAR_VIN_VDK
	ret = reset_control_deassert(isp->rstc);
	if (ret < 0) {
		pm_runtime_put(isp->dev);
		return ret;
	}
#endif

	return 0;
}

static void risp_power_off(struct rcar_isp *isp)
{
#ifndef CONFIG_VIDEO_RCAR_VIN_VDK
	reset_control_assert(isp->rstc);
#endif
	pm_runtime_put(isp->dev);
}

static void risp_start_gen3(struct rcar_isp *isp, const struct rcar_isp_format *format)
{
	unsigned int vc;
	u32 sel_csi = 0;

	/* Stage 1: Pixel Reconstructor (for MIPI CSI-2 based data types) */
	risp_write(isp, ISPPROCMODE_DT_REG(format->datatype),
		   ISPPROCMODE_DT_PROC_MODE_VC3(format->procmode) |
		   ISPPROCMODE_DT_PROC_MODE_VC2(format->procmode) |
		   ISPPROCMODE_DT_PROC_MODE_VC1(format->procmode) |
		   ISPPROCMODE_DT_PROC_MODE_VC0(format->procmode));

	/* Stage 1: Pixel Reconstructor (for custom data formats) <-- Skipped */

	/* Configure Channel Selector. */
	for (vc = 0; vc < 4; vc++) {
		u8 ch = vc + 4;
		u8 dt = format->datatype;

		/* Stage 2: VC Filter */
		risp_write(isp, ISPCS_FILTER_ID_CH_REG(ch), BIT(vc));

		/* Stage 3: DT_CODE Filter */
		risp_write(isp, ISPCS_DT_CODE03_CH_REG(ch),
			   ISPCS_DT_CODE03_EN3 | ISPCS_DT_CODE03_DT3(dt) |
			   ISPCS_DT_CODE03_EN2 | ISPCS_DT_CODE03_DT2(dt) |
			   ISPCS_DT_CODE03_EN1 | ISPCS_DT_CODE03_DT1(dt) |
			   ISPCS_DT_CODE03_EN0 | ISPCS_DT_CODE03_DT0(dt));

		/* Stage 4: Line count ID <-- Skipped */

		/* Stage 5: Line count Filter <-- Skipped */

		/* Stage 6: Horizontal Clipping <-- Skipped */

		/* Stage 7: Vertical Clipping <-- Skipped */

		/* Stage 8: De-Interleaveing Filter <-- Skipped */
	}

	/* FIFO enable for CSI */
	risp_write(isp, ISPFIFOCTL,
			   risp_read(isp, ISPFIFOCTL) | ISPFIFOCTL_FIFO_PUSH);

	/* Select CSI-2 input source. */
	if (isp->csi_input == RISP_CSI_INPUT1)
		sel_csi = ISPINPUTSEL0_SEL_CSI0;

	risp_write(isp, ISPINPUTSEL0_REG,
		   risp_read(isp, ISPINPUTSEL0_REG) | sel_csi);
}

static void risp_start_gen5(struct rcar_isp *isp, const struct rcar_isp_format *format)
{
	unsigned int vc;

	/* Stage 1: Pixel Reconstructor (for MIPI CSI-2 based data types) */
	risp_write(isp, ISPPROCMODE_DT_REG(format->datatype),
		   ISPPROCMODE_DT_PROC_MODE_VC3(format->procmode) |
		   ISPPROCMODE_DT_PROC_MODE_VC2(format->procmode) |
		   ISPPROCMODE_DT_PROC_MODE_VC1(format->procmode) |
		   ISPPROCMODE_DT_PROC_MODE_VC0(format->procmode));

	/* Stage 1: Pixel Reconstructor (for custom data formats) <-- Skipped */

	/* Configure Channel Selector. */
	for (vc = 0; vc < 4; vc++) {
		u8 ch = vc + 4;
		u8 dt = format->datatype;

		/* Stage 2: VC Filter */
		risp_write(isp, ISPCS_FILTER_VC_EN_CH(ch), BIT(vc));

		/* Stage 3: DT_CODE Filter */
		risp_write(isp, ISPCS_DT_CODE03_CH_REG(ch),
			   ISPCS_DT_CODE03_EN3 | ISPCS_DT_CODE03_DT3(dt) |
			   ISPCS_DT_CODE03_EN2 | ISPCS_DT_CODE03_DT2(dt) |
			   ISPCS_DT_CODE03_EN1 | ISPCS_DT_CODE03_DT1(dt) |
			   ISPCS_DT_CODE03_EN0 | ISPCS_DT_CODE03_DT0(dt));

		/* Stage 4: LUT based Line Filter */
		risp_write(isp, ISPCS_LUT_FILTER_CTRL_CH(ch),
					risp_read(isp, ISPCS_LUT_FILTER_CTRL_CH(ch)) &
					~CPLX & ~ENABLE_LINE_FILTER &~ENABLE_FRAME_FILTER);

		/* Stage 5: Horizontal Clipping Filter <-- Skipped */

		/* Stage 6: Vertical Clipping Filter <-- Skipped */

		/* Stage 7: LUT based Pixel Filter <-- Skipped */

		/* Stage 8: LUT based Frame Filter <-- Skipped */
	}

	/* FIFO enable for CSI */
	risp_write(isp, ISPFIFOCTL,
			   risp_read(isp, ISPFIFOCTL) | ISPFIFOCTL_FIFO_PUSH);

	/* Select CSI-2 input source. <-- Skipped */
}

static int risp_start(struct rcar_isp *isp)
{
	const struct rcar_isp_format *format;
	int ret;

	format = risp_code_to_fmt(isp->mf.code);
	if (!format) {
		dev_err(isp->dev, "Unsupported bus format\n");
		return -EINVAL;
	}

	ret = risp_power_on(isp);
	if (ret) {
		dev_err(isp->dev, "Failed to power on ISP\n");
		return ret;
	}

	isp->info->risp_start(isp, format);

	/* Start ISP. */
	risp_write(isp, ISPSTART_REG, ISPSTART_START);

	ret = v4l2_subdev_call(isp->remote, video, s_stream, 1);
	if (ret)
		risp_power_off(isp);

	return ret;
}

static void risp_stop(struct rcar_isp *isp)
{
	v4l2_subdev_call(isp->remote, video, s_stream, 0);

#ifndef CONFIG_VIDEO_RCAR_VIN_VDK
	/* Stop ISP. */
	risp_write(isp, ISPSTART_REG, ISPSTART_STOP);
#endif

	risp_power_off(isp);
}

static int risp_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rcar_isp *isp = sd_to_isp(sd);
	int ret = 0;

	mutex_lock(&isp->lock);

	if (!isp->remote) {
		ret = -ENODEV;
		goto out;
	}

	if (enable && isp->stream_count == 0) {
		ret = risp_start(isp);
		if (ret)
			goto out;
	} else if (!enable && isp->stream_count == 1) {
		risp_stop(isp);
	}

	isp->stream_count += enable ? 1 : -1;
out:
	mutex_unlock(&isp->lock);

	return ret;
}

static const struct v4l2_subdev_video_ops risp_video_ops = {
	.s_stream = risp_s_stream,
};

static int risp_set_pad_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_format *format)
{
	struct rcar_isp *isp = sd_to_isp(sd);
	struct v4l2_mbus_framefmt *framefmt;

	mutex_lock(&isp->lock);

	if (!risp_code_to_fmt(format->format.code))
		format->format.code = rcar_isp_formats[0].code;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		isp->mf = format->format;
	} else {
		framefmt = v4l2_subdev_get_try_format(sd, sd_state, 0);
		*framefmt = format->format;
	}

	mutex_unlock(&isp->lock);

	return 0;
}

static int risp_get_pad_format(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_format *format)
{
	struct rcar_isp *isp = sd_to_isp(sd);

	mutex_lock(&isp->lock);

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		format->format = isp->mf;
	else
		format->format = *v4l2_subdev_get_try_format(sd, sd_state, 0);

	mutex_unlock(&isp->lock);

	return 0;
}

static const struct v4l2_subdev_pad_ops risp_pad_ops = {
	.set_fmt = risp_set_pad_format,
	.get_fmt = risp_get_pad_format,
	.link_validate = v4l2_subdev_link_validate_default,
};

static const struct v4l2_subdev_ops rcar_isp_subdev_ops = {
	.video	= &risp_video_ops,
	.pad	= &risp_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Async handling and registration of subdevices and links
 */

static int risp_notify_bound(struct v4l2_async_notifier *notifier,
			     struct v4l2_subdev *subdev,
			     struct v4l2_async_subdev *asd)
{
	struct rcar_isp *isp = notifier_to_isp(notifier);
	int pad;

	pad = media_entity_get_fwnode_pad(&subdev->entity, asd->match.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		dev_err(isp->dev, "Failed to find pad for %s\n", subdev->name);
		return pad;
	}

	isp->remote = subdev;

	dev_dbg(isp->dev, "Bound %s pad: %d\n", subdev->name, pad);

	return media_create_pad_link(&subdev->entity, pad,
				     &isp->subdev.entity, 0,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static void risp_notify_unbind(struct v4l2_async_notifier *notifier,
			       struct v4l2_subdev *subdev,
			       struct v4l2_async_subdev *asd)
{
	struct rcar_isp *isp = notifier_to_isp(notifier);

	isp->remote = NULL;

	dev_dbg(isp->dev, "Unbind %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations risp_notify_ops = {
	.bound = risp_notify_bound,
	.unbind = risp_notify_unbind,
};

static int risp_parse_dt(struct rcar_isp *isp)
{
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *fwnode;
	struct fwnode_handle *ep;
	unsigned int id;
	int ret;

	for (id = 0; id < isp->info->max_csi_input; id++) {
		ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(isp->dev),
						     0, id, 0);
		if (ep)
			break;
	}

	if (!ep) {
		dev_err(isp->dev, "Not connected to subdevice\n");
		return -EINVAL;
	}

	 /* for Gen3/4 only */
	if (id == 1)
		isp->csi_input = RISP_CSI_INPUT1;

	fwnode = fwnode_graph_get_remote_endpoint(ep);
	fwnode_handle_put(ep);

	dev_dbg(isp->dev, "Found '%pOF'\n", to_of_node(fwnode));

	v4l2_async_nf_init(&isp->notifier);
	isp->notifier.ops = &risp_notify_ops;

	asd = v4l2_async_nf_add_fwnode(&isp->notifier, fwnode,
				       struct v4l2_async_subdev);
	fwnode_handle_put(fwnode);
	if (IS_ERR(asd))
		return PTR_ERR(asd);

	ret = v4l2_async_subdev_nf_register(&isp->subdev, &isp->notifier);
	if (ret)
		v4l2_async_nf_cleanup(&isp->notifier);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver
 */

static const struct media_entity_operations risp_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int risp_probe_resources(struct rcar_isp *isp,
				struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	isp->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(isp->base))
		return PTR_ERR(isp->base);

#ifndef CONFIG_VIDEO_RCAR_VIN_VDK
	isp->rstc = devm_reset_control_get(&pdev->dev, NULL);

	return PTR_ERR_OR_ZERO(isp->rstc);
#endif
	return 0;
}

static const struct rcar_isp_info rcar_isp_info_r8a779a0 = {
	.soc_id = R8A779A0,
	.risp_start = risp_start_gen3,
	.max_csi_input = 4,
	.num_vin_conn_bridge = 8
};

static const struct rcar_isp_info rcar_isp_info_r8a779g0 = {
	.soc_id = R8A779G0,
	.risp_start = risp_start_gen3,
	.max_csi_input = 4,
	.num_vin_conn_bridge = 8
};

static const struct rcar_isp_info rcar_isp_info_r8a78000 = {
	.soc_id = R8A78000,
	.risp_start = risp_start_gen5,
	.max_csi_input = 4,
	.num_vin_conn_bridge = 24
};

static const struct of_device_id risp_of_id_table[] = {
	{ .compatible = "renesas,r8a779a0-isp", .data = &rcar_isp_info_r8a779a0 },
	{ .compatible = "renesas,r8a779g0-isp", .data = &rcar_isp_info_r8a779g0 },
	{ .compatible = "renesas,r8a78000-isp", .data = &rcar_isp_info_r8a78000 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, risp_of_id_table);

static int risp_probe(struct platform_device *pdev)
{
	struct rcar_isp *isp;
	unsigned int i;
	int ret;

	isp = devm_kzalloc(&pdev->dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->info = of_device_get_match_data(&pdev->dev);

	isp->dev = &pdev->dev;

	mutex_init(&isp->lock);

	ret = risp_probe_resources(isp, pdev);
	if (ret) {
		dev_err(isp->dev, "Failed to get resources\n");
		goto error_mutex;
	}

	platform_set_drvdata(pdev, isp);

	pm_runtime_enable(&pdev->dev);

	ret = risp_parse_dt(isp);
	if (ret)
		goto error_pm;

	isp->subdev.owner = THIS_MODULE;
	isp->subdev.dev = &pdev->dev;
	v4l2_subdev_init(&isp->subdev, &rcar_isp_subdev_ops);
	v4l2_set_subdevdata(&isp->subdev, &pdev->dev);
	snprintf(isp->subdev.name, V4L2_SUBDEV_NAME_SIZE, "%s %s",
		 KBUILD_MODNAME, dev_name(&pdev->dev));
	isp->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	isp->subdev.entity.function = MEDIA_ENT_F_VID_MUX;
	isp->subdev.entity.ops = &risp_entity_ops;

	isp->pads[RCAR_ISP_SINK].flags = MEDIA_PAD_FL_SINK;
	for (i = RCAR_ISP_PORT0; i <= isp->info->num_vin_conn_bridge; i++)
		isp->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&isp->subdev.entity, isp->info->num_vin_conn_bridge,
				     isp->pads);
	if (ret)
		goto error_notifier;

	ret = v4l2_async_register_subdev(&isp->subdev);
	if (ret < 0)
		goto error_notifier;

	dev_info(isp->dev, "Using CSI-2 input: %u\n", isp->csi_input);

	return 0;
error_notifier:
	v4l2_async_nf_unregister(&isp->notifier);
	v4l2_async_nf_cleanup(&isp->notifier);
error_pm:
	pm_runtime_disable(&pdev->dev);
error_mutex:
	mutex_destroy(&isp->lock);

	return ret;
}

static int risp_remove(struct platform_device *pdev)
{
	struct rcar_isp *isp = platform_get_drvdata(pdev);

	v4l2_async_nf_unregister(&isp->notifier);
	v4l2_async_nf_cleanup(&isp->notifier);

	v4l2_async_unregister_subdev(&isp->subdev);

	pm_runtime_disable(&pdev->dev);

	mutex_destroy(&isp->lock);

	return 0;
}

static struct platform_driver rcar_isp_driver = {
	.driver = {
		.name = "rcar-isp",
		.of_match_table = risp_of_id_table,
	},
	.probe = risp_probe,
	.remove = risp_remove,
};

module_platform_driver(rcar_isp_driver);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Renesas R-Car ISP Channel Selector driver");
MODULE_LICENSE("GPL");
