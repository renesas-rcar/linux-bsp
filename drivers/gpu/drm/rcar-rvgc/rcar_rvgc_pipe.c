//#define DEBUG


#include <drm/drm_device.h>

#include "rcar_rvgc_drv.h"
#include "rcar_rvgc_pipe.h"
#include <drm/drm_simple_kms_helper.h>
/* TODO: simple kms helper not needed anymore ?*/
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
/* TODO: Need plane helper ? */
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_drv.h>
#include <drm/drm_probe_helper.h>

#include "rcar_rvgc_taurus.h"
#include "r_taurus_rvgc_protocol.h"
#include "r_taurus_bridge.h"

#if 1
#pragma GCC optimize ("-Og")
#endif

struct rvgc_connector {
	struct drm_connector base;
	struct rcar_rvgc_device* rvgc_dev;
	unsigned int pipe_idx;
	struct drm_display_mode* mode;
};


static inline struct rvgc_connector*
to_rvgc_connector(struct drm_connector* connector) {
	return container_of(connector, struct rvgc_connector, base);
}

static int rvgc_connector_get_modes(struct drm_connector* connector) {
	struct rvgc_connector* rconn = to_rvgc_connector(connector);
	struct rcar_rvgc_device* rvgc_dev = rconn->rvgc_dev;
	struct rcar_rvgc_pipe* rvgc_pipe = &rvgc_dev->rvgc_pipes[rconn->pipe_idx];
	struct drm_display_mode* mode;

	if (!rconn->mode) {
		rconn->mode = drm_mode_create(rvgc_dev->ddev);
		if (!rconn->mode) {
			dev_err(rvgc_dev->dev,
				"%s() Failed to create rvgc connector mode\n",
				__FUNCTION__);
			return 0;
		}
		/* The first device tree plane is the primary plane (use its dimensions) */
		rconn->mode->hdisplay = rvgc_pipe->planes[0].size_w;
		rconn->mode->vdisplay = rvgc_pipe->planes[0].size_h;

		/* The following memebers in struct drm_display_mode
		 * are set to some fake values just to make the
		 * drm_mode_validate* functions happy. */
		rconn->mode->hsync_start = rconn->mode->hdisplay + 10;
		rconn->mode->hsync_end = rconn->mode->hsync_start + 10;
		rconn->mode->htotal = rconn->mode->hsync_end + 10;
		rconn->mode->vsync_start = rconn->mode->vdisplay + 10;
		rconn->mode->vsync_end = rconn->mode->vsync_start + 10;
		rconn->mode->vtotal = rconn->mode->vsync_end + 10;

		rconn->mode->clock = (rconn->mode->htotal * rconn->mode->vtotal * 60) / 1000;
	}

	mode = drm_mode_duplicate(connector->dev, rconn->mode);
	if (!mode) {
		DRM_ERROR("Failed to duplicate mode\n");
		return 0;
	}

	if (mode->name[0] == '\0')
		drm_mode_set_name(mode);

	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	if (mode->width_mm) {
		connector->display_info.width_mm = mode->width_mm;
		connector->display_info.height_mm = mode->height_mm;
	}

	return 1;
}

static const struct drm_connector_helper_funcs rvgc_connector_hfuncs = {
	.get_modes = rvgc_connector_get_modes,
};

static enum drm_connector_status
rvgc_connector_detect(struct drm_connector* connector, bool force) {
	if (drm_dev_is_unplugged(connector->dev))
		return connector_status_disconnected;

	return connector->status;
}

static void rvgc_connector_destroy(struct drm_connector* connector) {
	struct rvgc_connector* rconn = to_rvgc_connector(connector);

	drm_connector_cleanup(connector);
	kfree(rconn);
}

static const struct drm_connector_funcs rvgc_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = rvgc_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rvgc_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

