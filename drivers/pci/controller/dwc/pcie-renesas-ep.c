// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe driver for Renesas R-Car V3U and Gen4 Series SoCs
 *  Copyright (C) 2020-2021 Renesas Electronics Corporation
 *
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "../../pci.h"
#include "pcie-designware.h"

/* Configuration */
#define PCICONF3		0x000C
#define  MULTI_FUNC		BIT(23)
#define EXPCAP3			0x007C
#define  MLW_X1			BIT(4)
#define  MLW_X2			BIT(5)
#define  MLW_X4			BIT(6)
#define EXPCAP12		0x00A0

/* Renesas-specific */
#define PCIEMSR0		0x0000
#define  BIFUR_MOD_SET_ON	(0x1 << 0)
#define  DEVICE_TYPE_EP		(0x0 << 2)

#define PCIERSTCTRL1		0x0014
#define  APP_HOLD_PHY_RST	BIT(16)
#define  APP_LTSSM_ENABLE	BIT(0)

#define PCIEINTSTS0		0x0084
#define  SMLH_LINK_UP		BIT(7)
#define  RDLH_LINK_UP		BIT(6)

/* PCIEC PHY */
#define RCVRCTRLP0		0x0040
#define  PHY0_RX1_TERM_ACDC	BIT(14)
#define  PHY0_RX0_TERM_ACDC	BIT(13)

#define REFCLKCTRLP0		0x0B8

/* Shadow regs */
#define BAR0MASKF0		0x10
#define BAR1MASKF0		0x14
#define BAR2MASKF0		0x18
#define BAR3MASKF0		0x1c
#define BAR4MASKF0		0x20
#define BAR5MASKF0		0x24

#define DWC_VERSION		0x520A

#define to_renesas_pcie(x)	dev_get_drvdata((x)->dev)

struct renesas_pcie_ep {
	struct dw_pcie			*pci;
	void __iomem			*base;
	void __iomem			*phy_base;
	void __iomem			*dma_base;
	void __iomem			*shadow_base;
	struct clk			*bus_clk;
	struct reset_control		*rst;
	u32				num_lanes;
	enum dw_pcie_device_mode        mode;
};

struct renesas_pcie_of_data {
	enum dw_pcie_device_mode	mode;
};

static const struct of_device_id renesas_pcie_of_match[];

static u32 renesas_pcie_readl(struct renesas_pcie_ep *pcie, u32 reg)
{
	return readl(pcie->base + reg);
}

static void renesas_pcie_writel(struct renesas_pcie_ep *pcie, u32 reg, u32 val)
{
	writel(val, pcie->base + reg);
}

static u32 renesas_pcie_phy_readl(struct renesas_pcie_ep *pcie, u32 reg)
{
	return readl(pcie->phy_base + reg);
}

static void renesas_pcie_phy_writel(struct renesas_pcie_ep *pcie, u32 reg,
				    u32 val)
{
	writel(val, pcie->phy_base + reg);
}

static void renesas_pcie_ltssm_enable(struct renesas_pcie_ep *pcie,
				      bool enable)
{
	u32 val;

	val = renesas_pcie_readl(pcie, PCIERSTCTRL1);
	if (enable) {
		val |= APP_LTSSM_ENABLE;
		val &= ~APP_HOLD_PHY_RST;
	} else {
		val &= ~APP_LTSSM_ENABLE;
		val |= APP_HOLD_PHY_RST;
	}
	renesas_pcie_writel(pcie, PCIERSTCTRL1, val);
}

static int renesas_pcie_link_up(struct dw_pcie *pci)
{
	struct renesas_pcie_ep *pcie = to_renesas_pcie(pci);
	u32 val, mask;

	val = renesas_pcie_readl(pcie, PCIEINTSTS0);
	mask = RDLH_LINK_UP | SMLH_LINK_UP;

	return (val & mask) == mask;
}

static int renesas_pcie_start_link(struct dw_pcie *pci)
{
	struct renesas_pcie_ep *pcie = to_renesas_pcie(pci);

	renesas_pcie_ltssm_enable(pcie, true);

	return 0;
}

static void renesas_pcie_stop_link(struct dw_pcie *pci)
{
	struct renesas_pcie_ep *pcie = to_renesas_pcie(pci);

	renesas_pcie_ltssm_enable(pcie, false);
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = renesas_pcie_start_link,
	.stop_link = renesas_pcie_stop_link,
	.link_up = renesas_pcie_link_up,
};

