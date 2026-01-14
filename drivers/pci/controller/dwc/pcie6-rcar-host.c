// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe 6.0 Host driver for Renesas R-Car Gen5 Series SoCs
 *
 * Authors: Tin Tran <tin.tran.xk@renesas.com>
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
#include <linux/pm_runtime.h>

#include "pcie6-rcar-gen5.h"
#include "pcie6-designware.h"

static int rcar_gen5_pcie6_host_init(struct dw_pcie6_rp *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);
	struct rcar_pcie6 *rcar_pcie6 = to_rcar_gen5_pcie6(pci);
	u32 val;

	if (reset_control_assert(rcar_pcie6->perst))
		dev_err(pci->dev, "Failed to assert PERST#");

	val = readl(rcar_pcie6->base + PCIEMSR0);
	val |= BIT(6);
	writel(val, rcar_pcie6->base + PCIEMSR0);

	rcar_gen5_pcie6_module_reset(pci);
	rcar_gen5_pcie6_module_run(pci);

	/* Set device type - RootComplex */
	rcar_gen5_pcie6_set_device_type(rcar_pcie6, true);

	dw_pcie6_dbi_ro_wr_en(pci);

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		/* Enable MSI interrupt signal */
		val = readl(rcar_pcie6->base + 0x2C0);
		val |= MSI_CTRL_INT;
		writel(val, rcar_pcie6->base + 0x2C0);

		val = dw_pcie6_readl_dbi(pci, MSICAP0F0);
		val |= BIT(16);
		dw_pcie6_writel_dbi(pci, MSICAP0F0, val);
	}

	rcar_gen5_pcie6_set_max_link_width(rcar_pcie6, pci->num_lanes);

	/* Sharing REFCLK setting */
	rcar_gen5_pcie6_refclk_phy1(rcar_pcie6, pci->num_lanes);

	/* Power Manegement Setting */
	val = readl(rcar_pcie6->base + PCIEPWRMNGCTRL);
	val |= GENMASK(11, 10);
	writel(val, rcar_pcie6->base + PCIEPWRMNGCTRL);

	/* Error Status Enable */
	val = readl(rcar_pcie6->base + PCIEERRSTS0EN);
	val |= BIT(9) | BIT(5) | BIT(4);
	writel(val, rcar_pcie6->base + PCIEERRSTS0EN);

	/* Clear hold phy reset */
	val = readl(rcar_pcie6->base + PCIERSTCTRL1);
	val &= ~BIT(16);
	writel(val, rcar_pcie6->base + PCIERSTCTRL1);

	rcar_gen5_pcie6_bootload(rcar_pcie6, pci->num_lanes, rcar_pcie6->ch);

	/* Separate REFCLK */
	val = readl(rcar_pcie6->base + PCIEMSR0);
	val |= BIT(6);
	writel(val, rcar_pcie6->base + PCIEMSR0);

	val = dw_pcie6_readl_dbi(pci, PRTLGC2);
	val |= BIT(23);
	dw_pcie6_writel_dbi(pci, PRTLGC2, val);

	/* ECRC */
	val = dw_pcie6_readl_dbi(pci, PCIE_ADV_ERR_CTRL);
	val |= BIT(8) | BIT(6);
	dw_pcie6_writel_dbi(pci, PCIE_ADV_ERR_CTRL, val);

	/* IDE Logic disable  */
	val = dw_pcie6_readl_dbi(pci, PCIE_PF0_IDE_CTRL);
	val |= BIT(0);
	dw_pcie6_writel_dbi(pci, PCIE_PF0_IDE_CTRL, val);

	/* Disable CXL Mode by default */
	val = dw_pcie6_readl_dbi(pci, PCIE6_PL32G_CAP);
	val &= ~GENMASK(10, 8);
	dw_pcie6_writel_dbi(pci, PCIE6_PL32G_CAP, val);

	/* Disable flit mode by default */
	val = dw_pcie6_readw_dbi(pci, EXPCAP(PCI_EXP_LNKCTL));
	val |= BIT(13);
	dw_pcie6_writew_dbi(pci, EXPCAP(PCI_EXP_LNKCTL), val);

	/* Monitor PMD */
	if (!rcar_gen5_pcie6_monitor_pmd(rcar_pcie6))
		dev_info(pci->dev, "All PMD check passed\n");

	rcar_gen5_pcie6_txpreset_coef_mapping(pci);

	/* lane0 Rx 10kohm change to 60ohm */
	val = readl(rcar_pcie6->phy_base + 0x8);
	val |= BIT(9);
	writel(val, rcar_pcie6->phy_base + 0x8);

	msleep(100);
	if (reset_control_deassert(rcar_pcie6->perst))
		dev_err(pci->dev, "Failed to deassert PERST#");

	dw_pcie6_dbi_ro_wr_dis(pci);

	return 0;
}

