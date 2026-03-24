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
#include <linux/pm_domain.h>
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
#define DPMODE_PHY_CLK_SEL_I                    BIT(0)

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

#define DPTX_PIXELIF_IPI_FORMAT			0x0318
#define DPTX_PIXELIF_IPI_FORMAT_MASK(s)		(0x1f << ((s) * 0x05))
#define DPTX_PIXELIF_IPI_FORMAT_REG(s, v)	((v) << ((s) * 0x05))

#define DPTX_PIXELIF_IPI_COLOR			0x031c
#define DPTX_PIXELIF_IPI_COLOR_DEPTH_MASK(s)	(0x1f << ((s) * 0x05))
#define DPTX_PIXELIF_IPI_COLOR_DEPTH_REG(s, v)	((v) << ((s) * 0x05))

#define DPTX_PHY_AUX                            0x0400
#define PHY_AUX_MASK                            0x020b
#define PHY_AUX_DEFAULT                         0x020b

#define DPTX_PHY_PAD                            0x0408
#define PHY_PAD_MASK                            0x0004
#define PHY_PAD_DEFAULT                         0x0004

#define DPTX_PHY_PIPE0                          0x0410
#define PHY_PIPE0_MASK                          0x0202
#define PHY_PIPE0_DEFAULT                       0x0202
#define PHY_PIPE0_PIPE_LANE1_MAXPCLKREQ		GENMASK(9, 8)
#define PHY_PIPE0_PIPE_LANE0_MAXPCLKACK		GENMASK(3, 2)
#define PHY_PIPE0_PIPE_LANE0_MAXPCLKREQ		GENMASK(1, 0)

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
#define PHY_CNTMON_PHY0_SRAM_INIT_DONE          BIT(23)
#define PHY_CNTMON_PHY0_SRAM_EXT_LD_DONE	BIT(13)
#define PHY_CNTMON_PHY0_SRAM_BYPASS		GENMASK(12, 11)
#define PHY_CNTMON_PHY0_SRAM_BOOTLOAD_BYPASS	GENMASK(10, 9)

#define DPTX_DPCTRL_ENCRYPTION_MODE             0x70040
#define ENCRYPTION_MODE                         BIT(0)
#define ENCRYPTION_DIS                          BIT(0)

#define	PIPE_MSGBUS_LANE0			0
#define PIPE_MSGBUS_LANE1			1
#define PIPE_MSGBUS_1ST_DATA(addr)		(0x20 | (((addr) >> 8) & 0x0f))
#define PIPE_MSGBUS_2ND_DATA(addr)		((addr) & 0xff)
#define PIPE_MSGBUS_3RD_DATA(data)		((data) & 0xff)
#define PIPE_MSGBUS_START_LANE0			BIT(0)
#define PIPE_MSGBUS_START_LANE1			BIT(8)
#define PIPE_MSGBUS_WR_ACK_LANE0		BIT(8)
#define PIPE_MSGBUS_WR_ACK_LANE1		BIT(24)

#define PCS_PHY_TX_CONTROL2			0x0402
#define PCS_PHY_TX_CONTROL8			0x0408
#define PCS_PHY_HDP_TX_CONTROL2			0x0602
#define PCS_PHY_HDP_TX_CONTROL8			0x0608

#define RCAR_DW_DP_TIMEOUT_US			10000000

enum {
	IPI_RGB		= 0,
	IPI_YCBCR422	= 0x1,
	IPI_YCBCR444	= 0x2,
};

enum {
	IPI_6BPC	= 0x3,
	IPI_8BPC	= 0x5,
	IPI_10BPC	= 0x6,
	IPI_12BPC	= 0x7,
	IPI_16BPC	= 0x9,
	IPI_48BPC	= 0xe,
};

