/*
 * Driver for Renesas R-Car VIN
 *
 * Copyright (C) 2016-2017 Renesas Electronics Corp.
 * Copyright (C) 2011-2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc., <source@cogentembedded.com>
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on the soc-camera rcar_vin driver
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/pm_runtime.h>

#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-rect.h>

#include "rcar-vin.h"

#define RVIN_DEFAULT_FORMAT	V4L2_PIX_FMT_YUYV

/* -----------------------------------------------------------------------------
 * Format Conversions
 */

static const struct rvin_video_format rvin_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_NV12,
		.bpp			= 1,
	},
	{
		.fourcc			= V4L2_PIX_FMT_NV16,
		.bpp			= 1,
	},
	{
		.fourcc			= V4L2_PIX_FMT_YUYV,
		.bpp			= 2,
	},
	{
		.fourcc			= V4L2_PIX_FMT_UYVY,
		.bpp			= 2,
	},
	{
		.fourcc			= V4L2_PIX_FMT_RGB565,
		.bpp			= 2,
	},
	{
		.fourcc			= V4L2_PIX_FMT_ARGB555,
		.bpp			= 2,
	},
	{
		.fourcc			= V4L2_PIX_FMT_ABGR32,
		.bpp			= 4,
	},
	{
		.fourcc			= V4L2_PIX_FMT_XBGR32,
		.bpp			= 4,
	},
};

const struct rvin_video_format *rvin_format_from_pixel(u32 pixelformat)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rvin_formats); i++)
		if (rvin_formats[i].fourcc == pixelformat)
			return rvin_formats + i;

	return NULL;
}

static u32 rvin_format_bytesperline(struct v4l2_pix_format *pix)
{
	const struct rvin_video_format *fmt;

	fmt = rvin_format_from_pixel(pix->pixelformat);

	if (WARN_ON(!fmt))
		return -EINVAL;

	return pix->width * fmt->bpp;
}

static u32 rvin_format_sizeimage(struct v4l2_pix_format *pix)
{
	if (pix->pixelformat == V4L2_PIX_FMT_NV16)
		return pix->bytesperline * pix->height * 2;

	if (pix->pixelformat == V4L2_PIX_FMT_NV12)
		return pix->bytesperline * pix->height * 3 / 2;

	return pix->bytesperline * pix->height;
}

/* -----------------------------------------------------------------------------
 * V4L2
 */

static void rvin_reset_crop_compose(struct rvin_dev *vin)
{
	vin->crop.top = vin->crop.left = 0;
	vin->crop.width = vin->source.width;
	vin->crop.height = vin->source.height;

	vin->compose.top = vin->compose.left = 0;
	vin->compose.width = vin->format.width;
	vin->compose.height = vin->format.height;
}

static int rvin_reset_format(struct rvin_dev *vin)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_mbus_framefmt *mf = &fmt.format;
	struct rvin_graph_entity *rent;
	int ret;

	rent = vin_to_entity(vin);
	if (!rent)
		return -ENODEV;

	fmt.pad = rent->source_pad_idx;

	ret = v4l2_subdev_call(vin_to_source(vin), pad, get_fmt, NULL, &fmt);
	if (ret)
		return ret;

	vin->format.width	= mf->width;
	vin->format.height	= mf->height;
	vin->format.colorspace	= mf->colorspace;
	vin->format.field	= mf->field;

	/*
	 * If the subdevice uses ALTERNATE field mode and G_STD is
	 * implemented use the VIN HW to combine the two fields to
	 * one INTERLACED frame. The ALTERNATE field mode can still
	 * be requested in S_FMT and be respected, this is just the
	 * default which is applied at probing or when S_STD is called.
	 */
	if (vin->format.field == V4L2_FIELD_ALTERNATE &&
	    v4l2_subdev_has_op(vin_to_source(vin), video, g_std))
		vin->format.field = V4L2_FIELD_INTERLACED;

	switch (vin->format.field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_ALTERNATE:
		vin->format.height /= 2;
		break;
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
	case V4L2_FIELD_INTERLACED:
		break;
	default:
		vin->format.field = V4L2_FIELD_NONE;
		break;
	}

	rvin_reset_crop_compose(vin);

	vin->format.bytesperline = rvin_format_bytesperline(&vin->format);
	vin->format.sizeimage = rvin_format_sizeimage(&vin->format);

	return 0;
}

