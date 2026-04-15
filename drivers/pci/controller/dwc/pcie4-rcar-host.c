// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe RC driver for R-Car Gen5 Series
 *
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
#include <linux/reset.h>
#include <linux/gpio/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/phy/phy.h>
#include <linux/phy.h>

#include "pcie-designware.h"

/* PCI Express capability */
#define	EXPCAP(x)		(0x0070 + (x))

#define	PCI_EXP_LNKCAP_MLW_X1	0x00000010 /* Maximum Link Width x1 */
#define	PCI_EXP_LNKCAP_MLW_X2	0x00000020 /* Maximum Link Width x2 */
#define	PCI_EXP_LNKCAP_MLW_X4	0x00000040 /* Maximum Link Width x4 */

/* Renesas-specific */
#define	PCIEMSR0		0x200
#define	BIFUR_MOD_SET_ON	BIT(0)
#define	DEVICE_TYPE_EP		0
#define	DEVICE_TYPE_RC		BIT(4)

#define PCIERSTCTRL1		0x214
#define	PCIEPWRMNGCTRL		0x270
#define	PCIEINTSTS0		0x284

#define	PCIEINTSTS0EN		0x0510
#define	MSI_CTRL_INT		BIT(26)

#define MDLC_HSCN_BASE		0xC9C90000UL
#define MDLC_HSCN_SIZE		0x1000

#define STANDBY			0x0
#define RESET			0x1
#define STOP			0x2
#define RUN			0x3

#define MDLC_PKCPROT0_OFFSET		0x0CF0
#define MDLC_PKCPROT1_OFFSET		0x0CF4
#define MDLC_MPDG_OFFSET(pdid)		(0x0200 + (pdid)*4)
#define MDLC_MPDGS_OFFSET(pdid)		(0x0300 + (pdid)*4)
#define MDLC_MSRES_OFFSET(regno)	(0x0900 + (regno)*4)
#define MDLC_MSRESS_OFFSET(regno)	(0x0960 + (regno)*4)

#define PDID_PCI4		0

#define PCIE401_REG_NO		6
#define PCIE402_REG_NO		6
#define PCIE411_REG_NO		6
#define PCIE412_REG_NO		6
#define PCIE401_BIT_NO		18
#define PCIE402_BIT_NO		22
#define PCIE411_BIT_NO		20
#define PCIE412_BIT_NO		24

#define to_rcar_gen5_pcie(x)	dev_get_drvdata((x)->dev)

struct rcar_pcie4 {
	struct dw_pcie			*pci;
	struct phy			*phy;
	phy_interface_t			phy_interface;
	enum dw_pcie_device_mode	mode;
	void __iomem			*base;
	void __iomem			*phy_base;
	struct reset_control		*rst;
	struct clk			*bus_clk;
	struct gpio_desc		*perst;
};

static void rcar_gen5_pcie_ltssm_enable(struct rcar_pcie4 *rcar_pcie4,
				      bool enable)
{
	u32 val;

	val = readl(rcar_pcie4->base + PCIERSTCTRL1);
	if (enable) {
		val |= BIT(0);
		val &= ~BIT(16);
	} else {
		val &= ~BIT(0);
		val |= BIT(16);
	}
	writel(val, rcar_pcie4->base + PCIERSTCTRL1);

	phy_power_on(rcar_pcie4->phy);
}

static void rcar_gen5_pcie_retrain_link(struct dw_pcie *pci)
{
	u32 val, lnksta, retries;

	val = dw_pcie_readl_dbi(pci, EXPCAP(PCI_EXP_LNKCTL));
	val |= PCI_EXP_LNKCTL_RL;
	dw_pcie_writel_dbi(pci, EXPCAP(PCI_EXP_LNKCTL), val);

	/* Wait for link retrain */
	for (retries = 0; retries <= 10; retries++) {
		lnksta = dw_pcie_readw_dbi(pci, EXPCAP(PCI_EXP_LNKSTA));

		/* Check retrain flag */
		if (!(lnksta & PCI_EXP_LNKSTA_LT))
			break;
		mdelay(1);
	}
}

static void rcar_gen5_pcie_check_speed(struct dw_pcie *pci)
{
	u32 lnkcap, lnksta;

	lnkcap = dw_pcie_readl_dbi(pci, EXPCAP(PCI_EXP_LNKCAP));
	lnksta = dw_pcie_readw_dbi(pci, EXPCAP(PCI_EXP_LNKSTA));

	if ((lnksta & PCI_EXP_LNKSTA_CLS) != (lnkcap & PCI_EXP_LNKCAP_SLS))
		rcar_gen5_pcie_retrain_link(pci);
}

