// SPDX-License-Identifier: GPL-2.0-only
/*
 * UCIe host/endpoint controller driver for Renesas R-Car Gen5 Series SoCs
 * Copyright (C) 2024 Renesas Electronics Corporation
 */

#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/sys_soc.h>

#include "ucie-rcar.h"

static bool rcar_ucie_calc_even_parity(u64 data)
{
	int i;

	for (i = 32; i > 0; i /= 2)
		data ^= data >> i;

	return (data & 1);
}

static int rcar_ucie_reg_read32(struct rcar_ucie *ucie, bool is_phy,
				bool is_mem, u32 reg, u32 *data)
{
	u32 phase0, phase1;
	u64 val;
	int ret;

	phase0 = is_mem ? OPCODE_MEM_READ32 : OPCODE_CONF_READ32;
	phase0 |= BYTE_ENABLES_32 | SRCID_PROTO_STACK0_ACCESS;

	iowrite32(phase0, ucie->base + APB_BRIDGE_CTL0);

	phase1 = is_phy ? DSTID_PHY_STACK_ACCESS : DSTID_PROTO_STACK_ACCESS;
	phase1 |= reg;

	val = ((u64)phase1 << 32) | phase0;
	phase1 |= CONTROL_PARITY(rcar_ucie_calc_even_parity(val));

	iowrite32(phase1, ucie->base + APB_BRIDGE_CTL1);

	ret = readl_poll_timeout(ucie->base + APB_BRIDGE_STS1, phase1,
				 (phase1 & COMPL_STATUS) == COMPL_SUCCESS, 1000, 1000000);

	*data = ioread32(ucie->base + APB_BRIDGE_STS2);

	return ret;
}

static void rcar_ucie_reg_write32(struct rcar_ucie *ucie, bool is_phy,
				  bool is_mem, u32 reg, u32 data)
{
	u32 phase0, phase1;
	u64 val;

	phase0 = is_mem ? OPCODE_MEM_WRITE32 : OPCODE_CONF_WRITE32;
	phase0 |= BYTE_ENABLES_32 | SRCID_PROTO_STACK0_ACCESS;

	iowrite32(phase0, ucie->base + APB_BRIDGE_CTL0);

	phase1 = is_phy ? DSTID_PHY_STACK_ACCESS : DSTID_PROTO_STACK_ACCESS;
	phase1 |= reg;

	val = ((u64)phase1 << 32) | phase0;
	phase1 |= CONTROL_PARITY(rcar_ucie_calc_even_parity(val));
	phase1 |= DATA_PARITY(rcar_ucie_calc_even_parity(data));

	iowrite32(phase1, ucie->base + APB_BRIDGE_CTL1);

	iowrite32(data, ucie->base + APB_BRIDGE_CTL2);
	iowrite32(0, ucie->base + APB_BRIDGE_CTL3);
}

u32 rcar_ucie_mem_read32(struct rcar_ucie *ucie, u32 reg)
{
	u32 val;

	rcar_ucie_reg_read32(ucie, false, true, reg, &val);

	return val;
}

void rcar_ucie_mem_write32(struct rcar_ucie *ucie, u32 reg, u32 data)
{
	rcar_ucie_reg_write32(ucie, false, true, reg, data);
}

u32 rcar_ucie_conf_read32(struct rcar_ucie *ucie, u32 reg)
{
	u32 val;

	rcar_ucie_reg_read32(ucie, false, false, reg, &val);

	return val;
}

void rcar_ucie_conf_write32(struct rcar_ucie *ucie, u32 reg, u32 data)
{
	rcar_ucie_reg_write32(ucie, false, false, reg, data);
}

u32 rcar_ucie_phy_read32(struct rcar_ucie *ucie, u32 reg)
{
	u32 val;

	rcar_ucie_reg_read32(ucie, true, true, reg, &val);

	return val;
}

void rcar_ucie_phy_write32(struct rcar_ucie *ucie, u32 reg, u32 data)
{
	rcar_ucie_reg_write32(ucie, true, true, reg, data);
}

void rcar_ucie_conf_modify32(struct rcar_ucie *ucie, u32 reg, u32 mask, u32 val)
{
	rcar_ucie_conf_write32(ucie, reg, (rcar_ucie_conf_read32(ucie, reg) & ~mask) | val);
}

void rcar_ucie_phy_modify32(struct rcar_ucie *ucie, u32 reg, u32 mask, u32 val)
{
	rcar_ucie_phy_write32(ucie, reg, (rcar_ucie_phy_read32(ucie, reg) & ~mask) | val);
}

int rcar_ucie_phy_reg_wait(struct rcar_ucie *ucie, u32 reg, u32 mask, u32 expected)
{
	int i;

	for (i = 0; i < 100; i++) {
		if ((rcar_ucie_phy_read32(ucie, reg) & mask) == expected)
			return 0;
		mdelay(10);
	}

	return -ETIMEDOUT;
}

