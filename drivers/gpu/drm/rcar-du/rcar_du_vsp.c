/*
 * rcar_du_vsp.h  --  R-Car Display Unit VSP-Based Compositor
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

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/rcar_du_drm.h>

#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/scatterlist.h>
#include <linux/videodev2.h>

#include <media/vsp1.h>

#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_vsp.h"

static void rcar_du_vsp_complete(void *private, bool completed)
{
	struct rcar_du_crtc *crtc = private;

	if (crtc->vblank_enable)
		drm_crtc_handle_vblank(&crtc->crtc);

	if (completed)
		rcar_du_crtc_finish_page_flip(crtc);
}

void rcar_du_vsp_enable(struct rcar_du_crtc *crtc)
{
	const struct drm_display_mode *mode = &crtc->crtc.state->adjusted_mode;
	struct rcar_du_device *rcdu = crtc->group->dev;
	struct vsp1_du_lif_config cfg = {
		.width = mode->hdisplay,
		.height = mode->vdisplay,
		.callback = rcar_du_vsp_complete,
		.callback_data = crtc,
	};
	struct rcar_du_plane_state state = {
		.state = {
			.crtc = &crtc->crtc,
			.crtc_x = 0,
			.crtc_y = 0,
			.crtc_w = mode->hdisplay,
			.crtc_h = mode->vdisplay,
			.src_x = 0,
			.src_y = 0,
			.src_w = mode->hdisplay << 16,
			.src_h = mode->vdisplay << 16,
			.zpos = 0,
		},
		.format = rcar_du_format_info(DRM_FORMAT_ARGB8888),
		.source = RCAR_DU_PLANE_VSPD1,
		.alpha = 255,
		.colorkey = 0,
	};

	if (rcdu->info->gen >= 3)
		state.hwindex = (crtc->index % 2) ? 2 : 0;
	else
		state.hwindex = crtc->index % 2;

	__rcar_du_plane_setup(crtc->group, &state);

	/*
	 * Ensure that the plane source configuration takes effect by requesting
	 * a restart of the group. See rcar_du_plane_atomic_update() for a more
	 * detailed explanation.
	 *
	 * TODO: Check whether this is still needed on Gen3.
	 */
	crtc->group->need_restart = true;

	vsp1_du_setup_lif(crtc->vsp->vsp, crtc->vsp_pipe, &cfg);
}

void rcar_du_vsp_disable(struct rcar_du_crtc *crtc)
{
	vsp1_du_setup_lif(crtc->vsp->vsp, crtc->vsp_pipe, NULL);
}

void rcar_du_vsp_atomic_begin(struct rcar_du_crtc *crtc)
{
	vsp1_du_atomic_begin(crtc->vsp->vsp, crtc->vsp_pipe);
}

void rcar_du_vsp_atomic_flush(struct rcar_du_crtc *crtc)
{
	vsp1_du_atomic_flush(crtc->vsp->vsp, crtc->vsp_pipe);
}

/* Keep the two tables in sync. */
static const u32 formats_kms[] = {
	DRM_FORMAT_RGB332,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV61,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_YVU444,
};

static const u32 formats_v4l2[] = {
	V4L2_PIX_FMT_RGB332,
	V4L2_PIX_FMT_ARGB444,
	V4L2_PIX_FMT_XRGB444,
	V4L2_PIX_FMT_ARGB555,
	V4L2_PIX_FMT_XRGB555,
	V4L2_PIX_FMT_RGB565,
	V4L2_PIX_FMT_RGB24,
	V4L2_PIX_FMT_BGR24,
	V4L2_PIX_FMT_ARGB32,
	V4L2_PIX_FMT_XRGB32,
	V4L2_PIX_FMT_ABGR32,
	V4L2_PIX_FMT_XBGR32,
	V4L2_PIX_FMT_UYVY,
	V4L2_PIX_FMT_YUYV,
	V4L2_PIX_FMT_YVYU,
	V4L2_PIX_FMT_NV12M,
	V4L2_PIX_FMT_NV21M,
	V4L2_PIX_FMT_NV16M,
	V4L2_PIX_FMT_NV61M,
	V4L2_PIX_FMT_YUV420M,
	V4L2_PIX_FMT_YVU420M,
	V4L2_PIX_FMT_YUV422M,
	V4L2_PIX_FMT_YVU422M,
	V4L2_PIX_FMT_YUV444M,
	V4L2_PIX_FMT_YVU444M,
};

