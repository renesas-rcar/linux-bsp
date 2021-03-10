//#define DEBUG



#include <drm/drm_device.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include <linux/kthread.h>
#include <linux/delay.h>

#include "rcar_rvgc_drv.h"
#include "rcar_rvgc_pipe.h"

#include "rcar_rvgc_taurus.h"
#include "r_taurus_rvgc_protocol.h"

#if 1
//#pragma GCC push_options
#pragma GCC optimize ("-Og")
//#pragma GCC pop_options
#endif


struct rcar_rvgc_format_info {
	u32 fourcc;
	unsigned int bpp;
	unsigned int planes;
};

/********** Fromat Info **********/
static const struct rcar_rvgc_format_info rcar_rvgc_format_infos[] = {
	{
		.fourcc = DRM_FORMAT_XRGB8888,
		.bpp = 32,
		.planes = 1,
	}, {
		.fourcc = DRM_FORMAT_ARGB8888,
		.bpp = 32,
		.planes = 1,
	},
};

const struct rcar_rvgc_format_info* rcar_rvgc_format_info(u32 fourcc) {
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rcar_rvgc_format_infos); ++i) {
		if (rcar_rvgc_format_infos[i].fourcc == fourcc)
			return &rcar_rvgc_format_infos[i];
	}

	return NULL;
}

