/*
 * rcar_lvds_regs.h  --  R-Car LVDS Interface Registers Definitions
 *
 * Copyright (C) 2013-2017 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#ifndef __RCAR_LVDS_REGS_H__
#define __RCAR_LVDS_REGS_H__

#define LVDCR0				0x0000
#define LVDCR0_DUSEL			(1 << 15)
#define LVDCR0_DMD			(1 << 12)		/* Gen2 only */
#define LVDCR0_LVMD_MASK		(0xf << 8)
#define LVDCR0_LVMD_SHIFT		8
#define LVDCR0_PLLON			(1 << 4)
#define LVDCR0_PWD			(1 << 2)		/* Gen3 only */
#define LVDCR0_BEN			(1 << 2)		/* Gen2 only */
#define LVDCR0_LVEN			(1 << 1)
#define LVDCR0_LVRES			(1 << 0)

#define LVDCR1				0x0004
#define LVDCR1_CKSEL			(1 << 15)		/* Gen2 only */
#define LVDCR1_CHSTBY_GEN2(n)		(3 << (2 + (n) * 2))	/* Gen2 only */
#define LVDCR1_CHSTBY_GEN3(n)		(3 << (2 + (n) * 2))	/* Gen3 only */
#define LVDCR1_CLKSTBY_GEN2		(3 << 0)		/* Gen2 only */
#define LVDCR1_CLKSTBY_GEN3		(3 << 0)		/* Gen3 only */

#define LVDPLLCR			0x0008
#define LVDPLLCR_CEEN			(1 << 14)
#define LVDPLLCR_FBEN			(1 << 13)
#define LVDPLLCR_COSEL			(1 << 12)
/* Gen2 */
#define LVDPLLCR_PLLDLYCNT_150M		(0x1bf << 0)
#define LVDPLLCR_PLLDLYCNT_121M		(0x22c << 0)
#define LVDPLLCR_PLLDLYCNT_60M		(0x77b << 0)
#define LVDPLLCR_PLLDLYCNT_38M		(0x69a << 0)
#define LVDPLLCR_PLLDLYCNT_MASK		(0x7ff << 0)
/* Gen3 */
#define LVDPLLCR_PLLDIVCNT_42M		(0x014cb << 0)
#define LVDPLLCR_PLLDIVCNT_85M		(0x00a45 << 0)
#define LVDPLLCR_PLLDIVCNT_128M		(0x006c3 << 0)
#define LVDPLLCR_PLLDIVCNT_148M		(0x046c1 << 0)
#define LVDPLLCR_PLLDIVCNT_MASK		(0x7ffff << 0)

#define LVDPLLCR_PLLON			(1 << 22)
#define LVDPLLCR_PLLSEL_PLL0		(0 << 20)
#define LVDPLLCR_PLLSEL_LVX		(1 << 20)
#define LVDPLLCR_PLLSEL_PLL1		(2 << 20)
#define LVDPLLCR_CKSEL_LVX		(1 << 17)
#define LVDPLLCR_CKSEL_EXTAL		(3 << 17)
#define LVDPLLCR_CKSEL_DU_DOTCLKIN(n)	((5 + (n) * 2) << 17)
#define LVDPLLCR_OCKSEL_7		(0 << 16)
#define LVDPLLCR_OCKSEL_NOT_DIVIDED	(1 << 16)
#define LVDPLLCR_STP_CLKOUTE_DIS	(0 << 14)
#define LVDPLLCR_STP_CLKOUTE_EN		(1 << 14)
#define LVDPLLCR_OUTCLKSEL_BEFORE	(0 << 12)
#define LVDPLLCR_OUTCLKSEL_AFTER	(1 << 12)
#define LVDPLLCR_CLKOUT_DISABLE		(0 << 11)
#define LVDPLLCR_CLKOUT_ENABLE		(1 << 11)

#define LVDCTRCR			0x000c
#define LVDCTRCR_CTR3SEL_ZERO		(0 << 12)
#define LVDCTRCR_CTR3SEL_ODD		(1 << 12)
#define LVDCTRCR_CTR3SEL_CDE		(2 << 12)
#define LVDCTRCR_CTR3SEL_MASK		(7 << 12)
#define LVDCTRCR_CTR2SEL_DISP		(0 << 8)
#define LVDCTRCR_CTR2SEL_ODD		(1 << 8)
#define LVDCTRCR_CTR2SEL_CDE		(2 << 8)
#define LVDCTRCR_CTR2SEL_HSYNC		(3 << 8)
#define LVDCTRCR_CTR2SEL_VSYNC		(4 << 8)
#define LVDCTRCR_CTR2SEL_MASK		(7 << 8)
#define LVDCTRCR_CTR1SEL_VSYNC		(0 << 4)
#define LVDCTRCR_CTR1SEL_DISP		(1 << 4)
#define LVDCTRCR_CTR1SEL_ODD		(2 << 4)
#define LVDCTRCR_CTR1SEL_CDE		(3 << 4)
#define LVDCTRCR_CTR1SEL_HSYNC		(4 << 4)
#define LVDCTRCR_CTR1SEL_MASK		(7 << 4)
#define LVDCTRCR_CTR0SEL_HSYNC		(0 << 0)
#define LVDCTRCR_CTR0SEL_VSYNC		(1 << 0)
#define LVDCTRCR_CTR0SEL_DISP		(2 << 0)
#define LVDCTRCR_CTR0SEL_ODD		(3 << 0)
#define LVDCTRCR_CTR0SEL_CDE		(4 << 0)
#define LVDCTRCR_CTR0SEL_MASK		(7 << 0)

#define LVDCHCR				0x0010
#define LVDCHCR_CHSEL_CH(n, c)		((((c) - (n)) & 3) << ((n) * 4))
#define LVDCHCR_CHSEL_MASK(n)		(3 << ((n) * 4))

#define LVDSTRIPE			0x0014
#define LVDSTRIPE_ST_TRGSEL_DISP	(0 << 2)
#define LVDSTRIPE_ST_TRGSEL_HSYNC_R	(1 << 2)
#define LVDSTRIPE_ST_TRGSEL_HSYNC_F	(2 << 2)

#define LVDSTRIPE_ST_SWAP_NORMAL	(0 << 1)
#define LVDSTRIPE_ST_SWAP_SWAP		(1 << 1)
#define LVDSTRIPE_ST_ON			(1 << 0)

#define LVDSCR				0x0018
#define LVDSCR_DEPTH_DP1		(0 << 29)
#define LVDSCR_DEPTH_DP2		(1 << 29)
#define LVDSCR_DEPTH_DP3		(2 << 29)
#define LVDSCR_BANDSET_10KHZ_LESS_THAN	(1 << 28)
#define LVDSCR_SDIV_SR1			(0 << 22)
#define LVDSCR_SDIV_SR2			(1 << 22)
#define LVDSCR_SDIV_SR4			(2 << 22)
#define LVDSCR_SDIV_SR8			(3 << 22)
#define LVDSCR_MODE_DOWN		(1 << 21)
#define LVDSCR_RSTN_ENABLE		(1 << 20)

#define LVDDIV				0x001c
#define LVDDIV_DIVSEL			(1 << 8)
#define LVDDIV_DIVRESET			(1 << 7)
#define LVDDIV_DIVSTP			(1 << 6)

#endif /* __RCAR_LVDS_REGS_H__ */
