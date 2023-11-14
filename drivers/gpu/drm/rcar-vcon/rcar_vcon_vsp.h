/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_vcon_vsp.h  --  R-Car Video Interface Converter VSP-Based Compositor
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 *
 */

#ifndef __RCAR_VCON_VSP_H__
#define __RCAR_VCON_VSP_H__

#include <drm/drm_plane.h>

#define RCAR_VCON_COLORKEY_NONE         (0 << 24)
#define RCAR_VCON_COLORKEY_MASK         BIT(24)
#define RCAR_VCON_COLORKEY_EN_MASK      RCAR_VCON_COLORKEY_MASK
#define RCAR_VCON_COLORKEY_COLOR_MASK   0xFFFFFF
#define RCAR_VCON_COLORKEY_ALPHA_MASK   0xFF

struct drm_framebuffer;
struct rcar_vcon_format_info;
struct rcar_vcon_vsp;
struct sg_table;

struct rcar_vcon_vsp_plane {
	struct drm_plane plane;
	struct rcar_vcon_vsp *vsp;
	unsigned int index;
};

struct rcar_vcon_vsp {
	unsigned int index;
	struct device *vsp;
	struct rcar_vcon_device *dev;
	struct rcar_vcon_vsp_plane *planes;
	unsigned int num_planes;
};

static inline struct rcar_vcon_vsp_plane *to_rcar_vsp_plane(struct drm_plane *p)
{
	return container_of(p, struct rcar_vcon_vsp_plane, plane);
}

/**
 * struct rcar_vcon_vsp_plane_state - Driver-specific plane state
 * @state: base DRM plane state
 * @format: information about the pixel format used by the plane
 * @sg_tables: scatter-gather tables for the frame buffer memory
 * @alpha: value of the plane alpha property
 * @colorkey: value of the color for which to apply colorkey_alpha, bit 24
 * tells if it is enabled or not
 * @colorkey_alpha: alpha to be used for pixels with color equal to colorkey
 */
struct rcar_vcon_vsp_plane_state {
	struct drm_plane_state state;

	const struct rcar_vcon_format_info *format;
	struct sg_table sg_tables[3];

	unsigned int alpha;
	u32 colorkey;
	u32 colorkey_alpha;
};

static inline struct rcar_vcon_vsp_plane_state *
to_rcar_vsp_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct rcar_vcon_vsp_plane_state, state);
}

int rcar_vcon_vsp_init(struct rcar_vcon_vsp *vsp, struct device_node *np, unsigned int crtcs);

void rcar_vcon_vsp_enable(struct rcar_vcon_crtc *crtc);
void rcar_vcon_vsp_disable(struct rcar_vcon_crtc *crtc);
void rcar_vcon_vsp_atomic_begin(struct rcar_vcon_crtc *crtc);
void rcar_vcon_vsp_atomic_flush(struct rcar_vcon_crtc *crtc);
int rcar_vcon_set_vmute(struct drm_device *dev, void *data, struct drm_file *file_priv);
int rcar_vcon_vsp_write_back(struct drm_device *dev, void *data, struct drm_file *file_priv);
int rcar_vcon_vsp_map_fb(struct rcar_vcon_vsp *vsp, struct drm_framebuffer *fb,
			 struct sg_table sg_tables[3]);
void rcar_vcon_vsp_unmap_fb(struct rcar_vcon_vsp *vsp, struct drm_framebuffer *fb,
			    struct sg_table sg_tables[3]);

#endif /* __RCAR_DU_VSP_H__ */