static void rcar_du_vsp_plane_setup(struct rcar_du_vsp_plane *plane)
{
	struct rcar_du_vsp_plane_state *state =
		to_rcar_vsp_plane_state(plane->plane.state);
	struct rcar_du_crtc *crtc = to_rcar_crtc(state->state.crtc);
	struct drm_framebuffer *fb = plane->plane.state->fb;
	struct vsp1_du_atomic_config cfg = {
		.pixelformat = 0,
		.pitch = fb->pitches[0],
		.alpha = state->alpha,
		.zpos = state->state.zpos,
		.colorkey = state->colorkey & RCAR_DU_COLORKEY_COLOR_MASK,
		.colorkey_en =
			((state->colorkey & RCAR_DU_COLORKEY_EN_MASK) != 0),
		.colorkey_alpha =
			(state->colorkey_alpha & RCAR_DU_COLORKEY_ALPHA_MASK),
	};
	unsigned int i;

	if (plane->plane.state->crtc->mode.flags
				 & DRM_MODE_FLAG_INTERLACE)
		cfg.interlaced = true;
	else
		cfg.interlaced = false;

	cfg.src.left = state->state.src_x >> 16;
	cfg.src.top = state->state.src_y >> 16;
	cfg.src.width = state->state.src_w >> 16;
	cfg.src.height = state->state.src_h >> 16;

	cfg.dst.left = state->state.crtc_x;
	cfg.dst.top = state->state.crtc_y;
	cfg.dst.width = state->state.crtc_w;
	cfg.dst.height = state->state.crtc_h;

	for (i = 0; i < state->format->planes; ++i)
		cfg.mem[i] = sg_dma_address(state->sg_tables[i].sgl)
			   + fb->offsets[i];

	for (i = 0; i < ARRAY_SIZE(formats_kms); ++i) {
		if (formats_kms[i] == state->format->fourcc) {
			cfg.pixelformat = formats_v4l2[i];
			break;
		}
	}

	vsp1_du_atomic_update(plane->vsp->vsp, crtc->vsp_pipe,
			      plane->index, &cfg);
}

static int rcar_du_vsp_plane_prepare_fb(struct drm_plane *plane,
					struct drm_plane_state *state)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);
	struct rcar_du_vsp *vsp = to_rcar_vsp_plane(plane)->vsp;
	struct rcar_du_device *rcdu = vsp->dev;
	unsigned int i;
	int ret;

	if (!state->fb)
		return 0;

	for (i = 0; i < rstate->format->planes; ++i) {
		struct drm_gem_cma_object *gem =
			drm_fb_cma_get_gem_obj(state->fb, i);
		struct sg_table *sgt = &rstate->sg_tables[i];

		ret = dma_get_sgtable(rcdu->dev, sgt, gem->vaddr, gem->paddr,
				      gem->base.size);
		if (ret)
			goto fail;

		ret = vsp1_du_map_sg(vsp->vsp, sgt);
		if (!ret) {
			sg_free_table(sgt);
			ret = -ENOMEM;
			goto fail;
		}
	}

	ret = drm_gem_fb_prepare_fb(plane, state);
	if (ret)
		goto fail;

	return 0;

fail:
	while (i--) {
		struct sg_table *sgt = &rstate->sg_tables[i];

		vsp1_du_unmap_sg(vsp->vsp, sgt);
		sg_free_table(sgt);
	}

	return ret;
}

static void rcar_du_vsp_plane_cleanup_fb(struct drm_plane *plane,
					 struct drm_plane_state *state)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);
	struct rcar_du_vsp *vsp = to_rcar_vsp_plane(plane)->vsp;
	unsigned int i;

	if (!state->fb)
		return;

	for (i = 0; i < rstate->format->planes; ++i) {
		struct sg_table *sgt = &rstate->sg_tables[i];

		vsp1_du_unmap_sg(vsp->vsp, sgt);
		sg_free_table(sgt);
	}
}

