// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCIe host/endpoint controller driver for Renesas R-Car Gen4 Series SoCs
 * Copyright (C) 2022-2023 Renesas Electronics Corporation
 */

#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/delay.h>


#include "pcie-rcar-gen4.h"
#include "pcie-designware.h"

/* Renesas-specific */
#define PCIERSTCTRL1		0x0014
#define  APP_HOLD_PHY_RST	BIT(16)
#define  APP_LTSSM_ENABLE	BIT(0)

#define	PRTLGC5			0x0714
#define LANE_CONFIG_DUAL	BIT(6)

#define	MAX_RETRIES		10

static void rcar_gen4_pcie_ltssm_enable(struct rcar_gen4_pcie *rcar,
					bool enable)
{
	u32 val;

	val = readl(rcar->base + PCIERSTCTRL1);
	if (enable) {
		val |= APP_LTSSM_ENABLE;
		val &= ~APP_HOLD_PHY_RST;
	} else {
		val &= ~APP_LTSSM_ENABLE;
		val |= APP_HOLD_PHY_RST;
	}
	writel(val, rcar->base + PCIERSTCTRL1);
}

static void rcar_gen4_pcie_retrain_link(struct dw_pcie *dw)
{
	u32 val, lnksta, retries;

	val = dw_pcie_readl_dbi(dw, EXPCAP(PCI_EXP_LNKCTL));
	val |= PCI_EXP_LNKCTL_RL;
	dw_pcie_writel_dbi(dw, EXPCAP(PCI_EXP_LNKCTL), val);

	/* Wait for link retrain */
	for (retries = 0; retries <= MAX_RETRIES; retries++) {
		lnksta = dw_pcie_readw_dbi(dw, EXPCAP(PCI_EXP_LNKSTA));

		/* Check retrain flag */
		if (!(lnksta & PCI_EXP_LNKSTA_LT))
			break;
		mdelay(1);
	}
}

static void rcar_gen4_pcie_check_speed(struct dw_pcie *dw)
{
	u32 lnkcap, lnksta;

	lnkcap = dw_pcie_readl_dbi(dw, EXPCAP(PCI_EXP_LNKCAP));
	lnksta = dw_pcie_readw_dbi(dw, EXPCAP(PCI_EXP_LNKSTA));

	if ((lnksta & PCI_EXP_LNKSTA_CLS) != (lnkcap & PCI_EXP_LNKCAP_SLS))
		rcar_gen4_pcie_retrain_link(dw);
}

static int rcar_gen4_pcie_link_up(struct dw_pcie *dw)
{
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);
	u32 val, mask;

	val = readl(rcar->base + PCIEINTSTS0);
	mask = RDLH_LINK_UP | SMLH_LINK_UP;

	rcar_gen4_pcie_check_speed(dw);

	return (val & mask) == mask;
}

static int rcar_gen4_pcie_start_link(struct dw_pcie *dw)
{
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);

	rcar_gen4_pcie_ltssm_enable(rcar, true);

	return 0;
}

static void rcar_gen4_pcie_stop_link(struct dw_pcie *dw)
{
	struct rcar_gen4_pcie *rcar = to_rcar_gen4_pcie(dw);

	rcar_gen4_pcie_ltssm_enable(rcar, false);
}

int rcar_gen4_pcie_set_device_type(struct rcar_gen4_pcie *rcar, bool rc,
				   int num_lanes)
{
	u32 val;

	/* Note: Assume the reset is asserted here */
	val = readl(rcar->base + PCIEMSR0);
	if (rc)
		val |= DEVICE_TYPE_RC;
	else
		val |= DEVICE_TYPE_EP;
	if (num_lanes < 4)
		val |= BIFUR_MOD_SET_ON;
	writel(val, rcar->base + PCIEMSR0);

	return 0;
}

void rcar_gen4_pcie_disable_bar(struct dw_pcie *dw, u32 bar_mask_reg)
{
	dw_pcie_writel_dbi(dw, SHADOW_REG(bar_mask_reg), 0x0);
}

void rcar_gen4_pcie_workaround_settings(struct dw_pcie *dw)
{
	/* workaround for V4H */
	u32 val = dw_pcie_readl_dbi(dw, PRTLGC5);

	val |= LANE_CONFIG_DUAL;
	dw_pcie_writel_dbi(dw, PRTLGC5, val);
}

int rcar_gen4_pcie_prepare(struct rcar_gen4_pcie *rcar)
{
	struct device *dev = rcar->dw.dev;
	int err;
	const char *clk_names[PCIE_LINKUP_WA_CLK_NUM] = {
		[0] = "pcie0_clk",
		[1] = "pcie1_clk",
	};
	struct clk *clk;
	unsigned int i;
	struct device_node *np = dev_of_node(dev);
	int num_lanes;

	/* "dw->num_lanes" have not yet been set. */
	of_property_read_u32(np, "num-lanes", &num_lanes);

	if (num_lanes == 4)
	{
		if (of_find_property(np, "clock-names", NULL) != NULL)
		{
			for (i = 0; i < PCIE_LINKUP_WA_CLK_NUM; i++) {
				clk = devm_clk_get(dev, clk_names[i]);
				if (PTR_ERR(clk) == -EPROBE_DEFER)
				{
					dev_err(dev, "Failed to get Clock\n");
					return -EPROBE_DEFER;
				}

				rcar->clks[i] = clk;
			}

			err = 0;
			pm_runtime_get_sync(dev);

			for (i = 0; i < PCIE_LINKUP_WA_CLK_NUM; i++) {
				clk_prepare_enable(rcar->clks[i]);
			}
		}
		else
		{
			dev_warn(dev, "Failed to get Clock name.\n");
			return -EINVAL;
		}
	}
	else
	{
		pm_runtime_enable(dev);
		err = pm_runtime_resume_and_get(dev);
		if (err < 0) {
			dev_err(dev, "Failed to resume/get Runtime PM\n");
			pm_runtime_disable(dev);
		}
	}

	return err;
}

