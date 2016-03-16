/*
 * rcar_du_vsp.c  --  R-Car Display Unit VSP-Based Compositor
 *
 * Copyright (C) 2015-2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include <linux/soc/renesas/s2ram_ddr_backup.h>
#include <linux/of_platform.h>
#include <linux/videodev2.h>

#include <media/vsp1.h>

#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_vsp.h"

#ifdef CONFIG_RCAR_DDR_BACKUP
static struct hw_register vspd0_ip_regs[] = {
	{"VI6_CMD0",		0x00000, 32, 0},
	{"VI6_CMD1",		0x00004, 32, 0},
	{"VI6_CLK_CTRL0",	0x00010, 32, 0},
	{"VI6_CLK_CTRL1",	0x00014, 32, 0},
	{"VI6_CLK_DCSWT",	0x00018, 32, 0},
	{"VI6_CLK_DCSM0",	0x0001C, 32, 0},
	{"VI6_CLK_DCSM1",	0x00020, 32, 0},
	{"VI6_WPF0_IRQ_ENB",	0x00048, 32, 0},
	{"VI6_WPF1_IRQ_ENB",	0x00054, 32, 0},
	{"VI6_DISP_IRQ_ENB",	0x00078, 32, 0},
	{"VI6_DL_CTRL",		0x00100, 32, 0},
	{"VI6_DL_HDR_ADDR0",	0x00104, 32, 0},
	{"VI6_DL_HDR_ADDR1",	0x00108, 32, 0},
	{"VI6_DL_SWAP",		0x00114, 32, 0},
	{"VI6_DL_EXT_CTRL",	0x0011C, 32, 0},
	{"VI6_DL_BODY_SIZE0",	0x00120, 32, 0},
	{"VI6_DL_HDR_REF_ADDR0",	0x00130, 32, 0},
	{"VI6_DL_HDR_REF_ADDR1",	0x00134, 32, 0},
	{"VI6_RPF0_SRC_BSIZE",	0x00300, 32, 0},
	{"VI6_RPF0_SRC_ESIZE",	0x00304, 32, 0},
	{"VI6_RPF0_INFMT",	0x00308, 32, 0},
	{"VI6_RPF0_DSWAP",	0x0030C, 32, 0},
	{"VI6_RPF0_LOC",	0x00310, 32, 0},
	{"VI6_RPF0_ALPH_SEL",	0x00314, 32, 0},
	{"VI6_RPF0_VRTCOL_SET",	0x00318, 32, 0},
	{"VI6_RPF0_MSKCTRL",	0x0031C, 32, 0},
	{"VI6_RPF0_MSKSET0",	0x00320, 32, 0},
	{"VI6_RPF0_MSKSET1",	0x00324, 32, 0},
	{"VI6_RPF0_CKEY_CTRL",	0x00328, 32, 0},
	{"VI6_RPF0_CKEY_SETM0",	0x0032C, 32, 0},
	{"VI6_RPF0_CKEY_SETM1",	0x00330, 32, 0},
	{"VI6_RPF0_SRCM_PSTRIDE",	0x00334, 32, 0},
	{"VI6_RPF0_SRCM_ASTRIDE",	0x00338, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_Y",	0x0033C, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_C0",	0x00340, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_C1",	0x00344, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_AI",	0x00348, 32, 0},
	{"VI6_RPF0_MULT_ALPHA",	0x0036C, 32, 0},
	{"VI6_RPF1_SRC_BSIZE",	0x00400, 32, 0},
	{"VI6_RPF1_SRC_ESIZE",	0x00404, 32, 0},
	{"VI6_RPF1_INFMT",	0x00408, 32, 0},
	{"VI6_RPF1_DSWAP",	0x0040C, 32, 0},
	{"VI6_RPF1_LOC",	0x00410, 32, 0},
	{"VI6_RPF1_ALPH_SEL",	0x00414, 32, 0},
	{"VI6_RPF1_VRTCOL_SET",	0x00418, 32, 0},
	{"VI6_RPF1_MSKCTRL",	0x0041C, 32, 0},
	{"VI6_RPF1_MSKSET0",	0x00420, 32, 0},
	{"VI6_RPF1_MSKSET1",	0x00424, 32, 0},
	{"VI6_RPF1_CKEY_CTRL",	0x00428, 32, 0},
	{"VI6_RPF1_CKEY_SETM0",	0x0042C, 32, 0},
	{"VI6_RPF1_CKEY_SETM1",	0x00430, 32, 0},
	{"VI6_RPF1_SRCM_PSTRIDE",	0x00434, 32, 0},
	{"VI6_RPF1_SRCM_ASTRIDE",	0x00438, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_Y",	0x0043C, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_C0",	0x00440, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_C1",	0x00444, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_AI",	0x00448, 32, 0},
	{"VI6_RPF1_MULT_ALPHA",	0x0046C, 32, 0},
	{"VI6_RPF2_SRC_BSIZE",	0x00500, 32, 0},
	{"VI6_RPF2_SRC_ESIZE",	0x00504, 32, 0},
	{"VI6_RPF2_INFMT",	0x00508, 32, 0},
	{"VI6_RPF2_DSWAP",	0x0050C, 32, 0},
	{"VI6_RPF2_LOC",	0x00510, 32, 0},
	{"VI6_RPF2_ALPH_SEL",	0x00514, 32, 0},
	{"VI6_RPF2_VRTCOL_SET",	0x00518, 32, 0},
	{"VI6_RPF2_MSKCTRL",	0x0051C, 32, 0},
	{"VI6_RPF2_MSKSET0",	0x00520, 32, 0},
	{"VI6_RPF2_MSKSET1",	0x00524, 32, 0},
	{"VI6_RPF2_CKEY_CTRL",	0x00528, 32, 0},
	{"VI6_RPF2_CKEY_SETM0",	0x0052C, 32, 0},
	{"VI6_RPF2_CKEY_SETM1",	0x00530, 32, 0},
	{"VI6_RPF2_SRCM_PSTRIDE",	0x00534, 32, 0},
	{"VI6_RPF2_SRCM_ASTRIDE",	0x00538, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_Y",	0x0053C, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_C0",	0x00540, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_C1",	0x00544, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_AI",	0x00548, 32, 0},
	{"VI6_RPF2_MULT_ALPHA",	0x0056C, 32, 0},
	{"VI6_RPF3_SRC_BSIZE",	0x00600, 32, 0},
	{"VI6_RPF3_SRC_ESIZE",	0x00604, 32, 0},
	{"VI6_RPF3_INFMT",	0x00608, 32, 0},
	{"VI6_RPF3_DSWAP",	0x0060C, 32, 0},
	{"VI6_RPF3_LOC",	0x00610, 32, 0},
	{"VI6_RPF3_ALPH_SEL",	0x00614, 32, 0},
	{"VI6_RPF3_VRTCOL_SET",	0x00618, 32, 0},
	{"VI6_RPF3_MSKCTRL",	0x0061C, 32, 0},
	{"VI6_RPF3_MSKSET0",	0x00620, 32, 0},
	{"VI6_RPF3_MSKSET1",	0x00624, 32, 0},
	{"VI6_RPF3_CKEY_CTRL",	0x00628, 32, 0},
	{"VI6_RPF3_CKEY_SETM0",	0x0062C, 32, 0},
	{"VI6_RPF3_CKEY_SETM1",	0x00630, 32, 0},
	{"VI6_RPF3_SRCM_PSTRIDE",	0x00634, 32, 0},
	{"VI6_RPF3_SRCM_ASTRIDE",	0x00638, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_Y",	0x0063C, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_C0",	0x00640, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_C1",	0x00644, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_AI",	0x00648, 32, 0},
	{"VI6_RPF3_MULT_ALPHA",	0x0066C, 32, 0},
	{"VI6_RPF4_SRC_BSIZE",	0x00700, 32, 0},
	{"VI6_RPF4_SRC_ESIZE",	0x00704, 32, 0},
	{"VI6_RPF4_INFMT",	0x00708, 32, 0},
	{"VI6_RPF4_DSWAP",	0x0070C, 32, 0},
	{"VI6_RPF4_LOC",	0x00710, 32, 0},
	{"VI6_RPF4_ALPH_SEL",	0x00714, 32, 0},
	{"VI6_RPF4_VRTCOL_SET",	0x00718, 32, 0},
	{"VI6_RPF4_MSKCTRL",	0x0071C, 32, 0},
	{"VI6_RPF4_MSKSET0",	0x00720, 32, 0},
	{"VI6_RPF4_MSKSET1",	0x00724, 32, 0},
	{"VI6_RPF4_CKEY_CTRL",	0x00728, 32, 0},
	{"VI6_RPF4_CKEY_SETM0",	0x0072C, 32, 0},
	{"VI6_RPF4_CKEY_SETM1",	0x00730, 32, 0},
	{"VI6_RPF4_SRCM_PSTRIDE",	0x00734, 32, 0},
	{"VI6_RPF4_SRCM_ASTRIDE",	0x00738, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_Y",	0x0073C, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_C0",	0x00740, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_C1",	0x00744, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_AI",	0x00748, 32, 0},
	{"VI6_RPF4_MULT_ALPHA",	0x0076C, 32, 0},
	{"VI6_WPF0_SRCRPF",	0x01000, 32, 0},
	{"VI6_WPF0_HSZCLIP",	0x01004, 32, 0},
	{"VI6_WPF0_VSZCLIP",	0x01008, 32, 0},
	{"VI6_WPF0_OUTDMT",	0x0100C, 32, 0},
	{"VI6_WPF0_DSWAP",	0x01010, 32, 0},
	{"VI6_WPF0_RNDCTRL",	0x01014, 32, 0},
	{"VI6_WPF0_ROT_CTRL",	0x01018, 32, 0},
	{"VI6_WPF0_DSTM_STRIDE_Y",	0x0101C, 32, 0},
	{"VI6_WPF0_DSTM_STRIDE_C",	0x01020, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_Y",	0x01024, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_C0",	0x01028, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_C1",	0x0102C, 32, 0},
	{"VI6_WPF0_WRBCK_CTRL",	0x00134, 32, 0},
	{"VI6_WPF1_SRCRPF",	0x01100, 32, 0},
	{"VI6_WPF1_HSZCLIP",	0x01104, 32, 0},
	{"VI6_WPF1_VSZCLIP",	0x01108, 32, 0},
	{"VI6_WPF1_OUTDMT",	0x0110C, 32, 0},
	{"VI6_WPF1_DSWAP",	0x01110, 32, 0},
	{"VI6_WPF1_RNDCTRL",	0x01114, 32, 0},
	{"VI6_WPF1_ROT_CTRL",	0x01118, 32, 0},
	{"VI6_WPF1_DSTM_STRIDE_Y",	0x0111C, 32, 0},
	{"VI6_WPF1_DSTM_STRIDE_C",	0x01120, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_Y",	0x01124, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_C0",	0x01128, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_C1",	0x0112C, 32, 0},
	{"VI6_WPF1_WRBCK_CTRL",	0x01134, 32, 0},
	{"VI6_DPR_RPF0_ROUTE",	0x02000, 32, 0},
	{"VI6_DPR_RPF1_ROUTE",	0x02004, 32, 0},
	{"VI6_DPR_RPF2_ROUTE",	0x02008, 32, 0},
	{"VI6_DPR_RPF3_ROUTE",	0x0200C, 32, 0},
	{"VI6_DPR_RPF4_ROUTE",	0x02010, 32, 0},
	{"VI6_DPR_WPF0_FPORCH",	0x02014, 32, 0},
	{"VI6_DPR_WPF1_FPORCH",	0x02018, 32, 0},
	{"VI6_DPR_UIF4_ROUTE",	0x02074, 32, 0},
	{"VI6_DPR_BRU_ROUTE",	0x0204C, 32, 0},
	{"VI6_BRU_INCTRL",	0x02C00, 32, 0},
	{"VI6_BRU_VIRRPF_SIZE",	0x02C04, 32, 0},
	{"VI6_BRU_VIRRPF_LOC",	0x02C08, 32, 0},
	{"VI6_BRU_VIRRPF_COL",	0x02C0C, 32, 0},
	{"VI6_BRUA_CTRL",	0x02C10, 32, 0},
	{"VI6_BRUB_CTRL",	0x02C18, 32, 0},
	{"VI6_BRUC_CTRL",	0x02C20, 32, 0},
	{"VI6_BRUD_CTRL",	0x02C28, 32, 0},
	{"VI6_BRUE_CTRL",	0x02C34, 32, 0},
	{"VI6_BRUA_BLD",	0x02C14, 32, 0},
	{"VI6_BRUB_BLD",	0x02C1C, 32, 0},
	{"VI6_BRUC_BLD",	0x02C24, 32, 0},
	{"VI6_BRUD_BLD",	0x02C2C, 32, 0},
	{"VI6_BRUE_BLD",	0x02C38, 32, 0},
	{"VI6_BRU_ROP",		0x02C30, 32, 0},
};

static struct rcar_ip vspd0_ip = {
	.ip_name = "VSPD0",
	.base_addr = 0xFEA20000,
	.size = 0x3000,
	.reg_count = ARRAY_SIZE(vspd0_ip_regs),
	.ip_reg = vspd0_ip_regs,
};

static struct hw_register vspd1_ip_regs[] = {
	{"VI6_CMD0",		0x00000, 32, 0},
	{"VI6_CMD1",		0x00004, 32, 0},
	{"VI6_CLK_CTRL0",	0x00010, 32, 0},
	{"VI6_CLK_CTRL1",	0x00014, 32, 0},
	{"VI6_CLK_DCSWT",	0x00018, 32, 0},
	{"VI6_CLK_DCSM0",	0x0001C, 32, 0},
	{"VI6_CLK_DCSM1",	0x00020, 32, 0},
	{"VI6_WPF0_IRQ_ENB",	0x00048, 32, 0},
	{"VI6_WPF1_IRQ_ENB",	0x00054, 32, 0},
	{"VI6_DISP_IRQ_ENB",	0x00078, 32, 0},
	{"VI6_DL_CTRL",		0x00100, 32, 0},
	{"VI6_DL_HDR_ADDR0",	0x00104, 32, 0},
	{"VI6_DL_HDR_ADDR1",	0x00108, 32, 0},
	{"VI6_DL_SWAP",		0x00114, 32, 0},
	{"VI6_DL_EXT_CTRL",	0x0011C, 32, 0},
	{"VI6_DL_BODY_SIZE0",	0x00120, 32, 0},
	{"VI6_DL_HDR_REF_ADDR0",	0x00130, 32, 0},
	{"VI6_DL_HDR_REF_ADDR1",	0x00134, 32, 0},
	{"VI6_RPF0_SRC_BSIZE",	0x00300, 32, 0},
	{"VI6_RPF0_SRC_ESIZE",	0x00304, 32, 0},
	{"VI6_RPF0_INFMT",	0x00308, 32, 0},
	{"VI6_RPF0_DSWAP",	0x0030C, 32, 0},
	{"VI6_RPF0_LOC",	0x00310, 32, 0},
	{"VI6_RPF0_ALPH_SEL",	0x00314, 32, 0},
	{"VI6_RPF0_VRTCOL_SET",	0x00318, 32, 0},
	{"VI6_RPF0_MSKCTRL",	0x0031C, 32, 0},
	{"VI6_RPF0_MSKSET0",	0x00320, 32, 0},
	{"VI6_RPF0_MSKSET1",	0x00324, 32, 0},
	{"VI6_RPF0_CKEY_CTRL",	0x00328, 32, 0},
	{"VI6_RPF0_CKEY_SETM0",	0x0032C, 32, 0},
	{"VI6_RPF0_CKEY_SETM1",	0x00330, 32, 0},
	{"VI6_RPF0_SRCM_PSTRIDE",	0x00334, 32, 0},
	{"VI6_RPF0_SRCM_ASTRIDE",	0x00338, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_Y",	0x0033C, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_C0",	0x00340, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_C1",	0x00344, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_AI",	0x00348, 32, 0},
	{"VI6_RPF0_MULT_ALPHA",	0x0036C, 32, 0},
	{"VI6_RPF1_SRC_BSIZE",	0x00400, 32, 0},
	{"VI6_RPF1_SRC_ESIZE",	0x00404, 32, 0},
	{"VI6_RPF1_INFMT",	0x00408, 32, 0},
	{"VI6_RPF1_DSWAP",	0x0040C, 32, 0},
	{"VI6_RPF1_LOC",	0x00410, 32, 0},
	{"VI6_RPF1_ALPH_SEL",	0x00414, 32, 0},
	{"VI6_RPF1_VRTCOL_SET",	0x00418, 32, 0},
	{"VI6_RPF1_MSKCTRL",	0x0041C, 32, 0},
	{"VI6_RPF1_MSKSET0",	0x00420, 32, 0},
	{"VI6_RPF1_MSKSET1",	0x00424, 32, 0},
	{"VI6_RPF1_CKEY_CTRL",	0x00428, 32, 0},
	{"VI6_RPF1_CKEY_SETM0",	0x0042C, 32, 0},
	{"VI6_RPF1_CKEY_SETM1",	0x00430, 32, 0},
	{"VI6_RPF1_SRCM_PSTRIDE",	0x00434, 32, 0},
	{"VI6_RPF1_SRCM_ASTRIDE",	0x00438, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_Y",	0x0043C, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_C0",	0x00440, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_C1",	0x00444, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_AI",	0x00448, 32, 0},
	{"VI6_RPF1_MULT_ALPHA",	0x0046C, 32, 0},
	{"VI6_RPF2_SRC_BSIZE",	0x00500, 32, 0},
	{"VI6_RPF2_SRC_ESIZE",	0x00504, 32, 0},
	{"VI6_RPF2_INFMT",	0x00508, 32, 0},
	{"VI6_RPF2_DSWAP",	0x0050C, 32, 0},
	{"VI6_RPF2_LOC",	0x00510, 32, 0},
	{"VI6_RPF2_ALPH_SEL",	0x00514, 32, 0},
	{"VI6_RPF2_VRTCOL_SET",	0x00518, 32, 0},
	{"VI6_RPF2_MSKCTRL",	0x0051C, 32, 0},
	{"VI6_RPF2_MSKSET0",	0x00520, 32, 0},
	{"VI6_RPF2_MSKSET1",	0x00524, 32, 0},
	{"VI6_RPF2_CKEY_CTRL",	0x00528, 32, 0},
	{"VI6_RPF2_CKEY_SETM0",	0x0052C, 32, 0},
	{"VI6_RPF2_CKEY_SETM1",	0x00530, 32, 0},
	{"VI6_RPF2_SRCM_PSTRIDE",	0x00534, 32, 0},
	{"VI6_RPF2_SRCM_ASTRIDE",	0x00538, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_Y",	0x0053C, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_C0",	0x00540, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_C1",	0x00544, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_AI",	0x00548, 32, 0},
	{"VI6_RPF2_MULT_ALPHA",	0x0056C, 32, 0},
	{"VI6_RPF3_SRC_BSIZE",	0x00600, 32, 0},
	{"VI6_RPF3_SRC_ESIZE",	0x00604, 32, 0},
	{"VI6_RPF3_INFMT",	0x00608, 32, 0},
	{"VI6_RPF3_DSWAP",	0x0060C, 32, 0},
	{"VI6_RPF3_LOC",	0x00610, 32, 0},
	{"VI6_RPF3_ALPH_SEL",	0x00614, 32, 0},
	{"VI6_RPF3_VRTCOL_SET",	0x00618, 32, 0},
	{"VI6_RPF3_MSKCTRL",	0x0061C, 32, 0},
	{"VI6_RPF3_MSKSET0",	0x00620, 32, 0},
	{"VI6_RPF3_MSKSET1",	0x00624, 32, 0},
	{"VI6_RPF3_CKEY_CTRL",	0x00628, 32, 0},
	{"VI6_RPF3_CKEY_SETM0",	0x0062C, 32, 0},
	{"VI6_RPF3_CKEY_SETM1",	0x00630, 32, 0},
	{"VI6_RPF3_SRCM_PSTRIDE",	0x00634, 32, 0},
	{"VI6_RPF3_SRCM_ASTRIDE",	0x00638, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_Y",	0x0063C, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_C0",	0x00640, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_C1",	0x00644, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_AI",	0x00648, 32, 0},
	{"VI6_RPF3_MULT_ALPHA",	0x0066C, 32, 0},
	{"VI6_RPF4_SRC_BSIZE",	0x00700, 32, 0},
	{"VI6_RPF4_SRC_ESIZE",	0x00704, 32, 0},
	{"VI6_RPF4_INFMT",	0x00708, 32, 0},
	{"VI6_RPF4_DSWAP",	0x0070C, 32, 0},
	{"VI6_RPF4_LOC",	0x00710, 32, 0},
	{"VI6_RPF4_ALPH_SEL",	0x00714, 32, 0},
	{"VI6_RPF4_VRTCOL_SET",	0x00718, 32, 0},
	{"VI6_RPF4_MSKCTRL",	0x0071C, 32, 0},
	{"VI6_RPF4_MSKSET0",	0x00720, 32, 0},
	{"VI6_RPF4_MSKSET1",	0x00724, 32, 0},
	{"VI6_RPF4_CKEY_CTRL",	0x00728, 32, 0},
	{"VI6_RPF4_CKEY_SETM0",	0x0072C, 32, 0},
	{"VI6_RPF4_CKEY_SETM1",	0x00730, 32, 0},
	{"VI6_RPF4_SRCM_PSTRIDE",	0x00734, 32, 0},
	{"VI6_RPF4_SRCM_ASTRIDE",	0x00738, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_Y",	0x0073C, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_C0",	0x00740, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_C1",	0x00744, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_AI",	0x00748, 32, 0},
	{"VI6_RPF4_MULT_ALPHA",	0x0076C, 32, 0},
	{"VI6_WPF0_SRCRPF",	0x01000, 32, 0},
	{"VI6_WPF0_HSZCLIP",	0x01004, 32, 0},
	{"VI6_WPF0_VSZCLIP",	0x01008, 32, 0},
	{"VI6_WPF0_OUTDMT",	0x0100C, 32, 0},
	{"VI6_WPF0_DSWAP",	0x01010, 32, 0},
	{"VI6_WPF0_RNDCTRL",	0x01014, 32, 0},
	{"VI6_WPF0_ROT_CTRL",	0x01018, 32, 0},
	{"VI6_WPF0_DSTM_STRIDE_Y",	0x0101C, 32, 0},
	{"VI6_WPF0_DSTM_STRIDE_C",	0x01020, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_Y",	0x01024, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_C0",	0x01028, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_C1",	0x0102C, 32, 0},
	{"VI6_WPF0_WRBCK_CTRL",	0x00134, 32, 0},
	{"VI6_WPF1_SRCRPF",	0x01100, 32, 0},
	{"VI6_WPF1_HSZCLIP",	0x01104, 32, 0},
	{"VI6_WPF1_VSZCLIP",	0x01108, 32, 0},
	{"VI6_WPF1_OUTDMT",	0x0110C, 32, 0},
	{"VI6_WPF1_DSWAP",	0x01110, 32, 0},
	{"VI6_WPF1_RNDCTRL",	0x01114, 32, 0},
	{"VI6_WPF1_ROT_CTRL",	0x01118, 32, 0},
	{"VI6_WPF1_DSTM_STRIDE_Y",	0x0111C, 32, 0},
	{"VI6_WPF1_DSTM_STRIDE_C",	0x01120, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_Y",	0x01124, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_C0",	0x01128, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_C1",	0x0112C, 32, 0},
	{"VI6_WPF1_WRBCK_CTRL",	0x01134, 32, 0},
	{"VI6_DPR_RPF0_ROUTE",	0x02000, 32, 0},
	{"VI6_DPR_RPF1_ROUTE",	0x02004, 32, 0},
	{"VI6_DPR_RPF2_ROUTE",	0x02008, 32, 0},
	{"VI6_DPR_RPF3_ROUTE",	0x0200C, 32, 0},
	{"VI6_DPR_RPF4_ROUTE",	0x02010, 32, 0},
	{"VI6_DPR_WPF0_FPORCH",	0x02014, 32, 0},
	{"VI6_DPR_WPF1_FPORCH",	0x02018, 32, 0},
	{"VI6_DPR_UIF4_ROUTE",	0x02074, 32, 0},
	{"VI6_DPR_BRU_ROUTE",	0x0204C, 32, 0},
	{"VI6_BRU_INCTRL",	0x02C00, 32, 0},
	{"VI6_BRU_VIRRPF_SIZE",	0x02C04, 32, 0},
	{"VI6_BRU_VIRRPF_LOC",	0x02C08, 32, 0},
	{"VI6_BRU_VIRRPF_COL",	0x02C0C, 32, 0},
	{"VI6_BRUA_CTRL",	0x02C10, 32, 0},
	{"VI6_BRUB_CTRL",	0x02C18, 32, 0},
	{"VI6_BRUC_CTRL",	0x02C20, 32, 0},
	{"VI6_BRUD_CTRL",	0x02C28, 32, 0},
	{"VI6_BRUE_CTRL",	0x02C34, 32, 0},
	{"VI6_BRUA_BLD",	0x02C14, 32, 0},
	{"VI6_BRUB_BLD",	0x02C1C, 32, 0},
	{"VI6_BRUC_BLD",	0x02C24, 32, 0},
	{"VI6_BRUD_BLD",	0x02C2C, 32, 0},
	{"VI6_BRUE_BLD",	0x02C38, 32, 0},
	{"VI6_BRU_ROP",		0x02C30, 32, 0},
};

static struct rcar_ip vspd1_ip = {
	.ip_name = "VSPD1",
	.base_addr = 0xFEA28000,
	.size = 0x3000,
	.reg_count = ARRAY_SIZE(vspd1_ip_regs),
	.ip_reg = vspd1_ip_regs,
};

static struct hw_register vspd2_ip_regs[] = {
	{"VI6_CMD0",		0x00000, 32, 0},
	{"VI6_CMD1",		0x00004, 32, 0},
	{"VI6_CLK_CTRL0",	0x00010, 32, 0},
	{"VI6_CLK_CTRL1",	0x00014, 32, 0},
	{"VI6_CLK_DCSWT",	0x00018, 32, 0},
	{"VI6_CLK_DCSM0",	0x0001C, 32, 0},
	{"VI6_CLK_DCSM1",	0x00020, 32, 0},
	{"VI6_WPF0_IRQ_ENB",	0x00048, 32, 0},
	{"VI6_WPF1_IRQ_ENB",	0x00054, 32, 0},
	{"VI6_DISP_IRQ_ENB",	0x00078, 32, 0},
	{"VI6_DL_CTRL",		0x00100, 32, 0},
	{"VI6_DL_HDR_ADDR0",	0x00104, 32, 0},
	{"VI6_DL_HDR_ADDR1",	0x00108, 32, 0},
	{"VI6_DL_SWAP",		0x00114, 32, 0},
	{"VI6_DL_EXT_CTRL",	0x0011C, 32, 0},
	{"VI6_DL_BODY_SIZE0",	0x00120, 32, 0},
	{"VI6_DL_HDR_REF_ADDR0",	0x00130, 32, 0},
	{"VI6_DL_HDR_REF_ADDR1",	0x00134, 32, 0},
	{"VI6_RPF0_SRC_BSIZE",	0x00300, 32, 0},
	{"VI6_RPF0_SRC_ESIZE",	0x00304, 32, 0},
	{"VI6_RPF0_INFMT",	0x00308, 32, 0},
	{"VI6_RPF0_DSWAP",	0x0030C, 32, 0},
	{"VI6_RPF0_LOC",	0x00310, 32, 0},
	{"VI6_RPF0_ALPH_SEL",	0x00314, 32, 0},
	{"VI6_RPF0_VRTCOL_SET",	0x00318, 32, 0},
	{"VI6_RPF0_MSKCTRL",	0x0031C, 32, 0},
	{"VI6_RPF0_MSKSET0",	0x00320, 32, 0},
	{"VI6_RPF0_MSKSET1",	0x00324, 32, 0},
	{"VI6_RPF0_CKEY_CTRL",	0x00328, 32, 0},
	{"VI6_RPF0_CKEY_SETM0",	0x0032C, 32, 0},
	{"VI6_RPF0_CKEY_SETM1",	0x00330, 32, 0},
	{"VI6_RPF0_SRCM_PSTRIDE",	0x00334, 32, 0},
	{"VI6_RPF0_SRCM_ASTRIDE",	0x00338, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_Y",	0x0033C, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_C0",	0x00340, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_C1",	0x00344, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_AI",	0x00348, 32, 0},
	{"VI6_RPF0_MULT_ALPHA",	0x0036C, 32, 0},
	{"VI6_RPF1_SRC_BSIZE",	0x00400, 32, 0},
	{"VI6_RPF1_SRC_ESIZE",	0x00404, 32, 0},
	{"VI6_RPF1_INFMT",	0x00408, 32, 0},
	{"VI6_RPF1_DSWAP",	0x0040C, 32, 0},
	{"VI6_RPF1_LOC",	0x00410, 32, 0},
	{"VI6_RPF1_ALPH_SEL",	0x00414, 32, 0},
	{"VI6_RPF1_VRTCOL_SET",	0x00418, 32, 0},
	{"VI6_RPF1_MSKCTRL",	0x0041C, 32, 0},
	{"VI6_RPF1_MSKSET0",	0x00420, 32, 0},
	{"VI6_RPF1_MSKSET1",	0x00424, 32, 0},
	{"VI6_RPF1_CKEY_CTRL",	0x00428, 32, 0},
	{"VI6_RPF1_CKEY_SETM0",	0x0042C, 32, 0},
	{"VI6_RPF1_CKEY_SETM1",	0x00430, 32, 0},
	{"VI6_RPF1_SRCM_PSTRIDE",	0x00434, 32, 0},
	{"VI6_RPF1_SRCM_ASTRIDE",	0x00438, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_Y",	0x0043C, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_C0",	0x00440, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_C1",	0x00444, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_AI",	0x00448, 32, 0},
	{"VI6_RPF1_MULT_ALPHA",	0x0046C, 32, 0},
	{"VI6_RPF2_SRC_BSIZE",	0x00500, 32, 0},
	{"VI6_RPF2_SRC_ESIZE",	0x00504, 32, 0},
	{"VI6_RPF2_INFMT",	0x00508, 32, 0},
	{"VI6_RPF2_DSWAP",	0x0050C, 32, 0},
	{"VI6_RPF2_LOC",	0x00510, 32, 0},
	{"VI6_RPF2_ALPH_SEL",	0x00514, 32, 0},
	{"VI6_RPF2_VRTCOL_SET",	0x00518, 32, 0},
	{"VI6_RPF2_MSKCTRL",	0x0051C, 32, 0},
	{"VI6_RPF2_MSKSET0",	0x00520, 32, 0},
	{"VI6_RPF2_MSKSET1",	0x00524, 32, 0},
	{"VI6_RPF2_CKEY_CTRL",	0x00528, 32, 0},
	{"VI6_RPF2_CKEY_SETM0",	0x0052C, 32, 0},
	{"VI6_RPF2_CKEY_SETM1",	0x00530, 32, 0},
	{"VI6_RPF2_SRCM_PSTRIDE",	0x00534, 32, 0},
	{"VI6_RPF2_SRCM_ASTRIDE",	0x00538, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_Y",	0x0053C, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_C0",	0x00540, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_C1",	0x00544, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_AI",	0x00548, 32, 0},
	{"VI6_RPF2_MULT_ALPHA",	0x0056C, 32, 0},
	{"VI6_RPF3_SRC_BSIZE",	0x00600, 32, 0},
	{"VI6_RPF3_SRC_ESIZE",	0x00604, 32, 0},
	{"VI6_RPF3_INFMT",	0x00608, 32, 0},
	{"VI6_RPF3_DSWAP",	0x0060C, 32, 0},
	{"VI6_RPF3_LOC",	0x00610, 32, 0},
	{"VI6_RPF3_ALPH_SEL",	0x00614, 32, 0},
	{"VI6_RPF3_VRTCOL_SET",	0x00618, 32, 0},
	{"VI6_RPF3_MSKCTRL",	0x0061C, 32, 0},
	{"VI6_RPF3_MSKSET0",	0x00620, 32, 0},
	{"VI6_RPF3_MSKSET1",	0x00624, 32, 0},
	{"VI6_RPF3_CKEY_CTRL",	0x00628, 32, 0},
	{"VI6_RPF3_CKEY_SETM0",	0x0062C, 32, 0},
	{"VI6_RPF3_CKEY_SETM1",	0x00630, 32, 0},
	{"VI6_RPF3_SRCM_PSTRIDE",	0x00634, 32, 0},
	{"VI6_RPF3_SRCM_ASTRIDE",	0x00638, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_Y",	0x0063C, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_C0",	0x00640, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_C1",	0x00644, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_AI",	0x00648, 32, 0},
	{"VI6_RPF3_MULT_ALPHA",	0x0066C, 32, 0},
	{"VI6_RPF4_SRC_BSIZE",	0x00700, 32, 0},
	{"VI6_RPF4_SRC_ESIZE",	0x00704, 32, 0},
	{"VI6_RPF4_INFMT",	0x00708, 32, 0},
	{"VI6_RPF4_DSWAP",	0x0070C, 32, 0},
	{"VI6_RPF4_LOC",	0x00710, 32, 0},
	{"VI6_RPF4_ALPH_SEL",	0x00714, 32, 0},
	{"VI6_RPF4_VRTCOL_SET",	0x00718, 32, 0},
	{"VI6_RPF4_MSKCTRL",	0x0071C, 32, 0},
	{"VI6_RPF4_MSKSET0",	0x00720, 32, 0},
	{"VI6_RPF4_MSKSET1",	0x00724, 32, 0},
	{"VI6_RPF4_CKEY_CTRL",	0x00728, 32, 0},
	{"VI6_RPF4_CKEY_SETM0",	0x0072C, 32, 0},
	{"VI6_RPF4_CKEY_SETM1",	0x00730, 32, 0},
	{"VI6_RPF4_SRCM_PSTRIDE",	0x00734, 32, 0},
	{"VI6_RPF4_SRCM_ASTRIDE",	0x00738, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_Y",	0x0073C, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_C0",	0x00740, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_C1",	0x00744, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_AI",	0x00748, 32, 0},
	{"VI6_RPF4_MULT_ALPHA",	0x0076C, 32, 0},
	{"VI6_WPF0_SRCRPF",	0x01000, 32, 0},
	{"VI6_WPF0_HSZCLIP",	0x01004, 32, 0},
	{"VI6_WPF0_VSZCLIP",	0x01008, 32, 0},
	{"VI6_WPF0_OUTDMT",	0x0100C, 32, 0},
	{"VI6_WPF0_DSWAP",	0x01010, 32, 0},
	{"VI6_WPF0_RNDCTRL",	0x01014, 32, 0},
	{"VI6_WPF0_ROT_CTRL",	0x01018, 32, 0},
	{"VI6_WPF0_DSTM_STRIDE_Y",	0x0101C, 32, 0},
	{"VI6_WPF0_DSTM_STRIDE_C",	0x01020, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_Y",	0x01024, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_C0",	0x01028, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_C1",	0x0102C, 32, 0},
	{"VI6_WPF0_WRBCK_CTRL",	0x00134, 32, 0},
	{"VI6_WPF1_SRCRPF",	0x01100, 32, 0},
	{"VI6_WPF1_HSZCLIP",	0x01104, 32, 0},
	{"VI6_WPF1_VSZCLIP",	0x01108, 32, 0},
	{"VI6_WPF1_OUTDMT",	0x0110C, 32, 0},
	{"VI6_WPF1_DSWAP",	0x01110, 32, 0},
	{"VI6_WPF1_RNDCTRL",	0x01114, 32, 0},
	{"VI6_WPF1_ROT_CTRL",	0x01118, 32, 0},
	{"VI6_WPF1_DSTM_STRIDE_Y",	0x0111C, 32, 0},
	{"VI6_WPF1_DSTM_STRIDE_C",	0x01120, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_Y",	0x01124, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_C0",	0x01128, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_C1",	0x0112C, 32, 0},
	{"VI6_WPF1_WRBCK_CTRL",	0x01134, 32, 0},
	{"VI6_DPR_RPF0_ROUTE",	0x02000, 32, 0},
	{"VI6_DPR_RPF1_ROUTE",	0x02004, 32, 0},
	{"VI6_DPR_RPF2_ROUTE",	0x02008, 32, 0},
	{"VI6_DPR_RPF3_ROUTE",	0x0200C, 32, 0},
	{"VI6_DPR_RPF4_ROUTE",	0x02010, 32, 0},
	{"VI6_DPR_WPF0_FPORCH",	0x02014, 32, 0},
	{"VI6_DPR_WPF1_FPORCH",	0x02018, 32, 0},
	{"VI6_DPR_UIF4_ROUTE",	0x02074, 32, 0},
	{"VI6_DPR_BRU_ROUTE",	0x0204C, 32, 0},
	{"VI6_BRU_INCTRL",	0x02C00, 32, 0},
	{"VI6_BRU_VIRRPF_SIZE",	0x02C04, 32, 0},
	{"VI6_BRU_VIRRPF_LOC",	0x02C08, 32, 0},
	{"VI6_BRU_VIRRPF_COL",	0x02C0C, 32, 0},
	{"VI6_BRUA_CTRL",	0x02C10, 32, 0},
	{"VI6_BRUB_CTRL",	0x02C18, 32, 0},
	{"VI6_BRUC_CTRL",	0x02C20, 32, 0},
	{"VI6_BRUD_CTRL",	0x02C28, 32, 0},
	{"VI6_BRUE_CTRL",	0x02C34, 32, 0},
	{"VI6_BRUA_BLD",	0x02C14, 32, 0},
	{"VI6_BRUB_BLD",	0x02C1C, 32, 0},
	{"VI6_BRUC_BLD",	0x02C24, 32, 0},
	{"VI6_BRUD_BLD",	0x02C2C, 32, 0},
	{"VI6_BRUE_BLD",	0x02C38, 32, 0},
	{"VI6_BRU_ROP",		0x02C30, 32, 0},
};

static struct rcar_ip vspd2_ip = {
	.ip_name = "VSPD2",
	.base_addr = 0xFEA30000,
	.size = 0x3000,
	.reg_count = ARRAY_SIZE(vspd2_ip_regs),
	.ip_reg = vspd2_ip_regs,
};

static struct hw_register vspd3_ip_regs[] = {
	{"VI6_CMD0",		0x00000, 32, 0},
	{"VI6_CMD1",		0x00004, 32, 0},
	{"VI6_CLK_CTRL0",	0x00010, 32, 0},
	{"VI6_CLK_CTRL1",	0x00014, 32, 0},
	{"VI6_CLK_DCSWT",	0x00018, 32, 0},
	{"VI6_CLK_DCSM0",	0x0001C, 32, 0},
	{"VI6_CLK_DCSM1",	0x00020, 32, 0},
	{"VI6_WPF0_IRQ_ENB",	0x00048, 32, 0},
	{"VI6_WPF1_IRQ_ENB",	0x00054, 32, 0},
	{"VI6_DISP_IRQ_ENB",	0x00078, 32, 0},
	{"VI6_DL_CTRL",		0x00100, 32, 0},
	{"VI6_DL_HDR_ADDR0",	0x00104, 32, 0},
	{"VI6_DL_HDR_ADDR1",	0x00108, 32, 0},
	{"VI6_DL_SWAP",		0x00114, 32, 0},
	{"VI6_DL_EXT_CTRL",	0x0011C, 32, 0},
	{"VI6_DL_BODY_SIZE0",	0x00120, 32, 0},
	{"VI6_DL_HDR_REF_ADDR0",	0x00130, 32, 0},
	{"VI6_DL_HDR_REF_ADDR1",	0x00134, 32, 0},
	{"VI6_RPF0_SRC_BSIZE",	0x00300, 32, 0},
	{"VI6_RPF0_SRC_ESIZE",	0x00304, 32, 0},
	{"VI6_RPF0_INFMT",	0x00308, 32, 0},
	{"VI6_RPF0_DSWAP",	0x0030C, 32, 0},
	{"VI6_RPF0_LOC",	0x00310, 32, 0},
	{"VI6_RPF0_ALPH_SEL",	0x00314, 32, 0},
	{"VI6_RPF0_VRTCOL_SET",	0x00318, 32, 0},
	{"VI6_RPF0_MSKCTRL",	0x0031C, 32, 0},
	{"VI6_RPF0_MSKSET0",	0x00320, 32, 0},
	{"VI6_RPF0_MSKSET1",	0x00324, 32, 0},
	{"VI6_RPF0_CKEY_CTRL",	0x00328, 32, 0},
	{"VI6_RPF0_CKEY_SETM0",	0x0032C, 32, 0},
	{"VI6_RPF0_CKEY_SETM1",	0x00330, 32, 0},
	{"VI6_RPF0_SRCM_PSTRIDE",	0x00334, 32, 0},
	{"VI6_RPF0_SRCM_ASTRIDE",	0x00338, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_Y",	0x0033C, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_C0",	0x00340, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_C1",	0x00344, 32, 0},
	{"VI6_RPF0_SRCM_ADDR_AI",	0x00348, 32, 0},
	{"VI6_RPF0_MULT_ALPHA",	0x0036C, 32, 0},
	{"VI6_RPF1_SRC_BSIZE",	0x00400, 32, 0},
	{"VI6_RPF1_SRC_ESIZE",	0x00404, 32, 0},
	{"VI6_RPF1_INFMT",	0x00408, 32, 0},
	{"VI6_RPF1_DSWAP",	0x0040C, 32, 0},
	{"VI6_RPF1_LOC",	0x00410, 32, 0},
	{"VI6_RPF1_ALPH_SEL",	0x00414, 32, 0},
	{"VI6_RPF1_VRTCOL_SET",	0x00418, 32, 0},
	{"VI6_RPF1_MSKCTRL",	0x0041C, 32, 0},
	{"VI6_RPF1_MSKSET0",	0x00420, 32, 0},
	{"VI6_RPF1_MSKSET1",	0x00424, 32, 0},
	{"VI6_RPF1_CKEY_CTRL",	0x00428, 32, 0},
	{"VI6_RPF1_CKEY_SETM0",	0x0042C, 32, 0},
	{"VI6_RPF1_CKEY_SETM1",	0x00430, 32, 0},
	{"VI6_RPF1_SRCM_PSTRIDE",	0x00434, 32, 0},
	{"VI6_RPF1_SRCM_ASTRIDE",	0x00438, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_Y",	0x0043C, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_C0",	0x00440, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_C1",	0x00444, 32, 0},
	{"VI6_RPF1_SRCM_ADDR_AI",	0x00448, 32, 0},
	{"VI6_RPF1_MULT_ALPHA",	0x0046C, 32, 0},
	{"VI6_RPF2_SRC_BSIZE",	0x00500, 32, 0},
	{"VI6_RPF2_SRC_ESIZE",	0x00504, 32, 0},
	{"VI6_RPF2_INFMT",	0x00508, 32, 0},
	{"VI6_RPF2_DSWAP",	0x0050C, 32, 0},
	{"VI6_RPF2_LOC",	0x00510, 32, 0},
	{"VI6_RPF2_ALPH_SEL",	0x00514, 32, 0},
	{"VI6_RPF2_VRTCOL_SET",	0x00518, 32, 0},
	{"VI6_RPF2_MSKCTRL",	0x0051C, 32, 0},
	{"VI6_RPF2_MSKSET0",	0x00520, 32, 0},
	{"VI6_RPF2_MSKSET1",	0x00524, 32, 0},
	{"VI6_RPF2_CKEY_CTRL",	0x00528, 32, 0},
	{"VI6_RPF2_CKEY_SETM0",	0x0052C, 32, 0},
	{"VI6_RPF2_CKEY_SETM1",	0x00530, 32, 0},
	{"VI6_RPF2_SRCM_PSTRIDE",	0x00534, 32, 0},
	{"VI6_RPF2_SRCM_ASTRIDE",	0x00538, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_Y",	0x0053C, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_C0",	0x00540, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_C1",	0x00544, 32, 0},
	{"VI6_RPF2_SRCM_ADDR_AI",	0x00548, 32, 0},
	{"VI6_RPF2_MULT_ALPHA",	0x0056C, 32, 0},
	{"VI6_RPF3_SRC_BSIZE",	0x00600, 32, 0},
	{"VI6_RPF3_SRC_ESIZE",	0x00604, 32, 0},
	{"VI6_RPF3_INFMT",	0x00608, 32, 0},
	{"VI6_RPF3_DSWAP",	0x0060C, 32, 0},
	{"VI6_RPF3_LOC",	0x00610, 32, 0},
	{"VI6_RPF3_ALPH_SEL",	0x00614, 32, 0},
	{"VI6_RPF3_VRTCOL_SET",	0x00618, 32, 0},
	{"VI6_RPF3_MSKCTRL",	0x0061C, 32, 0},
	{"VI6_RPF3_MSKSET0",	0x00620, 32, 0},
	{"VI6_RPF3_MSKSET1",	0x00624, 32, 0},
	{"VI6_RPF3_CKEY_CTRL",	0x00628, 32, 0},
	{"VI6_RPF3_CKEY_SETM0",	0x0062C, 32, 0},
	{"VI6_RPF3_CKEY_SETM1",	0x00630, 32, 0},
	{"VI6_RPF3_SRCM_PSTRIDE",	0x00634, 32, 0},
	{"VI6_RPF3_SRCM_ASTRIDE",	0x00638, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_Y",	0x0063C, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_C0",	0x00640, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_C1",	0x00644, 32, 0},
	{"VI6_RPF3_SRCM_ADDR_AI",	0x00648, 32, 0},
	{"VI6_RPF3_MULT_ALPHA",	0x0066C, 32, 0},
	{"VI6_RPF4_SRC_BSIZE",	0x00700, 32, 0},
	{"VI6_RPF4_SRC_ESIZE",	0x00704, 32, 0},
	{"VI6_RPF4_INFMT",	0x00708, 32, 0},
	{"VI6_RPF4_DSWAP",	0x0070C, 32, 0},
	{"VI6_RPF4_LOC",	0x00710, 32, 0},
	{"VI6_RPF4_ALPH_SEL",	0x00714, 32, 0},
	{"VI6_RPF4_VRTCOL_SET",	0x00718, 32, 0},
	{"VI6_RPF4_MSKCTRL",	0x0071C, 32, 0},
	{"VI6_RPF4_MSKSET0",	0x00720, 32, 0},
	{"VI6_RPF4_MSKSET1",	0x00724, 32, 0},
	{"VI6_RPF4_CKEY_CTRL",	0x00728, 32, 0},
	{"VI6_RPF4_CKEY_SETM0",	0x0072C, 32, 0},
	{"VI6_RPF4_CKEY_SETM1",	0x00730, 32, 0},
	{"VI6_RPF4_SRCM_PSTRIDE",	0x00734, 32, 0},
	{"VI6_RPF4_SRCM_ASTRIDE",	0x00738, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_Y",	0x0073C, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_C0",	0x00740, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_C1",	0x00744, 32, 0},
	{"VI6_RPF4_SRCM_ADDR_AI",	0x00748, 32, 0},
	{"VI6_RPF4_MULT_ALPHA",	0x0076C, 32, 0},
	{"VI6_WPF0_SRCRPF",	0x01000, 32, 0},
	{"VI6_WPF0_HSZCLIP",	0x01004, 32, 0},
	{"VI6_WPF0_VSZCLIP",	0x01008, 32, 0},
	{"VI6_WPF0_OUTDMT",	0x0100C, 32, 0},
	{"VI6_WPF0_DSWAP",	0x01010, 32, 0},
	{"VI6_WPF0_RNDCTRL",	0x01014, 32, 0},
	{"VI6_WPF0_ROT_CTRL",	0x01018, 32, 0},
	{"VI6_WPF0_DSTM_STRIDE_Y",	0x0101C, 32, 0},
	{"VI6_WPF0_DSTM_STRIDE_C",	0x01020, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_Y",	0x01024, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_C0",	0x01028, 32, 0},
	{"VI6_WPF0_DSTM_ADDR_C1",	0x0102C, 32, 0},
	{"VI6_WPF0_WRBCK_CTRL",	0x00134, 32, 0},
	{"VI6_WPF1_SRCRPF",	0x01100, 32, 0},
	{"VI6_WPF1_HSZCLIP",	0x01104, 32, 0},
	{"VI6_WPF1_VSZCLIP",	0x01108, 32, 0},
	{"VI6_WPF1_OUTDMT",	0x0110C, 32, 0},
	{"VI6_WPF1_DSWAP",	0x01110, 32, 0},
	{"VI6_WPF1_RNDCTRL",	0x01114, 32, 0},
	{"VI6_WPF1_ROT_CTRL",	0x01118, 32, 0},
	{"VI6_WPF1_DSTM_STRIDE_Y",	0x0111C, 32, 0},
	{"VI6_WPF1_DSTM_STRIDE_C",	0x01120, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_Y",	0x01124, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_C0",	0x01128, 32, 0},
	{"VI6_WPF1_DSTM_ADDR_C1",	0x0112C, 32, 0},
	{"VI6_WPF1_WRBCK_CTRL",	0x01134, 32, 0},
	{"VI6_DPR_RPF0_ROUTE",	0x02000, 32, 0},
	{"VI6_DPR_RPF1_ROUTE",	0x02004, 32, 0},
	{"VI6_DPR_RPF2_ROUTE",	0x02008, 32, 0},
	{"VI6_DPR_RPF3_ROUTE",	0x0200C, 32, 0},
	{"VI6_DPR_RPF4_ROUTE",	0x02010, 32, 0},
	{"VI6_DPR_WPF0_FPORCH",	0x02014, 32, 0},
	{"VI6_DPR_WPF1_FPORCH",	0x02018, 32, 0},
	{"VI6_DPR_UIF4_ROUTE",	0x02074, 32, 0},
	{"VI6_DPR_BRU_ROUTE",	0x0204C, 32, 0},
	{"VI6_BRU_INCTRL",	0x02C00, 32, 0},
	{"VI6_BRU_VIRRPF_SIZE",	0x02C04, 32, 0},
	{"VI6_BRU_VIRRPF_LOC",	0x02C08, 32, 0},
	{"VI6_BRU_VIRRPF_COL",	0x02C0C, 32, 0},
	{"VI6_BRUA_CTRL",	0x02C10, 32, 0},
	{"VI6_BRUB_CTRL",	0x02C18, 32, 0},
	{"VI6_BRUC_CTRL",	0x02C20, 32, 0},
	{"VI6_BRUD_CTRL",	0x02C28, 32, 0},
	{"VI6_BRUE_CTRL",	0x02C34, 32, 0},
	{"VI6_BRUA_BLD",	0x02C14, 32, 0},
	{"VI6_BRUB_BLD",	0x02C1C, 32, 0},
	{"VI6_BRUC_BLD",	0x02C24, 32, 0},
	{"VI6_BRUD_BLD",	0x02C2C, 32, 0},
	{"VI6_BRUE_BLD",	0x02C38, 32, 0},
	{"VI6_BRU_ROP",		0x02C30, 32, 0},
};

static struct rcar_ip vspd3_ip = {
	.ip_name = "VSPD3",
	.base_addr = 0xFEA38000,
	.size = 0x3000,
	.reg_count = ARRAY_SIZE(vspd3_ip_regs),
	.ip_reg = vspd3_ip_regs,
};

static struct rcar_ip *ip_info_tbl[] = {
	&vspd0_ip,
	&vspd1_ip,
	&vspd2_ip,
	&vspd3_ip,
};

int rcar_du_vsp_save_regs(struct rcar_du_crtc *crtc)
{
	struct rcar_ip *vsp_ip = ip_info_tbl[crtc->vsp->index];
	int ret;

	if (!vsp_ip->virt_addr) {
		ret = handle_registers(vsp_ip, DO_IOREMAP);
		if (ret)
			return ret;
	}

	ret = handle_registers(vsp_ip, DO_BACKUP);
	pr_debug("%s: Backup %s registers\n", __func__, vsp_ip->ip_name);

	return ret;
}

int rcar_du_vsp_restore_regs(struct rcar_du_crtc *crtc)
{
	struct rcar_ip *vsp_ip = ip_info_tbl[crtc->vsp->index];
	int ret;

	ret = handle_registers(vsp_ip, DO_RESTORE);
	pr_debug("%s: Restore %s registers\n", __func__, vsp_ip->ip_name);

	return ret;
}
#endif /* CONFIG_RCAR_DDR_BACKUP */

