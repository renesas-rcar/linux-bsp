// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_vcon_crtc.c  --  R-Car Video Interface Converter CRTCs
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 *
 */

#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sys_soc.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include "rcar_vcon_regs.h"
#include "rcar_vcon_crtc.h"
#include "rcar_vcon_drv.h"
#include "rcar_vcon_kms.h"
#include "rcar_vcon_encoder.h"
#include "rcar_vcon_vsp.h"

static u32 __maybe_unused rcar_vcon_crtc_read(struct rcar_vcon_crtc *rcrtc, u32 reg)
{
	return ioread32(rcrtc->addr + reg);
}

static void rcar_vcon_crtc_write(struct rcar_vcon_crtc *rcrtc, u32 reg, u32 data)
{
	iowrite32(data, rcrtc->addr + reg);
}

static void rcar_vcon_crtc_modify(struct rcar_vcon_crtc *rcrtc, u32 reg, u32 clear, u32 set)
{
	u32 val = ioread32(rcrtc->addr + reg);

	iowrite32((val & ~clear) | set, rcrtc->addr + reg);
}

/* -----------------------------------------------------------------------------
 * Hardware Setup
 */

static void rcar_vcon_dclk_divider(struct rcar_vcon_crtc *rcrtc)
{
	const struct drm_display_mode *mode = &rcrtc->crtc.state->adjusted_mode;
	unsigned long dclk_target = mode->clock * 1000;
	unsigned long best_diff = (unsigned long)-1;
	unsigned long diff;
	int nume, deno, nume_set, deno_set;

	for (nume = NUME_MIN; nume <= NUME_MAX; nume++)
		for (deno = DENO_MIN; deno <= DENO_MAX; deno++) {
			unsigned long output;

			output = rcrtc->dclk_src * nume / deno;
			if (output < dclk_target)
				continue;

			diff = abs((long)output - (long)dclk_target);
			if (best_diff > diff) {
				best_diff = diff;
				nume_set = nume;
				deno_set = deno;
			}

			if (diff == 0)
				goto done;
		}
done:
	rcar_vcon_crtc_write(rcrtc, PIX_CLK_NUME, nume_set);
	rcar_vcon_crtc_write(rcrtc, PIX_CLK_DENO, deno_set);
	rcar_vcon_crtc_write(rcrtc, PIX_CLK_CTRL, PIX_CLK_CTRL_DIV);
}

static void rcar_vcon_crtc_set_display_timing(struct rcar_vcon_crtc *rcrtc)
{
	const struct drm_display_mode *mode = &rcrtc->crtc.state->adjusted_mode;

	/* Find divider */
	rcar_vcon_dclk_divider(rcrtc);

	/* Display timings */
	rcar_vcon_crtc_write(rcrtc, HTOTAL, mode->htotal);
	rcar_vcon_crtc_write(rcrtc, HACT_START, mode->htotal - mode->hsync_end);
	rcar_vcon_crtc_write(rcrtc, HSYNC, mode->hsync_end - mode->hsync_start);
	rcar_vcon_crtc_write(rcrtc, AVW, mode->hdisplay);

	rcar_vcon_crtc_write(rcrtc, VTOTAL, mode->vtotal);
	rcar_vcon_crtc_write(rcrtc, VACT_START, mode->vtotal - mode->vsync_end);
	rcar_vcon_crtc_write(rcrtc, VSYNC, mode->vsync_end - mode->vsync_start);
	rcar_vcon_crtc_write(rcrtc, AVH, mode->vdisplay);
}

/* -----------------------------------------------------------------------------
 * Page Flip
 */

void rcar_vcon_crtc_finish_page_flip(struct rcar_vcon_crtc *rcrtc)
{
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = rcrtc->event;
	rcrtc->event = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (!event)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	drm_crtc_send_vblank_event(&rcrtc->crtc, event);
	wake_up(&rcrtc->flip_wait);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	drm_crtc_vblank_put(&rcrtc->crtc);
}

