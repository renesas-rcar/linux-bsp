/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rcar_mipi_dsi.h  --  R-Car MIPI_DSI Encoder
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 */

#ifndef __RCAR_MIPI_DSI_H__
#define __RCAR_MIPI_DSI_H__

struct drm_bridge;

#if IS_ENABLED(CONFIG_DRM_RCAR_MIPI_DSI)
int rcar_mipi_dsi_clk_enable(struct drm_bridge *bridge);
void rcar_mipi_dsi_clk_disable(struct drm_bridge *bridge);

#else
static inline int rcar_mipi_dsi_clk_enable(struct drm_bridge *bridge)
{
	return -ENOSYS;
}
static inline void rcar_mipi_dsi_clk_disable(struct drm_bridge *bridge) { }

#endif /* CONFIG_DRM_RCAR_MIPI_DSI */

#endif /* __RCAR_MIPI_DSI_H__ */
