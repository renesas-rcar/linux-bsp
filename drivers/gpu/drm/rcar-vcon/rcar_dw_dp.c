// SPDX-License-Identifier: GPL-2.0-only
/*
 * rcar_dw_dp.c  --  R-Car Designware Display port driver
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/iopoll.h>

#include <drm/bridge/dw_dp.h>

#include "rcar_dw_dp_fw_1.h"
#include "rcar_dw_dp_fw_2.h"
#include "rcar_dw_dp_fw_3.h"
#include "rcar_dw_dp_fw_4.h"
#include "rcar_dw_dp_fw_5.h"

#define DPTX_DPMODE                             0x0000
#define PHY_CLK_SEL                             BIT(0)

#define DPTX_CLKGEN_RST_CNT                     0x0100
#define DPTX_CLKGEN_RST_CNT_DEFAULT             GENMASK(14, 0)

#define DPTX_PIPE_LANE0_M2P_MESSAGEBUS          0x0460
#define DPTX_PIPE_LANE1_M2P_MESSAGEBUS          0x0464
#define DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START    0x0468
#define DPTX_PIPE_LANEX_P2M_MESSAGEBUS          0x046c
#define PIPE_LANE0_P2M_MESSAGEBUS_CMD_WR	BIT(8)
#define PIPE_LANE1_P2M_MESSAGEBUS_CMD_WR	BIT(24)

#define DPTX_CLKGEN_DIV_AUXCLK                  0x0104
#define DIV_AUXCLK_DEFAULT                      (0x31)
#define DPTX_CLKGEN_DIV_FWCLK                   0x010c
#define DIV_FWCLK_DEFAULT                       (0x07)
#define DPTX_CLKGEN_DIV_PXLCLK                  0x0110
#define DIV_PXLCLK_DEFAULT                      (0x1f)

#define DPTX_PHY_AUX                            0x0400
#define PHY_AUX_MASK                            0x020b
#define PHY_AUX_DEFAULT                         0x020b

#define DPTX_PHY_PAD                            0x0408
#define PHY_PAD_MASK                            0x0004
#define PHY_PAD_DEFAULT                         0x0004

#define DPTX_PHY_PIPE0                          0x0410
#define PHY_PIPE0_MASK                          0x0202
#define PHY_PIPE0_DEFAULT                       0x0202
#define PHY_PIPE2_PIPE_LANE0_MAXPCLKACK		GENMASK(3, 2)

#define DPTX_PHY_PIPE1                          0x0414
#define PHY_PIPE1_MASK                          0x0034
#define PHY_PIPE1_DEFAULT                       0x0034

#define DPTX_PHY_PIPE2                          0x0418
#define PHY_PIPE2_PIPE_LANE1_MAXPCLKACK		GENMASK(3, 2)

#define DPTX_PHY_PIPE3                          0x041c
#define PHY_PIPE3_MASK                          0x0034
#define PHY_PIPE3_DEFAULT                       0x0034

#define DPTX_PHY_PIPE5                          0x0424
#define PHY_PIPE5_MASK                          0x0002
#define PHY_PIPE5_DEFAULT                       0x0002

#define DPTX_PHY_PIPE6                          0x0428
#define PHY_PIPE6_MASK                          0x0002
#define PHY_PIPE6_DEFAULT                       0x0002

#define DPTX_PHY_RX                             0x0438
#define PHY_RX_MASK                             0x0003
#define PHY_RX_DEFAULT                          0x0003

#define DPTX_PHY_CNTMON                         0x0440
#define PHY_CNTMON_MASK                         GENMASK(12, 9)
#define PHY_CNTMON_DEFAULT                      (BIT(9) | BIT(10))
#define PHY_CNTMON_PHY0_SRAM_INIT_DONE		BIT(23)

#define DPTX_DPCTRL_ENCRYPTION_MODE             0x70040
#define ENCRYPTION_MODE                         BIT(0)
#define ENCRYPTION_DIS                          BIT(0)

#define RCAR_DW_DP_TIMEOUT_US			10000000

struct rcar_dw_dp {
	struct device *dev;
	struct dw_dp *dp;
	struct phy *phy;
	void __iomem *phy_addr;
	void __iomem *fw_addr;
};

static void rcar_dw_dp_phy_write(struct rcar_dw_dp *dw_dp, u32 reg, u32 data)
{
	iowrite32(data, dw_dp->phy_addr + reg);
}

static u32 rcar_dw_dp_phy_read(struct rcar_dw_dp *dw_dp, u32 reg)
{
	return ioread32(dw_dp->phy_addr + reg);
}

static void rcar_dw_dp_phy_modify(struct rcar_dw_dp *dw_dp, u32 reg, u32 clear, u32 set)
{
	rcar_dw_dp_phy_write(dw_dp, reg, (rcar_dw_dp_phy_read(dw_dp, reg) & ~clear) | set);
}

static int rcar_dw_dp_phy_reg_wait(struct rcar_dw_dp *dw_dp, u32 offs, u32 mask, u32 expected)
{
	u32 val;

	return readl_poll_timeout_atomic(dw_dp->phy_addr + offs, val, (val & mask) == expected,
					 1, RCAR_DW_DP_TIMEOUT_US);
}

static int rcar_dw_dp_phy_write_fw(struct rcar_dw_dp *dw_dp, const u32 *array, u32 size, u32 offset)
{
	int i;

	for (i = 0; i < size; i++) {
		iowrite32(array[i], dw_dp->fw_addr + offset);
		offset += 4;
	}

	return 0;
}

static int rcar_dw_dp_phy_load_fw(struct rcar_dw_dp *dw_dp)
{
	struct platform_device *pdev = to_platform_device(dw_dp->dev);
	struct resource *res;
	u32 offset, size;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fw");
	if (!res)
		return -EINVAL;

	dw_dp->fw_addr = devm_ioremap_resource(dw_dp->dev, res);
	if (IS_ERR(dw_dp->fw_addr))
		return PTR_ERR(dw_dp->fw_addr);

	offset = 0;
	size = ARRAY_SIZE(rcar_dw_dp_fw_data_1);
	ret = rcar_dw_dp_phy_write_fw(dw_dp, rcar_dw_dp_fw_data_1, size, offset);
	if (ret)
		return ret;

	offset = 0x4000;
	size = ARRAY_SIZE(rcar_dw_dp_fw_data_2);
	ret = rcar_dw_dp_phy_write_fw(dw_dp, rcar_dw_dp_fw_data_2, size, offset);
	if (ret)
		return ret;

	offset = 0x8000;
	size = ARRAY_SIZE(rcar_dw_dp_fw_data_3);
	ret = rcar_dw_dp_phy_write_fw(dw_dp, rcar_dw_dp_fw_data_3, size, offset);
	if (ret)
		return ret;

	offset = 0xc000;
	size = ARRAY_SIZE(rcar_dw_dp_fw_data_4);
	ret = rcar_dw_dp_phy_write_fw(dw_dp, rcar_dw_dp_fw_data_4, size, offset);
	if (ret)
		return ret;

	offset = 0x10000;
	size = ARRAY_SIZE(rcar_dw_dp_fw_data_5);
	ret = rcar_dw_dp_phy_write_fw(dw_dp, rcar_dw_dp_fw_data_5, size, offset);
	if (ret)
		return ret;

	return 0;
}

static int rcar_dw_dp_phy_init(struct phy *p)
{
	struct rcar_dw_dp *dw_dp = phy_get_drvdata(p);

	/* De-assert DPTX module internal reset */
	rcar_dw_dp_phy_write(dw_dp, DPTX_CLKGEN_RST_CNT, DPTX_CLKGEN_RST_CNT_DEFAULT);
	/* Config corresponding clock controlling registers */
	rcar_dw_dp_phy_write(dw_dp, DPTX_CLKGEN_DIV_AUXCLK, DIV_AUXCLK_DEFAULT);
	rcar_dw_dp_phy_write(dw_dp, DPTX_CLKGEN_DIV_FWCLK, DIV_FWCLK_DEFAULT);
	rcar_dw_dp_phy_write(dw_dp, DPTX_CLKGEN_DIV_PXLCLK, DIV_PXLCLK_DEFAULT);

	return 0;
}

