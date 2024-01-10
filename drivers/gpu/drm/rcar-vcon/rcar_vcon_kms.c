// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_vcon_kms.c  --  R-Car Video Interface Converter Mode Setting
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 *
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include <linux/device.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/wait.h>

#include "rcar_vcon_drv.h"
#include "rcar_vcon_kms.h"
#include "rcar_vcon_crtc.h"
#include "rcar_vcon_encoder.h"
#include "rcar_vcon_vsp.h"
#include "rcar_vcon_writeback.h"

/* -----------------------------------------------------------------------------
 * Format helpers
 */

static const struct rcar_vcon_format_info rcar_vcon_format_infos[] = {
	{
		.fourcc = DRM_FORMAT_RGB565,
		.v4l2 = V4L2_PIX_FMT_RGB565,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_ARGB1555,
		.v4l2 = V4L2_PIX_FMT_ARGB555,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_XRGB1555,
		.v4l2 = V4L2_PIX_FMT_XRGB555,
		.bpp = 16,
		.planes = 1,
	}, {
		.fourcc = DRM_FORMAT_XRGB8888,
		.v4l2 = V4L2_PIX_FMT_XBGR32,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_ARGB8888,
		.v4l2 = V4L2_PIX_FMT_ABGR32,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_UYVY,
		.v4l2 = V4L2_PIX_FMT_UYVY,
		.bpp = 16,
		.planes = 1,
		.hsub = 2,
	}, {
		.fourcc = DRM_FORMAT_YUYV,
		.v4l2 = V4L2_PIX_FMT_YUYV,
		.bpp = 16,
		.planes = 1,
		.hsub = 2,
	}, {
		.fourcc = DRM_FORMAT_NV12,
		.v4l2 = V4L2_PIX_FMT_NV12M,
		.bpp = 12,
		.planes = 2,
		.hsub = 2,
	}, {
		.fourcc = DRM_FORMAT_NV21,
		.v4l2 = V4L2_PIX_FMT_NV21M,
		.bpp = 12,
		.planes = 2,
		.hsub = 2,
	}, {
		.fourcc = DRM_FORMAT_NV16,
		.v4l2 = V4L2_PIX_FMT_NV16M,
		.bpp = 16,
		.planes = 2,
		.hsub = 2,
	},
	{
		.fourcc = DRM_FORMAT_RGB332,
		.v4l2 = V4L2_PIX_FMT_RGB332,
		.bpp = 8,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_ARGB4444,
		.v4l2 = V4L2_PIX_FMT_ARGB444,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_XRGB4444,
		.v4l2 = V4L2_PIX_FMT_XRGB444,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_RGBA4444,
		.v4l2 = V4L2_PIX_FMT_RGBA444,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_RGBX4444,
		.v4l2 = V4L2_PIX_FMT_RGBX444,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_ABGR4444,
		.v4l2 = V4L2_PIX_FMT_ABGR444,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_XBGR4444,
		.v4l2 = V4L2_PIX_FMT_XBGR444,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_BGRA4444,
		.v4l2 = V4L2_PIX_FMT_BGRA444,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_BGRX4444,
		.v4l2 = V4L2_PIX_FMT_BGRX444,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_RGBA5551,
		.v4l2 = V4L2_PIX_FMT_RGBA555,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_RGBX5551,
		.v4l2 = V4L2_PIX_FMT_RGBX555,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_ABGR1555,
		.v4l2 = V4L2_PIX_FMT_ABGR555,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_XBGR1555,
		.v4l2 = V4L2_PIX_FMT_XBGR555,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_BGRA5551,
		.v4l2 = V4L2_PIX_FMT_BGRA555,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_BGRX5551,
		.v4l2 = V4L2_PIX_FMT_BGRX555,
		.bpp = 16,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_BGR888,
		.v4l2 = V4L2_PIX_FMT_RGB24,
		.bpp = 24,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_RGB888,
		.v4l2 = V4L2_PIX_FMT_BGR24,
		.bpp = 24,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_RGBA8888,
		.v4l2 = V4L2_PIX_FMT_BGRA32,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_RGBX8888,
		.v4l2 = V4L2_PIX_FMT_BGRX32,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_ABGR8888,
		.v4l2 = V4L2_PIX_FMT_RGBA32,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_XBGR8888,
		.v4l2 = V4L2_PIX_FMT_RGBX32,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_BGRA8888,
		.v4l2 = V4L2_PIX_FMT_ARGB32,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_BGRX8888,
		.v4l2 = V4L2_PIX_FMT_XRGB32,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_XRGB2101010,
		.v4l2 = V4L2_PIX_FMT_RGB10,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_ARGB2101010,
		.v4l2 = V4L2_PIX_FMT_A2RGB10,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_RGBA1010102,
		.v4l2 = V4L2_PIX_FMT_RGB10A2,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_YVYU,
		.v4l2 = V4L2_PIX_FMT_YVYU,
		.bpp = 16,
		.planes = 1,
		.hsub = 2,
	}, {
		.fourcc = DRM_FORMAT_NV61,
		.v4l2 = V4L2_PIX_FMT_NV61M,
		.bpp = 16,
		.planes = 2,
		.hsub = 2,
	}, {
		.fourcc = DRM_FORMAT_YUV420,
		.v4l2 = V4L2_PIX_FMT_YUV420M,
		.bpp = 12,
		.planes = 3,
		.hsub = 2,
	}, {
		.fourcc = DRM_FORMAT_YVU420,
		.v4l2 = V4L2_PIX_FMT_YVU420M,
		.bpp = 12,
		.planes = 3,
		.hsub = 2,
	}, {
		.fourcc = DRM_FORMAT_YUV422,
		.v4l2 = V4L2_PIX_FMT_YUV422M,
		.bpp = 16,
		.planes = 3,
		.hsub = 2,
	}, {
		.fourcc = DRM_FORMAT_YVU422,
		.v4l2 = V4L2_PIX_FMT_YVU422M,
		.bpp = 16,
		.planes = 3,
		.hsub = 2,
	}, {
		.fourcc = DRM_FORMAT_YUV444,
		.v4l2 = V4L2_PIX_FMT_YUV444M,
		.bpp = 24,
		.planes = 3,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_YVU444,
		.v4l2 = V4L2_PIX_FMT_YVU444M,
		.bpp = 24,
		.planes = 3,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_Y210,
		.v4l2 = V4L2_PIX_FMT_Y210,
		.bpp = 32,
		.planes = 1,
		.hsub = 2,
	},
};

