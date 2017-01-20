/*
 * vsp1_drm.c  --  R-Car VSP1 DRM API
 *
 * Copyright (C) 2015-2017 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include <media/media-entity.h>
#include <media/rcar-fcp.h>
#include <media/v4l2-subdev.h>
#include <media/vsp1.h>

#include "vsp1.h"
#include "vsp1_bru.h"
#include "vsp1_brs.h"
#include "vsp1_dl.h"
#include "vsp1_drm.h"
#include "vsp1_lif.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"
#include "vsp1_video.h"


static const struct soc_device_attribute r8a7795es1[] = {
	{ .soc_id = "r8a7795", .revision = "ES1.*" },
	{ /* sentinel */ }
};

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

void vsp1_drm_display_start(struct vsp1_device *vsp1, unsigned int lif_index)
{
	vsp1_dlm_irq_display_start(vsp1->drm->pipe[lif_index].output->dlm);
}

/* -----------------------------------------------------------------------------
 * DU Driver API
 */

int vsp1_du_init(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	if (!vsp1)
		return -EPROBE_DEFER;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_init);

int vsp1_du_if_set_mute(struct device *dev, bool on, unsigned int lif_index)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe[lif_index];

	if (on)
		pipe->vmute_flag = true;
	else
		pipe->vmute_flag = false;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_if_set_mute);

/**
 * vsp1_du_setup_lif - Setup the output part of the VSP pipeline
 * @dev: the VSP device
 * @width: output frame width in pixels
 * @height: output frame height in pixels
 *
 * Configure the output part of VSP DRM pipeline for the given frame @width and
 * @height. This sets up formats on the BRU source pad, the WPF0 sink and source
 * pads, and the LIF sink pad.
 *
 * As the media bus code on the BRU source pad is conditioned by the
 * configuration of the BRU sink 0 pad, we also set up the formats on all BRU
 * sinks, even if the configuration will be overwritten later by
 * vsp1_du_setup_rpf(). This ensures that the BRU configuration is set to a well
 * defined state.
 *
 * Return 0 on success or a negative error code on failure.
 */