struct drm_connector*
rvgc_connector_create(struct rcar_rvgc_pipe* rvgc_pipe) {
	struct rcar_rvgc_device* rvgc_dev = rvgc_pipe->rcar_rvgc_dev;
	struct drm_device* drm = rvgc_dev->ddev;
	struct rvgc_connector* rconn;
	struct drm_connector* connector;
	int ret;
	int connector_type = DRM_MODE_CONNECTOR_HDMIA;

	rconn = kzalloc(sizeof(*rconn), GFP_KERNEL);
	if (!rconn)
		return ERR_PTR(-ENOMEM);

	rconn->rvgc_dev = rvgc_dev;
	rconn->pipe_idx = rvgc_pipe->idx;
	connector = &rconn->base;

	drm_connector_helper_add(connector, &rvgc_connector_hfuncs);
	ret = drm_connector_init(drm, connector, &rvgc_connector_funcs,
				 connector_type);
	if (ret) {
		kfree(rconn);
		return ERR_PTR(ret);
	}

	connector->status = connector_status_connected;

	return connector;
}

static const u32 rvgc_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const struct drm_encoder_funcs drm_simple_kms_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int rvgc_crtc_check(struct drm_crtc* crtc,
			   struct drm_crtc_state* state) {
	bool has_primary = state->plane_mask &
		BIT(drm_plane_index(crtc->primary));

	/* We always want to have an active plane with an active CRTC */
	if (has_primary != state->enable)
		return -EINVAL;

	return drm_atomic_add_affected_planes(state->state, crtc);
}

static int rvgc_crtc_enable_vblank(struct drm_crtc* crtc) {  /* TODO: Check that new vblank will be enabled and old remove */
	int ret = 0;
	struct rcar_rvgc_pipe* rvgc_pipe = container_of(crtc, struct rcar_rvgc_pipe, crtc);
	struct rcar_rvgc_device* rcrvgc = rvgc_pipe->rcar_rvgc_dev;

	//printk(KERN_ERR "%s():%d", __FUNCTION__, __LINE__);

	rvgc_pipe->vblank_enabled = 1;
	atomic_inc(&rcrvgc->global_vblank_enable);
	wake_up_interruptible(&rcrvgc->vblank_enable_wait_queue);

	return ret;
}

static void rvgc_crtc_disable_vblank(struct drm_crtc* crtc) { /* TODO: Check that new vblank will be enabled and old remove */
	struct rcar_rvgc_pipe* rvgc_pipe = container_of(crtc, struct rcar_rvgc_pipe, crtc);
	struct rcar_rvgc_device* rcrvgc = rvgc_pipe->rcar_rvgc_dev;

	//printk(KERN_ERR "%s():%d", __FUNCTION__, __LINE__);

	rvgc_pipe->vblank_enabled = 0;
	atomic_dec(&rcrvgc->global_vblank_enable);
	WARN_ON(atomic_read(&rcrvgc->global_vblank_enable) < 0);
	return;
}


static void rvgc_crtc_enable(struct drm_crtc* crtc,
			     struct drm_crtc_state* old_state) {
	//printk(KERN_ERR "%s():%d", __FUNCTION__, __LINE__);
	drm_crtc_vblank_on(crtc);
	drm_crtc_vblank_get(crtc);
}

static void rvgc_crtc_disable(struct drm_crtc* crtc,
			      struct drm_crtc_state* old_state) {
	//printk(KERN_ERR "%s():%d", __FUNCTION__, __LINE__);
	drm_crtc_vblank_off(crtc);
	drm_crtc_vblank_put(crtc);
}

void rvgc_crtc_atomic_flush(struct drm_crtc* crtc,
			    struct drm_crtc_state* old_crtc_state) {
	int ret;
	struct rcar_rvgc_pipe* rvgc_pipe = container_of(crtc, struct rcar_rvgc_pipe, crtc);
	struct rcar_rvgc_device* rcrvgc    = rvgc_pipe->rcar_rvgc_dev;
	unsigned long flags;
	struct taurus_rvgc_res_msg res_msg;

	/* Save the event in the rvgc_pipe struct so that we can send
	 * it as soon as the Taurus notifies us. */
	if (crtc->state->event) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		rvgc_pipe->event = crtc->state->event;
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
	}
	/* Ask the Taurus server to flush the changes */
	ret = rvgc_taurus_display_flush(rcrvgc,
					rvgc_pipe->display_mapping,
					0,
					&res_msg);
	if (ret) {
		dev_err(rcrvgc->dev, "%s(): rvgc_taurus_display_flush(%d) failed\n",
			__FUNCTION__,
			rvgc_pipe->display_mapping);
	}
}

