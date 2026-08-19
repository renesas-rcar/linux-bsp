// SPDX-License-Identifier: GPL-2.0-only
/*
 * UCIe Endpoint driver for Renesas R-Car Gen5 Series SoCs
 * Copyright (C) 2026 Renesas Electronics Corporation
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/pci-epc.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>

#include "ucie-rcar.h"

static void rcar_ucie_ep_init(struct dw_pcie6_ep *ep)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	enum pci_barno bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie6_ep_reset_bar(pci, bar);
}

static int rcar_ucie_ep_raise_irq(struct dw_pcie6_ep *ep, u8 func_no,
				  unsigned int type,
				  u16 interrupt_num)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);

	switch (type) {
	case PCI_IRQ_INTX:
		return dw_pcie6_ep_raise_intx_irq(ep, func_no);
	case PCI_IRQ_MSI:
		return dw_pcie6_ep_raise_msi_irq(ep, func_no, interrupt_num);
	case PCI_IRQ_MSIX:
		return dw_pcie6_ep_raise_msix_irq(ep, func_no, interrupt_num);
	default:
		dev_err(pci->dev, "unknown IRQ type\n");
		return -EINVAL;
	}

	return 0;
}

static const struct pci_epc_features rcar_ucie_epc_get_features = {
	.linkup_notifier	= false,
	.msi_capable		= false,
	.msix_capable		= false,
};

static const struct pci_epc_features *rcar_ucie_ep_get_features(struct dw_pcie6_ep *ep)
{
	return &rcar_ucie_epc_get_features;
}

static const struct dw_pcie6_ep_ops rcar_ucie_ep_ops = {
	.init		= rcar_ucie_ep_init,
	.raise_irq	= rcar_ucie_ep_raise_irq,
	.get_features	= rcar_ucie_ep_get_features,
};

static int rcar_ucie_add_pcie_ep(struct rcar_ucie_pcie *dw_plat,
				 struct platform_device *pdev)
{
	struct dw_pcie6 *pci = dw_plat->pci;
	struct rcar_ucie *ucie = dev_get_drvdata(pci->dev);
	struct device *dev = &pdev->dev;
	struct dw_pcie6_ep *ep;
	u32 val;
	int ret;

	pci->dbi_base = ucie->axi_base;

	ep = &pci->ep;
	ep->ops = &rcar_ucie_ep_ops;

	ret = dw_pcie6_ep_init(ep);
	if (ret) {
		dev_err(dev, "Failed to initialize endpoint\n");
		return ret;
	}

	dw_pcie6_dbi_ro_wr_en(pci);
	val = dw_pcie6_readl_dbi(pci, PCICONF3);
	val &= ~EP_MULTI_FUNC;
	dw_pcie6_writel_dbi(pci, PCICONF3, val);

	return 0;
}

static int rcar_ucie_ep_runtime_resume(struct device *dev)
{
	struct rcar_ucie *ucie = dev_get_drvdata(dev);

	return rcar_ucie_clk_init(ucie);
}

static int rcar_ucie_ep_runtime_suspend(struct device *dev)
{
	struct rcar_ucie *ucie = dev_get_drvdata(dev);

	rcar_ucie_clk_deinit(ucie);

	return 0;
}

static const struct dev_pm_ops rcar_ucie_ep_pm_ops = {
	SET_RUNTIME_PM_OPS(rcar_ucie_ep_runtime_suspend,
			   rcar_ucie_ep_runtime_resume, NULL)
};

static int rcar_ucie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_ucie_pcie *dw_plat;
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

	ucie->dev = dev;
	dw_plat->pci = pci;
	dw_plat->mode = DW_PCIE_EP_TYPE;
	ucie->dw_plat = dw_plat;

	platform_set_drvdata(pdev, ucie);

	ret = rcar_ucie_get_resources(ucie, pdev);
	if (ret)
		return ret;

	ret = rcar_ucie_power_up(ucie);
	if (ret)
		return ret;

	rcar_ucie_hw_init(ucie, false);
	rcar_ucie_start_link_up(ucie, false);

	ret = rcar_ucie_add_pcie_ep(dw_plat, pdev);
	if (ret)
		goto err_power_down;

	return 0;

err_power_down:
	rcar_ucie_power_down(ucie);

	return ret;
}

static void rcar_ucie_ep_remove(struct platform_device *pdev)
{
	struct rcar_ucie *ucie = dev_get_drvdata(&pdev->dev);

	rcar_ucie_power_down(ucie);
}

static const struct of_device_id rcar_ucie_ep_of_match[] = {
	{ .compatible = "renesas,r8a78000-ucie-ep", },
	{},
};

static struct platform_driver rcar_ucie_ep_driver = {
	.driver = {
		.name = "ucie-ep-rcar",
		.of_match_table = rcar_ucie_ep_of_match,
		.pm = &rcar_ucie_ep_pm_ops,
	},
	.probe = rcar_ucie_ep_probe,
	.remove = rcar_ucie_ep_remove,
};
module_platform_driver(rcar_ucie_ep_driver);

MODULE_DESCRIPTION("Renesas R-Car UCIe Endpoint driver");
MODULE_AUTHOR("Phong Hoang");
MODULE_LICENSE("GPL");