void rcar_du_vsp_enable(struct rcar_du_crtc *crtc)
{
	const struct drm_display_mode *mode = &crtc->crtc.state->adjusted_mode;
	struct rcar_du_device *rcdu = crtc->group->dev;
	struct rcar_du_plane_state state = {
		.state = {
			.crtc = &crtc->crtc,
			.crtc_x = 0,
			.crtc_y = 0,
			.crtc_w = mode->hdisplay,
			.crtc_h = mode->vdisplay,
			.src_x = 0,
			.src_y = 0,
			.src_w = mode->hdisplay << 16,
			.src_h = mode->vdisplay << 16,
		},
		.format = rcar_du_format_info(DRM_FORMAT_ARGB8888),
		.source = RCAR_DU_PLANE_VSPD1,
		.alpha = 255,
		.colorkey = 0,
		.zpos = 0,
	};

	if (rcdu->info->gen >= 3)
		state.hwindex = (crtc->index % 2) ? 2 : 0;
	else
		state.hwindex = crtc->index % 2;

	__rcar_du_plane_setup(crtc->group, &state);

	/* Ensure that the plane source configuration takes effect by requesting
	 * a restart of the group. See rcar_du_plane_atomic_update() for a more
	 * detailed explanation.
	 *
	 * TODO: Check whether this is still needed on Gen3.
	 */
	crtc->group->need_restart = true;

	vsp1_du_setup_lif(crtc->vsp->vsp, mode->hdisplay, mode->vdisplay);
}

