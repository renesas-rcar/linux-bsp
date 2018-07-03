/*
 * rcar_du_lvdsenc.c  --  R-Car Display Unit LVDS Encoder
 *
 * Copyright (C) 2013-2017 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_lvdsenc.h"
#include "rcar_lvds_regs.h"

struct rcar_du_lvdsenc {
	struct rcar_du_device *dev;
	struct reset_control *rstc;

	unsigned int index;
	void __iomem *mmio;
	struct clk *clock;
	bool enabled;

	enum rcar_lvds_input input;
	enum rcar_lvds_mode mode;
	enum rcar_lvds_link_mode link_mode;

	u32 lvdpllcr;
	u32 lvddiv;
};

struct pll_info {
	unsigned int pllclk;
	unsigned int diff;
	unsigned int clk_n;
	unsigned int clk_m;
	unsigned int clk_e;
	unsigned int div;
};

static void rcar_lvds_write(struct rcar_du_lvdsenc *lvds, u32 reg, u32 data)
{
	iowrite32(data, lvds->mmio + reg);
}

static u32 rcar_lvds_read(struct rcar_du_lvdsenc *lvds, u32 reg)
{
	return ioread32(lvds->mmio + reg);
}

static void rcar_du_lvdsenc_start_gen2(struct rcar_du_lvdsenc *lvds,
				       struct rcar_du_crtc *rcrtc)
{
	const struct drm_display_mode *mode = &rcrtc->crtc.mode;
	unsigned int freq = mode->clock;
	u32 lvdcr0;
	u32 pllcr;

	/* PLL clock configuration */
	if (freq < 39000)
		pllcr = LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_38M;
	else if (freq < 61000)
		pllcr = LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_60M;
	else if (freq < 121000)
		pllcr = LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_121M;
	else
		pllcr = LVDPLLCR_PLLDLYCNT_150M;

	rcar_lvds_write(lvds, LVDPLLCR, pllcr);

	/*
	 * Set the  LVDS mode, select the input, enable LVDS operation,
	 * and turn bias circuitry on.
	 */
	lvdcr0 = (lvds->mode << LVDCR0_LVMD_SHIFT) | LVDCR0_BEN | LVDCR0_LVEN;
	if (rcrtc->index == 2)
		lvdcr0 |= LVDCR0_DUSEL;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	/* Turn all the channels on. */
	rcar_lvds_write(lvds, LVDCR1,
			LVDCR1_CHSTBY_GEN2(3) | LVDCR1_CHSTBY_GEN2(2) |
			LVDCR1_CHSTBY_GEN2(1) | LVDCR1_CHSTBY_GEN2(0) |
			LVDCR1_CLKSTBY_GEN2);

	/*
	 * Turn the PLL on, wait for the startup delay, and turn the output
	 * on.
	 */
	lvdcr0 |= LVDCR0_PLLON;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	usleep_range(100, 150);

	lvdcr0 |= LVDCR0_LVRES;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);
}

static void rcar_du_lvdsenc_start_gen3(struct rcar_du_lvdsenc *lvds,
				       struct rcar_du_crtc *rcrtc)
{
	const struct drm_display_mode *mode = &rcrtc->crtc.mode;
	unsigned int freq = mode->clock;
	u32 lvdcr0;
	u32 pllcr;

	/* PLL clock configuration */
	if (freq < 42000)
		pllcr = LVDPLLCR_PLLDIVCNT_42M;
	else if (freq < 85000)
		pllcr = LVDPLLCR_PLLDIVCNT_85M;
	else if (freq < 128000)
		pllcr = LVDPLLCR_PLLDIVCNT_128M;
	else
		pllcr = LVDPLLCR_PLLDIVCNT_148M;

	rcar_lvds_write(lvds, LVDPLLCR, pllcr);

	lvdcr0 = lvds->mode << LVDCR0_LVMD_SHIFT;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	/* Turn all the channels on. */
	rcar_lvds_write(lvds, LVDCR1,
			LVDCR1_CHSTBY_GEN3(3) | LVDCR1_CHSTBY_GEN3(2) |
			LVDCR1_CHSTBY_GEN3(1) | LVDCR1_CHSTBY_GEN3(0) |
			LVDCR1_CLKSTBY_GEN3);

	/*
	 * Turn the PLL on, set it to LVDS normal mode, wait for the startup
	 * delay and turn the output on.
	 */

	lvdcr0 |= LVDCR0_PLLON;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	lvdcr0 |= LVDCR0_PWD;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	usleep_range(100, 150);