int vsp1_du_setup_lif(struct device *dev, unsigned int width,
		      unsigned int height, unsigned int lif_index,
		      bool suspend)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe[lif_index];
	struct vsp1_bru *bru = vsp1->bru;
	struct vsp1_brs *brs = vsp1->brs;
	unsigned int init_bru_num, end_bru_num;
	unsigned int init_brs_num, end_brs_num;
	struct v4l2_subdev_format format;
	unsigned int i;
	int ret;

	dev_dbg(vsp1->dev, "%s: configuring LIF%d with format %ux%u\n",
		__func__, lif_index, width, height);

	if (vsp1_gen3_vspdl_check(vsp1)) {
		if (!vsp1->brs || !vsp1->lif[1])
			return -ENXIO;

		init_bru_num = 0;
		init_brs_num = vsp1->info->rpf_count -
			       vsp1->num_brs_inputs;
		end_bru_num = vsp1->info->rpf_count -
			      vsp1->num_brs_inputs;
		end_brs_num = vsp1->info->rpf_count;
	} else {
		init_bru_num = 0;
		init_brs_num = 0;
		end_bru_num = bru->entity.source_pad;
		end_brs_num = 0;
	}

	if (width == 0 || height == 0) {
		/* Zero width or height means the CRTC is being disabled, stop
		 * the pipeline and turn the light off.
		 */
		ret = vsp1_pipeline_stop(pipe);
		if (ret == -ETIMEDOUT)
			dev_err(vsp1->dev, "DRM pipeline stop timeout\n");

		media_entity_pipeline_stop(&pipe->output->entity.subdev.entity);

		if (!suspend) {
			if (lif_index == 1) {
				for (i = init_brs_num; i < end_brs_num; ++i) {
					vsp1->drm->inputs[i].enabled = false;
					brs->inputs[i].rpf = NULL;
					pipe->inputs[i] = NULL;
				}
			} else {
				for (i = init_bru_num; i < end_bru_num; ++i) {
					vsp1->drm->inputs[i].enabled = false;
					bru->inputs[i].rpf = NULL;
					pipe->inputs[i] = NULL;
				}
			}
		}

		pipe->num_inputs = 0;

		vsp1_dlm_reset(pipe->output->dlm);
		vsp1_device_put(vsp1);

		dev_dbg(vsp1->dev, "%s: pipeline disabled\n", __func__);

		return 0;
	}

	/* Configure the format at the BRU sinks and propagate it through the
	 * pipeline.
	 */
	memset(&format, 0, sizeof(format));
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	if (lif_index == 1) {
		for (i = init_brs_num; i < end_brs_num; ++i) {
			format.pad = i;

			format.format.width = width;
			format.format.height = height;
			format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
			format.format.field = V4L2_FIELD_NONE;

			ret = v4l2_subdev_call(&brs->entity.subdev, pad,
					       set_fmt, NULL, &format);
			if (ret < 0)
				return ret;

			dev_dbg(vsp1->dev,
				"%s: set format %ux%u (%x) on BRS pad %u\n",
				__func__, format.format.width,
				format.format.height,
				format.format.code, i);
		}
		format.pad = brs->entity.source_pad;
	} else {
		for (i = init_bru_num; i < end_bru_num; ++i) {
			format.pad = i;

			format.format.width = width;
			format.format.height = height;
			format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
			format.format.field = V4L2_FIELD_NONE;

			ret = v4l2_subdev_call(&bru->entity.subdev, pad,
					       set_fmt, NULL, &format);
			if (ret < 0)
				return ret;

			dev_dbg(vsp1->dev,
				"%s: set format %ux%u (%x) on BRU pad %u\n",
				__func__, format.format.width,
				format.format.height,
				format.format.code, i);
		}
		format.pad = bru->entity.source_pad;
	}

	format.format.width = width;
	format.format.height = height;
	format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
	format.format.field = V4L2_FIELD_NONE;

	if (lif_index == 1) {
		ret = v4l2_subdev_call(&brs->entity.subdev, pad, set_fmt, NULL,
				       &format);
		if (ret < 0)
			return ret;

		dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on BRS pad %u\n",
			__func__, format.format.width, format.format.height,
			format.format.code, i);
	} else {
		ret = v4l2_subdev_call(&bru->entity.subdev, pad, set_fmt, NULL,
				       &format);
		if (ret < 0)
			return ret;

		dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on BRU pad %u\n",
			__func__, format.format.width, format.format.height,
			format.format.code, i);
	}

	format.pad = RWPF_PAD_SINK;
	ret = v4l2_subdev_call(&vsp1->wpf[lif_index]->entity.subdev,
			       pad, set_fmt, NULL, &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on WPF%d sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code, lif_index);

	format.pad = RWPF_PAD_SOURCE;
	ret = v4l2_subdev_call(&vsp1->wpf[lif_index]->entity.subdev,
			       pad, get_fmt, NULL, &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: got format %ux%u (%x) on WPF%d source\n",
		__func__, format.format.width, format.format.height,
		format.format.code, lif_index);

	format.pad = LIF_PAD_SINK;
	ret = v4l2_subdev_call(&vsp1->lif[lif_index]->entity.subdev,
			       pad, set_fmt, NULL, &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on LIF%d sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code, lif_index);

	/* Verify that the format at the output of the pipeline matches the
	 * requested frame size and media bus code.
	 */
	if (format.format.width != width || format.format.height != height ||
	    format.format.code != MEDIA_BUS_FMT_ARGB8888_1X32) {
		dev_dbg(vsp1->dev, "%s: format mismatch\n", __func__);
		return -EPIPE;
	}

	/* Mark the pipeline as streaming and enable the VSP1. This will store
	 * the pipeline pointer in all entities, which the s_stream handlers
	 * will need. We don't start the entities themselves right at this point
	 * as there's no plane configured yet, so we can't start processing
	 * buffers.
	 */
	ret = vsp1_device_get(vsp1);
	if (ret < 0)
		return ret;

	ret = media_entity_pipeline_start(&pipe->output->entity.subdev.entity,
					  &pipe->pipe);
	if (ret < 0) {
		dev_dbg(vsp1->dev, "%s: pipeline start failed\n", __func__);
		vsp1_device_put(vsp1);
		return ret;
	}

	dev_dbg(vsp1->dev, "%s: pipeline enabled\n", __func__);

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_setup_lif);

/**
 * vsp1_du_atomic_begin - Prepare for an atomic update
 * @dev: the VSP device
 */