void rcar_du_vsp_disable(struct rcar_du_crtc *crtc)
{
	vsp1_du_setup_lif(crtc->vsp->vsp, 0, 0);
}

void rcar_du_vsp_atomic_begin(struct rcar_du_crtc *crtc)
{
	vsp1_du_atomic_begin(crtc->vsp->vsp);
}

void rcar_du_vsp_atomic_flush(struct rcar_du_crtc *crtc)
{
	vsp1_du_atomic_flush(crtc->vsp->vsp);
}

/* Keep the two tables in sync. */
static const u32 formats_kms[] = {
	DRM_FORMAT_RGB332,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV61,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_YVU444,
};

static const u32 formats_v4l2[] = {
	V4L2_PIX_FMT_RGB332,
	V4L2_PIX_FMT_ARGB444,
	V4L2_PIX_FMT_XRGB444,
	V4L2_PIX_FMT_ARGB555,
	V4L2_PIX_FMT_XRGB555,
	V4L2_PIX_FMT_RGB565,
	V4L2_PIX_FMT_RGB24,
	V4L2_PIX_FMT_BGR24,
	V4L2_PIX_FMT_ARGB32,
	V4L2_PIX_FMT_XRGB32,
	V4L2_PIX_FMT_ABGR32,
	V4L2_PIX_FMT_XBGR32,
	V4L2_PIX_FMT_UYVY,
	V4L2_PIX_FMT_VYUY,
	V4L2_PIX_FMT_YUYV,
	V4L2_PIX_FMT_YVYU,
	V4L2_PIX_FMT_NV12M,
	V4L2_PIX_FMT_NV21M,
	V4L2_PIX_FMT_NV16M,
	V4L2_PIX_FMT_NV61M,
	V4L2_PIX_FMT_YUV420M,
	V4L2_PIX_FMT_YVU420M,
	V4L2_PIX_FMT_YUV422M,
	V4L2_PIX_FMT_YVU422M,
	V4L2_PIX_FMT_YUV444M,
	V4L2_PIX_FMT_YVU444M,
};