static int rcar_du_vsp_plane_atomic_check(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);
	struct rcar_du_vsp_plane *rplane = to_rcar_vsp_plane(plane);
	struct rcar_du_device *rcdu = rplane->vsp->dev;
	int hdisplay, vdisplay;

	if (!state->fb || !state->crtc) {
		rstate->format = NULL;
		return 0;
	}

	hdisplay = state->crtc->mode.hdisplay;
	vdisplay = state->crtc->mode.vdisplay;

	if ((hdisplay > 0 && vdisplay > 0) &&
	    state->plane->type == DRM_PLANE_TYPE_OVERLAY &&
	    (((state->crtc_w + state->crtc_x) > hdisplay) ||
	    ((state->crtc_h + state->crtc_y) > vdisplay))) {
		dev_err(rcdu->dev,
			"%s: specify (%dx%d) + (%d, %d) < (%dx%d).\n",
			__func__, state->crtc_w, state->crtc_h, state->crtc_x,
			state->crtc_y, hdisplay, vdisplay);
		return -EINVAL;
	}

	if (state->src_w >> 16 != state->crtc_w ||
	    state->src_h >> 16 != state->crtc_h) {
		dev_dbg(rcdu->dev, "%s: scaling not supported\n", __func__);
		return -EINVAL;
	}

	rstate->format = rcar_du_format_info(state->fb->format->format);
	if (rstate->format == NULL) {
		dev_dbg(rcdu->dev, "%s: unsupported format %08x\n", __func__,
			state->fb->format->format);
		return -EINVAL;
	}

	return 0;
}

static void rcar_du_vsp_plane_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct rcar_du_vsp_plane *rplane = to_rcar_vsp_plane(plane);
	struct rcar_du_crtc *crtc = to_rcar_crtc(old_state->crtc);

	if (plane->state->crtc)
		rcar_du_vsp_plane_setup(rplane);
	else
		vsp1_du_atomic_update(rplane->vsp->vsp, crtc->vsp_pipe,
				      rplane->index, NULL);
}

static const struct drm_plane_helper_funcs rcar_du_vsp_plane_helper_funcs = {
	.prepare_fb = rcar_du_vsp_plane_prepare_fb,
	.cleanup_fb = rcar_du_vsp_plane_cleanup_fb,
	.atomic_check = rcar_du_vsp_plane_atomic_check,
	.atomic_update = rcar_du_vsp_plane_atomic_update,
};

static struct drm_plane_state *
rcar_du_vsp_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct rcar_du_vsp_plane_state *state;
	struct rcar_du_vsp_plane_state *copy;

	if (WARN_ON(!plane->state))
		return NULL;

	state = to_rcar_vsp_plane_state(plane->state);
	copy = kmemdup(state, sizeof(*state), GFP_KERNEL);
	if (copy == NULL)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->state);

	return &copy->state;
}

static void rcar_du_vsp_plane_atomic_destroy_state(struct drm_plane *plane,
						   struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_rcar_vsp_plane_state(state));
}

static void rcar_du_vsp_plane_reset(struct drm_plane *plane)
{
	struct rcar_du_vsp_plane_state *state;

	if (plane->state) {
		rcar_du_vsp_plane_atomic_destroy_state(plane, plane->state);
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return;

	state->alpha = 255;
	state->colorkey = RCAR_DU_COLORKEY_NONE;
	state->colorkey_alpha = 0;
	state->state.zpos = plane->type == DRM_PLANE_TYPE_PRIMARY ? 0 : 1;

	plane->state = &state->state;
	plane->state->plane = plane;
}

static int rcar_du_vsp_plane_atomic_set_property(struct drm_plane *plane,
	struct drm_plane_state *state, struct drm_property *property,
	uint64_t val)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);
	struct rcar_du_device *rcdu = to_rcar_vsp_plane(plane)->vsp->dev;

	if (property == rcdu->props.alpha)
		rstate->alpha = val;
	else if (property == rcdu->props.colorkey)
		rstate->colorkey = val;
	else if (property == rcdu->props.colorkey_alpha)
		rstate->colorkey_alpha = val;
	else
		return -EINVAL;

	return 0;
}