void vsp1_du_atomic_begin(struct device *dev, unsigned int lif_index)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe[lif_index];

	vsp1->drm->num_inputs = pipe->num_inputs;

	/* Prepare the display list. */
	pipe->dl = vsp1_dl_list_get(pipe->output->dlm);
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_begin);

/**
 * vsp1_du_atomic_update - Setup one RPF input of the VSP pipeline
 * @dev: the VSP device
 * @rpf_index: index of the RPF to setup (0-based)
 * @cfg: the RPF configuration
 *
 * Configure the VSP to perform image composition through RPF @rpf_index as
 * described by the @cfg configuration. The image to compose is referenced by
 * @cfg.mem and composed using the @cfg.src crop rectangle and the @cfg.dst
 * composition rectangle. The Z-order is configurable with higher @zpos values
 * displayed on top.
 *
 * If the @cfg configuration is NULL, the RPF will be disabled. Calling the
 * function on a disabled RPF is allowed.
 *
 * Image format as stored in memory is expressed as a V4L2 @cfg.pixelformat
 * value. The memory pitch is configurable to allow for padding at end of lines,
 * or simply for images that extend beyond the crop rectangle boundaries. The
 * @cfg.pitch value is expressed in bytes and applies to all planes for
 * multiplanar formats.
 *
 * The source memory buffer is referenced by the DMA address of its planes in
 * the @cfg.mem array. Up to two planes are supported. The second plane DMA
 * address is ignored for formats using a single plane.
 *
 * This function isn't reentrant, the caller needs to serialize calls.
 *
 * Return 0 on success or a negative error code on failure.
 */
int vsp1_du_atomic_update(struct device *dev, unsigned int rpf_index,
			  const struct vsp1_du_atomic_config *cfg)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	const struct vsp1_format_info *fmtinfo;
	struct vsp1_rwpf *rpf;

	if (rpf_index >= vsp1->info->rpf_count)
		return -EINVAL;

	rpf = vsp1->rpf[rpf_index];

	if (!cfg) {
		dev_dbg(vsp1->dev, "%s: RPF%u: disable requested\n", __func__,
			rpf_index);

		vsp1->drm->inputs[rpf_index].enabled = false;
		return 0;
	}

	dev_dbg(vsp1->dev,
		"%s: RPF%u: (%u,%u)/%ux%u -> (%u,%u)/%ux%u (%08x), pitch %u dma { %pad, %pad, %pad } zpos %u\n",
		__func__, rpf_index,
		cfg->src.left, cfg->src.top, cfg->src.width, cfg->src.height,
		cfg->dst.left, cfg->dst.top, cfg->dst.width, cfg->dst.height,
		cfg->pixelformat, cfg->pitch, &cfg->mem[0], &cfg->mem[1],
		&cfg->mem[2], cfg->zpos);

	/*
	 * Store the format, stride, memory buffer address, crop and compose
	 * rectangles and Z-order position and for the input.
	 */
	fmtinfo = vsp1_get_format_info(vsp1, cfg->pixelformat);
	if (!fmtinfo) {
		dev_dbg(vsp1->dev, "Unsupport pixel format %08x for RPF\n",
			cfg->pixelformat);
		return -EINVAL;
	}

	rpf->fmtinfo = fmtinfo;
	rpf->format.num_planes = fmtinfo->planes;
	rpf->format.plane_fmt[0].bytesperline = cfg->pitch;
	if ((rpf->fmtinfo->fourcc == V4L2_PIX_FMT_YUV420M) ||
		(rpf->fmtinfo->fourcc == V4L2_PIX_FMT_YVU420M) ||
		(rpf->fmtinfo->fourcc == V4L2_PIX_FMT_YUV422M) ||
		(rpf->fmtinfo->fourcc == V4L2_PIX_FMT_YVU422M))
		rpf->format.plane_fmt[1].bytesperline = cfg->pitch / 2;
	else
		rpf->format.plane_fmt[1].bytesperline = cfg->pitch;
	rpf->alpha = cfg->alpha;
	rpf->interlaced = cfg->interlaced;

	if (soc_device_match(r8a7795es1) && rpf->interlaced) {
		dev_err(vsp1->dev,
			"Interlaced mode is not supported.\n");
		return -EINVAL;
	}

	rpf->mem.addr[0] = cfg->mem[0];
	rpf->mem.addr[1] = cfg->mem[1];
	rpf->mem.addr[2] = cfg->mem[2];

	vsp1->drm->inputs[rpf_index].crop = cfg->src;
	vsp1->drm->inputs[rpf_index].compose = cfg->dst;
	vsp1->drm->inputs[rpf_index].zpos = cfg->zpos;
	vsp1->drm->inputs[rpf_index].enabled = true;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_update);