static bool rcar_vcon_crtc_page_flip_pending(struct rcar_vcon_crtc *rcrtc)
{
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;
	bool pending;

	spin_lock_irqsave(&dev->event_lock, flags);
	pending = rcrtc->event != NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return pending;
}

static void rcar_vcon_crtc_wait_page_flip(struct rcar_vcon_crtc *rcrtc)
{
	struct rcar_vcon_device *rcdu = rcrtc->dev;

	if (wait_event_timeout(rcrtc->flip_wait,
			       !rcar_vcon_crtc_page_flip_pending(rcrtc),
			       msecs_to_jiffies(50)))
		return;

	dev_warn(rcdu->dev, "page flip timeout\n");

	rcar_vcon_crtc_finish_page_flip(rcrtc);
}

/* -----------------------------------------------------------------------------
 * Start/Stop and Suspend/Resume
 */

static void rcar_vcon_crtc_setup(struct rcar_vcon_crtc *rcrtc)
{
	/* Configure display timings */
	rcar_vcon_crtc_set_display_timing(rcrtc);

	/* Enable the VSP compositor. */
	rcar_vcon_vsp_enable(rcrtc);

	/* Turn vertical blanking interrupt reporting on. */
	drm_crtc_vblank_on(&rcrtc->crtc);
}

static int rcar_vcon_crtc_get(struct rcar_vcon_crtc *rcrtc)
{
	if (rcrtc->initialized)
		return 0;

	/* FIXME: Clock handling for non-VDK environenment */

	rcar_vcon_crtc_setup(rcrtc);
	rcrtc->initialized = true;

	return 0;
}

static void rcar_vcon_crtc_put(struct rcar_vcon_crtc *rcrtc)
{
	/* FIXME: Clock handling for non-VDK environenment */

	rcrtc->initialized = false;
}

static void rcar_vcon_crtc_start(struct rcar_vcon_crtc *rcrtc)
{
	rcar_vcon_crtc_write(rcrtc, START, 0x01);
}

static void rcar_vcon_crtc_stop(struct rcar_vcon_crtc *rcrtc)
{
	struct drm_crtc *crtc = &rcrtc->crtc;

	rcar_vcon_crtc_wait_page_flip(rcrtc);
	drm_crtc_vblank_off(crtc);

	rcar_vcon_crtc_write(rcrtc, STOP, 0x01);
}

/* -----------------------------------------------------------------------------
 * CRTC Functions
 */

static int rcar_vcon_crtc_atomic_check(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	struct rcar_vcon_crtc_state *rstate = to_rcar_crtc_state(state);
	struct drm_encoder *encoder;

	/* Store the routes from the CRTC output to the DU outputs. */
	rstate->outputs = 0;

	drm_for_each_encoder_mask(encoder, crtc->dev, state->encoder_mask) {
		struct rcar_vcon_encoder *renc;

		/* Skip the writeback encoder. */
		if (encoder->encoder_type == DRM_MODE_ENCODER_VIRTUAL)
			continue;

		renc = to_rcar_encoder(encoder);
		rstate->outputs |= BIT(renc->output);
	}

	return 0;
}

static void rcar_vcon_crtc_atomic_enable(struct drm_crtc *crtc, struct drm_crtc_state *old_state)
{
	struct rcar_vcon_crtc *rcrtc = to_rcar_crtc(crtc);

	rcar_vcon_crtc_get(rcrtc);

	rcar_vcon_crtc_start(rcrtc);
}

