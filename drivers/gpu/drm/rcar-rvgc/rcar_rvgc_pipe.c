#include <drm/drmP.h>
#include <drm/drm_device.h>

#include "rcar_rvgc_drv.h"
#include "rcar_rvgc_pipe.h"
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "rcar_rvgc_taurus.h"
#include "r_taurus_rvgc_protocol.h"
#include "r_taurus_bridge.h"

struct rvgc_connector {
	struct drm_connector base;
	struct rcar_rvgc_device *rvgc_dev;
	unsigned int pipe_idx;
	struct drm_display_mode *mode;
};


static inline struct rvgc_connector *
to_rvgc_connector(struct drm_connector *connector)
{
	return container_of(connector, struct rvgc_connector, base);
}

static int rvgc_connector_get_modes(struct drm_connector *connector)
{
	struct rvgc_connector *rconn = to_rvgc_connector(connector);
	struct rcar_rvgc_device *rvgc_dev = rconn->rvgc_dev;
	struct rcar_rvgc_pipe *rvgc_pipe = &rvgc_dev->rvgc_pipes[rconn->pipe_idx];
	struct drm_display_mode *mode;

	if (!rconn->mode) {
		struct taurus_rvgc_res_msg res_msg;
		int ret;

		ret = rvgc_taurus_display_get_info(rvgc_dev,
						rvgc_pipe->display_mapping,
						&res_msg);
		if (ret) {
			dev_err(rvgc_dev->dev, "%s(): rvgc_taurus_display_get_info(%d) failed\n",
				__FUNCTION__,
				rvgc_pipe->display_mapping);
			return ret;
		}

		rconn->mode = drm_mode_create(rvgc_dev->ddev);
		if (!rconn->mode) {
			dev_err(rvgc_dev->dev,
				"%s() Failed to create rvgc connector mode\n",
				__FUNCTION__);
			return 0;
		}

		rconn->mode->hdisplay = res_msg.params.ioc_display_get_info.width;
		rconn->mode->vdisplay = res_msg.params.ioc_display_get_info.height;

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
	.best_encoder = drm_atomic_helper_best_encoder,
};

static enum drm_connector_status
rvgc_connector_detect(struct drm_connector *connector, bool force)
{
	if (drm_dev_is_unplugged(connector->dev))
		return connector_status_disconnected;

	return connector->status;
}

static void rvgc_connector_destroy(struct drm_connector *connector)
{
	struct rvgc_connector *rconn = to_rvgc_connector(connector);

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

struct drm_connector *
rvgc_connector_create(struct rcar_rvgc_pipe *rvgc_pipe)
{
	struct rcar_rvgc_device *rvgc_dev = rvgc_pipe->rcar_rvgc_dev;
	struct drm_device *drm = rvgc_dev->ddev;
	struct rvgc_connector *rconn;
	struct drm_connector *connector;
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

static void rvgc_pipe_enable(struct drm_simple_display_pipe *pipe,
			struct drm_crtc_state *crtc_state)
{
	struct device *ddev = pipe->plane.dev->dev;
	struct rcar_rvgc_pipe *rvgc_pipe = container_of(pipe, struct rcar_rvgc_pipe, drm_simple_pipe);
	dev_dbg(ddev, "%s() rvgc_pipe = %d\n", __FUNCTION__, rvgc_pipe->idx);
	drm_crtc_vblank_on(&pipe->crtc);
}

static void rvgc_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct device *ddev = pipe->plane.dev->dev;
	struct rcar_rvgc_pipe *rvgc_pipe = container_of(pipe, struct rcar_rvgc_pipe, drm_simple_pipe);
	dev_dbg(ddev, "%s() rvgc_pipe = %d\n", __FUNCTION__, rvgc_pipe->idx);
	drm_crtc_vblank_off(&pipe->crtc);
}

static void rvgc_display_pipe_update(struct drm_simple_display_pipe *pipe,
				struct drm_plane_state *old_state)
{
	struct drm_gem_cma_object *gem_obj;
	int ret;
	struct taurus_rvgc_res_msg res_msg;

	struct drm_plane_state *new_plane_state = pipe->plane.state;
	struct drm_crtc *crtc = &pipe->crtc;
	struct device *dev = pipe->plane.dev->dev;
	struct rcar_rvgc_device *rcrvgc = dev_get_drvdata(dev);
	struct rcar_rvgc_pipe *rvgc_pipe = container_of(pipe, struct rcar_rvgc_pipe, drm_simple_pipe);
	unsigned int display_idx = rvgc_pipe->display_mapping;
	unsigned int display_layer = rvgc_pipe->display_layer;
	unsigned long flags;

	/* No need to notify the Taurus server. Just send the vblank
	 * event to notify the DRM that the commit is completed. */
	if (crtc->state->active_changed || (new_plane_state->fb == old_state->fb)) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
		return;
	}

	/* Update the framebuffer address */
	if (new_plane_state->fb) {

		gem_obj = drm_fb_cma_get_gem_obj(new_plane_state->fb, 0); //we support only single planar formats

		ret = rvgc_taurus_layer_set_addr(rcrvgc,
						display_idx,
						display_layer,
						gem_obj->paddr,
						&res_msg);
		if (ret) {
			dev_err(rcrvgc->dev, "%s(): rvgc_taurus_layer_set_addr(display=%d, layer=%d) failed\n",
				__FUNCTION__,
				rvgc_pipe->display_mapping,
				0);
			return;
		}
	}

	/* Save the event in the rvgc_pipe struct so that we can send
	 * it as soon as the Taurus notifies us. */
	drm_crtc_vblank_get(&pipe->crtc);

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	rvgc_pipe->event = crtc->state->event;
	crtc->state->event = NULL;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

	/* Ask the Taurus server to flush the changes */
	ret = rvgc_taurus_display_flush(rcrvgc,
					display_idx,
					0,
					&res_msg);
	if (ret) {
		dev_err(rcrvgc->dev, "%s(): rvgc_taurus_display_flush(%d) failed\n",
			__FUNCTION__,
			rvgc_pipe->display_mapping);
	}
}

static int rvgc_display_pipe_prepare_fb(struct drm_simple_display_pipe *pipe,
					struct drm_plane_state *plane_state)
{
	return drm_fb_cma_prepare_fb(&pipe->plane, plane_state);
}

static const struct drm_simple_display_pipe_funcs rvgc_pipe_funcs = {
	.enable = rvgc_pipe_enable,
	.disable = rvgc_pipe_disable,
	.update = rvgc_display_pipe_update,
	.prepare_fb = rvgc_display_pipe_prepare_fb,
};

static const u32 rvgc_formats[] = {
        DRM_FORMAT_XRGB8888,
        DRM_FORMAT_ARGB8888,
};

int rcar_rvgc_pipe_init(struct rcar_rvgc_device *rvgc_dev,
			struct rcar_rvgc_pipe *rvgc_pipe)
{
	int ret = 0;
	struct drm_device *drm = rvgc_dev->ddev;
	struct drm_connector *connector;
	struct taurus_rvgc_res_msg res_msg;
	int rvgc_layer_width;
	int rvgc_layer_height;

	rvgc_pipe->rcar_rvgc_dev = rvgc_dev;

	connector = rvgc_connector_create(rvgc_pipe);

	ret = drm_simple_display_pipe_init(drm,
					&rvgc_pipe->drm_simple_pipe,
					&rvgc_pipe_funcs,
					rvgc_formats, ARRAY_SIZE(rvgc_formats),
					NULL,
					connector);
	if (ret) {
		dev_err(rvgc_dev->dev,
			"%s() drm_simple_display_pipe_init(pipe=%d) returned an error (%d)\n",
			__FUNCTION__,
			rvgc_pipe->idx,
			ret);
		return ret;
	}

	ret = rvgc_taurus_display_init(rvgc_dev,
				rvgc_pipe->display_mapping,
				rvgc_pipe->display_layer,
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

	rvgc_layer_width = res_msg.params.ioc_display_get_info.width;
	rvgc_layer_height = res_msg.params.ioc_display_get_info.height;

	ret = rvgc_taurus_layer_set_size(rvgc_dev,
					rvgc_pipe->display_mapping,
					rvgc_pipe->display_layer,
					rvgc_layer_width,
					rvgc_layer_height,
					&res_msg);
	if (ret) {
		dev_err(rvgc_dev->dev, "%s(): rvgc_taurus_layer_set_size(display=%d, layer=%d) failed\n",
			__FUNCTION__,
			rvgc_pipe->display_mapping,
			0);
	}

	return ret;
}

struct rcar_rvgc_pipe* rvgc_pipe_find(struct rcar_rvgc_device *rcrvgc, unsigned int pipe_idx)
{
	struct rcar_rvgc_pipe *rvgc_pipe = NULL;
	unsigned int nr_rvgc_pipes = rcrvgc->nr_rvgc_pipes;
	int i;
	for (i=0; i<nr_rvgc_pipes; i++) {
		if (rcrvgc->rvgc_pipes[i].idx == pipe_idx) {
			rvgc_pipe = &rcrvgc->rvgc_pipes[i];
			break;
		}
	}
	return rvgc_pipe;
}