const struct rcar_vcon_format_info *rcar_vcon_format_info(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rcar_vcon_format_infos); ++i) {
		if (rcar_vcon_format_infos[i].fourcc == fourcc)
			return &rcar_vcon_format_infos[i];
	}

	return NULL;
}

/* -----------------------------------------------------------------------------
 * Frame buffer
 */

int rcar_vcon_dumb_create(struct drm_file *file, struct drm_device *dev,
			  struct drm_mode_create_dumb *args)
{
	unsigned int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	unsigned int align;

	/* As the current TS v0.32, bit depth is 8 and no align information */
	align = args->bpp / 8;

	args->pitch = roundup(min_pitch, align);

	return drm_gem_cma_dumb_create_internal(file, dev, args);
}

static struct drm_framebuffer *
rcar_vcon_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		    const struct drm_mode_fb_cmd2 *mode_cmd)
{
	return drm_gem_fb_create(dev, file_priv, mode_cmd);
}

/* -----------------------------------------------------------------------------
 * Atomic Check and Update
 */
static void rcar_vcon_atomic_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;

	/* Apply the atomic update. */
	drm_atomic_helper_commit_modeset_disables(dev, old_state);
	drm_atomic_helper_commit_planes(dev, old_state, DRM_PLANE_COMMIT_ACTIVE_ONLY);
	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_commit_hw_done(old_state);
	drm_atomic_helper_wait_for_flip_done(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);
}

int rcar_vcon_async_commit(struct drm_device *dev, struct drm_crtc *crtc)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_state *state;
	int ret;

	drm_modeset_lock_all(dev);

	state = drm_atomic_state_alloc(dev);
	if (!state) {
		ret = -ENOMEM;
		goto err;
	}

	crtc_state = drm_atomic_helper_crtc_duplicate_state(crtc);
	if (!crtc_state) {
		ret = -ENOMEM;
		goto err;
	}

	state->crtcs->state = crtc_state;
	state->crtcs->old_state = crtc->state;
	state->crtcs->new_state = crtc_state;
	state->crtcs->ptr = crtc;
	crtc_state->state = state;
	crtc_state->active = true;

	state->acquire_ctx = config->acquire_ctx;
	ret = drm_atomic_commit(state);
	drm_atomic_state_put(state);
