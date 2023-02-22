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

/* Renesas-specific */
#define PCIEMSR0		0x0000
#define  BIFUR_MOD_SET_ON	BIT(0)
#define  DEVICE_TYPE_EP		0
#define  DEVICE_TYPE_RC		BIT(4)

#define PCIEINTSTS0		0x0084
#define PCIEINTSTS0EN		0x0310
#define  MSI_CTRL_INT		BIT(26)
#define  SMLH_LINK_UP		BIT(7)
#define  RDLH_LINK_UP		BIT(6)
#define PCIEDMAINTSTSEN		0x0314
#define  PCIEDMAINTSTSEN_INIT	GENMASK(15, 0)

struct rcar_gen4_pcie {
	struct dw_pcie		dw;
	void __iomem		*base;
	struct reset_control	*rst;
};
#define to_rcar_gen4_pcie(x)	dev_get_drvdata((x)->dev)

u32 rcar_gen4_pcie_readl(struct rcar_gen4_pcie *pcie, u32 reg);
void rcar_gen4_pcie_writel(struct rcar_gen4_pcie *pcie, u32 reg, u32 val);
int rcar_gen4_pcie_set_device_type(struct rcar_gen4_pcie *rcar, bool rc,
				   int num_lanes);
void rcar_gen4_pcie_disable_bar(struct dw_pcie *dw, u32 bar_mask_reg);
void rcar_gen4_pcie_set_max_link_width(struct dw_pcie *pci, int num_lanes);
int rcar_gen4_pcie_prepare(struct rcar_gen4_pcie *pcie);
void rcar_gen4_pcie_unprepare(struct rcar_gen4_pcie *pcie);
int rcar_gen4_pcie_devm_reset_get(struct rcar_gen4_pcie *pcie,
				  struct device *dev);
struct rcar_gen4_pcie *rcar_gen4_pcie_devm_alloc(struct device *dev);

#endif /* _PCIE_RCAR_GEN4_H_ */
