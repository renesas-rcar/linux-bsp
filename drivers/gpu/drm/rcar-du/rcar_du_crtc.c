/*
 * rcar_du_crtc.c  --  R-Car Display Unit CRTCs
 *
 * Copyright (C) 2013-2017 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/sys_soc.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include "rcar_du_crtc.h"
#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_plane.h"
#include "rcar_du_regs.h"
#include "rcar_du_vsp.h"

static u32 rcar_du_crtc_read(struct rcar_du_crtc *rcrtc, u32 reg)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;

	return rcar_du_read(rcdu, rcrtc->mmio_offset + reg);
}

static void rcar_du_crtc_write(struct rcar_du_crtc *rcrtc, u32 reg, u32 data)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;

	rcar_du_write(rcdu, rcrtc->mmio_offset + reg, data);
}

static void rcar_du_crtc_clr(struct rcar_du_crtc *rcrtc, u32 reg, u32 clr)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;

	rcar_du_write(rcdu, rcrtc->mmio_offset + reg,
		      rcar_du_read(rcdu, rcrtc->mmio_offset + reg) & ~clr);
}

static void rcar_du_crtc_set(struct rcar_du_crtc *rcrtc, u32 reg, u32 set)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;

	rcar_du_write(rcdu, rcrtc->mmio_offset + reg,
		      rcar_du_read(rcdu, rcrtc->mmio_offset + reg) | set);
}

static void rcar_du_crtc_clr_set(struct rcar_du_crtc *rcrtc, u32 reg,
				 u32 clr, u32 set)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;
	u32 value = rcar_du_read(rcdu, rcrtc->mmio_offset + reg);

	rcar_du_write(rcdu, rcrtc->mmio_offset + reg, (value & ~clr) | set);
}

int rcar_du_crtc_get(struct rcar_du_crtc *rcrtc)
{
	int ret;

	ret = clk_prepare_enable(rcrtc->clock);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(rcrtc->extclock);
	if (ret < 0)
		goto error_clock;

	ret = rcar_du_group_get(rcrtc->group);
	if (ret < 0)
		goto error_group;

	return 0;

error_group:
	clk_disable_unprepare(rcrtc->extclock);
error_clock:
	clk_disable_unprepare(rcrtc->clock);
	return ret;
}

void rcar_du_crtc_put(struct rcar_du_crtc *rcrtc)
{
	rcar_du_group_put(rcrtc->group);

	clk_disable_unprepare(rcrtc->extclock);
	clk_disable_unprepare(rcrtc->clock);
}

void rcar_du_crtc_vbk_check(struct rcar_du_crtc *rcrtc)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;
	u32 i, timeout = 100, loop = 1;
	u32 status;

	if (rcdu->ths_quirks & RCAR_DU_VBK_CHECK_WA)
		loop = 2;

	/* Check next VBK flag */
	for (i = 0; i < timeout; i++) {
		status = rcar_du_crtc_read(rcrtc, DSSR);
		if (status & DSSR_VBK) {

			rcar_du_crtc_write(rcrtc, DSRCR, DSRCR_VBCL);
			if (--loop == 0)
				break;
		}
		mdelay(1);
	}
}

/* -----------------------------------------------------------------------------
 * Hardware Setup
 */

