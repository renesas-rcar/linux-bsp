/*
 * Copyright (C) 2017 Renesas Electronics Corporation
 *
 * drivers/gpu/drm/panel/panel-jdi-lam123g068a.c
 *     This file is JDI LAM123G068A LCD Driver.
 *
 * This file is based on the
 * drivers/gpu/drm/panel/panel-lg-lg4573.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_panel.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

struct lam123g068a {
	struct device *dev;

	struct drm_panel panel;
	struct spi_device *spi;
	struct videomode vm;
	struct backlight_device *backlight;

	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;
};

static inline struct lam123g068a *panel_to_lam123g068a(struct drm_panel *panel)
{
	return container_of(panel, struct lam123g068a, panel);
}

static int lam123g068a_spi_write_u32(struct lam123g068a *ctx, u32 data)
{
	struct spi_transfer xfer = {
		.len = 4,
		.tx_buf = &data,
	};
	struct spi_message msg;

	dev_dbg(ctx->panel.dev, "writing data: %x\n", data);
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(ctx->spi, &msg);
}

static int lam123g068a_spi_write_u32_array(struct lam123g068a *ctx,
					   const u32 *buffer,
					   unsigned int count)
{
	unsigned int i;
	int ret;

	for (i = 0; i < count; i++) {
		ret = lam123g068a_spi_write_u32(ctx, buffer[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int lam123g068a_power_on(struct lam123g068a *ctx)
{
	static const u32 power_on_settings[] = {
		0x0080e001,
	};

	dev_dbg(ctx->panel.dev, "power on\n");
	return lam123g068a_spi_write_u32_array(ctx, power_on_settings,
					  ARRAY_SIZE(power_on_settings));
}

static int lam123g068a_disable(struct drm_panel *panel)
{
	struct lam123g068a *ctx = panel_to_lam123g068a(panel);

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		ctx->backlight->props.state |= BL_CORE_FBBLANK;
		backlight_update_status(ctx->backlight);
	}

	return 0;
}

static int lam123g068a_prepare(struct drm_panel *panel)
{
	struct lam123g068a *ctx = panel_to_lam123g068a(panel);

	if (ctx->enable_gpio)
		gpiod_set_value_cansleep(ctx->enable_gpio, 1);

	return 0;
}

static int lam123g068a_unprepare(struct drm_panel *panel)
{
	struct lam123g068a *ctx = panel_to_lam123g068a(panel);

	if (ctx->enable_gpio)
		gpiod_set_value_cansleep(ctx->enable_gpio, 0);

	return 0;
}

static int lam123g068a_enable(struct drm_panel *panel)
{
	struct lam123g068a *ctx = panel_to_lam123g068a(panel);

	if (ctx->backlight) {
		ctx->backlight->props.state &= ~BL_CORE_FBBLANK;
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	return lam123g068a_power_on(ctx);
}

static const struct drm_display_mode default_mode = {
	.clock = 98700,		/* refresh rate = 60Hz */
	.hdisplay = 1920,
	.hsync_start = 1920 + 88,
	.hsync_end = 1920 + 88 + 44,
	.htotal = 1920 + 44 + 88 + 148,
	.vdisplay = 720,
	.vsync_start = 720 + 4,
	.vsync_end = 720 + 4 + 20,
	.vtotal = 720 + 20 + 4 + 4,
	.vrefresh = 60,
};

static int lam123g068a_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	panel->connector->display_info.width_mm = 292;
	panel->connector->display_info.height_mm = 109;

	return 1;
}

static const struct drm_panel_funcs lam123g068a_drm_funcs = {
	.disable = lam123g068a_disable,
	.enable = lam123g068a_enable,
	.prepare = lam123g068a_prepare,
	.unprepare = lam123g068a_unprepare,
	.get_modes = lam123g068a_get_modes,
};

static int lam123g068a_probe(struct spi_device *spi)
{
	struct lam123g068a *ctx;
	struct device_node *np;
	int ret;

	ctx = devm_kzalloc(&spi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = &spi->dev;
	ctx->spi = spi;

	spi_set_drvdata(spi, ctx);
	spi->bits_per_word = 24;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "SPI setup failed: %d\n", ret);
		return ret;
	}

	/* Get GPIOs and backlight controller. */
	ctx->enable_gpio = devm_gpiod_get_optional(ctx->dev, "enable",
						   GPIOD_OUT_LOW);
	if (IS_ERR(ctx->enable_gpio)) {
		ret = PTR_ERR(ctx->enable_gpio);
		dev_err(ctx->dev, "failed to request %s GPIO: %d\n",
			"enable", ret);
		return ret;
	}

	ctx->reset_gpio = devm_gpiod_get_optional(ctx->dev, "reset",
						  GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(ctx->dev, "failed to request %s GPIO: %d\n",
			"reset", ret);
		return ret;
	}

	np = of_parse_phandle(ctx->dev->of_node, "backlight", 0);
	if (np) {
		ctx->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = &spi->dev;
	ctx->panel.funcs = &lam123g068a_drm_funcs;

	return drm_panel_add(&ctx->panel);
}

static int lam123g068a_remove(struct spi_device *spi)
{
	struct lam123g068a *ctx = spi_get_drvdata(spi);

	drm_panel_detach(&ctx->panel);
	drm_panel_remove(&ctx->panel);

	if (ctx->backlight)
		put_device(&ctx->backlight->dev);

	return 0;
}

static const struct of_device_id lam123g068a_of_match[] = {
	{ .compatible = "jdi,lam123g068a" },
	{ }
};
MODULE_DEVICE_TABLE(of, lam123g068a_of_match);

static struct spi_driver lam123g068a_driver = {
	.probe = lam123g068a_probe,
	.remove = lam123g068a_remove,
	.driver = {
		.name = "lam123g068a",
		.of_match_table = lam123g068a_of_match,
	},
};
module_spi_driver(lam123g068a_driver);

MODULE_AUTHOR("Koji Matsuoka <koji.matsuoka.xm@renesas.com>");
MODULE_DESCRIPTION("JDI LAM123G068A LCD Driver");
MODULE_LICENSE("GPL v2");

