#include <drm/drmP.h>
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

static wait_queue_head_t vblank_enable_wait_queue;
static atomic_t global_vblank_enable = ATOMIC_INIT(0);

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

const struct rcar_rvgc_format_info *rcar_rvgc_format_info(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rcar_rvgc_format_infos); ++i) {
		if (rcar_rvgc_format_infos[i].fourcc == fourcc)
			return &rcar_rvgc_format_infos[i];
	}

	return NULL;
}

/********** DRM Framebuffer **********/
static struct drm_framebuffer *rcar_rvgc_fb_create(struct drm_device *dev, struct drm_file *file_priv,
							   const struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct rcar_rvgc_format_info *format;

	format = rcar_rvgc_format_info(mode_cmd->pixel_format);
	if (format == NULL) {
		dev_dbg(dev->dev, "unsupported pixel format %08x\n",
				mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	return drm_fb_cma_create(dev, file_priv, mode_cmd);
}

/********** VBlank Handling **********/
static int vsync_thread_fn(void *data)
{
	struct rcar_rvgc_device *rcrvgc = (struct rcar_rvgc_device*)data;
	struct drm_crtc *crtc;
	struct rcar_rvgc_pipe *rvgc_pipe;
	unsigned int nr_rvgc_pipes = rcrvgc->nr_rvgc_pipes;
	int i;
	int pipe_vblk_pending;
	unsigned int display_idx;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	while (!kthread_should_stop()) {

		wait_event_interruptible(vblank_enable_wait_queue, atomic_read(&global_vblank_enable));

		wait_event_interruptible(rcrvgc->vblank_pending_wait_queue, rcrvgc->vblank_pending);

		for (i=0; i<nr_rvgc_pipes; i++) {
			rvgc_pipe = &rcrvgc->rvgc_pipes[i];
			display_idx = rvgc_pipe->display_mapping;;

			pipe_vblk_pending = test_and_clear_bit(display_idx, (long unsigned int*) &rcrvgc->vblank_pending);

			if (pipe_vblk_pending && rvgc_pipe->vblank_enabled) {
				crtc = &rvgc_pipe->drm_simple_pipe.crtc;

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

				drm_crtc_vblank_put(crtc);
			}
		}
	}

	dev_dbg(rcrvgc->dev, "vsync thread exiting\n");
	return 0;
}

int rcar_rvgc_crtc_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	int ret = 0;
	struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);
	struct drm_simple_display_pipe *drm_simple_pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	struct rcar_rvgc_pipe *rvgc_pipe = container_of(drm_simple_pipe, struct rcar_rvgc_pipe, drm_simple_pipe);

	dev_dbg(dev->dev, "%s(%d)\n", __FUNCTION__, pipe);

	rvgc_pipe->vblank_enabled = 1;
	atomic_inc(&global_vblank_enable);
	wake_up_interruptible(&vblank_enable_wait_queue);
	
	return ret;
}

void rcar_rvgc_crtc_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);
	struct drm_simple_display_pipe *drm_simple_pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	struct rcar_rvgc_pipe *rvgc_pipe = container_of(drm_simple_pipe, struct rcar_rvgc_pipe, drm_simple_pipe);

	dev_dbg(dev->dev, "%s(%d)\n", __FUNCTION__, pipe);

	rvgc_pipe->vblank_enabled = 0;
	atomic_dec(&global_vblank_enable);
	WARN_ON(atomic_read(&global_vblank_enable) < 0);
	return;
}

static void rcar_rvgc_atomic_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;

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

int rcar_rvgc_modeset_init(struct rcar_rvgc_device *rcrvgc)
{
	struct drm_device *dev = rcrvgc->ddev;
	struct drm_fbdev_cma *fbdev;
	struct device_node *dt_node;
	u32 nr_rvgc_pipes;
	int ret = 0;
	int i;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 2160;
	dev->mode_config.funcs = &rcar_rvgc_mode_config_funcs;
	dev->mode_config.helper_private = &rcar_rvgc_mode_config_helper;

	dt_node = of_find_node_by_path("/rvgc");
	if (!dt_node) {
		dev_err(rcrvgc->dev, "Cannot find devicetree node \"/rvgc\"\n");
		ret = -EINVAL;
		goto exit;
	}

	ret = of_property_read_u32(dt_node, "nr-displays", &nr_rvgc_pipes);
	if (ret) {
		dev_err(rcrvgc->dev, "can't read property \"nr-displays\" in node \"/rvgc\"\n");
		ret = -EINVAL;
		goto exit;
	}

	rcrvgc->nr_rvgc_pipes = nr_rvgc_pipes;
	rcrvgc->rvgc_pipes = kzalloc(sizeof(struct rcar_rvgc_pipe) * nr_rvgc_pipes, GFP_KERNEL);
	if (!rcrvgc->rvgc_pipes)
		return -ENOMEM;

	dev_info(rcrvgc->dev, "Number of virtual displays = %u\n", rcrvgc->nr_rvgc_pipes);

	/*
	 * Initialize display pipes
	 */
	for  (i=0; i<nr_rvgc_pipes ; i++) {
		struct rcar_rvgc_pipe *rvgc_pipe = &rcrvgc->rvgc_pipes[i];
		rvgc_pipe->idx = i;

		ret = of_property_read_u32_index(dt_node, "display-mappings", i, &rvgc_pipe->display_mapping);
		if (ret) {
			dev_err(rcrvgc->dev, "can't read value in \"display-mappings\" index = %d\n", i);
			ret = -EINVAL;
			goto exit;
		}

		ret = of_property_read_u32_index(dt_node, "display-layer", i, &rvgc_pipe->display_layer);
		if (ret) {
			dev_err(rcrvgc->dev, "can't read value in \"display-layer\" index = %d. Using default (4)\n", i);
			rvgc_pipe->display_layer = 4;
		}

		ret = rcar_rvgc_pipe_init(rcrvgc, rvgc_pipe);
		if (ret) {
			dev_err(rcrvgc->dev, "Pipe %d init failed: %d\n", i, ret);
			goto exit;
		}
	}

	init_waitqueue_head(&vblank_enable_wait_queue);

	/*
	 * Initialize vertical blanking interrupts handling. Start with vblank
	 * disabled for all CRTCs.
	 */
	ret = drm_vblank_init(dev, nr_rvgc_pipes);
	if (ret < 0) {
		dev_err(rcrvgc->dev, "drm_vblank_init failed: %d\n", ret);
		goto exit;
	}
	for  (i=0; i<nr_rvgc_pipes ; i++) {
		struct rcar_rvgc_pipe *rvgc_pipe = &rcrvgc->rvgc_pipes[i];
		drm_crtc_vblank_off(&rvgc_pipe->drm_simple_pipe.crtc);
	}

	/* Reset crtcs, encoders and connectors */
	drm_mode_config_reset(dev);

	/*
	 * Initializes drm_fbdev_cma struct
	 */
	fbdev = drm_fbdev_cma_init(dev, 32, dev->mode_config.num_connector);
	ret = IS_ERR(fbdev);
	if (ret) {
		dev_err(rcrvgc->dev, "drm_fbdev_cma_init failed: %d\n", ret);
		goto exit;
	}

	rcrvgc->fbdev = fbdev;

	if (rcrvgc->vsync_thread)
		dev_warn(rcrvgc->dev, "vsync_thread is already running\n");
	else
		rcrvgc->vsync_thread = kthread_run(vsync_thread_fn,
						rcrvgc,
						"rvgc_vsync kthread");

exit:
	return ret;
}