/********** DRM Framebuffer **********/
static struct drm_framebuffer* rcar_rvgc_fb_create(struct drm_device* dev, struct drm_file* file_priv,
						   const struct drm_mode_fb_cmd2* mode_cmd) {
	const struct rcar_rvgc_format_info* format;

	format = rcar_rvgc_format_info(mode_cmd->pixel_format);
	if (format == NULL) {
		dev_dbg(dev->dev, "unsupported pixel format %08x\n",
			mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	return drm_fb_cma_create(dev, file_priv, mode_cmd);
}

/********** VBlank Handling **********/
static int vsync_thread_fn(void* data) {
	struct rcar_rvgc_device* rcrvgc = (struct rcar_rvgc_device*)data;
	struct drm_crtc* crtc;
	struct rcar_rvgc_pipe* rvgc_pipe;
	unsigned int nr_rvgc_pipes = rcrvgc->nr_rvgc_pipes;
	int i;
	int pipe_vblk_pending;
	unsigned int display_idx;
	struct drm_pending_vblank_event* event;
	unsigned long flags;

	while (!kthread_should_stop()) {

		wait_event_interruptible(rcrvgc->vblank_enable_wait_queue, atomic_read(&rcrvgc->global_vblank_enable));
		/* TODO: Check if possible problem here ? Only one wait line needed maybe. */
		wait_event_interruptible(rcrvgc->vblank_pending_wait_queue, rcrvgc->vblank_pending);

		for (i = 0; i < nr_rvgc_pipes; i++) {
			rvgc_pipe = &rcrvgc->rvgc_pipes[i];
			display_idx = rvgc_pipe->display_mapping;
			;

			pipe_vblk_pending = test_and_clear_bit(display_idx, (long unsigned int*)&rcrvgc->vblank_pending);

			if (pipe_vblk_pending && rvgc_pipe->vblank_enabled) {
				/* TODO: removed simple pipe here nothing else need to be done here */
				crtc = &rvgc_pipe->crtc;

				drm_crtc_handle_vblank(crtc);

				spin_lock_irqsave(&crtc->dev->event_lock, flags);
				event = rvgc_pipe->event;
				rvgc_pipe->event = NULL;
				spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

				if (event == NULL)
					continue;

				spin_lock_irqsave(&crtc->dev->event_lock, flags);
				drm_crtc_send_vblank_event(crtc, event);
				spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

				/* JMB: Where is corresponding "get" ? */
				// drm_crtc_vblank_put(crtc);
			}
		}
	}

	dev_dbg(rcrvgc->dev, "vsync thread exiting\n");
	return 0;
}

static void rcar_rvgc_atomic_commit_tail(struct drm_atomic_state* old_state) {
	struct drm_device* dev = old_state->dev;

	/* Apply the atomic update. */
	drm_atomic_helper_commit_modeset_disables(dev, old_state);
	drm_atomic_helper_commit_planes(dev, old_state, 0);
	drm_atomic_helper_commit_modeset_enables(dev, old_state);
	drm_atomic_helper_commit_hw_done(old_state);
	//drm_atomic_helper_wait_for_vblanks(dev, old_state);
	drm_atomic_helper_wait_for_flip_done(dev, old_state);
	drm_atomic_helper_cleanup_planes(dev, old_state);
}

/********** Kernel Mode Setting Init **********/
static const struct drm_mode_config_funcs rcar_rvgc_mode_config_funcs = {
	.fb_create = rcar_rvgc_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs rcar_rvgc_mode_config_helper = {
	.atomic_commit_tail = rcar_rvgc_atomic_commit_tail,
};

int rcar_rvgc_modeset_init(struct rcar_rvgc_device* rcrvgc) {
	struct drm_device* dev = rcrvgc->ddev;
	struct device_node* displays_node;
	struct device_node* display_node;
	int ret = 0;
	int display;
	int layer;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 2160;
	dev->mode_config.funcs = &rcar_rvgc_mode_config_funcs;
	dev->mode_config.helper_private = &rcar_rvgc_mode_config_helper;
	/* have drm_atomic_helper_check normalize zpos */
	dev->mode_config.normalize_zpos = true;

	displays_node = of_find_node_by_path("/rvgc/displays");
	if (!displays_node) {
		dev_err(rcrvgc->dev, "Cannot find devicetree node \"/rvgc/displays\"\n");
		ret = -EINVAL;
		goto exit;
	}

	/* count display nodes */
	rcrvgc->nr_rvgc_pipes = 0;
	for_each_child_of_node(displays_node, display_node) {
		rcrvgc->nr_rvgc_pipes++;
	}

	rcrvgc->rvgc_pipes = kzalloc(sizeof(struct rcar_rvgc_pipe) * rcrvgc->nr_rvgc_pipes, GFP_KERNEL);
	if (!rcrvgc->rvgc_pipes)
		return -ENOMEM;

	dev_info(rcrvgc->dev, "Number of virtual displays = %u\n", rcrvgc->nr_rvgc_pipes);

	/*
	 * Initialize display pipes
	 */
	display = 0;
	for_each_child_of_node(displays_node, display_node) {
		struct rcar_rvgc_pipe* rvgc_pipe = &rcrvgc->rvgc_pipes[display];
		struct device_node* layers_node;
		struct device_node* layer_node;

		rvgc_pipe->idx = display;

		ret = of_property_read_u32(display_node, "display-map", &rvgc_pipe->display_mapping);
		if (ret) {
			dev_err(rcrvgc->dev, "can't read value in \"display-map\" display = %d\n", display);
			ret = -EINVAL;
			goto exit;
		}

		layers_node = of_get_child_by_name(display_node, "layers");
		if (!layers_node) {
			dev_err(rcrvgc->dev, "Cannot find display %d \"layers\" node\n", display);
			ret = -EINVAL;
			goto exit;
		}

		rvgc_pipe->plane_nr = 0;
		for_each_child_of_node(layers_node, layer_node) {
			rvgc_pipe->plane_nr++;
		}

		rvgc_pipe->planes = devm_kzalloc(rcrvgc->dev, sizeof(rvgc_pipe->planes[0]) * rvgc_pipe->plane_nr, GFP_KERNEL);

		ret = rcar_rvgc_pipe_init(rcrvgc, rvgc_pipe);
		if (ret) {
			dev_err(rcrvgc->dev, "Pipe %d init failed: %d\n", display, ret);
			goto exit;
		}

		layer = 0;
		for_each_child_of_node(layers_node, layer_node) {
			struct rcar_rvgc_plane* cur_plane = &rvgc_pipe->planes[layer];
			
			of_property_read_u32(layer_node, "layer-map", &cur_plane->hw_plane);
			cur_plane->no_scan = of_property_read_bool(layer_node, "no-scan");
			cur_plane->size_override = of_property_read_bool(layer_node, "size-override");
			if (of_property_read_u32(layer_node, "size-w", &cur_plane->size_w))
				cur_plane->size_w = rvgc_pipe->display_width;
			if (of_property_read_u32(layer_node, "size-h", &cur_plane->size_h))
				cur_plane->size_h = rvgc_pipe->display_height;
			cur_plane->pos_override = of_property_read_bool(layer_node, "pos-override");
			of_property_read_u32(layer_node, "pos-x", &cur_plane->pos_x);
			of_property_read_u32(layer_node, "pos-y", &cur_plane->pos_y);
			layer++;
		}

		display++;
	}

	init_waitqueue_head(&rcrvgc->vblank_enable_wait_queue);
	rcrvgc->global_vblank_enable = (atomic_t)ATOMIC_INIT(0);

	if (rcrvgc->vsync_thread)
		dev_warn(rcrvgc->dev, "vsync_thread is already running\n");
	else
		rcrvgc->vsync_thread = kthread_run(vsync_thread_fn,
						   rcrvgc,
						   "rvgc_vsync kthread");


	/*
	 * Initialize vertical blanking interrupts handling. Start with vblank
	 * disabled for all CRTCs.
	 */
	ret = drm_vblank_init(dev, rcrvgc->nr_rvgc_pipes);
	if (ret < 0) {
		dev_err(rcrvgc->dev, "drm_vblank_init failed: %d\n", ret);
		goto exit;
	}
	for  (layer = 0; layer < rcrvgc->nr_rvgc_pipes; layer++) {
		struct rcar_rvgc_pipe* rvgc_pipe = &rcrvgc->rvgc_pipes[layer];
		drm_crtc_vblank_off(&rvgc_pipe->crtc);
	}

	/* Reset crtcs, encoders and connectors */
	drm_mode_config_reset(dev);

	/* Add zpos after structures initialized, but before CRT inits...
	   having tough time finding place where normalized zpos correctly init */
	for (display = 0; display < rcrvgc->nr_rvgc_pipes; display++) {
		struct rcar_rvgc_pipe* rvgc_pipe = &rcrvgc->rvgc_pipes[display];
		for (layer = 0;
		     layer < rvgc_pipe->plane_nr;
		     layer++) {
			struct rcar_rvgc_plane* cur_plane = &rvgc_pipe->planes[layer];
			drm_plane_create_zpos_immutable_property(&cur_plane->plane, cur_plane->hw_plane);
		}
	}

 exit:
	return ret;
}
