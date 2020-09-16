// SPDX-License-Identifier: GPL-2.0
/*
 * rcar_mipi_dsi.c  --  R-Car MIPI DSI Encoder
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_mipi_dsi.h>

#include "rcar_mipi_dsi_regs.h"

struct rcar_mipi_dsi;

struct rcar_mipi_dsi {
	struct device *dev;
	const struct rcar_mipi_dsi_device_info *info;
	struct reset_control *rstc;

	struct mipi_dsi_host host;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector connector;

	void __iomem *mmio;
	struct {
		struct clk *mod;
		struct clk *extal;
		struct clk *dsi;
	} clocks;

	struct drm_display_mode display_mode;
};

#define bridge_to_rcar_mipi_dsi(b) \
	container_of(b, struct rcar_mipi_dsi, bridge)

#define connector_to_rcar_mipi_dsi(c) \
	container_of(c, struct rcar_mipi_dsi, connector)

static const u32 phtw[] = {
	0x01020114, 0x01600115, /* General testing */
	0x01030116, 0x0102011d, /* General testing */
	0x011101a4, 0x018601a4, /* 1Gbps testing */
	0x014201a0, 0x010001a3, /* 1Gbps testing */
	0x0101011f,				/* 1Gbps testing */
};

static void rcar_mipi_dsi_write(struct rcar_mipi_dsi *mipi_dsi,
				u32 reg, u32 data)
{
	iowrite32(data, mipi_dsi->mmio + reg);
}

static u32 rcar_mipi_dsi_read(struct rcar_mipi_dsi *mipi_dsi, u32 reg)
{
	return ioread32(mipi_dsi->mmio + reg);
}

static void rcar_mipi_dsi_clr(struct rcar_mipi_dsi *mipi_dsi,
			     u32 reg, u32 clr)
{
	rcar_mipi_dsi_write(mipi_dsi, reg,
			rcar_mipi_dsi_read(mipi_dsi, reg) & ~clr);
}

static void rcar_mipi_dsi_set(struct rcar_mipi_dsi *mipi_dsi,
				u32 reg, u32 set)
{
	rcar_mipi_dsi_write(mipi_dsi, reg,
			    rcar_mipi_dsi_read(mipi_dsi, reg) | set);
}

static int rcar_mipi_dsi_phtw_test(struct rcar_mipi_dsi *mipi_dsi, u32 phtw)
{
	unsigned int timeout;
	u32 status;

	rcar_mipi_dsi_write(mipi_dsi, PHTW, phtw);

	for (timeout = 10; timeout > 0; --timeout) {
		status = rcar_mipi_dsi_read(mipi_dsi, PHTW);
		if (!(status & PHTW_DWEN) &&
		    !(status & PHTW_CWEN))
			break;

		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(mipi_dsi->dev,
			"failed to test phtw with data %x\n", phtw);
		return -ETIMEDOUT;
	}

	return timeout;
}

/* -----------------------------------------------------------------------------
 * Hardware Setup
 */
static void rcar_mipi_dsi_set_display_timing(struct rcar_mipi_dsi *mipi_dsi)
{
	struct drm_display_mode *mode = &mipi_dsi->display_mode;
	u32 psphsetr;
	u32 setr;
	u32 vprmset0r;
	u32 vprmset1r;
	u32 vprmset2r;
	u32 vprmset3r;
	u32 vprmset4r;

	/* Configuration for Pixel Stream and Packet Header */
	psphsetr = TXVMPSPHSETR_DT_RGB24;
	rcar_mipi_dsi_write(mipi_dsi, TXVMPSPHSETR, psphsetr);

	/* Configuration for Blanking sequence and Input Pixel */
	setr = TXVMSETR_HSABPEN_EN | TXVMSETR_HBPBPEN_EN |
	       TXVMSETR_HFPBPEN_EN | TXVMSETR_SYNSEQ_PULSES |
	       TXVMSETR_PIXWDTH | TXVMSETR_VSTPM;
	rcar_mipi_dsi_write(mipi_dsi, TXVMSETR, setr);

	/* Configuration for Video Parameters */
	vprmset0r = ((mode->flags & DRM_MODE_FLAG_PVSYNC) ?
		TXVMVPRMSET0R_VSPOL_HIG : TXVMVPRMSET0R_VSPOL_LOW) |
		((mode->flags & DRM_MODE_FLAG_PHSYNC) ?
		TXVMVPRMSET0R_HSPOL_HIG : TXVMVPRMSET0R_HSPOL_LOW) |
		TXVMVPRMSET0R_CSPC_RGB | TXVMVPRMSET0R_BPP_24;

	vprmset1r = TXVMVPRMSET1R_VACTIVE(mode->vdisplay) |
		    TXVMVPRMSET1R_VSA(mode->vsync_end - mode->vsync_start);

	vprmset2r = TXVMVPRMSET2R_VFP(mode->vsync_start - mode->vdisplay) |
		    TXVMVPRMSET2R_VBP(mode->vtotal - mode->vsync_end);

	vprmset3r = TXVMVPRMSET3R_HACTIVE(mode->hdisplay) |
		    TXVMVPRMSET3R_HSA(mode->hsync_end - mode->hsync_start);

	vprmset4r = TXVMVPRMSET4R_HFP(mode->hsync_start - mode->hdisplay) |
		    TXVMVPRMSET4R_HBP(mode->htotal - mode->hsync_end);

	rcar_mipi_dsi_write(mipi_dsi, TXVMVPRMSET0R, vprmset0r);
	rcar_mipi_dsi_write(mipi_dsi, TXVMVPRMSET1R, vprmset1r);
	rcar_mipi_dsi_write(mipi_dsi, TXVMVPRMSET2R, vprmset2r);
	rcar_mipi_dsi_write(mipi_dsi, TXVMVPRMSET3R, vprmset3r);
	rcar_mipi_dsi_write(mipi_dsi, TXVMVPRMSET4R, vprmset4r);
}