struct rcar_dw_dp {
	struct device *dev;
	struct dw_dp *dp;
	struct phy *phy;
	void __iomem *phy_addr;
	void __iomem *fw_addr;
	int num_pd;
	struct device **pd_dev;
	struct clk *clk;
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
	u32 offset, size;
	int ret;

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

static int rcar_dw_dp_msg_bus_write(struct rcar_dw_dp *dw_dp, unsigned int lane,
				    u16 addr, u8 data)
{
	u32 msg, start_bit, ack_bit;

	if (lane != PIPE_MSGBUS_LANE0 && lane != PIPE_MSGBUS_LANE1)
		return -EINVAL;

	msg = (PIPE_MSGBUS_3RD_DATA(data) << 16) | (PIPE_MSGBUS_2ND_DATA(addr)  << 8)  |
		PIPE_MSGBUS_1ST_DATA(addr);

	if (lane == PIPE_MSGBUS_LANE0) {
		rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE0_M2P_MESSAGEBUS, msg);
		start_bit = PIPE_MSGBUS_START_LANE0;
		ack_bit   = PIPE_MSGBUS_WR_ACK_LANE0;
	} else {
		rcar_dw_dp_phy_write(dw_dp, DPTX_PIPE_LANE1_M2P_MESSAGEBUS, msg);
		start_bit = PIPE_MSGBUS_START_LANE1;
		ack_bit   = PIPE_MSGBUS_WR_ACK_LANE1;
	}

	rcar_dw_dp_phy_modify(dw_dp, DPTX_PIPE_LANEX_M2P_MESSAGEBUS_START, 0, start_bit);

	return rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PIPE_LANEX_P2M_MESSAGEBUS, ack_bit, ack_bit);
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
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_CNTMON,
			      PHY_CNTMON_PHY0_SRAM_BYPASS | PHY_CNTMON_PHY0_SRAM_BOOTLOAD_BYPASS,
			      0);

	rcar_dw_dp_phy_modify(dw_dp, DPTX_DPCTRL_ENCRYPTION_MODE, ENCRYPTION_MODE, ENCRYPTION_DIS);

	rcar_dw_dp_phy_modify(dw_dp, DPTX_DPMODE, DPMODE_PHY_CLK_SEL_I, DPMODE_PHY_CLK_SEL_I);
	usleep_range(100, 101);

	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_CNTMON, PHY_CNTMON_PHY0_SRAM_BYPASS, 0);
	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_CNTMON, PHY_CNTMON_PHY0_SRAM_BOOTLOAD_BYPASS,
			      PHY_CNTMON_PHY0_SRAM_BOOTLOAD_BYPASS);

	rcar_dw_dp_phy_modify(dw_dp, DPTX_DPMODE, DPMODE_PHY_CLK_SEL_I, DPMODE_PHY_CLK_SEL_I);

	return 0;
}

static int rcar_dw_dp_phy_post_init_1(struct phy *p)
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

	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_CNTMON, PHY_CNTMON_PHY0_SRAM_EXT_LD_DONE,
			      PHY_CNTMON_PHY0_SRAM_EXT_LD_DONE);

	usleep_range(100, 101);

	return 0;
}

static int rcar_dw_dp_phy_post_init_2(struct phy *p)
{
	struct rcar_dw_dp *dw_dp = phy_get_drvdata(p);
	int ret;

	rcar_dw_dp_phy_modify(dw_dp, DPTX_PHY_PIPE0,
			      PHY_PIPE0_PIPE_LANE0_MAXPCLKREQ | PHY_PIPE0_PIPE_LANE1_MAXPCLKREQ,
			      PHY_PIPE0_PIPE_LANE0_MAXPCLKREQ | PHY_PIPE0_PIPE_LANE1_MAXPCLKREQ);

	ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PHY_PIPE0, PHY_PIPE0_PIPE_LANE0_MAXPCLKACK,
				      PHY_PIPE0_PIPE_LANE0_MAXPCLKACK);
	if (ret)
		return ret;

	ret = rcar_dw_dp_phy_reg_wait(dw_dp, DPTX_PHY_PIPE2, PHY_PIPE2_PIPE_LANE1_MAXPCLKACK,
				      PHY_PIPE2_PIPE_LANE1_MAXPCLKACK);
	if (ret)
		return ret;

	return 0;
}