static void rcar_vcon_crtc_atomic_disable(struct drm_crtc *crtc, struct drm_crtc_state *old_state)
{
	struct rcar_vcon_crtc *rcrtc = to_rcar_crtc(crtc);

	rcar_vcon_crtc_stop(rcrtc);
	rcar_vcon_crtc_put(rcrtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static void rcar_vcon_crtc_atomic_begin(struct drm_crtc *crtc,
					struct drm_crtc_state *old_crtc_state)
{
	struct rcar_vcon_crtc *rcrtc = to_rcar_crtc(crtc);

	WARN_ON(!crtc->state->enable);

	rcar_vcon_crtc_get(rcrtc);

	rcar_vcon_vsp_atomic_begin(rcrtc);
}

static void rcar_vcon_crtc_atomic_flush(struct drm_crtc *crtc,
					struct drm_crtc_state *old_crtc_state)
{
	struct rcar_vcon_crtc *rcrtc = to_rcar_crtc(crtc);
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;

	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irqsave(&dev->event_lock, flags);
		rcrtc->event = crtc->state->event;
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	rcar_vcon_vsp_atomic_flush(rcrtc);
}

static enum drm_mode_status rcar_vcon_crtc_mode_valid(struct drm_crtc *crtc,
						      const struct drm_display_mode *mode)
{
	unsigned long mode_clock = mode->clock * 1000;

	if (mode_clock < VCON_DCLK_MIN)
		return MODE_CLOCK_LOW;

	if (mode_clock > VCON_DCLK_MAX)
		return MODE_CLOCK_HIGH;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	return MODE_OK;
}

static const struct drm_crtc_helper_funcs crtc_helper_funcs = {
	.atomic_check = rcar_vcon_crtc_atomic_check,
	.atomic_begin = rcar_vcon_crtc_atomic_begin,
	.atomic_flush = rcar_vcon_crtc_atomic_flush,
	.atomic_enable = rcar_vcon_crtc_atomic_enable,
	.atomic_disable = rcar_vcon_crtc_atomic_disable,
	.mode_valid = rcar_vcon_crtc_mode_valid,
};

static void rcar_vcon_crtc_crc_init(struct rcar_vcon_crtc *rcrtc)
{
	const char **sources;
	unsigned int count;
	int i = -1;

	/* Reserve 1 for "auto" source. */
	count = rcrtc->vsp->num_planes + 1;

	sources = kmalloc_array(count, sizeof(*sources), GFP_KERNEL);
	if (!sources)
		return;

	sources[0] = kstrdup("auto", GFP_KERNEL);
	if (!sources[0])
		goto error;

	for (i = 0; i < rcrtc->vsp->num_planes; ++i) {
		struct drm_plane *plane = &rcrtc->vsp->planes[i].plane;
		char name[16];

		sprintf(name, "plane%u", plane->base.id);
		sources[i + 1] = kstrdup(name, GFP_KERNEL);
		if (!sources[i + 1])
			goto error;
	}

	rcrtc->sources = sources;
	rcrtc->sources_count = count;
	return;

error:
	while (i >= 0) {
		kfree(sources[i]);
		i--;
	}
	kfree(sources);
}

static void rcar_vcon_crtc_crc_cleanup(struct rcar_vcon_crtc *rcrtc)
{
	unsigned int i;

	if (!rcrtc->sources)
		return;

	for (i = 0; i < rcrtc->sources_count; i++)
		kfree(rcrtc->sources[i]);
	kfree(rcrtc->sources);

	rcrtc->sources = NULL;
	rcrtc->sources_count = 0;
}

static struct drm_crtc_state *rcar_vcon_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct rcar_vcon_crtc_state *state;
	struct rcar_vcon_crtc_state *copy;

	if (WARN_ON(!crtc->state))
		return NULL;

	state = to_rcar_crtc_state(crtc->state);
	copy = kmemdup(state, sizeof(*state), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &copy->state);

	return &copy->state;
}

static void rcar_vcon_crtc_atomic_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	__drm_atomic_helper_crtc_destroy_state(state);
	kfree(to_rcar_crtc_state(state));
}

