// SPDX-License-Identifier: GPL-2.0
/*
 * rcar_dw_dp.c  --  R-Car Designware Display port
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/iopoll.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#define bridge_to_rcar_dw_dp(b) \
	container_of(b, struct rcar_dw_dp, bridge)

#define connector_to_rcar_dw_dp(c) \
	container_of(c, struct rcar_dw_dp, connector)

struct rcar_dw_dp {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_connector connector;

	struct drm_display_mode display_mode;
};

/* For simulating connector */
static int con_status;
module_param(con_status, int, 0644);
MODULE_PARM_DESC(con_status, "DP connector status");

/* -----------------------------------------------------------------------------
 * Connector
 */

static int rcar_dw_dp_connector_get_modes(struct drm_connector *connector)
{
	return 0;
}

static enum drm_mode_status rcar_dw_dp_connector_mode_valid(struct drm_connector *connector,
							    struct drm_display_mode *mode)
{
	if (mode->clock > 594000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static struct drm_connector_helper_funcs rcar_dw_dp_connector_helper_funcs = {
	.get_modes = rcar_dw_dp_connector_get_modes,
	.mode_valid = rcar_dw_dp_connector_mode_valid,
};

static enum drm_connector_status rcar_dw_dp_connector_detect(struct drm_connector *connector,
							     bool force)
{
	if (con_status) {
		drm_add_modes_noedid(connector, 4096, 2160);
		return connector_status_connected;
	}

	return connector_status_disconnected;
}

static const struct drm_connector_funcs rcar_dw_dp_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = rcar_dw_dp_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* -----------------------------------------------------------------------------
 * Bridge
 */

static int rcar_dw_dp_attach(struct drm_bridge *bridge, enum drm_bridge_attach_flags flags)
{
	struct rcar_dw_dp *dw_dp = bridge_to_rcar_dw_dp(bridge);
	int ret;

	dw_dp->connector.polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	ret = drm_connector_init(bridge->dev, &dw_dp->connector,
				 &rcar_dw_dp_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);

	if (ret)
		return ret;

	drm_connector_helper_add(&dw_dp->connector, &rcar_dw_dp_connector_helper_funcs);

	drm_connector_attach_encoder(&dw_dp->connector, bridge->encoder);

	return 0;
}

static void rcar_dw_dp_pre_enable(struct drm_bridge *bridge)
{
}

static void rcar_dw_dp_mode_set(struct drm_bridge *bridge,
				const struct drm_display_mode *mode,
				const struct drm_display_mode *adjusted_mode)
{
	struct rcar_dw_dp *dw_dp = bridge_to_rcar_dw_dp(bridge);

	dw_dp->display_mode = *adjusted_mode;
}

static void rcar_dw_dp_enable(struct drm_bridge *bridge)
{
}

static void rcar_dw_dp_disable(struct drm_bridge *bridge)
{
}

static void rcar_dw_dp_post_disable(struct drm_bridge *bridge)
{
}

static enum drm_mode_status rcar_dw_dp_bridge_mode_valid(struct drm_bridge *bridge,
							 const struct drm_display_info *info,
							 const struct drm_display_mode *mode)
{
	return MODE_OK;
}

static const struct drm_bridge_funcs rcar_dw_dp_bridge_ops = {
	.attach = rcar_dw_dp_attach,
	.pre_enable = rcar_dw_dp_pre_enable,
	.mode_set = rcar_dw_dp_mode_set,
	.enable = rcar_dw_dp_enable,
	.disable = rcar_dw_dp_disable,
	.post_disable = rcar_dw_dp_post_disable,
	.mode_valid = rcar_dw_dp_bridge_mode_valid,
};

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int rcar_dw_dp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_dw_dp *dw_dp;

	dw_dp = devm_kzalloc(&pdev->dev, sizeof(*dw_dp), GFP_KERNEL);
	if (!dw_dp)
		return -ENOMEM;

	platform_set_drvdata(pdev, dw_dp);

	dw_dp->dev = dev;

	/* Init bridge */
	dw_dp->bridge.driver_private = dw_dp;
	dw_dp->bridge.funcs = &rcar_dw_dp_bridge_ops;
	dw_dp->bridge.of_node = pdev->dev.of_node;

	drm_bridge_add(&dw_dp->bridge);

	return 0;
}

static int rcar_dw_dp_remove(struct platform_device *pdev)
{
	struct rcar_dw_dp *dw_dp = platform_get_drvdata(pdev);

	drm_bridge_remove(&dw_dp->bridge);

	return 0;
}

static const struct of_device_id rcar_dw_dp_of_table[] = {
	{
		.compatible = "renesas,r8a78000-dw-dp",
	},
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, rcar_dw_dp_of_table);

static struct platform_driver rcar_dw_dp_platform_driver = {
	.probe          = rcar_dw_dp_probe,
	.remove         = rcar_dw_dp_remove,
	.driver         = {
		.name   = "rcar-dw-dp",
		.of_match_table = rcar_dw_dp_of_table,
	},
};

module_platform_driver(rcar_dw_dp_platform_driver);

MODULE_DESCRIPTION("Renesas R-Car DesignWare Display port Driver");
MODULE_LICENSE("GPL");