static const u32 formats_xlate[][2] = {
	{ DRM_FORMAT_RGB332, V4L2_PIX_FMT_RGB332 },
	{ DRM_FORMAT_ARGB4444, V4L2_PIX_FMT_ARGB444 },
	{ DRM_FORMAT_XRGB4444, V4L2_PIX_FMT_XRGB444 },
	{ DRM_FORMAT_ARGB1555, V4L2_PIX_FMT_ARGB555 },
	{ DRM_FORMAT_XRGB1555, V4L2_PIX_FMT_XRGB555 },
	{ DRM_FORMAT_RGB565, V4L2_PIX_FMT_RGB565 },
	{ DRM_FORMAT_BGR888, V4L2_PIX_FMT_RGB24 },
	{ DRM_FORMAT_RGB888, V4L2_PIX_FMT_BGR24 },
	{ DRM_FORMAT_BGRA8888, V4L2_PIX_FMT_ARGB32 },
	{ DRM_FORMAT_BGRX8888, V4L2_PIX_FMT_XRGB32 },
	{ DRM_FORMAT_ARGB8888, V4L2_PIX_FMT_ABGR32 },
	{ DRM_FORMAT_XRGB8888, V4L2_PIX_FMT_XBGR32 },
	{ DRM_FORMAT_UYVY, V4L2_PIX_FMT_UYVY },
	{ DRM_FORMAT_YUYV, V4L2_PIX_FMT_YUYV },
	{ DRM_FORMAT_YVYU, V4L2_PIX_FMT_YVYU },
	{ DRM_FORMAT_NV12, V4L2_PIX_FMT_NV12M },
	{ DRM_FORMAT_NV21, V4L2_PIX_FMT_NV21M },
	{ DRM_FORMAT_NV16, V4L2_PIX_FMT_NV16M },
	{ DRM_FORMAT_NV61, V4L2_PIX_FMT_NV61M },
};

