/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rcar_vcon_regs.h  --  R-Car Video Interface Converter Registers Definitions
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 *
 */

#ifndef __RCAR_VCON_REGS_H__
#define __RCAR_VCON_REGS_H__

#define START			0x0000
#define STOP			0x0004

#define IRQ_ENB			0x0010
#define IRQ_ENB_VSYNC		BIT(0)

#define IRQ_STA			0x0014
#define IRQ_STA_VSYNC		BIT(0)

#define HTOTAL			0x0030
#define VTOTAL			0x0034
#define HACT_START		0x0038
#define VACT_START		0x003c
#define HSYNC			0x0040
#define VSYNC			0x0044
#define AVW			0x0048
#define AVH			0x004c

#define PIX_CLK_NUME		0x0084
#define PIX_CLK_DENO		0x0088
#define NUME_MIN		3
#define NUME_MAX		99
#define DENO_MIN		20
#define DENO_MAX		1000

#define PIX_CLK_CTRL		0x0090
#define PIX_CLK_CTRL_DIV	0x01

#endif /* __RCAR_VCON_REGS_H__ */
