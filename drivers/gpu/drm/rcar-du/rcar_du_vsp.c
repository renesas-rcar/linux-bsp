// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_du_vsp.h  --  R-Car Display Unit VSP-Based Compositor
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>
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
#include "rcar_du_writeback.h"

static void rcar_du_vsp_complete(void *private, unsigned int status, u32 crc)
{
	struct rcar_du_crtc *crtc = private;

	if (crtc->vblank_enable)
		drm_crtc_handle_vblank(&crtc->crtc);

	if (status & VSP1_DU_STATUS_COMPLETE)
		rcar_du_crtc_finish_page_flip(crtc);
	if (status & VSP1_DU_STATUS_WRITEBACK)
		rcar_du_writeback_complete(crtc);

	drm_crtc_add_crc_entry(&crtc->crtc, false, 0, &crc);
}

void rcar_du_vsp_enable(struct rcar_du_crtc *crtc)
{
	const struct drm_display_mode *mode = &crtc->crtc.state->adjusted_mode;
	struct rcar_du_device *rcdu = crtc->dev;
	struct vsp1_du_lif_config cfg = {
		.width = mode->hdisplay,
		.height = mode->vdisplay,
		.interlaced = mode->flags & DRM_MODE_FLAG_INTERLACE,
		.callback = rcar_du_vsp_complete,
		.callback_data = crtc,
	};
	struct rcar_du_plane_state state = {
		.state = {
			.crtc = &crtc->crtc,
			.dst.x1 = 0,
			.dst.y1 = 0,
			.dst.x2 = mode->hdisplay,
			.dst.y2 = mode->vdisplay,
			.src.x1 = 0,
			.src.y1 = 0,
			.src.x2 = mode->hdisplay << 16,
			.src.y2 = mode->vdisplay << 16,
			.zpos = 0,
		},
		.format = rcar_du_format_info(DRM_FORMAT_ARGB8888),
		.source = RCAR_DU_PLANE_VSPD1,
		.alpha = 255,
		.colorkey = 0,
	};

	if (rcar_du_has(rcdu, RCAR_DU_FEATURE_R8A779A0_REGS))
		state.format = rcar_du_format_info(DRM_FORMAT_XRGB8888);

	if (rcdu->info->gen >= 3)
		state.hwindex = (crtc->index % 2) ? 2 : 0;
	else
		state.hwindex = crtc->index % 2;

	__rcar_du_plane_setup(crtc->group, &state);

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
	struct vsp1_du_atomic_pipe_config cfg = { { 0, } };
	struct rcar_du_crtc_state *state;

	state = to_rcar_crtc_state(crtc->crtc.state);
	cfg.crc = state->crc;

	rcar_du_writeback_setup(crtc, &cfg.writeback);

	vsp1_du_atomic_flush(crtc->vsp->vsp, crtc->vsp_pipe, &cfg);
}

static const u32 rcar_du_vsp_formats[] = {
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
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_RGBA1010102,
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

static void rcar_du_vsp_plane_setup(struct rcar_du_vsp_plane *plane)
{
	struct rcar_du_vsp_plane_state *state =
		to_rcar_vsp_plane_state(plane->plane.state);
	struct rcar_du_crtc *crtc = to_rcar_crtc(state->state.crtc);
	struct drm_framebuffer *fb = plane->plane.state->fb;
	const struct rcar_du_format_info *format;
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

	cfg.src.left = state->state.src.x1 >> 16;
	cfg.src.top = state->state.src.y1 >> 16;
	cfg.src.width = drm_rect_width(&state->state.src) >> 16;
	cfg.src.height = drm_rect_height(&state->state.src) >> 16;

	cfg.dst.left = state->state.dst.x1;
	cfg.dst.top = state->state.dst.y1;
	cfg.dst.width = drm_rect_width(&state->state.dst);
	cfg.dst.height = drm_rect_height(&state->state.dst);

	for (i = 0; i < state->format->planes; ++i)
		cfg.mem[i] = sg_dma_address(state->sg_tables[i].sgl)
			   + fb->offsets[i];

	format = rcar_du_format_info(state->format->fourcc);
	cfg.pixelformat = format->v4l2;

	vsp1_du_atomic_update(plane->vsp->vsp, crtc->vsp_pipe,
			      plane->index, &cfg);
}

int rcar_du_vsp_map_fb(struct rcar_du_vsp *vsp, struct drm_framebuffer *fb,
		       struct sg_table sg_tables[3])
{
	struct rcar_du_device *rcdu = vsp->dev;
	unsigned int i, j;
	int ret;

	for (i = 0; i < fb->format->num_planes; ++i) {
		struct drm_gem_cma_object *gem = drm_fb_cma_get_gem_obj(fb, i);
		struct sg_table *sgt = &sg_tables[i];

		if (gem->sgt) {
			struct scatterlist *src;
			struct scatterlist *dst;

			/*
			 * If the GEM buffer has a scatter gather table, it has
			 * been imported from a dma-buf and has no physical
			 * address as it might not be physically contiguous.
			 * Copy the original scatter gather table to map it to
			 * the VSP.
			 */
			ret = sg_alloc_table(sgt, gem->sgt->orig_nents,
					     GFP_KERNEL);
			if (ret)
				goto fail;

			src = gem->sgt->sgl;
			dst = sgt->sgl;
			for (j = 0; j < gem->sgt->orig_nents; ++j) {
				sg_set_page(dst, sg_page(src), src->length,
					    src->offset);
				src = sg_next(src);
				dst = sg_next(dst);
			}
		} else {
			ret = dma_get_sgtable(rcdu->dev, sgt, gem->vaddr,
					      gem->paddr, gem->base.size);
			if (ret)
				goto fail;
		}

		ret = vsp1_du_map_sg(vsp->vsp, sgt);
		if (ret) {
			sg_free_table(sgt);
			goto fail;
		}
	}

	return 0;

fail:
	while (i--) {
		struct sg_table *sgt = &sg_tables[i];

		vsp1_du_unmap_sg(vsp->vsp, sgt);
		sg_free_table(sgt);
	}

	return ret;
}

static int rcar_du_vsp_plane_prepare_fb(struct drm_plane *plane,
					struct drm_plane_state *state)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);
	struct rcar_du_vsp *vsp = to_rcar_vsp_plane(plane)->vsp;
	int ret;

	/*
	 * There's no need to prepare (and unprepare) the framebuffer when the
	 * plane is not visible, as it will not be displayed.
	 */
	if (!state->visible)
		return 0;

	ret = rcar_du_vsp_map_fb(vsp, state->fb, rstate->sg_tables);
	if (ret < 0)
		return ret;

	return drm_gem_fb_prepare_fb(plane, state);
}