static void rcar_du_vsp_plane_setup(struct rcar_du_vsp_plane *plane)
{
	struct rcar_du_vsp_plane_state *state =
		to_rcar_vsp_plane_state(plane->plane.state);
	struct drm_framebuffer *fb = plane->plane.state->fb;
	struct vsp1_du_atomic_config cfg = {
		.pixelformat = 0,
		.pitch = fb->pitches[0],
		.alpha = state->alpha,
		.zpos = state->zpos,
	};
	unsigned int i;

	cfg.src.left = state->state.src_x >> 16;
	cfg.src.top = state->state.src_y >> 16;
	cfg.src.width = state->state.src_w >> 16;
	cfg.src.height = state->state.src_h >> 16;

	cfg.dst.left = state->state.crtc_x;
	cfg.dst.top = state->state.crtc_y;
	cfg.dst.width = state->state.crtc_w;
	cfg.dst.height = state->state.crtc_h;

	for (i = 0; i < state->format->planes; ++i) {
		struct drm_gem_cma_object *gem;

		gem = drm_fb_cma_get_gem_obj(fb, i);
		cfg.mem[i] = gem->paddr + fb->offsets[i];
	}

	for (i = 0; i < ARRAY_SIZE(formats_kms); ++i) {
		if (formats_kms[i] == state->format->fourcc) {
			cfg.pixelformat = formats_v4l2[i];
			break;
		}
	}

	vsp1_du_atomic_update(plane->vsp->vsp, plane->index, &cfg);
}

