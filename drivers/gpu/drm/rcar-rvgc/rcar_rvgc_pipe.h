/*
 * rcar_rvgc_pipe.h  --  R-Car Display Unit DRM driver
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

#ifndef __RCAR_RVGC_PIPE_H__
#define __RCAR_RVGC_PIPE_H__

#include <linux/kernel.h>
#include <drm/drm_simple_kms_helper.h>

struct rcar_rvgc_device;
struct drm_crtc;

struct rcar_rvgc_pipe {
	struct rcar_rvgc_device *rcar_rvgc_dev;
	unsigned int idx;
	unsigned int display_mapping;
	unsigned int display_layer;
	unsigned int vblank_enabled;
	struct drm_simple_display_pipe drm_simple_pipe;
	struct drm_pending_vblank_event *event;
};

int rcar_rvgc_pipe_init(struct rcar_rvgc_device *rvgc_dev, struct rcar_rvgc_pipe *rvgc_pipe);
struct rcar_rvgc_pipe* rvgc_pipe_find(struct rcar_rvgc_device *rcrvgc, unsigned int pipe_idx);

#endif /* __RCAR_RVGC_PIPE_H__ */
