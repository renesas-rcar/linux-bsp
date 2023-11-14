/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_vcon_kms.h  --  R-Car Video Interface Converter Mode Setting
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 *
 */

#ifndef __RCAR_VCON_KMS_H__
#define __RCAR_VCON_KMS_H__

#include <linux/types.h>

struct drm_file;
struct drm_device;
struct drm_mode_create_dumb;
struct rcar_vcon_device;

struct rcar_vcon_format_info {
	u32 fourcc;
	u32 v4l2;
	unsigned int bpp;
	unsigned int planes;
	unsigned int hsub;
	unsigned int pnmr;
	unsigned int edf;
};

const struct rcar_vcon_format_info *rcar_vcon_format_info(u32 fourcc);

int rcar_vcon_modeset_init(struct rcar_vcon_device *rcdu);

int rcar_vcon_dumb_create(struct drm_file *file, struct drm_device *dev,
			  struct drm_mode_create_dumb *args);
int rcar_vcon_async_commit(struct drm_device *dev, struct drm_crtc *crtc);

#endif /* __RCAR_DU_KMS_H__ */
