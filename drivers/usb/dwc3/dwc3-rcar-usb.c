// SPDX-License-Identifier: GPL-2.0-only
/*
 * Renesas USB device driver with DWC3 integration
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/usb/ch9.h>
#include <linux/usb/of.h>

#define USB_CTRL_CONF19		0x26

/* USB_CTRL_CONF19 register bits */
#define USB_CTRL_CONF19_HNU2P		(0x1 << 0)
#define USB_CTRL_CONF19_HNU3P		(0x0 << 4)
#define USB_CTRL_CONF19_HU2PD		(0x0 << 8)
#define USB_CTRL_CONF19_HU3PD		(0x1 << 9)
#define USB_PHY_RESSHR_CTRL		0x81c
#define USB_PHY_CONF13			0x81a
#define USB_PHY_CONF1			0x802
#define USB_CTRL_CONF21			0x2a

#define USB_CTRL_CONF19_CONFIG		(USB_CTRL_CONF19_HNU2P | \
					 USB_CTRL_CONF19_HNU3P | \
					 USB_CTRL_CONF19_HU2PD | \
					 USB_CTRL_CONF19_HU3PD)
#define HIGH_SPEED		0
#define SUPER_SPEED_PLUS	1

struct usb_priv {
	struct device		*dev;
	void __iomem		*base;
	struct clk 		*clk;
	struct reset_control	*resets;
	struct phy		*usb3_phy;
	struct platform_device	*dwc3;
	bool			has_usb3;
	enum usb_dr_mode	dr_mode;
	enum usb_device_speed	maximum_speed;
	bool			use_usb3_flow;
};

static void usb_configure_registers(struct usb_priv *priv)
{
	/* USB Controller Configuration Register 19 */
	writew(0x211, priv->base + USB_CTRL_CONF19);

	dev_info(priv->dev, "USB Controller Configuration Register 19 configured\n");
}

static int init_usb3_phy(struct usb_priv *priv)
{
	int ret;

	if (!priv->usb3_phy)
		return 0;

	/* Set USB3 mode for MP-PHY based on dr_mode */
	switch (priv->dr_mode) {
	case USB_DR_MODE_HOST:
		ret = phy_set_mode(priv->usb3_phy, PHY_MODE_USB_HOST);
		break;
	case USB_DR_MODE_PERIPHERAL:
		ret = phy_set_mode(priv->usb3_phy, PHY_MODE_USB_DEVICE);
		break;
	case USB_DR_MODE_OTG:
	default:
		ret = phy_set_mode(priv->usb3_phy, PHY_MODE_USB_OTG);
		break;
	}

	if (ret) {
		dev_err(priv->dev, "Failed to set USB3 PHY mode: %d\n", ret);
		return ret;
	}

	/* Initialize MP-PHY for USB3 - calls mp_phy_init_usb() */
	ret = phy_init(priv->usb3_phy);
	if (ret) {
		dev_err(priv->dev, "Failed to initialize USB3 PHY: %d\n", ret);
		return ret;
	}

	dev_info(priv->dev, "USB3 MP-PHY initialized successfully\n");
	return 0;
}

/*
 * Parse DWC3 child node properties and create DWC3 platform device
 * This combines USB mode determination and device creation in one step
 * VERSION 2.0: Single function, single of_platform_populate() call
 */
static int rcar_gen5_usb_setup_dwc3(struct usb_priv *priv)
{
	struct device *dev = priv->dev;
	struct device_node *child;
	const char *maximum_speed;
	const char *dr_mode_str;
	int ret;

	/* Find DWC3 child node to check properties - SINGLE DT TRAVERSE */
	child = of_get_compatible_child(dev->of_node, "synopsys,dwc3");
	if (!child) {
		dev_err(dev, "Failed to find DWC3 child node\n");
		return -ENODEV;
	}

	/* Parse dr_mode property from child node */
	ret = of_property_read_string(child, "dr_mode", &dr_mode_str);
	if (ret) {
		dev_info(dev, "dr_mode not specified, defaulting to OTG\n");
		priv->dr_mode = USB_DR_MODE_OTG;
		dr_mode_str = "otg";
	} else {
		if (!strcmp(dr_mode_str, "host"))
			priv->dr_mode = USB_DR_MODE_HOST;
		else if (!strcmp(dr_mode_str, "peripheral") || !strcmp(dr_mode_str, "device"))
			priv->dr_mode = USB_DR_MODE_PERIPHERAL;
		else if (!strcmp(dr_mode_str, "otg"))
			priv->dr_mode = USB_DR_MODE_OTG;
		else {
			dev_warn(dev, "Invalid dr_mode '%s', defaulting to OTG\n", dr_mode_str);
			priv->dr_mode = USB_DR_MODE_OTG;
			dr_mode_str = "otg";
		}
	}

	/* Parse maximum-speed property to determine USB mode */
	ret = of_property_read_string(child, "maximum-speed", &maximum_speed);
	if (ret) {
		/* Default to USB3 mode if not specified and USB3 PHY available */
		priv->use_usb3_flow = priv->has_usb3;
		priv->maximum_speed = USB_SPEED_SUPER_PLUS;
		maximum_speed = "default";
	} else {
		if (!strcmp(maximum_speed, "super-speed-plus")) {
			priv->maximum_speed = USB_SPEED_SUPER_PLUS;
			priv->use_usb3_flow = true;
		} else if (!strcmp(maximum_speed, "super-speed")) {
			priv->maximum_speed = USB_SPEED_SUPER;
			priv->use_usb3_flow = true;
		} else if (!strcmp(maximum_speed, "high-speed")) {
			priv->maximum_speed = USB_SPEED_HIGH;
			priv->use_usb3_flow = false;
		} else {
			priv->maximum_speed = USB_SPEED_FULL;
			priv->use_usb3_flow = false;
		}
	}

	of_node_put(child);

	dev_info(dev, "USB mode: %s (max-speed: %s)\n",
		 priv->use_usb3_flow ? "USB3.1 SuperSpeed" : "USB2.0",
		 maximum_speed);

	/*
	 * Create DWC3 platform devices from device tree
	 * This must be done AFTER hardware initialization is complete
	 * so that PHY and clocks are ready for DWC3 framework
	 */
	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "Failed to populate DWC3 child devices: %d\n", ret);
		return ret;
	}

	dev_info(dev, "DWC3 child device created successfully\n");
	return 0;
}

