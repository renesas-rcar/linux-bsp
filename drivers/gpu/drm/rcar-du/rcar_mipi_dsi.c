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

/* Clock setting definition */
#define DSI_CLOCK_SETTING(mi, ma, vco, di, cpb, gmp, intc, pro, divi) \
	.min_freq = (mi), \
	.max_freq = (ma), \
	.vco_cntrl = (vco), \
	.div = (di), \
	.cpbias_cntrl = (cpb), \
	.gmp_cntrl = (gmp), \
	.int_cntrl = (intc), \
	.prop_cntrl = (pro), \
	.divider = (divi)

struct clockset_values {
	u16 min_freq;
	u16 max_freq;
	u8 vco_cntrl;
	u8 div;
	u8 cpbias_cntrl;
	u8 gmp_cntrl;
	u8 int_cntrl;
	u8 prop_cntrl;
	u8 divider;
};

static const struct clockset_values clockset_setting_table_r8a779g0[] = {
	{ DSI_CLOCK_SETTING(   40,   46, 0x2b, 0x05, 0x00, 0x00, 0x08, 0x0a, 64 ) },
	{ DSI_CLOCK_SETTING(   44,   56, 0x28, 0x05, 0x00, 0x00, 0x08, 0x0a, 64 ) },
	{ DSI_CLOCK_SETTING(   53,   62, 0x28, 0x05, 0x00, 0x00, 0x08, 0x0a, 64 ) },
	{ DSI_CLOCK_SETTING(   62,   77, 0x27, 0x04, 0x00, 0x00, 0x08, 0x0a, 32 ) },
	{ DSI_CLOCK_SETTING(   73,   93, 0x23, 0x04, 0x00, 0x00, 0x08, 0x0a, 32 ) },
	{ DSI_CLOCK_SETTING(   88,  121, 0x20, 0x04, 0x00, 0x00, 0x08, 0x0a, 32 ) },
	{ DSI_CLOCK_SETTING(  106,  125, 0x20, 0x04, 0x00, 0x00, 0x08, 0x0a, 32 ) },
	{ DSI_CLOCK_SETTING(  125,  154, 0x1f, 0x03, 0x00, 0x00, 0x08, 0x0a, 16 ) },
	{ DSI_CLOCK_SETTING(  146,  186, 0x1b, 0x03, 0x00, 0x00, 0x08, 0x0a, 16 ) },
	{ DSI_CLOCK_SETTING(  176,  224, 0x18, 0x03, 0x00, 0x00, 0x08, 0x0a, 16 ) },
	{ DSI_CLOCK_SETTING(  213,  250, 0x18, 0x03, 0x00, 0x00, 0x08, 0x0a, 16 ) },
	{ DSI_CLOCK_SETTING(  250,  307, 0x17, 0x02, 0x00, 0x00, 0x08, 0x0a,  8 ) },
	{ DSI_CLOCK_SETTING(  292,  371, 0x13, 0x02, 0x00, 0x00, 0x08, 0x0a,  8 ) },
	{ DSI_CLOCK_SETTING(  353,  484, 0x10, 0x02, 0x00, 0x00, 0x08, 0x0a,  8 ) },
	{ DSI_CLOCK_SETTING(  426,  500, 0x10, 0x02, 0x00, 0x00, 0x08, 0x0a,  8 ) },
	{ DSI_CLOCK_SETTING(  500,  615, 0x0f, 0x01, 0x00, 0x00, 0x08, 0x0a,  4 ) },
	{ DSI_CLOCK_SETTING(  585,  743, 0x0b, 0x01, 0x00, 0x00, 0x08, 0x0a,  4 ) },
	{ DSI_CLOCK_SETTING(  707,  899, 0x08, 0x01, 0x00, 0x00, 0x08, 0x0a,  4 ) },
	{ DSI_CLOCK_SETTING(  853, 1000, 0x08, 0x01, 0x00, 0x00, 0x08, 0x0a,  4 ) },
	{ DSI_CLOCK_SETTING( 1000, 1230, 0x07, 0x00, 0x00, 0x00, 0x08, 0x0a,  2 ) },
	{ DSI_CLOCK_SETTING( 1170, 1250, 0x03, 0x00, 0x00, 0x00, 0x08, 0x0a,  2 ) },
	{ /* sentinel */ },
};