static int __rvin_try_format_source(struct rvin_dev *vin,
				    u32 which,
				    struct v4l2_pix_format *pix,
				    struct rvin_source_fmt *source)
{
	struct v4l2_subdev *sd;
	struct v4l2_subdev_pad_config *pad_cfg;
	struct rvin_graph_entity *rent;
	struct v4l2_subdev_format format = {
		.which = which,
	};
	enum v4l2_field field;
	int ret;

	sd = vin_to_source(vin);
	rent = vin_to_entity(vin);
	if (!rent)
		return -ENODEV;

	v4l2_fill_mbus_format(&format.format, pix, rent->code);

	pad_cfg = v4l2_subdev_alloc_pad_config(sd);
	if (pad_cfg == NULL)
		return -ENOMEM;

	format.pad = rent->source_pad_idx;

	field = pix->field;

	ret = v4l2_subdev_call(sd, pad, set_fmt, pad_cfg, &format);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		goto done;

	/* If we are part of a CSI2 group update bridge */
	if (vin_have_bridge(vin)) {
		struct v4l2_subdev *bridge = vin_to_bridge(vin);

		if (!bridge) {
			ret = -EINVAL;
			goto done;
		}

		format.pad = 0;
		ret = v4l2_subdev_call(bridge, pad, set_fmt, pad_cfg, &format);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			goto done;
	}

	v4l2_fill_pix_format(pix, &format.format);

	pix->field = field;

	source->width = pix->width;
	source->height = pix->height;

	vin_dbg(vin, "Source resolution: %ux%u\n", source->width,
		source->height);

done:
	v4l2_subdev_free_pad_config(pad_cfg);
	return ret;
}

static int __rvin_try_format(struct rvin_dev *vin,
			     u32 which,
			     struct v4l2_pix_format *pix,
			     struct rvin_source_fmt *source)
{
	const struct rvin_video_format *info;
	struct v4l2_subdev *sd = vin_to_source(vin);
	u32 rwidth, rheight, walign;
	int ret;
	v4l2_std_id std;

	/* Requested */
	rwidth = pix->width;
	rheight = pix->height;

	/* Keep current field if no specific one is asked for */
	if (pix->field == V4L2_FIELD_ANY)
		pix->field = vin->format.field;

	/*
	 * Retrieve format information and select the current format if the
	 * requested format isn't supported.
	 */
	info = rvin_format_from_pixel(pix->pixelformat);
	if (!info) {
		vin_dbg(vin, "Format %x not found, keeping %x\n",
			pix->pixelformat, vin->format.pixelformat);
		*pix = vin->format;
		pix->width = rwidth;
		pix->height = rheight;
	}

	/* Always recalculate */
	pix->bytesperline = 0;
	pix->sizeimage = 0;

	/* Limit to source capabilities */
	__rvin_try_format_source(vin, which, pix, source);

	switch (pix->field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_ALTERNATE:
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
		break;
	case V4L2_FIELD_INTERLACED:
		ret = v4l2_subdev_call(sd, video, querystd, &std);
		if (ret == -ENOIOCTLCMD)
			pix->field = V4L2_FIELD_NONE;
		else if (ret < 0)
			goto out;
		else
			pix->field = std & V4L2_STD_625_50 ?
				V4L2_FIELD_INTERLACED_TB :
				V4L2_FIELD_INTERLACED_BT;
		break;
	default:
		pix->field = V4L2_FIELD_NONE;
		break;
	}

