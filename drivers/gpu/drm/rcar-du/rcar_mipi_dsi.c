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

#include "rcar_mipi_dsi.h"
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
	enum mipi_dsi_pixel_format format;
	unsigned int num_data_lanes;
	unsigned int lanes;
};

#define bridge_to_rcar_mipi_dsi(b) \
	container_of(b, struct rcar_mipi_dsi, bridge)

#define connector_to_rcar_mipi_dsi(c) \
	container_of(c, struct rcar_mipi_dsi, connector)

#define host_to_rcar_mipi_dsi(c) \
	container_of(c, struct rcar_mipi_dsi, host)

static const u32 phtw[] = {
	0x01020114, 0x01600115, /* General testing */
	0x01030116, 0x0102011d, /* General testing */
	0x011101a4, 0x018601a4, /* 1Gbps testing */
	0x014201a0, 0x010001a3, /* 1Gbps testing */
	0x0101011f,		/* 1Gbps testing */
};

static const u32 phtw_v4h[] = {
	0x01010100, 0x01030173,
	0x01000174, 0x01500175,
	0x01030176, 0x01040166,
	0x010201ad,
	0x01020100, 0x01010172,
	0x01570170, 0x01060171,
	0x01110172,
};

static const u32 phtw2[] = {
	0x01090160, 0x01090170,
};

static const u32 hsfreqrange_table[][2] = {
	{80,   0x00}, {90,   0x10}, {100,  0x20},
	{110,  0x30}, {120,  0x01}, {130,  0x11},
	{140,  0x21}, {150,  0x31}, {160,  0x02},
	{170,  0x12}, {180,  0x22}, {190,  0x32},
	{205,  0x03}, {220,  0x13}, {235,  0x23},
	{250,  0x33}, {275,  0x04}, {300,  0x14},
	{325,  0x25}, {350,  0x35}, {400,  0x05},
	{450,  0x16}, {500,  0x26}, {550,  0x37},
	{600,  0x07}, {650,  0x18}, {700,  0x28},
	{750,  0x39}, {800,  0x09}, {850,  0x19},
	{900,  0x29}, {950,  0x3a}, {1000, 0x0a},
	{1050, 0x1a}, {1100, 0x2a}, {1150, 0x3b},
	{1200, 0x0b}, {1250, 0x1b}, {1300, 0x2b},
	{1350, 0x3c}, {1400, 0x0c}, {1450, 0x1c},
	{1500, 0x2c}, {1550, 0x3d}, {1600, 0x0d},
	{1650, 0x1d}, {1700, 0x2e}, {1750, 0x3e},
	{1800, 0x0e}, {1850, 0x1e}, {1900, 0x2f},
	{1950, 0x3f}, {2000, 0x0f}, {2050, 0x40},
	{2100, 0x41}, {2150, 0x42}, {2200, 0x43},
	{2250, 0x44}, {2300, 0x45}, {2350, 0x46},
	{2400, 0x47}, {2450, 0x48}, {2500, 0x49},
	{ /* sentinel */ },
};

struct vco_cntrl_value {
	u32 min_freq;
	u32 max_freq;
	u16 value;
};