static void rcar_du_dpll_divider(struct rcar_du_crtc *rcrtc,
				 struct dpll_info *dpll, unsigned int extclk,
				 unsigned int mode_clock)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;
	unsigned long dpllclk;
	unsigned long diff;
	unsigned long n, m, fdpll;
	bool match_flag = false;
	bool clk_diff_set = true;
	bool clk_high = false;

	if (mode_clock > 148500000)
		clk_high = true;

	for (n = 39; n < 120; n++) {
		for (m = 0; m < 4; m++) {
			for (fdpll = 1; fdpll < 32; fdpll++) {
				if (rcdu->ths_quirks &
					RCAR_DU_DPLL_DUTY_RATE_WA) {
					/* 1/2 (FRQSEL=1) for duty rate 50% */
					dpllclk = extclk * (n + 1) / (m + 1)
							 / (fdpll + 1) / 2;
				} else {
					dpllclk = extclk * (n + 1) / (m + 1)
							 / (fdpll + 1);
				}

				if (dpllclk >= 400000000)
					continue;

				if (clk_high && (dpllclk < mode_clock))
					continue;

				diff = abs((long)dpllclk - (long)mode_clock);
				if (clk_diff_set ||
					((diff == 0) || (dpll->diff > diff))) {
					dpll->diff = diff;
					dpll->n = n;
					dpll->m = m;
					dpll->fdpll = fdpll;
					dpll->dpllclk = dpllclk;

					if (clk_diff_set)
						clk_diff_set = false;

					if (diff == 0) {
						match_flag = true;
						break;
					}
				}
			}
			if (match_flag)
				break;
		}
		if (match_flag)
			break;
	}
}

static void rcar_du_crtc_set_display_timing(struct rcar_du_crtc *rcrtc)
{
	const struct drm_display_mode *mode = &rcrtc->crtc.state->adjusted_mode;
	struct rcar_du_device *rcdu = rcrtc->group->dev;
	unsigned long mode_clock = mode->clock * 1000;
	unsigned long clk;
	u32 value;
	u32 escr;
	u32 div;
	u32 dpll_reg = 0;
	struct dpll_info *dpll;

	dpll = kzalloc(sizeof(*dpll), GFP_KERNEL);
	if (dpll == NULL)
		return;

	/* Compute the clock divisor and select the internal or external dot
	 * clock based on the requested frequency.
	 */
	clk = clk_get_rate(rcrtc->clock);
	div = DIV_ROUND_CLOSEST(clk, mode_clock);
	div = clamp(div, 1U, 64U) - 1;
	escr = div | ESCR_DCLKSEL_CLKS;

	if (rcrtc->extclock) {
		unsigned long extclk;
		unsigned long extrate;
		unsigned long rate;
		u32 extdiv;

		clk_set_rate(rcrtc->extclock, mode_clock);
		extclk = clk_get_rate(rcrtc->extclock);

		if (rcdu->info->dpll_ch & (0x01 << rcrtc->index)) {
			rcar_du_dpll_divider(rcrtc, dpll, extclk, mode_clock);
			extclk = dpll->dpllclk;
			dev_dbg(rcrtc->group->dev->dev,
				"dpllclk:%d, fdpll:%d, n:%d, m:%d, diff:%d\n",
				 dpll->dpllclk, dpll->fdpll, dpll->n, dpll->m,
				 dpll->diff);
		}
		extdiv = DIV_ROUND_CLOSEST(extclk, mode_clock);
		extdiv = clamp(extdiv, 1U, 64U) - 1;

		rate = clk / (div + 1);
		extrate = extclk / (extdiv + 1);

		if (abs((long)extrate - (long)mode_clock) <
		    abs((long)rate - (long)mode_clock)) {
			dev_dbg(rcrtc->group->dev->dev,
				"crtc%u: using external clock\n", rcrtc->index);
			if (rcdu->info->dpll_ch & (0x01 << rcrtc->index)) {
				if (rcdu->ths_quirks &
					RCAR_DU_DPLL_DUTY_RATE_WA)
					escr = ESCR_DCLKSEL_DCLKIN | 0x01;
				else
					escr = ESCR_DCLKSEL_DCLKIN;

				dpll_reg =  DPLLCR_CODE | DPLLCR_M(dpll->m) |
					DPLLCR_FDPLL(dpll->fdpll) |
					DPLLCR_CLKE | DPLLCR_N(dpll->n) |
					DPLLCR_STBY;

				if (rcrtc->index == DU_CH_1)
					dpll_reg |= (DPLLCR_PLCS1 |
						DPLLCR_INCS_DPLL01_DOTCLKIN13);
				if (rcrtc->index == DU_CH_2) {
					dpll_reg |= (DPLLCR_PLCS0 |
						DPLLCR_INCS_DPLL01_DOTCLKIN02);
					if (rcdu->ths_quirks &
						RCAR_DU_DPLLCR_REG_WA)
						dpll_reg |= (0x01 << 20);
				}

				rcar_du_group_write(rcrtc->group, DPLLCR,
								  dpll_reg);
			} else
				escr = extdiv | ESCR_DCLKSEL_DCLKIN;
		}
	}

	kfree(dpll);

	rcar_du_group_write(rcrtc->group, rcrtc->index % 2 ? ESCR2 : ESCR,
			    escr);
	rcar_du_group_write(rcrtc->group, rcrtc->index % 2 ? OTAR2 : OTAR, 0);

	/* Signal polarities */
	value = ((mode->flags & DRM_MODE_FLAG_PVSYNC) ? DSMR_VSL : 0)
	      | ((mode->flags & DRM_MODE_FLAG_PHSYNC) ? DSMR_HSL : 0)
	      | ((mode->flags & DRM_MODE_FLAG_INTERLACE) ? DSMR_ODEV : 0)
	      | DSMR_DIPM_DISP | DSMR_CSPM;
	rcar_du_crtc_write(rcrtc, DSMR, value);

	/* Display timings */
	rcar_du_crtc_write(rcrtc, HDSR, mode->htotal - mode->hsync_start - 19);
	rcar_du_crtc_write(rcrtc, HDER, mode->htotal - mode->hsync_start +
					mode->hdisplay - 19);
	rcar_du_crtc_write(rcrtc, HSWR, mode->hsync_end -
					mode->hsync_start - 1);
	rcar_du_crtc_write(rcrtc, HCR,  mode->htotal - 1);

	rcar_du_crtc_write(rcrtc, VDSR, mode->crtc_vtotal -
					mode->crtc_vsync_end - 2);
	rcar_du_crtc_write(rcrtc, VDER, mode->crtc_vtotal -
					mode->crtc_vsync_end +
					mode->crtc_vdisplay - 2);
	rcar_du_crtc_write(rcrtc, VSPR, mode->crtc_vtotal -
					mode->crtc_vsync_end +
					mode->crtc_vsync_start - 1);
	rcar_du_crtc_write(rcrtc, VCR,  mode->crtc_vtotal - 1);

	rcar_du_crtc_write(rcrtc, DESR,  mode->htotal - mode->hsync_start - 1);
	rcar_du_crtc_write(rcrtc, DEWR,  mode->hdisplay);
}