struct rcar_mipi_dsi_hsfeq {
	u16 mbps;
	u16 value;
};

static const struct rcar_mipi_dsi_hsfeq hsfreqrange_table_r8a779g0[] = {
	{ .mbps =   80, .value = 0x00},
	{ .mbps =   90, .value = 0x10},
	{ .mbps =  100, .value = 0x20},
	{ .mbps =  110, .value = 0x30},
	{ .mbps =  120, .value = 0x01},
	{ .mbps =  130, .value = 0x11},
	{ .mbps =  140, .value = 0x21},
	{ .mbps =  150, .value = 0x31},
	{ .mbps =  160, .value = 0x02},
	{ .mbps =  170, .value = 0x12},
	{ .mbps =  180, .value = 0x22},
	{ .mbps =  190, .value = 0x32},
	{ .mbps =  205, .value = 0x03},
	{ .mbps =  220, .value = 0x13},
	{ .mbps =  235, .value = 0x23},
	{ .mbps =  250, .value = 0x33},
	{ .mbps =  275, .value = 0x04},
	{ .mbps =  300, .value = 0x14},
	{ .mbps =  325, .value = 0x25},
	{ .mbps =  350, .value = 0x35},
	{ .mbps =  400, .value = 0x05},
	{ .mbps =  450, .value = 0x16},
	{ .mbps =  500, .value = 0x26},
	{ .mbps =  550, .value = 0x37},
	{ .mbps =  600, .value = 0x07},
	{ .mbps =  650, .value = 0x18},
	{ .mbps =  700, .value = 0x28},
	{ .mbps =  750, .value = 0x39},
	{ .mbps =  800, .value = 0x09},
	{ .mbps =  850, .value = 0x19},
	{ .mbps =  900, .value = 0x29},
	{ .mbps =  950, .value = 0x3a},
	{ .mbps = 1000, .value = 0x0a},
	{ .mbps = 1050, .value = 0x1a},
	{ .mbps = 1100, .value = 0x2a},
	{ .mbps = 1150, .value = 0x3b},
	{ .mbps = 1200, .value = 0x0b},
	{ .mbps = 1250, .value = 0x1b},
	{ .mbps = 1300, .value = 0x2b},
	{ .mbps = 1350, .value = 0x3c},
	{ .mbps = 1400, .value = 0x0c},
	{ .mbps = 1450, .value = 0x1c},
	{ .mbps = 1500, .value = 0x2c},
	{ .mbps = 1550, .value = 0x3d},
	{ .mbps = 1600, .value = 0x0d},
	{ .mbps = 1650, .value = 0x1d},
	{ .mbps = 1700, .value = 0x2e},
	{ .mbps = 1750, .value = 0x3e},
	{ .mbps = 1800, .value = 0x0e},
	{ .mbps = 1850, .value = 0x1e},
	{ .mbps = 1900, .value = 0x2f},
	{ .mbps = 1950, .value = 0x3f},
	{ .mbps = 2000, .value = 0x0f},
	{ .mbps = 2050, .value = 0x40},
	{ .mbps = 2100, .value = 0x41},
	{ .mbps = 2150, .value = 0x42},
	{ .mbps = 2200, .value = 0x43},
	{ .mbps = 2250, .value = 0x44},
	{ .mbps = 2300, .value = 0x45},
	{ .mbps = 2350, .value = 0x46},
	{ .mbps = 2400, .value = 0x47},
	{ .mbps = 2450, .value = 0x48},
	{ .mbps = 2500, .value = 0x49},
	{ /* sentinel */ },
};

struct rcar_mipi_dsi;