static int vsp1_du_setup_rpf_pipe(struct vsp1_device *vsp1,
				  struct vsp1_rwpf *rpf, unsigned int bru_input)
{
	struct v4l2_subdev_selection sel;
	struct v4l2_subdev_format format;
	const struct v4l2_rect *crop;
	struct v4l2_subdev *subdev;
	int ret;
	bool brs_use = false;

	/* Configure the format on the RPF sink pad and propagate it up to the
	 * BRU sink pad.
	 */
	crop = &vsp1->drm->inputs[rpf->entity.index].crop;

	memset(&format, 0, sizeof(format));
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	format.pad = RWPF_PAD_SINK;
	format.format.width = crop->width + crop->left;
	format.format.height = crop->height + crop->top;
	format.format.code = rpf->fmtinfo->mbus;
	format.format.field = V4L2_FIELD_NONE;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set format %ux%u (%x) on RPF%u sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code, rpf->entity.index);

	memset(&sel, 0, sizeof(sel));
	sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sel.pad = RWPF_PAD_SINK;
	sel.target = V4L2_SEL_TGT_CROP;
	sel.r = *crop;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, set_selection, NULL,
			       &sel);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set selection (%u,%u)/%ux%u on RPF%u sink\n",
		__func__, sel.r.left, sel.r.top, sel.r.width, sel.r.height,
		rpf->entity.index);

	/* RPF source, hardcode the format to ARGB8888 to turn on format
	 * conversion if needed.
	 */
	format.pad = RWPF_PAD_SOURCE;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, get_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: got format %ux%u (%x) on RPF%u source\n",
		__func__, format.format.width, format.format.height,
		format.format.code, rpf->entity.index);

	format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	/* BRU sink, propagate the format from the RPF source. */
	format.pad = bru_input;

	if (bru_input >=
		(vsp1->info->rpf_count - vsp1->num_brs_inputs))
		brs_use = true;

	if (vsp1_gen3_vspdl_check(vsp1)) {
		if (brs_use)
			subdev = &vsp1->brs->entity.subdev;
		else
			subdev = &vsp1->bru->entity.subdev;
	} else
		subdev = &vsp1->bru->entity.subdev;

	ret = v4l2_subdev_call(subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on %s pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, (brs_use ? "BRS" : "BRU"), format.pad);

	sel.pad = bru_input;
	sel.target = V4L2_SEL_TGT_COMPOSE;
	sel.r = vsp1->drm->inputs[rpf->entity.index].compose;

	ret = v4l2_subdev_call(subdev, pad, set_selection,
			       NULL, &sel);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set selection (%u,%u)/%ux%u on %s pad %u\n",
		__func__, sel.r.left, sel.r.top, sel.r.width, sel.r.height,
		(brs_use ? "BRS" : "BRU"), sel.pad);

	return 0;
}

static unsigned int rpf_zpos(struct vsp1_device *vsp1, struct vsp1_rwpf *rpf)
{
	return vsp1->drm->inputs[rpf->entity.index].zpos;
}

/**
 * vsp1_du_atomic_flush - Commit an atomic update
 * @dev: the VSP device
 */