static int rcar_du_vsp_plane_atomic_get_property(struct drm_plane *plane,
	const struct drm_plane_state *state, struct drm_property *property,
	uint64_t *val)
{
	const struct rcar_du_vsp_plane_state *rstate =
		container_of(state, const struct rcar_du_vsp_plane_state, state);
	struct rcar_du_device *rcdu = to_rcar_vsp_plane(plane)->vsp->dev;

	if (property == rcdu->props.alpha)
		*val = rstate->alpha;
	else if (property == rcdu->props.colorkey)
		*val = rstate->colorkey;
	else if (property == rcdu->props.colorkey_alpha)
		*val = rstate->colorkey_alpha;
	else
		return -EINVAL;

	return 0;
}

int rcar_du_vsp_write_back(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	int ret;
	struct rcar_du_screen_shot *sh = (struct rcar_du_screen_shot *)data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	struct rcar_du_crtc *rcrtc;
	struct rcar_du_device *rcdu;
	const struct drm_display_mode *mode;
	u32 pixelformat, bpp;
	unsigned int pitch;
	dma_addr_t mem[3];

	obj = drm_mode_object_find(dev, sh->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj)
		return -EINVAL;

	crtc = obj_to_crtc(obj);
	rcrtc = to_rcar_crtc(crtc);
	rcdu = rcrtc->group->dev;
	mode = &rcrtc->crtc.state->adjusted_mode;

	switch (sh->fmt) {
	case DRM_FORMAT_RGB565:
		bpp = 16;
		pixelformat = V4L2_PIX_FMT_RGB565;
		break;
	case DRM_FORMAT_ARGB1555:
		bpp = 16;
		pixelformat = V4L2_PIX_FMT_ARGB555;
		break;
	case DRM_FORMAT_ARGB8888:
		bpp = 32;
		pixelformat = V4L2_PIX_FMT_ABGR32;
		break;
	default:
		dev_err(rcdu->dev, "specified format is not supported.\n");
		return -EINVAL;
	}

	pitch = mode->hdisplay * bpp / 8;

	mem[0] = sh->buff;
	mem[1] = 0;
	mem[2] = 0;

	if (sh->width != mode->hdisplay ||
	    sh->height != mode->vdisplay)
		return -EINVAL;

	if ((pitch * mode->vdisplay) > sh->buff_len)
		return -EINVAL;

	ret = vsp1_du_setup_wb(rcrtc->vsp->vsp, pixelformat, pitch, mem,
			       rcrtc->vsp_pipe);
	if (ret != 0)
		return ret;

	ret = vsp1_du_wait_wb(rcrtc->vsp->vsp, WB_STAT_CATP_SET,
			      rcrtc->vsp_pipe);
	if (ret != 0)
		return ret;

	ret = rcar_du_async_commit(dev, crtc);
	if (ret != 0)
		return ret;

	ret = vsp1_du_wait_wb(rcrtc->vsp->vsp, WB_STAT_CATP_START,
			      rcrtc->vsp_pipe);
	if (ret != 0)
		return ret;

	ret = rcar_du_async_commit(dev, crtc);
	if (ret != 0)
		return ret;

	ret = vsp1_du_wait_wb(rcrtc->vsp->vsp, WB_STAT_CATP_DONE,
			      rcrtc->vsp_pipe);
	if (ret != 0)
		return ret;

	return ret;
}

int rcar_du_set_vmute(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct rcar_du_vmute *vmute =
		(struct rcar_du_vmute *)data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	struct rcar_du_crtc *rcrtc;
	int ret = 0;

	dev_dbg(dev->dev, "CRTC[%d], display:%s\n",
		vmute->crtc_id, vmute->on ? "off" : "on");

	obj = drm_mode_object_find(dev, vmute->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj)
		return -EINVAL;

	crtc = obj_to_crtc(obj);
	rcrtc = to_rcar_crtc(crtc);

	vsp1_du_if_set_mute(rcrtc->vsp->vsp, vmute->on, rcrtc->vsp_pipe);

	ret = rcar_du_async_commit(dev, crtc);

	return ret;
}