static int rcar_mipi_dsi_startup(struct rcar_mipi_dsi *mipi_dsi)
{
	int ret, i;
	unsigned int timeout;
	u32 phy_setup;
	u32 clockset2, clockset3;
	u32 ppisetr;

	/* LPCLK enable */
	rcar_mipi_dsi_set(mipi_dsi, LPCLKSET, LPCLKSET_CKEN);

	/* CFGCLK enabled */
	rcar_mipi_dsi_set(mipi_dsi, CFGCLKSET, CFGCLKSET_CKEN);

	rcar_mipi_dsi_clr(mipi_dsi, PHYSETUP, PHYSETUP_RSTZ);
	rcar_mipi_dsi_clr(mipi_dsi, PHYSETUP, PHYSETUP_SHUTDOWNZ);

	rcar_mipi_dsi_set(mipi_dsi, PHTC, PHTC_TESTCLR);
	rcar_mipi_dsi_clr(mipi_dsi, PHTC, PHTC_TESTCLR);

	/* if not work, try to add read before write */
	phy_setup = rcar_mipi_dsi_read(mipi_dsi, PHYSETUP);
	phy_setup &= ~PHYSETUP_HSFREQRANGE_MASK;
	phy_setup |= PHYSETUP_HSFREQRANGE(0x29);
	rcar_mipi_dsi_write(mipi_dsi, PHYSETUP, phy_setup);

	for (i = 0; i < ARRAY_SIZE(phtw); i++) {
		ret = rcar_mipi_dsi_phtw_test(mipi_dsi, phtw[i]);
		if (ret < 0)
			return ret;
	}

	/* Clocks setting */
	rcar_mipi_dsi_clr(mipi_dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);
	rcar_mipi_dsi_set(mipi_dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);
	rcar_mipi_dsi_clr(mipi_dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);

	clockset2 = CLOCKSET2_M(145) | CLOCKSET2_N(10) |
		    CLOCKSET2_VCO_CNTRL(0x09);
	clockset3 = CLOCKSET3_PROP_CNTRL(0x0b) |
		    CLOCKSET3_INT_CNTRL(0) |
		    CLOCKSET3_CPBIAS_CNTRL(0x10) |
		    CLOCKSET3_GMP_CNTRL(1);
	rcar_mipi_dsi_write(mipi_dsi, CLOCKSET2, clockset2);
	rcar_mipi_dsi_write(mipi_dsi, CLOCKSET3, clockset3);

	rcar_mipi_dsi_clr(mipi_dsi, CLOCKSET1, CLOCKSET1_UPDATEPLL);
	rcar_mipi_dsi_set(mipi_dsi, CLOCKSET1, CLOCKSET1_UPDATEPLL);
	udelay(10);
	rcar_mipi_dsi_clr(mipi_dsi, CLOCKSET1, CLOCKSET1_UPDATEPLL);

	ppisetr = PPISETR_DLEN_3 | PPISETR_CLEN;
	rcar_mipi_dsi_write(mipi_dsi, PPISETR, ppisetr);

	rcar_mipi_dsi_set(mipi_dsi, PHYSETUP, PHYSETUP_SHUTDOWNZ);
	rcar_mipi_dsi_set(mipi_dsi, PHYSETUP, PHYSETUP_RSTZ);
	usleep_range(400, 500);

	/* Checking PPI clock status register */
	for (timeout = 10; timeout > 0; --timeout) {
		if ((rcar_mipi_dsi_read(mipi_dsi, PPICLSR) & PPICLSR_STPST) &&
		    (rcar_mipi_dsi_read(mipi_dsi, PPIDLSR) & PPIDLSR_STPST) &&
		    (rcar_mipi_dsi_read(mipi_dsi, CLOCKSET1) & CLOCKSET1_LOCK))
			break;

		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(mipi_dsi->dev, "failed to enable PPI clock\n");
		return -ETIMEDOUT;
	}

	/* Enable DOT clock */
	rcar_mipi_dsi_set(mipi_dsi, VCLKSET, VCLKSET_CKEN);
	rcar_mipi_dsi_set(mipi_dsi, VCLKSET,
			  VCLKSET_COLOR_RGB | VCLKSET_DIV_1 |
			  VCLKSET_BPP_24 | VCLKSET_4_LANE);

	/* After setting VCLKSET register, enable VCLKEN */
	rcar_mipi_dsi_set(mipi_dsi, VCLKEN, VCLKEN_CKEN);

	dev_dbg(mipi_dsi->dev, "DSI device is started\n");

	return 0;
}