err:
	drm_modeset_unlock_all(dev);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

static const struct drm_mode_config_helper_funcs rcar_vcon_mode_config_helper = {
	.atomic_commit_tail = rcar_vcon_atomic_commit_tail,
};

static const struct drm_mode_config_funcs rcar_vcon_mode_config_funcs = {
	.fb_create = rcar_vcon_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int rcar_vcon_encoders_init_one(struct rcar_vcon_device *rvcon,
				       enum rcar_vcon_output output,
				       struct of_endpoint *ep)
{
	struct device_node *entity;
	int ret;

	/* Locate the connected entity and initialize the encoder. */
	entity = of_graph_get_remote_port_parent(ep->local_node);
	if (!entity) {
		dev_dbg(rvcon->dev, "unconnected endpoint %pOF, skipping\n", ep->local_node);
		return -ENODEV;
	}

	if (!of_device_is_available(entity)) {
		dev_dbg(rvcon->dev, "connected entity %pOF is disabled, skipping\n", entity);
		of_node_put(entity);
		return -ENODEV;
	}

	ret = rcar_vcon_encoder_init(rvcon, output, entity);
	if (ret && ret != -EPROBE_DEFER && ret != -ENOLINK)
		dev_warn(rvcon->dev,
			 "failed to initialize encoder %pOF on output %u (%d), skipping\n",
			 entity, output, ret);

	of_node_put(entity);

	return ret;
}

static int rcar_vcon_encoders_init(struct rcar_vcon_device *rvcon)
{
	struct device_node *np = rvcon->dev->of_node;
	struct device_node *ep_node;
	unsigned int num_encoders;

	/*
	 * Iterate over the endpoints and create one encoder for each output
	 * pipeline.
	 */

	for_each_endpoint_of_node(np, ep_node) {
		enum rcar_vcon_output output;
		struct of_endpoint ep;
		unsigned int i;
		int ret;

		ret = of_graph_parse_endpoint(ep_node, &ep);
		if (ret < 0) {
			of_node_put(ep_node);
			return ret;
		}

		/* Find the output route corresponding to the port number. */
		for (i = 0; i < RCAR_VCON_OUTPUT_MAX; ++i) {
			if (rvcon->info->routes[i].possible_crtcs &&
			    rvcon->info->routes[i].port == ep.port) {
				output = i;
				break;
			}
		}

		if (i == RCAR_VCON_OUTPUT_MAX) {
			dev_warn(rvcon->dev, "port %u references unexisting output, skipping\n",
				 ep.port);
			continue;
		}

		/* Process the output pipeline. */
		ret = rcar_vcon_encoders_init_one(rvcon, output, &ep);
		if (ret) {
			if (ret == -EPROBE_DEFER) {
				of_node_put(ep_node);
				return ret;
			}

			continue;
		}

		num_encoders++;
	}

	return num_encoders;
}

static int rcar_vcon_properties_init(struct rcar_vcon_device *rvcon)
{
	rvcon->props.alpha = drm_property_create_range(rvcon->ddev, 0, "alpha", 0, 255);
	if (!rvcon->props.alpha)
		return -ENOMEM;

	/*
	 * The color key is expressed as an RGB888 triplet stored in a 32-bit
	 * integer in XRGB8888 format. Bit 24 is used as a flag to disable (0)
	 * or enable source color keying (1).
	 */
	rvcon->props.colorkey = drm_property_create_range(rvcon->ddev, 0, "colorkey",
							  0, 0x01ffffff);
	if (!rvcon->props.colorkey)
		return -ENOMEM;

	rvcon->props.colorkey_alpha = drm_property_create_range(rvcon->ddev, 0, "colorkey_alpha",
								0, 255);
	if (!rvcon->props.colorkey_alpha)
		return -ENOMEM;

	return 0;
}

static int rcar_vcon_vsps_init(struct rcar_vcon_device *rvcon)
{
	const struct device_node *np = rvcon->dev->of_node;
	const char *vsps_prop_name = "renesas,vsps";
	struct of_phandle_args args;
	struct {
		struct device_node *np;
		unsigned int crtcs_mask;
	} vsps[RCAR_VCON_MAX_VSPS] = { { NULL, }, };
	unsigned int vsps_count = 0;
	unsigned int cells;
	unsigned int i;
	int ret;

	/*
	 * First parse the DT vsps property to populate the list of VSPs. Each
	 * entry contains a pointer to the VSP DT node and a bitmask of the
	 * connected VCON CRTCs.
	 */
	ret = of_property_count_u32_elems(np, vsps_prop_name);
	cells = ret / rvcon->num_crtcs - 1;
	if (cells > 1)
		return -EINVAL;

	for (i = 0; i < rvcon->num_crtcs; ++i) {
		unsigned int j;

		ret = of_parse_phandle_with_fixed_args(np, vsps_prop_name,
						       cells, i, &args);
		if (ret)
			goto done;

		/*
		 * Add the VSP to the list or update the corresponding existing
		 * entry if the VSP has already been added.
		 */
		for (j = 0; j < vsps_count; ++j) {
			if (vsps[j].np == args.np)
				break;
		}

		if (j < vsps_count)
			of_node_put(args.np);
		else
			vsps[vsps_count++].np = args.np;

		vsps[j].crtcs_mask |= BIT(i);

		/*
		 * Store the VSP pointer and pipe index in the CRTC. If the
		 * second cell of the 'renesas,vsps' specifier isn't present,
		 * default to 0 to remain compatible with older DT bindings.
		 */
		rvcon->crtcs[i].vsp = &rvcon->vsps[j];
		rvcon->crtcs[i].vsp_pipe = cells >= 1 ? args.args[0] : 0;
	}

	/*
	 * Then initialize all the VSPs from the node pointers and CRTCs bitmask
	 * computed previously.
	 */
	for (i = 0; i < vsps_count; ++i) {
		struct rcar_vcon_vsp *vsp = &rvcon->vsps[i];

		vsp->index = i;
		vsp->dev = rvcon;

		ret = rcar_vcon_vsp_init(vsp, vsps[i].np, vsps[i].crtcs_mask);
		if (ret)
			goto done;
	}

done:
	for (i = 0; i < ARRAY_SIZE(vsps); ++i)
		of_node_put(vsps[i].np);

	return ret;
}

int rcar_vcon_modeset_init(struct rcar_vcon_device *rvcon)
{
	struct drm_device *dev = rvcon->ddev;
	struct drm_encoder *encoder;
	unsigned int num_encoders;
	int i, ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.min_width = 64;
	dev->mode_config.min_height = 64;
	dev->mode_config.max_width = 6144;
	dev->mode_config.max_height = 8190;
	dev->mode_config.normalize_zpos = true;
	dev->mode_config.funcs = &rcar_vcon_mode_config_funcs;
	dev->mode_config.helper_private = &rcar_vcon_mode_config_helper;

	ret = rcar_vcon_properties_init(rvcon);
	if (ret)
		return ret;

	ret = drm_vblank_init(dev, rvcon->num_crtcs);
	if (ret)
		return ret;

	ret = rcar_vcon_vsps_init(rvcon);
	if (ret)
		return ret;

	for (i = 0; i < rvcon->num_crtcs; i++) {
		ret = rcar_vcon_crtc_create(rvcon, i);
		if (ret)
			return ret;
	}

	ret = rcar_vcon_encoders_init(rvcon);
	if (ret < 0)
		return ret;

	if (!ret) {
		dev_err(rvcon->dev, "error: no encoder could be initialized\n");
		return -EINVAL;
	}

	num_encoders = ret;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct rcar_vcon_encoder *renc = to_rcar_encoder(encoder);
		const struct rcar_vcon_output_routing *route = &rvcon->info->routes[renc->output];

		encoder->possible_crtcs = route->possible_crtcs;
		encoder->possible_clones = route->possible_clones;
	}

	for (i = 0; i < rvcon->num_crtcs; ++i) {
		struct rcar_vcon_crtc *rcrtc = &rvcon->crtcs[i];

		ret = rcar_vcon_writeback_init(rvcon, rcrtc);
		if (ret)
			return ret;
	}

	drm_mode_config_reset(dev);

	drm_kms_helper_poll_init(dev);

	return 0;
}