static int rcar_gen5_pcie_link_up(struct dw_pcie *pci)
{
	struct rcar_pcie4 *rcar_pcie4 = to_rcar_gen5_pcie(pci);
	u32 val, mask;

	val = readl(rcar_pcie4->base + PCIEINTSTS0);
	mask = GENMASK(7,6);

	rcar_gen5_pcie_check_speed(pci);

	return (val & mask) == mask;
}

static int rcar_gen5_pcie_start_link(struct dw_pcie *pci)
{
	struct rcar_pcie4 *rcar_pcie4 = to_rcar_gen5_pcie(pci);

	rcar_gen5_pcie_ltssm_enable(rcar_pcie4, true);

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = rcar_gen5_pcie_start_link,
	.link_up = rcar_gen5_pcie_link_up,
};

static void rcar_gen5_pcie_set_max_link_width(struct rcar_pcie4 *rcar_pcie4, int num_lanes)
{
	struct dw_pcie *pci = rcar_pcie4->pci;
	u32 val;

	val = dw_pcie_readl_dbi(pci, EXPCAP(PCI_EXP_LNKCAP));
	val &= ~PCI_EXP_LNKCAP_MLW;
	switch (num_lanes) {
	case 1:
		val |= PCI_EXP_LNKCAP_MLW_X1;
		break;
	case 2:
		val |= PCI_EXP_LNKCAP_MLW_X2;
		break;
	case 4:
		val |= PCI_EXP_LNKCAP_MLW_X4;
		break;
	default:
		dev_info(pci->dev, "Invalid num-lanes %d\n", num_lanes);
		break;
	}
	dw_pcie_writel_dbi(pci, EXPCAP(PCI_EXP_LNKCAP), val);
}

static int rcar_gen5_pcie_set_device_type(struct rcar_pcie4 *rcar_pcie4, bool rc)
{
	u32 val;

	/* Note: Assume the reset is asserted here */
	val = readl(rcar_pcie4->base + PCIEMSR0);
	if (rc)
		val |= DEVICE_TYPE_RC | 0x1;
	else
		val |= DEVICE_TYPE_EP | 0x1;
	writel(val, rcar_pcie4->base + PCIEMSR0);

	return 0;
}

static void __iomem *mdlc_hscn_base = NULL;

static inline u32 mdlc_readl(u32 offset) {
	return readl(mdlc_hscn_base + offset);
}

static inline void mdlc_writel(u32 offset, u32 val) {
	writel(val, mdlc_hscn_base + offset);
}

static void module_power_gate_change(u32 pdid, u32 state) {
	u32 val;
	mdlc_writel(MDLC_PKCPROT0_OFFSET, 0xA5A5A501);

	if ((mdlc_readl(MDLC_MPDGS_OFFSET(pdid)) & 0x3) == state)
		return;
	while (mdlc_readl(MDLC_MPDG_OFFSET(pdid)) != mdlc_readl(MDLC_MPDGS_OFFSET(pdid)));

	switch (state) {
	case STANDBY:
		if ((mdlc_readl(MDLC_MPDGS_OFFSET(pdid)) & 0x3) == RUN) {
			val = mdlc_readl(MDLC_MPDG_OFFSET(pdid));
			val = (val & ~0x3) | RESET;
			mdlc_writel(MDLC_MPDG_OFFSET(pdid), val);
			while (mdlc_readl(MDLC_MPDG_OFFSET(pdid)) != mdlc_readl(MDLC_MPDGS_OFFSET(pdid)));
		}

		val = mdlc_readl(MDLC_MPDG_OFFSET(pdid));
		val = (val & ~0x3) | STANDBY;
		mdlc_writel(MDLC_MPDG_OFFSET(pdid), val);
	break;

	case RESET:
		val = mdlc_readl(MDLC_MPDG_OFFSET(pdid));
		val = (val & ~0x3) | RESET;
		mdlc_writel(MDLC_MPDG_OFFSET(pdid), val);
	break;

	case RUN:
		if ((mdlc_readl(MDLC_MPDGS_OFFSET(pdid)) & 0x3) == STANDBY) {
			val = mdlc_readl(MDLC_MPDG_OFFSET(pdid));
			val = (val & ~0x3) | RESET;
			mdlc_writel(MDLC_MPDG_OFFSET(pdid), val);
			while (mdlc_readl(MDLC_MPDG_OFFSET(pdid)) != mdlc_readl(MDLC_MPDGS_OFFSET(pdid)));
		}
		val = mdlc_readl(MDLC_MPDG_OFFSET(pdid));
		val = (val & ~0x3) | RUN;
		mdlc_writel(MDLC_MPDG_OFFSET(pdid), val);
	break;
	}
	while (mdlc_readl(MDLC_MPDG_OFFSET(pdid)) != mdlc_readl(MDLC_MPDGS_OFFSET(pdid)));
}

