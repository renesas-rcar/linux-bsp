// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe Endpoint driver for Renesas R-Car Gen4 Series SoCs
 * Copyright (C) 2022-2023 Renesas Electronics Corporation
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include "pcie-rcar-gen4.h"
#include "pcie-designware.h"

/* Configuration */
#define PCICONF3		0x000c
#define  MULTI_FUNC		BIT(23)

static void rcar_gen4_pcie_ep_pre_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *dw = to_dw_pcie_from_ep(ep);
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);
	int val;

	rcar_gen4_pcie_set_device_type(rcar, false, dw->num_lanes);

	dw_pcie_dbi_ro_wr_en(dw);

	/* Single function */
	val = dw_pcie_readl_dbi(dw, PCICONF3);
	val &= ~MULTI_FUNC;
	dw_pcie_writel_dbi(dw, PCICONF3, val);

	rcar_gen4_pcie_disable_bar(dw, BAR5MASKF);

	rcar_gen4_pcie_set_max_link_width(dw, dw->num_lanes);

	dw_pcie_dbi_ro_wr_dis(dw);
}

static int rcar_gen4_pcie_ep_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				       enum pci_epc_irq_type type,
				       u16 interrupt_num)
{
	struct dw_pcie *dw = to_dw_pcie_from_ep(ep);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return dw_pcie_ep_raise_legacy_irq(ep, func_no);
	case PCI_EPC_IRQ_MSI:
		return dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
	default:
		dev_err(dw->dev, "UNKNOWN IRQ type\n");
		return -EINVAL;
	}

	return 0;
}

static const struct pci_epc_features rcar_gen4_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
	.reserved_bar = 1 << BAR_5,
	.align = SZ_1M,
};

static const struct pci_epc_features*
rcar_gen4_pcie_ep_get_features(struct dw_pcie_ep *ep)
{
	return &rcar_gen4_pcie_epc_features;
}

static const struct dw_pcie_ep_ops pcie_ep_ops = {
	.ep_pre_init = rcar_gen4_pcie_ep_pre_init,
	.raise_irq = rcar_gen4_pcie_ep_raise_irq,
	.get_features = rcar_gen4_pcie_ep_get_features,
};

static int rcar_gen4_add_pcie_ep(struct rcar_gen4_pcie *rcar,
				 struct platform_device *pdev)
{
	struct dw_pcie *dw = &rcar->dw;
	struct dw_pcie_ep *ep;
	int ret;

	ep = &dw->ep;
	ep->ops = &pcie_ep_ops;
	ep->intx_by_atu = true;

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize endpoint\n");
		return ret;
	}

	writel(PCIEDMAINTSTSEN_INIT, rcar->base + PCIEDMAINTSTSEN);

	dw->ops->start_link(dw);

	return 0;
}

static void rcar_gen4_remove_pcie_ep(struct rcar_gen4_pcie *rcar)
{
	writel(0, rcar->base + PCIEDMAINTSTSEN);
	dw_pcie_ep_exit(&rcar->dw.ep);
}

static int rcar_gen4_pcie_ep_get_resources(struct rcar_gen4_pcie *rcar,
					   struct platform_device *pdev)
{
	struct dw_pcie *dw = &rcar->dw;
	struct device *dev = dw->dev;

	/* Renesas-specific registers */
	rcar->base = devm_platform_ioremap_resource_byname(pdev, "appl");
	if (IS_ERR(rcar->base))
		return PTR_ERR(rcar->base);

	return rcar_gen4_pcie_devm_reset_get(rcar, dev);
}

static int rcar_gen4_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_gen4_pcie *rcar;
	int err;

	rcar = rcar_gen4_pcie_devm_alloc(dev);
	if (!rcar)
		return -ENOMEM;

	err = rcar_gen4_pcie_ep_get_resources(rcar, pdev);
	if (err < 0) {
		dev_err(dev, "failed to request resource: %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, rcar);

	err = rcar_gen4_pcie_prepare(rcar);
	if (err < 0)
		return err;

	err = rcar_gen4_add_pcie_ep(rcar, pdev);
	if (err < 0)
		goto err_add;

	return 0;

err_add:
	rcar_gen4_pcie_unprepare(rcar);

	return err;
}

static int rcar_gen4_pcie_ep_remove(struct platform_device *pdev)
{
	struct rcar_gen4_pcie *rcar = platform_get_drvdata(pdev);

	rcar_gen4_remove_pcie_ep(rcar);
	rcar_gen4_pcie_unprepare(rcar);

	return 0;
}

static const struct of_device_id rcar_gen4_pcie_of_match[] = {
	{ .compatible = "renesas,rcar-gen4-pcie-ep", },
	{},
};

static struct platform_driver rcar_gen4_pcie_ep_driver = {
	.driver = {
		.name = "pcie-rcar-gen4-ep",
		.of_match_table = rcar_gen4_pcie_of_match,
	},
	.probe = rcar_gen4_pcie_ep_probe,
	.remove = rcar_gen4_pcie_ep_remove,
};
module_platform_driver(rcar_gen4_pcie_ep_driver);

MODULE_DESCRIPTION("Renesas R-Car Gen4 PCIe endpoint controller driver");
MODULE_LICENSE("GPL");