static void rcar_mipi_dsi_shutdown(struct rcar_mipi_dsi *mipi_dsi)
{
	rcar_mipi_dsi_clr(mipi_dsi, PHYSETUP, PHYSETUP_RSTZ);
	rcar_mipi_dsi_clr(mipi_dsi, PHYSETUP, PHYSETUP_SHUTDOWNZ);

	dev_dbg(mipi_dsi->dev, "DSI device is shutdown\n");
}

static int rcar_mipi_dsi_start_hs_clock(struct rcar_mipi_dsi *mipi_dsi)
{
	/*
	 * In HW manual, we need to check TxDDRClkHS-Q Stable? but it dont
	 * write how to check. So we skip this check in this patch
	 */
	unsigned int timeout;
	u32 status;

	/* Start HS clock */
	rcar_mipi_dsi_set(mipi_dsi, PPICLCR, PPICLCR_TXREQHS);

	for (timeout = 10; timeout > 0; --timeout) {
		status = rcar_mipi_dsi_read(mipi_dsi, PPICLSR);

		if (status & PPICLSR_TOHS) {
			rcar_mipi_dsi_set(mipi_dsi, PPICLSCR, PPICLSCR_TOHS);
			break;
		}

		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(mipi_dsi->dev, "failed to enable HS clock\n");
		return -ETIMEDOUT;
	}

	dev_dbg(mipi_dsi->dev, "Start High Speed Clock");

	return 0;
}

static int rcar_mipi_dsi_start_video(struct rcar_mipi_dsi *mipi_dsi)
{
	unsigned int timeout;
	u32 status;

	/* Check status of Tranmission */
	for (timeout = 10; timeout > 0; --timeout) {
		status = rcar_mipi_dsi_read(mipi_dsi, LINKSR);
		if (!(status & LINKSR_LPBUSY) &&
		    !(status & LINKSR_HSBUSY)) {
			rcar_mipi_dsi_clr(mipi_dsi, TXVMCR, TXVMCR_VFCLR);
			break;
		}

		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(mipi_dsi->dev, "Failed to enable Video clock\n");
		return -ETIMEDOUT;
	}

	/* Check Clear Video mode FIFO */
	for (timeout = 10; timeout > 0; --timeout) {
		status = rcar_mipi_dsi_read(mipi_dsi, TXVMSR);
		if (status & TXVMSR_VFRDY) {
			rcar_mipi_dsi_set(mipi_dsi, TXVMCR, TXVMCR_EN_VIDEO);
			break;
		}

		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(mipi_dsi->dev, "Failed to enable Video clock\n");
		return -ETIMEDOUT;
	}

	/* Check Video transmission */
	for (timeout = 10; timeout > 0; --timeout) {
		status = rcar_mipi_dsi_read(mipi_dsi, TXVMSR);
		if (status & TXVMSR_RDY)
			break;

		usleep_range(1000, 2000);
	}

	if (!timeout) {
		dev_err(mipi_dsi->dev, "Failed to enable Video clock\n");
		return -ETIMEDOUT;
	}

	dev_dbg(mipi_dsi->dev, "Start video transferring");

	return 0;
}

/* -----------------------------------------------------------------------------
 * Bridge
 */
static int rcar_mipi_dsi_attach(struct drm_bridge *bridge)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);

	if (mipi_dsi->next_bridge)
		return drm_bridge_attach(bridge->encoder, mipi_dsi->next_bridge,
					bridge);
	else
		return -ENODEV;
}