	/* If source can't match format try if VIN can scale */
	if (source->width != rwidth || source->height != rheight)
		rvin_scale_try(vin, pix, rwidth, rheight);

	/* HW limit width to a multiple of 32 (2^5) for NV16/12 else 2 (2^1) */
	if ((pix->pixelformat == V4L2_PIX_FMT_NV12) ||
		(pix->pixelformat == V4L2_PIX_FMT_NV16))
		walign = 5;
	else if ((pix->pixelformat == V4L2_PIX_FMT_YUYV) ||
		(pix->pixelformat == V4L2_PIX_FMT_UYVY))
		walign = 1;
	else
		walign = 0;

	/* Limit to VIN capabilities */
	v4l_bound_align_image(&pix->width, 5, vin->info->max_width, walign,
			      &pix->height, 2, vin->info->max_height, 0, 0);

	pix->bytesperline = max_t(u32, pix->bytesperline,
				  rvin_format_bytesperline(pix));
	pix->sizeimage = max_t(u32, pix->sizeimage,
			       rvin_format_sizeimage(pix));

	if (vin->info->chip == RCAR_M1 &&
	    pix->pixelformat == V4L2_PIX_FMT_XBGR32) {
		vin_err(vin, "pixel format XBGR32 not supported on M1\n");
		return -EINVAL;
	}

	if ((vin->info->chip != RCAR_GEN3) &&
		(pix->pixelformat == V4L2_PIX_FMT_NV12)) {
		vin_err(vin, "pixel format NV12 is supported from GEN3\n");
		return -EINVAL;
	}

	if ((vin->info->chip != RCAR_GEN3) &&
		(pix->pixelformat == V4L2_PIX_FMT_ABGR32)) {
		vin_err(vin, "pixel format ARGB8888 is supported from GEN2\n");
		return -EINVAL;
	}

	vin_dbg(vin, "Requested %ux%u Got %ux%u bpl: %d size: %d\n",
		rwidth, rheight, pix->width, pix->height,
		pix->bytesperline, pix->sizeimage);

	return 0;
out:
	return ret;
}

static int rvin_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	struct rvin_dev *vin = video_drvdata(file);

	strlcpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	strlcpy(cap->card, "R_Car_VIN", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(vin->dev));
	return 0;
}

static int rvin_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct rvin_source_fmt source;

	return __rvin_try_format(vin, V4L2_SUBDEV_FORMAT_TRY, &f->fmt.pix,
				 &source);
}

static int __rvin_s_fmt_vid_cap(struct rvin_dev *vin, struct v4l2_format *f)
{
	struct rvin_source_fmt source;
	int ret;

	if (vb2_is_busy(&vin->queue))
		return -EBUSY;

	ret = __rvin_try_format(vin, V4L2_SUBDEV_FORMAT_ACTIVE, &f->fmt.pix,
				&source);
	if (ret)
		return ret;

	vin->source.width = source.width;
	vin->source.height = source.height;

	vin->format = f->fmt.pix;

	return 0;
}

static int rvin_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct rvin_dev *vin = video_drvdata(file);

	return __rvin_s_fmt_vid_cap(vin, f);
}

static int rvin_g_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct rvin_dev *vin = video_drvdata(file);

	f->fmt.pix = vin->format;

	return 0;
}

static int rvin_enum_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(rvin_formats))
		return -EINVAL;

	f->pixelformat = rvin_formats[f->index].fourcc;

	return 0;
}

