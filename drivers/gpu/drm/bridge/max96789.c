// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_edid.h>

struct max96789_priv {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct gpio_desc *gpiod_pwdn;
	struct device_node		*host_node;

	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
};

#define bridge_to_max96789_priv(b) \
	container_of(b, struct max96789_priv, bridge)

#define connector_to_max96789_priv(c) \
	container_of(c, struct max96789_priv, connector);

/* -----------------------------------------------------------------------------
 * DRM Bridge Operations
 */
static int max96789_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	int ret;
	struct max96789_priv *priv = bridge_to_max96789_priv(bridge);
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	const struct mipi_dsi_device_info info = { .type = "max96789_bridge",
						   .channel = 0,
						   .node = NULL,
						 };

	host = of_find_mipi_dsi_host_by_node(priv->host_node);
	if (!host) {
		DRM_ERROR("failed to find dsi host\n");
		return -ENODEV;
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		DRM_ERROR("failed to create dsi device\n");
		return PTR_ERR(dsi);
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		DRM_ERROR("failed to attach dsi to host\n");
		mipi_dsi_device_unregister(dsi);
		return ret;
	}

	return drm_bridge_attach(bridge->encoder, priv->next_bridge, bridge,
			flags);
}

static void max96789_bridge_enable(struct drm_bridge *bridge)
{
	struct max96789_priv *priv = bridge_to_max96789_priv(bridge);

	gpiod_set_value_cansleep(priv->gpiod_pwdn, 1);
}

static void max96789_bridge_disable(struct drm_bridge *bridge)
{
	struct max96789_priv *priv = bridge_to_max96789_priv(bridge);

	gpiod_set_value_cansleep(priv->gpiod_pwdn, 0);
}

static const struct drm_bridge_funcs max96789_bridge_funcs = {
	.attach = max96789_bridge_attach,
	.enable = max96789_bridge_enable,
	.disable = max96789_bridge_disable,
};

static const struct regmap_config max96789_i2c_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x1f00,
};

static int max96789_bridge_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max96789_priv *priv;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->dev = dev;

	priv->regmap = devm_regmap_init_i2c(client, &max96789_i2c_regmap);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->gpiod_pwdn = devm_gpiod_get_optional(&client->dev, "enable",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_pwdn))
		return PTR_ERR(priv->gpiod_pwdn);

	gpiod_set_consumer_name(priv->gpiod_pwdn, "max96789-pwdn");

	if (priv->gpiod_pwdn)
		usleep_range(4000, 5000);

	ret = drm_of_find_panel_or_bridge(priv->dev->of_node, 1, 0,
				NULL, &priv->next_bridge);
	if (ret) {
		DRM_ERROR("could not find bridge node\n");
		return ret;
	}

	priv->host_node = of_graph_get_remote_node(priv->dev->of_node
				, 0, 0);
	priv->bridge.driver_private = priv;
	priv->bridge.funcs = &max96789_bridge_funcs;
	priv->bridge.of_node = priv->dev->of_node;
	drm_bridge_add(&priv->bridge);

	return 0;
}

static int max96789_bridge_remove(struct i2c_client *client)
{
	return 0;
}

static const struct of_device_id max96789_bridge_match_table[] = {
	{.compatible = "maxim,max96789"},
	{},
};
MODULE_DEVICE_TABLE(of, max96789_bridge_match_table);

static struct i2c_driver max96789_bridge_driver = {
	.driver = {
		.name = "maxim-max96789",
		.of_match_table = max96789_bridge_match_table,
	},
	.probe_new = max96789_bridge_probe,
	.remove = max96789_bridge_remove,
};
module_i2c_driver(max96789_bridge_driver);

MODULE_DESCRIPTION("max96789 driver");
MODULE_LICENSE("GPL v2");
