/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UCIe host/endpoint controller driver for Renesas R-Car Gen5 Series SoCs
 * Copyright (C) 2024 Renesas Electronics Corporation
 */

#ifndef _UCIE_RCAR_H_
#define _UCIE_RCAR_H_

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/iopoll.h>

#include "pcie6-designware.h"

/* Vendor IP message */
#define OPCODE				GENMAKS(4, 0)
#define OPCODE_MEM_READ32		0
#define OPCODE_MEM_WRITE32		0x1
#define OPCODE_CONF_READ32		0x4
#define OPCODE_CONF_WRITE32		0x5

#define BYTE_ENABLES			GENMASK(21, 14)
#define BYTE_ENABLES_32			(0xf << 14)

#define SRCID				GENMASK(31, 29)
#define SRCID_PROTO_STACK0_ACCESS	(0 << 29)

#define DSTID				GENMASK(26, 24)
#define DSTID_PROTO_STACK_ACCESS	(0x1 << 24)
#define DSTID_PHY_STACK_ACCESS		(0x2 << 24)

#define COMPL_STATUS			GENMASK(4, 0)
#define COMPL_SUCCESS			0

#define CONTROL_PARITY(n)		((n) << 30)
#define DATA_PARITY(n)			((n) << 31)

/* APB registers */
#define APB_BRIDGE_CTL0			0x0100
#define APB_BRIDGE_CTL1			0x0104
#define APB_BRIDGE_CTL2			0x0108
#define APB_BRIDGE_CTL3			0x010c

#define APB_BRIDGE_STS0			0x0180
#define APB_BRIDGE_STS1			0x0184
#define APB_BRIDGE_STS2			0x0188
#define APB_BRIDGE_STS3			0x018c
#define APB_BRIDGE_STS4			0x0190

/* Adapter registers */
#define IMP_CORECONFIG_CONFIG0		0x280030
#define UCIECTL_DEF_RP_EN		BIT(0)
#define UCIECTL_DEF_EP_EN		BIT(1)

#define IMP_SB_CONFIG0			0x282000
#define IMP_SB_CONFIG2			0x282008
#define IMP_SB_CONFIG4			0x282010

/* DVSEC_UNIT base: Addr[23:0] should be 0x000_0000 instead of 0x1000_0000 */
#define DVSEC_UCIE_LINK_CONTROL		0x000010
#define DVSEC_TARGET_LINK_SPEED		GENMASK(9, 6)
#define DVSEC_START_UCIE_LINK		BIT(10)
#define DVSEC_UCIE_LINK_STATUS		0x000014
#define DVSEC_LINK_STATUS		BIT(15)

/* PHY registers */
#define DW_VREF_VAR			0x3000dc
#define DW_VREF_VAR_MIN			GENMASK(7, 0)
#define DW_VREF_VAR_MAX			GENMASK(23, 16)
#define DW_MODULE_DEGRADE_STATUS	0x30011c
#define DW_MODULE_DISABLE_STATUS	GENMASK(15, 0)
#define DW_DCC_CTRL1			0x3002c0

#define Z_CAL_CTRL0			0x30101c
#define Z_CAL_CTRL1			0x301020
#define TX_ZCAL_P_OFFSET		GENMASK(10, 6)
#define TX_ZCAL_N_OFFSET		GENMASK(15, 11)

#define MM_MODE_CTRL			0x301004
#define FREQ_CHANGE_TYPE		BIT(4)

#define PLL_CTRL0			0x301044
#define PLL_CTRL1			0x301048
#define PLL_CTRL3			0x301050
#define PLL_CTRL4			0x301054

#define ACSM_WAIT_DLY0			0x302020
#define ACSM_WAIT_DLY0_FIELD		GENMASK(15, 0)
#define ACSM_WAIT_DLY1			0x302024
#define ACSM_WAIT_DLY1_FIELD		GENMASK(15, 0)
#define ACSM_LTSM_STATUS		0x302050
#define ACSM_LTSM_STATE			GENMASK(4, 0)

#define MM_TRK_CTRL			0x301100
#define MM_TRK_EN			BIT(0)

struct rcar_ucie {
	struct device *dev;
	void __iomem *base;

	struct dw_plat_pcie6 *dw_plat;
};

u32 rcar_ucie_mem_read32(struct rcar_ucie *ucie, u32 reg);
void rcar_ucie_mem_write32(struct rcar_ucie *ucie, u32 reg, u32 data);
u32 rcar_ucie_conf_read32(struct rcar_ucie *ucie, u32 reg);
void rcar_ucie_conf_write32(struct rcar_ucie *ucie, u32 reg, u32 data);
u32 rcar_ucie_phy_read32(struct rcar_ucie *ucie, u32 reg);
void rcar_ucie_phy_write32(struct rcar_ucie *ucie, u32 reg, u32 data);
void rcar_ucie_conf_modify32(struct rcar_ucie *ucie, u32 reg, u32 mask, u32 val);
void rcar_ucie_phy_modify32(struct rcar_ucie *ucie, u32 reg, u32 mask, u32 val);
int rcar_ucie_phy_reg_wait(struct rcar_ucie *ucie, u32 reg, u32 mask, u32 expected);
void rcar_ucie_controller_enable(struct rcar_ucie *ucie);
void rcar_ucie_phy_enable(struct rcar_ucie *ucie);
int rcar_ucie_link_up(struct dw_pcie6 *pcie);
void rcar_ucie_link_down(struct dw_pcie6 *pcie);
int rcar_ucie_is_link_up(struct dw_pcie6 *pcie);
int rcar_ucie_wait_for_link(struct dw_pcie6 *pcie);

extern const struct dw_pcie6_ops rcar_ucie_ops;

#endif /* _UCIE_RCAR_H_ */