static int rvin_g_selection(struct file *file, void *fh,
			    struct v4l2_selection *s)
{
	struct rvin_dev *vin = video_drvdata(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		s->r.left = s->r.top = 0;
		s->r.width = vin->source.width;
		s->r.height = vin->source.height;
		break;
	case V4L2_SEL_TGT_CROP:
		s->r = vin->crop;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		s->r.left = s->r.top = 0;
		s->r.width = vin->format.width;
		s->r.height = vin->format.height;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		s->r = vin->compose;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rvin_s_selection(struct file *file, void *fh,
			    struct v4l2_selection *s)
{
	struct rvin_dev *vin = video_drvdata(file);
	const struct rvin_video_format *fmt;
	struct v4l2_rect r = s->r;
	struct v4l2_rect max_rect;
	struct v4l2_rect min_rect = {
		.width = 6,
		.height = 2,
	};

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	v4l2_rect_set_min_size(&r, &min_rect);

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		/* Can't crop outside of source input */
		max_rect.top = max_rect.left = 0;
		max_rect.width = vin->source.width;
		max_rect.height = vin->source.height;
		v4l2_rect_map_inside(&r, &max_rect);

		v4l_bound_align_image(&r.width, 2, vin->source.width, 1,
				      &r.height, 4, vin->source.height, 2, 0);

		r.top  = clamp_t(s32, r.top, 0, vin->source.height - r.height);
		r.left = clamp_t(s32, r.left, 0, vin->source.width - r.width);

		vin->crop = s->r = r;

		vin_dbg(vin, "Cropped %dx%d@%d:%d of %dx%d\n",
			r.width, r.height, r.left, r.top,
			vin->source.width, vin->source.height);
		break;
	case V4L2_SEL_TGT_COMPOSE:
		/* Make sure compose rect fits inside output format */
		max_rect.top = max_rect.left = 0;
		max_rect.width = vin->format.width;
		max_rect.height = vin->format.height;
		v4l2_rect_map_inside(&r, &max_rect);

		/*
		 * Composing is done by adding a offset to the buffer address,
		 * the HW wants this address to be aligned to HW_BUFFER_MASK.
		 * Make sure the top and left values meets this requirement.
		 */
		while ((r.top * vin->format.bytesperline) & HW_BUFFER_MASK)
			r.top--;

		fmt = rvin_format_from_pixel(vin->format.pixelformat);
		while ((r.left * fmt->bpp) & HW_BUFFER_MASK)
			r.left--;

		vin->compose = s->r = r;

		vin_dbg(vin, "Compose %dx%d@%d:%d in %dx%d\n",
			r.width, r.height, r.left, r.top,
			vin->format.width, vin->format.height);
		break;
	default:
		return -EINVAL;
	}

	/* HW supports modifying configuration while running */
	rvin_crop_scale_comp(vin);

	return 0;
}

static int rvin_cropcap(struct file *file, void *priv,
			struct v4l2_cropcap *crop)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	struct rvin_source_fmt source;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	__rvin_try_format_source(vin, V4L2_SUBDEV_FORMAT_TRY,
				 &vin->format, &source);

	vin->source.width = source.width;
	vin->source.height = source.height;

	return v4l2_subdev_call(sd, video, g_pixelaspect, &crop->pixelaspect);
}