static void module_standby_change(u32 regno, u32 offsetnum, u32 state)
{
        u32 ckMSRESS, ckMSRES, val, cur;

        mdlc_writel(MDLC_PKCPROT1_OFFSET, 0xA5A5A501);
        if (((mdlc_readl(MDLC_MSRESS_OFFSET(regno)) >> offsetnum) & 0x3) == state)
                return;

        do {
                ckMSRESS = mdlc_readl(MDLC_MSRESS_OFFSET(regno)) & (0x3 << offsetnum);
                ckMSRES  = mdlc_readl(MDLC_MSRES_OFFSET(regno))  & (0x3 << offsetnum);
        } while (ckMSRESS != ckMSRES);

        cur = (ckMSRES >> offsetnum) & 0x3;

        switch (state) {
        case STANDBY:
                if (cur == RUN) {
                        val = mdlc_readl(MDLC_MSRES_OFFSET(regno));
                        val = (val & ~(0x3 << offsetnum)) | (RESET << offsetnum);
                        mdlc_writel(MDLC_MSRES_OFFSET(regno), val);

                        do {
                                ckMSRESS = mdlc_readl(MDLC_MSRESS_OFFSET(regno)) & (0x3 << offsetnum);
                                ckMSRES  = mdlc_readl(MDLC_MSRES_OFFSET(regno))  & (0x3 << offsetnum);
                        } while (ckMSRESS != ckMSRES);
                }
                break;
        case RESET:
                if (cur == STOP) {
                        val = mdlc_readl(MDLC_MSRES_OFFSET(regno));
                        val = (val & ~(0x3 << offsetnum)) | (RUN << offsetnum);
                        mdlc_writel(MDLC_MSRES_OFFSET(regno), val);

                        do {
                                ckMSRESS = mdlc_readl(MDLC_MSRESS_OFFSET(regno)) & (0x3 << offsetnum);
                                ckMSRES  = mdlc_readl(MDLC_MSRES_OFFSET(regno))  & (0x3 << offsetnum);
                        } while (ckMSRESS != ckMSRES);
                }
                break;
        case STOP:
                if (cur == RESET) {
                        val = mdlc_readl(MDLC_MSRES_OFFSET(regno));
                        val = (val & ~(0x3 << offsetnum)) | (RUN << offsetnum);
                        mdlc_writel(MDLC_MSRES_OFFSET(regno), val);

                        do {
                                ckMSRESS = mdlc_readl(MDLC_MSRESS_OFFSET(regno)) & (0x3 << offsetnum);
                                ckMSRES  = mdlc_readl(MDLC_MSRES_OFFSET(regno))  & (0x3 << offsetnum);
                        } while (ckMSRESS != ckMSRES);
                }
                break;
        case RUN:
                if (cur == STANDBY) {
                        val = mdlc_readl(MDLC_MSRES_OFFSET(regno));
                        val = (val & ~(0x3 << offsetnum)) | (RESET << offsetnum);
                        mdlc_writel(MDLC_MSRES_OFFSET(regno), val);
                        do {
                                ckMSRESS = mdlc_readl(MDLC_MSRESS_OFFSET(regno)) & (0x3 << offsetnum);
                                ckMSRES  = mdlc_readl(MDLC_MSRES_OFFSET(regno))  & (0x3 << offsetnum);
                        } while (ckMSRESS != ckMSRES);
                }
                break;
        }
        val = mdlc_readl(MDLC_MSRES_OFFSET(regno));
        val = (val & ~(0x3 << offsetnum)) | (state << offsetnum);
        mdlc_writel(MDLC_MSRES_OFFSET(regno), val);
        do {
                ckMSRESS = mdlc_readl(MDLC_MSRESS_OFFSET(regno)) & (0x3 << offsetnum);
                ckMSRES  = mdlc_readl(MDLC_MSRES_OFFSET(regno))  & (0x3 << offsetnum);
        } while (ckMSRESS != ckMSRES);
}

