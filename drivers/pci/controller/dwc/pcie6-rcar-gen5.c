// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe 6.0 Controller driver for Renesas R-Car Gen5 Series SoCs
 */

#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/sys_soc.h>
#include <linux/gpio/consumer.h>
#include <linux/iopoll.h>

#include "pcie6-rcar-gen5.h"
#include "pcie6-designware.h"

void __iomem *mdlc_hscs_base = NULL;

inline u32 mdlc_readl(u32 offset)
{
	return readl(mdlc_hscs_base + offset);
}

inline void mdlc_writel(u32 offset, u32 val)
{
	writel(val, mdlc_hscs_base + offset);
}

void module_power_gate_change(u32 pdid, u32 state)
{
	u32 val;

	mdlc_writel(MDLC_PKCPROT0_OFFSET, 0xA5A5A501);

	if ((mdlc_readl(MDLC_MPDGS_OFFSET(pdid)) & 0x3) == state)
		return;

	while (mdlc_readl(MDLC_MPDG_OFFSET(pdid)) !=
		mdlc_readl(MDLC_MPDGS_OFFSET(pdid)));

	switch (state) {
	case STANDBY:
		if ((mdlc_readl(MDLC_MPDGS_OFFSET(pdid)) & 0x3) == RUN) {
			val = mdlc_readl(MDLC_MPDG_OFFSET(pdid));
			val = (val & ~0x3) | RESET;
			mdlc_writel(MDLC_MPDG_OFFSET(pdid), val);

			while (mdlc_readl(MDLC_MPDG_OFFSET(pdid)) !=
				mdlc_readl(MDLC_MPDGS_OFFSET(pdid)));
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

			while (mdlc_readl(MDLC_MPDG_OFFSET(pdid)) !=
				mdlc_readl(MDLC_MPDGS_OFFSET(pdid)));
		}
		val = mdlc_readl(MDLC_MPDG_OFFSET(pdid));
		val = (val & ~0x3) | RUN;
		mdlc_writel(MDLC_MPDG_OFFSET(pdid), val);
	break;
	}
	while (mdlc_readl(MDLC_MPDG_OFFSET(pdid)) !=
		mdlc_readl(MDLC_MPDGS_OFFSET(pdid)));
}

void module_standby_change(u32 regno, u32 offsetnum, u32 state)
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

void rcar_gen5_pcie6_module_reset(struct dw_pcie6 *pci)
{
	if (!mdlc_hscs_base) {
		mdlc_hscs_base = ioremap(MDLC_HSCS_BASE, MDLC_HSCS_SIZE);
		if (!mdlc_hscs_base) {
			dev_err(pci->dev,"Failed to ioremap MDLC_HSCS_BASE\n");
			return;							                }
	}

	module_power_gate_change(PDID_PCI6, RESET);
	module_standby_change(PCIE601_REG_NO, PCIE601_BIT_NO, RESET);
	module_standby_change(PCIE602_REG_NO, PCIE602_BIT_NO, RESET);
	dev_info(pci->dev, "HSCS module powered and reset.\n");
}

void rcar_gen5_pcie6_module_run(struct dw_pcie6 *pci)
{
	if (!mdlc_hscs_base) {
		mdlc_hscs_base = ioremap(MDLC_HSCS_BASE, MDLC_HSCS_SIZE);
		if (!mdlc_hscs_base) {
			dev_err(pci->dev,"Failed to ioremap MDLC_HSCS_BASE\n");
			return;
		}
	}

	module_power_gate_change(PDID_PCI6, RUN);
	module_standby_change(PCIE601_REG_NO, PCIE601_BIT_NO, RUN);
	module_standby_change(PCIE602_REG_NO, PCIE602_BIT_NO, RUN);
	dev_info(pci->dev, "HSCS module powered and run.\n");
}

void rcar_gen5_pcie6_txpreset_coef_mapping(struct dw_pcie6 *pci)
{
	u32 val;

	/* Full swing with preset = '0' */
	for (int k = 0; k <= 2; k++) {
		val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF);
		if (k == 0)
			val &= ~GENMASK(25, 24);
		else
			val |= (k << 24);
		val |= BIT(15);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF, val);

		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x0);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x0000C8C0);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_LOCAL_FS_LF_OFF, 0x00000BCF);

		val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF);
		val |= GENMASK(15, 12);
		dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF, val);
	}

	val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF);
	val |= BIT(15) | (0x3 << 24);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_RELATED_OFF, val);

	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF, 0x0);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0, 0x00000BC0);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_LOCAL_FS_LF_OFF, 0x00000BCF);

	val = dw_pcie6_readl_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF);
	val |= GENMASK(15, 12);
	dw_pcie6_writel_dbi(pci, PCIEG6_PF0_GEN3_EQ_CONTROL_OFF, val);
}

