// SPDX-License-Identifier: GPL-2.0-only
/*
 * UCIe host/endpoint controller driver for Renesas R-Car Gen5 Series SoCs
 * Copyright (C) 2026 Renesas Electronics Corporation
 */
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/minmax.h>
#include <linux/string.h>
#include <linux/sys_soc.h>

#include "ucie-rcar.h"

void rcar_ucie_apb_write(struct rcar_ucie *ucie, u32 reg, u32 val);

#define UCIE_CORE_CLK_ID	"ucie1"

int rcar_ucie_clk_get(struct rcar_ucie *ucie)
{
	int num_clks, i;

	num_clks = devm_clk_bulk_get_all(ucie->dev, &ucie->clks);
	if (num_clks < 0)
		return dev_err_probe(ucie->dev, num_clks,
				     "Failed to get clocks\n");
	if (!num_clks)
		return dev_err_probe(ucie->dev, -ENOENT, "No clocks specified\n");

	for (i = 0; i < num_clks; i++)
		if (ucie->clks[i].id &&
		    !strcmp(ucie->clks[i].id, UCIE_CORE_CLK_ID))
			break;

	if (i == num_clks)
		return dev_err_probe(ucie->dev, -ENOENT,
				     "Missing \"%s\" clock\n",
				     UCIE_CORE_CLK_ID);

	swap(ucie->clks[i], ucie->clks[num_clks - 1]);
	ucie->num_clks = num_clks;

	return 0;
}

int rcar_ucie_reset_get(struct rcar_ucie *ucie)
{
	ucie->rsts = devm_reset_control_array_get_exclusive(ucie->dev);

	return PTR_ERR_OR_ZERO(ucie->rsts);
}

int rcar_ucie_clk_init(struct rcar_ucie *ucie)
{
	int num_peri_clks = ucie->num_clks - 1;
	int ret;

	ret = clk_bulk_prepare_enable(num_peri_clks, ucie->clks);
	if (ret)
		return ret;

	rcar_ucie_apb_write(ucie, UCIEPWRMNGCTRL, APP_READY_ENTR_L23);

	ret = clk_bulk_prepare_enable(1, &ucie->clks[num_peri_clks]);
	if (ret)
		goto err_disable_peri;

	ret = reset_control_reset(ucie->rsts);
	if (ret) {
		dev_err(ucie->dev, "Failed to reset: %d\n", ret);
		goto err_disable_core;
	}

	return 0;

err_disable_core:
	clk_bulk_disable_unprepare(1, &ucie->clks[num_peri_clks]);

err_disable_peri:
	clk_bulk_disable_unprepare(num_peri_clks, ucie->clks);

	return ret;
}

int rcar_ucie_power_up(struct rcar_ucie *ucie)
{
	struct device *dev = ucie->dev;
	int ret;

	if (!dev->pm_domain) {
		ret = devm_pm_domain_attach_list(dev, NULL, &ucie->pd_list);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Failed to attach PM domains\n");
	}

	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err;

	ret = pm_runtime_put_sync_suspend(dev);
	if (ret < 0)
		goto err;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err;

	return 0;

err:
	dev_err(dev, "Failed to power up: %d\n", ret);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return ret;
}

void rcar_ucie_power_down(struct rcar_ucie *ucie)
{
	pm_runtime_put_sync(ucie->dev);
	pm_runtime_disable(ucie->dev);
}

void rcar_ucie_clk_deinit(struct rcar_ucie *ucie)
{
	clk_bulk_disable_unprepare(ucie->num_clks, ucie->clks);
}

static int rcar_ucie_get_max_link_speed(struct device_node *node)
{
	u32 max_link_speed;

	if (of_property_read_u32(node, "max-link-speed", &max_link_speed) ||
	    max_link_speed == 0 || max_link_speed > 7)
		return -EINVAL;

	return max_link_speed;
}