static void rcar_mipi_dsi_mode_set(struct drm_bridge *bridge,
				   const struct drm_display_mode *mode,
				   const struct drm_display_mode *adjusted_mode)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);
	mipi_dsi->display_mode = *adjusted_mode;

	clk_prepare_enable(mipi_dsi->clocks.mod);
	clk_prepare_enable(mipi_dsi->clocks.dsi);

	rcar_mipi_dsi_set_display_timing(mipi_dsi);

	clk_disable_unprepare(mipi_dsi->clocks.dsi);
	clk_disable_unprepare(mipi_dsi->clocks.mod);
}

static void rcar_mipi_dsi_pre_enable(struct drm_bridge *bridge)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);
	int ret;

	ret = clk_prepare_enable(mipi_dsi->clocks.mod);
	if (ret < 0)
		return;

	ret = clk_prepare_enable(mipi_dsi->clocks.dsi);
	if (ret < 0) {
		return;
	}

	reset_control_deassert(mipi_dsi->rstc);
}

static void rcar_mipi_dsi_enable(struct drm_bridge *bridge)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);
	int ret;

	ret = rcar_mipi_dsi_startup(mipi_dsi);
	if (ret < 0)
		return;

	ret = rcar_mipi_dsi_start_hs_clock(mipi_dsi);
	if (ret < 0)
		return;

	rcar_mipi_dsi_set_display_timing(mipi_dsi);

	ret = rcar_mipi_dsi_start_video(mipi_dsi);
	if (ret < 0)
		return;

}

static void rcar_mipi_dsi_disable(struct drm_bridge *bridge)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);

	rcar_mipi_dsi_shutdown(mipi_dsi);
}

static void rcar_mipi_dsi_post_disable(struct drm_bridge *bridge)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);

	/* Disable DSI clock and reset HW*/
	reset_control_assert(mipi_dsi->rstc);

	clk_disable_unprepare(mipi_dsi->clocks.dsi);

	clk_disable_unprepare(mipi_dsi->clocks.mod);
}

static const struct drm_bridge_funcs rcar_mipi_dsi_bridge_ops = {
	.attach = rcar_mipi_dsi_attach,
	.mode_set = rcar_mipi_dsi_mode_set,
	.pre_enable = rcar_mipi_dsi_pre_enable,
	.enable = rcar_mipi_dsi_enable,
	.disable = rcar_mipi_dsi_disable,
	.post_disable = rcar_mipi_dsi_post_disable,
};

/* -----------------------------------------------------------------------------
 * Host setting
 */
static int rcar_mipi_dsi_host_attach(struct mipi_dsi_host *host,
									 struct mipi_dsi_device *device)
{
	return 0;
}

static int rcar_mipi_dsi_host_detach(struct mipi_dsi_host *host,
									 struct mipi_dsi_device *device)
{
	return 0;
}

static const struct mipi_dsi_host_ops rcar_mipi_dsi_host_ops = {
	.attach = rcar_mipi_dsi_host_attach,
	.detach = rcar_mipi_dsi_host_detach,
};

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int rcar_mipi_dsi_parse_dt(struct rcar_mipi_dsi *mipi_dsi)
{
	struct device_node *local_output = NULL;
	struct device_node *remote_input = NULL;
	struct device_node *remote = NULL;
	struct device_node *node;
	bool is_bridge = false;
	int ret = 0;

	local_output = of_graph_get_endpoint_by_regs(mipi_dsi->dev->of_node,
						     1, 0);
	if (!local_output) {
		dev_dbg(mipi_dsi->dev, "unconnected port@1\n");
		ret = -ENODEV;
		goto done;
	}

	/*
	 * Locate the connected entity and
	 * infer its type from the number of endpoints.
	 */
	remote = of_graph_get_remote_port_parent(local_output);
	if (!remote) {
		dev_dbg(mipi_dsi->dev, "unconnected endpoint %pOF\n",
				local_output);
		ret = -ENODEV;
		goto done;
	}

	if (!of_device_is_available(remote)) {
		dev_dbg(mipi_dsi->dev, "connected entity %pOF is disabled\n",
				remote);
		ret = -ENODEV;
		goto done;
	}

	remote_input = of_graph_get_remote_endpoint(local_output);

	for_each_endpoint_of_node(remote, node) {
		if (node != remote_input) {
			/*
			 * The endpoint which is not input node must be bridge
			 */
			is_bridge = true;
			of_node_put(node);
			break;
		}
	}

	if (is_bridge) {
		mipi_dsi->next_bridge = of_drm_find_bridge(remote);
		if (!mipi_dsi->next_bridge) {
			ret = -EPROBE_DEFER;
			goto done;
		}
	} else {
		ret = -ENODEV;
		goto done;
	}

done:
	of_node_put(local_output);
	of_node_put(remote_input);
	of_node_put(remote);

	return ret;
}

