/*
 * vsp1_hgo.c  --  R-Car VSP1 Histogram Generator 1D
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/gfp.h>

#include <media/v4l2-subdev.h>
#include <media/videobuf2-vmalloc.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_hgo.h"

#define HGO_MIN_SIZE				4U
#define HGO_MAX_SIZE				8192U
#define HGO_DATA_SIZE				((2 + 256) * 4)

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline u32 vsp1_hgo_read(struct vsp1_hgo *hgo, u32 reg)
{
	return vsp1_read(hgo->entity.vsp1, reg);
}

static inline void vsp1_hgo_write(struct vsp1_hgo *hgo, struct vsp1_dl_list *dl,
				  u32 reg, u32 data)
{
	vsp1_dl_list_write(dl, reg, data);
}

/* -----------------------------------------------------------------------------
 * Frame End Handler
 */

void vsp1_hgo_frame_end(struct vsp1_entity *entity)
{
	struct vsp1_hgo *hgo = to_hgo(&entity->subdev);
	struct vsp1_histogram_buffer *buf;
	unsigned int i;
	size_t size;
	u32 *data;

	buf = vsp1_histogram_buffer_get(&hgo->histo);
	if (!buf)
		return;

	data = buf->addr;

	if (hgo->num_bins == 256) {
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_MAXMIN);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_SUM);

		for (i = 0; i < 256; ++i) {
			vsp1_write(hgo->entity.vsp1, VI6_HGO_EXT_HIST_ADDR, i);
			*data++ = vsp1_hgo_read(hgo, VI6_HGO_EXT_HIST_DATA);
		}

		size = (2 + 256) * sizeof(u32);
	} else if (hgo->max_rgb) {
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_MAXMIN);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_SUM);

		for (i = 0; i < 64; ++i)
			*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_HISTO(i));

		size = (2 + 64) * sizeof(u32);
	} else {
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_R_MAXMIN);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_MAXMIN);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_B_MAXMIN);

		*data++ = vsp1_hgo_read(hgo, VI6_HGO_R_SUM);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_G_SUM);
		*data++ = vsp1_hgo_read(hgo, VI6_HGO_B_SUM);

		for (i = 0; i < 64; ++i) {
			data[i] = vsp1_hgo_read(hgo, VI6_HGO_R_HISTO(i));
			data[i+64] = vsp1_hgo_read(hgo, VI6_HGO_G_HISTO(i));
			data[i+128] = vsp1_hgo_read(hgo, VI6_HGO_B_HISTO(i));
		}

		size = (6 + 64 * 3) * sizeof(u32);
	}

	vsp1_histogram_buffer_complete(&hgo->histo, buf, size);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

#define V4L2_CID_VSP1_HGO_MAX_RGB		(V4L2_CID_USER_BASE + 1)
#define V4L2_CID_VSP1_HGO_NUM_BINS		(V4L2_CID_USER_BASE + 2)

static const struct v4l2_ctrl_config hgo_max_rgb_control = {
	.id = V4L2_CID_VSP1_HGO_MAX_RGB,
	.name = "Maximum RGB Mode",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.def = 0,
	.step = 1,
};

static const s64 hgo_num_bins[] = {
	64, 256,
};

static const struct v4l2_ctrl_config hgo_num_bins_control = {
	.id = V4L2_CID_VSP1_HGO_NUM_BINS,
	.name = "Number of Bins",
	.type = V4L2_CTRL_TYPE_INTEGER_MENU,
	.min = 0,
	.max = 1,
	.def = 0,
	.qmenu_int = hgo_num_bins,
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static int hgo_enum_mbus_code(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		MEDIA_BUS_FMT_ARGB8888_1X32,
		MEDIA_BUS_FMT_AHSV8888_1X32,
		MEDIA_BUS_FMT_AYUV8_1X32,
	};

	if (code->pad == HGO_PAD_SOURCE) {
		code->code = MEDIA_BUS_FMT_FIXED;
		return 0;
	}

	return vsp1_subdev_enum_mbus_code(subdev, cfg, code, codes,
					  ARRAY_SIZE(codes));
}

