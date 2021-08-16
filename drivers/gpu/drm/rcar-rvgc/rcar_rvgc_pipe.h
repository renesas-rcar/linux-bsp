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

struct rcar_rvgc_plane {
	unsigned int hw_plane;
	unsigned int size_w;
	unsigned int size_h;
	unsigned int pos_x;
	unsigned int pos_y;
	bool pos_override;	/* always use rcar_rvgc_plane position (read from FDT) */
	bool size_override;	/* always use rcar_rvgc_plane size (read from FDT) */
	bool no_scan;		/* don't output this plane to vspd */
	bool plane_reserved;	/* we've actually managed to allocate a hardware plane */
	struct drm_plane plane;
	struct rcar_rvgc_pipe* pipe;
};

struct rcar_rvgc_pipe {
	struct rcar_rvgc_device* rcar_rvgc_dev;
	unsigned int idx;
	unsigned int display_mapping;
	unsigned int vblank_enabled;
	unsigned int plane_nr;
	unsigned int display_height;
	unsigned int display_width;

	struct drm_crtc crtc;
	struct rcar_rvgc_plane* planes;
	struct drm_encoder encoder;
	struct drm_connector* connector;

	struct drm_pending_vblank_event* event;
};

int rcar_rvgc_pipe_init(struct rcar_rvgc_device* rvgc_dev, struct rcar_rvgc_pipe* rvgc_pipe);
struct rcar_rvgc_pipe* rvgc_pipe_find(struct rcar_rvgc_device* rcrvgc, unsigned int pipe_idx);

#endif /* __RCAR_RVGC_PIPE_H__ */