void rcar_du_crtc_route_output(struct drm_crtc *crtc,
			       enum rcar_du_output output)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);
	struct rcar_du_device *rcdu = rcrtc->group->dev;

	/* Store the route from the CRTC output to the DU output. The DU will be
	 * configured when starting the CRTC.
	 */
	rcrtc->outputs |= BIT(output);

	/* Store RGB routing to DPAD0, the hardware will be configured when
	 * starting the CRTC.
	 */
	if (output == RCAR_DU_OUTPUT_DPAD0)
		rcdu->dpad0_source = rcrtc->index;
}

static unsigned int plane_zpos(struct rcar_du_plane *plane)
{
	return plane->plane.state->normalized_zpos;
}

static const struct rcar_du_format_info *
plane_format(struct rcar_du_plane *plane)
{
	return to_rcar_plane_state(plane->plane.state)->format;
}

static void rcar_du_crtc_update_planes(struct rcar_du_crtc *rcrtc)
{
	struct rcar_du_plane *planes[RCAR_DU_NUM_HW_PLANES];
	struct rcar_du_device *rcdu = rcrtc->group->dev;
	unsigned int num_planes = 0;
	unsigned int dptsr_planes;
	unsigned int hwplanes = 0;
	unsigned int prio = 0;
	unsigned int i;
	u32 dspr = 0;

	for (i = 0; i < rcrtc->group->num_planes; ++i) {
		struct rcar_du_plane *plane = &rcrtc->group->planes[i];
		unsigned int j;

		if (plane->plane.state->crtc != &rcrtc->crtc)
			continue;

		/* Insert the plane in the sorted planes array. */
		for (j = num_planes++; j > 0; --j) {
			if (plane_zpos(planes[j-1]) <= plane_zpos(plane))
				break;
			planes[j] = planes[j-1];
		}

		planes[j] = plane;
		prio += plane_format(plane)->planes * 4;
	}

	for (i = 0; i < num_planes; ++i) {
		struct rcar_du_plane *plane = planes[i];
		struct drm_plane_state *state = plane->plane.state;
		unsigned int index = to_rcar_plane_state(state)->hwindex;

		prio -= 4;
		dspr |= (index + 1) << prio;
		hwplanes |= 1 << index;

		if (plane_format(plane)->planes == 2) {
			index = (index + 1) % 8;

			prio -= 4;
			dspr |= (index + 1) << prio;
			hwplanes |= 1 << index;
		}
	}

	/* If VSP+DU integration is enabled the plane assignment is fixed. */
	if (rcar_du_has(rcdu, RCAR_DU_FEATURE_VSP1_SOURCE)) {
		if (rcdu->info->gen < 3) {
			dspr = (rcrtc->index % 2) + 1;
			hwplanes = 1 << (rcrtc->index % 2);
		} else {
			dspr = (rcrtc->index % 2) ? 3 : 1;
			hwplanes = 1 << ((rcrtc->index % 2) ? 2 : 0);
		}
	}

	/* Update the planes to display timing and dot clock generator
	 * associations.
	 *
	 * Updating the DPTSR register requires restarting the CRTC group,
	 * resulting in visible flicker. To mitigate the issue only update the
	 * association if needed by enabled planes. Planes being disabled will
	 * keep their current association.
	 */
	mutex_lock(&rcrtc->group->lock);

	dptsr_planes = rcrtc->index % 2 ? rcrtc->group->dptsr_planes | hwplanes
		     : rcrtc->group->dptsr_planes & ~hwplanes;

	if (dptsr_planes != rcrtc->group->dptsr_planes) {
		rcar_du_group_write(rcrtc->group, DPTSR,
				    (dptsr_planes << 16) | dptsr_planes);
		rcrtc->group->dptsr_planes = dptsr_planes;

		if (rcrtc->group->used_crtcs)
			rcar_du_group_restart(rcrtc->group, rcrtc);
	}

	/* Restart the group if plane sources have changed. */
	if (rcrtc->group->need_restart)
		rcar_du_group_restart(rcrtc->group, rcrtc);

	mutex_unlock(&rcrtc->group->lock);

	rcar_du_group_write(rcrtc->group, rcrtc->index % 2 ? DS2PR : DS1PR,
			    dspr);
}