int rcar_ucie_get_resources(struct rcar_ucie *ucie, struct platform_device *pdev)
{
	struct dw_pcie6 *pci = ucie->dw_plat->pci;
	struct device_node *np = dev_of_node(&pdev->dev);
	int ret;

	ucie->apb_base = devm_platform_ioremap_resource_byname(pdev, "apb");
	if (IS_ERR(ucie->apb_base))
		return PTR_ERR(ucie->apb_base);

	ucie->axi_base = devm_platform_ioremap_resource_byname(pdev, "axi");
	if (IS_ERR(ucie->axi_base))
		return PTR_ERR(ucie->axi_base);

	pci->max_link_speed = rcar_ucie_get_max_link_speed(np);

	of_property_read_u32(np, "num-lanes", &pci->num_lanes);

	ret = of_property_read_u32(np, "channel-id", &ucie->ch);
	if (ret)
		return ret;

	if (of_property_read_u32(np, "renesas,ucie-link-speed", &ucie->link_speed) ||
	    ucie->link_speed > UCIE_LINK_SPEED_16GT)
		ucie->link_speed = UCIE_LINK_SPEED_16GT;

	ret = rcar_ucie_clk_get(ucie);
	if (ret)
		return ret;

	return rcar_ucie_reset_get(ucie);
}

u32 rcar_ucie_apb_read(struct rcar_ucie *ucie, u32 reg)
{
	return ioread32(ucie->apb_base + reg);
}

void rcar_ucie_apb_write(struct rcar_ucie *ucie, u32 reg, u32 val)
{
	iowrite32(val, ucie->apb_base + reg);
}

u32 rcar_ucie_axi_read(struct rcar_ucie *ucie, u32 reg)
{
	u32 val, upper, lower;

	upper = UCIE_DBI_ADDR_UPPER(reg);
	lower = UCIE_DBI_ADDR_LOWER(reg);

	rcar_ucie_apb_write(ucie, UCIEDBIADR, UCIEDBIADR_DBI_ARADDR(upper));

	val = ioread32(ucie->axi_base + lower);

	rcar_ucie_apb_write(ucie, UCIEDBIADR, 0);

	return val;
}

void rcar_ucie_axi_write(struct rcar_ucie *ucie, u32 reg, u32 val)
{
	u32 upper, lower;

	upper = UCIE_DBI_ADDR_UPPER(reg);
	lower = UCIE_DBI_ADDR_LOWER(reg);

	rcar_ucie_apb_write(ucie, UCIEDBIADR, UCIEDBIADR_DBI_AWADDR(upper));

	iowrite32(val, ucie->axi_base + lower);

	rcar_ucie_apb_write(ucie, UCIEDBIADR, 0);
}

static void rcar_ucie_axi_modify(struct rcar_ucie *ucie, u32 reg, u32 clear, u32 set)
{
	rcar_ucie_axi_write(ucie, reg, (rcar_ucie_axi_read(ucie, reg) & ~clear) | set);
}

static void rcar_ucie_set_link_width(struct rcar_ucie *ucie)
{
	u32 num_lanes = ucie->dw_plat->pci->num_lanes;
	u32 mode;

	if (!num_lanes)
		return;

	switch (num_lanes) {
	case 1:
		mode = PORT_LINK_MODE_1_LANES;
		break;
	case 2:
		mode = PORT_LINK_MODE_2_LANES;
		break;
	case 4:
		mode = PORT_LINK_MODE_4_LANES;
		break;
	case 8:
		mode = PORT_LINK_MODE_8_LANES;
		break;
	default:
		dev_err(ucie->dev, "num-lanes %u: invalid value\n", num_lanes);
		return;
	}

	rcar_ucie_axi_modify(ucie, PCIE_PORT_LINK_CONTROL,
			     PORT_LINK_FAST_LINK_MODE | PORT_LINK_MODE_MASK, mode);
	rcar_ucie_axi_modify(ucie, PCIE_LINK_WIDTH_SPEED_CONTROL,
			     PORT_LOGIC_LINK_WIDTH_MASK, PORT_LOGIC_LINK_WIDTH_1_LANES);
}

