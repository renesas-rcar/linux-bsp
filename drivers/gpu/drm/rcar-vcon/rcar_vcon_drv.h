/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_vcon_drv.h  --  R-Car Video Interface Converter DRM driver
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 *
 */

#ifndef __RCAR_VCON_DRV_H__
#define __RCAR_VCON_DRV_H__

#include <linux/kernel.h>
#include <linux/wait.h>

#include "rcar_vcon_crtc.h"
#include "rcar_vcon_vsp.h"

struct clk;
struct device;
struct drm_bridge;
struct drm_device;
struct drm_property;
struct rcar_vcon_device;

struct rcar_vcon_output_routing {
	unsigned int possible_crtcs;
	unsigned int possible_clones;
	unsigned int port;
};

#define RCAR_VCON_MAX_CRTCS		4
#define RCAR_VCON_MAX_VSPS		4

struct rcar_vcon_device_info {
	struct rcar_vcon_output_routing routes[RCAR_VCON_OUTPUT_MAX];
};

struct rcar_vcon_device {
	struct device *dev;
	const struct rcar_vcon_device_info *info;

	struct drm_device *ddev;

	u32 num_crtcs;

	struct rcar_vcon_crtc crtcs[RCAR_VCON_MAX_CRTCS];

	struct rcar_vcon_vsp vsps[RCAR_VCON_MAX_VSPS];

	struct {
		struct drm_property *alpha;
		struct drm_property *colorkey;
		struct drm_property *colorkey_alpha;
	} props;

	unsigned int vspd1_sink;

	bool mode_config_initialized;
};

#endif /* __RCAR_VCON_DRV_H__ */