static int rcar_dw_dp_phy_post_init(struct phy *p)
{
	struct rcar_dw_dp *dw_dp = phy_get_drvdata(p);

	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_PIPE0, PHY_PIPE0_MASK, PHY_PIPE0_DEFAULT);
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_AUX, PHY_AUX_MASK, PHY_AUX_DEFAULT);
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_PAD, PHY_PAD_MASK, PHY_PAD_DEFAULT);
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_PIPE1, PHY_PIPE1_MASK, PHY_PIPE1_DEFAULT);
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_PIPE3, PHY_PIPE3_MASK, PHY_PIPE3_DEFAULT);
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_PIPE5, PHY_PIPE5_MASK, PHY_PIPE5_DEFAULT);
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_PIPE6, PHY_PIPE6_MASK, PHY_PIPE6_DEFAULT);
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_RX, PHY_RX_MASK, PHY_RX_DEFAULT);
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_CNTMON, PHY_CNTMON_MASK, 0);

	rcar_dw_dp_phy_modify(dw_dp, DPTX_DPCTRL_ENCRYPTION_MODE, ENCRYPTION_MODE, ENCRYPTION_DIS);

	return 0;
}

static int rcar_dw_dp_phy_post_init_1(struct phy *p)
{
	struct rcar_dw_dp *dw_dp = phy_get_drvdata(p);

	rcar_dw_dp_phy_modify(dw_dp, DPTX_DPMODE, PHY_CLK_SEL, PHY_CLK_SEL);
	usleep_range(100, 101);

	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_CNTMON, GENMASK(12, 11), 0);
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_CNTMON, GENMASK(10, 9), GENMASK(10, 9));

	rcar_dw_dp_phy_modify(dw_dp, DPTX_DPMODE, BIT(0), BIT(0));

	return 0;
}