	lvdcr0 |= LVDCR0_LVRES;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);
}

static int rcar_du_lvdsenc_start(struct rcar_du_lvdsenc *lvds,
				 struct rcar_du_crtc *rcrtc)
{
	u32 lvdhcr;
	int ret;

	if (lvds->enabled)
		return 0;

	reset_control_deassert(lvds->rstc);

	ret = clk_prepare_enable(lvds->clock);
	if (ret < 0)
		return ret;

	/*
	 * Hardcode the channels and control signals routing for now.
	 *
	 * HSYNC -> CTRL0
	 * VSYNC -> CTRL1
	 * DISP  -> CTRL2
	 * 0     -> CTRL3
	 */
	rcar_lvds_write(lvds, LVDCTRCR, LVDCTRCR_CTR3SEL_ZERO |
			LVDCTRCR_CTR2SEL_DISP | LVDCTRCR_CTR1SEL_VSYNC |
			LVDCTRCR_CTR0SEL_HSYNC);

	if (rcar_du_needs(lvds->dev, RCAR_DU_QUIRK_LVDS_LANES))
		lvdhcr = LVDCHCR_CHSEL_CH(0, 0) | LVDCHCR_CHSEL_CH(1, 3)
		       | LVDCHCR_CHSEL_CH(2, 2) | LVDCHCR_CHSEL_CH(3, 1);
	else
		lvdhcr = LVDCHCR_CHSEL_CH(0, 0) | LVDCHCR_CHSEL_CH(1, 1)
		       | LVDCHCR_CHSEL_CH(2, 2) | LVDCHCR_CHSEL_CH(3, 3);

	rcar_lvds_write(lvds, LVDCHCR, lvdhcr);

	/* Perform generation-specific initialization. */
	if (lvds->dev->info->gen < 3)
		rcar_du_lvdsenc_start_gen2(lvds, rcrtc);
	else
		rcar_du_lvdsenc_start_gen3(lvds, rcrtc);

	lvds->enabled = true;

	return 0;
}

static void rcar_du_lvdsenc_dual_mode(struct rcar_du_lvdsenc *lvds0,
				      struct rcar_du_lvdsenc *lvds1,
				      struct rcar_du_crtc *rcrtc)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;
	u32 lvdcr0 = 0, lvdcr1 = 0, lvdhcr;

	lvdhcr = LVDCHCR_CHSEL_CH(0, 0) | LVDCHCR_CHSEL_CH(1, 1) |
		 LVDCHCR_CHSEL_CH(2, 2) | LVDCHCR_CHSEL_CH(3, 3);

	rcar_lvds_write(lvds0, LVDCTRCR, LVDCTRCR_CTR3SEL_ZERO |
			LVDCTRCR_CTR2SEL_DISP | LVDCTRCR_CTR1SEL_VSYNC |
			LVDCTRCR_CTR0SEL_HSYNC);
	rcar_lvds_write(lvds0, LVDCHCR, lvdhcr);
	rcar_lvds_write(lvds0, LVDSTRIPE, LVDSTRIPE_ST_ON);

	rcar_lvds_write(lvds1, LVDCTRCR, LVDCTRCR_CTR3SEL_ZERO |
			LVDCTRCR_CTR2SEL_DISP | LVDCTRCR_CTR1SEL_VSYNC |
			LVDCTRCR_CTR0SEL_HSYNC);
	rcar_lvds_write(lvds1, LVDCHCR, lvdhcr);
	rcar_lvds_write(lvds1, LVDSTRIPE, LVDSTRIPE_ST_ON);

	/* Turn all the channels on. */
	rcar_lvds_write(lvds0, LVDCR1,
			LVDCR1_CHSTBY_GEN3(3) | LVDCR1_CHSTBY_GEN3(2) |
			LVDCR1_CHSTBY_GEN3(1) | LVDCR1_CHSTBY_GEN3(0) |
			LVDCR1_CLKSTBY_GEN3);
	rcar_lvds_write(lvds1, LVDCR1,
			LVDCR1_CHSTBY_GEN3(3) | LVDCR1_CHSTBY_GEN3(2) |
			LVDCR1_CHSTBY_GEN3(1) | LVDCR1_CHSTBY_GEN3(0) |
			LVDCR1_CLKSTBY_GEN3);

	/*
	 * Turn the PLL on, set it to LVDS normal mode, wait for the startup
	 * delay and turn the output on.
	 */
	if (rcar_du_has(rcdu, RCAR_DU_FEATURE_R8A77995_REGS)) {
		lvdcr0 |= LVDCR0_PWD;
		rcar_lvds_write(lvds0, LVDCR0, lvdcr0);

		lvdcr1 |= LVDCR0_PWD;
		rcar_lvds_write(lvds1, LVDCR0, lvdcr1);

		lvdcr1 |= LVDCR0_LVEN | LVDCR0_LVRES;
		rcar_lvds_write(lvds1, LVDCR0, lvdcr1);

		lvdcr0 |= LVDCR0_LVEN | LVDCR0_LVRES;
		rcar_lvds_write(lvds0, LVDCR0, lvdcr0);

		return;
	}

	lvdcr0 |= LVDCR0_LVEN;
	rcar_lvds_write(lvds0, LVDCR0, lvdcr0);

	lvdcr1 |= LVDCR0_LVEN;
	rcar_lvds_write(lvds1, LVDCR0, lvdcr1);

	lvdcr1 |= LVDCR0_LVRES;
	rcar_lvds_write(lvds1, LVDCR0, lvdcr1);

	lvdcr0 |= LVDCR0_LVRES;
	rcar_lvds_write(lvds0, LVDCR0, lvdcr0);
}

