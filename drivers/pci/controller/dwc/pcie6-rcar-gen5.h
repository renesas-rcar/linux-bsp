/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCIe 6.0 Controller driver for Renesas R-Car Gen5 Series SoCs
 */
#ifndef _PCIE6_RCAR_GEN5_H_
#define _PCIE6_RCAR_GEN5_H_

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>

#include "pcie6-designware.h"

/* PCI Express capability */
#define	PCICONF3		0x000C
#define	EP_MULTI_FUNC		BIT(23)

#define	MSICAP0F0		0x50
#define	EXPCAP(x)		(0x0070 + (x))
#define	PCI_EXP_LNKCAP_MLW_X1	0x00000010 /* Maximum Link Width x1 */
#define	PCI_EXP_LNKCAP_MLW_X2	0x00000020 /* Maximum Link Width x2 */
#define	PCI_EXP_LNKCAP_MLW_X4	0x00000040 /* Maximum Link Width x4 */

#define	PCIE_ADV_ERR_CTRL	0x118
#define	PCIE6_PL32G_CAP		0x1D0
#define	PCIE_PF1_ADV_ERR_CTRL	0x10118
#define	PCIE_PF0_IDE_CTRL	0xC8C

#define	PCIEG6_PF0_RESBAR_CTRL_REG_0_REG	0x618
#define	PCIEG6_PF1_RESBAR_CTRL_REG_0_REG	0x10618

#define	PCIEG6_PF0_RESBAR_CTRL_REG_2_REG	0x620
#define	PCIEG6_PF1_RESBAR_CTRL_REG_2_REG	0x10620

#define	PCIEMSR0		0
#define	DEVICE_TYPE_RC		BIT(4)
#define	DEVICE_TYPE_EP		0
#define	MSI_CTRL_INT		BIT(26)

#define	PCIERSTCTRL1		0x0001C
#define	PCIEPWRMNGCTRL		0x00084
#define	PCIEINTSTS0		0x00098
#define	PCIEERRSTS0EN		0x002BC
#define	PRTLGC2			0x00708
#define	PCIE6BOOTLC		0x00804
#define	PCI6RESETC		0x00808
#define	PCI6CPUCTLSTS		0x0080C
#define	PCI6PY0REFCLK		0x00814

#define	PCIEG6_PMD_RX_OVRDVAL_3(x)	(0xC400 + (x)*0x400 + 0x2CC)
#define	PCIEG6_PMD_TX_OVRDVAL_0(x)	(0xE800 + (x)*0x200 + 0x0E0)

#define	PCIEG6_PF0_PHY_CONTROL_OFF		0x814
#define	PCIEG6_PF0_GEN3_RELATED_OFF		0x890
#define	PCIEG6_PF0_GEN3_EQ_LOCAL_FS_LF_OFF	0x894
#define	PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0	0x898
#define	PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF	0x89c
#define	PCIEG6_PF0_GEN3_EQ_CONTROL_OFF		0x8a8
#define	PCIEG6_PF0_SD_EQ_CONTROL1_REG		0x41c

/* Module standby */
#define	MDLC_HSCS_BASE		0xDE200000
#define	MDLC_HSCS_SIZE		0x1000

#define	STANDBY			0x0
#define	RESET			0x1
#define	STOP			0x2
#define	RUN			0x3

#define	PDID_PCI6		1

#define	PCIE601_REG_NO		4
#define	PCIE611_REG_NO		4
#define	PCIE602_REG_NO		4
#define	PCIE612_REG_NO		4

#define	PCIE601_BIT_NO		0
#define	PCIE611_BIT_NO		2
#define	PCIE602_BIT_NO		4
#define	PCIE612_BIT_NO		6

#define	MDLC_PKCPROT0_OFFSET		0x0CF0
#define	MDLC_PKCPROT1_OFFSET		0x0CF4
#define	MDLC_MPDG_OFFSET(pdid)		(0x0200 + (pdid)*4)
#define	MDLC_MPDGS_OFFSET(pdid)		(0x0300 + (pdid)*4)
#define	MDLC_MSRES_OFFSET(regno)	(0x0900 + (regno)*4)
#define	MDLC_MSRESS_OFFSET(regno)	(0x0960 + (regno)*4)

struct rcar_pcie6 {
	struct dw_pcie6		*pci;
	void __iomem		*base;
	void __iomem		*phy_base;
	struct clk		*bus_clk;
	u32			ch;
};

#define to_rcar_gen5_pcie6(x)	dev_get_drvdata((x)->dev)

void rcar_gen5_pcie6_set_device_type(struct rcar_pcie6 *rcar_pcie6, bool rc);
int rcar_gen5_pcie6_get_resources(struct rcar_pcie6 *rcar_pcie6, struct platform_device *pdev);
void rcar_gen5_pcie6_module_reset(struct dw_pcie6 *pci);
void rcar_gen5_pcie6_module_run(struct dw_pcie6 *pci);
int rcar_gen5_pcie6_get_link_speed(struct device_node *node);
void rcar_gen5_pcie6_set_max_link_width(struct rcar_pcie6 *rcar_pcie6, int num_lanes);
void rcar_gen5_pcie6_refclk_phy1(struct rcar_pcie6 *rcar_pcie6, int num_lanes);
void rcar_gen5_pcie6_bootload(struct rcar_pcie6 *rcar_pcie6, int num_lanes, u32 channel);
int rcar_gen5_pcie6_monitor_pmd(struct rcar_pcie6 *rcar_pcie6);
void rcar_gen5_pcie6_txpreset_coef_mapping(struct dw_pcie6 *pci);
#endif /* _PCIE6_RCAR_GEN5_H_ */
