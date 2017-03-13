/*
 * R-Car Display Unit HDMI Encoder
 *
 * Copyright (C) 2014-2017 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/slab.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include <linux/of_platform.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_hdmienc.h"
#include "rcar_du_lvdsenc.h"

struct rcar_du_hdmienc {
	struct rcar_du_encoder *renc;
	struct device *dev;
	bool enabled;
	unsigned int index;
};

#define to_rcar_hdmienc(e)	(to_rcar_encoder(e)->hdmi)

void rcar_du_hdmienc_suspend(struct drm_encoder *encoder)
{
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;

	if ((bfuncs) && (bfuncs->disable))
		bfuncs->disable(encoder->bridge);
}

#define SMSTPCR7 0xE615014C

void rcar_du_hdmienc_resume(struct drm_encoder *encoder)
{
	void __iomem *smstpcr = ioremap_nocache(SMSTPCR7, 4);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;
	u32 temp;

	temp = readl(smstpcr);
	writel(temp & ~(0x3 << 28), smstpcr);

	if ((bfuncs) && (bfuncs->enable))
		bfuncs->enable(encoder->bridge);
}

void rcar_du_hdmienc_disable(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;

	if ((bfuncs) && (bfuncs->disable))
		bfuncs->disable(encoder->bridge);

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_enable(hdmienc->renc->lvds, encoder->crtc,
				       false);

	hdmienc->enabled = false;
}

void rcar_du_hdmienc_enable(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_enable(hdmienc->renc->lvds, encoder->crtc,
				       true);
	if ((bfuncs) && (bfuncs->enable))
		bfuncs->enable(encoder->bridge);

	hdmienc->enabled = true;
}

static int rcar_du_hdmienc_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	const struct drm_display_mode *mode = &crtc_state->mode;
	int ret = 0;

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_atomic_check(hdmienc->renc->lvds,
					     adjusted_mode);

	if ((bfuncs) && (bfuncs->mode_fixup))
		ret = bfuncs->mode_fixup(encoder->bridge, mode,
				 adjusted_mode) ? 0 : -EINVAL;
	return ret;
}


static void rcar_du_hdmienc_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;

	if ((bfuncs) && (bfuncs->mode_set))
		bfuncs->mode_set(encoder->bridge, mode, adjusted_mode);

	rcar_du_crtc_route_output(encoder->crtc, hdmienc->renc->output);
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.mode_set = rcar_du_hdmienc_mode_set,
	.disable = rcar_du_hdmienc_disable,
	.enable = rcar_du_hdmienc_enable,
	.atomic_check = rcar_du_hdmienc_atomic_check,
};

static void rcar_du_hdmienc_cleanup(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);

	if (hdmienc->enabled)
		rcar_du_hdmienc_disable(encoder);

	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = rcar_du_hdmienc_cleanup,
};

static const struct dw_hdmi_mpll_config rcar_du_hdmienc_mpll_cfg[] = {
	{
		44900000, {
			{ 0x0003, 0x0000},
			{ 0x0003, 0x0000},
			{ 0x0003, 0x0000}
		},
	}, {
		90000000, {
			{ 0x0002, 0x0000},
			{ 0x0002, 0x0000},
			{ 0x0002, 0x0000}
		},
	}, {
		182750000, {
			{ 0x0001, 0x0000},
			{ 0x0001, 0x0000},
			{ 0x0001, 0x0000}
		},
	}, {
		297000000, {
			{ 0x0000, 0x0000},
			{ 0x0000, 0x0000},
			{ 0x0000, 0x0000}
		},
	}, {
		~0UL, {
			{ 0xFFFF, 0xFFFF },
			{ 0xFFFF, 0xFFFF },
			{ 0xFFFF, 0xFFFF },
		},
	}
};

static const struct dw_hdmi_curr_ctrl rcar_du_hdmienc_cur_ctr[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		35500000,  { 0x0283, 0x0000, 0x0000 },
	}, {
		44900000,  { 0x0285, 0x0000, 0x0000 },
	}, {
		71000000,  { 0x1183, 0x0000, 0x0000 },
	}, {
		90000000,  { 0x1142, 0x0000, 0x0000 },
	}, {
		140250000, { 0x20c0, 0x0000, 0x0000 },
	}, {
		182750000, { 0x2080, 0x0000, 0x0000 },
	}, {
		281250000, { 0x3040, 0x0000, 0x0000 },
	}, {
		297000000, { 0x3041, 0x0000, 0x0000 },
	}, {
		~0UL,      { 0x0000, 0x0000, 0x0000 },
	}
};

static const struct dw_hdmi_multi_div rcar_du_hdmienc_multi_div[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		35500000,  { 0x0628, 0x0000, 0x0000 },
	}, {
		44900000,  { 0x0228, 0x0000, 0x0000 },
	}, {
		71000000,  { 0x0614, 0x0000, 0x0000 },
	}, {
		90000000,  { 0x0214, 0x0000, 0x0000 },
	}, {
		140250000, { 0x060a, 0x0000, 0x0000 },
	}, {
		182750000, { 0x020a, 0x0000, 0x0000 },
	}, {
		281250000, { 0x0605, 0x0000, 0x0000 },
	}, {
		297000000, { 0x0405, 0x0000, 0x0000 },
	}, {
		~0UL,      { 0x0000, 0x0000, 0x0000 },
	}
};

static const struct dw_hdmi_phy_config rcar_du_hdmienc_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 165000000, 0x0c88, 0x0007, 0x000c},
	{ 297000000, 0x03c8, 0x0004, 0x000c},
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static enum drm_mode_status
rcar_du_hdmienc_mode_valid(struct drm_connector *connector,
			    struct drm_display_mode *mode)
{
	if ((mode->hdisplay > 3840) || (mode->vdisplay > 2160))
		return MODE_BAD;

	if (((mode->hdisplay == 3840) && (mode->vdisplay == 2160))
		&& (mode->vrefresh > 30))
		return MODE_BAD;

	if (mode->clock > 297000)
		return MODE_BAD;

	return MODE_OK;
}

static const struct dw_hdmi_plat_data rcar_du_hdmienc_hdmi0_drv_data = {
	.mode_valid = rcar_du_hdmienc_mode_valid,
	.mpll_cfg   = rcar_du_hdmienc_mpll_cfg,
	.cur_ctr    = rcar_du_hdmienc_cur_ctr,
	.multi_div  = rcar_du_hdmienc_multi_div,
	.phy_config = rcar_du_hdmienc_phy_config,
	.dev_type   = RCAR_HDMI,
	.index      = 0,
};

static const struct dw_hdmi_plat_data rcar_du_hdmienc_hdmi1_drv_data = {
	.mode_valid = rcar_du_hdmienc_mode_valid,
	.mpll_cfg   = rcar_du_hdmienc_mpll_cfg,
	.cur_ctr    = rcar_du_hdmienc_cur_ctr,
	.multi_div  = rcar_du_hdmienc_multi_div,
	.phy_config = rcar_du_hdmienc_phy_config,
	.dev_type   = RCAR_HDMI,
	.index      = 1,
};

static const struct of_device_id rcar_du_hdmienc_dt_ids[] = {
	{
		.data = &rcar_du_hdmienc_hdmi0_drv_data
	},
	{
		.data = &rcar_du_hdmienc_hdmi1_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_du_hdmienc_dt_ids);

int rcar_du_hdmienc_init(struct rcar_du_device *rcdu,
			 struct rcar_du_encoder *renc, struct device_node *np)
{
	struct drm_encoder *encoder = rcar_encoder_to_drm_encoder(renc);
	struct rcar_du_hdmienc *hdmienc;
	struct resource *iores;
	struct platform_device *pdev;
	const struct dw_hdmi_plat_data *plat_data;
	int ret, irq;
	bool dw_hdmi_use = false;
	struct drm_bridge *bridge = NULL;

	hdmienc = devm_kzalloc(rcdu->dev, sizeof(*hdmienc), GFP_KERNEL);
	if (hdmienc == NULL)
		return -ENOMEM;

	if (strcmp(renc->device_name, "renesas,rcar-dw-hdmi") == 0) {
		dw_hdmi_use = true;

		if (renc->output == RCAR_DU_OUTPUT_HDMI0)
			hdmienc->index = 0;
		else if (renc->output == RCAR_DU_OUTPUT_HDMI1)
			hdmienc->index = 1;
		else
			return -EINVAL;

		pdev = of_find_device_by_node(np);
		of_node_put(np);
		if (!pdev)
			return -ENXIO;

		plat_data = rcar_du_hdmienc_dt_ids[hdmienc->index].data;
		hdmienc->dev = &pdev->dev;

		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;

		iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!iores)
			return -ENXIO;

	} else {
		/* Locate the DRM bridge from the HDMI encoder DT node. */
		bridge = of_drm_find_bridge(np);
		if (!bridge)
			return -EPROBE_DEFER;
	}

	ret = drm_encoder_init(rcdu->ddev, encoder, &encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret < 0)
		return ret;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);

	renc->hdmi = hdmienc;
	hdmienc->renc = renc;

	/* Link the bridge to the encoder. */
	if (bridge) {
		bridge->encoder = encoder;
		encoder->bridge = bridge;
	}

	if (dw_hdmi_use)
		ret = dw_hdmi_bind(hdmienc->dev, rcdu->dev, rcdu->ddev,
				   encoder, iores, irq, plat_data);

	if (bridge) {
		ret = drm_bridge_attach(rcdu->ddev, bridge);
		if (ret) {
			drm_encoder_cleanup(encoder);
			return ret;
		}
	}

	return 0;
}