/* -----------------------------------------------------------------------------
 * Page Flip
 */

void rcar_du_crtc_finish_page_flip(struct rcar_du_crtc *rcrtc)
{
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = rcrtc->event;
	rcrtc->event = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (event == NULL)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	drm_crtc_send_vblank_event(&rcrtc->crtc, event);
	wake_up(&rcrtc->flip_wait);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	drm_crtc_vblank_put(&rcrtc->crtc);
}

static bool rcar_du_crtc_page_flip_pending(struct rcar_du_crtc *rcrtc)
{
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;
	bool pending;

	spin_lock_irqsave(&dev->event_lock, flags);
	pending = rcrtc->event != NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return pending;
}

static void rcar_du_crtc_wait_page_flip(struct rcar_du_crtc *rcrtc)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;

	if (wait_event_timeout(rcrtc->flip_wait,
			       !rcar_du_crtc_page_flip_pending(rcrtc),
			       msecs_to_jiffies(50)))
		return;

	dev_warn(rcdu->dev, "page flip timeout\n");

	rcar_du_crtc_finish_page_flip(rcrtc);
}

/* -----------------------------------------------------------------------------
 * Start/Stop and Suspend/Resume
 */

static void rcar_du_crtc_setup(struct rcar_du_crtc *rcrtc)
{
	/* Set display off and background to black */
	rcar_du_crtc_write(rcrtc, DOOR, DOOR_RGB(0, 0, 0));
	rcar_du_crtc_write(rcrtc, BPOR, BPOR_RGB(0, 0, 0));

	/* Configure display timings and output routing */
	rcar_du_crtc_set_display_timing(rcrtc);
	rcar_du_group_set_routing(rcrtc->group);

	/* Start with all planes disabled. */
	rcar_du_group_write(rcrtc->group, rcrtc->index % 2 ? DS2PR : DS1PR, 0);

	/* Enable the VSP compositor. */
	if (rcar_du_has(rcrtc->group->dev, RCAR_DU_FEATURE_VSP1_SOURCE))
		rcar_du_vsp_enable(rcrtc);

	/* Turn vertical blanking interrupt reporting on. */
	drm_crtc_vblank_on(&rcrtc->crtc);
}