static int rcar_dw_dp_phy_configure(struct phy *p, union phy_configure_opts *phy_cfg)
{
	struct rcar_dw_dp *dw_dp = phy_get_drvdata(p);
	int lanes = phy_cfg->dp.lanes;
	int lane, ret;

	u16 deemph_addr[] = {
		[0] = PCS_PHY_HDP_TX_CONTROL2, /* lane 0 */
		[1] = PCS_PHY_TX_CONTROL2,     /* lane 1 */
		[2] = PCS_PHY_TX_CONTROL2,     /* lane 2 */
		[3] = PCS_PHY_HDP_TX_CONTROL2, /* lane 3 */
	};

	u16 margin_addr[] = {
		[0] = PCS_PHY_HDP_TX_CONTROL8, /* lane 0 */
		[1] = PCS_PHY_TX_CONTROL8,     /* lane 1 */
		[2] = PCS_PHY_TX_CONTROL8,     /* lane 2 */
		[3] = PCS_PHY_HDP_TX_CONTROL8, /* lane 3 */
	};

	if (!phy_cfg->dp.set_lanes)
		return 0;

	for (lane = 0; lane < lanes; lane++) {
		ret = rcar_dw_dp_msg_bus_write(dw_dp, lane % 2, deemph_addr[lane],
					       phy_cfg->dp.pre[lane]);
		if (ret)
			return ret;
	}

	for (lane = 0; lane < lanes; lane++) {
		ret = rcar_dw_dp_msg_bus_write(dw_dp, lane % 2, margin_addr[lane],
					       phy_cfg->dp.voltage[lane]);
		if (ret)
			return ret;
	}

	return 0;
}

static int rcar_dw_dp_phy_dp_set_format(struct phy *p, struct phy_configure_opts_dp_format *format)
{
	struct rcar_dw_dp *dw_dp = phy_get_drvdata(p);
	u8 stream = format->stream;
	u32 fmt, bpc;

	switch (format->format) {
	case DP_RGB:
		fmt = IPI_RGB;
		break;
	case DP_YCBCR422:
		fmt = IPI_YCBCR422;
		break;
	case DP_YCBCR444:
		fmt = IPI_YCBCR444;
		break;
	default:
		return -EOPNOTSUPP;
	}

	rcar_dw_dp_phy_modify(dw_dp, DPTX_PIXELIF_IPI_FORMAT,
			      DPTX_PIXELIF_IPI_FORMAT_MASK(stream),
			      DPTX_PIXELIF_IPI_FORMAT_REG(stream, fmt));

	switch (format->depth) {
	case DP_6BPC:
		bpc = IPI_6BPC;
		break;
	case DP_8BPC:
		bpc = IPI_8BPC;
		break;
	case DP_10BPC:
		bpc = IPI_10BPC;
		break;
	case DP_12BPC:
		bpc = IPI_12BPC;
		break;
	case DP_16BPC:
		bpc = IPI_16BPC;
		break;
	case DP_48BPC:
		bpc = IPI_48BPC;
		break;
	default:
		return -EOPNOTSUPP;
	};

	rcar_dw_dp_phy_modify(dw_dp, DPTX_PIXELIF_IPI_COLOR,
			      DPTX_PIXELIF_IPI_COLOR_DEPTH_MASK(stream),
			      DPTX_PIXELIF_IPI_COLOR_DEPTH_REG(stream, bpc));

	return 0;
}

static const struct phy_ops rcar_dw_dp_phy_ops = {
	.init           = rcar_dw_dp_phy_init,
	.post_init      = rcar_dw_dp_phy_post_init,
	.post_init_1    = rcar_dw_dp_phy_post_init_1,
	.post_init_2    = rcar_dw_dp_phy_post_init_2,
	.configure	= rcar_dw_dp_phy_configure,
	.set_dp_format	= rcar_dw_dp_phy_dp_set_format,
};

static void rcar_dw_dp_detach_pd(struct rcar_dw_dp *dw_dp)
{
	int i;

	for (i = 0; i < dw_dp->num_pd; i++) {
		if (dw_dp->pd_dev[i] && !IS_ERR(dw_dp->pd_dev[i]))
			dev_pm_domain_detach(dw_dp->pd_dev[i], true);
		dw_dp->pd_dev[i] = NULL;
	}
}