static int rcar_dw_dp_phy_post_init_2(struct phy *p)
{
	struct rcar_dw_dp *dw_dp = phy_get_drvdata(p);
	int ret;

	ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PHY_CNTMON, PHY_CNTMON_PHY0_SRAM_INIT_DONE,
				      PHY_CNTMON_PHY0_SRAM_INIT_DONE);
	if (ret)
		return ret;

	ret = rcar_dw_dp_phy_load_fw(dw_dp);
	if (ret)
		return ret;

	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_CNTMON, BIT(13), BIT(13));

	usleep_range(100, 101);

	return 0;
}

static int rcar_dw_dp_phy_post_init_3(struct phy *p)
{
	struct rcar_dw_dp *dw_dp = phy_get_drvdata(p);
	u32 val, mask;
	int ret;

	rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE0_M2P_MESSAGEBUS, 0x000224);
	rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE1_M2P_MESSAGEBUS, 0x000224);

	val = rcar_dw_dp_phy_read(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START);
	val |= 0x00000101;
	rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START, val);

	mask = PIPE_LANE0_P2M_MESSAGEBUS_CMD_WR | PIPE_LANE1_P2M_MESSAGEBUS_CMD_WR;
	ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PIPE_LANEX_P2M_MESSAGEBUS, mask, mask);
	if (ret)
		return ret;

	rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE0_M2P_MESSAGEBUS, 0x000226);
	rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE1_M2P_MESSAGEBUS, 0x000226);

	val = rcar_dw_dp_phy_read(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START);
	val |= 0x00000101;
	rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START, val);

	mask = PIPE_LANE0_P2M_MESSAGEBUS_CMD_WR | PIPE_LANE1_P2M_MESSAGEBUS_CMD_WR;
	ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PIPE_LANEX_P2M_MESSAGEBUS, mask, mask);
	if (ret)
		return ret;

	val = rcar_dw_dp_phy_read(dw_dp, DPTX_PHY_PIPE0);
	rcar_dw_dp_phy_write(dw_dp, DPTX_PHY_PIPE0, val | 0x303);

	return 0;
}