int rcar_ucie_hw_init(struct rcar_ucie *ucie, bool rc_mode)
{
	struct dw_pcie6 *pci = ucie->dw_plat->pci;

	if (rc_mode) {
		rcar_ucie_apb_write(ucie, UCIEFMIS, APP_SEGMENT_ID_RC);
		rcar_ucie_apb_write(ucie, UCIEMSR0, DEVICE_TYPE_RP);
		rcar_ucie_apb_write(ucie, UCIEPCR00, IRP_EN);
	} else {
		rcar_ucie_apb_write(ucie, UCIEFMIS, APP_SEGMENT_ID_EP);
		rcar_ucie_apb_write(ucie, UCIEMSR0, DEVICE_TYPE_EP);
		rcar_ucie_apb_write(ucie, UCIEPCR00, IEP_EN);
	}

	rcar_ucie_apb_write(ucie, UCIECSR00, CTL_CXLMODE_CXL);
	rcar_ucie_apb_write(ucie, UCIEICR27, INTREQ13_FREQ_CHANGE_REQ_EN);

	rcar_ucie_axi_modify(ucie, LINK_CONTROL2_LINK_STATUS2_REG,
			     PCIE_CAP_TARGET_LINK_SPEED, pci->max_link_speed);

	rcar_ucie_set_link_width(ucie);

	rcar_ucie_axi_modify(ucie, ADV_ERR_CAP_CTRL_OFF, ECRC_CHECK_EN | ECRC_GEN_EN,
			     ECRC_CHECK_EN | ECRC_GEN_EN);

	/* Update R-Car UCIe Vendor ID, Device ID */
	rcar_ucie_axi_write(ucie, TYPE1_DEV_ID_VEND_ID_REG,
			    DEVICE_ID(RCAR_UCIE_DEVICE_ID) | VENDOR_ID(RCAR_UCIE_VENDOR_ID));

	rcar_ucie_axi_write(ucie, PL32G_CONTROL_REG, MOD_TS_USAGE_MODE_SELECT_ALT);
	rcar_ucie_axi_write(ucie, CXL_VLSM_CSR_REG_OFF, CXL_VLSM_PM_ENABLE);

	if (rc_mode)
		rcar_ucie_axi_modify(ucie, PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG,
				     PCIE_DEV_PORT_TYPE, ROOT_PORT_PCIE_RC);

	rcar_ucie_axi_modify(ucie, MISC_CONTROL_1_OFF, OPTIONAL_OHC_CTRL, ADD_OHC_C);

	rcar_ucie_axi_write(ucie, MEMBAR0_RAS_UNCOR_ERROR_MASK_REG_OFF,
			    MEMBAR0_RAS_UNCOR_ERROR_MASK_DEFAULT);
	rcar_ucie_axi_write(ucie, MEMBAR0_RAS_CORR_ERROR_MASK_REG_OFF,
			    MEMBAR0_RAS_CORR_ERROR_MASK_DEFAULT);

	rcar_ucie_axi_modify(ucie, IMP_SB_CONFIG3, FDI_CPL_CR_EN, FDI_CPL_CR_DISABLE);

	if (rc_mode)
		rcar_ucie_axi_modify(ucie, IMP_SB_CONFIG4, DP | RETRY | STREAMING,
				     DP_ENABLE | RETRY_DISABLE | STREAMING_DISABLE);
	else
		rcar_ucie_axi_modify(ucie, IMP_SB_CONFIG4, UP | RETRY | STREAMING,
				     UP_ENABLE | RETRY_DISABLE | STREAMING_DISABLE);

	rcar_ucie_axi_write(ucie, DVSEC_UCIE_LINK_CONTROL,
			    DVSEC_STD256_EH_FLIT_FORMAT_EN | DVSEC_68B_FLIT_FORMAT_DISABLE);

	rcar_ucie_axi_write(ucie, CXL_DVSEC_FLEX_CTL_STATUS,
			    CXL_DVSEC_CXL_68B_FLIT_VH_EN | CXL_DVSEC_MEM_EN |
			    CXL_DVSEC_IO_EN | CXL_DVSEC_CACHE_EN);

	rcar_ucie_axi_write(ucie, IMP_MB_CONFIG11,
			    MAX_UNACK_FLITS_256B_DEF | MAX_UNACK_FLITS_68B_DEF);

	rcar_ucie_axi_write(ucie, MMPL_MMTRKCTRL, MMRXTRK_EN);
	rcar_ucie_axi_write(ucie, MMPL_MODULEDEGRADESTATUS, MODULE_DISABLE_STATUS_DEF);
	rcar_ucie_axi_write(ucie, MMPL_ZCALCTRL0,
			    ZCAL_OFFSET_SAMPLE_TIME_DEF | ZCAL_SAMPLE_TIME_DEF |
			    ZCAL_COMP_STARTUP_TIME_DEF);
	rcar_ucie_axi_write(ucie, MMPL_ZCALCTRL1, TX_ZCAL_N_OFFSET_DEF | TX_ZCAL_P_OFFSET_DEF);
	rcar_ucie_axi_write(ucie, MMPL_PLLCTRL0_P0, PLLCTRL0_P0_DEF);
	rcar_ucie_axi_write(ucie, MMPL_PLLCTRL1_P0, PLLCTRL1_P0_DEF);
	rcar_ucie_axi_write(ucie, MMPL_PLLCTRL3, PLLCTRL3_DEF);
	rcar_ucie_axi_write(ucie, MMPL_PLLCTRL4, PLLCTRL4_DEF);
	rcar_ucie_axi_write(ucie, MMPL_MMMODECTRL, MMMODECTRL_DEF);

	rcar_ucie_axi_write(ucie, DWORD_0_DWMODECTRL0, DWORD_0_DWMODECTRL0_DEF);
	rcar_ucie_axi_write(ucie, DWORD_0_DWMODULEDEGRADESTATUS, DWORD_0_DWMODULEDEGRADESTATUS_DEF);
	rcar_ucie_axi_write(ucie, DWORD_0_DWVREFVAR, DWORD_0_DWVREFVAR_DEF);
	rcar_ucie_axi_write(ucie, DWORD_0_DWTXZCALSB, DWORD_0_DWTXZCALSB_DEF);

	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(0), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(1), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(2), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(3), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(4), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(5), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(6), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(7), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(8), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(9), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(10), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(11), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(12), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(13), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(14), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(15), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(16), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(17), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(18), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_0_DWPERBITCTRL(19), 0x00440000);

	rcar_ucie_axi_write(ucie, DWORD_0_DWMISCCTRL0, 0x3D00A001);
	rcar_ucie_axi_write(ucie, DWORD_0_DWMISCCTRL1, 0x01240800);

	rcar_ucie_axi_write(ucie, DWORD_1_DWMODECTRL0, 0x0600000C);
	rcar_ucie_axi_write(ucie, DWORD_1_DWMODULEDEGRADESTATUS, 0x0000FFFC);
	rcar_ucie_axi_write(ucie, DWORD_1_DWVREFVAR, 0x007F0001);
	rcar_ucie_axi_write(ucie, DWORD_1_DWTXZCALSB, 0x00001B1D);

	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(0), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(1), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(2), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(3), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(4), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(5), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(6), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(7), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(8), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(9), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(10), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(11), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(12), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(13), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(14), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(15), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(16), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(17), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(18), 0x00440000);
	rcar_ucie_axi_write(ucie, DWORD_1_DWPERBITCTRL(19), 0x00440000);

	rcar_ucie_axi_write(ucie, DWORD_1_DWMISCCTRL0, 0x3D00A001);
	rcar_ucie_axi_write(ucie, DWORD_1_DWMISCCTRL1, 0x01240800);

	rcar_ucie_axi_write(ucie, ACSMIM_ACSMINSTRREG95, 0x000FFF53);
	rcar_ucie_axi_write(ucie, ACSMIM_ACSMINSTRREG96, 0x000FFFB3);
	rcar_ucie_axi_write(ucie, ACSMIM_ACSMINSTRREG97, 0x00080023);
	rcar_ucie_axi_write(ucie, ACSMIM_ACSMINSTRREG98, 0x0000B00B);
	rcar_ucie_axi_write(ucie, ACSMIM_ACSMINSTRREG99, 0x03970B6A);

	rcar_ucie_axi_modify(ucie, ACSMLTSMINDEX0VAR14, ~ACSMLTSMINDEX0VAR14_MASK, 0x1 << 20);

	rcar_ucie_axi_modify(ucie, ACSMLTSMINDEX0VAR16, ~ACSMLTSMINDEX0VAR16_MASK, 0x1 << 20);

	rcar_ucie_axi_modify(ucie, ACSMTRAINVAR0I0, ~ACSMTRAINVAR0I0_MASK, 0x1 << 16);

	rcar_ucie_axi_modify(ucie, ACSMTRAINVAR0I1, ~ACSMTRAINVAR0I1_MASK, 0x1 << 16);

	rcar_ucie_axi_modify(ucie, ACSMTRAINVAR0I2, ~ACSMTRAINVAR0I2_MASK, 0x1 << 16);

	rcar_ucie_axi_modify(ucie, ACSMTRAINVAR1I1, ~ACSMTRAINVAR1I1_MASK | 0x100, 0);

	rcar_ucie_axi_write(ucie, MEMBAR0_RAS_CAP_D8_REG_OFF, 0x0000000);

	rcar_ucie_axi_modify(ucie, DWORD_0_DWRXLATCTRL,
			     ~DWORD_0_DWRXLATCTRL_MASK |
			     (DWORD_0_DWRXLATCTRL_DWRXVLDMARGIN_MASK <<
			      DWORD_0_DWRXLATCTRL_DWRXVLDMARGIN_SHIFT),
			     (0x1 & DWORD_0_DWRXLATCTRL_DWRXVLDMARGIN_MASK) <<
			     DWORD_0_DWRXLATCTRL_DWRXVLDMARGIN_SHIFT);

	rcar_ucie_axi_modify(ucie, DWORD_1_DWRXLATCTRL,
			     ~DWORD_1_DWRXLATCTRL_MASK |
			     (DWORD_1_DWRXLATCTRL_DWRXVLDMARGIN_MASK <<
			      DWORD_1_DWRXLATCTRL_DWRXVLDMARGIN_SHIFT),
			     (0x1 & DWORD_1_DWRXLATCTRL_DWRXVLDMARGIN_MASK) <<
			     DWORD_1_DWRXLATCTRL_DWRXVLDMARGIN_SHIFT);

	rcar_ucie_axi_write(ucie, UCIE_TRAININGSETUP2_LWR, 0x00800000);
	rcar_ucie_axi_write(ucie, UCIE_TRAININGSETUP2_UPR, 0x00800000);

	rcar_ucie_axi_write(ucie, UCIE_TRAININGSETUP3_LWR, 0x00010000);
	rcar_ucie_axi_write(ucie, UCIE_TRAININGSETUP3_UPR, 0x00010000);

	rcar_ucie_axi_write(ucie, DWORD_0_DWMISCCTRL0, 0x3D00A001);

	rcar_ucie_axi_modify(ucie, DWORD_1_DWMISCCTRL0,
			     ~DWORD_1_DWMISCCTRL0_MASK |
			     (DWORD_1_DWMISCCTRL0_DWTXCKPARKLEVEL_MASK <<
			      DWORD_1_DWMISCCTRL0_DWTXCKPARKLEVEL_SHIFT),
			     (0x1 & DWORD_1_DWMISCCTRL0_DWTXCKPARKLEVEL_MASK) <<
			     DWORD_1_DWMISCCTRL0_DWTXCKPARKLEVEL_SHIFT);

	rcar_ucie_axi_modify(ucie, DWORD_0_DWMODECTRL0,
			     ~DWORD_0_DWMODECTRL0_MASK |
			     (DWORD_0_DWMODECTRL0_DWRXCTLCLKSEL_MASK <<
			      DWORD_0_DWMODECTRL0_DWRXCTLCLKSEL_SHIFT) |
			     (DWORD_0_DWMODECTRL0_DWRXLATALIGN_MASK <<
			      DWORD_0_DWMODECTRL0_DWRXLATALIGN_SHIFT),
			     ((0x1 & DWORD_0_DWMODECTRL0_DWRXCTLCLKSEL_MASK) <<
			      DWORD_0_DWMODECTRL0_DWRXCTLCLKSEL_SHIFT) |
			     ((0x1 & DWORD_0_DWMODECTRL0_DWRXLATALIGN_MASK) <<
			      DWORD_0_DWMODECTRL0_DWRXLATALIGN_SHIFT));

	rcar_ucie_axi_modify(ucie, DWORD_1_DWMODECTRL0,
			     ~DWORD_1_DWMODECTRL0_MASK |
			     (DWORD_1_DWMODECTRL0_DWRXCTLCLKSEL_MASK <<
			      DWORD_1_DWMODECTRL0_DWRXCTLCLKSEL_SHIFT) |
			     (DWORD_1_DWMODECTRL0_DWRXLATALIGN_MASK <<
			      DWORD_1_DWMODECTRL0_DWRXLATALIGN_SHIFT),
			     ((0x1 & DWORD_1_DWMODECTRL0_DWRXCTLCLKSEL_MASK) <<
			      DWORD_1_DWMODECTRL0_DWRXCTLCLKSEL_SHIFT) |
			     ((0x1 & DWORD_1_DWMODECTRL0_DWRXLATALIGN_MASK) <<
			      DWORD_1_DWMODECTRL0_DWRXLATALIGN_SHIFT));

	rcar_ucie_axi_modify(ucie, ACSMLTSMINDEX0VAR14,
			     ~ACSMLTSMINDEX0VAR14_MASK |
			     (ACSMLTSMINDEX0VAR14_ACSMLTSMINDEX0VAR14_MASK <<
			      ACSMLTSMINDEX0VAR14_ACSMLTSMINDEX0VAR14_SHIFT),
			     (0x14EFEF & ACSMLTSMINDEX0VAR14_ACSMLTSMINDEX0VAR14_MASK) <<
			     ACSMLTSMINDEX0VAR14_ACSMLTSMINDEX0VAR14_SHIFT);

	rcar_ucie_axi_modify(ucie, ACSM1_ACSMLTSMINDEX0VAR14,
			     ~ACSMLTSMINDEX0VAR14_MASK |
			     (ACSMLTSMINDEX0VAR14_ACSMLTSMINDEX0VAR14_MASK <<
			      ACSMLTSMINDEX0VAR14_ACSMLTSMINDEX0VAR14_SHIFT),
			     (0x14EFEF & ACSMLTSMINDEX0VAR14_ACSMLTSMINDEX0VAR14_MASK) <<
			     ACSMLTSMINDEX0VAR14_ACSMLTSMINDEX0VAR14_SHIFT);

	rcar_ucie_axi_modify(ucie, ACSMLTSMINDEX0VAR16,
			     ~ACSMLTSMINDEX0VAR16_MASK |
			     (ACSMLTSMINDEX0VAR16_ACSMLTSMINDEX0VAR16_MASK <<
			      ACSMLTSMINDEX0VAR16_ACSMLTSMINDEX0VAR16_SHIFT),
			     (0x10F7F1 & ACSMLTSMINDEX0VAR16_ACSMLTSMINDEX0VAR16_MASK) <<
			     ACSMLTSMINDEX0VAR16_ACSMLTSMINDEX0VAR16_SHIFT);

	rcar_ucie_axi_modify(ucie, ACSM1_ACSMLTSMINDEX0VAR16,
			     ~ACSMLTSMINDEX0VAR16_MASK |
			     (ACSMLTSMINDEX0VAR16_ACSMLTSMINDEX0VAR16_MASK <<
			      ACSMLTSMINDEX0VAR16_ACSMLTSMINDEX0VAR16_SHIFT),
			     (0x10F7F1 & ACSMLTSMINDEX0VAR16_ACSMLTSMINDEX0VAR16_MASK) <<
			     ACSMLTSMINDEX0VAR16_ACSMLTSMINDEX0VAR16_SHIFT);

	rcar_ucie_axi_write(ucie, ACSMLOOPVAR1, 0x0001600);

	rcar_ucie_axi_write(ucie, ACSMTIMEOUTCTRL1, 0);

	rcar_ucie_axi_write(ucie, ACSMINSTRREG(5), 0x03910133);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(6), 0x022403A3);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(7), 0x03910E33);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(8), 0x022404A3);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(9), 0x02210143);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(10), 0x02200000);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(11), 0x83100011);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(12), 0x0110C007);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(13), 0x02711003);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(14), 0xA3100801);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(15), 0xA3100011);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(16), 0xA3100021);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(17), 0x63970181);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(18), 0xA3100031);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(19), 0xA3100041);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(20), 0xA3100851);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(21), 0x22712003);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(22), 0x000122C9);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(23), 0x000C4125);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(24), 0x83045062);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(25), 0x000100C6);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(26), 0x83200091);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(27), 0x02500000);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(28), 0x83100041);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(29), 0x02714003);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(30), 0x02210243);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(31), 0x00042EC9);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(32), 0x000300A6);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(33), 0x02210443);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(34), 0x8300D0C1);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(35), 0x832000D1);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(36), 0x830001D1);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(37), 0x83102071);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(38), 0x00034125);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(39), 0x000F7155);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(40), 0x00021006);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(68), 0x00080423);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(69), 0x0004F853);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(70), 0x00010FB3);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(71), 0x03973C6A);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(72), 0x834090F1);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(73), 0x80006081);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(74), 0x80000281);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(75), 0x83209081);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(76), 0x83000281);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(77), 0x80004081);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(78), 0x80006481);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(79), 0x8000A181);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(80), 0x83000001);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(81), 0x0397606A);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(82), 0x83200001);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(83), 0x83000101);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(84), 0x83005181);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(85), 0x80004381);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(86), 0x000F5135);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(87), 0x00016145);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(88), 0x03979B6A);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(89), 0x03978D6A);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(91), 0x0397406A);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(92), 0x01180873);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(93), 0x02780863);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(94), 0x01180073);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(95), 0x000FFF53);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(96), 0x000FFFB3);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(97), 0x00080023);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(98), 0x0000B00B);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(99), 0x03970B6A);
	rcar_ucie_axi_write(ucie, ACSMINSTRREG(100), 0x00020173);

	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(0), 0x1f9);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(1), 0x2623001);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(2), 0x5);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(3), 0x1e1);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(4), 0x3727003);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(5), 0x3727003);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(6), 0x2733005);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(7), 0x36fb005);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(8), 0xde673e05);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(9), 0xde673e05);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(10), 0xb);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(11), 0x1e3);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(12), 0xc0000607);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(13), 0x32673805);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(14), 0xde673e05);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(15), 0x32673805);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(16), 0xde673e05);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(17), 0x5);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(18), 0x32673805);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(19), 0x26fb007);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(20), 0x1603005);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(21), 0xcec203);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(22), 0xc208985);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(23), 0x1020605);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(24), 0x4030005);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(25), 0xf8100673);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(26), 0xf8100673);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(27), 0x8800c277);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(28), 0x8800c275);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(29), 0x8800c277);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR(30), 0x7300000d);

	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(0), 0x140);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(1), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(2), 0x100);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(3), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(4), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(5), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(6), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(7), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(8), 0x123);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(9), 0x123);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(10), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(11), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(12), 0x123);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(13), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(14), 0x123);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(15), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(16), 0x123);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(17), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(18), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(19), 0x1dc);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(20), 0x120);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(21), 0x4);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(22), 0x18);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(23), 0x4);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(24), 0x8);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(25), 0x3);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(26), 0x3);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(27), 0xb);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(28), 0xb);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(29), 0xb);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR(30), 0x4);

	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR_ALT(21), 0xcc800b);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR_ALT(22), 0xc208985);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR_ALT(23), 0x100210d);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR_ALT(24), 0x4020005);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR_ALT(25), 0xf820a173);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR_ALT(26), 0xf820a173);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR_ALT(27), 0x88008077);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR_ALT(28), 0x88008075);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR_ALT(29), 0x88008077);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK0VAR_ALT(30), 0x7300000d);

	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR_ALT(21), 0x4);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR_ALT(22), 0x18);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR_ALT(23), 0x4);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR_ALT(24), 0x8);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR_ALT(25), 0x3);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR_ALT(26), 0x3);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR_ALT(27), 0xb);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR_ALT(28), 0xb);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR_ALT(29), 0xb);
	rcar_ucie_axi_write(ucie, ACSMLTSMMSK1VAR_ALT(30), 0x4);

	rcar_ucie_axi_write(ucie, MMPL_CSRADDR2, 0x304004);
	rcar_ucie_axi_write(ucie, MMPL_CSRADDR5, 0x304018);
	rcar_ucie_axi_write(ucie, MMPL_CSRADDR10, 0x3042BC);
	rcar_ucie_axi_write(ucie, MMPL_CSRADDR11, 0x304020);

	rcar_ucie_axi_modify(ucie, ACSMSEQ0CTRL,
			     ~ACSMSEQ0CTRL_MASK |
			     (ACSMSEQ0CTRL_ACSMSEQ0STOPADDR_MASK <<
			      ACSMSEQ0CTRL_ACSMSEQ0STOPADDR_SHIFT),
			     (0x28 & ACSMSEQ0CTRL_ACSMSEQ0STOPADDR_MASK) <<
			     ACSMSEQ0CTRL_ACSMSEQ0STOPADDR_SHIFT);

	rcar_ucie_axi_modify(ucie, ACSMSEQ1CTRL,
			     ~ACSMSEQ1CTRL_MASK |
			     (ACSMSEQ1CTRL_ACSMSEQ1STOPADDR_MASK <<
			      ACSMSEQ1CTRL_ACSMSEQ1STOPADDR_SHIFT),
			     (0x64 & ACSMSEQ1CTRL_ACSMSEQ1STOPADDR_MASK) <<
			     ACSMSEQ1CTRL_ACSMSEQ1STOPADDR_SHIFT);

	rcar_ucie_axi_modify(ucie, ACSMCTRL,
			     ~ACSMCTRL_MASK |
			     (ACSMCTRL_ACSMSTOPADDR_MASK << ACSMCTRL_ACSMSTOPADDR_SHIFT),
			     (0x28 & ACSMCTRL_ACSMSTOPADDR_MASK) << ACSMCTRL_ACSMSTOPADDR_SHIFT);

	/* Enable HDMA interrupt */
	rcar_ucie_apb_write(ucie, UCIEICR14, 0xffffffff);
	rcar_ucie_apb_write(ucie, UCIEICR15, 0xffffffff);

	return 0;
}