static const struct drm_crtc_helper_funcs rvgc_crtc_helper_funcs = {
	.atomic_check = rvgc_crtc_check,
	.atomic_enable = rvgc_crtc_enable,
	.atomic_disable = rvgc_crtc_disable,
	.atomic_flush = rvgc_crtc_atomic_flush,
};

static const struct drm_crtc_funcs rvgc_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = rvgc_crtc_enable_vblank,
	.disable_vblank = rvgc_crtc_disable_vblank,
};

static int rvgc_plane_atomic_check(struct drm_plane* plane,
				   struct drm_plane_state* plane_state) {
	struct rcar_rvgc_plane* rvgc_plane = container_of(plane, struct rcar_rvgc_plane, plane);
	struct rcar_rvgc_pipe* rvgc_pipe = rvgc_plane->pipe;
	struct drm_crtc_state* crtc_state;

	crtc_state = drm_atomic_get_new_crtc_state(plane_state->state,
						   &rvgc_pipe->crtc);
	if (!crtc_state || !crtc_state->enable)
		return 0; /* nothing to check when disabling or disabled */

	if (plane_state->fb)
		plane_state->visible = true;
	else
		plane_state->visible = false;

	return 0;
}

#if 0
static int plane_not_changed_drm_send_event(struct drm_plane* plane,
					    struct drm_plane_state* old_state) {
	struct drm_plane_state* new_plane_state = plane->state;
	struct rcar_rvgc_plane* rvgc_plane      = container_of(plane, struct rcar_rvgc_plane,
							       plane);
	struct rcar_rvgc_pipe* rvgc_pipe       = rvgc_plane->pipe;
	struct drm_crtc* crtc            = &rvgc_pipe->crtc;
	unsigned long flags = 0;
	/* No need to notify the Taurus server. Just send the vblank
	     * event to notify the DRM that the commit is completed. */
	if (crtc->state->active_changed || (new_plane_state->fb == old_state->fb)) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		if (NULL == crtc->state->event) {
			pr_err("[rvgc_drm] %s():%d", __FUNCTION__, __LINE__);
		} else {
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
		}
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
		return 1;
	}
	return 0;
}

static int check_refresh_primary(struct drm_plane* plane) {
	enum drm_plane_type      plane_type      = plane->type;
	struct device* ddev            = plane->dev->dev;
	struct rcar_rvgc_device* rcrvgc          = dev_get_drvdata(ddev);
	return ((plane_type == DRM_PLANE_TYPE_PRIMARY) && !rcrvgc->update_primary_plane);
}
#endif