static int rvin_attach_subdevices(struct rvin_dev *vin)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_mbus_framefmt *mf = &fmt.format;
	struct v4l2_subdev *sd = vin_to_source(vin);
	struct rvin_graph_entity *rent;
	struct v4l2_format f;
	int ret;

	rent = vin_to_entity(vin);
	if (!rent)
		return -ENODEV;

	ret = v4l2_subdev_call(sd, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	if (rent != vin->last_input) {
		/* Input source have changed, reset our format */

		vin->vdev.tvnorms = 0;
		ret = v4l2_subdev_call(sd, video, g_tvnorms,
				       &vin->vdev.tvnorms);
		if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
			goto error;

		/* Free old controls (safe even if there where none) */
		v4l2_ctrl_handler_free(&vin->ctrl_handler);

		ret = v4l2_ctrl_handler_init(&vin->ctrl_handler, 16);
		if (ret < 0)
			goto error;

		/* Add new controls */
		ret = v4l2_ctrl_add_handler(&vin->ctrl_handler,
					    sd->ctrl_handler, NULL);
		if (ret < 0)
			goto error;

		v4l2_ctrl_handler_setup(&vin->ctrl_handler);

		fmt.pad = rent->source_pad_idx;

		/* Try to improve our guess of a reasonable window format */
		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &fmt);
		if (ret)
			goto error;

		/* Set default format */
		vin->format.width	= mf->width;
		vin->format.height	= mf->height;
		vin->format.colorspace	= mf->colorspace;
		vin->format.field	= mf->field;
		vin->format.pixelformat	= RVIN_DEFAULT_FORMAT;

		/* Set initial crop and compose */
		vin->crop.top = vin->crop.left = 0;
		vin->crop.width = mf->width;
		vin->crop.height = mf->height;

		vin->compose.top = vin->compose.left = 0;
		vin->compose.width = mf->width;
		vin->compose.height = mf->height;

		f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		f.fmt.pix = vin->format;
		ret = __rvin_s_fmt_vid_cap(vin, &f);
		if (ret)
			goto error;
	}

	vin->last_input = rent;

	return 0;
error:
	v4l2_subdev_call(sd, core, s_power, 0);
	return ret;
}

static void rvin_detach_subdevices(struct rvin_dev *vin)
{
	struct v4l2_subdev *sd = vin_to_source(vin);

	v4l2_subdev_call(sd, core, s_power, 0);
}

static int rvin_enum_input(struct file *file, void *priv,
			   struct v4l2_input *i)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	int ret;

	if (i->index != 0)
		return -EINVAL;

	ret = v4l2_subdev_call(sd, video, g_input_status, &i->status);
	if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
		return ret;

	i->type = V4L2_INPUT_TYPE_CAMERA;

	if (v4l2_subdev_has_op(sd, pad, dv_timings_cap)) {
		i->capabilities = V4L2_IN_CAP_DV_TIMINGS;
		i->std = 0;
	} else {
		i->capabilities = V4L2_IN_CAP_STD;
		ret = v4l2_subdev_call(sd, video, g_tvnorms, &i->std);
		if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
			return ret;
	}

	strlcpy(i->name, "Camera", sizeof(i->name));

	return 0;
}

static int rvin_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int rvin_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;
	return 0;
}

static int rvin_querystd(struct file *file, void *priv, v4l2_std_id *a)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	return v4l2_subdev_call(sd, video, querystd, a);
}

static int rvin_s_std(struct file *file, void *priv, v4l2_std_id a)
{
	struct rvin_dev *vin = video_drvdata(file);
	int ret;

	ret = v4l2_subdev_call(vin_to_source(vin), video, s_std, a);
	if (ret < 0)
		return ret;

	/* Changing the standard will change the width/height */
	return rvin_reset_format(vin);
}

static int rvin_g_std(struct file *file, void *priv, v4l2_std_id *a)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	return v4l2_subdev_call(sd, video, g_std, a);
}

static int rvin_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_event_subscribe(fh, sub, 4, NULL);
	}
	return v4l2_ctrl_subscribe_event(fh, sub);
}

static int rvin_enum_dv_timings(struct file *file, void *priv_fh,
				struct v4l2_enum_dv_timings *timings)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	struct rvin_graph_entity *rent;
	int ret;

	rent = vin_to_entity(vin);
	if (!rent)
		return -ENODEV;

	if (timings->pad)
		return -EINVAL;

	timings->pad = rent->sink_pad_idx;

	ret = v4l2_subdev_call(sd, pad, enum_dv_timings, timings);

	timings->pad = 0;

	return ret;
}

static int rvin_s_dv_timings(struct file *file, void *priv_fh,
			     struct v4l2_dv_timings *timings)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	int ret;

	ret = v4l2_subdev_call(sd, video, s_dv_timings, timings);
	if (ret)
		return ret;

	/* Changing the timings will change the width/height */
	return rvin_reset_format(vin);
}

