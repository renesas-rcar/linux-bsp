/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_vcon_writeback.h  --  R-Car Video Converter Writeback Support
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 */

#ifndef __RCAR_VCON_WRITEBACK_H__
#define __RCAR_VCON_WRITEBACK_H__

#include <drm/drm_plane.h>

struct rcar_vcon_crtc;
struct rcar_vcon_device;
struct vsp1_du_atomic_pipe_config;

#ifdef CONFIG_DRM_RCAR_WRITEBACK
int rcar_vcon_writeback_init(struct rcar_vcon_device *rvcon, struct rcar_vcon_crtc *rcrtc);
void rcar_vcon_writeback_setup(struct rcar_vcon_crtc *rcrtc, struct vsp1_du_writeback_config *cfg);
void rcar_vcon_writeback_complete(struct rcar_vcon_crtc *rcrtc);
#else
static inline int rcar_vcon_writeback_init(struct rcar_vcon_device *rvcon,
					   struct rcar_vcon_crtc *rcrtc)
{
	return -ENXIO;
}

static inline void
rcar_vcon_writeback_setup(struct rcar_vcon_crtc *rcrtc,
			  struct vsp1_du_writeback_config *cfg)
{
}

static inline void rcar_vcon_writeback_complete(struct rcar_vcon_crtc *rcrtc)
{
}
#endif

#endif /* __RCAR_VCON_WRITEBACK_H__ */