static void rcar_du_crtc_start(struct rcar_du_crtc *rcrtc)
{
	bool interlaced;

	/* Select master sync mode. This enables display operation in master
	 * sync mode (with the HSYNC and VSYNC signals configured as outputs and
	 * actively driven).
	 */
	interlaced = rcrtc->crtc.mode.flags & DRM_MODE_FLAG_INTERLACE;
	rcar_du_crtc_clr_set(rcrtc, DSYSR, DSYSR_TVM_MASK | DSYSR_SCM_MASK,
			     (interlaced ? DSYSR_SCM_INT_VIDEO : 0) |
			     DSYSR_TVM_MASTER);

	/* Register update by DRES bit */
	rcar_du_group_write(rcrtc->group, DSYSR,
			    (rcar_du_group_read(rcrtc->group, DSYSR) &
			    ~(DSYSR_DRES | DSYSR_DEN)) | DSYSR_DRES);

	rcar_du_group_start_stop(rcrtc->group, true, rcrtc);
	rcar_du_crtc_vbk_check(rcrtc);
}

static void rcar_du_crtc_disable_planes(struct rcar_du_crtc *rcrtc)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;
	struct drm_crtc *crtc = &rcrtc->crtc;
	u32 status;

	/* Make sure vblank interrupts are enabled. */
	drm_crtc_vblank_get(crtc);

	/*
	 * Disable planes and calculate how many vertical blanking interrupts we
	 * have to wait for. If a vertical blanking interrupt has been triggered
	 * but not processed yet, we don't know whether it occurred before or
	 * after the planes got disabled. We thus have to wait for two vblank
	 * interrupts in that case.
	 */
	spin_lock_irq(&rcrtc->vblank_lock);
	rcar_du_group_write(rcrtc->group, rcrtc->index % 2 ? DS2PR : DS1PR, 0);
	status = rcar_du_crtc_read(rcrtc, DSSR);
	rcrtc->vblank_count = status & DSSR_VBK ? 2 : 1;
	spin_unlock_irq(&rcrtc->vblank_lock);

	if (!wait_event_timeout(rcrtc->vblank_wait, rcrtc->vblank_count == 0,
				msecs_to_jiffies(100)))
		dev_warn(rcdu->dev, "vertical blanking timeout\n");

	drm_crtc_vblank_put(crtc);
}