static int rvin_g_dv_timings(struct file *file, void *priv_fh,
			     struct v4l2_dv_timings *timings)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	return v4l2_subdev_call(sd, video, g_dv_timings, timings);
}

static int rvin_query_dv_timings(struct file *file, void *priv_fh,
				 struct v4l2_dv_timings *timings)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);

	return v4l2_subdev_call(sd, video, query_dv_timings, timings);
}

static int rvin_dv_timings_cap(struct file *file, void *priv_fh,
			       struct v4l2_dv_timings_cap *cap)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	struct rvin_graph_entity *rent;
	int ret;

	rent = vin_to_entity(vin);
	if (!rent)
		return -ENODEV;

	if (cap->pad)
		return -EINVAL;

	cap->pad = rent->sink_pad_idx;

	ret = v4l2_subdev_call(sd, pad, dv_timings_cap, cap);

	cap->pad = 0;

	return ret;
}

static int rvin_g_edid(struct file *file, void *fh, struct v4l2_edid *edid)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	struct rvin_graph_entity *rent;
	int ret;

	rent = vin_to_entity(vin);
	if (!rent)
		return -ENODEV;

	if (edid->pad)
		return -EINVAL;

	edid->pad = rent->sink_pad_idx;

	ret = v4l2_subdev_call(sd, pad, get_edid, edid);

	edid->pad = 0;

	return ret;
}

static int rvin_s_edid(struct file *file, void *fh, struct v4l2_edid *edid)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *sd = vin_to_source(vin);
	struct rvin_graph_entity *rent;
	int ret;

	rent = vin_to_entity(vin);
	if (!rent)
		return -ENODEV;

	if (edid->pad)
		return -EINVAL;

	edid->pad = rent->sink_pad_idx;

	ret = v4l2_subdev_call(sd, pad, set_edid, edid);

	edid->pad = 0;

	return ret;
}

static const struct v4l2_ioctl_ops rvin_ioctl_ops = {
	.vidioc_querycap		= rvin_querycap,
	.vidioc_try_fmt_vid_cap		= rvin_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= rvin_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= rvin_s_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap	= rvin_enum_fmt_vid_cap,

	.vidioc_g_selection		= rvin_g_selection,
	.vidioc_s_selection		= rvin_s_selection,

	.vidioc_cropcap			= rvin_cropcap,

	.vidioc_enum_input		= rvin_enum_input,
	.vidioc_g_input			= rvin_g_input,
	.vidioc_s_input			= rvin_s_input,

	.vidioc_dv_timings_cap		= rvin_dv_timings_cap,
	.vidioc_enum_dv_timings		= rvin_enum_dv_timings,
	.vidioc_g_dv_timings		= rvin_g_dv_timings,
	.vidioc_s_dv_timings		= rvin_s_dv_timings,
	.vidioc_query_dv_timings	= rvin_query_dv_timings,

	.vidioc_g_edid			= rvin_g_edid,
	.vidioc_s_edid			= rvin_s_edid,

	.vidioc_querystd		= rvin_querystd,
	.vidioc_g_std			= rvin_g_std,
	.vidioc_s_std			= rvin_s_std,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= rvin_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * File Operations
 */

static int rvin_open(struct file *file)
{
	struct rvin_dev *vin = video_drvdata(file);
	struct v4l2_subdev *source, *bridge;
	int ret;

	mutex_lock(&vin->lock);

	file->private_data = vin;

	ret = v4l2_fh_open(file);
	if (ret)
		goto err_out;

	/* If there is no subdevice there is not much we can do */
	source = vin_to_source(vin);
	if (!source) {
		ret = -EBUSY;
		goto err_open;
	}

	if (v4l2_fh_is_singular_file(file)) {
		if (vin_have_bridge(vin)) {

			/* If there are no bridge not much we can do */
			bridge = vin_to_bridge(vin);
			if (!bridge) {
				ret = -EBUSY;
				goto err_open;
			}

			v4l2_pipeline_pm_use(&vin->vdev.entity, 1);

			vin_dbg(vin, "Group source: %s bridge: %s\n",
				source->name,
				bridge->name);
		}
		pm_runtime_get_sync(vin->dev);
		ret = rvin_attach_subdevices(vin);
		if (ret) {
			vin_err(vin, "Error attaching subdevice\n");
			goto err_power;
		}
	}

	mutex_unlock(&vin->lock);

	return 0;
err_power:
	pm_runtime_put(vin->dev);
	if (vin_have_bridge(vin))
		v4l2_pipeline_pm_use(&vin->vdev.entity, 0);
err_open:
	v4l2_fh_release(file);
err_out:
	mutex_unlock(&vin->lock);
	return ret;
}

static int rvin_release(struct file *file)
{
	struct rvin_dev *vin = video_drvdata(file);
	bool fh_singular;
	int ret;

	mutex_lock(&vin->lock);

	/* Save the singular status before we call the clean-up helper */
	fh_singular = v4l2_fh_is_singular_file(file);

	/* the release helper will cleanup any on-going streaming */
	ret = _vb2_fop_release(file, NULL);

	/*
	 * If this was the last open file.
	 * Then de-initialize hw module.
	 */
	if (fh_singular) {
		rvin_detach_subdevices(vin);
		pm_runtime_put(vin->dev);
		if (vin_have_bridge(vin))
			v4l2_pipeline_pm_use(&vin->vdev.entity, 0);
	}

	mutex_unlock(&vin->lock);

	return ret;
}

static const struct v4l2_file_operations rvin_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open		= rvin_open,
	.release	= rvin_release,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.read		= vb2_fop_read,
};

