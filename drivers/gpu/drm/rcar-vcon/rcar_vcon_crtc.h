/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_vcon_crtc.h  --  R-Car Video Interface Converter CRTCs
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 *
 */

#ifndef __RCAR_VCON_CRTC_H__
#define __RCAR_VCON_CRTC_H__

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <drm/drm_crtc.h>
#include <drm/drm_writeback.h>

#include <media/vsp1.h>

#define VCON_DCLK_SRC_DEFAULT	600000000
#define VCON_DCLK_MAX		594000000
#define VCON_DCLK_MIN		25000000
struct rcar_vcon_vsp;

struct rcar_vcon_crtc {
	struct drm_crtc crtc;

	struct rcar_vcon_device *dev;
	unsigned long dclk_src;
	void __iomem *addr;
	unsigned int index;
	bool initialized;

	bool vblank_enable;
	struct drm_pending_vblank_event *event;
	wait_queue_head_t flip_wait;

	spinlock_t vblank_lock;	/* Vblank spinlock */

	struct rcar_vcon_vsp *vsp;
	unsigned int vsp_pipe;

	const char *const *sources;
	unsigned int sources_count;

	struct drm_writeback_connector writeback;
};

#define to_rcar_crtc(c)		container_of(c, struct rcar_vcon_crtc, crtc)
#define wb_to_rcar_crtc(c)	container_of(c, struct rcar_vcon_crtc, writeback)

struct rcar_vcon_crtc_state {
	struct drm_crtc_state state;

	struct vsp1_du_crc_config crc;
	unsigned int outputs;
};

#define to_rcar_crtc_state(s) container_of(s, struct rcar_vcon_crtc_state, state)

enum rcar_vcon_output {
	RCAR_VCON_OUTPUT_DP,
	RCAR_VCON_OUTPUT_MAX,
};

int rcar_vcon_crtc_create(struct rcar_vcon_device *rvcon, unsigned int index);

void rcar_vcon_crtc_finish_page_flip(struct rcar_vcon_crtc *rcrtc);

void rcar_vcon_crtc_dsysr_clr_set(struct rcar_vcon_crtc *rcrtc, u32 clr, u32 set);

#endif /* __RCAR_VCON_CRTC_H__ */