static void renesas_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar;

	for (bar = BAR_0; bar <= BAR_5; bar++)
		dw_pcie_ep_reset_bar(pci, bar);
}

static int renesas_pcie_ep_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				     enum pci_epc_irq_type type,
				     u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return dw_pcie_ep_raise_legacy_irq(ep, func_no);
	case PCI_EPC_IRQ_MSI:
		return dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
	case PCI_EPC_IRQ_MSIX:
		return dw_pcie_ep_raise_msix_irq(ep, func_no, interrupt_num);
	default:
	dev_err(pci->dev, "UNKNOWN IRQ type\n");
		return -EINVAL;
	}

	return 0;
}

static const struct pci_epc_features renesas_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = false,
	/* use 64-bit BARs so mark BAR[1,5] as reserved */
	.reserved_bar = 1 << BAR_1 | 1 << BAR_5,
	.bar_fixed_64bit = 1 << BAR_0 | 1 << BAR_4,
};

static const struct pci_epc_features*
renesas_pcie_ep_get_features(struct dw_pcie_ep *ep)
{
	return &renesas_pcie_epc_features;
}

static const struct dw_pcie_ep_ops pcie_ep_ops = {
	.ep_init = renesas_pcie_ep_init,
	.raise_irq = renesas_pcie_ep_raise_irq,
	.get_features = renesas_pcie_ep_get_features,
};

static int renesas_add_pcie_ep(struct renesas_pcie_ep *pcie,
			       struct platform_device *pdev)
{
	struct dw_pcie *pci = pcie->pci;
	struct dw_pcie_ep *ep;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	ep = &pci->ep;
	ep->ops = &pcie_ep_ops;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "addr_space");
	if (!res)
		return -EINVAL;

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		dev_err(dev, "failed to initialize endpoint\n");
		return ret;
	}

	renesas_pcie_ltssm_enable(pcie, true);

	if (dw_pcie_wait_for_link(pci))
		dev_info(pci->dev, "PCIe link down\n");

	return 0;
}

static void renesas_pcie_init_ep(struct renesas_pcie_ep *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	int val;

	/* Device type selection - Endpoint */
	val = renesas_pcie_readl(pcie, PCIEMSR0);
	val |= DEVICE_TYPE_EP;
	renesas_pcie_writel(pcie, PCIEMSR0, val);

	/* Enable DBI read-only registers for writing */
	dw_pcie_dbi_ro_wr_en(pci);

	/* Single function */
	val = dw_pcie_readl_dbi(pci, PCICONF3);
	val &= ~MULTI_FUNC;
	dw_pcie_writel_dbi(pci, PCICONF3, val);

	/* Disable unused BARs */
	writel(0x0, pcie->shadow_base + BAR2MASKF0);
	writel(0x0, pcie->shadow_base + BAR3MASKF0);

	/* Set Max Link Width */
	val = dw_pcie_readl_dbi(pci, EXPCAP3);
	val &= ~PCI_EXP_LNKCAP_MLW;
	switch (pcie->num_lanes) {
	case 1:
		val |= MLW_X1;
		break;
	case 2:
		val |= MLW_X2;
		break;
	case 4:
		val |= MLW_X4;
		break;
	}
	dw_pcie_writel_dbi(pci, EXPCAP3, val);

	dw_pcie_dbi_ro_wr_dis(pci);

	val = renesas_pcie_phy_readl(pcie, RCVRCTRLP0);
	val |= PHY0_RX0_TERM_ACDC | PHY0_RX1_TERM_ACDC;
	renesas_pcie_phy_writel(pcie, RCVRCTRLP0, val);

	val = renesas_pcie_phy_readl(pcie, REFCLKCTRLP0);
	val |= BIT(10) | BIT(9);
	renesas_pcie_phy_writel(pcie, REFCLKCTRLP0, val);
}