void rcar_du_vsp_unmap_fb(struct rcar_du_vsp *vsp, struct drm_framebuffer *fb,
			  struct sg_table sg_tables[3])
{
	unsigned int i;

	for (i = 0; i < fb->format->num_planes; ++i) {
		struct sg_table *sgt = &sg_tables[i];

		vsp1_du_unmap_sg(vsp->vsp, sgt);
		sg_free_table(sgt);
	}
}

static void rcar_du_vsp_plane_cleanup_fb(struct drm_plane *plane,
					 struct drm_plane_state *state)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);
	struct rcar_du_vsp *vsp = to_rcar_vsp_plane(plane)->vsp;

	if (!state->visible)
		return;

	rcar_du_vsp_unmap_fb(vsp, state->fb, rstate->sg_tables);
}

static int rcar_du_vsp_plane_atomic_check(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);

	return __rcar_du_plane_atomic_check(plane, state, &rstate->format);
}

static void rcar_du_vsp_plane_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct rcar_du_vsp_plane *rplane = to_rcar_vsp_plane(plane);
	struct rcar_du_crtc *crtc = to_rcar_crtc(old_state->crtc);

	if (plane->state->visible)
		rcar_du_vsp_plane_setup(rplane);
	else if (old_state->crtc)
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
	struct rcar_du_vsp_plane_state *copy;

	if (WARN_ON(!plane->state))
		return NULL;

	copy = kzalloc(sizeof(*copy), GFP_KERNEL);
	if (copy == NULL)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->state);
	copy->alpha = to_rcar_vsp_plane_state(plane->state)->alpha;
	copy->colorkey = to_rcar_vsp_plane_state(plane->state)->colorkey;
	copy->colorkey_alpha = to_rcar_vsp_plane_state(plane->state)->colorkey_alpha;

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

	__drm_atomic_helper_plane_reset(plane, &state->state);

	state->alpha = 255;
	state->colorkey = RCAR_DU_COLORKEY_NONE;
	state->colorkey_alpha = 0;
	state->state.zpos = plane->type == DRM_PLANE_TYPE_PRIMARY ? 0 : 1;

	plane->state = &state->state;
	plane->state->plane = plane;
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

	obj = drm_mode_object_find(dev, file_priv, sh->crtc_id,
				   DRM_MODE_OBJECT_CRTC);
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

	obj = drm_mode_object_find(dev, file_priv, vmute->crtc_id,
				   DRM_MODE_OBJECT_CRTC);
	if (!obj)
		return -EINVAL;

	crtc = obj_to_crtc(obj);
	rcrtc = to_rcar_crtc(crtc);

	vsp1_du_if_set_mute(rcrtc->vsp->vsp, vmute->on, rcrtc->vsp_pipe);

	ret = rcar_du_async_commit(dev, crtc);

	return ret;
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

static void rcar_du_vsp_cleanup(struct drm_device *dev, void *res)
{
	struct rcar_du_vsp *vsp = res;

	put_device(vsp->vsp);
}

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

	ret = drmm_add_action(rcdu->ddev, rcar_du_vsp_cleanup, vsp);
	if (ret < 0)
		return ret;

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
					       rcar_du_vsp_formats,
					       ARRAY_SIZE(rcar_du_vsp_formats),
					       NULL, type, NULL);
		if (ret < 0)
			return ret;

		drm_plane_helper_add(&plane->plane,
				     &rcar_du_vsp_plane_helper_funcs);

		if (type == DRM_PLANE_TYPE_PRIMARY) {
			drm_plane_create_zpos_immutable_property(&plane->plane,
								 0);
		} else {
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
	}

	return 0;
}