static int rcar_du_vsp_plane_atomic_check(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);
	struct rcar_du_vsp_plane *rplane = to_rcar_vsp_plane(plane);
	struct rcar_du_device *rcdu = rplane->vsp->dev;
	int hdisplay, vdisplay;

	if (!state->fb || !state->crtc) {
		rstate->format = NULL;
		return 0;
	}

	if (state->src_w >> 16 != state->crtc_w ||
	    state->src_h >> 16 != state->crtc_h) {
		dev_dbg(rcdu->dev, "%s: scaling not supported\n", __func__);
		return -EINVAL;
	}

	hdisplay = state->crtc->mode.hdisplay;
	vdisplay = state->crtc->mode.vdisplay;

	if ((state->plane->type == DRM_PLANE_TYPE_OVERLAY) &&
		(((state->crtc_w + state->crtc_x) > hdisplay) ||
		((state->crtc_h + state->crtc_y) > vdisplay))) {
		dev_err(rcdu->dev,
			"%s: specify (%dx%d) + (%d, %d) < (%dx%d).\n",
			__func__, state->crtc_w, state->crtc_h, state->crtc_x,
			state->crtc_y, hdisplay, vdisplay);
		return -EINVAL;
	}

	rstate->format = rcar_du_format_info(state->fb->pixel_format);

	if (rcar_du_has(rcdu, RCAR_DU_FEATURE_VSP1_SOURCE) &&
			 (rstate->format == NULL))
		rstate->format = rcar_vsp_format_info(state->fb->pixel_format);

	if (rstate->format == NULL) {
		dev_dbg(rcdu->dev, "%s: unsupported format %08x\n", __func__,
			state->fb->pixel_format);
		return -EINVAL;
	}

	return 0;
}