static void rcar_du_lvdsenc_pll_calc(struct rcar_du_crtc *rcrtc,
				     struct pll_info *pll, unsigned int in_fre,
				     unsigned int mode_freq, bool edivider)
{
	unsigned long diff = (unsigned long)-1;
	unsigned long fout, fpfd, fvco, n, m, e, div;
	bool clk_diff_set = true;

	if (in_fre < 12000000 || in_fre > 192000000)
		return;

	for (n = 0; n < 127; n++) {
		if (((n + 1) < 60) || ((n + 1) > 120))
			continue;

		for (m = 0; m < 7; m++) {
			for (e = 0; e < 1; e++) {
				if (edivider)
					fout = (((in_fre / 1000) * (n + 1)) /
						((m + 1) * (e + 1) * 2)) *
						1000;
				else
					fout = (((in_fre / 1000) * (n + 1)) /
						(m + 1)) * 1000;

				if (fout > 1039500000)
					continue;

				fpfd  = (in_fre / (m + 1));
				if (fpfd < 12000000 || fpfd > 24000000)
					continue;

				fvco  = (((in_fre / 1000) * (n + 1)) / (m + 1))
					 * 1000;
				if (fvco < 900000000 || fvco > 1800000000)
					continue;

				fout = fout / 7; /* 7 divider */

				for (div = 0; div < 64; div++) {
					diff = abs((long)(fout / (div + 1)) -
					       (long)mode_freq);

					if (clk_diff_set ||
					    (diff == 0 ||
					    pll->diff > diff)) {
						pll->diff = diff;
						pll->clk_n = n;
						pll->clk_m = m;
						pll->clk_e = e;
						pll->pllclk = fout;
						pll->div = div;

						clk_diff_set = false;

						if (diff == 0)
							goto done;
					}
				}
			}
		}
	}
done:
	return;
}

void rcar_du_lvdsenc_pll_pre_start(struct rcar_du_lvdsenc *lvds,
				   struct rcar_du_crtc *rcrtc)
{
	struct rcar_du_device *rcdu = lvds->dev;
	const struct drm_display_mode *mode =
				&rcrtc->crtc.state->adjusted_mode;
	unsigned int mode_freq = mode->clock * 1000;
	unsigned int ext_clk = 0;
	struct pll_info *lvds_pll[2];
	struct rcar_du_lvdsenc *lvds_wk0 = rcdu->lvds[0];
	struct rcar_du_lvdsenc *lvds_wk1 = rcdu->lvds[1];
	struct rcar_du_lvdsenc *lvds_wk = rcdu->lvds[lvds->index];
	int i, ret;
	u32 clksel, cksel;

	if (rcrtc->extclock)
		ext_clk = clk_get_rate(rcrtc->extclock);
	else
		dev_warn(rcdu->dev, "external clock is not set\n");

	dev_dbg(rcrtc->group->dev->dev, "external clock %d Hz\n", ext_clk);

	if (lvds_wk0->enabled && lvds->index == 0)
		return;

	if (lvds_wk1->enabled && lvds->index == 1)
		return;

	lvds->enabled = true;