void rcar_gen5_pcie_module_run(struct dw_pcie *pci)
{
	if (!mdlc_hscn_base) {
		mdlc_hscn_base = ioremap(MDLC_HSCN_BASE, MDLC_HSCN_SIZE);
		if (!mdlc_hscn_base) {
			dev_err(pci->dev,"[PCIE] Failed to ioremap MDLC_HSCN_BASE\n");
			return;
		}
	}

	module_power_gate_change(PDID_PCI4, RUN);
	module_standby_change(PCIE401_REG_NO, PCIE401_BIT_NO, RUN);
	module_standby_change(PCIE402_REG_NO, PCIE402_BIT_NO, RUN);
	module_standby_change(PCIE411_REG_NO, PCIE411_BIT_NO, RUN);
	module_standby_change(PCIE412_REG_NO, PCIE412_BIT_NO, RUN);
	dev_info(pci->dev,"[PCIE] HSCN module powered and running.\n");
}

static int rcar_gen5_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct rcar_pcie4 *rcar_pcie4 = to_rcar_gen5_pcie(pci);
	int ret;
	u32 val;

	rcar_gen5_pcie_module_run(pci);

	ret = clk_prepare_enable(rcar_pcie4->bus_clk);
	if (ret) {
		dev_err(pci->dev, "failed to enable bus clock: %d\n", ret);
	}

	ret = phy_set_mode_ext(rcar_pcie4->phy, PHY_MODE_PCIE,
			       rcar_pcie4->phy_interface);
	if (ret) {
		pr_info("PCIe4: Failed to set mode to mp-phy\n");
		return ret;
	}

	ret = phy_init(rcar_pcie4->phy);
	if (ret) {
		dev_err(pci->dev, "Failed to initialize MP-PHY\n");
		return ret;
	}

/*
	ret = reset_control_deassert(rcar_pcie4->rst);
	if (ret)
		goto err_clk_disable;
*/

	pci->ops = &dw_pcie_ops;

	/* Set device type */
	ret = rcar_gen5_pcie_set_device_type(rcar_pcie4, true);
	if (ret < 0)
		return ret;

	dw_pcie_dbi_ro_wr_en(pci);

	/* PCIe Lane Skew off */
	if (pci->num_lanes < 8) {
		val = dw_pcie_readl_dbi(pci, 0x714);
		val |= 0x40;
		dw_pcie_writel_dbi(pci, 0x714, val);
	}

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		/* Enable MSI interrupt signal */
		val = readl(rcar_pcie4->base + PCIEINTSTS0EN);
		val |= MSI_CTRL_INT;
		writel(val, rcar_pcie4->base + PCIEINTSTS0EN);
	}

	rcar_gen5_pcie_set_max_link_width(rcar_pcie4, pci->num_lanes);

	val = dw_pcie_readl_dbi(pci, 0x40);
	val |= BIT(19);
	dw_pcie_writel_dbi(pci, 0x40, val);

	dw_pcie_writel_dbi(pci, EXPCAP(PCI_EXP_LNKCTL2), 0x30100004);

	dw_pcie_dbi_ro_wr_dis(pci);

	/* PM setting */
	val = readl(rcar_pcie4->base + PCIEPWRMNGCTRL);
	val |= GENMASK(11, 10) | GENMASK(6, 5);
	writel(val, rcar_pcie4->base + PCIEPWRMNGCTRL);

	return 0;
}

static const struct dw_pcie_host_ops rcar_gen5_pcie_host_ops = {
	.init = rcar_gen5_pcie_host_init,
};

static int rcar_gen5_pcie_devm_reset_get(struct rcar_pcie4 *rcar_pcie4,
				  struct device *dev)
{
/*	rcar_pcie4->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(rcar_pcie4->rst)) {
		dev_err(dev, "Failed to get Cold-reset\n");
		return PTR_ERR(rcar_pcie4->rst);
	}
*/
	rcar_pcie4->bus_clk = devm_clk_get(dev, "pcie4_bus");
	if (IS_ERR(rcar_pcie4->bus_clk)) {
		dev_err(dev, "Cannot get pcie bus clock\n");
		return PTR_ERR(rcar_pcie4->bus_clk);
	}

	return 0;
}