static void rcar_du_crtc_stop(struct rcar_du_crtc *rcrtc)
{
	struct drm_crtc *crtc = &rcrtc->crtc;

	/* Disable all planes and wait for the change to take effect. This is
	 * required as the DSnPR registers are updated on vblank, and no vblank
	 * will occur once the CRTC is stopped. Disabling planes when starting
	 * the CRTC thus wouldn't be enough as it would start scanning out
	 * immediately from old frame buffers until the next vblank.
	 *
	 * This increases the CRTC stop delay, especially when multiple CRTCs
	 * are stopped in one operation as we now wait for one vblank per CRTC.
	 * Whether this can be improved needs to be researched.
	 */
	rcar_du_crtc_disable_planes(rcrtc);

	/* Disable vertical blanking interrupt reporting. We first need to wait
	 * for page flip completion before stopping the CRTC as userspace
	 * expects page flips to eventually complete.
	 */
	rcar_du_crtc_wait_page_flip(rcrtc);
	drm_crtc_vblank_off(crtc);

	/* Select switch sync mode. This stops display operation and configures
	 * the HSYNC and VSYNC signals as inputs.
	 */
	rcar_du_crtc_clr_set(rcrtc, DSYSR, DSYSR_TVM_MASK, DSYSR_TVM_SWITCH);

	rcar_du_group_start_stop(rcrtc->group, false, rcrtc);

	/* Disable the VSP compositor. */
	if (rcar_du_has(rcrtc->group->dev, RCAR_DU_FEATURE_VSP1_SOURCE))
		rcar_du_vsp_disable(rcrtc);
}

void rcar_du_crtc_remove_suspend(struct rcar_du_crtc *rcrtc)
{
	if (!rcrtc->initialized)
		return;

	rcar_du_group_write(rcrtc->group, rcrtc->index % 2 ? DS2PR : DS1PR, 0);

	rcar_du_crtc_clr_set(rcrtc, DSYSR, DSYSR_TVM_MASK, DSYSR_TVM_SWITCH);

	rcar_du_group_start_stop(rcrtc->group, false, rcrtc);

	rcar_du_crtc_put(rcrtc);

	rcrtc->initialized = false;
}

void rcar_du_crtc_suspend(struct rcar_du_crtc *rcrtc)
{
	rcrtc->suspend = true;
	rcar_du_crtc_stop(rcrtc);
	rcrtc->suspend = false;
	rcar_du_crtc_put(rcrtc);
}

void rcar_du_crtc_resume(struct rcar_du_crtc *rcrtc)
{
	unsigned int i;

	if (!rcrtc->crtc.state->active)
		return;

	rcar_du_crtc_get(rcrtc);
	rcar_du_crtc_setup(rcrtc);

	/* Commit the planes state. */
	if (!rcar_du_has(rcrtc->group->dev, RCAR_DU_FEATURE_VSP1_SOURCE)) {
		for (i = 0; i < rcrtc->group->num_planes; ++i) {
			struct rcar_du_plane *plane = &rcrtc->group->planes[i];

			if (plane->plane.state->crtc != &rcrtc->crtc)
				continue;

			rcar_du_plane_setup(plane);
		}
	}

	rcar_du_crtc_update_planes(rcrtc);
	rcar_du_crtc_start(rcrtc);
}

/* -----------------------------------------------------------------------------
 * CRTC Functions
 */

static void rcar_du_crtc_enable(struct drm_crtc *crtc)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);

	/*
	 * If the CRTC has already been setup by the .atomic_begin() handler we
	 * can skip the setup stage.
	 */
	if (!rcrtc->initialized) {
		rcar_du_crtc_get(rcrtc);
		rcar_du_crtc_setup(rcrtc);
		rcar_du_crtc_update_planes(rcrtc);
		rcrtc->initialized = true;
	}

	rcar_du_crtc_start(rcrtc);
}

static void rcar_du_crtc_disable(struct drm_crtc *crtc)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);

	rcar_du_crtc_stop(rcrtc);
	rcar_du_crtc_put(rcrtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	rcrtc->initialized = false;
	rcrtc->outputs = 0;
}

static void rcar_du_crtc_atomic_begin(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_crtc_state)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);

	WARN_ON(!crtc->state->enable);

	/*
	 * If a mode set is in progress we can be called with the CRTC disabled.
	 * We then need to first setup the CRTC in order to configure planes.
	 * The .atomic_enable() handler will notice and skip the CRTC setup.
	 */
	if (!rcrtc->initialized) {
		rcar_du_crtc_get(rcrtc);
		rcar_du_crtc_setup(rcrtc);
		rcar_du_crtc_update_planes(rcrtc);
		rcrtc->initialized = true;
	}

	if (rcar_du_has(rcrtc->group->dev, RCAR_DU_FEATURE_VSP1_SOURCE))
		rcar_du_vsp_atomic_begin(rcrtc);
}