static void rcar_vcon_crtc_cleanup(struct drm_crtc *crtc)
{
	struct rcar_vcon_crtc *rcrtc = to_rcar_crtc(crtc);

	rcar_vcon_crtc_crc_cleanup(rcrtc);

	drm_crtc_cleanup(crtc);
}

static void rcar_vcon_crtc_reset(struct drm_crtc *crtc)
{
	struct rcar_vcon_crtc_state *state;

	if (crtc->state) {
		rcar_vcon_crtc_atomic_destroy_state(crtc, crtc->state);
		crtc->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return;

	state->crc.source = VSP1_DU_CRC_NONE;
	state->crc.index = 0;

	__drm_atomic_helper_crtc_reset(crtc, &state->state);
}

static int rcar_vcon_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct rcar_vcon_crtc *rcrtc = to_rcar_crtc(crtc);

	rcar_vcon_crtc_modify(rcrtc, IRQ_STA, IRQ_STA_VSYNC, 0);
	rcar_vcon_crtc_modify(rcrtc, IRQ_ENB, 0, IRQ_ENB_VSYNC);
	rcrtc->vblank_enable = true;

	return 0;
}

static void rcar_vcon_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct rcar_vcon_crtc *rcrtc = to_rcar_crtc(crtc);

	rcar_vcon_crtc_modify(rcrtc, IRQ_ENB, IRQ_ENB_VSYNC, 0);
	rcrtc->vblank_enable = false;
}

static int rcar_vcon_crtc_parse_crc_source(struct rcar_vcon_crtc *rcrtc, const char *source_name,
					   enum vsp1_du_crc_source *source)
{
	unsigned int index;
	int ret;

	/*
	 * Parse the source name. Supported values are "plane%u" to compute the
	 * CRC on an input plane (%u is the plane ID), and "auto" to compute the
	 * CRC on the composer (VSP) output.
	 */

	if (!source_name) {
		*source = VSP1_DU_CRC_NONE;
		return 0;
	} else if (!strcmp(source_name, "auto")) {
		*source = VSP1_DU_CRC_OUTPUT;
		return 0;
	} else if (strstarts(source_name, "plane")) {
		unsigned int i;

		*source = VSP1_DU_CRC_PLANE;

		ret = kstrtouint(source_name + strlen("plane"), 10, &index);
		if (ret)
			return ret;

		for (i = 0; i < rcrtc->vsp->num_planes; ++i) {
			if (index == rcrtc->vsp->planes[i].plane.base.id)
				return i;
		}
	}

	return -EINVAL;
}

static int rcar_vcon_crtc_verify_crc_source(struct drm_crtc *crtc, const char *source_name,
					    size_t *values_cnt)
{
	struct rcar_vcon_crtc *rcrtc = to_rcar_crtc(crtc);
	enum vsp1_du_crc_source source;

	if (rcar_vcon_crtc_parse_crc_source(rcrtc, source_name, &source) < 0) {
		DRM_DEBUG_DRIVER("unknown source %s\n", source_name);
		return -EINVAL;
	}

	*values_cnt = 1;

	return 0;
}

static const char *const *rcar_vcon_crtc_get_crc_sources(struct drm_crtc *crtc, size_t *count)
{
	struct rcar_vcon_crtc *rcrtc = to_rcar_crtc(crtc);

	*count = rcrtc->sources_count;

	return rcrtc->sources;
}

static int rcar_vcon_crtc_set_crc_source(struct drm_crtc *crtc, const char *source_name)
{
	struct rcar_vcon_crtc *rcrtc = to_rcar_crtc(crtc);
	struct drm_modeset_acquire_ctx ctx;
	struct drm_crtc_state *crtc_state;
	struct drm_atomic_state *state;
	enum vsp1_du_crc_source source;
	unsigned int index;
	int ret;

	ret = rcar_vcon_crtc_parse_crc_source(rcrtc, source_name, &source);
	if (ret < 0)
		return ret;

	index = ret;

	/* Perform an atomic commit to set the CRC source. */
	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state) {
		ret = -ENOMEM;
		goto unlock;
	}

	state->acquire_ctx = &ctx;