static int rcar_gen5_pcie_get_resources(struct rcar_pcie4 *rcar_pcie4,
					struct platform_device *pdev)
{
	struct resource *res;
	struct dw_pcie *pci = rcar_pcie4->pci;
	struct device_node *np = dev_of_node(&pdev->dev);

	of_property_read_u32(np, "num-lanes", &pci->num_lanes);

	/* Renesas-specific registers */
	rcar_pcie4->base = devm_platform_ioremap_resource_byname(pdev, "apb");
	if (IS_ERR(rcar_pcie4->base))
		return PTR_ERR(rcar_pcie4->base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");
	if (res) {
		rcar_pcie4->phy_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(rcar_pcie4->phy_base))
			rcar_pcie4->phy_base = NULL;
	}

	/* Get PHY from device tree */
	rcar_pcie4->phy = devm_of_phy_get_by_index(&pdev->dev, np, 0);
	if (IS_ERR(rcar_pcie4->phy)) {
		dev_err(&pdev->dev, "Failed to get PHY: %ld\n", PTR_ERR(rcar_pcie4->phy));
		return PTR_ERR(rcar_pcie4->phy);
	}

	return rcar_gen5_pcie_devm_reset_get(rcar_pcie4, &pdev->dev);
}

static int rcar_gen5_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_pcie4 *rcar_pcie4;
	struct dw_pcie *pci;
	struct dw_pcie_rp *pp;
	int ret;

	rcar_pcie4 = devm_kzalloc(dev, sizeof(*rcar_pcie4), GFP_KERNEL);
	if (!rcar_pcie4)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pp = &pci->pp;
	pp->num_vectors = MAX_MSI_IRQS;
	pp->ops = &rcar_gen5_pcie_host_ops;
	rcar_pcie4->pci = pci;

	pm_runtime_enable(pci->dev);
	ret = pm_runtime_get_sync(pci->dev);
	if (ret < 0) {
		dev_err(pci->dev, "pm_runtime_get_sync failed\n");
		goto err_pm_put;
	}

	ret = rcar_gen5_pcie_get_resources(rcar_pcie4, pdev);
	if (ret < 0) {
		dev_err(dev, "Failed to request resource: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, rcar_pcie4);

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;

err_pm_put:
	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return ret;
}

static int rcar_gen5_pcie_suspend_noirq(struct device *dev)
{
	struct rcar_pcie4 *rcar_pcie4 = dev_get_drvdata(dev);

	if (rcar_pcie4->phy) {
		phy_power_off(rcar_pcie4->phy);
		phy_exit(rcar_pcie4->phy);
	}

	clk_disable_unprepare(rcar_pcie4->bus_clk);
	dev_info(dev, "Renesas PCIe4 glue layer suspended.\n");

	return 0;
}

static int rcar_gen5_pcie_resume_noirq(struct device *dev)
{
	struct rcar_pcie4 *rcar_pcie4 = dev_get_drvdata(dev);
	struct dw_pcie *pci = rcar_pcie4->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	int ret;

	ret = rcar_gen5_pcie_host_init(pp);
	if (ret)
		return ret;

	ret = dw_pcie_setup_rc(pp);
	if (ret)
		return ret;

	if (!dw_pcie_link_up(pci)) {
		ret = dw_pcie_start_link(pci);
		if (ret)
			return ret;
	}

	dw_pcie_wait_for_link(pci);

	dev_info(dev, "Renesas PCIe4 glue layer resumed.\n");
	return 0;
}

static const struct of_device_id rcar_gen5_pcie_of_match[] = {
	{ .compatible = "renesas,rcar-gen5-pcie", },
	{},
};

static const struct dev_pm_ops rcar_gen5_pcie_dw_pm_ops = {
	.suspend_noirq = rcar_gen5_pcie_suspend_noirq,
	.resume_noirq = rcar_gen5_pcie_resume_noirq,
};

static struct platform_driver pcie4_rcar_gen5_driver = {
	.driver = {
		.name	= "pcie4-rcar",
		.of_match_table = rcar_gen5_pcie_of_match,
		.suppress_bind_attrs = true,
		.pm = &rcar_gen5_pcie_dw_pm_ops,
	},
	.probe = rcar_gen5_pcie_probe,
};
builtin_platform_driver(pcie4_rcar_gen5_driver);

MODULE_AUTHOR("Tin Tran");
MODULE_DESCRIPTION("Renesas PCIe 4.0 driver");
MODULE_LICENSE("GPL v2");