void rcar_ucie_start_link_up(struct rcar_ucie *ucie, bool rc_mode)
{
	u32 val = DVSEC_STD256_EH_FLIT_FORMAT_EN |
		  FIELD_PREP(DVSEC_UCIE_LINK_SPEED, ucie->link_speed);

	rcar_ucie_axi_write(ucie, DVSEC_UCIE_LINK_CONTROL, val);

	if (rc_mode)
		rcar_ucie_axi_write(ucie, DVSEC_UCIE_LINK_CONTROL,
				    val | DVSEC_UCIE_RC_MODE_EN);

	rcar_ucie_apb_write(ucie, UCIEPCR01, 0x01);
}

int rcar_ucie_is_link_up(struct dw_pcie6 *pcie)
{
	struct rcar_ucie *ucie = dev_get_drvdata(pcie->dev);
	u32 status;

	status = rcar_ucie_axi_read(ucie, DVSEC_UCIE_LINK_STATUS);

	return FIELD_GET(DVSEC_LINK_STATUS, status);
}

void rcar_ucie_report_link(struct dw_pcie6 *pcie)
{
	static const u16 ucie_link_gt[] = { 4, 8, 12, 16, 24, 32, 48, 64 };
	static const u16 ucie_link_width[] = { 0, 8, 16, 32, 64, 128, 256 };
	struct rcar_ucie *ucie = dev_get_drvdata(pcie->dev);
	u32 status, speed, width;
	int link_up;

	status = rcar_ucie_axi_read(ucie, DVSEC_UCIE_LINK_STATUS);
	link_up = FIELD_GET(DVSEC_LINK_STATUS, status);
	speed = FIELD_GET(DVSEC_LINK_SPEED_ENABLED, status);
	width = FIELD_GET(DVSEC_LINK_WIDTH_ENABLED, status);

	if (link_up && width > 0)
		dev_info(pcie->dev, "UCIe link up: %u GT/s x %u\n",
			 ucie_link_gt[speed], ucie_link_width[width]);
	else if (link_up)
		dev_info(pcie->dev, "UCIe link up: speed_idx=%u width_idx=%u\n",
			 speed, width);
	else
		dev_info(pcie->dev, "UCIe link down\n");
}

const struct dw_pcie6_ops rcar_ucie_ops = {
	.link_up        = rcar_ucie_is_link_up,
	.link_report    = rcar_ucie_report_link,
};