void rvin_v4l2_remove(struct rvin_dev *vin)
{
	v4l2_info(&vin->v4l2_dev, "Removing %s\n",
		  video_device_node_name(&vin->vdev));

	/* Checks internaly if handlers have been init or not */
	v4l2_ctrl_handler_free(&vin->ctrl_handler);

	/* Checks internaly if vdev have been init or not */
	video_unregister_device(&vin->vdev);
}

static void rvin_notify(struct v4l2_subdev *sd,
			unsigned int notification, void *arg)
{
	struct rvin_dev *vin =
		container_of(sd->v4l2_dev, struct rvin_dev, v4l2_dev);

	switch (notification) {
	case V4L2_DEVICE_NOTIFY_EVENT:
		v4l2_event_queue(&vin->vdev, arg);
		break;
	default:
		break;
	}
}

int rvin_v4l2_probe(struct rvin_dev *vin)
{
	struct video_device *vdev = &vin->vdev;
	int ret;

	vin->v4l2_dev.notify = rvin_notify;

	/* video node */
	vdev->fops = &rvin_fops;
	vdev->v4l2_dev = &vin->v4l2_dev;
	vdev->queue = &vin->queue;
	snprintf(vdev->name, sizeof(vdev->name), "%s.%s", KBUILD_MODNAME,
		 dev_name(vin->dev));
	vdev->release = video_device_release_empty;
	vdev->ioctl_ops = &rvin_ioctl_ops;
	vdev->lock = &vin->lock;
	vdev->ctrl_handler = &vin->ctrl_handler;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
		V4L2_CAP_READWRITE;

	vin->format.pixelformat	= RVIN_DEFAULT_FORMAT;
	rvin_reset_format(vin);

	ret = video_register_device(&vin->vdev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		vin_err(vin, "Failed to register video device\n");
		return ret;
	}

	video_set_drvdata(&vin->vdev, vin);

	v4l2_info(&vin->v4l2_dev, "Device registered as %s\n",
		  video_device_node_name(&vin->vdev));

	return ret;
}
