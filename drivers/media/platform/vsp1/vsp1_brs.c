/*
 * vsp1_brs.c  --  R-Car VSP1 Blend ROP Sub Unit
 *
 * Copyright (C) 2016-2017 Renesas Electronics Corporation
 *
 * This file is based on the drivers/media/platform/vsp1/vsp1_bru.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/gfp.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_brs.h"
#include "vsp1_dl.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_video.h"

#define BRS_MIN_SIZE				1U
#define BRS_MAX_SIZE				8190U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_brs_write(struct vsp1_brs *brs, struct vsp1_dl_list *dl,
				  u32 reg, u32 data)
{
	vsp1_dl_list_write(dl, reg, data);
}

/* -----------------------------------------------------------------------------
 * Controls
 */

static int brs_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vsp1_brs *brs =
		container_of(ctrl->handler, struct vsp1_brs, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_BG_COLOR:
		brs->bgcolor = ctrl->val;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops brs_ctrl_ops = {
	.s_ctrl = brs_s_ctrl,
};

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

/*
 * The BRS can't perform format conversion, all sink and source formats must be
 * identical. We pick the format on the first sink pad (pad 0) and propagate it
 * to all other pads.
 */

static int brs_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		MEDIA_BUS_FMT_ARGB8888_1X32,
		MEDIA_BUS_FMT_AYUV8_1X32,
	};

	return vsp1_subdev_enum_mbus_code(subdev, cfg, code, codes,
					  ARRAY_SIZE(codes));
}

static int brs_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
	    fse->code != MEDIA_BUS_FMT_AYUV8_1X32)
		return -EINVAL;

	fse->min_width = BRS_MIN_SIZE;
	fse->max_width = BRS_MAX_SIZE;
	fse->min_height = BRS_MIN_SIZE;
	fse->max_height = BRS_MAX_SIZE;

	return 0;
}

static struct v4l2_rect *brs_get_compose(struct vsp1_brs *brs,
					 struct v4l2_subdev_pad_config *cfg,
					 unsigned int pad)
{
	return v4l2_subdev_get_try_compose(&brs->entity.subdev, cfg, pad);
}

static void brs_try_format(struct vsp1_brs *brs,
			   struct v4l2_subdev_pad_config *config,
			   unsigned int pad, struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_mbus_framefmt *format;
	struct vsp1_device *vsp1 = brs->entity.vsp1;
	int brs_base;

	brs_base = vsp1->info->rpf_count - vsp1->num_brs_inputs;

	if (!pad)
		goto not_set_code;

	if (pad == brs_base) {
		/* Default to YUV if the requested format is not supported. */
		if (fmt->code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
		    fmt->code != MEDIA_BUS_FMT_AYUV8_1X32)
			fmt->code = MEDIA_BUS_FMT_AYUV8_1X32;
	} else {
		/* The BRS can't perform format conversion. */
		format = vsp1_entity_get_pad_format(&brs->entity, config,
						    BRS_PAD_SINK(brs_base));
		fmt->code = format->code;
	}

not_set_code:
	fmt->width = clamp(fmt->width, BRS_MIN_SIZE, BRS_MAX_SIZE);
	fmt->height = clamp(fmt->height, BRS_MIN_SIZE, BRS_MAX_SIZE);
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

static int brs_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_brs *brs = to_brs(subdev);
	struct vsp1_device *vsp1 = brs->entity.vsp1;
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	int ret = 0;
	int brs_base;

	mutex_lock(&brs->entity.lock);

	config = vsp1_entity_get_pad_config(&brs->entity, cfg, fmt->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	brs_try_format(brs, config, fmt->pad, &fmt->format);

	format = vsp1_entity_get_pad_format(&brs->entity, config, fmt->pad);
	*format = fmt->format;

	/* Reset the compose rectangle */
	if (fmt->pad != brs->entity.source_pad) {
		struct v4l2_rect *compose;

		compose = brs_get_compose(brs, config, fmt->pad);
		compose->left = 0;
		compose->top = 0;
		compose->width = format->width;
		compose->height = format->height;
	}

	brs_base = vsp1->info->rpf_count - vsp1->num_brs_inputs;

	/* Propagate the format code to all pads */
	if (fmt->pad == BRS_PAD_SINK(brs_base)) {
		unsigned int i;

		for (i = brs_base; i <= brs->entity.source_pad; ++i) {
			format = vsp1_entity_get_pad_format(&brs->entity,
							    config, i);
			format->code = fmt->format.code;
		}
	}

done:
	mutex_unlock(&brs->entity.lock);
	return ret;
}

static int brs_get_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_brs *brs = to_brs(subdev);
	struct v4l2_subdev_pad_config *config;

	if (sel->pad == brs->entity.source_pad)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = BRS_MAX_SIZE;
		sel->r.height = BRS_MAX_SIZE;
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
		config = vsp1_entity_get_pad_config(&brs->entity, cfg,
						    sel->which);
		if (!config)
			return -EINVAL;

		mutex_lock(&brs->entity.lock);
		sel->r = *brs_get_compose(brs, config, sel->pad);
		mutex_unlock(&brs->entity.lock);
		return 0;

	default:
		return -EINVAL;
	}
}