static void rcar_du_vsp_plane_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct rcar_du_vsp_plane *rplane = to_rcar_vsp_plane(plane);

	if (plane->state->crtc)
		rcar_du_vsp_plane_setup(rplane);
	else
		vsp1_du_atomic_update(rplane->vsp->vsp, rplane->index, NULL);
}

static const struct drm_plane_helper_funcs rcar_du_vsp_plane_helper_funcs = {
	.atomic_check = rcar_du_vsp_plane_atomic_check,
	.atomic_update = rcar_du_vsp_plane_atomic_update,
};

static struct drm_plane_state *
rcar_du_vsp_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct rcar_du_vsp_plane_state *state;
	struct rcar_du_vsp_plane_state *copy;

	if (WARN_ON(!plane->state))
		return NULL;

	state = to_rcar_vsp_plane_state(plane->state);
	copy = kmemdup(state, sizeof(*state), GFP_KERNEL);
	if (copy == NULL)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &copy->state);

	return &copy->state;
}

static void rcar_du_vsp_plane_atomic_destroy_state(struct drm_plane *plane,
						   struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(plane, state);
	kfree(to_rcar_vsp_plane_state(state));
}

static void rcar_du_vsp_plane_reset(struct drm_plane *plane)
{
	struct rcar_du_vsp_plane_state *state;

	if (plane->state) {
		rcar_du_vsp_plane_atomic_destroy_state(plane, plane->state);
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return;

	state->alpha = 255;
	state->zpos = plane->type == DRM_PLANE_TYPE_PRIMARY ? 0 : 1;

	plane->state = &state->state;
	plane->state->plane = plane;
}

static int rcar_du_vsp_plane_atomic_set_property(struct drm_plane *plane,
	struct drm_plane_state *state, struct drm_property *property,
	uint64_t val)
{
	struct rcar_du_vsp_plane_state *rstate = to_rcar_vsp_plane_state(state);
	struct rcar_du_device *rcdu = to_rcar_vsp_plane(plane)->vsp->dev;

	if (property == rcdu->props.alpha)
		rstate->alpha = val;
	else if (property == rcdu->props.zpos)
		rstate->zpos = val;
	else
		return -EINVAL;

	return 0;
}

static int rcar_du_vsp_plane_atomic_get_property(struct drm_plane *plane,
	const struct drm_plane_state *state, struct drm_property *property,
	uint64_t *val)
{
	const struct rcar_du_vsp_plane_state *rstate =
		container_of(state, const struct rcar_du_vsp_plane_state, state);
	struct rcar_du_device *rcdu = to_rcar_vsp_plane(plane)->vsp->dev;

	if (property == rcdu->props.alpha)
		*val = rstate->alpha;
	else if (property == rcdu->props.zpos)
		*val = rstate->zpos;
	else
		return -EINVAL;

	return 0;
}

static const struct drm_plane_funcs rcar_du_vsp_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = rcar_du_vsp_plane_reset,
	.set_property = drm_atomic_helper_plane_set_property,
	.destroy = drm_plane_cleanup,
	.atomic_duplicate_state = rcar_du_vsp_plane_atomic_duplicate_state,
	.atomic_destroy_state = rcar_du_vsp_plane_atomic_destroy_state,
	.atomic_set_property = rcar_du_vsp_plane_atomic_set_property,
	.atomic_get_property = rcar_du_vsp_plane_atomic_get_property,
};