static void rvgc_plane_atomic_update(struct drm_plane* plane,
				     struct drm_plane_state* old_state) {
	struct drm_gem_cma_object* gem_obj;
	struct taurus_rvgc_res_msg res_msg;

	struct drm_framebuffer* fb              = plane->state->fb;
	struct rcar_rvgc_plane* rvgc_plane      = container_of(plane, struct rcar_rvgc_plane,
							       plane);
	struct rcar_rvgc_pipe* rvgc_pipe       = rvgc_plane->pipe;
	struct rcar_rvgc_device* rcrvgc          = rvgc_pipe->rcar_rvgc_dev;
	unsigned int             display_idx     = rvgc_pipe->display_mapping;
	int hw_plane;
	bool pos_z_via_pvr = false;
	int pos_x,pos_y,size_w,size_h;
	int ret = 0;

	/* Accomodate as many use case as possible by fdt/powervr.ini overrides */
	if (rvgc_plane->no_scan) {
		if ((0 == old_state->fb) && (0 != plane->state->fb)) {
			dev_info(rcrvgc->dev, "id=%d is NOT being displayed (FDT has no-scan)\n", plane->base.id);
		}
	} else {
		pos_x = (rvgc_plane->pos_override) ? rvgc_plane->pos_x : plane->state->crtc_x;
		pos_y = (rvgc_plane->pos_override) ? rvgc_plane->pos_y : plane->state->crtc_y;
		size_w = (rvgc_plane->size_override) ? rvgc_plane->size_w : plane->state->crtc_w;
		size_h = (rvgc_plane->size_override) ? rvgc_plane->size_h : plane->state->crtc_h;

		/* check if we've encoded z layer in the powervr.ini pos_y */
		if (0x00100000 == (pos_y & 0xFFF00000)) {
			hw_plane = (pos_y & 0x000F0000)>>16;
			pos_y = (int)((int16_t)(pos_y & 0xFFFF));
			pos_z_via_pvr = true;
		} else {
			/* zpos is being normalized by drm_atomic_helper_check
			   ...or should be anyhow :( */
			//hw_plane = plane->state->normalized_zpos;
			hw_plane = plane->state->zpos;
		}

		/* determine why we're here...*/
		if ((0 == old_state->fb) && (0 != plane->state->fb)) {
			dev_info(rcrvgc->dev, "Reserve id=%d, layer=%d (via %s):%sx=%d, y=%d, %sw=%d, h=%d\n",
				 plane->base.id, hw_plane, (pos_z_via_pvr) ? "PVR":"FDT",
				 (rvgc_plane->pos_override) ? "Force Pos,":"", pos_x, pos_y,
				 (rvgc_plane->size_override) ? "Force Size,":"", size_w, size_h);
			/* enabling */
			ret = rvgc_taurus_plane_reserve(rcrvgc,
							display_idx,
							hw_plane,
							&res_msg);
			if (ret) {
				dev_err(rcrvgc->dev, "%s(): rvgc_taurus_plane_reserve(display=%d, id=%d, layer=%d) failed\n",
					__FUNCTION__, display_idx, plane->base.id, hw_plane);
				rvgc_plane->plane_reserved = false;
				return;
			}
			rvgc_plane->plane_reserved = true;

			ret = rvgc_taurus_layer_set_size(rcrvgc,
							 display_idx,
							 hw_plane,
							 size_w,
							 size_h,
							 &res_msg);
			if (ret) {
				dev_err(rcrvgc->dev, "%s(): rvgc_taurus_layer_set_size(display=%d, id=%d, layer=%d) failed\n",
					__FUNCTION__, display_idx, plane->base.id, hw_plane);
			}
			ret = rvgc_taurus_layer_set_pos(rcrvgc,
							display_idx,
							hw_plane,
							pos_x,
							pos_y,
							&res_msg);
			if (ret) {
				dev_err(rcrvgc->dev, "%s(): rvgc_taurus_layer_set_position(display=%d, id=%d, layer=%d) failed\n",
					__FUNCTION__, display_idx, plane->base.id, hw_plane);
			}
			return;
		} 

		/* don't proceed from here if we don't actually have plane */
		if (!rvgc_plane->plane_reserved) {
			return;
		}

		if ((0 != old_state->fb) && (0 == plane->state->fb)) {
			/* disabling */
			dev_info(rcrvgc->dev, "Release id=%d, layer=%d\n", plane->base.id, hw_plane);
			ret = rvgc_taurus_layer_release(rcrvgc, display_idx, hw_plane, &res_msg);
			rvgc_plane->plane_reserved = false;
			if (ret) {
				dev_err(rcrvgc->dev, "%s(): rvgc_taurus_layer_release(display=%d, id=%d, layer=%d) failed\n",
					__FUNCTION__, display_idx, plane->base.id, hw_plane);
			}
			return;
		}

		if (0 != plane->state->fb) {
			/* updating fb */
			gem_obj = drm_fb_cma_get_gem_obj(fb, 0); //we support only single planar formats
			ret = rvgc_taurus_layer_set_addr(rcrvgc, display_idx, hw_plane, gem_obj->paddr, &res_msg);
			if (ret) {
				dev_err(rcrvgc->dev, "%s(): rvgc_taurus_layer_set_addr(display=%d, id=%d, layer=%d) failed\n",
					__FUNCTION__, display_idx, plane->base.id, hw_plane);
			}
			return;
		}

		WARN_ON(!plane->state->fb);
	}
}

static int rvgc_plane_prepare_fb(struct drm_plane* plane,
				 struct drm_plane_state* state) {
	return drm_gem_fb_prepare_fb(plane, state);
}

static void rvgc_plane_cleanup_fb(struct drm_plane* plane,
				  struct drm_plane_state* state) {

}

static const struct drm_plane_helper_funcs rvgc_plane_helper_funcs = {
	.prepare_fb = rvgc_plane_prepare_fb,
	.cleanup_fb = rvgc_plane_cleanup_fb,
	.atomic_check = rvgc_plane_atomic_check,
	.atomic_update = rvgc_plane_atomic_update,
};