static const struct vco_cntrl_value vco_cntrl_table[] = {
	{ .min_freq = 40000000,   .max_freq = 55000000,   .value = 0x3f},
	{ .min_freq = 52500000,   .max_freq = 80000000,   .value = 0x39},
	{ .min_freq = 80000000,   .max_freq = 110000000,  .value = 0x2f},
	{ .min_freq = 105000000,  .max_freq = 160000000,  .value = 0x29},
	{ .min_freq = 160000000,  .max_freq = 220000000,  .value = 0x1f},
	{ .min_freq = 210000000,  .max_freq = 320000000,  .value = 0x19},
	{ .min_freq = 320000000,  .max_freq = 440000000,  .value = 0x0f},
	{ .min_freq = 420000000,  .max_freq = 660000000,  .value = 0x09},
	{ .min_freq = 630000000,  .max_freq = 1149000000, .value = 0x03},
	{ .min_freq = 1100000000, .max_freq = 1152000000, .value = 0x01},
	{ .min_freq = 1150000000, .max_freq = 1250000000, .value = 0x01},
	{ /* sentinel */ },
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
struct dsi_setup_info {
	unsigned int err;
	u16 vco_cntrl;
	u16 prop_cntrl;
	u16 hsfreqrange;
	u16 div;
	u16 int_contrl;
	u16 cpbias_cntrl;
	u16 gmp_cntrl;
	unsigned int m;
	unsigned int n;
};

static void rcar_mipi_dsi_parametters_calc(struct rcar_mipi_dsi *mipi_dsi,
					struct clk *clk, unsigned long target,
					struct dsi_setup_info *setup_info)
{
#if 0
	const struct vco_cntrl_value *vco_cntrl;
	unsigned long fout_target;
	unsigned long fin, fout;
	unsigned long hsfreq;
	unsigned int divider;
	unsigned int n;
	unsigned int i;
	unsigned int err;

	/*
	 * Calculate Fout = dot clock * ColorDepth / (2 * Lane Count)
	 * The range out Fout is [40 - 1250] Mhz
	 */
	fout_target = target *
			mipi_dsi_pixel_format_to_bpp(mipi_dsi->format) /
			(2 * mipi_dsi->lanes);
	if (fout_target < 40000000 || fout_target > 1250000000)
		return;

	/* Find vco_cntrl */
	for (vco_cntrl = vco_cntrl_table; vco_cntrl->min_freq != 0; vco_cntrl++) {
		if (fout_target > vco_cntrl->min_freq &&
			fout_target <= vco_cntrl->max_freq) {
			setup_info->vco_cntrl = vco_cntrl->value;
			if (fout_target >= 1150000000)
				setup_info->prop_cntrl = 0x0c;
			else
				setup_info->prop_cntrl = 0x0b;
			break;
		}
	}

	/* Add divider */
	setup_info->div = (setup_info->vco_cntrl & 0x30) >> 4;

	/* Find hsfreqrange */
	hsfreq = fout_target * 2;
	do_div(hsfreq, 1000000);
	for (i = 0; i < ARRAY_SIZE(hsfreqrange_table); i++) {
		if (hsfreq > hsfreqrange_table[i][0] &&
			hsfreq <= hsfreqrange_table[i+1][0]) {
			setup_info->hsfreqrange = hsfreqrange_table[i+1][1];
			break;
		}
	}

	/*
	 * Calculate n and m for PLL clock
	 * Following the HW manual the ranges of n and m are
	 * n = [3-8] and m = [64-625]
	 */
	fin = clk_get_rate(clk);
	divider = 1 << setup_info->div;
	for (n = 3; n < 9; n++) {
		unsigned long fpfd;
		unsigned int m;

		fpfd = fin / n;

		for (m = 64; m < 626; m++) {
			fout = fpfd * m / divider;
			err = abs((long)(fout - fout_target) * 10000 /
					(long)fout_target);
			if (err < setup_info->err) {
				setup_info->m = m - 2;
				setup_info->n = n - 1;
				setup_info->err = err;
				if (err == 0)
					goto done;
			}
		}
	}

done:
	dev_dbg(mipi_dsi->dev,
		"%pC %lu Hz -> Fout %lu Hz (target %lu Hz, error %d.%02u%%), PLL M/N/DIV %u/%u/%u\n",
		clk, fin, fout, fout_target, setup_info->err / 100,
		setup_info->err % 100, setup_info->m,
		setup_info->n, setup_info->div);
	dev_dbg(mipi_dsi->dev,
		"vco_cntrl = 0x%x\tprop_cntrl = 0x%x\thsfreqrange = 0x%x\n",
		setup_info->vco_cntrl,
		setup_info->prop_cntrl,
		setup_info->hsfreqrange);
#endif

	setup_info->vco_cntrl = 0x10;
	setup_info->m = 856;
	setup_info->n = 1;
	setup_info->prop_cntrl = 0x0A;
	setup_info->int_contrl = 0x08;
	setup_info->cpbias_cntrl = 0x00;
	setup_info->gmp_cntrl = 0;
	setup_info->hsfreqrange = 0x29;
	setup_info->div = 2;
}

static void rcar_mipi_dsi_set_display_timing(struct rcar_mipi_dsi *mipi_dsi)
{
	struct drm_display_mode *mode = &mipi_dsi->display_mode;
	u32 setr;
	u32 vprmset0r;
	u32 vprmset1r;
	u32 vprmset2r;
	u32 vprmset3r;
	u32 vprmset4r;

	/* Configuration for Pixel Stream and Packet Header */
	if (mipi_dsi_pixel_format_to_bpp(mipi_dsi->format) == 24)
		rcar_mipi_dsi_write(mipi_dsi, TXVMPSPHSETR,
					TXVMPSPHSETR_DT_RGB24);
	else if (mipi_dsi_pixel_format_to_bpp(mipi_dsi->format) == 18)
		rcar_mipi_dsi_write(mipi_dsi, TXVMPSPHSETR,
					TXVMPSPHSETR_DT_RGB18);
	else if (mipi_dsi_pixel_format_to_bpp(mipi_dsi->format) == 16)
		rcar_mipi_dsi_write(mipi_dsi, TXVMPSPHSETR,
					TXVMPSPHSETR_DT_RGB16);
	else {
		dev_warn(mipi_dsi->dev, "unsupported format");
		return;
	}

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
	struct drm_display_mode *mode = &mipi_dsi->display_mode;
	struct dsi_setup_info setup_info = {.err = -1 };
	unsigned int timeout;
	int ret, i;
	int dsi_format;
	u32 phy_setup;
	u32 clockset2, clockset3;
	u32 ppisetr;
	u32 vclkset;

	/* Checking valid format */
	dsi_format = mipi_dsi_pixel_format_to_bpp(mipi_dsi->format);
	if (dsi_format < 0) {
		dev_warn(mipi_dsi->dev, "invalid format");
		return -EINVAL;
	}

	/* Parametters Calulation */
	rcar_mipi_dsi_parametters_calc(mipi_dsi, mipi_dsi->clocks.extal,
					mode->clock * 1000, &setup_info);

	/* LPCLK enable */
	rcar_mipi_dsi_set(mipi_dsi, LPCLKSET, LPCLKSET_CKEN);

	/* CFGCLK enabled */
	rcar_mipi_dsi_set(mipi_dsi, CFGCLKSET, CFGCLKSET_CKEN);

	rcar_mipi_dsi_clr(mipi_dsi, PHYSETUP, PHYSETUP_RSTZ);
	rcar_mipi_dsi_clr(mipi_dsi, PHYSETUP, PHYSETUP_SHUTDOWNZ);

	rcar_mipi_dsi_set(mipi_dsi, PHTC, PHTC_TESTCLR);
	rcar_mipi_dsi_clr(mipi_dsi, PHTC, PHTC_TESTCLR);

	/* PHY setting */
	phy_setup = rcar_mipi_dsi_read(mipi_dsi, PHYSETUP);
	phy_setup &= ~PHYSETUP_HSFREQRANGE_MASK;
	phy_setup |= PHYSETUP_HSFREQRANGE(setup_info.hsfreqrange);
	rcar_mipi_dsi_write(mipi_dsi, PHYSETUP, phy_setup);

	for (i = 0; i < ARRAY_SIZE(phtw_v4h); i++) {
		ret = rcar_mipi_dsi_phtw_test(mipi_dsi, phtw_v4h[i]);
		if (ret < 0)
			return ret;
	}

	rcar_mipi_dsi_set(mipi_dsi, CLOCKSET1, 0x0100000C);

	/* PLL Clock Setting */
	rcar_mipi_dsi_clr(mipi_dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);
	rcar_mipi_dsi_set(mipi_dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);
	rcar_mipi_dsi_clr(mipi_dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);

	clockset2 = CLOCKSET2_M(setup_info.m) | CLOCKSET2_N(setup_info.n) |
		    CLOCKSET2_VCO_CNTRL(setup_info.vco_cntrl);
	clockset3 = CLOCKSET3_PROP_CNTRL(setup_info.prop_cntrl) |
		    CLOCKSET3_INT_CNTRL(setup_info.int_contrl) |
		    CLOCKSET3_CPBIAS_CNTRL(setup_info.cpbias_cntrl) |
		    CLOCKSET3_GMP_CNTRL(setup_info.gmp_cntrl);
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

	for (i = 0; i < ARRAY_SIZE(phtw2); i++) {
		ret = rcar_mipi_dsi_phtw_test(mipi_dsi, phtw2[i]);
		if (ret < 0)
			return ret;
	}

	/* Enable DOT clock */
	vclkset = VCLKSET_CKEN;
	rcar_mipi_dsi_set(mipi_dsi, VCLKSET, vclkset);

	if (dsi_format == 24)
		vclkset |= VCLKSET_BPP_24;
	else if (dsi_format == 18)
		vclkset |= VCLKSET_BPP_18;
	else if (dsi_format == 16)
		vclkset |= VCLKSET_BPP_16;
	else {
		dev_warn(mipi_dsi->dev, "unsupported format");
		return -EINVAL;
	}
	vclkset |= VCLKSET_COLOR_RGB | VCLKSET_DIV(setup_info.div) |
			VCLKSET_LANE(mipi_dsi->lanes - 1);

	rcar_mipi_dsi_set(mipi_dsi, VCLKSET, vclkset);

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
		dev_err(mipi_dsi->dev, "Link busy\n");
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
		dev_err(mipi_dsi->dev, "Clear Video mode FIFO error\n");
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
		dev_err(mipi_dsi->dev, "Cannot enable Video mode\n");
		return -ETIMEDOUT;
	}

	dev_dbg(mipi_dsi->dev, "Start video transferring");

	return 0;
}

