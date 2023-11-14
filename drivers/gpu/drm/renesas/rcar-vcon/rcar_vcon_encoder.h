/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_du_encoder.h  --  R-Car Video Interface Converter Encoder
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 *
 */

#ifndef __RCAR_VCON_ENCODER_H__
#define __RCAR_VCON_ENCODER_H__

#include <drm/drm_encoder.h>

struct rcar_vcon_device;

struct rcar_vcon_encoder {
	struct drm_encoder base;
	enum rcar_vcon_output output;
	struct drm_bridge *bridge;
};

#define to_rcar_encoder(e) \
	container_of(e, struct rcar_vcon_encoder, base)

#define rcar_encoder_to_drm_encoder(e)	(&(e)->base)

int rcar_vcon_encoder_init(struct rcar_vcon_device *rvcon,
			   enum rcar_vcon_output output,
			   struct device_node *enc_node);

#endif /* __RCAR_VCON_ENCODER_H__ */