void rcar_gen4_pcie_unprepare(struct rcar_gen4_pcie *rcar)
{
	struct device *dev = rcar->dw.dev;
	struct dw_pcie *dw = &rcar->dw;
	unsigned int i;

	if (dw->num_lanes == 4)
	{
		for (i = PCIE_LINKUP_WA_CLK_NUM; i-- > 0; )
			clk_disable_unprepare(rcar->clks[i]);

		pm_runtime_put_sync(dev);
	}
	else
	{
		if (!reset_control_status(rcar->rst))
			reset_control_assert(rcar->rst);
		pm_runtime_put(dev);
		pm_runtime_disable(dev);
	}
}

int rcar_gen4_pcie_devm_reset_get(struct rcar_gen4_pcie *rcar,
				  struct device *dev)
{
	rcar->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(rcar->rst)) {
		dev_err(dev, "Failed to get Cold-reset\n");
		return PTR_ERR(rcar->rst);
	}

	return 0;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = rcar_gen4_pcie_start_link,
	.stop_link = rcar_gen4_pcie_stop_link,
	.link_up = rcar_gen4_pcie_link_up,
};

struct rcar_gen4_pcie *rcar_gen4_pcie_devm_alloc(struct device *dev)
{
	struct rcar_gen4_pcie *rcar;

	rcar = devm_kzalloc(dev, sizeof(*rcar), GFP_KERNEL);
	if (!rcar)
		return NULL;

	rcar->dw.dev = dev;
	rcar->dw.ops = &dw_pcie_ops;
	dw_pcie_cap_set(&rcar->dw, EDMA_UNROLL);

	return rcar;
}

void rcar_gen4_pcie_phy_setting(struct rcar_gen4_pcie *rcar)
{
	u32 val;

	/* PCIe PHY setting */
	val = readl(rcar->phy_base + REFCLKCTRLP0);
	val |= PHY_REF_REPEAT_CLK_EN;
	writel(val, rcar->phy_base + REFCLKCTRLP0);
}

void rcar_gen4_pcie_initial(struct rcar_gen4_pcie *rcar, bool rc)
{
	struct dw_pcie *dw = &rcar->dw;
	u32 val;

	/* Error Status Enable */
	val = readl(rcar->base + PCIEERRSTS0EN);
	val |= CFG_SYS_ERR_RC | CFG_SAFETY_UNCORR_CORR;
	writel(val, rcar->base + PCIEERRSTS0EN);

	/* Error Status Clear */
	val = readl(rcar->base + PCIEERRSTS0CLR);
	val |= ERRSTS0_EN;
	writel(val, rcar->base + PCIEERRSTS0CLR);

	if (rc) {
		/* Power Management */
		val = readl(rcar->base + PCIEPWRMNGCTRL);
		val |= CLK_REG | CLK_PM;
		writel(val, rcar->base + PCIEPWRMNGCTRL);

		/* MSI Enable */
		val = dw_pcie_readl_dbi(dw, MSICAP0F0);
		val |= MSIE;
		dw_pcie_writel_dbi(dw, MSICAP0F0, val);

		/* Set Max Payload Size */
		val = dw_pcie_readl_dbi(dw, EXPCAP(PCI_EXP_DEVCTL));
		val &= ~PCI_EXP_DEVCTL_PAYLOAD;
		val |= PCI_EXP_DEVCTL_PAYLOAD_256B;
		dw_pcie_writel_dbi(dw, EXPCAP(PCI_EXP_DEVCTL), val);

		/* Set Root Control */
		val = dw_pcie_readl_dbi(dw, EXPCAP(PCI_EXP_RTCTL));
		val |= PCI_EXP_RTCTL_SECEE | PCI_EXP_RTCTL_SENFEE |
			PCI_EXP_RTCTL_SEFEE | PCI_EXP_RTCTL_PMEIE |
			PCI_EXP_RTCTL_CRSSVE;
		dw_pcie_writel_dbi(dw, EXPCAP(PCI_EXP_RTCTL), val);

		/* Enable SERR */
		val = dw_pcie_readb_dbi(dw, PCI_BRIDGE_CONTROL);
		val |= PCI_BRIDGE_CTL_SERR;
		dw_pcie_writeb_dbi(dw, PCI_BRIDGE_CONTROL, val);

		/* Device control */
		val = dw_pcie_readl_dbi(dw, EXPCAP(PCI_EXP_DEVCTL));
		val |= PCI_EXP_DEVCTL_CERE | PCI_EXP_DEVCTL_NFERE |
			PCI_EXP_DEVCTL_FERE | PCI_EXP_DEVCTL_URRE;
		dw_pcie_writel_dbi(dw, EXPCAP(PCI_EXP_DEVCTL), val);

		/* Enable PME */
		val = dw_pcie_readl_dbi(dw, PMCAP1F0);
		val |= PMEE_EN;
		dw_pcie_writel_dbi(dw, PMCAP1F0, val);

	} else {
		/* Power Management */
		val = readl(rcar->base + PCIEPWRMNGCTRL);
		val |= CLK_REG | CLK_PM | READY_ENTR;
		writel(val, rcar->base + PCIEPWRMNGCTRL);

		/* Enable LTR */
		val = readl(rcar->base + PCIELTRMSGCTRL1);
		val |= LTR_EN;
		writel(val, rcar->base + PCIELTRMSGCTRL1);
	}
}
