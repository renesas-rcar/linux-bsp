// SPDX-License-Identifier: GPL-2.0
/*
 * rcar_dsc.c  --  R-Car MIPI DSC
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/reset.h>

#include <drm/drm_bridge.h>
#include <drm/drm_of.h>

#include "rcar_dsc.h"

struct rcar_dsc;

#define bridge_to_rcar_dsc(b) \
	container_of(b, struct rcar_dsc, bridge)

/* -----------------------------------------------------------------------------
 * Bridge
 */

static int rcar_dsc_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct rcar_dsc *dsc = bridge_to_rcar_dsc(bridge);

	return drm_bridge_attach(bridge->encoder, dsc->next_bridge, bridge,
				 flags);
}

static void rcar_dsc_enable(struct drm_bridge *bridge)
{
	struct rcar_dsc *dsc = bridge_to_rcar_dsc(bridge);

	reset_control_deassert(dsc->rstc);

	clk_prepare_enable(dsc->mod_clk);
}

static void rcar_dsc_disable(struct drm_bridge *bridge)
{
	struct rcar_dsc *dsc = bridge_to_rcar_dsc(bridge);

	clk_disable_unprepare(dsc->mod_clk);

	reset_control_assert(dsc->rstc);
}

static const struct drm_bridge_funcs rcar_dsc_bridge_ops = {
	.attach = rcar_dsc_attach,
	.enable = rcar_dsc_enable,
	.disable = rcar_dsc_disable,
};


/* -----------------------------------------------------------------------------
 * Probe & Remove
 */
static int rcar_dsc_probe(struct platform_device *pdev)
{
	struct rcar_dsc *dsc;
	struct resource *mem;
	int ret;

	dsc = devm_kzalloc(&pdev->dev, sizeof(*dsc), GFP_KERNEL);
	if (dsc == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, dsc);

	dsc->dev = &pdev->dev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dsc->mmio = devm_ioremap_resource(dsc->dev, mem);
	if (IS_ERR(dsc->mmio))
		return PTR_ERR(dsc->mmio);

	dsc->mod_clk = devm_clk_get(dsc->dev, NULL);
	if (IS_ERR(dsc->mod_clk)) {
		dev_err(dsc->dev, "failed to get clock\n");
		return PTR_ERR(dsc->mod_clk);
	}

	dsc->rstc = devm_reset_control_get(dsc->dev, NULL);
	if (IS_ERR(dsc->rstc)) {
		dev_err(dsc->dev, "failed to get cpg reset\n");
		return PTR_ERR(dsc->rstc);
	}

	/* Init bridge */
	ret = drm_of_find_panel_or_bridge(dsc->dev->of_node, 1, 0,
					0, &dsc->next_bridge);

	if (ret) {
		return -EPROBE_DEFER;
	}

	dsc->bridge.driver_private = dsc;
	dsc->bridge.funcs = &rcar_dsc_bridge_ops;
	dsc->bridge.of_node = pdev->dev.of_node;
	drm_bridge_add(&dsc->bridge);

	return 0;
}

static int rcar_dsc_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id rcar_dsc_of_table[] = {
	{ .compatible = "renesas,r8a779g0-dsc" },
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_dsc_of_table);

static struct platform_driver rcar_dsc_platform_driver = {
	.probe          = rcar_dsc_probe,
	.remove         = rcar_dsc_remove,
	.driver         = {
		.name   = "rcar-dsc",
		.of_match_table = rcar_dsc_of_table,
	},
};

module_platform_driver(rcar_dsc_platform_driver);

MODULE_DESCRIPTION("Renesas Display Stream Compression Driver");
MODULE_LICENSE("GPL");
