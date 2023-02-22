// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe host controller driver for Renesas R-Car Gen4 Series SoCs
 * Copyright (C) 2022-2023 Renesas Electronics Corporation
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include "pcie-rcar-gen4.h"
#include "pcie-designware.h"

static int rcar_gen4_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *dw = to_dw_pcie_from_pp(pp);
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);
	int ret;
	u32 val;

	ret = rcar_gen4_pcie_set_device_type(rcar, true, dw->num_lanes);
	if (ret < 0)
		return ret;

	dw_pcie_dbi_ro_wr_en(dw);

	rcar_gen4_pcie_disable_bar(dw, BAR0MASKF);
	rcar_gen4_pcie_disable_bar(dw, BAR1MASKF);

	dw_pcie_dbi_ro_wr_dis(dw);

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		/* Enable MSI interrupt signal */
		val = readl(rcar->base + PCIEINTSTS0EN);
		val |= MSI_CTRL_INT;
		writel(val, rcar->base + PCIEINTSTS0EN);
	}

	gpiod_set_value_cansleep(dw->pe_rst, 0);

	dw_pcie_setup_rc(pp);

	dw_pcie_dbi_ro_wr_en(dw);
	rcar_gen4_pcie_set_max_link_width(dw, dw->num_lanes);
	dw_pcie_dbi_ro_wr_dis(dw);

	if (!dw_pcie_link_up(dw)) {
		ret = dw->ops->start_link(dw);
		if (ret)
			return ret;
	}

	/* Ignore errors, the link may come up later */
	if (dw_pcie_wait_for_link(dw))
		dev_info(dw->dev, "PCIe link down\n");

	return 0;
}

static const struct dw_pcie_host_ops rcar_gen4_pcie_host_ops = {
	.host_init = rcar_gen4_pcie_host_init,
};

static int rcar_gen4_add_dw_pcie_rp(struct rcar_gen4_pcie *rcar,
				   struct platform_device *pdev)
{
	struct dw_pcie *dw = &rcar->dw;
	struct dw_pcie_rp *pp = &dw->pp;
	int ret;

	pp->ops = &rcar_gen4_pcie_host_ops;
	pp->no_msix = true;
	dw_pcie_cap_set(dw, REQ_RES);

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static void rcar_gen4_remove_dw_pcie_rp(struct rcar_gen4_pcie *rcar)
{
	dw_pcie_host_deinit(&rcar->dw.pp);
	gpiod_set_value_cansleep(rcar->dw.pe_rst, 1);
}

static int rcar_gen4_pcie_get_resources(struct rcar_gen4_pcie *rcar,
					struct platform_device *pdev)
{
	struct dw_pcie *dw = &rcar->dw;

	/* Renesas-specific registers */
	rcar->base = devm_platform_ioremap_resource_byname(pdev, "app");
	if (IS_ERR(rcar->base))
		return PTR_ERR(rcar->base);

	return rcar_gen4_pcie_devm_reset_get(rcar, dw->dev);
}

static int rcar_gen4_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_gen4_pcie *rcar;
	int err;

	rcar = rcar_gen4_pcie_devm_alloc(dev);
	if (!rcar)
		return -ENOMEM;

	err = rcar_gen4_pcie_get_resources(rcar, pdev);
	if (err < 0) {
		dev_err(dev, "Failed to request resource: %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, rcar);

	err = rcar_gen4_pcie_prepare(rcar);
	if (err < 0)
		return err;

	err = rcar_gen4_add_dw_pcie_rp(rcar, pdev);
	if (err < 0)
		goto err_add;

	return 0;

err_add:
	rcar_gen4_pcie_unprepare(rcar);

	return err;
}

static int rcar_gen4_pcie_remove(struct platform_device *pdev)
{
	struct rcar_gen4_pcie *rcar = platform_get_drvdata(pdev);

	rcar_gen4_remove_dw_pcie_rp(rcar);
	rcar_gen4_pcie_unprepare(rcar);

	return 0;
}

static const struct of_device_id rcar_gen4_pcie_of_match[] = {
	{ .compatible = "renesas,rcar-gen4-pcie", },
	{},
};

static struct platform_driver rcar_gen4_pcie_driver = {
	.driver = {
		.name = "pcie-rcar-gen4",
		.of_match_table = rcar_gen4_pcie_of_match,
	},
	.probe = rcar_gen4_pcie_probe,
	.remove = rcar_gen4_pcie_remove,
};
module_platform_driver(rcar_gen4_pcie_driver);

MODULE_DESCRIPTION("Renesas R-Car Gen4 PCIe host controller driver");
MODULE_LICENSE("GPL");