static int brs_set_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct vsp1_brs *brs = to_brs(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *compose;
	int ret = 0;

	if (sel->pad == brs->entity.source_pad)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	mutex_lock(&brs->entity.lock);

	config = vsp1_entity_get_pad_config(&brs->entity, cfg, sel->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	/*
	 * The compose rectangle top left corner must be inside the output
	 * frame.
	 */
	format = vsp1_entity_get_pad_format(&brs->entity, config,
					    brs->entity.source_pad);
	sel->r.left = clamp_t(unsigned int, sel->r.left, 0, format->width - 1);
	sel->r.top = clamp_t(unsigned int, sel->r.top, 0, format->height - 1);

	/* Scaling isn't supported, the compose rectangle size must be identical
	 * to the sink format size.
	 */
	format = vsp1_entity_get_pad_format(&brs->entity, config, sel->pad);
	sel->r.width = format->width;
	sel->r.height = format->height;

	compose = brs_get_compose(brs, config, sel->pad);
	*compose = sel->r;

done:
	mutex_unlock(&brs->entity.lock);
	return ret;
}

static const struct v4l2_subdev_pad_ops brs_pad_ops = {
	.init_cfg = vsp1_entity_init_cfg,
	.enum_mbus_code = brs_enum_mbus_code,
	.enum_frame_size = brs_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = brs_set_format,
	.get_selection = brs_get_selection,
	.set_selection = brs_set_selection,
};

static const struct v4l2_subdev_ops brs_ops = {
	.pad    = &brs_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void brs_configure(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_dl_list *dl,
			  enum vsp1_entity_params params)
{
	struct vsp1_brs *brs = to_brs(&entity->subdev);
	struct v4l2_mbus_framefmt *format;
	unsigned int flags;
	unsigned int i;

	if (params != VSP1_ENTITY_PARAMS_INIT)
		return;

	format = vsp1_entity_get_pad_format(&brs->entity, brs->entity.config,
					    brs->entity.source_pad);

	if (pipe->vmute_flag) {
		vsp1_brs_write(brs, dl, VI6_BRS_INCTRL, 0);
		vsp1_brs_write(brs, dl, VI6_BRS_VIRRPF_SIZE,
		  (format->width << VI6_BRS_VIRRPF_SIZE_HSIZE_SHIFT) |
		  (format->height << VI6_BRS_VIRRPF_SIZE_VSIZE_SHIFT));
		vsp1_brs_write(brs, dl, VI6_BRS_VIRRPF_LOC, 0);
		vsp1_brs_write(brs, dl, VI6_BRS_VIRRPF_COL, (0xFF << 24));

		for (i = 0; i < brs->entity.source_pad; ++i) {
			vsp1_brs_write(brs, dl, VI6_BRS_BLD(i),
			VI6_BRS_BLD_CCMDX_255_SRC_A |
			VI6_BRS_BLD_CCMDY_SRC_A |
			VI6_BRS_BLD_ACMDX_255_SRC_A |
			VI6_BRS_BLD_ACMDY_COEFY |
			VI6_BRS_BLD_COEFY_MASK);
		}

		return;
	}

	/* The hardware is extremely flexible but we have no userspace API to
	 * expose all the parameters, nor is it clear whether we would have use
	 * cases for all the supported modes. Let's just harcode the parameters
	 * to sane default values for now.
	 */

	/* Disable dithering and enable color data normalization unless the
	 * format at the pipeline output is premultiplied.
	 */
	flags = pipe->output ? pipe->output->format.flags : 0;
	vsp1_brs_write(brs, dl, VI6_BRS_INCTRL,
		       flags & V4L2_PIX_FMT_FLAG_PREMUL_ALPHA ?
		       0 : VI6_BRS_INCTRL_NRM);

	/* Set the background position to cover the whole output image and
	 * configure its color.
	 */
	vsp1_brs_write(brs, dl, VI6_BRS_VIRRPF_SIZE,
		       (format->width << VI6_BRS_VIRRPF_SIZE_HSIZE_SHIFT) |
		       (format->height << VI6_BRS_VIRRPF_SIZE_VSIZE_SHIFT));
	vsp1_brs_write(brs, dl, VI6_BRS_VIRRPF_LOC, 0);

	vsp1_brs_write(brs, dl, VI6_BRS_VIRRPF_COL, brs->bgcolor |
		       (0xff << VI6_BRS_VIRRPF_COL_A_SHIFT));

	for (i = 0; i < brs->entity.source_pad; ++i) {
		bool premultiplied = false;
		u32 ctrl = 0;

		/* Configure all Blend/ROP units corresponding to an enabled BRS
		 * input for alpha blending. Blend/ROP units corresponding to
		 * disabled BRS inputs are used in ROP NOP mode to ignore the
		 * SRC input.
		 */
		if (brs->inputs[i].rpf) {
			ctrl |= VI6_BRS_CTRL_RBC;

			premultiplied = brs->inputs[i].rpf->format.flags
				      & V4L2_PIX_FMT_FLAG_PREMUL_ALPHA;
		} else {
			ctrl |= VI6_BRS_CTRL_CROP(VI6_ROP_NOP)
			     |  VI6_BRS_CTRL_AROP(VI6_ROP_NOP);
		}

		/* Select the virtual RPF as the Blend/ROP unit A DST input to
		 * serve as a background color.
		 */
		if (i == 0)
			ctrl |= VI6_BRS_CTRL_DSTSEL_VRPF;

		/* Route BRS inputs 0 to 3 as SRC inputs to Blend/ROP units A to
		 * D in that order. The Blend/ROP unit B SRC is hardwired to the
		 * ROP unit output, the corresponding register bits must be set
		 * to 0.
		 */
		if (i != 1)
			ctrl |= VI6_BRS_CTRL_SRCSEL_BRSIN(i);

		vsp1_brs_write(brs, dl, VI6_BRS_CTRL(i), ctrl);

		/* Harcode the blending formula to
		 *
		 *	DSTc = DSTc * (1 - SRCa) + SRCc * SRCa
		 *	DSTa = DSTa * (1 - SRCa) + SRCa
		 *
		 * when the SRC input isn't premultiplied, and to
		 *
		 *	DSTc = DSTc * (1 - SRCa) + SRCc
		 *	DSTa = DSTa * (1 - SRCa) + SRCa
		 *
		 * otherwise.
		 */
		vsp1_brs_write(brs, dl, VI6_BRS_BLD(i),
			       VI6_BRS_BLD_CCMDX_255_SRC_A |
			       (premultiplied ? VI6_BRS_BLD_CCMDY_COEFY :
						VI6_BRS_BLD_CCMDY_SRC_A) |
			       VI6_BRS_BLD_ACMDX_255_SRC_A |
			       VI6_BRS_BLD_ACMDY_COEFY |
			       (0xff << VI6_BRS_BLD_COEFY_SHIFT));
	}
}

static const struct vsp1_entity_operations brs_entity_ops = {
	.configure = brs_configure,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_brs *vsp1_brs_create(struct vsp1_device *vsp1)
{
	struct vsp1_brs *brs;
	int ret;

	brs = devm_kzalloc(vsp1->dev, sizeof(*brs), GFP_KERNEL);
	if (brs == NULL)
		return ERR_PTR(-ENOMEM);

	brs->entity.ops = &brs_entity_ops;
	brs->entity.type = VSP1_ENTITY_BRS;

	ret = vsp1_entity_init(vsp1, &brs->entity, "brs",
			       vsp1->info->rpf_count + 1, &brs_ops,
			       MEDIA_ENT_F_PROC_VIDEO_COMPOSER);
	if (ret < 0)
		return ERR_PTR(ret);

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(&brs->ctrls, 1);
	v4l2_ctrl_new_std(&brs->ctrls, &brs_ctrl_ops, V4L2_CID_BG_COLOR,
			  0, 0xffffff, 1, 0);

	brs->bgcolor = 0;

	brs->entity.subdev.ctrl_handler = &brs->ctrls;

	if (brs->ctrls.error) {
		dev_err(vsp1->dev, "brs: failed to initialize controls\n");
		ret = brs->ctrls.error;
		vsp1_entity_destroy(&brs->entity);
		return ERR_PTR(ret);
	}

	return brs;
}