/* -----------------------------------------------------------------------------
 * Bridge
 */
static int rcar_mipi_dsi_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);

	if (mipi_dsi->next_bridge)
		return drm_bridge_attach(bridge->encoder, mipi_dsi->next_bridge,
					bridge, flags);
	else
		return -ENODEV;
}

static void rcar_mipi_dsi_mode_set(struct drm_bridge *bridge,
				   const struct drm_display_mode *mode,
				   const struct drm_display_mode *adjusted_mode)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);

	mipi_dsi->display_mode = *adjusted_mode;
}

static void rcar_mipi_dsi_enable(struct drm_bridge *bridge)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);
	int ret;

	rcar_mipi_dsi_set_display_timing(mipi_dsi);

	ret = rcar_mipi_dsi_start_hs_clock(mipi_dsi);
	if (ret < 0)
		return;

	ret = rcar_mipi_dsi_start_video(mipi_dsi);
	if (ret < 0)
		return;

}

static enum drm_mode_status
rcar_mipi_dsi_bridge_mode_valid(struct drm_bridge *bridge,
				const struct drm_display_info *info,
				const struct drm_display_mode *mode)
{
	if (mode->clock > 297000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_bridge_funcs rcar_mipi_dsi_bridge_ops = {
	.attach = rcar_mipi_dsi_attach,
	.mode_set = rcar_mipi_dsi_mode_set,
	.enable = rcar_mipi_dsi_enable,
	.mode_valid = rcar_mipi_dsi_bridge_mode_valid,
};

/* -----------------------------------------------------------------------------
 * Clock Setting
 */

int rcar_mipi_dsi_clk_enable(struct drm_bridge *bridge)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);
	int ret;

	reset_control_deassert(mipi_dsi->rstc);

	ret = clk_prepare_enable(mipi_dsi->clocks.mod);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(mipi_dsi->clocks.dsi);
	if (ret < 0)
		return ret;

	ret = rcar_mipi_dsi_startup(mipi_dsi);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_mipi_dsi_clk_enable);