static struct clk *rcar_mipi_dsi_get_clock(struct rcar_mipi_dsi *mipi_dsi,
					   const char *name,
					   bool optional)
{
	struct clk *clk;

	clk = devm_clk_get(mipi_dsi->dev, name);
	if (!IS_ERR(clk))
		return clk;

	if (PTR_ERR(clk) == -ENOENT && optional)
		return NULL;

	if (PTR_ERR(clk) != -EPROBE_DEFER)
		dev_err(mipi_dsi->dev, "failed to get %s clock\n",
			name ? name : "module");

	return clk;
}

static int rcar_mipi_dsi_get_clocks(struct rcar_mipi_dsi *mipi_dsi)
{
	mipi_dsi->clocks.mod = rcar_mipi_dsi_get_clock(mipi_dsi, NULL, false);
	if (IS_ERR(mipi_dsi->clocks.mod))
		return PTR_ERR(mipi_dsi->clocks.mod);

	mipi_dsi->clocks.extal = rcar_mipi_dsi_get_clock(mipi_dsi,
							 "extal",
							 true);
	if (IS_ERR(mipi_dsi->clocks.extal))
		return PTR_ERR(mipi_dsi->clocks.extal);

	mipi_dsi->clocks.dsi = rcar_mipi_dsi_get_clock(mipi_dsi,
							    "dsi",
							    true);
	if (IS_ERR(mipi_dsi->clocks.dsi))
		return PTR_ERR(mipi_dsi->clocks.dsi);

	if (!mipi_dsi->clocks.extal && !mipi_dsi->clocks.dsi) {
		dev_err(mipi_dsi->dev,
			"no input clock (extal, dclkin.0)\n");
		return -EINVAL;
	}

	return 0;
}

static int rcar_mipi_dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_mipi_dsi *mipi_dsi;
	struct resource *mem;
	int ret;

	mipi_dsi = devm_kzalloc(&pdev->dev, sizeof(*mipi_dsi), GFP_KERNEL);
	if (mipi_dsi == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, mipi_dsi);

	mipi_dsi->dev = dev;
	mipi_dsi->info = of_device_get_match_data(&pdev->dev);

	ret = rcar_mipi_dsi_parse_dt(mipi_dsi);
	if (ret < 0)
		return ret;

	mipi_dsi->host.dev = dev;
	mipi_dsi->host.ops = &rcar_mipi_dsi_host_ops;
	ret = mipi_dsi_host_register(&mipi_dsi->host);
	if (ret < 0)
		return ret;

	mipi_dsi->bridge.driver_private = mipi_dsi;
	mipi_dsi->bridge.funcs = &rcar_mipi_dsi_bridge_ops;
	mipi_dsi->bridge.of_node = pdev->dev.of_node;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mipi_dsi->mmio = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(mipi_dsi->mmio))
		return PTR_ERR(mipi_dsi->mmio);

	ret = rcar_mipi_dsi_get_clocks(mipi_dsi);
	if (ret < 0)
		return ret;

	mipi_dsi->rstc = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(mipi_dsi->rstc)) {
		dev_err(&pdev->dev, "failed to get cpg reset\n");
		return PTR_ERR(mipi_dsi->rstc);
	}

	drm_bridge_add(&mipi_dsi->bridge);

	return 0;
}

static int rcar_mipi_dsi_remove(struct platform_device *pdev)
{
	struct rcar_mipi_dsi *mipi_dsi = platform_get_drvdata(pdev);

	drm_bridge_remove(&mipi_dsi->bridge);

	return 0;
}

static const struct of_device_id rcar_mipi_dsi_of_table[] = {
	{.compatible = "renesas,r8a779a0-mipi-dsi"},
};

MODULE_DEVICE_TABLE(of, rcar_mipi_dsi_of_table);

static struct platform_driver rcar_mipi_dsi_platform_driver = {
	.probe          = rcar_mipi_dsi_probe,
	.remove         = rcar_mipi_dsi_remove,
	.driver         = {
		.name   = "rcar-mipi-dsi",
		.of_match_table = rcar_mipi_dsi_of_table,
	},
};

module_platform_driver(rcar_mipi_dsi_platform_driver);

MODULE_DESCRIPTION("Renesas R-Car MIPI DSI Encoder Driver");
MODULE_LICENSE("GPL");