static const uint32_t formats[] = {
	DRM_FORMAT_RGB332,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV61,
};

int rcar_du_vsp_init(struct rcar_du_vsp *vsp)
{
	struct rcar_du_device *rcdu = vsp->dev;
	struct platform_device *pdev;
	struct device_node *np;
	unsigned int i;
	int ret;

	/* Find the VSP device and initialize it. */
	np = of_parse_phandle(rcdu->dev->of_node, "vsps", vsp->index);
	if (!np) {
		dev_err(rcdu->dev, "vsps node not found\n");
		return -ENXIO;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return -ENXIO;

	vsp->vsp = &pdev->dev;

	ret = vsp1_du_init(vsp->vsp);
	if (ret < 0)
		return ret;

	 /* The VSP2D (Gen3) has 5 RPFs, but the VSP1D (Gen2) is limited to
	  * 4 RPFs.
	  */
	vsp->num_planes = rcdu->info->vsp_num;

	vsp->planes = devm_kcalloc(rcdu->dev, vsp->num_planes,
				   sizeof(*vsp->planes), GFP_KERNEL);
	if (!vsp->planes)
		return -ENOMEM;

	for (i = 0; i < vsp->num_planes; ++i) {
		enum drm_plane_type type = i ? DRM_PLANE_TYPE_OVERLAY
					 : DRM_PLANE_TYPE_PRIMARY;
		struct rcar_du_vsp_plane *plane = &vsp->planes[i];

		plane->vsp = vsp;
		plane->index = i;

		ret = drm_universal_plane_init(rcdu->ddev, &plane->plane,
					       1 << vsp->index,
					       &rcar_du_vsp_plane_funcs,
					       formats_kms,
					       ARRAY_SIZE(formats_kms), type,
					       NULL);
		if (ret < 0)
			return ret;

		drm_plane_helper_add(&plane->plane,
				     &rcar_du_vsp_plane_helper_funcs);

		if (type == DRM_PLANE_TYPE_PRIMARY)
			continue;

		drm_object_attach_property(&plane->plane.base,
					   rcdu->props.alpha, 255);
		drm_object_attach_property(&plane->plane.base,
					   rcdu->props.zpos, 1);
	}

	return 0;
}