static const struct drm_plane_funcs rvgc_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static int overlay_pipe_init(struct rcar_rvgc_device* rvgc_dev,
			     struct rcar_rvgc_pipe* rvgc_pipe) {

	int ret = 0, i;
	struct drm_device* ddev = rvgc_dev->ddev;
	struct drm_crtc* crtc = &rvgc_pipe->crtc;
	struct drm_encoder* encoder = &rvgc_pipe->encoder;
	struct drm_connector* connector;

	rvgc_pipe->rcar_rvgc_dev = rvgc_dev;
	rvgc_pipe->event = NULL;
	for (i = 0; i < rvgc_pipe->plane_nr; i++) {
		enum drm_plane_type type = i == 0
			? DRM_PLANE_TYPE_PRIMARY
			: DRM_PLANE_TYPE_OVERLAY;

		struct rcar_rvgc_plane* plane = &rvgc_pipe->planes[i];
		plane->pipe = rvgc_pipe;

		drm_plane_helper_add(&plane->plane, &rvgc_plane_helper_funcs);
		ret = drm_universal_plane_init(ddev, &plane->plane,
					       (1 << rvgc_pipe->idx),
					       &rvgc_plane_funcs,
					       rvgc_formats, ARRAY_SIZE(rvgc_formats), NULL,
					       type, NULL);
		if (ret)
			return ret;

	}

	drm_crtc_helper_add(crtc, &rvgc_crtc_helper_funcs);
	ret = drm_crtc_init_with_planes(ddev, crtc, &rvgc_pipe->planes[0].plane, NULL,
					&rvgc_crtc_funcs, NULL);
	if (ret)
		return ret;

	encoder->possible_crtcs = 1 << drm_crtc_index(crtc);
	ret = drm_encoder_init(ddev, encoder, &drm_simple_kms_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;
	connector = rvgc_connector_create(rvgc_pipe);

	return drm_connector_attach_encoder(connector, encoder);

}

static int taurus_init(struct rcar_rvgc_device* rvgc_dev,
		       struct rcar_rvgc_pipe* rvgc_pipe) {
#if 0
	struct rcar_rvgc_device* rcrvgc = rvgc_pipe->rcar_rvgc_dev;
#endif
	int ret = 0;
	struct taurus_rvgc_res_msg res_msg;

	ret = rvgc_taurus_display_init(rvgc_dev,
				       rvgc_pipe->display_mapping,
				       &res_msg);
	if (ret) {
		dev_err(rvgc_dev->dev, "%s(): rvgc_taurus_display_init(%d) failed\n",
			__FUNCTION__,
			rvgc_pipe->display_mapping);
		return ret;
	}

	ret = rvgc_taurus_display_get_info(rvgc_dev,
					   rvgc_pipe->display_mapping,
					   &res_msg);
	if (ret) {
		dev_err(rvgc_dev->dev, "%s(): rvgc_taurus_display_get_info(%d) failed\n",
			__FUNCTION__,
			rvgc_pipe->display_mapping);
		return ret;
	}

	/* not sure we'll use this, but keep the data */
	rvgc_pipe->display_width = res_msg.params.ioc_display_get_info.width;
	rvgc_pipe->display_height = res_msg.params.ioc_display_get_info.height;

	return ret;
}

int rcar_rvgc_pipe_init(struct rcar_rvgc_device* rvgc_dev,
			struct rcar_rvgc_pipe* rvgc_pipe) {
	int ret = 0;
	ret = overlay_pipe_init(rvgc_dev, rvgc_pipe);
	if (ret) return ret;
	ret = taurus_init(rvgc_dev, rvgc_pipe);
	return ret;
}

struct rcar_rvgc_pipe* rvgc_pipe_find(struct rcar_rvgc_device* rcrvgc, unsigned int pipe_idx) {
	struct rcar_rvgc_pipe* rvgc_pipe = NULL;
	unsigned int nr_rvgc_pipes = rcrvgc->nr_rvgc_pipes;
	int i;
	for (i = 0; i < nr_rvgc_pipes; i++) {
		if (rcrvgc->rvgc_pipes[i].idx == pipe_idx) {
			rvgc_pipe = &rcrvgc->rvgc_pipes[i];
			break;
		}
	}
	return rvgc_pipe;
}