static int hgo_enum_frame_size(struct v4l2_subdev *subdev,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad != HGO_PAD_SINK)
		return -EINVAL;

	return vsp1_subdev_enum_frame_size(subdev, cfg, fse, HGO_MIN_SIZE,
					   HGO_MIN_SIZE, HGO_MAX_SIZE,
					   HGO_MAX_SIZE);
}

static int hgo_get_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_hgo *hgo = to_hgo(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	if (sel->pad != HGO_PAD_SINK)
		return -EINVAL;

	config = vsp1_entity_get_pad_config(&hgo->entity, cfg, sel->which);
	if (!config)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		crop = vsp1_entity_get_pad_selection(&hgo->entity, config,
						     HGO_PAD_SINK,
						     V4L2_SEL_TGT_CROP);
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = crop->width;
		sel->r.height = crop->height;
		return 0;

	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		format = vsp1_entity_get_pad_format(&hgo->entity, config,
						    HGO_PAD_SINK);
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = format->width;
		sel->r.height = format->height;
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_CROP:
		sel->r = *vsp1_entity_get_pad_selection(&hgo->entity, config,
							sel->pad, sel->target);
		return 0;

	default:
		return -EINVAL;
	}
}

static int hgo_set_crop(struct v4l2_subdev *subdev,
			struct v4l2_subdev_pad_config *config,
			struct v4l2_subdev_selection *sel)
{
	struct vsp1_hgo *hgo = to_hgo(subdev);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *selection;

	/* The crop rectangle must be inside the input frame. */
	format = vsp1_entity_get_pad_format(&hgo->entity, config, HGO_PAD_SINK);
	sel->r.left = clamp_t(unsigned int, sel->r.left, 0, format->width - 1);
	sel->r.top = clamp_t(unsigned int, sel->r.top, 0, format->height - 1);
	sel->r.width = clamp_t(unsigned int, sel->r.width, HGO_MIN_SIZE,
			       format->width - sel->r.left);
	sel->r.height = clamp_t(unsigned int, sel->r.height, HGO_MIN_SIZE,
				format->height - sel->r.top);

	/* Set the crop rectangle and reset the compose rectangle. */
	selection = vsp1_entity_get_pad_selection(&hgo->entity, config,
						  sel->pad, V4L2_SEL_TGT_CROP);
	*selection = sel->r;

	selection = vsp1_entity_get_pad_selection(&hgo->entity, config,
						  sel->pad,
						  V4L2_SEL_TGT_COMPOSE);
	*selection = sel->r;

	return 0;
}

static int hgo_set_compose(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *config,
			   struct v4l2_subdev_selection *sel)
{
	struct vsp1_hgo *hgo = to_hgo(subdev);
	struct v4l2_rect *compose;
	struct v4l2_rect *crop;
	unsigned int ratio;

	/* The compose rectangle is used to configure downscaling, the top left
	 * corner is fixed to (0,0) and the size to 1/2 or 1/4 of the crop
	 * rectangle.
	 */
	sel->r.left = 0;
	sel->r.top = 0;

	crop = vsp1_entity_get_pad_selection(&hgo->entity, config, sel->pad,
					     V4L2_SEL_TGT_CROP);

	/* Clamp the width and height to acceptable values first and then
	 * compute the closest rounded dividing ratio.
	 *
	 * Ratio	Rounded ratio
	 * --------------------------
	 * [1.0 1.5[	1
	 * [1.5 3.0[	2
	 * [3.0 4.0]	4
	 *
	 * The rounded ratio can be computed using
	 *
	 * 1 << (ceil(ratio * 2) / 3)
	 */
	sel->r.width = clamp(sel->r.width, crop->width / 4, crop->width);
	ratio = 1 << (crop->width * 2 / sel->r.width / 3);
	sel->r.width = crop->width / ratio;


