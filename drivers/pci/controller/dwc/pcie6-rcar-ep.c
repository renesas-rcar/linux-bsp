// SPDX-License-Identifier: GPL-2.0-only
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
	int ret;

	for (int i = 0; i < PCIE6_RCAR_NUM_RSTS; i++) {
		ret = reset_control_deassert(rcar_pcie6->rsts[i].rstc);
		if (ret) {
			dev_err(pci->dev, "Failed to reset PCIe6 module %s: %d\n",
				rcar_pcie6->rsts[i].id, ret);
			return;
		}
	}

	/* 2. Set device type - Endpoint */
	rcar_gen5_pcie6_set_device_type(rcar_pcie6, false);

	/* 3..5 Channel aggregation */
	rcar_gen5_pcie6_channel_aggregation(rcar_pcie6, pci->num_lanes, rcar_pcie6->ch);

	/* DBI_RO_WR_EN */
	dw_pcie6_dbi_ro_wr_en(pci);
	if (pci->num_lanes == 8) {
		val = readl(rcar_pcie6->dbi_shared + PCIE_MISC_CONTROL_1_OFF);
		val |= PCIE_DBI_RO_WR_EN;
		writel(val, rcar_pcie6->dbi_shared + PCIE_MISC_CONTROL_1_OFF);
	}

	/* 6. Reference Register for PHY1 */
	rcar_gen5_pcie6_refres_phy1(rcar_pcie6, rcar_pcie6->ch);

	/* 7. Sharing REFCLK setting */
	rcar_gen5_pcie6_refclk_phy1(rcar_pcie6, rcar_pcie6->ch);

	/* Disable Endpoint Multi function support */
	val = dw_pcie6_readl_dbi(pci, PCICONF3);
	val &= ~EP_MULTI_FUNC;
	dw_pcie6_writel_dbi(pci, PCICONF3, val);

	/* 8..9 Power Manegement and Control State Machine Setting */
	val = readl(rcar_pcie6->base + PCIEPWRMNGCTRL);
	val |= APP_CLK_PM_EN_REQ_N | APP_ENTR_L1_L23;
	writel(val, rcar_pcie6->base + PCIEPWRMNGCTRL);

	/* 10. Lane setting would be handled by PCIe DWC */
	/* 11. Error Status Enable */
	val = readl(rcar_pcie6->base + PCIEERRSTS0EN);
	val |= CFG_SYS_ERR_RC | CFG_SAFETY_UNCORR | CFG_SAFETY_CORR;
	writel(val, rcar_pcie6->base + PCIEERRSTS0EN);

	/* 12. Clear hold phy reset */
	val = readl(rcar_pcie6->base + PCIERSTCTRL1);
	val &= ~APP_HOLD_PHY_RST;
	writel(val, rcar_pcie6->base + PCIERSTCTRL1);

	rcar_gen5_pcie6_bootload(rcar_pcie6, pci->num_lanes, rcar_pcie6->ch);

	/* 19. Separate REFCLK */
	val = readl(rcar_pcie6->base + PCIEMSR0);
	val |= APP_SRIS_MODE;
	writel(val, rcar_pcie6->base + PCIEMSR0);

	val = dw_pcie6_readl_dbi(pci, PRTLGC2);
	val |= PCIE_CAP_COMMON_CLK_CONFIG;
	dw_pcie6_writel_dbi(pci, PRTLGC2, val);

	val = dw_pcie6_readl_dbi(pci,  EXPCAP(PCI_EXP_LNKCTL));
	val &= ~DO_DESKEW_FOR_SRIS;
	dw_pcie6_writel_dbi(pci,  EXPCAP(PCI_EXP_LNKCTL), val);

	if (pci->num_lanes == 8) {
		val = readl(rcar_pcie6->dbi_shared + PRTLGC2);
		val |= PCIE_CAP_COMMON_CLK_CONFIG;
		writel(val, rcar_pcie6->dbi_shared + PRTLGC2);

		val = readl(rcar_pcie6->dbi_shared +  EXPCAP(PCI_EXP_LNKCTL));
		val &= ~DO_DESKEW_FOR_SRIS;
		writel(val, rcar_pcie6->dbi_shared +  EXPCAP(PCI_EXP_LNKCTL));
	}

	/* 20. Set Max Link Speed*/
	val = dw_pcie6_readl_dbi(pci, PCIEG6_LINK_CONTROL2_LINK_STATUS2_REG);
	val &= ~PCIE_CAP_TARGET_LINK_SPEED;
	val |= (pci->max_link_speed);
	dw_pcie6_writel_dbi(pci, PCIEG6_LINK_CONTROL2_LINK_STATUS2_REG, val);

	/* 21. ECRC gen&Chk for Function0/1 */
	val = dw_pcie6_readl_dbi(pci, PCIE_ADV_ERR_CTRL);
	val |= ECRC_CHECK_EN | ECRC_GEN_EN;
	dw_pcie6_writel_dbi(pci, PCIE_ADV_ERR_CTRL, val);

	val = dw_pcie6_readl_dbi(pci, PCIE_PF1_ADV_ERR_CTRL);
	val |= ECRC_CHECK_EN | ECRC_GEN_EN;
	dw_pcie6_writel_dbi(pci, PCIE_PF1_ADV_ERR_CTRL, val);

	/* 22. IDE Logic disable  */
	val = dw_pcie6_readl_dbi(pci, PCIE_PF0_IDE_CTRL);
	val |= IDE_CTRL_DISABLE;
	dw_pcie6_writel_dbi(pci, PCIE_PF0_IDE_CTRL, val);

	val = dw_pcie6_readl_dbi(pci, PCIEG6_FLIT_INJECT_CAP_HDR_REG);
	val &= FLIT_INJECT_CAP_NEXT_OFFSET;
	dw_pcie6_writel_dbi(pci, PCIEG6_FLIT_INJECT_CAP_HDR_REG, val);

	/* 23. Disable CXL Mode by default */
	/* Endpoint default settings */

	/* 24. Enable flit mode by default */
	val = dw_pcie6_readw_dbi(pci, EXPCAP(PCI_EXP_LNKCTL));
	val &= ~PCIE_CAP_FLIT_MODE_DISABLE;
	dw_pcie6_writew_dbi(pci, EXPCAP(PCI_EXP_LNKCTL), val);

	/* 25. DirectSpeed Change */
	val = dw_pcie6_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val &= ~PORT_LOGIC_SPEED_CHANGE;
	dw_pcie6_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	/* 26. Monitor PMD */
	if (!rcar_gen5_pcie6_monitor_pmd(rcar_pcie6, pci->num_lanes))
		dev_info(pci->dev, "All PMD check passed\n");

	/* 28. TxPreset, Coefficient Mapping */
	rcar_gen5_pcie6_txpreset_coef_mapping(pci);

	/* lane0 Rx 10kohm change to 60ohm */
	val = readl(rcar_pcie6->phy_base + 0x8);
	val |= BIT(9);
	writel(val, rcar_pcie6->phy_base + 0x8);

	/* Enable DMA interrupt */
	val = readl(rcar_pcie6->base + PCIEDMAINTSTS0EN);
	val |= DMA_INT_EN;
	writel(val, rcar_pcie6->base + PCIEDMAINTSTS0EN);

	val = readl(rcar_pcie6->base + PCIEDMAINTSTS1EN);
	val |= DMA_INT_EN;
	writel(val, rcar_pcie6->base + PCIEDMAINTSTS1EN);

	/* HW Workaround */
	val = dw_pcie6_readl_dbi(pci, 0x5b8);
	val &= GENMASK(19, 0);
	val |= 0x610 << 20;
	dw_pcie6_writel_dbi(pci, 0x5b8, val);

	/* DBI_RO_WR_DIS */
	dw_pcie6_dbi_ro_wr_dis(pci);
	if (pci->num_lanes == 8) {
		val = readl(rcar_pcie6->dbi_shared + PCIE_MISC_CONTROL_1_OFF);
		val &= ~PCIE_DBI_RO_WR_EN;
		writel(val, rcar_pcie6->dbi_shared + PCIE_MISC_CONTROL_1_OFF);
	}
}