static void rcar_du_crtc_atomic_flush(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_crtc_state)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);
	struct drm_device *dev = rcrtc->crtc.dev;
	unsigned long flags;

	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irqsave(&dev->event_lock, flags);
		rcrtc->event = crtc->state->event;
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	if (rcar_du_has(rcrtc->group->dev, RCAR_DU_FEATURE_VSP1_SOURCE))
		rcar_du_vsp_atomic_flush(rcrtc);
}

static bool rcar_du_crtc_mode_fixup(struct drm_crtc *crtc,
				    const struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode)
{
	struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);
	struct rcar_du_device *rcdu = rcrtc->group->dev;
	int vdsr_reg = mode->crtc_vtotal - mode->crtc_vsync_end - 2;

	/* It is prohibited to set a value less than 1 to VDSR register
	 * by the H/W specification.
	 */
	if (vdsr_reg < 1) {
		dev_err(rcdu->dev,
			"setting value (%d) to VDSR register is invalid.\n",
			vdsr_reg);
		return false;
	}

	return true;
}

static const struct drm_crtc_helper_funcs crtc_helper_funcs = {
	.disable = rcar_du_crtc_disable,
	.enable = rcar_du_crtc_enable,
	.atomic_begin = rcar_du_crtc_atomic_begin,
	.atomic_flush = rcar_du_crtc_atomic_flush,
	.mode_fixup = rcar_du_crtc_mode_fixup,
};

static const struct drm_crtc_funcs crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

/* -----------------------------------------------------------------------------
 * Interrupt Handling
 */