	sel->r.height = clamp(sel->r.height, crop->height / 4, crop->height);
	ratio = 1 << (crop->height * 2 / sel->r.height / 3);
	sel->r.height = crop->height / ratio;

	compose = vsp1_entity_get_pad_selection(&hgo->entity, config, sel->pad,
						V4L2_SEL_TGT_COMPOSE);
	*compose = sel->r;

	return 0;
}

static int hgo_set_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_hgo *hgo = to_hgo(subdev);
	struct v4l2_subdev_pad_config *config;

	if (sel->pad != HGO_PAD_SINK)
		return -EINVAL;

	config = vsp1_entity_get_pad_config(&hgo->entity, cfg, sel->which);
	if (!config)
		return -EINVAL;

	if (sel->target == V4L2_SEL_TGT_CROP)
		return hgo_set_crop(subdev, config, sel);
	else if (sel->target == V4L2_SEL_TGT_COMPOSE)
		return hgo_set_compose(subdev, config, sel);
	else
		return -EINVAL;
}

static int hgo_get_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	if (fmt->pad == HGO_PAD_SOURCE) {
		fmt->format.code = MEDIA_BUS_FMT_FIXED;
		fmt->format.width = 0;
		fmt->format.height = 0;
		fmt->format.field = V4L2_FIELD_NONE;
		fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	}

	return vsp1_subdev_get_pad_format(subdev, cfg, fmt);
}

static int hgo_set_format(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct vsp1_hgo *hgo = to_hgo(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *selection;

	if (fmt->pad != HGO_PAD_SINK)
		return hgo_get_format(subdev, cfg, fmt);

	config = vsp1_entity_get_pad_config(&hgo->entity, cfg, fmt->which);
	if (!config)
		return -EINVAL;

	/* Default to YUV if the requested format is not supported. */
	if (fmt->format.code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
	    fmt->format.code != MEDIA_BUS_FMT_AHSV8888_1X32 &&
	    fmt->format.code != MEDIA_BUS_FMT_AYUV8_1X32)
		fmt->format.code = MEDIA_BUS_FMT_AYUV8_1X32;

	format = vsp1_entity_get_pad_format(&hgo->entity, config, fmt->pad);

	format->code = fmt->format.code;
	format->width = clamp_t(unsigned int, fmt->format.width,
				HGO_MIN_SIZE, HGO_MAX_SIZE);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 HGO_MIN_SIZE, HGO_MAX_SIZE);
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	fmt->format = *format;

	/* Reset the crop and compose rectangles */
	selection = vsp1_entity_get_pad_selection(&hgo->entity, config,
						  fmt->pad, V4L2_SEL_TGT_CROP);
	selection->left = 0;
	selection->top = 0;
	selection->width = format->width;
	selection->height = format->height;

	selection = vsp1_entity_get_pad_selection(&hgo->entity, config,
						  fmt->pad,
						  V4L2_SEL_TGT_COMPOSE);
	selection->left = 0;
	selection->top = 0;
	selection->width = format->width;
	selection->height = format->height;

	return 0;
}

static struct v4l2_subdev_pad_ops hgo_pad_ops = {
	.enum_mbus_code = hgo_enum_mbus_code,
	.enum_frame_size = hgo_enum_frame_size,
	.get_fmt = hgo_get_format,
	.set_fmt = hgo_set_format,
	.get_selection = hgo_get_selection,
	.set_selection = hgo_set_selection,
};

