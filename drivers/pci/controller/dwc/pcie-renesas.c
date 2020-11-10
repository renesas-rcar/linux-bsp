// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe driver for Renesas R-Car V3U Series SoCs
 *  Copyright (C) 2020 Renesas Electronics Corporation
 *
 * Author: Hoang Vo <hoang.vo.eb@renesas.com>
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
#define EXPCAP2			0x0078
#define  URRPE			BIT(3)
#define  FERPE			BIT(2)
#define  NFERPE			BIT(1)
#define  CERPE			BIT(0)
#define EXPCAP3			0x007C
#define  MLW_X1			BIT(4)
#define  MLW_X2			BIT(5)
#define  MLW_X4			BIT(6)
#define EXPCAP7			0x008C
#define EXPCAP12		0x00A0

#define L1PSCAP2F0		0x01C4
#define  L11AE			BIT(3)
#define  L12AE			BIT(2)
#define  L11PE			BIT(1)
#define  L12PE			BIT(0)

/* Renesas-specific */
#define PCIEMSR0		0x0000
#define	 BIFUR_MOD_SET_ON	(0x1 << 0)
#define  DEVICE_TYPE_RC		(0x4 << 2)

#define PCIERSTCTRL1		0x0014
#define  APP_HOLD_PHY_RST	BIT(16)
#define  APP_LTSSM_ENABLE	BIT(0)

#define PCIEINTSTS0		0x0084
#define  SMLH_LINK_UP		BIT(7)
#define  RDLH_LINK_UP		BIT(6)

/* PCIEC PHY */
#define RCVRCTRLP0		0x0040
#define PHY0_RX1_TERM_ACDC	BIT(14)
#define PHY0_RX0_TERM_ACDC	BIT(13)

#define DWC_VERSION		0x520A

#define to_renesas_pcie(x)	dev_get_drvdata((x)->dev)

struct renesas_pcie {
	struct dw_pcie			*pci;
	void __iomem			*base;
	void __iomem			*phy_base;
	void __iomem			*dma_base;
	struct clk			*bus_clk;
	struct reset_control		*rst;
	int                             link_gen;
	u32				num_lanes;
	enum dw_pcie_device_mode        mode;
};

struct renesas_pcie_of_data {
	enum dw_pcie_device_mode	mode;
};

static const struct of_device_id renesas_pcie_of_match[];

static u32 renesas_pcie_readl(struct renesas_pcie *pcie, u32 reg)
{
	return readl(pcie->base + reg);
}

static void renesas_pcie_writel(struct renesas_pcie *pcie, u32 reg, u32 val)
{
	writel(val, pcie->base + reg);
}

static u32 renesas_pcie_phy_readl(struct renesas_pcie *pcie, u32 reg)
{
	return readl(pcie->phy_base + reg);
}

static void renesas_pcie_phy_writel(struct renesas_pcie *pcie, u32 reg, u32 val)
{
	writel(val, pcie->phy_base + reg);
}

static void renesas_pcie_change_speed(struct renesas_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	u32 val;

	/* Supports Gen1, Gen2, Gen3 and Gen4 */
	if (pcie->link_gen < 0 || pcie->link_gen > 4)
		pcie->link_gen = 4;

	val = dw_pcie_readl_dbi(pci, EXPCAP12);
	val &= ~((u32)PCI_EXP_LNKCTL2_TLS);
	switch (pcie->link_gen) {
	case 1:
		val |= PCI_EXP_LNKCTL2_TLS_2_5GT;
		break;
	case 2:
		val |= PCI_EXP_LNKCTL2_TLS_5_0GT;
		break;
	case 3:
		val |= PCI_EXP_LNKCTL2_TLS_8_0GT;
		break;
	case 4:
		val |= PCI_EXP_LNKCTL2_TLS_16_0GT;
		break;
	}
	dw_pcie_writel_dbi(pci, EXPCAP12, val);

	/* Deassert directed speed change */
	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val &= ~PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	/* Assert directed speed change */
	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);
}

static void renesas_pcie_ltssm_enable(struct renesas_pcie *pcie,
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
	struct renesas_pcie *pcie = to_renesas_pcie(pci);
	u32 val, mask;

	val = renesas_pcie_readl(pcie, PCIEINTSTS0);
	mask = RDLH_LINK_UP | SMLH_LINK_UP;

	return (val & mask) == mask;
}

static int renesas_pcie_start_link(struct dw_pcie *pci)
{
	struct renesas_pcie *pcie = to_renesas_pcie(pci);

	renesas_pcie_ltssm_enable(pcie, true);

	return 0;
}

static void renesas_pcie_stop_link(struct dw_pcie *pci)
{
	struct renesas_pcie *pcie = to_renesas_pcie(pci);

	renesas_pcie_ltssm_enable(pcie, false);
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = renesas_pcie_start_link,
	.stop_link = renesas_pcie_stop_link,
	.link_up = renesas_pcie_link_up,
};

static int renesas_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct renesas_pcie *pcie = to_renesas_pcie(pci);
	int ret;

	dw_pcie_setup_rc(pp);

	if (!dw_pcie_link_up(pci)) {
		ret = renesas_pcie_start_link(pci);
		if (ret)
			return ret;
	}

	renesas_pcie_change_speed(pcie);

	/* Ignore errors, the link may come up later */
	if (dw_pcie_wait_for_link(pci))
		dev_info(pci->dev, "PCIe link down\n");

	if (IS_ENABLED(CONFIG_PCI_MSI))
		dw_pcie_msi_init(pp);

	return 0;
}