void vsp1_du_atomic_flush(struct device *dev, unsigned int lif_index)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe[lif_index];
	struct vsp1_rwpf *inputs[VSP1_MAX_RPF] = { NULL, };
	struct vsp1_entity *entity;
	unsigned long flags;
	unsigned int i;
	unsigned int init_rpf, end_rpf;
	int ret;

	/* Count the number of enabled inputs and sort them by Z-order. */
	pipe->num_inputs = 0;

	if (vsp1_gen3_vspdl_check(vsp1)) {
		if (lif_index == 1) {
			init_rpf = vsp1->info->rpf_count -
				       vsp1->num_brs_inputs;
			end_rpf = vsp1->info->rpf_count;
		} else {
			init_rpf = 0;
			end_rpf = vsp1->info->rpf_count -
				      vsp1->num_brs_inputs;
		}
	} else {
		init_rpf = 0;
		end_rpf = vsp1->info->rpf_count;
	}

	for (i = init_rpf; i < end_rpf; ++i) {
		struct vsp1_rwpf *rpf = vsp1->rpf[i];
		unsigned int j;

		if (!vsp1->drm->inputs[i].enabled) {
			pipe->inputs[i] = NULL;
			continue;
		}

		pipe->inputs[i] = rpf;

		/* Insert the RPF in the sorted RPFs array. */
		for (j = pipe->num_inputs++; j > 0; --j) {
			if (rpf_zpos(vsp1, inputs[j-1+init_rpf]) <=
					rpf_zpos(vsp1, rpf))
				break;
			inputs[j+init_rpf] = inputs[j-1+init_rpf];
		}

		inputs[j+init_rpf] = rpf;
	}

	if (!vsp1_gen3_vspdl_check(vsp1))
		end_rpf = vsp1->info->num_bru_inputs;

	/* Setup the RPF input pipeline for every enabled input. */
	for (i = init_rpf; i < end_rpf; ++i) {
		struct vsp1_rwpf *rpf = inputs[i];
		bool brs_use = false;

		if (!rpf) {
			vsp1->bru->inputs[i].rpf = NULL;
			if ((lif_index == 1) && (vsp1->brs))
				vsp1->brs->inputs[i].rpf = NULL;
			continue;
		}

		if ((lif_index == 1) && (vsp1->brs)) {
			vsp1->brs->inputs[i].rpf = rpf;
			rpf->brs_input = i;
			brs_use = true;
		} else {
			vsp1->bru->inputs[i].rpf = rpf;
			rpf->bru_input = i;
		}
		rpf->entity.sink_pad = i;

		dev_dbg(vsp1->dev, "%s: connecting RPF.%u to %s:%u\n",
			__func__, rpf->entity.index,
			(brs_use ? "BRS" : "BRU"), i);

		ret = vsp1_du_setup_rpf_pipe(vsp1, rpf, i);
		if (ret < 0)
			dev_err(vsp1->dev,
				"%s: failed to setup RPF.%u\n",
				__func__, rpf->entity.index);
	}

	/*
	 * If we have a writeback node attached, we use this opportunity to
	 * update the video buffers.
	 */
	if (pipe->output->video && pipe->output->video->frame_end)
		pipe->output->video->frame_end(pipe);

	/* Configure all entities in the pipeline. */
	list_for_each_entry(entity, &pipe->entities, list_pipe) {
		/* Disconnect unused RPFs from the pipeline. */
		if (entity->type == VSP1_ENTITY_RPF) {
			struct vsp1_rwpf *rpf = to_rwpf(&entity->subdev);

			if (!pipe->inputs[rpf->entity.index]) {
				vsp1_dl_list_write(pipe->dl, entity->route->reg,
						   VI6_DPR_NODE_UNUSED);
				continue;
			}
		}

		vsp1_entity_route_setup(entity, pipe, pipe->dl);

		if (entity->ops->configure) {
			entity->ops->configure(entity, pipe, pipe->dl,
					       VSP1_ENTITY_PARAMS_INIT);
			entity->ops->configure(entity, pipe, pipe->dl,
					       VSP1_ENTITY_PARAMS_RUNTIME);
			entity->ops->configure(entity, pipe, pipe->dl,
					       VSP1_ENTITY_PARAMS_PARTITION);
		}
	}

	vsp1_dl_list_commit(pipe->dl, lif_index);
	pipe->dl = NULL;

	/* Start or stop the pipeline if needed. */
	if (!vsp1->drm->num_inputs && pipe->num_inputs) {
		vsp1_write(vsp1, VI6_DISP_IRQ_STA(lif_index), 0);
		vsp1_write(vsp1, VI6_DISP_IRQ_ENB(lif_index),
					 VI6_DISP_IRQ_ENB_DSTE);
		spin_lock_irqsave(&pipe->irqlock, flags);
		vsp1_pipeline_run(pipe);
		spin_unlock_irqrestore(&pipe->irqlock, flags);
	} else if (vsp1->drm->num_inputs && !pipe->num_inputs) {
		vsp1_write(vsp1, VI6_DISP_IRQ_ENB(lif_index), 0);
		vsp1_pipeline_stop(pipe);
	}
}
EXPORT_SYMBOL_GPL(vsp1_du_atomic_flush);

