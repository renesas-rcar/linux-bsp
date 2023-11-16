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
#include <linux/sys_soc.h>


#include "pcie-rcar-gen4.h"
#include "pcie-rcar-gen4-phy-firmware.h"
#include "pcie-designware.h"


/* Renesas-specific */
#define PCIERSTCTRL1		0x0014
#define  APP_HOLD_PHY_RST	BIT(16)
#define  APP_LTSSM_ENABLE	BIT(0)

#define	PRTLGC5			0x0714
#define LANE_CONFIG_DUAL	BIT(6)

#define MISCIFPHYP0		0x00F8	/* 0x70F8 */
#define MISCIFPHYP1		0x02F8	/* 0x72F8 */
#define  PHYn_SRAM_BYPASS		BIT(16)
#define  PHYn_SRAM_EXT_LD_DONE	BIT(17)
#define  PHYn_SRAM_INIT_DONE	BIT(18)
#define RVCRCTRL2P0		0x0148	/* 0x7148 */
#define RVCRCTRL2P1		0x0348	/* 0x7348 */
#define PHY0OVRDEN8		0x01D4	/* 0x71D4 */
#define PHY1OVRDEN8		0x03D4	/* 0x73D4 */
#define OSCTSTCTRL5		0x0514	/* 0x7514 */
#define  R_PARA_SEL		BIT(26)
#define PCSCNT0			0x0700	/* 0x7700 */
#define REFCLKCTRLP1	0x02B8 /* 0x72B8 */
#define  PHY_REF_CLKDET_EN		BIT(10)
#define  PHY_REF_USE_PAD		BIT(2)

#define EXPCAP3F			0x007c /* 0x007c */
#define  CLKPM				BIT(18)
#define PRTLGC89			0x0B70
#define  PHY_VIEWPORT_PENDING	BIT(31)
#define  PHY_VIEWPORT_STATUS	BIT(30)
#define   PHY_VIEWPORT_TIMEOUT	BIT(30)
#define   PHY_VIEWPORT_NOERRORS	0
#define  PHY_VIEWPORT_BCWR		BIT(21)
#define  PHY_VIEWPORT_READ		BIT(20)
#define PRTLGC90			0x0B74
#define PRTLGC2				0x0708 /* 0x0708 */
#define  DO_DESKEW_FOR_SRIS	BIT(23)

#define	MAX_RETRIES		10
#define	PHY_UPDATE_MAX_RETRIES		100
#define SIZE_OF_ARRAY(array)    (sizeof(array)/sizeof(array[0]))