static int rcar_dw_dp_attach_pd(struct rcar_dw_dp *dw_dp)
{
	struct device *dev = dw_dp->dev;
	struct device_node *np = dev->of_node;
	int i, ret;

	dw_dp->num_pd = of_count_phandle_with_args(np, "power-domains", "#power-domain-cells");
	if (dw_dp->num_pd < 0) {
		dev_err(dev, "No power domains defined\n");
		return dw_dp->num_pd;
	}

	dw_dp->pd_dev = devm_kmalloc_array(dev, dw_dp->num_pd,
					   sizeof(*dw_dp->pd_dev), GFP_KERNEL);
	if (!dw_dp->pd_dev)
		return -ENOMEM;

	for (i = 0; i < dw_dp->num_pd; i++) {
		dw_dp->pd_dev[i] = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR(dw_dp->pd_dev[i])) {
			ret = PTR_ERR(dw_dp->pd_dev[i]);
			goto err;
		}
	}

	return 0;

err:
	rcar_dw_dp_detach_pd(dw_dp);
	return ret;
}

static int rcar_dw_dp_clk_enable(struct rcar_dw_dp *dw_dp)
{
	int i, ret;

	for (i = 0; i < dw_dp->num_pd; i++)
		pm_runtime_get_sync(dw_dp->pd_dev[i]);

	ret = clk_prepare_enable(dw_dp->clk);
	if (ret)
		return ret;

	return 0;
}

static void rcar_dw_dp_clk_disable(struct rcar_dw_dp *dw_dp)
{
	int i;

	clk_disable_unprepare(dw_dp->clk);

	for (i = 0; i < dw_dp->num_pd; i++)
		pm_runtime_put(dw_dp->pd_dev[i]);
}

static int rcar_dw_dp_probe(struct platform_device *pdev)
{
	struct rcar_dw_dp *dw_dp;
	struct device *dev = &pdev->dev;
	struct dw_dp_plat_data plat_data;
	struct resource *res;
	int ret;

	dw_dp = devm_kzalloc(&pdev->dev, sizeof(*dw_dp), GFP_KERNEL);
	if (!dw_dp)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");
	if (!res)
		return -EINVAL;

	dw_dp->phy_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dw_dp->phy_addr))
		return PTR_ERR(dw_dp->phy_addr);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fw");
	if (!res)
		return -EINVAL;

	dw_dp->fw_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dw_dp->fw_addr))
		return PTR_ERR(dw_dp->fw_addr);

	dw_dp->dev = dev;
	platform_set_drvdata(pdev, dw_dp);

	ret = rcar_dw_dp_attach_pd(dw_dp);
	if (ret)
		return ret;

	dw_dp->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dw_dp->clk))
		return PTR_ERR(dw_dp->clk);

	pm_runtime_enable(dev);
	ret = rcar_dw_dp_clk_enable(dw_dp);
	if (ret)
		return ret;

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
	struct rcar_dw_dp *dw_dp = platform_get_drvdata(pdev);

	rcar_dw_dp_clk_disable(dw_dp);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int __maybe_unused rcar_dw_dp_pm_suspend(struct device *dev)
{
	struct rcar_dw_dp *dw_dp = dev_get_drvdata(dev);

	rcar_dw_dp_clk_disable(dw_dp);

	return 0;
}

static int __maybe_unused rcar_dw_dp_pm_resume(struct device *dev)
{
	struct rcar_dw_dp *dw_dp = dev_get_drvdata(dev);
	int ret;

	ret = rcar_dw_dp_clk_enable(dw_dp);
	if (ret)
		return ret;

	rcar_dw_dp_phy_write(dw_dp, DPTX_CLKGEN_RST_CNT, DPTX_CLKGEN_RST_CNT_DEFAULT);

	dw_dp_resume(dw_dp->dp);

	return 0;
}

static const struct dev_pm_ops rcar_dw_dp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rcar_dw_dp_pm_suspend, rcar_dw_dp_pm_resume)
};

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
		.name		= "rcar-dw-dp",
		.pm             = &rcar_dw_dp_pm_ops,
		.of_match_table = rcar_dw_dp_of_table,
	},
};

module_platform_driver(rcar_dw_dp_platform_driver);

MODULE_DESCRIPTION("Renesas R-Car DesignWare Display port Driver");
MODULE_LICENSE("GPL");
