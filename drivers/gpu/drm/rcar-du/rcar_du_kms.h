/*
 * rcar_du_kms.h  --  R-Car Display Unit Mode Setting
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

#ifndef __RCAR_DU_KMS_H__
#define __RCAR_DU_KMS_H__

#include <linux/types.h>

struct dma_buf_attachment;
struct drm_file;
struct drm_device;
struct drm_gem_object;
struct drm_mode_create_dumb;
struct rcar_du_device;
struct sg_table;

struct rcar_du_format_info {
	u32 fourcc;
	unsigned int bpp;
	unsigned int planes;
	unsigned int pnmr;
	unsigned int edf;
};

const struct rcar_du_format_info *rcar_du_format_info(u32 fourcc);

int rcar_du_modeset_init(struct rcar_du_device *rcdu);

int rcar_du_dumb_create(struct drm_file *file, struct drm_device *dev,
			struct drm_mode_create_dumb *args);
int rcar_du_async_commit(struct drm_device *dev, struct drm_crtc *crtc);

struct drm_gem_object *rcar_du_gem_prime_import_sg_table(struct drm_device *dev,
				struct dma_buf_attachment *attach,
				struct sg_table *sgt);

#endif /* __RCAR_DU_KMS_H__ */