int rcar_gen5_pcie6_monitor_pmd(struct rcar_pcie6 *rcar_pcie6)
{
	struct dw_pcie6 *pci = rcar_pcie6->pci;
	u32 val;
	int ret;

	for (int X = 0; X < 3; X++) {
		ret = readl_poll_timeout(rcar_pcie6->phy_base + PCIEG6_PMD_RX_OVRDVAL_3(X),
					val, !(val & BIT(23)), 1, 999999);

		if (ret) {
			dev_err(pci->dev, "PMD RX_ACK timeout at lane %d\n", X);
			return ret;
		}

		ret = readl_poll_timeout(rcar_pcie6->phy_base + PCIEG6_PMD_TX_OVRDVAL_0(X),
					val, !(val & BIT(27)), 1, 999999);

		if (ret) {
			dev_err(pci->dev, "PMD TX_ACK timeout at lane %d\n", X);
			return ret;
		}
	}

	return 0;
}

void rcar_gen5_pcie6_bootload(struct rcar_pcie6 *rcar_pcie6, int num_lanes, u32 channel)
{
	struct dw_pcie6 *pci = rcar_pcie6->pci;
	u32 val;
	bool boot_done = false;

	if (channel == 0) {
		if (num_lanes == 8) {
			/* load dccm initial value from ROM */
			val = readl(rcar_pcie6->base + PCIE6BOOTLC);
			val |= GENMASK(17, 16) | GENMASK(1, 0);
			writel(val, rcar_pcie6->base + PCIE6BOOTLC);

			/* wait boot load */
			for (int i = 0; i < 1000; i++) {
				val = readl(rcar_pcie6->base + PCIE6BOOTLC);
				if ((val & (BIT(2) | BIT(18))) == (BIT(2) | BIT(18))) {
					boot_done = true;
					break;
				}
			}

			if (!boot_done)
				dev_info(pci->dev,
					"Timeout: BootLoader failed to complete on PCIe6_ch%d\n",
					channel);
			else
				dev_info(pci->dev,
					"BootLoader load complete on PCIe6_ch%d\n",
					channel);

			/* BootLoader disable */
			val = readl(rcar_pcie6->base + PCIE6BOOTLC);
			val &= ~(BIT(1) | BIT(17));
			writel(val, rcar_pcie6->base + PCIE6BOOTLC);

			/* deassert PHY reset */
			val = readl(rcar_pcie6->base + PCI6RESETC);
			val |= GENMASK(7, 0);
			writel(val, rcar_pcie6->base + PCI6RESETC);

			/* request cpu to run */
			val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
			val |= BIT(2) | BIT(18);
			writel(val, rcar_pcie6->base + PCI6CPUCTLSTS);

			/* wait start its programmed sequence */
			boot_done = false;
			for (int i = 0; i < 1000; i++) {
				val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
				if ((val & (BIT(9) | BIT(25))) == (BIT(9) | BIT(25))) {
					boot_done = true;
					break;
				}
			}

			if (!boot_done)
				dev_info(pci->dev,
					"Timeout: CPU failed to start programmed sequence on PCIe6_ch%d\n",
					 channel);
			else
				dev_info(pci->dev,
					"CPU start programming sequence on PCIe6_ch%d\n",
					 channel);

			/* no CPU run request */
			val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
			val &= ~(BIT(2) | BIT(18));
			writel(val, rcar_pcie6->base + PCI6CPUCTLSTS);
		} else {
			val = readl(rcar_pcie6->base + PCIE6BOOTLC);
			val |= GENMASK(1, 0);
			writel(val, rcar_pcie6->base + PCIE6BOOTLC);

			for (int i = 0; i < 1000; i++) {
				val = readl(rcar_pcie6->base + PCIE6BOOTLC);
				if ((val & BIT(2)) == BIT(2)) {
					boot_done = true;
					break;
				}
			}

			if (!boot_done)
				dev_info(pci->dev,
					"Timeout: BootLoader failed to complete on PCIe6_ch%d\n",
					channel);
			else
				dev_info(pci->dev,
					"BootLoader load complete on PCIe6_ch%d\n",
					channel);

			val = readl(rcar_pcie6->base + PCIE6BOOTLC);
			val &= ~BIT(1);
			writel(val, rcar_pcie6->base + PCIE6BOOTLC);

			val = readl(rcar_pcie6->base + PCI6RESETC);
			val |= GENMASK(3, 0);
			writel(val, rcar_pcie6->base + PCI6RESETC);

			val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
			val |= BIT(2);
			writel(val, rcar_pcie6->base + PCI6CPUCTLSTS);

			boot_done = false;
			for (int i = 0; i < 1000; i++) {
				val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
				if ((val & BIT(9)) == BIT(9)) {
					boot_done = true;
					break;
				}
			}

			if (!boot_done)
				dev_info(pci->dev,
					"Timeout: CPU failed to start programmed sequence on PCIe6_ch%d\n",
					channel);
			else
				dev_info(pci->dev,
					"CPU start programming sequence on PCIe6_ch%d\n",
					channel);

			val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
			val &= ~BIT(2);
			writel(val, rcar_pcie6->base + PCI6CPUCTLSTS);
		}
	} else {
		val = readl(rcar_pcie6->base + PCIE6BOOTLC);
		val |= GENMASK(17, 16);
		writel(val, rcar_pcie6->base + PCIE6BOOTLC);

		boot_done = false;
		for (int i = 0; i < 1000; i++) {
			val = readl(rcar_pcie6->base + PCIE6BOOTLC);
			if ((val & BIT(18)) == BIT(18)) {
				boot_done = true;
				break;
			}
		}

		if (!boot_done)
			dev_info(pci->dev,
				"Timeout: BootLoader failed to complete on PCIe6_ch%d\n",
				channel);
		else
			dev_info(pci->dev,
				"BootLoader load complete on PCIe6_ch%d\n",
				channel);

		val = readl(rcar_pcie6->base + PCIE6BOOTLC);
		val &= ~BIT(17);
		writel(val, rcar_pcie6->base + PCIE6BOOTLC);

		val = readl(rcar_pcie6->base + PCI6RESETC);
		val |= GENMASK(7, 4);
		writel(val, rcar_pcie6->base + PCI6RESETC);

		val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
		val |= BIT(18);
		writel(val, rcar_pcie6->base + PCI6CPUCTLSTS);

		boot_done = false;
		for (int i = 0; i < 1000; i++) {
			val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
			if ((val & BIT(25)) == BIT(25)) {
				boot_done = true;
				break;
			}
		}

		if (!boot_done)
			dev_info(pci->dev,
				"Timeout: CPU failed to start programmed sequence on PCIe6_ch%d\n",
				 channel);
		else
			dev_info(pci->dev,
				"CPU start programming sequence on PCIe6_ch%d\n",
				channel);

		val = readl(rcar_pcie6->base + PCI6CPUCTLSTS);
		val &= ~BIT(18);
		writel(val, rcar_pcie6->base + PCI6CPUCTLSTS);
	}
}