retry:
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (!IS_ERR(crtc_state)) {
		struct rcar_vcon_crtc_state *rcrtc_state;

		rcrtc_state = to_rcar_crtc_state(crtc_state);
		rcrtc_state->crc.source = source;
		rcrtc_state->crc.index = index;

		ret = drm_atomic_commit(state);
	} else {
		ret = PTR_ERR(crtc_state);
	}

	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_atomic_state_put(state);

unlock:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;
}

static const struct drm_crtc_funcs crtc_funcs = {
	.reset = rcar_vcon_crtc_reset,
	.destroy = rcar_vcon_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = rcar_vcon_crtc_atomic_duplicate_state,
	.atomic_destroy_state = rcar_vcon_crtc_atomic_destroy_state,
	.enable_vblank = rcar_vcon_crtc_enable_vblank,
	.disable_vblank = rcar_vcon_crtc_disable_vblank,
	.set_crc_source = rcar_vcon_crtc_set_crc_source,
	.verify_crc_source = rcar_vcon_crtc_verify_crc_source,
	.get_crc_sources = rcar_vcon_crtc_get_crc_sources,
	.gamma_set = drm_atomic_helper_legacy_gamma_set,
};

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

static irqreturn_t rcar_vcon_crtc_irq(int irq, void *arg)
{
	struct rcar_vcon_crtc *rcrtc = arg;
	irqreturn_t ret = IRQ_NONE;
	u32 status;

	spin_lock(&rcrtc->vblank_lock);

	status = rcar_vcon_crtc_read(rcrtc, IRQ_STA);
	rcar_vcon_crtc_modify(rcrtc, IRQ_STA, IRQ_STA_VSYNC, 0);

	if (status & IRQ_STA_VSYNC)
		ret = IRQ_HANDLED;

	spin_unlock(&rcrtc->vblank_lock);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

int rcar_vcon_crtc_create(struct rcar_vcon_device *rvcon, unsigned int index)
{
	struct platform_device *pdev = to_platform_device(rvcon->dev);
	struct rcar_vcon_crtc *rcrtc = &rvcon->crtcs[index];
	struct drm_crtc *crtc = &rcrtc->crtc;
	char clk_name[9], irq_name[9];
	struct drm_plane *primary;
	struct clk *clk;
	char *name;
	int irq;
	int ret;

	/* Get dot-clock */
	sprintf(clk_name, "dclkin.%u", index);
	clk = devm_clk_get(rvcon->dev, clk_name);
	if (!IS_ERR(clk))
		rcrtc->dclk_src = clk_get_rate(clk);
	else if (PTR_ERR(clk) != -EPROBE_DEFER)
		rcrtc->dclk_src = VCON_DCLK_SRC_DEFAULT;
	else
		return -EPROBE_DEFER;

	init_waitqueue_head(&rcrtc->flip_wait);
	spin_lock_init(&rcrtc->vblank_lock);

	rcrtc->dev = rvcon;

	primary = &rcrtc->vsp->planes[rcrtc->vsp_pipe].plane;

	ret = drm_crtc_init_with_planes(rvcon->ddev, crtc, primary, NULL, &crtc_funcs, NULL);
	if (ret)
		return ret;

	drm_crtc_helper_add(crtc, &crtc_helper_funcs);

	/* Register interrupt */
	sprintf(irq_name, "ch%u", index);
	irq = platform_get_irq_byname(pdev, irq_name);
	if (irq < 0)
		return irq;

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s:%d", dev_name(rvcon->dev), index);
	ret = devm_request_irq(rvcon->dev, irq, rcar_vcon_crtc_irq, 0, name, rcrtc);
	if (ret < 0)
		return ret;

	rcar_vcon_crtc_crc_init(rcrtc);

	return 0;
}
