// SPDX-License-Identifier: GPL-2.0-only
/*
 * UCIe Endpoint driver for Renesas R-Car Gen5 Series SoCs
 * Copyright (C) 2024 Renesas Electronics Corporation
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/pci-epc.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>

#include "ucie-rcar.h"

static int rcar_ucie_ep_get_resources(struct rcar_ucie *ucie, struct platform_device *pdev)
{
	struct dw_pcie6 *pci = ucie->dw_plat->pci;

	ucie->base = devm_platform_ioremap_resource_byname(pdev, "apb");
	if (IS_ERR(ucie->base))
		return PTR_ERR(ucie->base);

	pci->dbi_base = devm_platform_ioremap_resource_byname(pdev, "dbi");
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	ucie->vdk_bypass = of_property_read_bool(pdev->dev.of_node, "vdk-bypass-mode");

	return 0;
}

static void rcar_ucie_ep_hw_enable(struct rcar_ucie *ucie)
{
	/* Configure as Endpoint */
	/* FIXME: Confirm the used of this register */
	rcar_ucie_mem_write32(ucie, IMP_CORECONFIG_CONFIG0, UCIECTL_DEF_EP_EN);

	rcar_ucie_controller_enable(ucie);
	rcar_ucie_phy_enable(ucie);
}

static void rcar_ucie_ep_init(struct dw_pcie6_ep *ep)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	enum pci_barno bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie6_ep_reset_bar(pci, bar);
}

static int rcar_ucie_ep_raise_irq(struct dw_pcie6_ep *ep, u8 func_no,
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
		dev_err(pci->dev, "unknown IRQ type\n");
		return -EINVAL;
	}

	return 0;
}

static const struct pci_epc_features rcar_ucie_epc_get_features = {
	.linkup_notifier	= false,
	.msi_capable		= true,
	.msix_capable		= false,
};

static const struct pci_epc_features *rcar_ucie_ep_get_features(struct dw_pcie6_ep *ep)
{
	return &rcar_ucie_epc_get_features;
}

static const struct dw_pcie6_ep_ops rcar_ucie_ep_ops = {
	.ep_init	= rcar_ucie_ep_init,
	.raise_irq	= rcar_ucie_ep_raise_irq,
	.get_features	= rcar_ucie_ep_get_features,
};

static int rcar_ucie_add_pcie_ep(struct dw_plat_pcie6 *dw_plat_pcie6,
				 struct platform_device *pdev)
{
	struct dw_pcie6 *pci = dw_plat_pcie6->pci;
	struct device *dev = &pdev->dev;
	struct dw_pcie6_ep *ep;
	struct resource *res;
	int ret;

	ep = &pci->ep;
	ep->ops = &rcar_ucie_ep_ops;

	pci->dbi_base2 = devm_platform_ioremap_resource_byname(pdev, "dbi2");
	if (IS_ERR(pci->dbi_base2))
		return PTR_ERR(pci->dbi_base2);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);

	ret = dw_pcie6_ep_init(ep);
	if (ret) {
		dev_err(dev, "Failed to initialize endpoint\n");
		return ret;
	}

	return 0;
}

static int rcar_ucie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_plat_pcie6 *dw_plat;
	struct rcar_ucie *ucie;
	struct dw_pcie6 *pci;
	int ret;

	ucie = devm_kzalloc(dev, sizeof(*ucie), GFP_KERNEL);
	if (!ucie)
		return -ENOMEM;

	dw_plat = devm_kzalloc(dev, sizeof(*dw_plat), GFP_KERNEL);
	if (!dw_plat)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &rcar_ucie_ops;

	dw_plat->pci = pci;
	dw_plat->mode = DW_PCIE_EP_TYPE;
	ucie->dw_plat = dw_plat;

	platform_set_drvdata(pdev, ucie);

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret)
		goto err_pm_disable;

	ret = rcar_ucie_ep_get_resources(ucie, pdev);
	if (ret)
		goto err_pm_put;

	rcar_ucie_ep_hw_enable(ucie);

	ret = rcar_ucie_add_pcie_ep(dw_plat, pdev);
	if (ret)
		goto err_pm_put;

	return 0;

err_pm_put:
	pm_runtime_put(dev);

err_pm_disable:
	pm_runtime_disable(dev);

	return ret;
}

static int rcar_ucie_ep_remove(struct platform_device *pdev)
{
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id rcar_ucie_ep_of_match[] = {
	{ .compatible = "renesas,r8a78000-ucie-ep", },
	{},
};

static struct platform_driver rcar_ucie_ep_driver = {
	.driver = {
		.name = "ucie-ep-rcar",
		.of_match_table = rcar_ucie_ep_of_match,
	},
	.probe = rcar_ucie_ep_probe,
	.remove = rcar_ucie_ep_remove,
};
module_platform_driver(rcar_ucie_ep_driver);

MODULE_DESCRIPTION("Renesas R-Car UCIe Endpoint driver");
MODULE_LICENSE("GPL v2");