static const struct drm_plane_funcs rcar_du_vsp_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = rcar_du_vsp_plane_reset,
	.destroy = drm_plane_cleanup,
	.atomic_duplicate_state = rcar_du_vsp_plane_atomic_duplicate_state,
	.atomic_destroy_state = rcar_du_vsp_plane_atomic_destroy_state,
	.atomic_set_property = rcar_du_vsp_plane_atomic_set_property,
	.atomic_get_property = rcar_du_vsp_plane_atomic_get_property,
};

int rcar_du_vsp_init(struct rcar_du_vsp *vsp, struct device_node *np,
		     unsigned int crtcs)
{
	struct rcar_du_device *rcdu = vsp->dev;
	struct platform_device *pdev;
	unsigned int num_crtcs = hweight32(crtcs);
	unsigned int i;
	int ret;

	/* Find the VSP device and initialize it. */
	pdev = of_find_device_by_node(np);
	if (!pdev)
		return -ENXIO;

	vsp->vsp = &pdev->dev;

	ret = vsp1_du_init(vsp->vsp);
	if (ret < 0)
		return ret;

	 /*
	  * The VSP2D (Gen3) has 5 RPFs, but the VSP1D (Gen2) is limited to
	  * 4 RPFs.
	  */
	vsp->num_planes = rcdu->info->gen >= 3 ? 5 : 4;

	vsp->planes = devm_kcalloc(rcdu->dev, vsp->num_planes,
				   sizeof(*vsp->planes), GFP_KERNEL);
	if (!vsp->planes)
		return -ENOMEM;

	for (i = 0; i < vsp->num_planes; ++i) {
		enum drm_plane_type type = i < num_crtcs
					 ? DRM_PLANE_TYPE_PRIMARY
					 : DRM_PLANE_TYPE_OVERLAY;
		struct rcar_du_vsp_plane *plane = &vsp->planes[i];

		plane->vsp = vsp;
		plane->index = i;

		/* Fix possible crtcs for plane when using VSPDL */
		if (rcdu->vspdl_fix && vsp->index == VSPDL_CH) {
			u32 pair_ch;
			enum rcar_du_output pair_con = RCAR_DU_OUTPUT_DPAD0;

			pair_ch = rcdu->info->routes[pair_con].possible_crtcs;

			if (rcdu->brs_num == 0) {
				crtcs = BIT(0);
				if (i > 0)
					type = DRM_PLANE_TYPE_OVERLAY;
			} else if (rcdu->brs_num == 1) {
				if (type == DRM_PLANE_TYPE_PRIMARY)
					i == 1 ? (crtcs = pair_ch) :
						 (crtcs = BIT(0));
				else
					crtcs = BIT(0);
			} else {
				if (type == DRM_PLANE_TYPE_PRIMARY)
					i == 1 ? (crtcs = pair_ch) :
						 (crtcs = BIT(0));
				else
					i == 4 ? (crtcs = pair_ch) :
						 (crtcs = BIT(0));
			}
		}

		ret = drm_universal_plane_init(rcdu->ddev, &plane->plane, crtcs,
					       &rcar_du_vsp_plane_funcs,
					       formats_kms,
					       ARRAY_SIZE(formats_kms),
					       NULL, type, NULL);
		if (ret < 0)
			return ret;

		drm_plane_helper_add(&plane->plane,
				     &rcar_du_vsp_plane_helper_funcs);

		if (type == DRM_PLANE_TYPE_PRIMARY)
			continue;

		drm_object_attach_property(&plane->plane.base,
					   rcdu->props.alpha, 255);
		drm_object_attach_property(&plane->plane.base,
					   rcdu->props.colorkey,
					   RCAR_DU_COLORKEY_NONE);
		if (rcdu->props.colorkey_alpha)
			drm_object_attach_property(&plane->plane.base,
						   rcdu->props.colorkey_alpha,
						   0);
		drm_plane_create_zpos_property(&plane->plane, 1, 1,
					       vsp->num_planes - 1);
	}

	return 0;
}