static void rcar_gen5_pcie6_ep_init(struct dw_pcie6_ep *ep)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_ep(ep);
	enum pci_barno bar;

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++)
		dw_pcie6_ep_reset_bar(pci, bar);

}

static int rcar_gen5_pcie6_ep_raise_irq(struct dw_pcie6_ep *ep, u8 func_no,
					unsigned int type, u16 interrupt_num)
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
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
	}

	return 0;
}

static const struct pci_epc_features pcie6_rcar_epc_get_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
	.bar[BAR_1] = { .type = BAR_RESERVED, },
	.bar[BAR_3] = { .type = BAR_RESERVED, },
	.bar[BAR_5] = { .type = BAR_RESERVED, },
	.align = SZ_1M,
};

static const struct pci_epc_features*
rcar_gen5_pcie6_ep_get_features(struct dw_pcie6_ep *ep)
{
	return &pcie6_rcar_epc_get_features;
}

static const struct dw_pcie6_ep_ops pcie6_rcar_ep_ops = {
	.pre_init = rcar_gen5_pcie6_ep_pre_init,
	.init = rcar_gen5_pcie6_ep_init,
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

	ret = rcar_gen5_pcie6_get_resources(rcar_pcie6, pdev);
	if (ret < 0) {
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

	for (int i = 0; i < PCIE6_RCAR_NUM_CLKS; i++) {
		ret = clk_prepare_enable(rcar_pcie6->clks[i].clk);
		if (ret) {
			dev_err(dev, "Failed to enable clks[%s] clock: %d\n",
				rcar_pcie6->clks[i].id, ret);
			goto err_pm_put;
		}
	}

	ret = dw_pcie6_ep_init(&pci->ep);
	if (ret)
		dev_err(dev, "Failed to initialize endpoint\n");

	ret = dw_pcie6_ep_init_registers(&pci->ep);
	if (ret) {
		dev_info(dev, "Failed to initialize DWC endpoint registers\n");
		dw_pcie6_ep_deinit(&pci->ep);
	}

	pci_epc_init_notify(pci->ep.epc);

	return 0;

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
