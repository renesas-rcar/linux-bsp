// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe 6.0 Endpoint driver for Renesas R-Car Gen5 Series SoCs
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

static void rcar_gen5_pcie6_ep_pre_init(struct dw_pcie6_ep *ep)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	struct rcar_pcie6 *rcar_pcie6 = to_rcar_gen5_pcie6(pci);
	u32 val;

	/* Separate clkreq */
	val = readl(rcar_pcie6->base + PCIEMSR0);
	val |= BIT(6);
	writel(val, rcar_pcie6->base + PCIEMSR0);

	rcar_gen5_pcie6_module_reset(pci);
	rcar_gen5_pcie6_module_run(pci);

	/* Set device type - Endpoint */
	rcar_gen5_pcie6_set_device_type(rcar_pcie6, false);

	dw_pcie6_dbi_ro_wr_en(pci);

	/* Disable Endpoint Multi function support */
	val = dw_pcie6_readl_dbi(pci, PCICONF3);
	val &= ~EP_MULTI_FUNC;
	dw_pcie6_writel_dbi(pci, PCICONF3, val);

	rcar_gen5_pcie6_set_max_link_width(rcar_pcie6, pci->num_lanes);

	/* Power Manegement Setting */
	val = readl(rcar_pcie6->base + PCIEPWRMNGCTRL);
	val |= GENMASK(11, 10) | GENMASK(6, 5);
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

	val = dw_pcie6_readl_dbi(pci, PCIE_PF1_ADV_ERR_CTRL);
	val |= BIT(8) | BIT(6);
	dw_pcie6_writel_dbi(pci, PCIE_PF1_ADV_ERR_CTRL, val);

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

	/* DirectSpeed Change */
	val = dw_pcie6_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie6_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	/* Monitor PMD */
	if (!rcar_gen5_pcie6_monitor_pmd(rcar_pcie6))
		dev_info(pci->dev, "All PMD check passed\n");

	rcar_gen5_pcie6_txpreset_coef_mapping(pci);

	/* lane0 Rx 10kohm change to 60ohm */
	val = readl(rcar_pcie6->phy_base + 0x8);
	val |= BIT(9);
	writel(val, rcar_pcie6->phy_base + 0x8);

	/* BAR0 resizing */
	val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_RESBAR_CTRL_REG_0_REG);
	val &= ~GENMASK(13, 8);
	val |= BIT(11);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_RESBAR_CTRL_REG_0_REG, val);

	val = dw_pcie6_readl_dbi(pci, PCIEG6_PF1_RESBAR_CTRL_REG_0_REG);
	val &= ~GENMASK(13, 8);
	val |= BIT(11);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF1_RESBAR_CTRL_REG_0_REG, val);

	dw_pcie6_dbi_ro_wr_dis(pci);
}

static void rcar_gen5_pcie6_ep_init(struct dw_pcie6_ep *ep)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	enum pci_barno bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie6_ep_reset_bar(pci, bar);

}

static int rcar_gen5_pcie6_ep_raise_irq(struct dw_pcie6_ep *ep, u8 func_no,
					enum pci_epc_irq_type type, u16 interrupt_num)
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
rcar_gen5_pcie6_ep_get_features(struct dw_pcie6_ep *ep)
{
	return &pcie6_rcar_epc_get_features;
}

static const struct dw_pcie6_ep_ops pcie6_rcar_ep_ops = {
	.ep_init = rcar_gen5_pcie6_ep_init,
	.raise_irq = rcar_gen5_pcie6_ep_raise_irq,
	.get_features = rcar_gen5_pcie6_ep_get_features,
};

static int pcie6_rcar_ep_probe(struct platform_device *pdev)
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
	pci->ep.ops = &pcie6_rcar_ep_ops;

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync failed\n");
		goto err_pm_put;
	}

	ret = rcar_gen5_pcie6_get_resources(rcar_pcie6, pdev);
	if (ret < 0) {
		dev_err(dev, "Failed to request resource: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, rcar_pcie6);

	rcar_gen5_pcie6_module_run(pci);

	ret = clk_prepare_enable(rcar_pcie6->bus_clk);
	if (ret)
		dev_err(dev, "failed to enable bus clock: %d\n", ret);

	rcar_gen5_pcie6_ep_pre_init(&pci->ep);

	ret = dw_pcie6_ep_init(&pci->ep);
	if (ret)
		dev_err(dev, "failed to initialize endpoint\n");

err_pm_put:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

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