struct rcar_mipi_dsi_info {
	int (*init_phtw)(struct rcar_mipi_dsi *mipi_dsi);
	int (*post_init_phtw)(struct rcar_mipi_dsi *mipi_dsi);
	const struct rcar_mipi_dsi_hsfeq *hsfeqrange_values;
	const struct clockset_values *clkset_values;
	u8 m_offset;
	u8 n_offset;
	u8 freq_mul;
};

struct rcar_mipi_dsi {
	struct device *dev;
	const struct rcar_mipi_dsi_info *info;
	struct reset_control *rstc;

	struct mipi_dsi_host host;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector connector;

	void __iomem *mmio;
	struct {
		struct clk *mod;
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

/* PHTW init */

static int rcar_mipi_dsi_write_phtw(struct rcar_mipi_dsi *mipi_dsi, const u32 *phtw_values)
{
	unsigned int timeout;
	u32 status;
	const u32 *phtw_value;

	for (phtw_value = phtw_values; *phtw_value; phtw_value++) {
		rcar_mipi_dsi_write(mipi_dsi, PHTW, *phtw_value);

		for (timeout = 10; timeout > 0; --timeout) {
			status = rcar_mipi_dsi_read(mipi_dsi, PHTW);
			if (!(status & PHTW_DWEN) && !(status & PHTW_CWEN))
				break;

			usleep_range(1000, 2000);
		}

		if (!timeout) {
			dev_err(mipi_dsi->dev, "failed to write PHTW\n");
			return -ETIMEDOUT;
		}
	}

	return timeout;
}

static int rcar_mipi_dsi_init_phtw_v4h(struct rcar_mipi_dsi *mipi_dsi)
{
	static const u32 phtw_init[] = {
		0x01010100, 0x01030173, 0x01000174, 0x01500175,
		0x01030176, 0x01040166, 0x010201ad, 0x01020100,
		0x01010172, 0x01570170, 0x01060171, 0x01110172,
		0,
	};

	return rcar_mipi_dsi_write_phtw (mipi_dsi, phtw_init);
}

static int rcar_mipi_dsi_post_init_phtw_v4h(struct rcar_mipi_dsi *mipi_dsi)
{
	static const u32 phtw_post_init[] = {
		0x01090160, 0x01090170,
		0,
	};

	return rcar_mipi_dsi_write_phtw (mipi_dsi, phtw_post_init);
}

/* -----------------------------------------------------------------------------
 * Hardware Setup
 */
struct dsi_setup_info {
	struct rcar_mipi_dsi_hsfeq hsfeq;
	struct clockset_values clk_setting;
	unsigned int m;
	unsigned int n;
};

static void rcar_mipi_dsi_parametters_calc(struct rcar_mipi_dsi *mipi_dsi,
					struct clk *clk, unsigned long target,
					struct dsi_setup_info *setup_info)
{
	const struct clockset_values *clkset_value;
	const struct rcar_mipi_dsi_hsfeq *hsfreq_value;
	unsigned long fout_target;
	unsigned long fin;
	unsigned long fpfd;
	unsigned long mbps;
	unsigned int n;
	unsigned int m;

	/*
	 * Calculate Fout = dot clock * ColorDepth / (2 * Lane Count)
	 * The range out Fout is [40 - 1250] Mhz
	 */
	fout_target = target *
			mipi_dsi_pixel_format_to_bpp(mipi_dsi->format) /
			(2 * mipi_dsi->lanes);
	do_div(fout_target, 1000000);
	if (fout_target < 40 || fout_target > 1250) {
		dev_err(mipi_dsi->dev, "clock is out of range\n");
		return;
	}

	/* Find clk setting */
	for (clkset_value = mipi_dsi->info->clkset_values;
		 clkset_value->min_freq;
		 clkset_value++) {
		if (fout_target > clkset_value->min_freq &&
			fout_target <= clkset_value->max_freq) {
			setup_info->clk_setting = *clkset_value;
			break;
		}
	}