static int rcar_dw_dp_phy_post_init_4(struct phy *p)
{
	struct rcar_dw_dp *dw_dp = phy_get_drvdata(p);
	int ret;

	ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PHY_PIPE0, PHY_PIPE2_PIPE_LANE0_MAXPCLKACK,
				      PHY_PIPE2_PIPE_LANE1_MAXPCLKACK);
	if (ret)
		return ret;

	ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PHY_PIPE2, PHY_PIPE2_PIPE_LANE1_MAXPCLKACK,
				      PHY_PIPE2_PIPE_LANE1_MAXPCLKACK);
	if (ret)
		return ret;

	/* FIXME: Hardcoded for 8-bit per component,
	 * it should be set when configuring VIDEO_CONFIG
	 */
	rcar_dw_dp_phy_write(dw_dp, 0x031c, 0x05);

	return 0;
}

static int rcar_dw_dp_phy_configure(struct phy *p, union phy_configure_opts *phy_cfg)
{
	struct rcar_dw_dp *dw_dp = phy_get_drvdata(p);
	int i, j;
	u32 messagebus_write_data[4][2]; /* [lane][0:deemph, 1:margin] */
	u32 deemph_addr_0n3 = 0x602;
	u32 deemph_addr_1n2 = 0x402;
	u32 margin_addr_0n3 = 0x608;
	u32 margin_addr_1n2 = 0x408;
	int lanes = phy_cfg->dp.lanes;
	u32 start_data, chk_data;
	u32 val;
	int ret;

	for (i = 0; i < lanes; i++) {
		if (i == 0 || i == 3) {
			messagebus_write_data[i][0] = ((deemph_addr_0n3 & GENMASK(11, 8)) >> 8) |
				((deemph_addr_0n3 & GENMASK(7, 0)) << 8);
			messagebus_write_data[i][1] = ((margin_addr_0n3 & GENMASK(11, 8)) >> 8) |
				((margin_addr_0n3 & GENMASK(7, 0)) << 8);
		} else {
			messagebus_write_data[i][0] = ((deemph_addr_1n2 & GENMASK(11, 8)) >> 8) |
				((deemph_addr_1n2 & GENMASK(7, 0)) << 8);
			messagebus_write_data[i][1] = ((margin_addr_1n2 & GENMASK(11, 8)) >> 8) |
				((margin_addr_1n2 & GENMASK(7, 0)) << 8);
		}
	}

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 2; j++)
			messagebus_write_data[i][j] |= (0x2 << 4);
	}

	/* Set deemphasis and margin values from PHY configuration */
	for (i = 0; i < lanes; i++) {
		messagebus_write_data[i][0] |= (phy_cfg->dp.pre[i] << 16);
		messagebus_write_data[i][1] |= (phy_cfg->dp.voltage[i] << 16);
	}

	/* Determine start and check patterns */
	if (lanes == 4) {
		start_data = 0x00000101;
		chk_data   = 0x01000100;
	} else {
		start_data = 0x00000100;
		chk_data   = 0x01000000;
	}

	/* Transmit lane 0/2 deemphasis */
	rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE1_M2P_MESSAGEBUS,
			     messagebus_write_data[0][0]);
	if (lanes == 4)
		rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE0_M2P_MESSAGEBUS,
				     messagebus_write_data[2][0]);

	val = rcar_dw_dp_phy_read(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START);
	val |= start_data;
	rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START, start_data);
	ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PIPE_LANEX_P2M_MESSAGEBUS, chk_data, chk_data);
	if (ret)
		return ret;

	/* Transmit lane 1/3 deemphasis */
	if (lanes >= 2) {
		rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE1_M2P_MESSAGEBUS,
				     messagebus_write_data[1][0]);
		if (lanes == 4)
			rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE0_M2P_MESSAGEBUS,
					     messagebus_write_data[3][0]);

		val = rcar_dw_dp_phy_read(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START);
		val |= start_data;
		rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START, start_data);
		ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PIPE_LANEX_P2M_MESSAGEBUS,
					      chk_data, chk_data);
		if (ret)
			return ret;
	}

	/* Transmit lane 0/2 margin */
	rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE1_M2P_MESSAGEBUS,
			     messagebus_write_data[0][1]);
	if (lanes == 4)
		rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE0_M2P_MESSAGEBUS,
				     messagebus_write_data[2][1]);

	val = rcar_dw_dp_phy_read(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START);
	val |= start_data;
	rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START, start_data);
	ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PIPE_LANEX_P2M_MESSAGEBUS, chk_data, chk_data);
	if (ret)
		return ret;

	/* Transmit lane 1/3 margin */
	if (lanes >= 2) {
		rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE1_M2P_MESSAGEBUS,
				     messagebus_write_data[1][1]);
		if (lanes == 4)
			rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE0_M2P_MESSAGEBUS,
					     messagebus_write_data[3][1]);

		val = rcar_dw_dp_phy_read(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START);
		val |= start_data;
		rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START, start_data);
		ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PIPE_LANEX_P2M_MESSAGEBUS,
					      chk_data, chk_data);
		if (ret)
			return ret;
	}

	return 0;
}