int vsp1_du_map_sg(struct device *dev, struct sg_table *sgt)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct device *map_dev;

	map_dev = vsp1->fcp ? rcar_fcp_get_device(vsp1->fcp) : dev;

	return dma_map_sg(map_dev, sgt->sgl, sgt->nents, DMA_TO_DEVICE);
}
EXPORT_SYMBOL_GPL(vsp1_du_map_sg);

void vsp1_du_unmap_sg(struct device *dev, struct sg_table *sgt)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct device *map_dev;

	map_dev = vsp1->fcp ? rcar_fcp_get_device(vsp1->fcp) : dev;

	dma_unmap_sg(map_dev, sgt->sgl, sgt->nents, DMA_TO_DEVICE);
}
EXPORT_SYMBOL_GPL(vsp1_du_unmap_sg);

int vsp1_du_setup_wb(struct device *dev, u32 pixelformat, unsigned int pitch,
		      dma_addr_t mem[2], unsigned int lif_index)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe[lif_index];
	struct vsp1_rwpf *wpf = pipe->output;
	const struct vsp1_format_info *fmtinfo;
	struct vsp1_rwpf *rpf = pipe->inputs[0];
	unsigned long flags;
	int i;

	fmtinfo = vsp1_get_format_info(vsp1, pixelformat);
	if (!fmtinfo) {
		dev_err(vsp1->dev, "Unsupport pixel format %08x for RPF\n",
			pixelformat);
		return -EINVAL;
	}

	if (rpf->interlaced) {
		dev_err(vsp1->dev, "Prohibited in interlaced mode\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&pipe->irqlock, flags);

	wpf->fmtinfo = fmtinfo;
	wpf->format.num_planes = fmtinfo->planes;
	wpf->format.plane_fmt[0].bytesperline = pitch;
	wpf->format.plane_fmt[1].bytesperline = pitch;

	for (i = 0; i < wpf->format.num_planes; ++i)
		wpf->buf_addr[i] = mem[i];

	pipe->output->write_back = 3;

	spin_unlock_irqrestore(&pipe->irqlock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_setup_wb);

void vsp1_du_wait_wb(struct device *dev, u32 count, unsigned int lif_index)
{
	int ret;
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe[lif_index];

	ret = wait_event_interruptible(pipe->event_wait,
				       (pipe->output->write_back == count));
}
EXPORT_SYMBOL_GPL(vsp1_du_wait_wb);

/* -----------------------------------------------------------------------------
 * Initialization
 */

int vsp1_drm_create_links(struct vsp1_device *vsp1)
{
	const u32 flags = MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE;
	unsigned int i;
	unsigned int init_bru_num, end_bru_num;
	unsigned int init_brs_num, end_brs_num;
	int ret;

	/* VSPD instances require a BRU to perform composition and a LIF to
	 * output to the DU.
	 */
	if (!vsp1->bru || !vsp1->lif[0])
		return -ENXIO;

	if (vsp1_gen3_vspdl_check(vsp1)) {
		if (!vsp1->brs || !vsp1->lif[1])
			return -ENXIO;

		init_bru_num = 0;
		init_brs_num = vsp1->info->rpf_count -
			       vsp1->num_brs_inputs;
		end_bru_num = vsp1->info->rpf_count -
			      vsp1->num_brs_inputs;
		end_brs_num = vsp1->info->rpf_count;
	} else {
		init_bru_num = 0;
		init_brs_num = 0;
		end_bru_num = vsp1->info->rpf_count;
		end_brs_num = 0;
	}

	for (i = init_bru_num; i < end_bru_num; ++i) {
		struct vsp1_rwpf *rpf = vsp1->rpf[i];

		ret = media_create_pad_link(&rpf->entity.subdev.entity,
					    RWPF_PAD_SOURCE,
					    &vsp1->bru->entity.subdev.entity,
					    i, flags);
		if (ret < 0)
			return ret;
		rpf->entity.sink = &vsp1->bru->entity.subdev.entity;
		rpf->entity.sink_pad = i;
	}

	for (i = init_brs_num; i < end_brs_num; ++i) {
		struct vsp1_rwpf *rpf = vsp1->rpf[i];

		ret = media_create_pad_link(&rpf->entity.subdev.entity,
					    RWPF_PAD_SOURCE,
					    &vsp1->brs->entity.subdev.entity,
					    i, flags);
		if (ret < 0)
			return ret;
		rpf->entity.sink = &vsp1->brs->entity.subdev.entity;
		rpf->entity.sink_pad = i;
	}

	ret = media_create_pad_link(&vsp1->bru->entity.subdev.entity,
				    vsp1->bru->entity.source_pad,
				    &vsp1->wpf[0]->entity.subdev.entity,
				    RWPF_PAD_SINK, flags);
	if (ret < 0)
		return ret;

	vsp1->bru->entity.sink = &vsp1->wpf[0]->entity.subdev.entity;
	vsp1->bru->entity.sink_pad = RWPF_PAD_SINK;

	ret = media_create_pad_link(&vsp1->wpf[0]->entity.subdev.entity,
				    RWPF_PAD_SOURCE,
				    &vsp1->lif[0]->entity.subdev.entity,
				    LIF_PAD_SINK, flags);
	if (ret < 0)
		return ret;

	if (vsp1_gen3_vspdl_check(vsp1)) {
		ret = media_create_pad_link(
					&vsp1->brs->entity.subdev.entity,
					vsp1->brs->entity.source_pad,
					&vsp1->wpf[1]->entity.subdev.entity,
					RWPF_PAD_SINK, flags);
		if (ret < 0)
			return ret;

		vsp1->brs->entity.sink = &vsp1->wpf[1]->entity.subdev.entity;
		vsp1->brs->entity.sink_pad = RWPF_PAD_SINK;

		ret = media_create_pad_link(
					&vsp1->wpf[1]->entity.subdev.entity,
					RWPF_PAD_SOURCE,
					&vsp1->lif[1]->entity.subdev.entity,
					LIF_PAD_SINK, flags);
		if (ret < 0)
			return ret;
	}

	if (vsp1->wpf[0]->has_writeback) {
		/* Connect the video device to the WPF for Writeback support */
		ret = media_create_pad_link(&vsp1->wpf[0]->entity.subdev.entity,
				    RWPF_PAD_SOURCE,
				    &vsp1->wpf[0]->video->video.entity,
				    0, flags);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int vsp1_drm_init(struct vsp1_device *vsp1)
{
	struct vsp1_pipeline *pipe;
	unsigned int i, j, lif_cnt = 1;
	int rpf_share = -1;

	vsp1->drm = devm_kzalloc(vsp1->dev, sizeof(*vsp1->drm), GFP_KERNEL);
	if (!vsp1->drm)
		return -ENOMEM;

	if (vsp1_gen3_vspdl_check(vsp1)) {
		lif_cnt = 2;
		rpf_share = vsp1->info->rpf_count - vsp1->num_brs_inputs;
	}

	for (i = 0; i < lif_cnt; i++) {
		pipe = &vsp1->drm->pipe[i];
		vsp1_pipeline_init(pipe);

		if (i == 1) {
			list_add_tail(&vsp1->brs->entity.list_pipe,
				      &pipe->entities);
			pipe->brs = &vsp1->brs->entity;
		} else {
			list_add_tail(&vsp1->bru->entity.list_pipe,
				      &pipe->entities);
			pipe->bru = &vsp1->bru->entity;
		}
		/* The DRM pipeline is static, add entities manually. */
		for (j = 0; j < vsp1->info->rpf_count; ++j) {
			struct vsp1_rwpf *input = vsp1->rpf[j];

			if (rpf_share < 0)
				list_add_tail(&input->entity.list_pipe,
						 &pipe->entities);
			else {
				if ((i == 0) && (j < rpf_share))
					list_add_tail(&input->entity.list_pipe,
						      &pipe->entities);
				if ((i == 1) && (j >= rpf_share))
					list_add_tail(&input->entity.list_pipe,
						      &pipe->entities);
			}
		}
		list_add_tail(&vsp1->wpf[i]->entity.list_pipe,
			      &pipe->entities);
		list_add_tail(&vsp1->lif[i]->entity.list_pipe,
			      &pipe->entities);

		pipe->lif = &vsp1->lif[i]->entity;
		pipe->output = vsp1->wpf[i];
		pipe->output->pipe = pipe;
		pipe->output->write_back = 0;
		init_waitqueue_head(&pipe->event_wait);
	}

	return 0;
}

void vsp1_drm_cleanup(struct vsp1_device *vsp1)
{
}