	/* Find hsfreqrange */
	mbps = fout_target * 2;
	for (hsfreq_value = mipi_dsi->info->hsfeqrange_values;
		 hsfreq_value->mbps;
		 hsfreq_value++) {
		if (hsfreq_value->mbps >= mbps) {
			setup_info->hsfeq = *hsfreq_value;
			break;
		}
	}

	/*
	 * Calculate n and m for PLL clock
	 * There is variety of [n, m] sets.
	 * However, to reduce compulation cost, n = 0 is used
	 */
	fin = clk_get_rate(clk);

	setup_info->n = 0;
	n = mipi_dsi->info->n_offset;

	fpfd = fin / (n
		* mipi_dsi->info->freq_mul
		* setup_info->clk_setting.divider);

	m = fout_target * 1000000 / fpfd;
	setup_info->m = m - mipi_dsi->info->m_offset;
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
	struct dsi_setup_info setup_info;
	unsigned int timeout;
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
	rcar_mipi_dsi_parametters_calc(mipi_dsi, mipi_dsi->clocks.mod,
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
	phy_setup |= PHYSETUP_HSFREQRANGE(setup_info.hsfeq.value);
	rcar_mipi_dsi_write(mipi_dsi, PHYSETUP, phy_setup);

	if (mipi_dsi->info->init_phtw)
		mipi_dsi->info->init_phtw(mipi_dsi);

	rcar_mipi_dsi_set(mipi_dsi, CLOCKSET1, 0x0100000C);

	/* PLL Clock Setting */
	rcar_mipi_dsi_clr(mipi_dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);
	rcar_mipi_dsi_set(mipi_dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);
	rcar_mipi_dsi_clr(mipi_dsi, CLOCKSET1, CLOCKSET1_SHADOW_CLEAR);

	clockset2 = CLOCKSET2_M(setup_info.m) | CLOCKSET2_N(setup_info.n) |
		    CLOCKSET2_VCO_CNTRL(setup_info.clk_setting.vco_cntrl);
	clockset3 = CLOCKSET3_PROP_CNTRL(setup_info.clk_setting.prop_cntrl) |
		    CLOCKSET3_INT_CNTRL(setup_info.clk_setting.int_cntrl) |
		    CLOCKSET3_CPBIAS_CNTRL(setup_info.clk_setting.cpbias_cntrl) |
		    CLOCKSET3_GMP_CNTRL(setup_info.clk_setting.gmp_cntrl);

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

	if (mipi_dsi->info->post_init_phtw)
		mipi_dsi->info->post_init_phtw(mipi_dsi);

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
	vclkset |= VCLKSET_COLOR_RGB | VCLKSET_DIV(setup_info.clk_setting.div) |
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

	mipi_dsi->clocks.dsi = rcar_mipi_dsi_get_clock(mipi_dsi, "dsi", true);
	if (IS_ERR(mipi_dsi->clocks.dsi))
		return PTR_ERR(mipi_dsi->clocks.dsi);

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

static const struct rcar_mipi_dsi_info rcar_mipi_dsi_info_r8a779a0 = {
	.m_offset = 0,
	.n_offset = 2,
	.freq_mul = 1,
};

static const struct rcar_mipi_dsi_info rcar_mipi_dsi_info_r8a779g0 = {
	.init_phtw = rcar_mipi_dsi_init_phtw_v4h,
	.post_init_phtw = rcar_mipi_dsi_post_init_phtw_v4h,
	.hsfeqrange_values = hsfreqrange_table_r8a779g0,
	.clkset_values = clockset_setting_table_r8a779g0,
	.m_offset = 0,
	.n_offset = 1,
	.freq_mul = 2,
};

static const struct of_device_id rcar_mipi_dsi_of_table[] = {
	{
		.compatible = "renesas,r8a779a0-mipi-dsi",
		.data = &rcar_mipi_dsi_info_r8a779a0,
	},
	{
		.compatible = "renesas,r8a779g0-mipi-dsi",
		.data = &rcar_mipi_dsi_info_r8a779g0,
	},
	{ /* sentinel */ },
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