static int rcar_dw_dp_phy_exit(struct phy *p)
{
	return 0;
}

static int rcar_dw_dp_phy_power_on(struct phy *p)
{
	return 0;
}

static int rcar_dw_dp_phy_power_off(struct phy *p)
{
	return 0;
}

static const struct phy_ops rcar_dw_dp_phy_ops = {
	.init           = rcar_dw_dp_phy_init,
	.post_init      = rcar_dw_dp_phy_post_init,
	.post_init_1    = rcar_dw_dp_phy_post_init_1,
	.post_init_2    = rcar_dw_dp_phy_post_init_2,
	.post_init_3    = rcar_dw_dp_phy_post_init_3,
	.post_init_4    = rcar_dw_dp_phy_post_init_4,
	.exit           = rcar_dw_dp_phy_exit,
	.power_on       = rcar_dw_dp_phy_power_on,
	.power_off      = rcar_dw_dp_phy_power_off,
	.configure	= rcar_dw_dp_phy_configure,
};

static int rcar_dw_dp_probe(struct platform_device *pdev)
{
	struct rcar_dw_dp *dw_dp;
	struct device *dev = &pdev->dev;
	struct dw_dp_plat_data plat_data;
	struct resource *res;

	dw_dp = devm_kzalloc(&pdev->dev, sizeof(*dw_dp), GFP_KERNEL);
	if (!dw_dp)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");
	if (!res)
		return -EINVAL;

	dw_dp->phy_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dw_dp->phy_addr))
		return PTR_ERR(dw_dp->phy_addr);

	dw_dp->dev = dev;
	platform_set_drvdata(pdev, dw_dp);

	dw_dp->phy = devm_phy_create(dev, NULL, &rcar_dw_dp_phy_ops);
	if (IS_ERR(dw_dp->phy))
		return PTR_ERR(dw_dp->phy);

	phy_set_drvdata(dw_dp->phy, dw_dp);
	dw_dp->phy->attrs.max_link_rate = 810000;
	dw_dp->phy->attrs.bus_width = 4;

	plat_data.max_link_rate = 810000;
	dw_dp->dp = dw_dp_bind(dev, NULL, dw_dp->phy, &plat_data);
	if (IS_ERR(dw_dp->dp))
		return PTR_ERR(dw_dp->dp);

	return 0;
}

static int rcar_dw_dp_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id rcar_dw_dp_of_table[] = {
	{
		.compatible = "renesas,r8a78000-dw-dp",
	},
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, rcar_dw_dp_of_table);

static struct platform_driver rcar_dw_dp_platform_driver = {
	.probe          = rcar_dw_dp_probe,
	.remove         = rcar_dw_dp_remove,
	.driver         = {
		.name   = "rcar-dw-dp",
		.of_match_table = rcar_dw_dp_of_table,
	},
};

module_platform_driver(rcar_dw_dp_platform_driver);

MODULE_DESCRIPTION("Renesas R-Car DesignWare Display port Driver");
MODULE_LICENSE("GPL");
