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
#include <linux/firmware.h>
#include <linux/unaligned.h>

#include "pcie6-designware.h"

/* PCI Express capability */
#define	PCICONF3		0x000C
#define	EP_MULTI_FUNC		BIT(23)

#define	MSICAP0F0		0x50
#define	EXPCAP(x)		(0x0070 + (x))
#define	PCI_EXP_LNKCAP_MLW_X1	0x00000010 /* Maximum Link Width x1 */
#define	PCI_EXP_LNKCAP_MLW_X2	0x00000020 /* Maximum Link Width x2 */
#define	PCI_EXP_LNKCAP_MLW_X4	0x00000040 /* Maximum Link Width x4 */
#define	PCI_EXP_LNKCAP_MLW_X8	0x00000080 /* Maximum Link Width x8 */

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
#define BFRCTNST						BIT(0)
#define APP_CLK_PM_EN_REQ_N				GENMASK(11, 10)
#define APP_ENTR_L1_L23					GENMASK(6, 5)
#define CFG_SYS_ERR_RC					BIT(9)
#define CFG_SAFETY_UNCORR				BIT(5)
#define CFG_SAFETY_CORR					BIT(4)
#define APP_HOLD_PHY_RST				BIT(16)
#define IB_SEL_OFFSET					BIT(8)
#define PHY1_CM0_RESCAL_MODE_INT		BIT(0)
#define PHY0_REF0_REPEAT_CLK_EN_INT		BIT(14)
#define PHY1_REF0_REPEAT_CLK_EN_INT		GENMASK(29, 28)
#define CLK_DET_EN_INT					(BIT(18) | BIT(2))
#define PIPE_LANE_RESET_0_3				GENMASK(3, 0)
#define PIPE_LANE_RESET_4_7				GENMASK(7, 4)
#define PHY0_CPU_RUN_REQ_INT			BIT(2)
#define PHY1_CPU_RUN_REQ_INT			BIT(18)
#define PHY0_CPU_RUN_ACK_OUT			BIT(9)
#define PHY1_CPU_RUN_ACK_OUT			BIT(25)
#define PHY_CPU_RUN_ACK_MASK			(PHY0_CPU_RUN_ACK_OUT | PHY1_CPU_RUN_ACK_OUT)
#define APP_SRIS_MODE					BIT(6)
#define PCIE_CAP_COMMON_CLK_CONFIG		BIT(23)
#define DO_DESKEW_FOR_SRIS				BIT(6)
#define PCIE_CAP_TARGET_LINK_SPEED		GENMASK(3, 0)
#define ECRC_CHECK_EN					BIT(8)
#define ECRC_GEN_EN						BIT(6)
#define IDE_CTRL_DISABLE				BIT(0)
#define FLIT_INJECT_CAP_NEXT_OFFSET		(0x610U << 20)
#define MOD_TS_USAGE_MODE_SELECT		GENMASK(10, 8)
#define PCIE_CAP_FLIT_MODE_DISABLE		BIT(13)
#define RATE_SHADOW_SELECT				GENMASK(25, 24)
#define APP_LTSSM_ENABLE				BIT(0)
#define SMLH_RDLH_LINK_UP				GENMASK(7, 6)
#define RX_ACK_OVRDVAL					BIT(23)
#define TX_ACK_OVRDVAL					BIT(27)

#define	PCIERSTCTRL1		0x0001C
#define	PCIEPWRMNGCTRL		0x00084
#define	PCIEINTSTS0		0x00098
#define	PCIEERRSTS0EN		0x002BC
#define	PCIEDMAINTSTS0EN	0x002C4
#define	PCIEDMAINTSTS1EN	0x002C8
#define	DMA_INT_EN		GENMASK(31, 0)

#define	PRTLGC2			0x00708
#define	PCIE6BOOTLC		0x00804
#define	PCI6RESETC		0x00808
#define	PCI6CPUCTLSTS		0x0080C
#define	PCI6PY0REFCLK		0x00814
#define	PCI6RESCODE1		0x00824

#define	PCIEG6_PMD_RX_OVRDVAL_3(x)	(0xC400 + (x)*0x400 + 0x2CC)
#define	PCIEG6_PMD_TX_OVRDVAL_0(x)	(0xE800 + (x)*0x200 + 0x0E0)

