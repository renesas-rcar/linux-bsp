// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_vcon_encoder.c  --  R-Car Display Unit Encoder
 *
 * Copyright (C) 2013-2018 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/export.h>

#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_panel.h>
#include <drm/drm_simple_kms_helper.h>

#include "rcar_vcon_drv.h"
#include "rcar_vcon_encoder.h"
#include "rcar_vcon_kms.h"

/* -----------------------------------------------------------------------------
 * Encoder
 */

int rcar_vcon_encoder_init(struct rcar_vcon_device *rvcon,
			   enum rcar_vcon_output output,
			   struct device_node *enc_node)
{
	struct rcar_vcon_encoder *renc;
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;
	int encoder_type, ret;

	renc = devm_kzalloc(rvcon->dev, sizeof(*renc), GFP_KERNEL);
	if (!renc)
		return -ENOMEM;

	renc->output = output;
	encoder = rcar_encoder_to_drm_encoder(renc);

	dev_dbg(rvcon->dev, "initializing encoder %pOF for output %u\n",
		enc_node, output);

	/*
	 * create a panel bridge.
	 */
	bridge = of_drm_find_bridge(enc_node);
	if (!bridge) {
		if (output == RCAR_VCON_OUTPUT_DP) {
#if IS_ENABLED(CONFIG_DRM_RCAR_DW_DP)
			ret = -EPROBE_DEFER;
#else
			ret = 0;
#endif
			goto done;
		} else {
			ret = EOPNOTSUPP;

			goto done;
		}
	}

	renc->bridge = bridge;

	if (output == RCAR_VCON_OUTPUT_DP)
		encoder_type = DRM_MODE_ENCODER_DPMST;
	else
		encoder_type = DRM_MODE_ENCODER_NONE;

	ret = drm_simple_encoder_init(rvcon->ddev, encoder, encoder_type);
	if (ret)
		goto done;

	/*
	 * Attach the bridge to the encoder. The bridge will create the
	 * connector.
	 */
	ret = drm_bridge_attach(encoder, bridge, NULL, 0);
	if (ret) {
		drm_encoder_cleanup(encoder);
		return ret;
	}

done:
	if (ret) {
		if (encoder->name)
			encoder->funcs->destroy(encoder);
		devm_kfree(rvcon->dev, renc);
	}

	return ret;
}
