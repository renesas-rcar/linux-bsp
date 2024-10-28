// SPDX-License-Identifier: GPL-2.0-only
/*
 * UCIe host controller driver for Renesas R-Car Gen5 Series SoCs
 * Copyright (C) 2024 Renesas Electronics Corporation
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>

#include "ucie-rcar.h"

static int rcar_ucie_get_resources(struct rcar_ucie *ucie, struct platform_device *pdev)
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

static void rcar_ucie_hw_enable(struct rcar_ucie *ucie)
{
	/* Configure as Root Port */
	/* FIXME: Confirm the used of this register */
	rcar_ucie_mem_write32(ucie, IMP_CORECONFIG_CONFIG0, UCIECTL_DEF_RP_EN);

	rcar_ucie_controller_enable(ucie);
	rcar_ucie_phy_enable(ucie);

	/* Ignore errors, the link may come up later */
	rcar_ucie_wait_for_link(ucie->dw_plat->pci);
}

static int dw_plat_pcie6_host_init(struct pcie_port *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct rcar_ucie *ucie = dev_get_drvdata(pci->dev);

	dw_pcie6_setup_rc(pp);
	rcar_ucie_hw_enable(ucie);
	dw_pcie6_msi_init(pp);

	return 0;
}

static void rcar_ucie_set_num_vectors(struct pcie_port *pp)
{
	pp->num_vectors = MAX_MSI_IRQS;
}

static const struct dw_pcie6_host_ops rcar_ucie_pcie_host_ops = {
	.host_init = dw_plat_pcie6_host_init,
	.set_num_vectors = rcar_ucie_set_num_vectors,
};

static int rcar_ucie_add_pcie_host(struct dw_plat_pcie6 *dw_plat_pcie6,
				   struct platform_device *pdev)
{
	struct dw_pcie6 *pci = dw_plat_pcie6->pci;
	struct pcie_port *pp = &pci->pp;
	int ret;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = platform_get_irq_byname(pdev, "others");
		if (pp->msi_irq < 0)
			return pp->msi_irq;
	}

	pp->irq = platform_get_irq_byname(pdev, "dma");
	if (pp->irq < 0)
		return pp->irq;

	pp->ops = &rcar_ucie_pcie_host_ops;

	ret = dw_pcie6_host_init(pp);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int rcar_ucie_probe(struct platform_device *pdev)
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
	dw_plat->mode = DW_PCIE_RC_TYPE;
	ucie->dw_plat = dw_plat;

	platform_set_drvdata(pdev, ucie);

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret)
		goto err_pm_disable;

	ret = rcar_ucie_get_resources(ucie, pdev);
	if (ret)
		goto err_pm_put;

	ret = rcar_ucie_add_pcie_host(dw_plat, pdev);
	if (ret)
		goto err_pm_put;

	return 0;

err_pm_put:
	pm_runtime_put(dev);

err_pm_disable:
	pm_runtime_disable(dev);

	return ret;
}

static int rcar_ucie_remove(struct platform_device *pdev)
{
	struct rcar_ucie *ucie = dev_get_drvdata(&pdev->dev);
	struct dw_pcie6 *pci = ucie->dw_plat->pci;
	struct pcie_port *pp = &pci->pp;

	dw_pcie6_host_deinit(pp);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id rcar_ucie_of_match[] = {
	{ .compatible = "renesas,r8a78000-ucie", },
	{},
};

static struct platform_driver rcar_ucie_driver = {
	.driver = {
		.name = "ucie-rcar",
		.of_match_table = rcar_ucie_of_match,
	},
	.probe = rcar_ucie_probe,
	.remove = rcar_ucie_remove,
};
module_platform_driver(rcar_ucie_driver);

MODULE_DESCRIPTION("Renesas R-Car UCIe host controller driver");
MODULE_LICENSE("GPL v2");