/*
 * Chapter 94.3.1.1 Using USB3.1 Flow
 * For USB3 controllers operating in SuperSpeed mode
 */
static int rcar_gen5_usb_init_usb31_flow(struct usb_priv *priv)
{
	int ret;

	ret = init_usb3_phy(priv);
	if( ret) {
		dev_err(priv->dev, "Failed USB3 PHY initialization: %d\n", ret);
		return ret;
	}

	usb_configure_registers(priv);

	dev_info(priv->dev, "USB3.1 SuperSpeed flow completed\n");

	return ret;
}

/*
 * Chapter 94.3.1.2 Using USB2.0 Flow
 * For controllers operating in USB2 mode (High-Speed and below)
 */
static int rcar_gen5_usb_init_usb20_flow(struct usb_priv *priv)
{
	int ret;

	dev_info(priv->dev, "Initializing USB2.0 flow\n");

	if (priv->has_usb3) {
		/* USB3 controller operating in USB2 mode - Figure 94.11 */
		dev_info(priv->dev, "USB3 controller in USB2 mode (Figure 94.11)\n");

		ret = init_usb3_phy(priv);
		if( ret) {
			dev_err(priv->dev, "Failed MP-PHY initialization: %d\n", ret);
			return ret;
		}

		usb_configure_registers(priv);
	} else {
		/* Native USB2 controller - Figure 94.12 */
		dev_info(priv->dev, "Native USB2 controller (Figure 94.12)\n");

		/* Step (1): Release USB Module Standby */
		/* Step (2): Setting USB Register */
		usb_configure_registers(priv);
	}

	dev_info(priv->dev, "USB2.0 flow completed\n");
	return 0;
}