static irqreturn_t rcar_du_crtc_irq(int irq, void *arg)
{
	struct rcar_du_crtc *rcrtc = arg;
	struct rcar_du_device *rcdu = rcrtc->group->dev;
	irqreturn_t ret = IRQ_NONE;
	u32 status;

	spin_lock(&rcrtc->vblank_lock);

	status = rcar_du_crtc_read(rcrtc, DSSR);
	rcar_du_crtc_write(rcrtc, DSRCR, status & DSRCR_MASK);

	if (status & DSSR_VBK) {
		/*
		 * Wake up the vblank wait if the counter reaches 0. This must
		 * be protected by the vblank_lock to avoid races in
		 * rcar_du_crtc_disable_planes().
		 */
		if (rcrtc->vblank_count) {
			if (--rcrtc->vblank_count == 0)
				wake_up(&rcrtc->vblank_wait);
		}
	}

	spin_unlock(&rcrtc->vblank_lock);

	if (status & DSSR_VBK) {
		if (rcdu->info->gen < 3) {
			drm_crtc_handle_vblank(&rcrtc->crtc);
			rcar_du_crtc_finish_page_flip(rcrtc);
		}

		ret = IRQ_HANDLED;
	}

	return ret;
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

int rcar_du_crtc_create(struct rcar_du_group *rgrp, unsigned int index)
{
	static const unsigned int mmio_offsets[] = {
		DU0_REG_OFFSET, DU1_REG_OFFSET, DU2_REG_OFFSET, DU3_REG_OFFSET
	};

	struct rcar_du_device *rcdu = rgrp->dev;
	struct platform_device *pdev = to_platform_device(rcdu->dev);
	struct rcar_du_crtc *rcrtc = &rcdu->crtcs[index];
	struct drm_crtc *crtc = &rcrtc->crtc;
	struct drm_plane *primary;
	unsigned int irqflags;
	struct clk *clk;
	char clk_name[9];
	char *name;
	int irq;
	int ret;
	int offset_index;

	if (rcdu->info->skip_ch && (rcdu->info->skip_ch == (0x01 << index)))
		offset_index = index + 1; /* offset for r8a77965 */
	else
		offset_index = index;

	if (rcar_du_has(rcdu, RCAR_DU_FEATURE_VSPDL_SOURCE)) {
		if (offset_index == rcdu->info->vspdl_pair_ch)
			rcrtc->lif_index = 1;
		else
			rcrtc->lif_index = 0;

		if ((rcrtc->lif_index == 1) && (rcrtc->vsp->num_brs == 0))
			return 0;
	} else {
		rcrtc->lif_index = 0;
	}

	/* Get the CRTC clock and the optional external clock. */
	if (rcar_du_has(rcdu, RCAR_DU_FEATURE_CRTC_IRQ_CLOCK)) {
		sprintf(clk_name, "du.%u", offset_index);
		name = clk_name;
	} else {
		name = NULL;
	}

	rcrtc->clock = devm_clk_get(rcdu->dev, name);
	if (IS_ERR(rcrtc->clock)) {
		dev_err(rcdu->dev, "no clock for CRTC %u\n", offset_index);
		return PTR_ERR(rcrtc->clock);
	}

	sprintf(clk_name, "dclkin.%u", offset_index);
	clk = devm_clk_get(rcdu->dev, clk_name);
	if (!IS_ERR(clk)) {
		rcrtc->extclock = clk;
	} else if (PTR_ERR(rcrtc->clock) == -EPROBE_DEFER) {
		dev_info(rcdu->dev, "can't get external clock %u\n",
			 offset_index);
		return -EPROBE_DEFER;
	}

	init_waitqueue_head(&rcrtc->flip_wait);
	init_waitqueue_head(&rcrtc->vblank_wait);
	spin_lock_init(&rcrtc->vblank_lock);

	rcrtc->group = rgrp;
	rcrtc->mmio_offset = mmio_offsets[offset_index];
	rcrtc->index = offset_index;
	rcrtc->lvds_ch = -1;
	rcrtc->suspend = false;

	if (rcar_du_has(rcdu, RCAR_DU_FEATURE_VSP1_SOURCE)) {
		unsigned int primary_num;

		primary_num = rcrtc->vsp->num_planes - rcrtc->vsp->num_brs;

		if (rcrtc->lif_index == 1)
			primary = &rcrtc->vsp->planes[primary_num].plane;
		else
			primary = &rcrtc->vsp->planes[0].plane;
	} else
		primary = &rgrp->planes[offset_index % 2].plane;

	ret = drm_crtc_init_with_planes(rcdu->ddev, crtc, primary,
					NULL, &crtc_funcs, NULL);
	if (ret < 0)
		return ret;

	drm_crtc_helper_add(crtc, &crtc_helper_funcs);

	/* Start with vertical blanking interrupt reporting disabled. */
	drm_crtc_vblank_off(crtc);

	/* Register the interrupt handler. */
	if (rcar_du_has(rcdu, RCAR_DU_FEATURE_CRTC_IRQ_CLOCK)) {
		irq = platform_get_irq(pdev, index);
		irqflags = 0;
	} else {
		irq = platform_get_irq(pdev, 0);
		irqflags = IRQF_SHARED;
	}

	if (irq < 0) {
		dev_err(rcdu->dev, "no IRQ for CRTC %u\n", offset_index);
		return irq;
	}

	ret = devm_request_irq(rcdu->dev, irq, rcar_du_crtc_irq, irqflags,
			       dev_name(rcdu->dev), rcrtc);
	if (ret < 0) {
		dev_err(rcdu->dev,
			"failed to register IRQ for CRTC %u\n", offset_index);
		return ret;
	}

	return 0;
}

void rcar_du_crtc_enable_vblank(struct rcar_du_crtc *rcrtc, bool enable)
{
	if (enable) {
		rcar_du_crtc_write(rcrtc, DSRCR, DSRCR_VBCL);
		rcar_du_crtc_set(rcrtc, DIER, DIER_VBE);
		rcrtc->vblank_enable = true;
	} else {
		rcar_du_crtc_clr(rcrtc, DIER, DIER_VBE);
		rcrtc->vblank_enable = false;
	}
}