static const struct dw_pcie6_host_ops pcie6_rcar_host_ops = {
	.host_init = rcar_gen5_pcie6_host_init,
};

static int rcar_add_pcie6_port(struct rcar_pcie6 *rcar_pcie6,
				 struct platform_device *pdev)
{
	struct dw_pcie6 *pci = rcar_pcie6->pci;
	struct dw_pcie6_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	rcar_gen5_pcie6_module_run(pci);

	ret = clk_prepare_enable(rcar_pcie6->bus_clk);
	if (ret)
		dev_err(pci->dev, "failed to enable bus clock: %d\n", ret);

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
	struct rcar_pcie6 *rcar_pcie6;
	struct dw_pcie6 *pci;
	int ret;

	rcar_pcie6 = devm_kzalloc(dev, sizeof(*rcar_pcie6), GFP_KERNEL);
	if (!rcar_pcie6)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	rcar_pcie6->pci = pci;

	ret = rcar_gen5_pcie6_get_resources(rcar_pcie6, pdev);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to request resource: %d\n", ret);
		return ret;
	}

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync failed\n");
		goto err_pm_put;
	}

	platform_set_drvdata(pdev, rcar_pcie6);

	ret = rcar_add_pcie6_port(rcar_pcie6, pdev);
	if (ret)
		dev_err(dev, "failed to initialize host\n");

	return 0;

err_pm_put:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return ret;
}

static int rcar_gen5_pcie6_suspend_noirq(struct device *dev)
{
	struct rcar_pcie6 *rcar_pcie6 = dev_get_drvdata(dev);

	clk_disable_unprepare(rcar_pcie6->bus_clk);
	dev_info(dev, "Renesas PCIe6 glue layer suspended.\n");

	return 0;
}

static int rcar_gen5_pcie6_resume_noirq(struct device *dev)
{
	struct rcar_pcie6 *rcar_pcie6 = dev_get_drvdata(dev);
	struct dw_pcie6 *pci = rcar_pcie6->pci;
	struct dw_pcie6_rp *pp = &pci->pp;
	u32 val, ret;

	rcar_gen5_pcie6_module_run(pci);

	ret = clk_prepare_enable(rcar_pcie6->bus_clk);
	if (ret) {
		dev_err(pci->dev, "failed to enable bus clock: %d\n", ret);
	}

	/* Re-initialize Root Complex */
	ret = rcar_gen5_pcie6_host_init(pp);
	if (ret < 0) {
		dev_err(dev, "Failed to init host: %d\n", ret);
		return ret;
	}

	ret = dw_pcie6_setup_rc(pp);
        if (ret < 0) {
                dev_err(dev, "Failed to init RC: %d\n", ret);
                return ret;
        }

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		val = readl(rcar_pcie6->base + 0x2C0);
		val |= MSI_CTRL_INT;
		writel(val, rcar_pcie6->base + 0x2C0);
	}

	if (!dw_pcie6_link_up(pci)) {
		ret = dw_pcie6_start_link(pci);
		if (ret)
			return ret;
	}

	dw_pcie6_wait_for_link(pci);

	dev_info(dev, "Renesas PCIe6 glue layer resumed.\n");
	return 0;
}

static const struct of_device_id pcie6_rcar_host_of_match[] = {
	{
		.compatible = "renesas,rcar-gen5-pcie6",
	},
	{},
};

static const struct dev_pm_ops rcar_gen5_pcie6_dw_pm_ops = {
	.suspend_noirq = rcar_gen5_pcie6_suspend_noirq,
	.resume_noirq = rcar_gen5_pcie6_resume_noirq,
};

static struct platform_driver pcie6_rcar_host_driver = {
	.driver = {
		.name	= "pcie6-rcar",
		.of_match_table = pcie6_rcar_host_of_match,
		.suppress_bind_attrs = true,
		.pm = &rcar_gen5_pcie6_dw_pm_ops,
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