static int rcar_gen5_usb_init_hardware(struct usb_priv *priv)
{
	int ret;

	usb_module_power_run();
	msleep(20);

	/*
	 * The datasheet describes initialization procedure without full
	 * information about the registers. Therefore, the source code is based
	 * on the bare metal code shared by the board team.
	 */
	writew(0x00000011, priv->base + USB_PHY_RESSHR_CTRL);
	writew(0x00000000, priv->base + USB_PHY_CONF13);
	writew(0x00000001, priv->base + USB_PHY_CONF1);
	msleep(10);
	writew(0x00000000, priv->base + USB_PHY_CONF1);
	writew(0x00000001, priv->base + USB_CTRL_CONF21);
	writew(0x00000001, priv->base + USB_PHY_CONF13);
	msleep(10);

	/* Execute the appropriate initialization flow */
	if (priv->use_usb3_flow) {
		/* Chapter 94.3.1.1 Using USB3.1 */
		ret = rcar_gen5_usb_init_usb31_flow(priv);
	} else {
		/* Chapter 94.3.1.2 Using USB2.0 */
		ret = rcar_gen5_usb_init_usb20_flow(priv);
	}

	/* Parse DWC3 properties and determine USB mode */
	ret = rcar_gen5_usb_setup_dwc3(priv);
	if (ret)
		return ret;

	if (priv->use_usb3_flow) {
		ret = phy_set_speed(priv->usb3_phy, SUPER_SPEED_PLUS);
		if (ret) {
			dev_err(priv->dev, "Failed to set TCA register in Super-Speed-Plus: %d\n", ret);
			return ret;
		}
	} else {
		ret = phy_set_speed(priv->usb3_phy, HIGH_SPEED);
		if (ret) {
			dev_err(priv->dev, "Failed to set TCA register in High-Speed: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static int rcar_gen5_usb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_priv *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	platform_set_drvdata(pdev, priv);

	/* Get memory resources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base)) {
		dev_err(dev, "Failed to map registers\n");
		return PTR_ERR(priv->base);
	}

	/* TODO: Enable reset control when DTS binding is ready */
	/* Get reset control */
	//priv->reset = devm_reset_control_get(dev, NULL);
	//if (IS_ERR(priv->reset)) {
	//	dev_err(dev, "Failed to get reset control\n");
	//	return PTR_ERR(priv->reset);
	//}

	/* TODO: Enable clock control when clock is available */
	/* Get clocks */
	//priv->clk = devm_clk_get(dev, NULL);
	//if (IS_ERR(priv->clk)) {
	//	dev_err(dev, "Failed to get mp_phy clock\n");
	//	return PTR_ERR(priv->clk);
	//}

	/* Enable clock if available */
	//if (priv->clk) {
	//	int ret = clk_prepare_enable(priv->clk);
	//	if (ret) {
	//		dev_err(dev, "Failed to enable clock: %d\n", ret);
	//		return ret;
	//	}
	//}

	/* Get PHYs - Only USB3 MP-PHY, USB2 PHY controlled by I2C0 */
	priv->usb3_phy = devm_phy_optional_get(dev, "usb3-phy");
	if (IS_ERR(priv->usb3_phy)) {
		ret = PTR_ERR(priv->usb3_phy);
		dev_err(dev, "Failed to get USB3 PHY: %d\n", ret);
		return ret;
	}

	/* Determine USB3 support based on MP-PHY availability */
	priv->has_usb3 = !!priv->usb3_phy;

	/* For USB2.0 only controllers, allow operation without USB3 PHY */
	if (!priv->has_usb3) {
		dev_info(dev, "Operating in USB2.0 mode (no USB3 MP-PHY)\n");
	}

	/* Enable runtime PM early */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	/* Initialize hardware and setup DWC3 */
	ret = rcar_gen5_usb_init_hardware(priv);
	if (ret) {
		dev_err(dev, "Failed to initialize hardware: %d\n", ret);
		goto err_pm;
	}

	dev_info(dev, "Renesas USB probed successfully\n");

	return 0;

err_pm:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	return ret;
}

static int rcar_gen5_usb_remove(struct platform_device *pdev)
{
	struct usb_priv *priv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	/* SINGLE of_platform_depopulate() CALL - LINE 367 (cleanup only) */
	of_platform_depopulate(dev);

	/* Cleanup PHY */
	if (priv->usb3_phy) {
		phy_power_off(priv->usb3_phy);
		phy_exit(priv->usb3_phy);
	}

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	dev_info(dev, "Renesas USB3 glue layer removed\n");
	return 0;
}

static int __maybe_unused rcar_gen5_usb_suspend(struct device *dev)
{
	struct usb_priv *priv = dev_get_drvdata(dev);

	/* Let DWC3 handle suspend first */
	/* Then handle Renesas-specific suspend */
	if (priv->usb3_phy)
		phy_power_off(priv->usb3_phy);

	dev_info(dev, "Renesas USB glue layer suspended\n");
	return 0;
}

static int __maybe_unused rcar_gen5_usb_resume(struct device *dev)
{
	struct usb_priv *priv = dev_get_drvdata(dev);
	int ret;

	/* Handle Renesas-specific resume */
	if (priv->usb3_phy) {
		ret = phy_power_on(priv->usb3_phy);
		if (ret) {
			dev_err(dev, "Failed to power on USB3 PHY: %d\n", ret);
			return ret;
		}
	}

	/* Re-configure registers after resume */
	if (priv->use_usb3_flow) {
		ret = rcar_gen5_usb_init_usb31_flow(priv);
	} else {
		ret = rcar_gen5_usb_init_usb20_flow(priv);
	}

	if (ret) {
		dev_err(dev, "Failed to reinitialize hardware: %d\n", ret);
		return ret;
	}

	dev_info(dev, "Renesas USB glue layer resumed\n");
	return 0;
}

static const struct dev_pm_ops rcar_gen5_usb_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rcar_gen5_usb_suspend, rcar_gen5_usb_resume)
};

static const struct of_device_id rcar_gen5_usb_of_match[] = {
	{ .compatible = "renesas,rcar-gen5-usb" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rcar_gen5_usb_of_match);

static struct platform_driver rcar_gen5_usb_driver = {
	.probe		= rcar_gen5_usb_probe,
	.remove		= rcar_gen5_usb_remove,
	.driver		= {
		.name	= "renesas-rcar-gen5-usb",
		.of_match_table = rcar_gen5_usb_of_match,
		.pm	= &rcar_gen5_usb_pm_ops,
	},
};

module_platform_driver(rcar_gen5_usb_driver);

MODULE_AUTHOR("Thanh Quan");
MODULE_DESCRIPTION("Renesas R-Car X5H USB Glue Layer Driver");
MODULE_LICENSE("GPL v2");