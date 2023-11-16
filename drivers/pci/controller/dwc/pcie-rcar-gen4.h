/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCIe host/endpoint controller driver for Renesas R-Car Gen4 Series SoCs
 * Copyright (C) 2022-2023 Renesas Electronics Corporation
 */

#ifndef _PCIE_RCAR_GEN4_H_
#define _PCIE_RCAR_GEN4_H_

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/reset.h>
#include <linux/clk.h>

#include "pcie-designware.h"

/* PCI Express capability */
#define EXPCAP(x)		(0x0070 + (x))
/* ASPM L1 PM Substates */
#define L1PSCAP(x)		(0x01bc + (x))
/* PCI Shadow offset */
#define SHADOW_REG(x)		(0x2000 + (x))
/* BAR Mask registers */
#define BAR0MASKF		0x0010
#define BAR1MASKF		0x0014
#define BAR2MASKF		0x0018
#define BAR3MASKF		0x001c
#define BAR4MASKF		0x0020
#define BAR5MASKF		0x0024

/* PCIEC PHY */
#define	REFCLKCTRLP0		0x0B8
#define	PHY_REF_CLKDET_EN	BIT(10)
#define	PHY_REF_REPEAT_CLK_EN	BIT(9)
#define	PHY_REF_USE_PAD		BIT(2)

/* Renesas-specific */
#define PCIEMSR0		0x0000
#define  BIFUR_MOD_SET_ON	BIT(0)
#define  DEVICE_TYPE_EP		0
#define  DEVICE_TYPE_RC		BIT(4)
#define  APP_SRIS_MODE		BIT(6)
#define  NONSRIS_MODE		0
#define  SRIS_MODE			BIT(6)


#define PCIEINTSTS0		0x0084
#define PCIEINTSTS0EN		0x0310
#define  MSI_CTRL_INT		BIT(26)
#define  SMLH_LINK_UP		BIT(7)
#define  RDLH_LINK_UP		BIT(6)
#define PCIEDMAINTSTSEN		0x0314
#define  PCIEDMAINTSTSEN_INIT	GENMASK(15, 0)

#define	MSICAP0F0		0x0050
#define	MSIE			BIT(16)

#define	PCIELTRMSGCTRL1		0x0054
#define	LTR_EN			BIT(31)

/*Power Management*/
#define	PCIEPWRMNGCTRL		0x0070
#define	CLK_REG			BIT(11)
#define	CLK_PM			BIT(10)
#define	READY_ENTR		GENMASK(6, 5)
#define	PMCAP1F0		0x0044
#define	PMEE_EN			BIT(8)

/* Error Status Clear */
#define	PCIEERRSTS0CLR		0x033C
#define	PCIEERRSTS1CLR		0x035C
#define	PCIEERRSTS2CLR		0x0360
#define	ERRSTS0_EN		GENMASK(10, 6)
#define	ERRSTS1_EN		GENMASK(29, 0)
#define	ERRSTS2_EN		GENMASK(5, 0)

#define	PCIEERRSTS0EN		0x030C
#define	CFG_SYS_ERR_RC		GENMASK(10, 9)
#define	CFG_SAFETY_UNCORR_CORR	GENMASK(5, 4)

/* Resizable BAR */
#define	PCI_RESBAR_MASK		0x3F00
#define	PCI_RESBAR_CTRL_BAR0	0x03A4

#define PCIE_LINKUP_WA_CLK_NUM (2)

enum pcie_bar_size {
	RESBAR_1M, 	/*0*/
	RESBAR_2M,	/*1*/
	RESBAR_4M,	/*2*/
	RESBAR_8M,	/*3*/
	RESBAR_16M,	/*4*/
	RESBAR_32M,	/*5*/
	RESBAR_64M,	/*6*/
	RESBAR_128M,	/*7*/
	RESBAR_256M,	/*8*/
};

struct rcar_gen4_pcie {
	struct dw_pcie		dw;
	void __iomem		*base;
	void __iomem		*phy_base;
	struct reset_control	*rst;
	struct clk *clks[PCIE_LINKUP_WA_CLK_NUM];
	bool				linkup_setting;
	enum pcie_bar_size	resbar_size;
};
#define to_rcar_gen4_pcie(x)	dev_get_drvdata((x)->dev)

u32 rcar_gen4_pcie_readl(struct rcar_gen4_pcie *pcie, u32 reg);
void rcar_gen4_pcie_writel(struct rcar_gen4_pcie *pcie, u32 reg, u32 val);
int rcar_gen4_pcie_set_device_type(struct rcar_gen4_pcie *rcar, bool rc,
				   int num_lanes);
void rcar_gen4_pcie_disable_bar(struct dw_pcie *dw, u32 bar_mask_reg);
void rcar_gen4_pcie_set_max_link_width(struct dw_pcie *pci, int num_lanes);
void rcar_gen4_pcie_workaround_settings(struct dw_pcie *dw);
int rcar_gen4_pcie_prepare(struct rcar_gen4_pcie *pcie);
void rcar_gen4_pcie_unprepare(struct rcar_gen4_pcie *pcie);
int rcar_gen4_pcie_devm_reset_get(struct rcar_gen4_pcie *pcie,
				  struct device *dev);
struct rcar_gen4_pcie *rcar_gen4_pcie_devm_alloc(struct device *dev);
void rcar_gen4_pcie_phy_setting(struct rcar_gen4_pcie *rcar);
void rcar_gen4_pcie_initial(struct rcar_gen4_pcie *rcar, bool rc);
#endif /* _PCIE_RCAR_GEN4_H_ */