void rcar_mipi_dsi_clk_disable(struct drm_bridge *bridge)
{
	struct rcar_mipi_dsi *mipi_dsi = bridge_to_rcar_mipi_dsi(bridge);

	rcar_mipi_dsi_shutdown(mipi_dsi);

	/* Disable DSI clock and reset HW */
	clk_disable_unprepare(mipi_dsi->clocks.dsi);

	clk_disable_unprepare(mipi_dsi->clocks.mod);

	reset_control_assert(mipi_dsi->rstc);
}
EXPORT_SYMBOL_GPL(rcar_mipi_dsi_clk_disable);

/* -----------------------------------------------------------------------------
 * Host setting
 */
static int rcar_mipi_dsi_host_attach(struct mipi_dsi_host *host,
					struct mipi_dsi_device *device)
{
	struct rcar_mipi_dsi *mipi_dsi = host_to_rcar_mipi_dsi(host);

	if (device->lanes > mipi_dsi->num_data_lanes)
		return -EINVAL;

	mipi_dsi->lanes = device->lanes;
	mipi_dsi->format = device->format;

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
	struct property *prop;
	bool is_bridge = false;
	int ret = 0;
	int len, num_lanes;

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

	/* Get lanes information */
	prop = of_find_property(local_output, "data-lanes", &len);
	if (!prop) {
		mipi_dsi->num_data_lanes = 4;
		dev_dbg(mipi_dsi->dev,
			"failed to find data lane information, using default\n");
		goto done;
	}

	num_lanes = len / sizeof(u32);

	if (num_lanes < 1 || num_lanes > 4) {
		dev_err(mipi_dsi->dev, "data lanes definition is not correct\n");
		return -EINVAL;
	}

	mipi_dsi->num_data_lanes = num_lanes;
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

	/* Init bridge */
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

	/* Init host device */
	mipi_dsi->host.dev = dev;
	mipi_dsi->host.ops = &rcar_mipi_dsi_host_ops;
	ret = mipi_dsi_host_register(&mipi_dsi->host);
	if (ret < 0)
		return ret;

	drm_bridge_add(&mipi_dsi->bridge);

	return 0;
}

static int rcar_mipi_dsi_remove(struct platform_device *pdev)
{
	struct rcar_mipi_dsi *mipi_dsi = platform_get_drvdata(pdev);

	drm_bridge_remove(&mipi_dsi->bridge);

	mipi_dsi_host_unregister(&mipi_dsi->host);

	return 0;
}

static const struct of_device_id rcar_mipi_dsi_of_table[] = {
	{ .compatible = "renesas,r8a779a0-mipi-dsi" },
	{ .compatible = "renesas,r8a779g0-mipi-dsi" },
	{ }
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