static inline int rcar_gen4_pcie_phy_viewport_wait(struct rcar_gen4_pcie *rcar)
{
	struct dw_pcie *dw = &rcar->dw;
	uint32_t data;
	int retries;

	for (retries = 0; retries < PHY_UPDATE_MAX_RETRIES; retries++)
	{
		data = dw_pcie_readl_dbi(dw, PRTLGC89);
		if ((data & PHY_VIEWPORT_PENDING) == 0x00000000)
		{
			break;
		}
		usleep_range(100, 110);
	}

	if (retries >= PHY_UPDATE_MAX_RETRIES) {
		dev_err(rcar->dw.dev, "Failed to wait phy viewport\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int rcar_gen4_pcie_phy_viewport_write(struct rcar_gen4_pcie *rcar, uint32_t addr, uint32_t wr_data)
{
	uint32_t data;
	struct dw_pcie *dw = &rcar->dw;

	dw_pcie_writel_dbi(dw, PRTLGC89, 0x00000000);
	rcar_gen4_pcie_phy_viewport_wait(rcar);
	dw_pcie_writel_dbi(dw, PRTLGC90, 0x00000000);
	rcar_gen4_pcie_phy_viewport_wait(rcar);
	dw_pcie_writel_dbi(dw, PRTLGC89, addr);
	rcar_gen4_pcie_phy_viewport_wait(rcar);
	dw_pcie_writel_dbi(dw, PRTLGC90, wr_data);

	data = dw_pcie_readl_dbi(dw, PRTLGC89);
	if((data & PHY_VIEWPORT_STATUS) == PHY_VIEWPORT_TIMEOUT)
	{
		dev_err(rcar->dw.dev, "Failed to write phy viewport @%04x=%04x\n",addr,wr_data);
		return -ETIMEDOUT;
	}

	return 0;
}

static int rcar_gen4_pcie_phy_viewport_read(struct rcar_gen4_pcie *rcar, uint32_t addr, uint32_t *rd_data){

	uint32_t data;
	struct dw_pcie *dw = &rcar->dw;

	dw_pcie_writel_dbi(dw, PRTLGC89, 0x00000000);
	rcar_gen4_pcie_phy_viewport_wait(rcar);
	dw_pcie_writel_dbi(dw, PRTLGC90, 0x00000000);
	rcar_gen4_pcie_phy_viewport_wait(rcar);
	dw_pcie_writel_dbi(dw, PRTLGC89, PHY_VIEWPORT_READ | addr);
	rcar_gen4_pcie_phy_viewport_wait(rcar);
	*rd_data = dw_pcie_readl_dbi(dw, PRTLGC90);

	data = dw_pcie_readl_dbi(dw, PRTLGC89);
	if((data & PHY_VIEWPORT_STATUS) == PHY_VIEWPORT_TIMEOUT)
	{
		dev_err(rcar->dw.dev, "Failed to read phy viewport\n");
		return -ETIMEDOUT;
	}

	return 0;
}


static int rcar_gen4_pcie_phy_viewport_ack_release(struct rcar_gen4_pcie *rcar, uint32_t addr)
{
	struct dw_pcie *dw = &rcar->dw;
	uint32_t data_prtlgc89;
	uint32_t data_prtlgc90;
	int retries;

	for (retries = 0; retries < PHY_UPDATE_MAX_RETRIES; retries++)
	{
		dw_pcie_writel_dbi(dw, PRTLGC89, PHY_VIEWPORT_READ | addr);
		rcar_gen4_pcie_phy_viewport_wait(rcar);
		data_prtlgc90 = dw_pcie_readl_dbi(dw, PRTLGC90);

		data_prtlgc89 = dw_pcie_readl_dbi(dw, PRTLGC89);
		if((data_prtlgc89 & PHY_VIEWPORT_STATUS) == PHY_VIEWPORT_TIMEOUT)
		{
			dev_err(rcar->dw.dev, "Failed to wait phy viewport ack release\n");
			return -ETIMEDOUT;
		}

		if((data_prtlgc90 & BIT(0)) == 0)
		{
			break;
		}

		usleep_range(1000, 1100);
	}

	if (retries >= PHY_UPDATE_MAX_RETRIES) {
		dev_err(rcar->dw.dev, "Failed to wait phy viewport ack release @0x%08x.\n", addr);
		return -ETIMEDOUT;
	}

	return 0;
}


static void rcar_gen4_pcie_fw_update(struct rcar_gen4_pcie *rcar)
{
	uint32_t phy_addr;
	uint32_t set_lane_bit;
	int i;
	struct dw_pcie *dw = &rcar->dw;

	if (dw->num_lanes == 4)
	{
		set_lane_bit = PHY_VIEWPORT_BCWR;
	}
	else
	{
		set_lane_bit = 0;
	}

	phy_addr = 0xC000 | set_lane_bit;
	for(i = 0;i < SIZE_OF_ARRAY(rcar_gen4_pcie_phy_firmware_data1); i++){
		rcar_gen4_pcie_phy_viewport_write(rcar, phy_addr, rcar_gen4_pcie_phy_firmware_data1[i]);
		phy_addr += 1;
	}

	phy_addr = 0xD000 | set_lane_bit;
	for(i = 0; i < SIZE_OF_ARRAY(rcar_gen4_pcie_phy_firmware_data2); i++){
		rcar_gen4_pcie_phy_viewport_write(rcar, phy_addr, rcar_gen4_pcie_phy_firmware_data2[i]);
		phy_addr += 1;
	}

	phy_addr = 0xE000 | set_lane_bit;
	for(i = 0; i < SIZE_OF_ARRAY(rcar_gen4_pcie_phy_firmware_data3); i++){
		rcar_gen4_pcie_phy_viewport_write(rcar, phy_addr, rcar_gen4_pcie_phy_firmware_data3[i]);
		phy_addr += 1;
	}

	phy_addr = 0xF000 | set_lane_bit;
	for(i = 0; i < SIZE_OF_ARRAY(rcar_gen4_pcie_phy_firmware_data4); i++){
		rcar_gen4_pcie_phy_viewport_write(rcar, phy_addr, rcar_gen4_pcie_phy_firmware_data4[i]);
		phy_addr += 1;
	}
}


static int rcar_gen4_pcie_wait_sram_ld_done(struct rcar_gen4_pcie *rcar)
{
	struct dw_pcie *dw = &rcar->dw;
	int ret;
	int i;
	const uint32_t addr[] = {0x1018, 0x1118, 0x1021, 0x1121};

	for (i = 0; i < SIZE_OF_ARRAY(addr); i++)
	{
		ret = rcar_gen4_pcie_phy_viewport_ack_release(rcar, addr[i]);
		if (ret)
		{
			return ret;
		}
	}

	if (dw->num_lanes == 4)
	{
		for (i = 0; i < SIZE_OF_ARRAY(addr); i++)
		{
			ret = rcar_gen4_pcie_phy_viewport_ack_release(rcar, addr[i] | BIT(16));
			if (ret)
			{
				return ret;
			}
		}
	}

	return 0;
}


static void inline rcar_gen4_pcie_reg_mask(void __iomem *addr, uint32_t offset, uint32_t mask, uint32_t val)
{
	u32 reg_val;

	reg_val = readl(addr + offset);
	reg_val &= ~mask;
	reg_val |= val;
	writel(reg_val, addr + offset);
}

static int rcar_gen4_pcie_linkup_wa(struct rcar_gen4_pcie *rcar)
{
	u32 val;
	int retries;
	struct device *dev = rcar->dw.dev;
	struct dw_pcie *dw = &rcar->dw;

	/* SRIS/SRNS(Separate Refclk) */
	val = dw_pcie_readl_dbi(dw, PRTLGC2);
	val |= DO_DESKEW_FOR_SRIS;
	dw_pcie_writel_dbi(dw, PRTLGC2, val);

	rcar_gen4_pcie_reg_mask(rcar->base,     PCIEMSR0, APP_SRIS_MODE, SRIS_MODE);

	rcar_gen4_pcie_reg_mask(rcar->phy_base, PCSCNT0, BIT(28), 0 << 28);
	rcar_gen4_pcie_reg_mask(rcar->phy_base, PCSCNT0, BIT(20), 0 << 20);
	rcar_gen4_pcie_reg_mask(rcar->phy_base, PCSCNT0, BIT(12), 0 << 12);
	rcar_gen4_pcie_reg_mask(rcar->phy_base, PCSCNT0, BIT(4),  0 << 4);

	/* Fuse initial value */
	rcar_gen4_pcie_reg_mask(rcar->phy_base, RVCRCTRL2P0,  BIT(6),  1 << 6 );
	rcar_gen4_pcie_reg_mask(rcar->phy_base, RVCRCTRL2P0,  BIT(22), 1 << 22 );
	rcar_gen4_pcie_reg_mask(rcar->phy_base, PHY0OVRDEN8,  BIT(15), 1 << 15 );

	rcar_gen4_pcie_reg_mask(rcar->phy_base, RVCRCTRL2P0,  GENMASK(1, 0),   3 << 0 );
	rcar_gen4_pcie_reg_mask(rcar->phy_base, RVCRCTRL2P0,  GENMASK(17, 16), 3 << 16 );
	rcar_gen4_pcie_reg_mask(rcar->phy_base, PHY0OVRDEN8,  BIT(16),         1 << 16 );

	if (dw->num_lanes == 4)
	{
		rcar_gen4_pcie_reg_mask(rcar->phy_base, RVCRCTRL2P1,  BIT(6), 1 << 6 );
		rcar_gen4_pcie_reg_mask(rcar->phy_base, RVCRCTRL2P1,  BIT(22), 1 << 22 );
		rcar_gen4_pcie_reg_mask(rcar->phy_base, PHY1OVRDEN8,  BIT(15), 1 << 15 );

		rcar_gen4_pcie_reg_mask(rcar->phy_base, RVCRCTRL2P1,  GENMASK(1, 0), 3 << 0 );
		rcar_gen4_pcie_reg_mask(rcar->phy_base, RVCRCTRL2P1,  GENMASK(17, 16), 3 << 16 );
		rcar_gen4_pcie_reg_mask(rcar->phy_base, PHY1OVRDEN8,  BIT(16), 1 << 16 );
	}

	/* PHY FW update */
	rcar_gen4_pcie_reg_mask(rcar->phy_base, OSCTSTCTRL5,  R_PARA_SEL, R_PARA_SEL);

	rcar_gen4_pcie_reg_mask(rcar->phy_base, MISCIFPHYP0, PHYn_SRAM_BYPASS, 0 );
	rcar_gen4_pcie_reg_mask(rcar->phy_base, MISCIFPHYP0, BIT(19),          1 << 19 );
	if (dw->num_lanes == 4)
	{
		rcar_gen4_pcie_reg_mask(rcar->phy_base, MISCIFPHYP1, PHYn_SRAM_BYPASS, 0);
		rcar_gen4_pcie_reg_mask(rcar->phy_base, MISCIFPHYP1, BIT(19),          1 << 19 );
	}

	rcar_gen4_pcie_reg_mask(rcar->base, PCIERSTCTRL1, APP_HOLD_PHY_RST, 0);

	for (retries = 0; retries < PHY_UPDATE_MAX_RETRIES; retries++) {
		val = readl(rcar->phy_base + MISCIFPHYP0);
		if((val & PHYn_SRAM_INIT_DONE) == PHYn_SRAM_INIT_DONE)
			break;

		usleep_range(100, 110);
	}
	if (retries >= PHY_UPDATE_MAX_RETRIES) {
		dev_err(dev, "sram_init_done error.\n");
		return -ETIMEDOUT;
	}

	if (dw->num_lanes == 4)
	{
		for (retries = 0; retries < PHY_UPDATE_MAX_RETRIES; retries++) {
			val = readl(rcar->phy_base + MISCIFPHYP1);
			if((val & PHYn_SRAM_INIT_DONE) == PHYn_SRAM_INIT_DONE)
				break;

			usleep_range(100, 110);
		}
		if (retries >= PHY_UPDATE_MAX_RETRIES) {
			dev_err(dev, "sram_init_done error.\n");
			return -ETIMEDOUT;
		}
	}

	rcar_gen4_pcie_fw_update(rcar);

	rcar_gen4_pcie_reg_mask(rcar->phy_base, MISCIFPHYP0, PHYn_SRAM_EXT_LD_DONE, PHYn_SRAM_EXT_LD_DONE);
	if (dw->num_lanes == 4)
	{
		rcar_gen4_pcie_reg_mask(rcar->phy_base, MISCIFPHYP1, PHYn_SRAM_EXT_LD_DONE, PHYn_SRAM_EXT_LD_DONE);
	}

	rcar_gen4_pcie_wait_sram_ld_done(rcar);

	{
		uint32_t data;
		/* check FW version */
		rcar_gen4_pcie_phy_viewport_read(rcar,0x2058					,&data);
		dev_info(dev, "FW version :0x%04x\n", data);
		rcar_gen4_pcie_phy_viewport_read(rcar,0x2059					,&data);
		dev_info(dev, "FW version :0x%04x\n", data);
	}
	rcar_gen4_pcie_reg_mask(rcar->base, PCIERSTCTRL1, APP_LTSSM_ENABLE, APP_LTSSM_ENABLE);

	return 0;
}

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
	int ret = 0;

	if (rcar->linkup_setting)
	{
		ret = rcar_gen4_pcie_linkup_wa(rcar);
	}
	else
	{
		rcar_gen4_pcie_ltssm_enable(rcar, true);
	}

	return ret;
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

void rcar_gen4_pcie_set_max_link_width(struct dw_pcie *dw, int num_lanes)
{
	u32 val = dw_pcie_readl_dbi(dw, EXPCAP(PCI_EXP_LNKCAP));

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
		dev_info(dw->dev, "Invalid num-lanes %d\n", num_lanes);
		val |= PCI_EXP_LNKCAP_MLW_X1;
		break;
	}
	dw_pcie_writel_dbi(dw, EXPCAP(PCI_EXP_LNKCAP), val);
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
			dev_err(dev, "Failed to get Clock name.\n");
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
	val |= PHY_REF_CLKDET_EN | PHY_REF_REPEAT_CLK_EN;
	writel(val, rcar->phy_base + REFCLKCTRLP0);

	val = readl(rcar->phy_base + REFCLKCTRLP1);
	val &= ~PHY_REF_USE_PAD; /* bit2 is default 0. */
	writel(val, rcar->phy_base + REFCLKCTRLP1);
	val |= PHY_REF_REPEAT_CLK_EN | PHY_REF_CLKDET_EN;
	writel(val, rcar->phy_base + REFCLKCTRLP1);
}

static const struct soc_device_attribute r8a779g0[] = {
	{ .soc_id = "r8a779g0" },
	{ /* sentinel */ }
};

void rcar_gen4_pcie_initial(struct rcar_gen4_pcie *rcar, bool rc)
{
	struct dw_pcie *dw = &rcar->dw;
	u32 val;

	if (soc_device_match(r8a779g0))
	{
		rcar->linkup_setting = true;
	}

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

		val = dw_pcie_readl_dbi(dw, EXPCAP3F);
		val |= CLKPM;
		dw_pcie_writel_dbi(dw, EXPCAP3F, val);

		/* Enable LTR */
		val = readl(rcar->base + PCIELTRMSGCTRL1);
		val |= LTR_EN;
		writel(val, rcar->base + PCIELTRMSGCTRL1);
	}
}
