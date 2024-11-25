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

static void pcie6_rcar_init_ep(struct dw_pcie6_ep *ep)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	enum pci_barno bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie6_ep_reset_bar(pci, bar);
}

static int pcie6_rcar_ep_raise_irq(struct dw_pcie6_ep *ep, u8 func_no,
				     enum pci_epc_irq_type type,
				     u16 interrupt_num)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return dw_pcie6_ep_raise_legacy_irq(ep, func_no);
	case PCI_EPC_IRQ_MSI:
		return dw_pcie6_ep_raise_msi_irq(ep, func_no, interrupt_num);
	case PCI_EPC_IRQ_MSIX:
		return dw_pcie6_ep_raise_msix_irq(ep, func_no, interrupt_num);
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
	}

	return 0;
}

static const struct pci_epc_features pcie6_rcar_epc_get_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
	.reserved_bar = 1 << BAR_5,
};

static const struct pci_epc_features*
pcie6_rcar_get_features(struct dw_pcie6_ep *ep)
{
	return &pcie6_rcar_epc_get_features;
}

static const struct dw_pcie6_ep_ops pcie6_rcar_ep_ops = {
	.ep_init = pcie6_rcar_init_ep,
	.raise_irq = pcie6_rcar_ep_raise_irq,
	.get_features = pcie6_rcar_get_features,
};

static int pcie6_rcar_ep_probe(struct platform_device *pdev)
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

	/* Get the PCIe Generation from DT */
	pci->link_gen = pcie6_rcar_get_link_speed(pci->dev->of_node);

	pci->ep.ops = &pcie6_rcar_ep_ops;

	ret = dw_pcie6_ep_init(&pci->ep);
	if (ret)
		dev_err(dev, "failed to initialize endpoint\n");

	return ret;
}

static const struct of_device_id pcie6_rcar_ep_of_match[] = {
	{
		.compatible = "renesas,rcar-gen5-pcie6-ep",
	},
	{},
};

static struct platform_driver pcie6_rcar_ep_driver = {
	.driver = {
		.name	= "pcie6-rcar-ep",
		.of_match_table = pcie6_rcar_ep_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = pcie6_rcar_ep_probe,
};
static int __init pcie6_rcar_ep_init(void)
{
	return platform_driver_register(&pcie6_rcar_ep_driver);
}

static void __exit pcie6_rcar_ep_exit(void)
{
	platform_driver_unregister(&pcie6_rcar_ep_driver);
}

module_init(pcie6_rcar_ep_init);
module_exit(pcie6_rcar_ep_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCIe 6.0 R-Car Gen5 Endpoint Driver");