static struct v4l2_subdev_ops hgo_ops = {
	.pad    = &hgo_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void hgo_configure(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_dl_list *dl)
{
	struct vsp1_hgo *hgo = to_hgo(&entity->subdev);
	struct v4l2_rect *compose;
	struct v4l2_rect *crop;
	unsigned int hratio;
	unsigned int vratio;

	crop = vsp1_entity_get_pad_selection(entity, entity->config,
					     HGO_PAD_SINK, V4L2_SEL_TGT_CROP);
	compose = vsp1_entity_get_pad_selection(entity, entity->config,
						HGO_PAD_SINK,
						V4L2_SEL_TGT_COMPOSE);

	vsp1_hgo_write(hgo, dl, VI6_HGO_REGRST, VI6_HGO_REGRST_RCLEA);

	vsp1_hgo_write(hgo, dl, VI6_HGO_OFFSET,
		       (crop->left << VI6_HGO_OFFSET_HOFFSET_SHIFT) |
		       (crop->top << VI6_HGO_OFFSET_VOFFSET_SHIFT));
	vsp1_hgo_write(hgo, dl, VI6_HGO_SIZE,
		       (crop->width << VI6_HGO_SIZE_HSIZE_SHIFT) |
		       (crop->height << VI6_HGO_SIZE_VSIZE_SHIFT));

	mutex_lock(hgo->ctrls.handler.lock);
	hgo->max_rgb = hgo->ctrls.max_rgb->cur.val;
	if (hgo->ctrls.num_bins)
		hgo->num_bins = hgo_num_bins[hgo->ctrls.num_bins->cur.val];
	mutex_unlock(hgo->ctrls.handler.lock);

	hratio = crop->width * 2 / compose->width / 3;
	vratio = crop->height * 2 / compose->height / 3;
	vsp1_hgo_write(hgo, dl, VI6_HGO_MODE,
		       (hgo->num_bins == 256 ? VI6_HGO_MODE_STEP : 0) |
		       (hgo->max_rgb ? VI6_HGO_MODE_MAXRGB : 0) |
		       (hratio << VI6_HGO_MODE_HRATIO_SHIFT) |
		       (vratio << VI6_HGO_MODE_VRATIO_SHIFT));
}

static void hgo_destroy(struct vsp1_entity *entity)
{
	struct vsp1_hgo *hgo = to_hgo(&entity->subdev);

	vsp1_histogram_cleanup(&hgo->histo);
}

static const struct vsp1_entity_operations hgo_entity_ops = {
	.configure = hgo_configure,
	.destroy = hgo_destroy,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_hgo *vsp1_hgo_create(struct vsp1_device *vsp1)
{
	struct vsp1_hgo *hgo;
	int ret;

	hgo = devm_kzalloc(vsp1->dev, sizeof(*hgo), GFP_KERNEL);
	if (hgo == NULL)
		return ERR_PTR(-ENOMEM);

	hgo->entity.ops = &hgo_entity_ops;
	hgo->entity.type = VSP1_ENTITY_HGO;

	ret = vsp1_entity_init(vsp1, &hgo->entity, "hgo", 2, &hgo_ops,
			       MEDIA_ENT_F_PROC_VIDEO_STATISTICS);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(&hgo->ctrls.handler,
			       vsp1->info->gen == 3 ? 2 : 1);
	hgo->ctrls.max_rgb = v4l2_ctrl_new_custom(&hgo->ctrls.handler,
						  &hgo_max_rgb_control, NULL);
	if (vsp1->info->gen == 3)
		hgo->ctrls.num_bins =
			v4l2_ctrl_new_custom(&hgo->ctrls.handler,
					     &hgo_num_bins_control, NULL);

	hgo->max_rgb = false;
	hgo->num_bins = 64;

	hgo->entity.subdev.ctrl_handler = &hgo->ctrls.handler;

	/* Initialize the video device and queue for statistics data. */
	ret = vsp1_histogram_init(vsp1, &hgo->histo, hgo->entity.subdev.name,
				  HGO_DATA_SIZE);
	if (ret < 0) {
		vsp1_entity_destroy(&hgo->entity);
		return ERR_PTR(ret);
	}

	return hgo;
}
