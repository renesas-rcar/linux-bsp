// SPDX-License-Identifier: GPL-2.0-only
/*
 * UCIe LDR address mapping driver for Renesas R-Car
 *
 * Platform driver that ioremaps the LDR (Log Data Recorder) physical
 * address for each UCIe channel. The LDR regions are described via the
 * "ldr-regions" phandle array in the DT, pointing to reserved-memory nodes
 * — the same convention used by ucie-dummy-rcar-host for "shared-region".
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define DRV_MODULE_NAME		"ucie-ldr-map"
#define UCIE_LDR_MAX_CH		2

struct ucie_ldr_ch {
	void __iomem		*base;
	phys_addr_t		phys;
	resource_size_t		size;
};

struct ucie_ldr_priv {
	struct device		*dev;
	struct ucie_ldr_ch	ch[UCIE_LDR_MAX_CH];
	int			num_ch;
};

static ssize_t ldr_info_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct ucie_ldr_priv *priv = dev_get_drvdata(dev);
	int i, n = 0;

	for (i = 0; i < priv->num_ch; i++)
		n += sysfs_emit_at(buf, n,
			"ch%d: phys=0x%llx size=0x%llx mapped=%s\n",
			i,
			(unsigned long long)priv->ch[i].phys,
			(unsigned long long)priv->ch[i].size,
			priv->ch[i].base ? "yes" : "no");
	return n;
}
static DEVICE_ATTR_RO(ldr_info);

static struct attribute *ucie_ldr_attrs[] = {
	&dev_attr_ldr_info.attr,
	NULL,
};

static const struct attribute_group ucie_ldr_attr_group = {
	.attrs = ucie_ldr_attrs,
};

static int ucie_ldr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ucie_ldr_priv *priv;
	int i, err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	for (i = 0; i < UCIE_LDR_MAX_CH; i++) {
		struct device_node *ldr_np;
		u32 reg[4];

		ldr_np = of_parse_phandle(dev->of_node, "ldr-regions", i);
		if (!ldr_np)
			break;

		if (of_property_read_u32_array(ldr_np, "reg", reg,
					       ARRAY_SIZE(reg))) {
			dev_err(dev, "ch%d: failed to read ldr-regions[%d] reg\n",
				i, i);
			of_node_put(ldr_np);
			return -EINVAL;
		}
		of_node_put(ldr_np);

		priv->ch[i].phys = ((phys_addr_t)reg[0] << 32) | reg[1];
		priv->ch[i].size = reg[3];

		/* devm_ioremap is unwound automatically on probe failure */
		priv->ch[i].base = devm_ioremap(dev, priv->ch[i].phys,
						priv->ch[i].size);
		if (!priv->ch[i].base) {
			dev_err(dev, "ch%d: failed to ioremap 0x%llx\n",
				i, (unsigned long long)priv->ch[i].phys);
			return -ENOMEM;
		}

		dev_info(dev, "ch%d: LDR mapped phys=0x%llx size=0x%llx\n",
			 i, (unsigned long long)priv->ch[i].phys,
			 (unsigned long long)priv->ch[i].size);
		priv->num_ch = i + 1;
	}

	if (!priv->num_ch) {
		dev_err(dev, "no ldr-regions defined in DT\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, priv);

	err = sysfs_create_group(&dev->kobj, &ucie_ldr_attr_group);
	if (err)
		dev_warn(dev, "failed to create sysfs group: %d\n", err);

	return 0;
}

static void ucie_ldr_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &ucie_ldr_attr_group);
}

static const struct of_device_id ucie_ldr_of_match[] = {
	{ .compatible = "renesas,ucie-ldr-map" },
	{},
};
MODULE_DEVICE_TABLE(of, ucie_ldr_of_match);

static struct platform_driver ucie_ldr_driver = {
	.driver = {
		.name		= DRV_MODULE_NAME,
		.of_match_table	= ucie_ldr_of_match,
	},
	.probe	= ucie_ldr_probe,
	.remove	= ucie_ldr_remove,
};

static int __init ucie_ldr_init(void)
{
	pr_info(DRV_MODULE_NAME ": UCIe LDR mapping driver loading\n");
	return platform_driver_register(&ucie_ldr_driver);
}

static void __exit ucie_ldr_exit(void)
{
	platform_driver_unregister(&ucie_ldr_driver);
	pr_info(DRV_MODULE_NAME ": UCIe LDR mapping driver unloaded\n");
}

module_init(ucie_ldr_init);
module_exit(ucie_ldr_exit);

MODULE_DESCRIPTION("UCIe LDR address mapping driver for Renesas R-Car");
MODULE_AUTHOR("Hau Vo");
MODULE_LICENSE("GPL v2");