	for (i = 0; i < RCAR_DU_LVDS_EDIVIDER; i++) {
		lvds_pll[i] = kzalloc(sizeof(*lvds_pll), GFP_KERNEL);
		if (!lvds_pll[i])
			return;
	}

	/* software reset release */
	reset_control_deassert(lvds->rstc);
	ret = clk_prepare_enable(lvds->clock);
	if (ret < 0)
		goto end;

	for (i = 0; i < RCAR_DU_LVDS_EDIVIDER; i++) {
		bool edivider;

		if (i == 0)
			edivider = true;
		else
			edivider = false;

		rcar_du_lvdsenc_pll_calc(rcrtc, lvds_pll[i], ext_clk,
					 mode_freq, edivider);
	}

	dev_dbg(rcrtc->group->dev->dev, "mode_frequency %d Hz\n", mode_freq);

	if (lvds_pll[1]->diff >= lvds_pll[0]->diff) {
		/* use E-edivider */
		i = 0;
		clksel = LVDPLLCR_OUTCLKSEL_AFTER |
			 LVDPLLCR_STP_CLKOUTE_EN;
	} else {
		/* not use E-divider */
		i = 1;
		clksel = LVDPLLCR_OUTCLKSEL_BEFORE |
			 LVDPLLCR_STP_CLKOUTE_DIS;
	}
	dev_dbg(rcrtc->group->dev->dev,
		"E-divider %s\n", (i == 0 ? "is used" : "is not used"));

	dev_dbg(rcrtc->group->dev->dev,
		"pllclk:%u, n:%u, m:%u, e:%u, diff:%u, div:%u\n",
		 lvds_pll[i]->pllclk, lvds_pll[i]->clk_n, lvds_pll[i]->clk_m,
		 lvds_pll[i]->clk_e, lvds_pll[i]->diff, lvds_pll[i]->div);

	if (rcrtc->extal_use)
		cksel = LVDPLLCR_CKSEL_EXTAL;
	else
		cksel = LVDPLLCR_CKSEL_DU_DOTCLKIN(rcrtc->index);

	lvds_wk->lvdpllcr = (LVDPLLCR_PLLON |
		LVDPLLCR_OCKSEL_7 | clksel | LVDPLLCR_CLKOUT_ENABLE |
		cksel | (lvds_pll[i]->clk_e << 10) |
		(lvds_pll[i]->clk_n << 3) | lvds_pll[i]->clk_m);

	if (lvds_pll[i]->div > 0)
		lvds_wk->lvddiv = (LVDDIV_DIVSEL | LVDDIV_DIVRESET |
				  lvds_pll[i]->div);
	else
		lvds_wk->lvddiv = 0;

	rcar_lvds_write(lvds_wk, LVDPLLCR, lvds_wk->lvdpllcr);
	/* SSC function = off */

	usleep_range(200, 250);		/* Wait 200us until pll-lock */

	rcar_lvds_write(lvds_wk, LVDDIV, lvds_wk->lvddiv);

	dev_dbg(rcrtc->group->dev->dev, "LVDPLLCR: 0x%x\n",
		rcar_lvds_read(lvds, LVDPLLCR));
	dev_dbg(rcrtc->group->dev->dev, "LVDDIV: 0x%x\n",
		rcar_lvds_read(lvds, LVDDIV));

	if (lvds->link_mode == RCAR_LVDS_DUAL)
		rcar_du_lvdsenc_dual_mode(rcdu->lvds[0], rcdu->lvds[1],
					  rcrtc);
end:
	for (i = 0; i < RCAR_DU_LVDS_EDIVIDER; i++)
		kfree(lvds_pll[i]);
}

static int rcar_du_lvdsenc_pll_start(struct rcar_du_lvdsenc *lvds,
				     struct rcar_du_crtc *rcrtc)
{
	struct rcar_du_device *rcdu = rcrtc->group->dev;
	u32 lvdhcr, lvdcr0;

	rcar_lvds_write(lvds, LVDCTRCR, LVDCTRCR_CTR3SEL_ZERO |
			LVDCTRCR_CTR2SEL_DISP | LVDCTRCR_CTR1SEL_VSYNC |
			LVDCTRCR_CTR0SEL_HSYNC);

	lvdhcr = LVDCHCR_CHSEL_CH(0, 0) | LVDCHCR_CHSEL_CH(1, 1) |
		 LVDCHCR_CHSEL_CH(2, 2) | LVDCHCR_CHSEL_CH(3, 3);
	rcar_lvds_write(lvds, LVDCHCR, lvdhcr);

