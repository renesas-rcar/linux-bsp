// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe driver for Synopsys DesignWare Core
 *
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors: Joao Pinto <Joao.Pinto@synopsys.com>
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/types.h>

#include "pcie6-designware.h"


static const struct dw_pcie6_host_ops pcie6_rcar_host_ops = {
};

static int rcar_add_pcie6_port(struct dw_plat_pcie6 *dw_plat_pcie6,
				 struct platform_device *pdev)
{
	struct dw_pcie6 *pci = dw_plat_pcie6->pci;
	struct dw_pcie6_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	pp->irq = platform_get_irq(pdev, 1);
	if (pp->irq < 0)
		return pp->irq;

	pp->num_vectors = MAX_MSI_IRQS;
	pp->ops = &pcie6_rcar_host_ops;

	ret = dw_pcie6_host_init(pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int pcie6_rcar_host_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_plat_pcie6 *dw_plat_pcie6;
	struct dw_pcie6 *pci;
	int ret;

	dw_plat_pcie6 = devm_kzalloc(dev, sizeof(*dw_plat_pcie6), GFP_KERNEL);
	if (!dw_plat_pcie6)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;

	dw_plat_pcie6->pci = pci;

	platform_set_drvdata(pdev, dw_plat_pcie6);

	ret = rcar_add_pcie6_port(dw_plat_pcie6, pdev);
	if (ret)
		dev_err(dev, "failed to initialize host\n");

	return ret;
}

static const struct of_device_id pcie6_rcar_host_of_match[] = {
	{
		.compatible = "renesas,rcar-gen5-pcie6",
	},
	{},
};

static struct platform_driver pcie6_rcar_host_driver = {
	.driver = {
		.name	= "pcie6-rcar",
		.of_match_table = pcie6_rcar_host_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = pcie6_rcar_host_probe,
};
static int __init pcie6_rcar_init(void)
{
	return platform_driver_register(&pcie6_rcar_host_driver);
}

static void __exit pcie6_rcar_exit(void)
{
	platform_driver_unregister(&pcie6_rcar_host_driver);
}

module_init(pcie6_rcar_init);
module_exit(pcie6_rcar_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCIe 6.0 R-Car Gen5 Host Driver");