static const struct dw_pcie_host_ops renesas_pcie_host_ops = {
	.host_init = renesas_pcie_host_init,
};

static int renesas_add_pcie_port(struct renesas_pcie *pcie,
				 struct platform_device *pdev)
{
	struct dw_pcie *pci = pcie->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = platform_get_irq_byname(pdev, "msi");
		if (pp->msi_irq < 0)
			return pp->msi_irq;
	}

	pp->ops = &renesas_pcie_host_ops;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static void renesas_pcie_init_rc(struct renesas_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;
	int val;

	/* Device type selection - Root Complex */
	val = renesas_pcie_readl(pcie, PCIEMSR0);
	val |= BIFUR_MOD_SET_ON | DEVICE_TYPE_RC;
	renesas_pcie_writel(pcie, PCIEMSR0, val);

	/* Enable DBI read-only registers for writing */
	dw_pcie_dbi_ro_wr_en(pci);

	/* Enable L1 Substates */
	val = dw_pcie_readl_dbi(pci, L1PSCAP2F0);
	val &= ~PCI_L1SS_CTL1_L1SS_MASK;
	val |= PCI_L1SS_CTL1_PCIPM_L1_2 | PCI_L1SS_CTL1_PCIPM_L1_1 |
		PCI_L1SS_CTL1_ASPM_L1_2 | PCI_L1SS_CTL1_ASPM_L1_1;
	dw_pcie_writel_dbi(pci, L1PSCAP2F0, val);

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

	/* Set Root Control */
	val = dw_pcie_readl_dbi(pci, EXPCAP7);
	val |= PCI_EXP_RTCTL_SECEE | PCI_EXP_RTCTL_SENFEE |
		PCI_EXP_RTCTL_SEFEE | PCI_EXP_RTCTL_PMEIE |
		PCI_EXP_RTCTL_CRSSVE;
	dw_pcie_writel_dbi(pci, EXPCAP7, val);

	/* Set Interrupt Disable, SERR# Enable, Parity Error Response */
	val = dw_pcie_readl_dbi(pci, PCI_COMMAND);
	val |= PCI_COMMAND_PARITY | PCI_COMMAND_SERR |
		PCI_COMMAND_INTX_DISABLE;
	dw_pcie_writel_dbi(pci, PCI_COMMAND, val);

	/* Enable SERR */
	val = dw_pcie_readb_dbi(pci, PCI_BRIDGE_CONTROL);
	val |= PCI_BRIDGE_CTL_SERR;
	dw_pcie_writeb_dbi(pci, PCI_BRIDGE_CONTROL, val);

	/* Device control */
	val = dw_pcie_readl_dbi(pci, EXPCAP2);
	val |= CERPE | NFERPE | FERPE | URRPE;
	dw_pcie_writel_dbi(pci, EXPCAP2, val);

	dw_pcie_dbi_ro_wr_dis(pci);

	val = renesas_pcie_phy_readl(pcie, RCVRCTRLP0);
	val |= PHY0_RX0_TERM_ACDC | PHY0_RX1_TERM_ACDC;
	renesas_pcie_phy_writel(pcie, RCVRCTRLP0, val);
}

static int renesas_pcie_host_enable(struct renesas_pcie *pcie)
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

	renesas_pcie_init_rc(pcie);

	return 0;

err_clk_disable:
	clk_disable_unprepare(pcie->bus_clk);

	return ret;
}

static int renesas_pcie_get_resources(struct renesas_pcie *pcie,
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

	pcie->link_gen = of_pci_get_max_link_speed(np);

	of_property_read_u32(np, "num-lanes", &pcie->num_lanes);
	if (!pcie->num_lanes) {
		dev_info(dev, "property num-lanes isn't found\n");
		pcie->num_lanes = 1;
	}

	return 0;
}

static int renesas_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci;
	struct renesas_pcie *pcie;
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

	err = renesas_pcie_get_resources(pcie, pdev);
	if (err < 0) {
		dev_err(dev, "failed to request resource: %d\n", err);
		goto err_pm_put;
	}

	platform_set_drvdata(pdev, pcie);

	switch (pcie->mode) {
	case DW_PCIE_RC_TYPE:
		err = renesas_pcie_host_enable(pcie);
		if (err < 0)
			goto err_pm_put;

		err = renesas_add_pcie_port(pcie, pdev);
		if (err < 0)
			goto err_host_disable;
		break;
	case DW_PCIE_EP_TYPE:
		dev_err(dev, "Not support EP mode\n");
		err = -ENODEV;
		goto err_pm_put;
	default:
		dev_err(dev, "Invalid device type: %d\n", pcie->mode);
		err = -ENODEV;
		goto err_pm_put;
	}

	return 0;

err_host_disable:
	reset_control_assert(pcie->rst);
	clk_disable_unprepare(pcie->bus_clk);

err_pm_put:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return err;
}

static const struct renesas_pcie_of_data renesas_pcie_rc_of_data = {
	.mode = DW_PCIE_RC_TYPE,
};

static const struct of_device_id renesas_pcie_of_match[] = {
	{
		.compatible = "renesas,r8a779a0-pcie",
		.data = &renesas_pcie_rc_of_data,
	},
	{},
};

static struct platform_driver renesas_pcie_driver = {
	.driver = {
		.name = "pcie-r8a779a0",
		.of_match_table = renesas_pcie_of_match,
	},
	.probe = renesas_pcie_probe,
};
builtin_platform_driver(renesas_pcie_driver);
