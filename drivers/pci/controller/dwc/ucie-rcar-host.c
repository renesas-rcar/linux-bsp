// SPDX-License-Identifier: GPL-2.0-only
/*
 * UCIe host controller driver for Renesas R-Car Gen5 Series SoCs
 * Copyright (C) 2026 Renesas Electronics Corporation
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>

#include "ucie-rcar.h"

static int rcar_ucie_host_init(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct rcar_ucie *ucie = dev_get_drvdata(pci->dev);

	dw_pcie6_setup_rc(pp);
	rcar_ucie_hw_init(ucie, true);
	rcar_ucie_start_link_up(ucie, true);

	return 0;
}

static const struct dw_pcie6_host_ops rcar_ucie_pcie_host_ops = {
	.init = rcar_ucie_host_init,
};

static int rcar_ucie_add_pcie_host(struct rcar_ucie_pcie *dw_plat,
				   struct platform_device *pdev)
{
	struct dw_pcie6 *pci = dw_plat->pci;
	struct rcar_ucie *ucie = dev_get_drvdata(pci->dev);
	struct dw_pcie6_rp *pp = &pci->pp;
	int ret;

	pci->dbi_base = ucie->axi_base;

	pp->irq = platform_get_irq_byname(pdev, "dma");
	if (pp->irq < 0)
		return pp->irq;

	pp->num_vectors = MAX_MSI_IRQS;
	pp->ops = &rcar_ucie_pcie_host_ops;

	ret = dw_pcie6_host_init(pp);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int rcar_ucie_runtime_resume(struct device *dev)
{
	struct rcar_ucie *ucie = dev_get_drvdata(dev);

	return rcar_ucie_clk_init(ucie);
}

static int rcar_ucie_runtime_suspend(struct device *dev)
{
	struct rcar_ucie *ucie = dev_get_drvdata(dev);

	rcar_ucie_clk_deinit(ucie);

	return 0;
}

static const struct dev_pm_ops rcar_ucie_pm_ops = {
	SET_RUNTIME_PM_OPS(rcar_ucie_runtime_suspend,
			   rcar_ucie_runtime_resume, NULL)
};

static int rcar_ucie_probe(struct platform_device *pdev)
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
	dw_plat->mode = DW_PCIE_RC_TYPE;
	ucie->dw_plat = dw_plat;

	platform_set_drvdata(pdev, ucie);

	ret = rcar_ucie_get_resources(ucie, pdev);
	if (ret)
		return ret;

	ret = rcar_ucie_power_up(ucie);
	if (ret)
		return ret;

	ret = rcar_ucie_add_pcie_host(dw_plat, pdev);
	if (ret)
		goto err_power_down;

	return 0;

err_power_down:
	rcar_ucie_power_down(ucie);

	return ret;
}

static void rcar_ucie_remove(struct platform_device *pdev)
{
	struct rcar_ucie *ucie = dev_get_drvdata(&pdev->dev);
	struct dw_pcie6 *pci = ucie->dw_plat->pci;
	struct dw_pcie6_rp *pp = &pci->pp;

	dw_pcie6_host_deinit(pp);

	rcar_ucie_power_down(ucie);
}

static const struct of_device_id rcar_ucie_of_match[] = {
	{ .compatible = "renesas,r8a78000-ucie", },
	{},
};

static struct platform_driver rcar_ucie_driver = {
	.driver = {
		.name = "ucie-rcar",
		.of_match_table = rcar_ucie_of_match,
		.pm = &rcar_ucie_pm_ops,
	},
	.probe = rcar_ucie_probe,
	.remove = rcar_ucie_remove,
};
module_platform_driver(rcar_ucie_driver);

MODULE_DESCRIPTION("Renesas R-Car UCIe host controller driver");
MODULE_AUTHOR("Phong Hoang");
MODULE_LICENSE("GPL");
