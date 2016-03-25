/*
 * vsp1_clu.c  --  R-Car VSP1 Cubic Look-Up Table
 *
 * Copyright (C) 2015-2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/vsp1.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_clu.h"
#include "vsp1_dl.h"

#define CLU_MIN_SIZE				4U
#define CLU_MAX_SIZE				8190U

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_clu_write(struct vsp1_clu *clu, struct vsp1_dl_list *dl,
				  u32 reg, u32 data)
{
	vsp1_dl_list_write(dl, reg, data);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Core Operations
 */

static int clu_setup(struct vsp1_clu *clu, struct vsp1_clu_config *config)
{
	struct vsp1_clu_entry __user *entries = config->entries;
	struct vsp1_dl_body *dlb;
	unsigned int i;
	int ret;

	if (config->nentries == 0 || config->nentries > 17*17*17)
		return -EINVAL;

	dlb = vsp1_dl_fragment_alloc(clu->entity.vsp1, 2 * config->nentries);
	if (!dlb)
		return -ENOMEM;

	for (i = 0; i < config->nentries; ++i) {
		u32 addr;
		u32 value;

		if (get_user(addr, &entries[i].addr) ||
		    get_user(value, &entries[i].value)) {
			ret = -EFAULT;
			goto done;
		}

		if (((addr >> 0) & 0xff) >= 17 ||
		    ((addr >> 8) & 0xff) >= 17 ||
		    ((addr >> 16) & 0xff) >= 17 ||
		    ((addr >> 24) & 0xff) != 0 ||
		    (value & 0xff000000) != 0) {
			ret = -EINVAL;
			goto done;
		}

		vsp1_dl_fragment_write(dlb, VI6_CLU_ADDR, addr);
		vsp1_dl_fragment_write(dlb, VI6_CLU_DATA, value);
	}

	mutex_lock(&clu->lock);
	swap(clu->clu, dlb);
	mutex_unlock(&clu->lock);

	ret = 0;

done:
	vsp1_dl_fragment_free(dlb);
	return ret;
}

static long clu_ioctl(struct v4l2_subdev *subdev, unsigned int cmd, void *arg)
{
	struct vsp1_clu *clu = to_clu(subdev);

	switch (cmd) {
	case VIDIOC_VSP1_CLU_CONFIG:
		return clu_setup(clu, arg);

	default:
		return -ENOIOCTLCMD;
	}
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int clu_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		MEDIA_BUS_FMT_ARGB8888_1X32,
		MEDIA_BUS_FMT_AHSV8888_1X32,
		MEDIA_BUS_FMT_AYUV8_1X32,
	};

	return vsp1_subdev_enum_mbus_code(subdev, cfg, code, codes,
					  ARRAY_SIZE(codes));
}

static int clu_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	return vsp1_subdev_enum_frame_size(subdev, cfg, fse, CLU_MIN_SIZE,
					   CLU_MIN_SIZE, CLU_MAX_SIZE,
					   CLU_MAX_SIZE);
}

static int clu_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_clu *clu = to_clu(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;

	config = vsp1_entity_get_pad_config(&clu->entity, cfg, fmt->which);
	if (!config)
		return -EINVAL;

	/* Default to YUV if the requested format is not supported. */
	if (fmt->format.code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
	    fmt->format.code != MEDIA_BUS_FMT_AHSV8888_1X32 &&
	    fmt->format.code != MEDIA_BUS_FMT_AYUV8_1X32)
		fmt->format.code = MEDIA_BUS_FMT_AYUV8_1X32;

	format = vsp1_entity_get_pad_format(&clu->entity, config, fmt->pad);

	if (fmt->pad == CLU_PAD_SOURCE) {
		/* The CLU output format can't be modified. */
		fmt->format = *format;
		return 0;
	}

	format->code = fmt->format.code;
	format->width = clamp_t(unsigned int, fmt->format.width,
				CLU_MIN_SIZE, CLU_MAX_SIZE);
	format->height = clamp_t(unsigned int, fmt->format.height,
				 CLU_MIN_SIZE, CLU_MAX_SIZE);
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	fmt->format = *format;

	/* Propagate the format to the source pad. */
	format = vsp1_entity_get_pad_format(&clu->entity, config,
					    CLU_PAD_SOURCE);
	*format = fmt->format;

	return 0;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static struct v4l2_subdev_core_ops clu_core_ops = {
	.ioctl = clu_ioctl,
};

static struct v4l2_subdev_pad_ops clu_pad_ops = {
	.init_cfg = vsp1_entity_init_cfg,
	.enum_mbus_code = clu_enum_mbus_code,
	.enum_frame_size = clu_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = clu_set_format,
};

static struct v4l2_subdev_ops clu_ops = {
	.core	= &clu_core_ops,
	.pad    = &clu_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void clu_configure(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_dl_list *dl)
{
	struct vsp1_clu *clu = to_clu(&entity->subdev);

	vsp1_clu_write(clu, dl, VI6_CLU_CTRL,
		       VI6_CLU_CTRL_MVS | VI6_CLU_CTRL_EN);

	mutex_lock(&clu->lock);
	if (clu->clu) {
		vsp1_dl_list_add_fragment(dl, clu->clu);
		clu->clu = NULL;
	}
	mutex_unlock(&clu->lock);
}

static const struct vsp1_entity_operations clu_entity_ops = {
	.configure = clu_configure,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_clu *vsp1_clu_create(struct vsp1_device *vsp1)
{
	struct vsp1_clu *clu;
	int ret;

	clu = devm_kzalloc(vsp1->dev, sizeof(*clu), GFP_KERNEL);
	if (clu == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_init(&clu->lock);

	clu->entity.ops = &clu_entity_ops;
	clu->entity.type = VSP1_ENTITY_CLU;

	ret = vsp1_entity_init(vsp1, &clu->entity, "clu", 2, &clu_ops,
			       MEDIA_ENT_F_PROC_VIDEO_GENERIC);
	if (ret < 0)
		return ERR_PTR(ret);

	return clu;
}