#define	PCIEG6_PF0_PHY_CONTROL_OFF		0x814
#define	PCIEG6_PF0_GEN3_RELATED_OFF		0x890
#define PCIEG6_PF0_MISC_CONTROL_1_OFF		0x8BC
#define	PCIEG6_PF0_GEN3_EQ_LOCAL_FS_LF_OFF	0x894
#define	PCIEG6_PF0_GEN3_EQ_PSET_COEF_MAP_0	0x898
#define	PCIEG6_PF0_GEN3_EQ_PSET_INDEX_OFF	0x89c
#define	PCIEG6_PF0_GEN3_EQ_CONTROL_OFF		0x8a8
#define	PCIEG6_PF0_SD_EQ_CONTROL1_REG		0x41c

#define PCIEG6_PHY_CONTROL_OFF			0x814
#define PCIEG6_SD_EQ_CONTROL1_REG		0x34C
#define PCIEG6_SPCIE_CAP_OFF_0CH_REG	0x164
#define PCIEG6_SPCIE_CAP_OFF_10H_REG	0x168
#define PCIEG6_SPCIE_CAP_OFF_14H_REG	0x16C
#define PCIEG6_SPCIE_CAP_OFF_18H_REG	0x184
#define PCIEG6_PL16G_CAP_OFF_20H_REG	0x198
#define PCIEG6_PL16G_CAP_OFF_24H_REG	0x19C
#define PCIEG6_PL32G_CAP_OFF_20H_REG	0x1E8
#define PCIEG6_PL32G_CAP_OFF_24H_REG	0x1EC
#define PCIEG6_PL64G_LANE_EQ_10H_REG	0x200
#define PCIEG6_PL64G_LANE_EQ_14H_REG	0x204

#define PCIEG6_PL32G_CONTROL_REG		0x1D0
#define PCIEG6_LINK_CONTROL3_REG		0x15C
#define PCIEG6_FLIT_INJECT_CAP_HDR_REG	0x5B8
#define PCIEG6_LINK_CONTROL2_LINK_STATUS2_REG	0xA0

#define	ICCM_OFFSET	0x10000
#define	DCCM_OFFSET	0x20000

#define PCIE6_FW_DATA_ICCM_NAME		"rcar_gen5_pcie6_iccm.bin"
#define PCIE6_FW_DATA_DCCM_NAME		"rcar_gen5_pcie6_dccm.bin"

#define PCIE6_RCAR_NUM_CLKS			6
#define PCIE6_RCAR_NUM_RSTS			4
#define MAX_LANES_CH1				4

static const char * const rcar_pcie6_clk_ids[PCIE6_RCAR_NUM_CLKS] = {
	"pci601", "pci611", "pci602", "pci612", "pci60bg0", "pci60bg1"
};

static const char * const rcar_pcie6_reset_ids[PCIE6_RCAR_NUM_RSTS] = {
	"rst01", "rst02", "rst11", "rst12"
};

struct rcar_pcie6 {
	struct dw_pcie6		*pci;
	void __iomem		*base;
	void __iomem		*phy_base;
	void __iomem		*phy_shared;
	void __iomem		*dbi_shared;
	void __iomem		*base_shared;
	struct clk_bulk_data	clks[PCIE6_RCAR_NUM_CLKS];
	u32			ch;
	struct reset_control	*perst;
	struct reset_control_bulk_data	rsts[PCIE6_RCAR_NUM_RSTS];
	const struct firmware	*fw_iccm;
	const struct firmware	*fw_dccm;
};

#define to_rcar_gen5_pcie6(x)	dev_get_drvdata((x)->dev)

void rcar_gen5_pcie6_set_device_type(struct rcar_pcie6 *rcar_pcie6, bool rc);
void rcar_gen5_pcie6_channel_aggregation(struct rcar_pcie6 *rcar_pcie6, int num_lanes, int channel);
int rcar_gen5_pcie6_get_resources(struct rcar_pcie6 *rcar_pcie6, struct platform_device *pdev);
int rcar_gen5_pcie6_get_link_speed(struct device_node *node);
void rcar_gen5_pcie6_set_max_link_width(struct rcar_pcie6 *rcar_pcie6, int num_lanes);
void rcar_gen5_pcie6_refclk_phy1(struct rcar_pcie6 *rcar_pcie6, int channel);
void rcar_gen5_pcie6_refres_phy1(struct rcar_pcie6 *rcar_pcie6, int channel);
void rcar_gen5_pcie6_bootload(struct rcar_pcie6 *rcar_pcie6, int num_lanes, u32 channel);
int rcar_gen5_pcie6_monitor_pmd(struct rcar_pcie6 *rcar_pcie6, int num_lanes);
void rcar_gen5_pcie6_txpreset_coef_mapping(struct dw_pcie6 *pci);
#endif /* _PCIE6_RCAR_GEN5_H_ */