	rcar_lvds_write(lvds, LVDSTRIPE, 0);

	lvdcr0 = (lvds->mode << LVDCR0_LVMD_SHIFT);
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	/* Turn all the channels on. */
	rcar_lvds_write(lvds, LVDCR1,
			LVDCR1_CHSTBY_GEN3(3) | LVDCR1_CHSTBY_GEN3(2) |
			LVDCR1_CHSTBY_GEN3(1) | LVDCR1_CHSTBY_GEN3(0) |
			LVDCR1_CLKSTBY_GEN3);
	/*
	 * Turn the PLL on, set it to LVDS normal mode, wait for the startup
	 * delay and turn the output on.
	 */
	if (rcar_du_has(rcdu, RCAR_DU_FEATURE_R8A77995_REGS)) {
		lvdcr0 |= LVDCR0_PWD;
		rcar_lvds_write(lvds, LVDCR0, lvdcr0);

		lvdcr0 |= LVDCR0_LVEN | LVDCR0_LVRES;
		rcar_lvds_write(lvds, LVDCR0, lvdcr0);

		goto done;
	}

	lvdcr0 |= LVDCR0_LVEN;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	lvdcr0 |= LVDCR0_LVRES;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

done:
	lvds->enabled = true;

	return 0;
}

void __rcar_du_lvdsenc_stop(struct rcar_du_lvdsenc *lvds)
{
	struct rcar_du_device *rcdu = lvds->dev;

	if (lvds->link_mode == RCAR_LVDS_DUAL) {
		struct rcar_du_lvdsenc *lvds_wk0 = rcdu->lvds[0];
		struct rcar_du_lvdsenc *lvds_wk1 = rcdu->lvds[1];
		struct rcar_du_lvdsenc *lvds1 = rcdu->lvds[1];

		if (lvds_wk0->enabled || lvds_wk1->enabled) {
			if (lvds->index == 0)
				lvds_wk0->enabled = false;
			else if (lvds->index == 1)
				lvds_wk1->enabled = false;
			return;
		}
		/* Back up LVDS1 register. */
		lvds->lvdpllcr = rcar_lvds_read(lvds, LVDPLLCR);
		lvds->lvddiv = rcar_lvds_read(lvds, LVDDIV);
		lvds1->lvdpllcr = rcar_lvds_read(lvds1, LVDPLLCR);
		lvds1->lvddiv = rcar_lvds_read(lvds1, LVDDIV);

		rcar_lvds_write(lvds, LVDCR0, 0);
		rcar_lvds_write(lvds1, LVDCR0, 0);
		rcar_lvds_write(lvds, LVDCR1, 0);
		rcar_lvds_write(lvds1, LVDCR1, 0);
		rcar_lvds_write(lvds, LVDPLLCR, 0);
		rcar_lvds_write(lvds1, LVDPLLCR, 0);
	} else {
		u32 lvdcr0 = 0;

		lvdcr0 = rcar_lvds_read(lvds, LVDCR0) & ~LVDCR0_LVRES;
		rcar_lvds_write(lvds, LVDCR0, lvdcr0);

		if (rcar_du_has(rcdu, RCAR_DU_FEATURE_R8A77995_REGS) ||
		    rcar_du_has(rcdu, RCAR_DU_FEATURE_R8A77990_REGS)) {
			lvdcr0 = rcar_lvds_read(lvds, LVDCR0) & ~LVDCR0_LVEN;
			rcar_lvds_write(lvds, LVDCR0, lvdcr0);
		}

		if (!rcar_du_has(rcdu, RCAR_DU_FEATURE_R8A77990_REGS)) {
			lvdcr0 = rcar_lvds_read(lvds, LVDCR0) & ~LVDCR0_PWD;
			rcar_lvds_write(lvds, LVDCR0, lvdcr0);
		}

		lvdcr0 = rcar_lvds_read(lvds, LVDCR0) & ~LVDCR0_PLLON;
		rcar_lvds_write(lvds, LVDCR0, lvdcr0);

		rcar_lvds_write(lvds, LVDCR1, 0);
		rcar_lvds_write(lvds, LVDPLLCR, 0);
	}

	clk_disable_unprepare(lvds->clock);

	reset_control_assert(lvds->rstc);

	lvds->enabled = false;
}