void rcar_gen5_pcie6_ltssm_enable(struct rcar_pcie6 *rcar_pcie6,
					bool enable)
{
	u32 val;

	val = readl(rcar_pcie6->base + PCIERSTCTRL1);
	if (enable) {
		val |= BIT(0);
		val &= ~BIT(16);
	} else {
		val &= ~BIT(0);
		val |= BIT(16);
	}
	writel(val, rcar_pcie6->base + PCIERSTCTRL1);
}

void rcar_gen5_pcie6_retrain_link(struct dw_pcie6 *pci)
{
	u32 val, lnksta, retries;

	val = dw_pcie6_readl_dbi(pci, EXPCAP(PCI_EXP_LNKCTL));
	val |= PCI_EXP_LNKCTL_RL;
	dw_pcie6_writel_dbi(pci, EXPCAP(PCI_EXP_LNKCTL), val);

	/* Wait for link retrain */
	for (retries = 0; retries <= 10; retries++) {
		lnksta = dw_pcie6_readw_dbi(pci, EXPCAP(PCI_EXP_LNKSTA));

		/* Check retrain flag */
		if (!(lnksta & PCI_EXP_LNKSTA_LT))
			break;
		mdelay(1);
	}	
}

void rcar_gen5_pcie6_check_speed(struct dw_pcie6 *pci)
{
	u32 lnkcap, lnksta;

	lnkcap = dw_pcie6_readl_dbi(pci, EXPCAP(PCI_EXP_LNKCAP));
	lnksta = dw_pcie6_readw_dbi(pci, EXPCAP(PCI_EXP_LNKSTA));

	if ((lnksta & PCI_EXP_LNKSTA_CLS) != (lnkcap & PCI_EXP_LNKCAP_SLS))
		rcar_gen5_pcie6_retrain_link(pci);
}