void rcar_ucie_controller_enable(struct rcar_ucie *ucie)
{
	rcar_ucie_mem_write32(ucie, IMP_SB_CONFIG0, 0xa0190);
	rcar_ucie_mem_write32(ucie, IMP_SB_CONFIG2, 0xa0190);
	rcar_ucie_mem_write32(ucie, IMP_SB_CONFIG4, 0x91);
	rcar_ucie_conf_write32(ucie, DVSEC_UCIE_LINK_CONTROL, 0x01);
}

void rcar_ucie_phy_enable(struct rcar_ucie *ucie)
{
	rcar_ucie_phy_modify32(ucie, MM_TRK_CTRL, MM_TRK_EN, 0);
	rcar_ucie_phy_modify32(ucie, DW_MODULE_DEGRADE_STATUS, DW_MODULE_DISABLE_STATUS, 0xfffc);
	rcar_ucie_phy_modify32(ucie, ACSM_WAIT_DLY0, ACSM_WAIT_DLY0_FIELD, 0x03e8);
	rcar_ucie_phy_modify32(ucie, ACSM_WAIT_DLY1, ACSM_WAIT_DLY1_FIELD, 0x251c);
	rcar_ucie_phy_write32(ucie, Z_CAL_CTRL0, 0x320fa3e8);
	rcar_ucie_phy_modify32(ucie, Z_CAL_CTRL1, TX_ZCAL_N_OFFSET, 0x13 << 11);
	rcar_ucie_phy_modify32(ucie, Z_CAL_CTRL1, TX_ZCAL_P_OFFSET, 0x13 << 6);
	rcar_ucie_phy_write32(ucie, PLL_CTRL0, 0x4 | (0x6 << 8) | (0x7c << 24));
	rcar_ucie_phy_write32(ucie, PLL_CTRL1, 0x29a | (0x3 << 10) | (0 << 13));
	rcar_ucie_phy_write32(ucie, PLL_CTRL3, 0x56439);
	rcar_ucie_phy_write32(ucie, PLL_CTRL4, 0x4200330);
	rcar_ucie_phy_modify32(ucie, MM_MODE_CTRL, FREQ_CHANGE_TYPE, 0x1 << 4);
	rcar_ucie_phy_write32(ucie, DW_DCC_CTRL1, 0x32 | (0x15 << 8) | (0x14 << 16));
	rcar_ucie_phy_modify32(ucie, DW_MODULE_DEGRADE_STATUS, DW_MODULE_DISABLE_STATUS, 0xfffc);
	rcar_ucie_phy_modify32(ucie, DW_VREF_VAR, DW_VREF_VAR_MAX, 0x80 << 16);
	rcar_ucie_phy_modify32(ucie, DW_VREF_VAR, DW_VREF_VAR_MIN, 0x7e);
}

int rcar_ucie_link_up(struct dw_pcie6 *pcie)
{
	struct rcar_ucie *ucie = dev_get_drvdata(pcie->dev);

	if (ucie->vdk_bypass)
		return 0;

	rcar_ucie_conf_modify32(ucie, DVSEC_UCIE_LINK_CONTROL, DVSEC_TARGET_LINK_SPEED, 0x1 << 6);
	rcar_ucie_conf_modify32(ucie, DVSEC_UCIE_LINK_CONTROL, DVSEC_START_UCIE_LINK, 0x1 << 10);

	/* FIXME: Return error if timedout */
	rcar_ucie_phy_reg_wait(ucie, ACSM_LTSM_STATUS, ACSM_LTSM_STATE, 0x16);

	/* TODO: Re-initialization to Negotiated Data Rate */

	return 0;
}

void rcar_ucie_link_down(struct dw_pcie6 *pcie)
{
	struct rcar_ucie *ucie = dev_get_drvdata(pcie->dev);

	rcar_ucie_conf_modify32(ucie, DVSEC_UCIE_LINK_CONTROL, DVSEC_START_UCIE_LINK, 0);
}

int rcar_ucie_is_link_up(struct dw_pcie6 *pcie)
{
	struct rcar_ucie *ucie = dev_get_drvdata(pcie->dev);
	u32 val;

	if (ucie->vdk_bypass)
		return 1;

	val = rcar_ucie_phy_read32(ucie, DVSEC_UCIE_LINK_STATUS);

	return val & DVSEC_LINK_STATUS;
}

int rcar_ucie_wait_for_link(struct dw_pcie6 *pcie)
{
	int i;

	for (i = 0; i < LINK_WAIT_MAX_RETRIES; i++) {
		if (rcar_ucie_is_link_up(pcie)) {
			dev_info(pcie->dev, "Link up\n");

			return 0;
		}
		usleep_range(LINK_WAIT_USLEEP_MIN, LINK_WAIT_USLEEP_MAX);
	}

	dev_info(pcie->dev, "Phy link never came up\n");

	return -ETIMEDOUT;
}

const struct dw_pcie6_ops rcar_ucie_ops = {
	.start_link     = rcar_ucie_link_up,
	.stop_link	= rcar_ucie_link_down,
	.link_up        = rcar_ucie_is_link_up,
};