static int renesas_pcie_ep_enable(struct renesas_pcie_ep *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	int ret;

	ret = clk_prepare_enable(pcie->bus_clk);
	if (ret) {
		dev_err(pci->dev, "failed to enable bus clock: %d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(pcie->rst);
	if (ret)
		goto err_clk_disable;

	renesas_pcie_init_ep(pcie);

	return 0;

err_clk_disable:
	clk_disable_unprepare(pcie->bus_clk);

	return ret;
}

static int renesas_pcie_ep_get_resources(struct renesas_pcie_ep *pcie,
					 struct platform_device *pdev)
{
	struct dw_pcie *pci = pcie->pci;
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi2");
	pci->dbi_base2 = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pci->dbi_base2))
		return PTR_ERR(pci->dbi_base2);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "shadow");
	pcie->shadow_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->shadow_base))
		return PTR_ERR(pcie->shadow_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "atu");
	pci->atu_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pci->atu_base))
		return PTR_ERR(pci->atu_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dma");
	pcie->dma_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->dma_base))
		return PTR_ERR(pcie->dma_base);

	/* Renesas-specific registers */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "app");
	pcie->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");
	pcie->phy_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->phy_base))
		return PTR_ERR(pcie->phy_base);

	pcie->bus_clk = devm_clk_get(dev, "pcie_bus");
	if (IS_ERR(pcie->bus_clk)) {
		dev_err(dev, "cannot get pcie bus clock\n");
		return PTR_ERR(pcie->bus_clk);
	}

	pcie->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(pcie->rst)) {
		dev_err(dev, "failed to get Cold-reset\n");
		return PTR_ERR(pcie->rst);
	}

	of_property_read_u32(np, "num-lanes", &pcie->num_lanes);
	if (!pcie->num_lanes) {
		dev_info(dev, "property num-lanes isn't found\n");
		pcie->num_lanes = 2;
	}

	if (pcie->num_lanes <= 0 || pcie->num_lanes > 4 ||
	    pcie->num_lanes == 3) {
		dev_info(dev, "invalid value for num-lanes\n");
		pcie->num_lanes = 2;
	}

	return 0;
}

static int renesas_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct renesas_pcie_ep *pcie;
	int err;
	const struct of_device_id *match;
	const struct renesas_pcie_of_data *data;
	enum dw_pcie_device_mode mode;

	match = of_match_device(renesas_pcie_of_match, dev);
	if (!match)
		return -EINVAL;

	data = (struct renesas_pcie_of_data *)match->data;
	mode = (enum dw_pcie_device_mode)data->mode;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie_ops;
	pci->version = DWC_VERSION;

	pcie->pci = pci;
	pcie->mode = mode;

	pm_runtime_enable(pci->dev);
	err = pm_runtime_get_sync(pci->dev);
	if (err < 0) {
		dev_err(pci->dev, "pm_runtime_get_sync failed\n");
		goto err_pm_put;
	}

	err = renesas_pcie_ep_get_resources(pcie, pdev);
	if (err < 0) {
		dev_err(dev, "failed to request resource: %d\n", err);
		goto err_pm_put;
	}

	platform_set_drvdata(pdev, pcie);

	switch (pcie->mode) {
	case DW_PCIE_RC_TYPE:
		dev_err(dev, "Not support RC mode\n");
		err = -ENODEV;
		goto err_pm_put;
	case DW_PCIE_EP_TYPE:
		err = renesas_pcie_ep_enable(pcie);
		if (err < 0)
			goto err_pm_put;
		err = renesas_add_pcie_ep(pcie, pdev);
		if (err < 0)
			goto err_ep_disable;
		break;
	default:
		dev_err(dev, "Invalid device type: %d\n", pcie->mode);
		err = -ENODEV;
		goto err_pm_put;
	}

	return 0;

err_ep_disable:
	reset_control_assert(pcie->rst);
	clk_disable_unprepare(pcie->bus_clk);

err_pm_put:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return err;
}

static const struct renesas_pcie_of_data renesas_pcie_ep_of_data = {
	.mode = DW_PCIE_EP_TYPE,
};

static const struct of_device_id renesas_pcie_of_match[] = {
	{
		.compatible = "renesas,r8a779a0-pcie-ep",
		.data = &renesas_pcie_ep_of_data,
	},
	{
		.compatible = "renesas,r8a779g0-pcie-ep",
		.data = &renesas_pcie_ep_of_data,
	},
	{},
};

static struct platform_driver renesas_pcie_ep_driver = {
	.driver = {
		.name = "pcie-renesas-ep",
		.of_match_table = renesas_pcie_of_match,
	},
	.probe = renesas_pcie_ep_probe,
};
builtin_platform_driver(renesas_pcie_ep_driver);