bool rcar_du_lvdsenc_stop_pll(struct rcar_du_lvdsenc *lvds)
{
	struct rcar_du_device *rcdu;

	if (!lvds)
		return false;

	rcdu = lvds->dev;

	if (rcar_du_has(rcdu, RCAR_DU_FEATURE_R8A77965_REGS) &&
	    lvds->link_mode == RCAR_LVDS_DUAL) {
		int ret;

		ret = rcar_lvds_read(lvds, LVDPLLCR);
		if (!ret)
			return true;
	}

	return false;
}

static void rcar_du_lvdsenc_stop(struct rcar_du_lvdsenc *lvds)
{
	struct rcar_du_device *rcdu = lvds->dev;

	if (!lvds->enabled || rcar_du_has(rcdu, RCAR_DU_FEATURE_LVDS_PLL))
		return;

	__rcar_du_lvdsenc_stop(lvds);
}

int rcar_du_lvdsenc_enable(struct rcar_du_lvdsenc *lvds, struct drm_crtc *crtc,
			   bool enable)
{
	struct rcar_du_device *rcdu = lvds->dev;

	if (!enable) {
		rcar_du_lvdsenc_stop(lvds);
		return 0;
	} else if (crtc) {
		struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);
		if (rcar_du_has(rcdu, RCAR_DU_FEATURE_LVDS_PLL)) {
			if (lvds->link_mode == RCAR_LVDS_DUAL)
				rcar_du_lvdsenc_pll_pre_start(lvds, rcrtc);
			else
				return rcar_du_lvdsenc_pll_start(lvds, rcrtc);
		} else {
			return rcar_du_lvdsenc_start(lvds, rcrtc);
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

void rcar_du_lvdsenc_atomic_check(struct rcar_du_lvdsenc *lvds,
				  struct drm_display_mode *mode)
{
	struct rcar_du_device *rcdu = lvds->dev;

	/*
	 * The internal LVDS encoder has a restricted clock frequency operating
	 * range (30MHz to 150MHz on Gen2, 25.175MHz to 148.5MHz on Gen3). Clamp
	 * the clock accordingly.
	 */
	if (rcdu->info->gen < 3)
		mode->clock = clamp(mode->clock, 30000, 150000);
	else
		mode->clock = clamp(mode->clock, 25175, 148500);
}

void rcar_du_lvdsenc_set_mode(struct rcar_du_lvdsenc *lvds,
			      enum rcar_lvds_mode mode)
{
	lvds->mode = mode;
}

static int rcar_du_lvdsenc_get_resources(struct rcar_du_lvdsenc *lvds,
					 struct platform_device *pdev)
{
	struct resource *mem;
	char name[7];

	sprintf(name, "lvds.%u", lvds->index);

	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	lvds->mmio = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(lvds->mmio))
		return PTR_ERR(lvds->mmio);

	lvds->clock = devm_clk_get(&pdev->dev, name);
	if (IS_ERR(lvds->clock)) {
		dev_err(&pdev->dev, "failed to get clock for %s\n", name);
		return PTR_ERR(lvds->clock);
	}

	lvds->rstc = devm_reset_control_get(&pdev->dev, name);
	if (IS_ERR(lvds->rstc)) {
		dev_err(&pdev->dev, "failed to get cpg reset %s\n", name);
		return PTR_ERR(lvds->rstc);
	}

	return 0;
}

int rcar_du_lvdsenc_init(struct rcar_du_device *rcdu)
{
	struct platform_device *pdev = to_platform_device(rcdu->dev);
	struct rcar_du_lvdsenc *lvds;
	unsigned int i;
	int ret;
	struct device_node *np = rcdu->dev->of_node;
	const char *str;

	for (i = 0; i < rcdu->info->num_lvds; ++i) {
		lvds = devm_kzalloc(&pdev->dev, sizeof(*lvds), GFP_KERNEL);
		if (lvds == NULL)
			return -ENOMEM;

		lvds->dev = rcdu;
		lvds->index = i;
		lvds->input = i ? RCAR_LVDS_INPUT_DU1 : RCAR_LVDS_INPUT_DU0;
		lvds->enabled = false;

		if (!of_property_read_string(np, "mode", &str)) {
			if (!strcmp(str, "dual-link"))
				lvds->link_mode = RCAR_LVDS_DUAL;
			else
				lvds->link_mode = RCAR_LVDS_SINGLE;
		} else {
			lvds->link_mode = RCAR_LVDS_SINGLE;
		}

		ret = rcar_du_lvdsenc_get_resources(lvds, pdev);
		if (ret < 0)
			return ret;

		rcdu->lvds[i] = lvds;
	}

	return 0;
}