int rcar_gen5_pcie6_link_up(struct dw_pcie6 *pci)
{
	struct rcar_pcie6 *rcar_pcie6 = to_rcar_gen5_pcie6(pci);
	u32 val, mask;

	val = readl(rcar_pcie6->base + PCIEINTSTS0);
	mask = GENMASK(7, 6);

	//rcar_gen5_pcie6_check_speed(pci);

	return (val & mask) == mask;
}

int rcar_gen5_pcie6_start_link(struct dw_pcie6 *pci)
{
	struct rcar_pcie6 *rcar_pcie6 = to_rcar_gen5_pcie6(pci);

	rcar_gen5_pcie6_ltssm_enable(rcar_pcie6, true);

	return 0;
}

static const struct dw_pcie6_ops dw_pcie6_ops = {
	.start_link = rcar_gen5_pcie6_start_link,
	.link_up = rcar_gen5_pcie6_link_up,
};

void rcar_gen5_pcie6_set_max_link_width(struct rcar_pcie6 *rcar_pcie6, int num_lanes)
{
	struct dw_pcie6 *pci = rcar_pcie6->pci;
	u32 val;

	val = dw_pcie6_readl_dbi(pci, EXPCAP(PCI_EXP_LNKCAP));
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
	dw_pcie6_writel_dbi(pci, EXPCAP(PCI_EXP_LNKCAP), val);
}

int rcar_gen5_pcie6_get_link_speed(struct device_node *node)
{
	u32 max_link_speed;

	if (of_property_read_u32(node, "max-link-speed", &max_link_speed) ||
		max_link_speed == 0 || max_link_speed > 6)
			return -EINVAL;

	return max_link_speed;
}

void rcar_gen5_pcie6_refclk_phy1(struct rcar_pcie6 *rcar_pcie6, int num_lanes)
{
	u32 val;

	if (num_lanes == 8) {
		val = readl(rcar_pcie6->base + PCI6PY0REFCLK);
		val |= BIT(14);
		val |= ~GENMASK(17, 16);
		writel(val, rcar_pcie6->base + PCI6PY0REFCLK);
	}

	val = readl(rcar_pcie6->base + PCI6PY0REFCLK);
	val |= BIT(18) | BIT(2);
	writel(val, rcar_pcie6->base + PCI6PY0REFCLK);
}

void rcar_gen5_pcie6_set_device_type(struct rcar_pcie6 *rcar_pcie6, bool rc)
{
	struct dw_pcie6 *pci = rcar_pcie6->pci;
	u32 val;

	/* Set device type to Root or Endpoint */
	val = readl(rcar_pcie6->base + PCIEMSR0);
	if (rc)
		val |= DEVICE_TYPE_RC;
	else
		val |= DEVICE_TYPE_EP;
	/* Set lane bifurcation */
	if (pci->num_lanes < 8)
		val |= BIT(0);
	writel(val, rcar_pcie6->base + PCIEMSR0);

	if (pci->num_lanes < 8) {
		val = readl(rcar_pcie6->base + PCIE6BOOTLC);
		val |= BIT(8);
		writel(val, rcar_pcie6->base + PCIE6BOOTLC);
	}
}

int rcar_gen5_pcie6_get_resources(struct rcar_pcie6 *rcar_pcie6,
					struct platform_device *pdev)
{
	struct dw_pcie6 *pci = rcar_pcie6->pci;
	struct device_node *np = dev_of_node(&pdev->dev);
	struct resource *res;

	pci->ops = &dw_pcie6_ops;

	of_property_read_u32(np, "num-lanes", &pci->num_lanes);

	pci->link_gen = rcar_gen5_pcie6_get_link_speed(np);

	if (of_property_read_u32(np, "channel-id", &rcar_pcie6->ch))
		dev_err(&pdev->dev, "Missing channel-id\n");

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	pci->dbi_base = devm_pci_remap_cfg_resource(&pdev->dev, res);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	/* Renesas-specific registers */
	rcar_pcie6->base = devm_platform_ioremap_resource_byname(pdev, "apb");
	if (IS_ERR(rcar_pcie6->base))
		return PTR_ERR(rcar_pcie6->base);

	rcar_pcie6->phy_base = devm_platform_ioremap_resource_byname(pdev, "phy");
	if (IS_ERR(rcar_pcie6->phy_base))
		return PTR_ERR(rcar_pcie6->phy_base);

	rcar_pcie6->bus_clk = devm_clk_get(&pdev->dev, "pcie6_bus");
	if (IS_ERR(rcar_pcie6->bus_clk)) {
		dev_err(&pdev->dev, "Cannot get pcie bus clock\n");
		return PTR_ERR(rcar_pcie6->bus_clk);
	}

	return 0;
}
