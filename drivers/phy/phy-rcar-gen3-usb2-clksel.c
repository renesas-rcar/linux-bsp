/*
 * Renesas R-Car Gen3 for USB2.0 clock selector PHY driver
 *
 * Copyright (C) 2017 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define USB_CLOCK_TYPE			0x00
#define USB_CLOCK_TYPE_EXTAL_ONLY	0x0801

struct rcar_gen3_usb2_clksel {
	void __iomem *base;
	struct phy *phy;
	bool usb_extal_only;
};

static int rcar_gen3_usb2_clksel_init(struct phy *p)
{
	struct rcar_gen3_usb2_clksel *r = phy_get_drvdata(p);
	u16 val = readw(r->base + USB_CLOCK_TYPE);

	dev_vdbg(&r->phy->dev, "%s: %d, %04x\n", __func__, r->usb_extal_only,
		 val);

	if (r->usb_extal_only && val != USB_CLOCK_TYPE_EXTAL_ONLY)
		writew(USB_CLOCK_TYPE_EXTAL_ONLY, r->base + USB_CLOCK_TYPE);

	return 0;
}

static const struct phy_ops rcar_gen3_usb2_clksel_ops = {
	.init		= rcar_gen3_usb2_clksel_init,
	.owner		= THIS_MODULE,
};

static const struct of_device_id rcar_gen3_usb2_clksel_match_table[] = {
	{ .compatible = "renesas,usb2-clksel-phy-r8a7795" },
	{ .compatible = "renesas,usb2-clksel-phy-r8a7796" },
	{ .compatible = "renesas,rcar-gen3-usb2-clksel-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, rcar_gen3_usb2_clksel_match_table);

static int rcar_gen3_usb2_clksel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_gen3_usb2_clksel *r;
	struct phy_provider *provider;
	struct resource *res;
	int ret = 0;

	if (!dev->of_node) {
		dev_err(dev, "This driver needs device tree\n");
		return -EINVAL;
	}

	r = devm_kzalloc(dev, sizeof(*r), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	r->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(r->base))
		return PTR_ERR(r->base);

	/*
	 * devm_phy_create() will call pm_runtime_enable(&phy->dev);
	 * And then, phy-core will manage runtime pm for this device.
	 */
	pm_runtime_enable(dev);

	r->phy = devm_phy_create(dev, NULL, &rcar_gen3_usb2_clksel_ops);
	if (IS_ERR(r->phy)) {
		dev_err(dev, "Failed to create USB 2.0 clock selector PHY\n");
		ret = PTR_ERR(r->phy);
		goto error;
	}

	r->usb_extal_only = of_property_read_bool(dev->of_node,
						"renesas,usb-extal-only");

	platform_set_drvdata(pdev, r);
	phy_set_drvdata(r->phy, r);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "Failed to register PHY provider\n");
		ret = PTR_ERR(provider);
		goto error;
	}

	return 0;

error:
	pm_runtime_disable(dev);

	return ret;
}

static int rcar_gen3_usb2_clksel_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
};

static struct platform_driver rcar_gen3_usb2_clksel_driver = {
	.driver = {
		.name		= "phy_rcar_gen3_usb2_clksel",
		.of_match_table	= rcar_gen3_usb2_clksel_match_table,
	},
	.probe	= rcar_gen3_usb2_clksel_probe,
	.remove = rcar_gen3_usb2_clksel_remove,
};
module_platform_driver(rcar_gen3_usb2_clksel_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas R-Car Gen3 USB 2.0 clock selector PHY");
MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
