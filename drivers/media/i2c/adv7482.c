/*
 * drivers/media/i2c/adv7482.c
 *     This file is Analog Devices ADV7482 HDMI receiver driver.
 *
 * Copyright (C) 2015-2017 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-of.h>
#include <linux/mutex.h>
#include <media/soc_camera.h>

#include <linux/delay.h>  /* for msleep() */

#define DRIVER_NAME "adv7482"

/****************************************/
/* ADV7482 I2C slave address definition */
/****************************************/
#define ADV7482_I2C_IO			0x70	/* IO Map */
#define ADV7482_I2C_DPLL		0x26	/* DPLL Map */
#define ADV7482_I2C_CP			0x22	/* CP Map */
#define ADV7482_I2C_HDMI		0x34	/* HDMI Map */
#define ADV7482_I2C_EDID		0x36	/* EDID Map */
#define ADV7482_I2C_REPEATER		0x32	/* HDMI RX Repeater Map */
#define ADV7482_I2C_INFOFRAME		0x31	/* HDMI RX InfoFrame Map */
#define ADV7482_I2C_CEC			0x41	/* CEC Map */
#define ADV7482_I2C_SDP			0x79	/* SDP Map */
#define ADV7482_I2C_TXB			0x48	/* CSI-TXB Map */
#define ADV7482_I2C_TXA			0x4A	/* CSI-TXA Map */
#define ADV7482_I2C_WAIT		0xFE	/* Wait x mesec */
#define ADV7482_I2C_EOR			0xFF	/* End Mark */

/****************************************/
/* ADV7482 IO register definition       */
/****************************************/

/* Revision */
#define ADV7482_IO_RD_INFO1_REG	0xDF	/* chip version register */
#define ADV7482_IO_RD_INFO2_REG	0xE0	/* chip version register */

#define ADV7482_IO_CP_DATAPATH_REG	0x03	/* datapath ctrl */
#define ADV7482_IO_CP_COLORSPACE_REG	0x04
#define ADV7482_IO_CP_VID_STD_REG	0x05	/* Video Standard */

/* Power Management */
#define ADV7482_IO_PWR_MAN_REG	0x0C	/* Power management register */
#define ADV7482_IO_PWR_ON		0xE0	/* Power on */
#define ADV7482_IO_PWR_OFF		0x00	/* Power down */

#define ADV7482_HDMI_DDC_PWRDN	0x73	/* Power DDC pads control register */
#define ADV7482_HDMI_DDC_PWR_ON		0x00	/* Power on */
#define ADV7482_HDMI_DDC_PWR_OFF	0x01	/* Power down */

#define ADV7482_IO_CP_VID_STD_480I	0x40
#define ADV7482_IO_CP_VID_STD_576I	0x41
#define ADV7482_IO_CP_VID_STD_480P	0x4A
#define ADV7482_IO_CP_VID_STD_576P	0x4B
#define ADV7482_IO_CP_VID_STD_720P	0x53
#define ADV7482_IO_CP_VID_STD_1080I	0x54
#define ADV7482_IO_CP_VID_STD_1080P	0x5E
#define ADV7482_IO_CP_VID_STD_SVGA56	0x80
#define ADV7482_IO_CP_VID_STD_SVGA60	0x81
#define ADV7482_IO_CP_VID_STD_SVGA72	0x82
#define ADV7482_IO_CP_VID_STD_SVGA75	0x83
#define ADV7482_IO_CP_VID_STD_SVGA85	0x84
#define ADV7482_IO_CP_VID_STD_SXGA60	0x85
#define ADV7482_IO_CP_VID_STD_SXGA75	0x86
#define ADV7482_IO_CP_VID_STD_VGA60	0x88
#define ADV7482_IO_CP_VID_STD_VGA72	0x89
#define ADV7482_IO_CP_VID_STD_VGA75	0x8A
#define ADV7482_IO_CP_VID_STD_VGA85	0x8B
#define ADV7482_IO_CP_VID_STD_XGA60	0x8C
#define ADV7482_IO_CP_VID_STD_XGA70	0x8D
#define ADV7482_IO_CP_VID_STD_XGA75	0x8E
#define ADV7482_IO_CP_VID_STD_XGA85	0x8F
#define ADV7482_IO_CP_VID_STD_UXGA60	0x96

#define ADV7482_IO_CSI4_EN_ENABLE       0x80
#define ADV7482_IO_CSI4_EN_DISABLE      0x00

#define ADV7482_IO_CSI1_EN_ENABLE       0x40
#define ADV7482_IO_CSI1_EN_DISABLE      0x00

/****************************************/
/* ADV7482 CP register definition       */
/****************************************/

/* Contrast Control */
#define ADV7482_CP_CON_REG	0x3a	/* Contrast (unsigned) */
#define ADV7482_CP_CON_MIN	0	/* Minimum contrast */
#define ADV7482_CP_CON_DEF	128	/* Default */
#define ADV7482_CP_CON_MAX	255	/* Maximum contrast */

/* Saturation Control */
#define ADV7482_CP_SAT_REG	0x3b	/* Saturation (unsigned) */
#define ADV7482_CP_SAT_MIN	0	/* Minimum saturation */
#define ADV7482_CP_SAT_DEF	128	/* Default */
#define ADV7482_CP_SAT_MAX	255	/* Maximum saturation */

/* Brightness Control */
#define ADV7482_CP_BRI_REG	0x3c	/* Brightness (signed) */
#define ADV7482_CP_BRI_MIN	-128	/* Luma is -512d */
#define ADV7482_CP_BRI_DEF	0	/* Luma is 0 */
#define ADV7482_CP_BRI_MAX	127	/* Luma is 508d */

/* Hue Control */
#define ADV7482_CP_HUE_REG	0x3d	/* Hue (unsigned) */
#define ADV7482_CP_HUE_MIN	0	/* -90 degree */
#define ADV7482_CP_HUE_DEF	0	/* -90 degree */
#define ADV7482_CP_HUE_MAX	255	/* +90 degree */

/* Video adjustment register */
#define ADV7482_CP_VID_ADJ_REG		0x3e
/* Video adjustment mask */
#define ADV7482_CP_VID_ADJ_MASK		0x7F
/* Enable color controls */
#define ADV7482_CP_VID_ADJ_ENABLE	0x80

/****************************************/
/* ADV7482 HDMI register definition     */
/****************************************/

/* HDMI status register */
#define ADV7482_HDMI_STATUS1_REG		0x07
/* VERT_FILTER_LOCKED flag */
#define ADV7482_HDMI_VF_LOCKED_FLG		0x80
/* DE_REGEN_FILTER_LOCKED flag */
#define ADV7482_HDMI_DERF_LOCKED_FLG		0x20
/* LINE_WIDTH[12:8] mask */
#define ADV7482_HDMI_LWIDTH_MSBS_MASK		0x1F

/* LINE_WIDTH[7:0] register */
#define ADV7482_HDMI_LWIDTH_REG			0x08

/* FIELD0_HEIGHT[12:8] register */
#define ADV7482_HDMI_F0HEIGHT_MSBS_REG		0x09
/* FIELD0_HEIGHT[12:8] mask */
#define ADV7482_HDMI_F0HEIGHT_MSBS_MASK		0x1F

/* FIELD0_HEIGHT[7:0] register */
#define ADV7482_HDMI_F0HEIGHT_LSBS_REG		0x0A

/* HDMI status register */
#define ADV7482_HDMI_STATUS2_REG		0x0B
/* DEEP_COLOR_MODE[1:0] mask */
#define ADV7482_HDMI_DCM_MASK			0xC0
/* HDMI_INTERLACED flag */
#define ADV7482_HDMI_IP_FLAG			0x20
/* FIELD1_HEIGHT[12:8] mask */
#define ADV7482_HDMI_F1HEIGHT_MSBS_MASK		0x1F

/* FIELD1_HEIGHT[7:0] register */
#define ADV7482_HDMI_F1HEIGHT_REG		0x0C

struct adv7482_sdp_main_info {
	u8			status_reg_10;
};

/****************************************/
/* ADV7482 SDP register definition      */
/****************************************/

#define ADV7482_SDP_MAIN_MAP	0x00
#define ADV7482_SDP_SUB_MAP1	0x20
#define ADV7482_SDP_SUB_MAP2	0x40

#define ADV7482_SDP_NO_RO_MAIN_MAP	0x00
#define ADV7482_SDP_RO_MAIN_MAP		0x01
#define ADV7482_SDP_RO_SUB_MAP1		0x02
#define ADV7482_SDP_RO_SUB_MAP2		0x03

#define ADV7482_SDP_MAIN_MAP_RW \
			(ADV7482_SDP_MAIN_MAP | ADV7482_SDP_NO_RO_MAIN_MAP)

#define ADV7482_SDP_STD_AD_PAL_BG_NTSC_J_SECAM		0x0
#define ADV7482_SDP_STD_AD_PAL_BG_NTSC_J_SECAM_PED	0x1
#define ADV7482_SDP_STD_AD_PAL_N_NTSC_J_SECAM		0x2
#define ADV7482_SDP_STD_AD_PAL_N_NTSC_M_SECAM		0x3
#define ADV7482_SDP_STD_NTSC_J				0x4
#define ADV7482_SDP_STD_NTSC_M				0x5
#define ADV7482_SDP_STD_PAL60				0x6
#define ADV7482_SDP_STD_NTSC_443			0x7
#define ADV7482_SDP_STD_PAL_BG				0x8
#define ADV7482_SDP_STD_PAL_N				0x9
#define ADV7482_SDP_STD_PAL_M				0xa
#define ADV7482_SDP_STD_PAL_M_PED			0xb
#define ADV7482_SDP_STD_PAL_COMB_N			0xc
#define ADV7482_SDP_STD_PAL_COMB_N_PED			0xd
#define ADV7482_SDP_STD_PAL_SECAM			0xe
#define ADV7482_SDP_STD_PAL_SECAM_PED			0xf

#define ADV7482_SDP_REG_INPUT_CONTROL		0x00
#define ADV7482_SDP_INPUT_CONTROL_INSEL_MASK	0x0f

#define ADV7482_SDP_REG_INPUT_VIDSEL		0x02

#define ADV7482_SDP_REG_CTRL			0x0e

#define ADV7482_SDP_REG_PWR_MAN		0x0f
#define ADV7482_SDP_PWR_MAN_ON		0x00
#define ADV7482_SDP_PWR_MAN_OFF		0x20
#define ADV7482_SDP_PWR_MAN_RES		0x80

/* Contrast */
#define ADV7482_SDP_REG_CON		0x08	/*Unsigned */
#define ADV7482_SDP_CON_MIN		0
#define ADV7482_SDP_CON_DEF		128
#define ADV7482_SDP_CON_MAX		255
/* Brightness*/
#define ADV7482_SDP_REG_BRI		0x0a	/*Signed */
#define ADV7482_SDP_BRI_MIN		-128
#define ADV7482_SDP_BRI_DEF		0
#define ADV7482_SDP_BRI_MAX		127
/* Hue */
#define ADV7482_SDP_REG_HUE		0x0b	/*Signed, inverted */
#define ADV7482_SDP_HUE_MIN		-127
#define ADV7482_SDP_HUE_DEF		0
#define ADV7482_SDP_HUE_MAX		128

/* Saturation */
#define ADV7482_SDP_REG_SD_SAT_CB	0xe3
#define ADV7482_SDP_REG_SD_SAT_CR	0xe4
#define ADV7482_SDP_SAT_MIN		0
#define ADV7482_SDP_SAT_DEF		128
#define ADV7482_SDP_SAT_MAX		255

#define ADV7482_SDP_INPUT_CVBS_AIN1		0x00
#define ADV7482_SDP_INPUT_CVBS_AIN2		0x01
#define ADV7482_SDP_INPUT_CVBS_AIN3		0x02
#define ADV7482_SDP_INPUT_CVBS_AIN4		0x03
#define ADV7482_SDP_INPUT_CVBS_AIN5		0x04
#define ADV7482_SDP_INPUT_CVBS_AIN6		0x05
#define ADV7482_SDP_INPUT_CVBS_AIN7		0x06
#define ADV7482_SDP_INPUT_CVBS_AIN8		0x07
#define ADV7482_SDP_INPUT_SVIDEO_AIN1_AIN2	0x08
#define ADV7482_SDP_INPUT_SVIDEO_AIN3_AIN4	0x09
#define ADV7482_SDP_INPUT_SVIDEO_AIN5_AIN6	0x0a
#define ADV7482_SDP_INPUT_SVIDEO_AIN7_AIN8	0x0b
#define ADV7482_SDP_INPUT_YPRPB_AIN1_AIN2_AIN3	0x0c
#define ADV7482_SDP_INPUT_YPRPB_AIN4_AIN5_AIN6	0x0d
#define ADV7482_SDP_INPUT_DIFF_CVBS_AIN1_AIN2	0x0e
#define ADV7482_SDP_INPUT_DIFF_CVBS_AIN3_AIN4	0x0f
#define ADV7482_SDP_INPUT_DIFF_CVBS_AIN5_AIN6	0x10
#define ADV7482_SDP_INPUT_DIFF_CVBS_AIN7_AIN8	0x11

#define ADV7482_SDP_R_REG_10			0x10
#define ADV7482_SDP_R_REG_10_IN_LOCK		0x01
#define ADV7482_SDP_R_REG_10_FSC_LOCK		0x04

#define ADV7482_SDP_R_REG_10_AUTOD_MASK		0x70
#define ADV7482_SDP_R_REG_10_AUTOD_NTSM_M_J	0x00
#define ADV7482_SDP_R_REG_10_AUTOD_NTSC_4_43	0x10
#define ADV7482_SDP_R_REG_10_AUTOD_PAL_M	0x20
#define ADV7482_SDP_R_REG_10_AUTOD_PAL_60	0x30
#define ADV7482_SDP_R_REG_10_AUTOD_PAL_B_G	0x40
#define ADV7482_SDP_R_REG_10_AUTOD_SECAM	0x50
#define ADV7482_SDP_R_REG_10_AUTOD_PAL_COMB	0x60
#define ADV7482_SDP_R_REG_10_AUTOD_SECAM_525	0x70

#define ADV7482_MAX_WIDTH		1920
#define ADV7482_MAX_HEIGHT		1080

/****************************************/
/* ADV7482 structure definition         */
/****************************************/

/* Structure for register values */
struct adv7482_reg_value {
	u8 addr;				/* i2c slave address */
	u8 reg;					/* sub (register) address */
	u8 value;				/* register value */
};

#define END_REGISTER_TABLE \
{ADV7482_I2C_EOR, 0xFF, 0xFF} /* End of register table */

/* Register default values */

static const struct adv7482_reg_value adv7482_sw_reset[] = {

	{ADV7482_I2C_IO, 0xFF, 0xFF},	/* SW reset */
	{ADV7482_I2C_WAIT, 0x00, 0x05},	/* delay 5 */
	{ADV7482_I2C_IO, 0x01, 0x76},	/* ADI Required Write */
	{ADV7482_I2C_IO, 0xF2, 0x01},	/* Enable I2C Read Auto-Increment */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_set_slave_address[] = {

	/* I2C Slave Address settings */
	{ADV7482_I2C_IO, 0xF3, ADV7482_I2C_DPLL * 2},	/* DPLL Map */
	{ADV7482_I2C_IO, 0xF4, ADV7482_I2C_CP * 2},	/* CP Map */
	{ADV7482_I2C_IO, 0xF5, ADV7482_I2C_HDMI * 2},	/* HDMI Map */
	{ADV7482_I2C_IO, 0xF6, ADV7482_I2C_EDID * 2},	/* EDID Map */
	{ADV7482_I2C_IO, 0xF7, ADV7482_I2C_REPEATER * 2},
						/* HDMI RX Repeater Map */
	{ADV7482_I2C_IO, 0xF8, ADV7482_I2C_INFOFRAME * 2},
						/* HDMI RX InfoFrame Map */
	{ADV7482_I2C_IO, 0xFA, ADV7482_I2C_CEC * 2},	/* CEC Map */
	{ADV7482_I2C_IO, 0xFB, ADV7482_I2C_SDP * 2},	/* SDP Map */
	{ADV7482_I2C_IO, 0xFC, ADV7482_I2C_TXB * 2},	/* CSI-TXB Map */
	{ADV7482_I2C_IO, 0xFD, ADV7482_I2C_TXA * 2},	/* CSI-TXA Map */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

/* Supported Formats For Script Below */
/* - 01-29 HDMI to MIPI TxA CSI 4-Lane - RGB888: */
static const struct adv7482_reg_value adv7482_init_txa_4lane[] = {
	/* I2C Slave Address settings */
	{ADV7482_I2C_IO, 0xF3, ADV7482_I2C_DPLL * 2},	/* DPLL Map */
	{ADV7482_I2C_IO, 0xF4, ADV7482_I2C_CP * 2},	/* CP Map */
	{ADV7482_I2C_IO, 0xF5, ADV7482_I2C_HDMI * 2},	/* HDMI Map */
	{ADV7482_I2C_IO, 0xF6, ADV7482_I2C_EDID * 2},	/* EDID Map */
	{ADV7482_I2C_IO, 0xF7, ADV7482_I2C_REPEATER * 2},
						/* HDMI RX Repeater Map */
	{ADV7482_I2C_IO, 0xF8, ADV7482_I2C_INFOFRAME * 2},
						/* HDMI RX InfoFrame Map */
	{ADV7482_I2C_IO, 0xFA, ADV7482_I2C_CEC * 2},	/* CEC Map */
	{ADV7482_I2C_IO, 0xFB, ADV7482_I2C_SDP * 2},	/* SDP Map */
	{ADV7482_I2C_IO, 0xFC, ADV7482_I2C_TXB * 2},	/* CSI-TXB Map */
	{ADV7482_I2C_IO, 0xFD, ADV7482_I2C_TXA * 2},	/* CSI-TXA Map */

	/* Disable chip powerdown & Enable HDMI Rx block */
	{ADV7482_I2C_IO, 0x00, 0x40},

	{ADV7482_I2C_REPEATER, 0x40, 0x83},	/* Enable HDCP 1.1 */

	{ADV7482_I2C_HDMI, 0x00, 0x08},	/* Foreground Channel = A */
	{ADV7482_I2C_HDMI, 0x98, 0xFF},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x99, 0xA3},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x9A, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x9B, 0x0A},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x9D, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0xCB, 0x09},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x3D, 0x10},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x3E, 0x7B},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x3F, 0x5E},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x4E, 0xFE},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x4F, 0x18},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x57, 0xA3},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x58, 0x04},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0x85, 0x10},	/* ADI Required Write */

	{ADV7482_I2C_HDMI, 0x83, 0x00},	/* Enable All Terminations */
	{ADV7482_I2C_HDMI, 0xA3, 0x01},	/* ADI Required Write */
	{ADV7482_I2C_HDMI, 0xBE, 0x00},	/* ADI Required Write */

	{ADV7482_I2C_HDMI, 0x6C, 0x01},	/* HPA Manual Enable */
	{ADV7482_I2C_HDMI, 0xF8, 0x01},	/* HPA Asserted */
	{ADV7482_I2C_HDMI, 0x0F, 0x00},	/* Audio Mute Speed Set to Fastest */
					/* (Smallest Step Size) */

	/* EDID */
	/* Header information(0-19th byte) */
	{ADV7482_I2C_EDID, 0x00, 0x00},	/* Fixed header pattern */
	{ADV7482_I2C_EDID, 0x01, 0xFF},
	{ADV7482_I2C_EDID, 0x02, 0xFF},
	{ADV7482_I2C_EDID, 0x03, 0xFF},
	{ADV7482_I2C_EDID, 0x04, 0xFF},
	{ADV7482_I2C_EDID, 0x05, 0xFF},
	{ADV7482_I2C_EDID, 0x06, 0xFF},
	{ADV7482_I2C_EDID, 0x07, 0x00},
	{ADV7482_I2C_EDID, 0x08, 0x00},	/* Manufacturer ID */
	{ADV7482_I2C_EDID, 0x09, 0x00},
	{ADV7482_I2C_EDID, 0x0A, 0x00},	/* Manufacturer product code */
	{ADV7482_I2C_EDID, 0x0B, 0x00},
	{ADV7482_I2C_EDID, 0x0C, 0x00},	/* Serial number */
	{ADV7482_I2C_EDID, 0x0D, 0x00},
	{ADV7482_I2C_EDID, 0x0E, 0x00},
	{ADV7482_I2C_EDID, 0x0F, 0x00},
	{ADV7482_I2C_EDID, 0x10, 0x01},	/* Week of manufacture */
	{ADV7482_I2C_EDID, 0x11, 0x0C},	/* Year of manufacture */
	{ADV7482_I2C_EDID, 0x12, 0x01},	/* EDID version */
	{ADV7482_I2C_EDID, 0x13, 0x03},
	/* Basic display parameters(20-24th byte) */
	{ADV7482_I2C_EDID, 0x14, 0x80},	/* Video input parameters bitmap */
	{ADV7482_I2C_EDID, 0x15, 0x50},	/* Maximum horizontal image size */
	{ADV7482_I2C_EDID, 0x16, 0x2D},	/* Maximum vertical image size */
	{ADV7482_I2C_EDID, 0x17, 0x78},	/* Display gamma */
	{ADV7482_I2C_EDID, 0x18, 0x0A},	/* Supported features bitmap */
	/* Chromaticity coordinates(25-34th byte) */
	{ADV7482_I2C_EDID, 0x19, 0x0D},
	{ADV7482_I2C_EDID, 0x1A, 0xC9},
	{ADV7482_I2C_EDID, 0x1B, 0xA0},
	{ADV7482_I2C_EDID, 0x1C, 0x57},
	{ADV7482_I2C_EDID, 0x1D, 0x47},
	{ADV7482_I2C_EDID, 0x1E, 0x98},
	{ADV7482_I2C_EDID, 0x1F, 0x27},
	{ADV7482_I2C_EDID, 0x20, 0x12},
	{ADV7482_I2C_EDID, 0x21, 0x48},
	{ADV7482_I2C_EDID, 0x22, 0x4C},
	/* Established timing bitmap(35-37th byte) */
	{ADV7482_I2C_EDID, 0x23, 0x20},
	{ADV7482_I2C_EDID, 0x24, 0x00},
	{ADV7482_I2C_EDID, 0x25, 0x00},
	/* Standard timing information(38-53th byte) */
	/* Because they are unused, in this field, all values are 0101h. */
	{ADV7482_I2C_EDID, 0x26, 0x01},
	{ADV7482_I2C_EDID, 0x27, 0x01},
	{ADV7482_I2C_EDID, 0x28, 0x01},
	{ADV7482_I2C_EDID, 0x29, 0x01},
	{ADV7482_I2C_EDID, 0x2A, 0x01},
	{ADV7482_I2C_EDID, 0x2B, 0x01},
	{ADV7482_I2C_EDID, 0x2C, 0x01},
	{ADV7482_I2C_EDID, 0x2D, 0x01},
	{ADV7482_I2C_EDID, 0x2E, 0x01},
	{ADV7482_I2C_EDID, 0x2F, 0x01},
	{ADV7482_I2C_EDID, 0x30, 0x01},
	{ADV7482_I2C_EDID, 0x31, 0x01},
	{ADV7482_I2C_EDID, 0x32, 0x01},
	{ADV7482_I2C_EDID, 0x33, 0x01},
	{ADV7482_I2C_EDID, 0x34, 0x01},
	{ADV7482_I2C_EDID, 0x35, 0x01},
	/* Descriptor blocks of Descriptor 1(54-71th byte) */
	{ADV7482_I2C_EDID, 0x36, 0x02},
	{ADV7482_I2C_EDID, 0x37, 0x3A},
	{ADV7482_I2C_EDID, 0x38, 0x80},
	{ADV7482_I2C_EDID, 0x39, 0x18},
	{ADV7482_I2C_EDID, 0x3A, 0x71},
	{ADV7482_I2C_EDID, 0x3B, 0x38},
	{ADV7482_I2C_EDID, 0x3C, 0x2D},
	{ADV7482_I2C_EDID, 0x3D, 0x40},
	{ADV7482_I2C_EDID, 0x3E, 0x58},
	{ADV7482_I2C_EDID, 0x3F, 0x2C},
	{ADV7482_I2C_EDID, 0x40, 0x45},
	{ADV7482_I2C_EDID, 0x41, 0x00},
	{ADV7482_I2C_EDID, 0x42, 0xDC},
	{ADV7482_I2C_EDID, 0x43, 0x0B},
	{ADV7482_I2C_EDID, 0x44, 0x11},
	{ADV7482_I2C_EDID, 0x45, 0x00},
	{ADV7482_I2C_EDID, 0x46, 0x00},
	{ADV7482_I2C_EDID, 0x47, 0x1E},
	/* Descriptor blocks of Descriptor 2(72-89th byte) */
	{ADV7482_I2C_EDID, 0x48, 0x8C},
	{ADV7482_I2C_EDID, 0x49, 0x0A},
	{ADV7482_I2C_EDID, 0x4A, 0xD0},
	{ADV7482_I2C_EDID, 0x4B, 0x8A},
	{ADV7482_I2C_EDID, 0x4C, 0x20},
	{ADV7482_I2C_EDID, 0x4D, 0xE0},
	{ADV7482_I2C_EDID, 0x4E, 0x2D},
	{ADV7482_I2C_EDID, 0x4F, 0x10},
	{ADV7482_I2C_EDID, 0x50, 0x10},
	{ADV7482_I2C_EDID, 0x51, 0x3E},
	{ADV7482_I2C_EDID, 0x52, 0x96},
	{ADV7482_I2C_EDID, 0x53, 0x00},
	{ADV7482_I2C_EDID, 0x54, 0x58},
	{ADV7482_I2C_EDID, 0x55, 0xC2},
	{ADV7482_I2C_EDID, 0x56, 0x21},
	{ADV7482_I2C_EDID, 0x57, 0x00},
	{ADV7482_I2C_EDID, 0x58, 0x00},
	{ADV7482_I2C_EDID, 0x59, 0x18},
	/* Descriptor blocks of Descriptor 3(90-107th byte) */
	{ADV7482_I2C_EDID, 0x5A, 0x00},
	{ADV7482_I2C_EDID, 0x5B, 0x00},
	{ADV7482_I2C_EDID, 0x5C, 0x00},
	{ADV7482_I2C_EDID, 0x5D, 0x7C},
	{ADV7482_I2C_EDID, 0x5E, 0x00},
	{ADV7482_I2C_EDID, 0x5F, 0x4D},
	{ADV7482_I2C_EDID, 0x60, 0x59},
	{ADV7482_I2C_EDID, 0x61, 0x20},
	{ADV7482_I2C_EDID, 0x62, 0x48},
	{ADV7482_I2C_EDID, 0x63, 0x44},
	{ADV7482_I2C_EDID, 0x64, 0x54},
	{ADV7482_I2C_EDID, 0x65, 0x56},
	{ADV7482_I2C_EDID, 0x66, 0x0A},
	{ADV7482_I2C_EDID, 0x67, 0x20},
	{ADV7482_I2C_EDID, 0x68, 0x20},
	{ADV7482_I2C_EDID, 0x69, 0x20},
	{ADV7482_I2C_EDID, 0x6A, 0x20},
	{ADV7482_I2C_EDID, 0x6B, 0x20},
	/* Descriptor blocks of Descriptor 4(108-125th byte) */
	{ADV7482_I2C_EDID, 0x6C, 0x00},
	{ADV7482_I2C_EDID, 0x6D, 0x00},
	{ADV7482_I2C_EDID, 0x6E, 0x00},
	{ADV7482_I2C_EDID, 0x6F, 0x00},
	{ADV7482_I2C_EDID, 0x70, 0x00},
	{ADV7482_I2C_EDID, 0x71, 0x00},
	{ADV7482_I2C_EDID, 0x72, 0x00},
	{ADV7482_I2C_EDID, 0x73, 0x0A},
	{ADV7482_I2C_EDID, 0x74, 0x2E},
	{ADV7482_I2C_EDID, 0x75, 0x06},
	{ADV7482_I2C_EDID, 0x76, 0x00},
	{ADV7482_I2C_EDID, 0x77, 0x0A},
	{ADV7482_I2C_EDID, 0x78, 0x20},
	{ADV7482_I2C_EDID, 0x79, 0x20},
	{ADV7482_I2C_EDID, 0x7A, 0x20},
	{ADV7482_I2C_EDID, 0x7B, 0x20},
	{ADV7482_I2C_EDID, 0x7C, 0x20},
	{ADV7482_I2C_EDID, 0x7D, 0x20},
	/* Number of extensions to follow(126th byte) */
	{ADV7482_I2C_EDID, 0x7E, 0x01},	/* Extension enable */
	/* Checksum(127th byte) */
	{ADV7482_I2C_EDID, 0x7F, 0x75},

	/* CEA EDID Timing Extension Version 3 */
	{ADV7482_I2C_EDID, 0x80, 0x02},	/* CEA 861 Externsion Block */
	{ADV7482_I2C_EDID, 0x81, 0x03},
	{ADV7482_I2C_EDID, 0x82, 0x1E},
	{ADV7482_I2C_EDID, 0x83, 0x40},
	{ADV7482_I2C_EDID, 0x84, 0x83},
	{ADV7482_I2C_EDID, 0x85, 0x7F},
	{ADV7482_I2C_EDID, 0x86, 0x40},
	{ADV7482_I2C_EDID, 0x87, 0x05},
	{ADV7482_I2C_EDID, 0x88, 0x40},
	{ADV7482_I2C_EDID, 0x89, 0x48},
	{ADV7482_I2C_EDID, 0x8A, 0x10},	/* 1920x1080p @ 59.94/60 Hz */
	{ADV7482_I2C_EDID, 0x8B, 0x05},	/* 1920x1080i @ 59.94/60 Hz */
	{ADV7482_I2C_EDID, 0x8C, 0x04},	/* 1280x720p @ 59.94/60 Hz */
	{ADV7482_I2C_EDID, 0x8D, 0x01},	/* 640x480p @ 59.94/60 Hz */
	{ADV7482_I2C_EDID, 0x8E, 0x02},	/* 720x480p @ 59.94/60 Hz */
	{ADV7482_I2C_EDID, 0x8F, 0x06},	/* 720(1440)x480i @ 59.94/60 Hz */
	{ADV7482_I2C_EDID, 0x90, 0x15},	/* 720(1440)x576i @ 50 Hz */
	{ADV7482_I2C_EDID, 0x91, 0x11},	/* 720x576p @ 50 Hz */
	{ADV7482_I2C_EDID, 0x92, 0x00},
	{ADV7482_I2C_EDID, 0x93, 0x00},
	{ADV7482_I2C_EDID, 0x94, 0x00},
	{ADV7482_I2C_EDID, 0x95, 0x00},
	{ADV7482_I2C_EDID, 0x96, 0x00},
	{ADV7482_I2C_EDID, 0x97, 0x00},
	{ADV7482_I2C_EDID, 0x98, 0x00},
	{ADV7482_I2C_EDID, 0x99, 0x00},
	{ADV7482_I2C_EDID, 0x9A, 0x00},
	{ADV7482_I2C_EDID, 0x9B, 0x00},
	{ADV7482_I2C_EDID, 0x9C, 0x00},
	{ADV7482_I2C_EDID, 0x9D, 0x00},
	{ADV7482_I2C_EDID, 0x9E, 0x00},
	{ADV7482_I2C_EDID, 0x9F, 0x00},
	{ADV7482_I2C_EDID, 0xA0, 0x00},
	{ADV7482_I2C_EDID, 0xA1, 0x00},
	{ADV7482_I2C_EDID, 0xA2, 0x00},
	{ADV7482_I2C_EDID, 0xA3, 0x00},
	{ADV7482_I2C_EDID, 0xA4, 0x00},
	{ADV7482_I2C_EDID, 0xA5, 0x00},
	{ADV7482_I2C_EDID, 0xA6, 0x00},
	{ADV7482_I2C_EDID, 0xA7, 0x00},
	{ADV7482_I2C_EDID, 0xA8, 0x00},
	{ADV7482_I2C_EDID, 0xA9, 0x00},
	{ADV7482_I2C_EDID, 0xAA, 0x00},
	{ADV7482_I2C_EDID, 0xAB, 0x00},
	{ADV7482_I2C_EDID, 0xAC, 0x00},
	{ADV7482_I2C_EDID, 0xAD, 0x00},
	{ADV7482_I2C_EDID, 0xAE, 0x00},
	{ADV7482_I2C_EDID, 0xAF, 0x00},
	{ADV7482_I2C_EDID, 0xB0, 0x00},
	{ADV7482_I2C_EDID, 0xB1, 0x00},
	{ADV7482_I2C_EDID, 0xB2, 0x00},
	{ADV7482_I2C_EDID, 0xB3, 0x00},
	{ADV7482_I2C_EDID, 0xB4, 0x00},
	{ADV7482_I2C_EDID, 0xB5, 0x00},
	{ADV7482_I2C_EDID, 0xB6, 0x00},
	{ADV7482_I2C_EDID, 0xB7, 0x00},
	{ADV7482_I2C_EDID, 0xB8, 0x00},
	{ADV7482_I2C_EDID, 0xB9, 0x00},
	{ADV7482_I2C_EDID, 0xBA, 0x20},
	{ADV7482_I2C_EDID, 0xBB, 0x20},
	{ADV7482_I2C_EDID, 0xBC, 0x20},
	{ADV7482_I2C_EDID, 0xBD, 0x20},
	{ADV7482_I2C_EDID, 0xBE, 0x20},
	{ADV7482_I2C_EDID, 0xBF, 0x20},
	{ADV7482_I2C_EDID, 0xC0, 0x20},
	{ADV7482_I2C_EDID, 0xC1, 0x20},
	{ADV7482_I2C_EDID, 0xC2, 0x20},
	{ADV7482_I2C_EDID, 0xC3, 0x20},
	{ADV7482_I2C_EDID, 0xC4, 0x20},
	{ADV7482_I2C_EDID, 0xC5, 0x20},
	{ADV7482_I2C_EDID, 0xC6, 0x00},
	{ADV7482_I2C_EDID, 0xC7, 0x00},
	{ADV7482_I2C_EDID, 0xC8, 0x00},
	{ADV7482_I2C_EDID, 0xC9, 0xFF},
	{ADV7482_I2C_EDID, 0xCA, 0x00},
	{ADV7482_I2C_EDID, 0xCB, 0x0A},
	{ADV7482_I2C_EDID, 0xCC, 0x20},
	{ADV7482_I2C_EDID, 0xCD, 0x20},
	{ADV7482_I2C_EDID, 0xCE, 0x20},
	{ADV7482_I2C_EDID, 0xCF, 0x20},
	{ADV7482_I2C_EDID, 0xD0, 0x20},
	{ADV7482_I2C_EDID, 0xD1, 0x20},
	{ADV7482_I2C_EDID, 0xD2, 0x20},
	{ADV7482_I2C_EDID, 0xD3, 0x20},
	{ADV7482_I2C_EDID, 0xD4, 0x20},
	{ADV7482_I2C_EDID, 0xD5, 0x20},
	{ADV7482_I2C_EDID, 0xD6, 0x20},
	{ADV7482_I2C_EDID, 0xD7, 0x20},
	{ADV7482_I2C_EDID, 0xD8, 0x00},
	{ADV7482_I2C_EDID, 0xD9, 0x00},
	{ADV7482_I2C_EDID, 0xDA, 0x00},
	{ADV7482_I2C_EDID, 0xDB, 0x1B},
	{ADV7482_I2C_EDID, 0xDC, 0x00},
	{ADV7482_I2C_EDID, 0xDD, 0x0A},
	{ADV7482_I2C_EDID, 0xDE, 0x20},
	{ADV7482_I2C_EDID, 0xDF, 0x20},
	{ADV7482_I2C_EDID, 0xE0, 0x20},
	{ADV7482_I2C_EDID, 0xE1, 0x20},
	{ADV7482_I2C_EDID, 0xE2, 0x20},
	{ADV7482_I2C_EDID, 0xE3, 0x20},
	{ADV7482_I2C_EDID, 0xE4, 0x20},
	{ADV7482_I2C_EDID, 0xE5, 0x20},
	{ADV7482_I2C_EDID, 0xE6, 0x20},
	{ADV7482_I2C_EDID, 0xE7, 0x20},
	{ADV7482_I2C_EDID, 0xE8, 0x20},
	{ADV7482_I2C_EDID, 0xE9, 0x20},
	{ADV7482_I2C_EDID, 0xEA, 0x00},
	{ADV7482_I2C_EDID, 0xEB, 0x00},
	{ADV7482_I2C_EDID, 0xEC, 0x00},
	{ADV7482_I2C_EDID, 0xED, 0x00},
	{ADV7482_I2C_EDID, 0xEE, 0x00},
	{ADV7482_I2C_EDID, 0xEF, 0x00},
	{ADV7482_I2C_EDID, 0xF0, 0x00},
	{ADV7482_I2C_EDID, 0xF1, 0x00},
	{ADV7482_I2C_EDID, 0xF2, 0x00},
	{ADV7482_I2C_EDID, 0xF3, 0x00},
	{ADV7482_I2C_EDID, 0xF4, 0x00},
	{ADV7482_I2C_EDID, 0xF5, 0x00},
	{ADV7482_I2C_EDID, 0xF6, 0x00},
	{ADV7482_I2C_EDID, 0xF7, 0x00},
	{ADV7482_I2C_EDID, 0xF8, 0x00},
	{ADV7482_I2C_EDID, 0xF9, 0x00},
	{ADV7482_I2C_EDID, 0xFA, 0x00},
	{ADV7482_I2C_EDID, 0xFB, 0x00},
	{ADV7482_I2C_EDID, 0xFC, 0x00},
	{ADV7482_I2C_EDID, 0xFD, 0x00},
	{ADV7482_I2C_EDID, 0xFE, 0x00},
	{ADV7482_I2C_EDID, 0xFF, 0xD8},	/* Set the Most Significant Bit */

	{ADV7482_I2C_REPEATER, 0x70, 0x10},
					/* EDID image, in 128-byte blocks. */
	{ADV7482_I2C_REPEATER, 0x74, 0x01},
				/* Manual enable active for E-EDID on Port A */

	{ADV7482_I2C_IO, 0x04, 0x02},	/* RGB Out of CP */
	{ADV7482_I2C_IO, 0x12, 0xF0},
		/* CSC Depends on ip Packets - SDR 444 */
	{ADV7482_I2C_IO, 0x17, 0x80},
		/* Luma & Chroma Values Can Reach 254d */
	{ADV7482_I2C_IO, 0x03, 0x86},	/* CP-Insert_AV_Code */

	{ADV7482_I2C_CP, 0x7C, 0x00},	/* ADI Required Write */

	{ADV7482_I2C_IO, 0x0C, 0xE0},	/* Enable LLC_DLL & Double LLC Timing */
	{ADV7482_I2C_IO, 0x0E, 0xDD},	/* LLC/PIX/SPI PINS TRISTATED AUD */
					/* Outputs Enabled */
	{ADV7482_I2C_IO, 0x10, 0xA0},	/* Enable 4-lane CSI Tx & Pixel Port */

	{ADV7482_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXA, 0x00, 0xA4},	/* Set Auto DPHY Timing */
	{ADV7482_I2C_TXA, 0xDB, 0x10},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xD6, 0x07},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xC4, 0x0A},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x71, 0x33},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x72, 0x11},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xF0, 0x00},	/* i2c_dphy_pwdn - 1'b0 */

	{ADV7482_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXA, 0x00, 0x24 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0x31, 0x80},	/* ADI Required Write */

	/* for debug */
#ifdef REL_DGB_FORCE_TO_SEND_COLORBAR
	{ADV7482_I2C_CP, 0x37, 0x81},	/* Output Colorbars Pattern */
#endif
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

/* 02-01 Analog CVBS to MIPI TX-B CSI 1-Lane - */
/* Autodetect CVBS Single Ended In Ain 1 - MIPI Out */
static const struct adv7482_reg_value adv7482_init_txb_1lane[] = {

	{ADV7482_I2C_IO, 0x00, 0x30},
		/* Disable chip powerdown - powerdown Rx */
	{ADV7482_I2C_IO, 0xF2, 0x01},	/* Enable I2C Read Auto-Increment */

	/* I2C Slave Address settings */
	{ADV7482_I2C_IO, 0xF3, ADV7482_I2C_DPLL * 2},	/* DPLL Map */
	{ADV7482_I2C_IO, 0xF4, ADV7482_I2C_CP * 2},	/* CP Map */
	{ADV7482_I2C_IO, 0xF5, ADV7482_I2C_HDMI * 2},	/* HDMI Map */
	{ADV7482_I2C_IO, 0xF6, ADV7482_I2C_EDID * 2},	/* EDID Map */
	{ADV7482_I2C_IO, 0xF7, ADV7482_I2C_REPEATER * 2},
						/* HDMI RX Repeater Map */
	{ADV7482_I2C_IO, 0xF8, ADV7482_I2C_INFOFRAME * 2},
						/* HDMI RX InfoFrame Map */
	{ADV7482_I2C_IO, 0xFA, ADV7482_I2C_CEC * 2},	/* CEC Map */
	{ADV7482_I2C_IO, 0xFB, ADV7482_I2C_SDP * 2},	/* SDP Map */
	{ADV7482_I2C_IO, 0xFC, ADV7482_I2C_TXB * 2},	/* CSI-TXB Map */
	{ADV7482_I2C_IO, 0xFD, ADV7482_I2C_TXA * 2},	/* CSI-TXA Map */

	{ADV7482_I2C_IO, 0x0E, 0xFF},	/* LLC/PIX/AUD/SPI PINS TRISTATED */

	{ADV7482_I2C_SDP, ADV7482_SDP_REG_PWR_MAN, ADV7482_SDP_PWR_MAN_ON},
		/* Exit Power Down Mode */
	{ADV7482_I2C_SDP, 0x52, 0xCD},	/* ADI Required Write */
	{ADV7482_I2C_SDP, ADV7482_SDP_REG_INPUT_CONTROL,
		ADV7482_SDP_INPUT_CVBS_AIN8},	/* INSEL = CVBS in on Ain 8 */
	{ADV7482_I2C_SDP, ADV7482_SDP_REG_CTRL, 0x80},
		/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x9C, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x9C, 0xFF},	/* ADI Required Write */
	{ADV7482_I2C_SDP, ADV7482_SDP_REG_CTRL, ADV7482_SDP_MAIN_MAP_RW},
		/* ADI Required Write */

	/* ADI recommended writes for improved video quality */
	{ADV7482_I2C_SDP, 0x80, 0x51},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x81, 0x51},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0x82, 0x68},	/* ADI Required Write */

	{ADV7482_I2C_SDP, 0x03, 0x42},
		/* Tri-S Output Drivers, PwrDwn 656 pads */
	{ADV7482_I2C_SDP, 0x04, 0xB5},	/* ITU-R BT.656-4 compatible */
	{ADV7482_I2C_SDP, 0x13, 0x00},	/* ADI Required Write */

	{ADV7482_I2C_SDP, 0x17, 0x41},	/* Select SH1 */
	{ADV7482_I2C_SDP, 0x31, 0x12},	/* ADI Required Write */
	{ADV7482_I2C_SDP, 0xE6, 0x4F},
		/* Set V bit end position manually in NTSC mode */

#ifdef REL_DGB_FORCE_TO_SEND_COLORBAR
	{ADV7482_I2C_SDP, 0x0C, 0x01},	/* ColorBar */
	{ADV7482_I2C_SDP, 0x14, 0x01},	/* ColorBar */
#endif
	/* Enable 1-Lane MIPI Tx, */
	/* enable pixel output and route SD through Pixel port */
	{ADV7482_I2C_IO, 0x10, 0x70},

	{ADV7482_I2C_TXB, 0x00, 0x81},	/* Enable 1-lane MIPI */
	{ADV7482_I2C_TXB, 0x00, 0xA1},	/* Set Auto DPHY Timing */
#if 0 /* If CVBS input only is used, please enable this code. */
	{ADV7482_I2C_TXA, 0xF0, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xD6, 0x07},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xC0, 0x3C},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xC3, 0x3C},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xC6, 0x3C},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xC9, 0x3C},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xCC, 0x3C},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xD5, 0x03},	/* ADI Required Write */
#endif
	{ADV7482_I2C_TXB, 0xD2, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xC4, 0x0A},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x71, 0x33},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x72, 0x11},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xF0, 0x00},	/* i2c_dphy_pwdn - 1'b0 */
	{ADV7482_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */

	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXB, 0x00, 0x21 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0x31, 0x80},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_up_txa_4lane[] = {

	{ADV7482_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXA, 0x00, 0xA4},	/* Set Auto DPHY Timing */

	{ADV7482_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXA, 0x00, 0x24 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXA, 0x31, 0x80},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_down_txa_4lane[] = {

	{ADV7482_I2C_TXA, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x1E, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_TXA, 0x00, 0x84},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXA, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_TXA, 0xC1, 0x3B},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_up_txb_1lane[] = {

	{ADV7482_I2C_TXB, 0x00, 0x81},	/* Enable 1-lane MIPI */
	{ADV7482_I2C_TXB, 0x00, 0xA1},	/* Set Auto DPHY Timing */

	{ADV7482_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x1E, 0x40},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_WAIT, 0x00, 0x02},	/* delay 2 */
	{ADV7482_I2C_TXB, 0x00, 0x21 },	/* Power-up CSI-TX */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0xC1, 0x2B},	/* ADI Required Write */
	{ADV7482_I2C_WAIT, 0x00, 0x01},	/* delay 1 */
	{ADV7482_I2C_TXB, 0x31, 0x80},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_down_txb_1lane[] = {

	{ADV7482_I2C_TXB, 0x31, 0x82},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x1E, 0x00},	/* ADI Required Write */
	{ADV7482_I2C_TXB, 0x00, 0x81},	/* Enable 4-lane MIPI */
	{ADV7482_I2C_TXB, 0xDA, 0x01},	/* i2c_mipi_pll_en - 1'b1 */
	{ADV7482_I2C_TXB, 0xC1, 0x3B},	/* ADI Required Write */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_up_hdmi_rx[] = {

	/* Disable chip powerdown & Enable HDMI Rx block */
	{ADV7482_I2C_IO, 0x00, 0x40},

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_power_down_hdmi_rx[] = {

	{ADV7482_I2C_IO, 0x00, 0x30},	/* Disable chip powerdown */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_enable_csi4_csi1[] = {

	{ADV7482_I2C_IO, 0x10, 0xE0},	/* Enable 4-lane CSI Tx & Pixel Port */

	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_set_virtual_channel0[] = {

	{ADV7482_I2C_TXB, 0x0D, 0x00},	/* Set virtual channel 0 */
	{ADV7482_I2C_TXA, 0x0D, 0x00},	/* Set virtual channel 0 */
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_set_virtual_channel1[] = {

	{ADV7482_I2C_TXB, 0x0D, 0x40},	/* Set virtual channel 1 */
	{ADV7482_I2C_TXA, 0x0D, 0x40},	/* Set virtual channel 1 */
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_set_virtual_channel2[] = {

	{ADV7482_I2C_TXB, 0x0D, 0x80},	/* Set virtual channel 2 */
	{ADV7482_I2C_TXA, 0x0D, 0x80},	/* Set virtual channel 2 */
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

static const struct adv7482_reg_value adv7482_set_virtual_channel3[] = {

	{ADV7482_I2C_TXB, 0x0D, 0xC0},	/* Set virtual channel 3 */
	{ADV7482_I2C_TXA, 0x0D, 0xC0},	/* Set virtual channel 3 */
	{ADV7482_I2C_EOR, 0xFF, 0xFF}	/* End of register table */
};

enum decoder_input_interface {
	DECODER_INPUT_INTERFACE_RGB888,
	DECODER_INPUT_INTERFACE_YCBCR422,
};

/**
 * struct adv7482_link_config - Describes adv7482 hardware configuration
 * @input_colorspace:		The input colorspace (RGB, YUV444, YUV422)
 */
struct adv7482_link_config {
	enum decoder_input_interface	input_interface;
	struct adv7482_reg_value	*regs;

	struct adv7482_reg_value	*power_up;
	struct adv7482_reg_value	*power_down;

	int (*init_device)(void *);
	int (*init_controls)(void *);
	int (*s_power)(void *);
	int (*s_ctrl)(void *);
	int (*enum_mbus_code)(void *);
	int (*set_pad_format)(void *);
	int (*get_pad_format)(void *);
	int (*s_std)(void *);
	int (*querystd)(void *);
	int (*g_input_status)(void *);
	int (*s_routing)(void *);
	int (*g_mbus_config)(void *);

	struct device	*dev;
	bool		sw_reset;
	bool		hdmi_in;
	bool		sdp_in;
	int		vc_ch;
};

struct adv7482_state {
	struct v4l2_ctrl_handler		ctrl_hdl;
	struct v4l2_subdev			sd;
	struct media_pad			pad;
	 /* mutual excl. when accessing chip */
	struct mutex				mutex;
	int					irq;
	v4l2_std_id				curr_norm;
	bool					streaming;
	const struct adv7482_color_format	*cfmt;
	u32					width;
	u32					height;

	struct i2c_client			*client;
	unsigned int				register_page;
	struct i2c_client			*csi_client;
	enum v4l2_field				field;

	struct device				*dev;
	struct adv7482_link_config		mipi_csi2_link[2];
};

/*****************************************************************************/
/*  Private functions                                                        */
/*****************************************************************************/

#define to_adv7482_sd(_ctrl) (&container_of(_ctrl->handler,		\
					    struct adv7482_state,	\
					    ctrl_hdl)->sd)

static inline struct adv7482_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7482_state, sd);
}

/*
 * adv7482_write_registers() - Write adv7482 device registers
 * @client: pointer to i2c_client structure
 * @regs: pointer to adv7482_reg_value structure
 *
 * Write the specified adv7482 register's values.
 */
static int adv7482_write_registers(struct i2c_client *client,
					const struct adv7482_reg_value *regs)
{
	struct i2c_msg msg;
	u8 data_buf[2];
	int ret = -EINVAL;

	if (!client->adapter)
		return -ENODEV;

	msg.flags = 0;
	msg.len = 2;
	msg.buf = &data_buf[0];

	while (regs->addr != ADV7482_I2C_EOR) {

		if (regs->addr == ADV7482_I2C_WAIT)
			msleep(regs->value);
		else {
			msg.addr = regs->addr;
			data_buf[0] = regs->reg;
			data_buf[1] = regs->value;

			ret = i2c_transfer(client->adapter, &msg, 1);
			if (ret < 0)
				break;
		}
		regs++;
	}

	return (ret < 0) ? ret : 0;
}

/*
 * adv7482_write_register() - Write adv7482 device register
 * @client: pointer to i2c_client structure
 * @addr: i2c slave address of adv7482 device
 * @reg: adv7482 device register address
 * @value: the value to be written
 *
 * Write the specified adv7482 register's value.
 */
static int adv7482_write_register(struct i2c_client *client,
		u8 addr, u8 reg, u8 value)
{
	struct adv7482_reg_value regs[2];
	int ret;

	regs[0].addr = addr;
	regs[0].reg = reg;
	regs[0].value = value;
	regs[1].addr = ADV7482_I2C_EOR;
	regs[1].reg = 0xFF;
	regs[1].value = 0xFF;

	ret = adv7482_write_registers(client, regs);

	return ret;
}

/*
 * adv7482_read_register() - Read adv7482 device register
 * @client: pointer to i2c_client structure
 * @addr: i2c slave address of adv7482 device
 * @reg: adv7482 device register address
 * @value: pointer to the value
 *
 * Read the specified adv7482 register's value.
 */
static int adv7482_read_register(struct i2c_client *client,
		u8 addr, u8 reg, u8 *value)
{
	struct i2c_msg msg[2];
	u8 reg_buf, data_buf;
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg_buf;
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &data_buf;

	reg_buf = reg;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0)
		return ret;

	*value = data_buf;

	return (ret < 0) ? ret : 0;
}

static int adv7482_read_sdp_main_info(struct i2c_client *client,
		struct adv7482_sdp_main_info *info)
{
	int ret;
	u8 value;

	ret = adv7482_write_register(client, ADV7482_I2C_SDP,
				ADV7482_SDP_REG_CTRL, ADV7482_SDP_RO_MAIN_MAP);
	if (ret < 0)
		return ret;

	/* status_reg_10 */
	ret = adv7482_read_register(client, ADV7482_I2C_SDP,
			ADV7482_SDP_R_REG_10, &value);
	if (ret < 0)
		return ret;

	info->status_reg_10 = value;

	return ret;
}

static int adv7482_set_video_standard(struct adv7482_state *state,
				      unsigned int std)
{
	int ret;
	u8 value;

	ret = adv7482_read_register(state->client, ADV7482_I2C_SDP,
				ADV7482_SDP_REG_INPUT_VIDSEL, &value);
	if (ret < 0)
		return ret;

	value &= 0xf;
	value |= std << 4;

	return adv7482_write_register(state->client, ADV7482_I2C_SDP,
				ADV7482_SDP_REG_INPUT_VIDSEL, value);
}

static v4l2_std_id adv7482_std_to_v4l2(u8 status_reg_10)
{
	/* in case V4L2_IN_ST_NO_SIGNAL */
	if (!(status_reg_10 & ADV7482_SDP_R_REG_10_IN_LOCK))
		return V4L2_STD_UNKNOWN;

	switch (status_reg_10 & ADV7482_SDP_R_REG_10_AUTOD_MASK) {
	case ADV7482_SDP_R_REG_10_AUTOD_NTSM_M_J:
		return V4L2_STD_NTSC;
	case ADV7482_SDP_R_REG_10_AUTOD_NTSC_4_43:
		return V4L2_STD_NTSC_443;
	case ADV7482_SDP_R_REG_10_AUTOD_PAL_M:
		return V4L2_STD_PAL_M;
	case ADV7482_SDP_R_REG_10_AUTOD_PAL_60:
		return V4L2_STD_PAL_60;
	case ADV7482_SDP_R_REG_10_AUTOD_PAL_B_G:
		return V4L2_STD_PAL;
	case ADV7482_SDP_R_REG_10_AUTOD_SECAM:
		return V4L2_STD_SECAM;
	case ADV7482_SDP_R_REG_10_AUTOD_PAL_COMB:
		return V4L2_STD_PAL_Nc | V4L2_STD_PAL_N;
	case ADV7482_SDP_R_REG_10_AUTOD_SECAM_525:
		return V4L2_STD_SECAM;
	default:
		return V4L2_STD_UNKNOWN;
	}
}

static int v4l2_std_to_adv7482(v4l2_std_id std)
{
	if (std == V4L2_STD_PAL_60)
		return ADV7482_SDP_STD_PAL60;
	if (std == V4L2_STD_NTSC_443)
		return ADV7482_SDP_STD_NTSC_443;
	if (std == V4L2_STD_PAL_N)
		return ADV7482_SDP_STD_PAL_N;
	if (std == V4L2_STD_PAL_M)
		return ADV7482_SDP_STD_PAL_M;
	if (std == V4L2_STD_PAL_Nc)
		return ADV7482_SDP_STD_PAL_COMB_N;
	if (std & V4L2_STD_PAL)
		return ADV7482_SDP_STD_PAL_BG;
	if (std & V4L2_STD_NTSC)
		return ADV7482_SDP_STD_NTSC_M;
	if (std & V4L2_STD_SECAM)
		return ADV7482_SDP_STD_PAL_SECAM;

	return -EINVAL;
}

static u32 adv7482_status_to_v4l2(u8 status_reg_10)
{
	if (!(status_reg_10 & ADV7482_SDP_R_REG_10_IN_LOCK))
		return V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static int __adv7482_status(struct adv7482_state *state, u32 *status,
			    v4l2_std_id *std)
{
	int ret;
	u8 status_reg_10;
	struct adv7482_sdp_main_info sdp_info;

	ret = adv7482_read_sdp_main_info(state->client, &sdp_info);
	if (ret < 0)
		return ret;

	status_reg_10 = sdp_info.status_reg_10;

	if (status)
		*status = adv7482_status_to_v4l2(status_reg_10);
	if (std)
		*std = adv7482_std_to_v4l2(status_reg_10);

	return 0;
}

/*
 * adv7482_get_vid_info() - Get video information
 * @sd: pointer to standard V4L2 sub-device structure
 * @progressive: progressive or interlace
 * @width: line width
 * @height: lines per frame
 *
 * Get video information.
 */
static int adv7482_get_vid_info(struct v4l2_subdev *sd, u8 *progressive,
				u32 *width, u32 *height, u8 *signal)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 hdmi_int;
	u8 msb;
	u8 lsb;
	int ret;

	if (signal)
		*signal = 0;

	/* decide line width */
	ret = adv7482_read_register(client, ADV7482_I2C_HDMI,
			ADV7482_HDMI_STATUS1_REG, &msb);
	if (ret < 0)
		return ret;

	if (!(msb & ADV7482_HDMI_VF_LOCKED_FLG) ||
	    !(msb & ADV7482_HDMI_DERF_LOCKED_FLG))
		return -EIO;

	if (signal)
		*signal = 1;

	/* decide interlaced or progressive */
	ret = adv7482_read_register(client, ADV7482_I2C_HDMI,
			ADV7482_HDMI_STATUS2_REG, &hdmi_int);
	if (ret < 0)
		return ret;

	*progressive =  1;
	if ((hdmi_int & ADV7482_HDMI_IP_FLAG) != 0)
		*progressive =  0;

	ret = adv7482_read_register(client, ADV7482_I2C_HDMI,
			ADV7482_HDMI_LWIDTH_REG, &lsb);
	if (ret < 0)
		return ret;

	*width = (u32)(ADV7482_HDMI_LWIDTH_MSBS_MASK & msb);
	*width = (lsb | (*width << 8));

	/* decide lines per frame */
	ret = adv7482_read_register(client, ADV7482_I2C_HDMI,
			ADV7482_HDMI_F0HEIGHT_MSBS_REG, &msb);
	if (ret < 0)
		return ret;

	ret = adv7482_read_register(client, ADV7482_I2C_HDMI,
			ADV7482_HDMI_F0HEIGHT_LSBS_REG, &lsb);
	if (ret < 0)
		return ret;

	*height = (u32)(ADV7482_HDMI_F0HEIGHT_MSBS_MASK & msb);
	*height = (lsb | (*height << 8));
	if (!(*progressive))
		*height = *height * 2;

	if (*width == 0 || *height == 0)
		return -EIO;

	return 0;
}

static int adv7482_set_vid_info(struct v4l2_subdev *sd)
{
	struct adv7482_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	u8 progressive;
	u8 signal;
	u8 val = ADV7482_IO_CP_VID_STD_480P;
	u32 width;
	u32 height;
	int ret;

	/* Get video information */
	ret = adv7482_get_vid_info(sd, &progressive, &width, &height, &signal);
	if (ret < 0) {
		width		= ADV7482_MAX_WIDTH;
		height		= ADV7482_MAX_HEIGHT;
		progressive	= 1;
	} else {
		if ((width == 640) &&
			(height == 480) && (progressive == 1)) {
			val = ADV7482_IO_CP_VID_STD_VGA60;
			dev_info(state->dev,
				 "Changed active resolution to 640x480p\n");
		} else if ((width == 720) &&
			(height == 480) && (progressive == 1)) {
			val = ADV7482_IO_CP_VID_STD_480P;
			dev_info(state->dev,
				 "Changed active resolution to 720x480p\n");
		} else if ((width == 720) &&
			(height == 576) && (progressive == 1)) {
			val = ADV7482_IO_CP_VID_STD_576P;
			dev_info(state->dev,
				 "Changed active resolution to 720x576p\n");
		} else if ((width == 1280) &&
			(height == 720) && (progressive == 1)) {
			val = ADV7482_IO_CP_VID_STD_720P;
			dev_info(state->dev,
				 "Changed active resolution to 1280x720p\n");
		} else if ((width == 1920) &&
			(height == 1080) && (progressive == 1)) {
			val = ADV7482_IO_CP_VID_STD_1080P;
			dev_info(state->dev,
				 "Changed active resolution to 1920x1080p\n");
		} else if ((width == 1920) &&
			(height == 1080) && (progressive == 0)) {
			val = ADV7482_IO_CP_VID_STD_1080I;
			dev_info(state->dev,
				 "Changed active resolution to 1920x1080i\n");
		} else {
			dev_err(state->dev,
				 "Not support resolution %dx%d%c\n",
				  width, height, (progressive) ? 'p' : 'i');
			return -EINVAL;
		}
	}

	/*
	 * The resolution of 720p, 1080i and 1080p is Hsync width of 40 pixel
	 * clock cycles. These resolutions must be shifted horizontally to
	 * the left in active video mode.
	 */
	if ((val == ADV7482_IO_CP_VID_STD_1080I) ||
		(val == ADV7482_IO_CP_VID_STD_1080P)) {
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8B, 0x43);
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8C, 0xD4);
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8B, 0x4F);
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8D, 0xD4);
	} else if (val == ADV7482_IO_CP_VID_STD_720P) {
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8B, 0x43);
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8C, 0xD8);
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8B, 0x4F);
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8D, 0xD8);
	} else {
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8B, 0x40);
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8C, 0x00);
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8B, 0x40);
		ret = adv7482_write_register(client,
					ADV7482_I2C_CP, 0x8D, 0x00);
	}

	ret = adv7482_write_register(client, ADV7482_I2C_IO,
				ADV7482_IO_CP_VID_STD_REG, val);

	return ret;
}

/*****************************************************************************/
/*  V4L2 decoder i/f handler for v4l2_subdev_core_ops                        */
/*****************************************************************************/

/*
 * adv7482_querystd() - V4L2 decoder i/f handler for querystd
 * @sd: ptr to v4l2_subdev struct
 * @std: standard input video id
 *
 * Obtains the video standard input id
 */
static int adv7482_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct adv7482_state *state = to_state(sd);
	struct adv7482_link_config *config = &state->mipi_csi2_link[0];
	int err = mutex_lock_interruptible(&state->mutex);

	if (err)
		return err;

	if (config->input_interface != DECODER_INPUT_INTERFACE_YCBCR422) {
		*std = V4L2_STD_ATSC;
		goto unlock;
	}

	if (state->streaming) {
		err = -EBUSY;
		goto unlock;
	}

	err = adv7482_set_video_standard(state,
				ADV7482_SDP_STD_AD_PAL_BG_NTSC_J_SECAM);
	if (err)
		goto unlock;

	msleep(100);
	err = __adv7482_status(state, NULL, std);
	if (err)
		goto unlock;

	err = v4l2_std_to_adv7482(state->curr_norm);
	if (err < 0)
		goto unlock;

	err = adv7482_set_video_standard(state, err);

unlock:
	mutex_unlock(&state->mutex);

	return err;
}

/*
 * adv7482_g_input_status() - V4L2 decoder i/f handler for g_input_status
 * @sd: ptr to v4l2_subdev struct
 * @status: video input status flag
 *
 * Obtains the video input status flags.
 */
static int adv7482_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct adv7482_state *state = to_state(sd);
	struct adv7482_link_config *config = &state->mipi_csi2_link[0];
	u8 status1 = 0;
	int ret = mutex_lock_interruptible(&state->mutex);

	if (ret)
		return ret;

	if (config->input_interface == DECODER_INPUT_INTERFACE_YCBCR422)
		ret = __adv7482_status(state, status, NULL);
	else {
		ret = adv7482_read_register(client, ADV7482_I2C_HDMI,
				ADV7482_HDMI_STATUS1_REG, &status1);
		if (ret < 0)
			goto out;

		if (!(status1 & ADV7482_HDMI_VF_LOCKED_FLG))
			*status = V4L2_IN_ST_NO_SIGNAL;
		else if (!(status1 & ADV7482_HDMI_DERF_LOCKED_FLG))
			*status = V4L2_IN_ST_NO_SIGNAL;
		else
			*status = 0;
	}

	ret = 0;
out:
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7482_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct adv7482_state *state = to_state(sd);
	struct adv7482_link_config *config = &state->mipi_csi2_link[0];

	if (code->index != 0)
		return -EINVAL;

	if (config->input_interface == DECODER_INPUT_INTERFACE_YCBCR422)
		code->code = MEDIA_BUS_FMT_YUYV8_2X8;
	else
		code->code = MEDIA_BUS_FMT_RGB888_1X24;

	return 0;
}

static int adv7482_mbus_fmt(struct v4l2_subdev *sd,
			    struct v4l2_mbus_framefmt *fmt)
{
	struct adv7482_state *state = to_state(sd);

	u8 progressive;
	u8 signal;
	u32 width;
	u32 height;
	int ret;
	struct adv7482_link_config *config = &state->mipi_csi2_link[0];
	u8 status_reg_10;
	struct adv7482_sdp_main_info sdp_info;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (config->input_interface == DECODER_INPUT_INTERFACE_YCBCR422) {
		ret = __adv7482_status(state, NULL, &state->curr_norm);
		if (ret < 0)
			return ret;

		fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
		fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
		fmt->width  = 720;
		fmt->height = state->curr_norm & V4L2_STD_525_60
							 ? 480 : 576;

		/* Get video information */
		ret = adv7482_read_sdp_main_info(client, &sdp_info);
		if (ret < 0)
			return ret;

		status_reg_10 = sdp_info.status_reg_10;

		if ((status_reg_10 & ADV7482_SDP_R_REG_10_IN_LOCK) &&
			(status_reg_10 & ADV7482_SDP_R_REG_10_FSC_LOCK)
			&& (((status_reg_10 &
			ADV7482_SDP_R_REG_10_AUTOD_PAL_M) ==
			ADV7482_SDP_R_REG_10_AUTOD_PAL_M) ||
			((status_reg_10 &
			ADV7482_SDP_R_REG_10_AUTOD_PAL_60) ==
			ADV7482_SDP_R_REG_10_AUTOD_PAL_60) ||
			((status_reg_10 &
			ADV7482_SDP_R_REG_10_AUTOD_PAL_B_G) ==
			ADV7482_SDP_R_REG_10_AUTOD_PAL_B_G) ||
			((status_reg_10 &
			ADV7482_SDP_R_REG_10_AUTOD_PAL_COMB) ==
			ADV7482_SDP_R_REG_10_AUTOD_PAL_COMB)))
			dev_info(state->dev,
			   "Detected the PAL video input signal\n");
		else if ((status_reg_10 & ADV7482_SDP_R_REG_10_IN_LOCK)
			&& (status_reg_10 &
			ADV7482_SDP_R_REG_10_FSC_LOCK) &&
			(((status_reg_10 &
			ADV7482_SDP_R_REG_10_AUTOD_NTSC_4_43) ==
			ADV7482_SDP_R_REG_10_AUTOD_NTSC_4_43) ||
			((status_reg_10 &
			ADV7482_SDP_R_REG_10_AUTOD_MASK) ==
			ADV7482_SDP_R_REG_10_AUTOD_NTSM_M_J)))
			dev_info(state->dev,
			   "Detected the NTSC video input signal\n");
		else
			dev_info(state->dev,
			   "Not detect any video input signal\n");

		state->width = fmt->width;
		state->height = fmt->height;
		state->field = V4L2_FIELD_INTERLACED;

	} else {
		fmt->code = MEDIA_BUS_FMT_RGB888_1X24;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;

		/* Get video information */
		ret = adv7482_get_vid_info(sd, &progressive,
			&width, &height, &signal);
		if (ret < 0) {
			width		= ADV7482_MAX_WIDTH;
			height		= ADV7482_MAX_HEIGHT;
			progressive	= 1;
		}

		if (signal)
			dev_info(state->dev,
			 "Detected the HDMI video input signal (%dx%d%c)\n",
			  width, height, (progressive) ? 'p' : 'i');
		else
			dev_info(state->dev,
				"Not detect any video input signal\n");

		state->width = width;
		state->height = height;
		state->field =
		      (progressive) ? V4L2_FIELD_NONE : V4L2_FIELD_INTERLACED;

		fmt->width = state->width;
		fmt->height = state->height;
	}

	return 0;
}

static int adv7482_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	*norm = V4L2_STD_ALL;
	return 0;
}

static int adv7482_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct adv7482_state *state = to_state(sd);

	*norm = state->curr_norm;

	return 0;
}

static int adv7482_program_std(struct adv7482_state *state)
{
	struct adv7482_link_config *config = &state->mipi_csi2_link[0];
	int ret;

	if (config->input_interface != DECODER_INPUT_INTERFACE_YCBCR422)
		return -EINVAL;

	ret = v4l2_std_to_adv7482(state->curr_norm);
	if (ret < 0)
		return ret;

	ret = adv7482_set_video_standard(state, ret);
	if (ret < 0)
		return ret;

	return 0;
}

static int adv7482_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct adv7482_state *state = to_state(sd);
	struct adv7482_link_config *config = &state->mipi_csi2_link[0];
	int ret = mutex_lock_interruptible(&state->mutex);

	if (ret)
		return ret;

	if (config->input_interface != DECODER_INPUT_INTERFACE_YCBCR422) {
		ret = -EINVAL;
		goto out;
	}

	/* Make sure we can support this std */
	ret = v4l2_std_to_adv7482(std);
	if (ret < 0)
		goto out;

	state->curr_norm = std;

	ret = adv7482_program_std(state);
out:
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7482_set_field_mode(struct adv7482_state *state)
{
	/* FIXME */

	return 0;
}

static int adv7482_set_power(struct adv7482_state *state, bool on)
{
	u8 val;
	int ret;
	struct adv7482_link_config *config = &state->mipi_csi2_link[0];

	if (config->input_interface == DECODER_INPUT_INTERFACE_YCBCR422) {
		ret = adv7482_read_register(state->client, ADV7482_I2C_TXB,
				0x1E, &val);
		if (ret < 0)
			return ret;

		if (on && ((val & 0x40) == 0)) {
			/* Power up */
			ret = adv7482_write_registers(state->client,
					adv7482_power_up_txb_1lane);
			if (ret < 0)
				goto fail;
		} else {
			/* Power down */
			ret = adv7482_write_registers(state->client,
					adv7482_power_down_txb_1lane);
			if (ret < 0)
				goto fail;
		}
	} else {
		/* set active resolution */
		ret = adv7482_set_vid_info(&state->sd);
		ret = adv7482_read_register(state->client, ADV7482_I2C_TXA,
				0x1E, &val);
		if (ret < 0)
			return ret;

		if (on && ((val & 0x40) == 0)) {
			/* Power up */
			ret = adv7482_write_registers(state->client,
					adv7482_power_up_txa_4lane);
			if (ret < 0)
				goto fail;
		} else {
			/* Power down */
			ret = adv7482_write_registers(state->client,
					adv7482_power_down_txa_4lane);
			if (ret < 0)
				goto fail;
		}
	}

	return 0;
fail:
	pr_info("%s: Failed set power operation, ret = %d\n", __func__, ret);
	return ret;
}

static int adv7482_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct adv7482_state *state = to_state(sd);
	int ret;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;

	ret = adv7482_set_power(state, enable);
	if (ret)
		goto out;

	state->streaming = enable;

out:
	mutex_unlock(&state->mutex);
	return ret;
}

static int adv7482_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct adv7482_state *state = to_state(sd);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		format->format = *v4l2_subdev_get_try_format(sd, cfg, 0);
	else {
		adv7482_mbus_fmt(sd, &format->format);
		format->format.field = state->field;
	}

	return 0;
}

static int adv7482_set_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *format)
{
	struct adv7482_state *state = to_state(sd);
	struct v4l2_mbus_framefmt *framefmt;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		framefmt = &format->format;
		if (state->field != format->format.field) {
			state->field = format->format.field;
			adv7482_set_field_mode(state);
		}
	} else {
		adv7482_mbus_fmt(sd, &format->format);

		if (format->format.field == V4L2_FIELD_ANY)
			format->format.field = state->field;

		return 0;
	}

	return adv7482_mbus_fmt(sd, framefmt);
}

/*
 * adv7482_g_mbus_config() - V4L2 decoder i/f handler for g_mbus_config
 * @sd: pointer to standard V4L2 sub-device structure
 * @cfg: pointer to V4L2 mbus_config structure
 *
 * Get mbus configuration.
 */
static int adv7482_g_mbus_config(struct v4l2_subdev *sd,
					struct v4l2_mbus_config *cfg)
{
	struct adv7482_state *state = to_state(sd);
	struct adv7482_link_config *config = &state->mipi_csi2_link[0];

	if (config->input_interface == DECODER_INPUT_INTERFACE_YCBCR422)
		cfg->flags = V4L2_MBUS_CSI2_1_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	else
		cfg->flags = V4L2_MBUS_CSI2_LANES |
				V4L2_MBUS_CSI2_CHANNELS |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	cfg->type = V4L2_MBUS_CSI2;

	return 0;
}

/*****************************************************************************/
/*  V4L2 decoder i/f handler for v4l2_ctrl_ops                               */
/*****************************************************************************/
static int adv7482_cp_s_ctrl(struct v4l2_ctrl *ctrl, struct i2c_client *client)
{
	u8 val;
	int ret;

	/* Enable video adjustment first */
	ret = adv7482_read_register(client, ADV7482_I2C_CP,
			ADV7482_CP_VID_ADJ_REG, &val);
	if (ret < 0)
		return ret;
	val |= ADV7482_CP_VID_ADJ_ENABLE;
	ret = adv7482_write_register(client, ADV7482_I2C_CP,
			ADV7482_CP_VID_ADJ_REG, val);
	if (ret < 0)
		return ret;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if ((ctrl->val < ADV7482_CP_BRI_MIN) ||
					(ctrl->val > ADV7482_CP_BRI_MAX))
			ret = -ERANGE;
		else
			ret = adv7482_write_register(client, ADV7482_I2C_CP,
					ADV7482_CP_BRI_REG, ctrl->val);
		break;
	case V4L2_CID_HUE:
		if ((ctrl->val < ADV7482_CP_HUE_MIN) ||
					(ctrl->val > ADV7482_CP_HUE_MAX))
			ret = -ERANGE;
		else
			ret = adv7482_write_register(client, ADV7482_I2C_CP,
					ADV7482_CP_HUE_REG, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		if ((ctrl->val < ADV7482_CP_CON_MIN) ||
					(ctrl->val > ADV7482_CP_CON_MAX))
			ret = -ERANGE;
		else
			ret = adv7482_write_register(client, ADV7482_I2C_CP,
					ADV7482_CP_CON_REG, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		if ((ctrl->val < ADV7482_CP_SAT_MIN) ||
					(ctrl->val > ADV7482_CP_SAT_MAX))
			ret = -ERANGE;
		else
			ret = adv7482_write_register(client, ADV7482_I2C_CP,
					ADV7482_CP_SAT_REG, ctrl->val);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int adv7482_sdp_s_ctrl(struct v4l2_ctrl *ctrl, struct i2c_client *client)
{
	int ret;

	ret = adv7482_write_register(client, ADV7482_I2C_SDP,
			ADV7482_SDP_REG_CTRL, ADV7482_SDP_MAIN_MAP_RW);
	if (ret < 0)
		return ret;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if ((ctrl->val < ADV7482_SDP_BRI_MIN) ||
					(ctrl->val > ADV7482_SDP_BRI_MAX))
			ret = -ERANGE;
		else
			ret = adv7482_write_register(client, ADV7482_I2C_SDP,
					ADV7482_SDP_REG_BRI, ctrl->val);
		break;
	case V4L2_CID_HUE:
		if ((ctrl->val < ADV7482_SDP_HUE_MIN) ||
					(ctrl->val > ADV7482_SDP_HUE_MAX))
			ret = -ERANGE;
		else
			/*Hue is inverted according to HSL chart */
			ret = adv7482_write_register(client, ADV7482_I2C_SDP,
					ADV7482_SDP_REG_HUE, -ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		if ((ctrl->val < ADV7482_SDP_CON_MIN) ||
					(ctrl->val > ADV7482_SDP_CON_MAX))
			ret = -ERANGE;
		else
			ret = adv7482_write_register(client, ADV7482_I2C_SDP,
					ADV7482_SDP_REG_CON, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		/*
		 *This could be V4L2_CID_BLUE_BALANCE/V4L2_CID_RED_BALANCE
		 *Let's not confuse the user, everybody understands saturation
		 */
		if ((ctrl->val < ADV7482_SDP_SAT_MIN) ||
					(ctrl->val > ADV7482_SDP_SAT_MAX))
			ret = -ERANGE;
		else {
			ret = adv7482_write_register(client, ADV7482_I2C_SDP,
					ADV7482_SDP_REG_SD_SAT_CB, ctrl->val);
			if (ret < 0)
				break;

			ret = adv7482_write_register(client, ADV7482_I2C_SDP,
					ADV7482_SDP_REG_SD_SAT_CR, ctrl->val);
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/*
 * adv7482_s_ctrl() - V4L2 decoder i/f handler for s_ctrl
 * @ctrl: pointer to standard V4L2 control structure
 *
 * Set a control in ADV7482 decoder device.
 */
static int adv7482_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_adv7482_sd(ctrl);
	struct adv7482_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = mutex_lock_interruptible(&state->mutex);
	struct adv7482_link_config *config = &state->mipi_csi2_link[0];

	if (ret)
		return ret;

	if (config->input_interface == DECODER_INPUT_INTERFACE_YCBCR422)
		ret = adv7482_sdp_s_ctrl(ctrl, client);
	else
		ret = adv7482_cp_s_ctrl(ctrl, client);

	mutex_unlock(&state->mutex);

	return ret;
}

static const struct v4l2_subdev_video_ops adv7482_video_ops = {
	.g_std		= adv7482_g_std,
	.s_std		= adv7482_s_std,
	.querystd	= adv7482_querystd,
	.g_input_status = adv7482_g_input_status,
	.g_tvnorms	= adv7482_g_tvnorms,
	.g_mbus_config	= adv7482_g_mbus_config,
	.s_stream	= adv7482_s_stream,
};

static const struct v4l2_subdev_pad_ops adv7482_pad_ops = {
	.enum_mbus_code = adv7482_enum_mbus_code,
	.set_fmt = adv7482_set_pad_format,
	.get_fmt = adv7482_get_pad_format,
};

static const struct v4l2_subdev_ops adv7482_ops = {
	.video = &adv7482_video_ops,
	.pad = &adv7482_pad_ops,
};

static const struct v4l2_ctrl_ops adv7482_ctrl_ops = {
	.s_ctrl = adv7482_s_ctrl,
};

/*
 * adv7482_init_controls() - Init controls
 * @state: pointer to private state structure
 *
 * Init ADV7482 supported control handler.
 */
static int adv7482_cp_init_controls(struct adv7482_state *state)
{
	v4l2_ctrl_handler_init(&state->ctrl_hdl, 4);

	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7482_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, ADV7482_CP_BRI_MIN,
			  ADV7482_CP_BRI_MAX, 1, ADV7482_CP_BRI_DEF);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7482_ctrl_ops,
			  V4L2_CID_CONTRAST, ADV7482_CP_CON_MIN,
			  ADV7482_CP_CON_MAX, 1, ADV7482_CP_CON_DEF);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7482_ctrl_ops,
			  V4L2_CID_SATURATION, ADV7482_CP_SAT_MIN,
			  ADV7482_CP_SAT_MAX, 1, ADV7482_CP_SAT_DEF);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7482_ctrl_ops,
			  V4L2_CID_HUE, ADV7482_CP_HUE_MIN,
			  ADV7482_CP_HUE_MAX, 1, ADV7482_CP_HUE_DEF);
	state->sd.ctrl_handler = &state->ctrl_hdl;
	if (state->ctrl_hdl.error) {
		int err = state->ctrl_hdl.error;

		v4l2_ctrl_handler_free(&state->ctrl_hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&state->ctrl_hdl);

	return 0;
}

static int adv7482_sdp_init_controls(struct adv7482_state *state)
{
	v4l2_ctrl_handler_init(&state->ctrl_hdl, 4);

	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7482_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, ADV7482_SDP_BRI_MIN,
			  ADV7482_SDP_BRI_MAX, 1, ADV7482_SDP_BRI_DEF);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7482_ctrl_ops,
			  V4L2_CID_CONTRAST, ADV7482_SDP_CON_MIN,
			  ADV7482_SDP_CON_MAX, 1, ADV7482_SDP_CON_DEF);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7482_ctrl_ops,
			  V4L2_CID_SATURATION, ADV7482_SDP_SAT_MIN,
			  ADV7482_SDP_SAT_MAX, 1, ADV7482_SDP_SAT_DEF);
	v4l2_ctrl_new_std(&state->ctrl_hdl, &adv7482_ctrl_ops,
			  V4L2_CID_HUE, ADV7482_SDP_HUE_MIN,
			  ADV7482_SDP_HUE_MAX, 1, ADV7482_SDP_HUE_DEF);

	state->sd.ctrl_handler = &state->ctrl_hdl;
	if (state->ctrl_hdl.error) {
		int err = state->ctrl_hdl.error;

		v4l2_ctrl_handler_free(&state->ctrl_hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&state->ctrl_hdl);

	return 0;
}

static void adv7482_exit_controls(struct adv7482_state *state)
{
	v4l2_ctrl_handler_free(&state->ctrl_hdl);
}

/*****************************************************************************/
/*  i2c driver interface handlers                                            */
/*****************************************************************************/

static int adv7482_parse_dt(struct device_node *np,
			    struct adv7482_link_config *config)
{
	struct v4l2_of_endpoint bus_cfg;
	struct device_node *endpoint;
	const char *str;
	int ret;
	int ch;

	/* Parse the endpoint. */
	endpoint = of_graph_get_next_endpoint(np, NULL);
	if (!endpoint)
		return -EINVAL;

	v4l2_of_parse_endpoint(endpoint, &bus_cfg);
	of_node_put(endpoint);

	/* check input-interface */
	ret = of_property_read_string(np, "adi,input-interface", &str);
	if (ret < 0)
		return ret;

	if (!strcmp(str, "rgb888")) {
		config->input_interface = DECODER_INPUT_INTERFACE_RGB888;
		config->regs =
		(struct adv7482_reg_value *)adv7482_init_txa_4lane;
		config->power_up =
		(struct adv7482_reg_value *)adv7482_power_up_txa_4lane;
		config->power_down =
		(struct adv7482_reg_value *)adv7482_power_down_txa_4lane;
		config->init_controls =
		(int (*)(void *))adv7482_cp_init_controls;
	} else {
		config->input_interface = DECODER_INPUT_INTERFACE_YCBCR422;
		config->regs =
		(struct adv7482_reg_value *)adv7482_init_txb_1lane;
		config->power_up =
		(struct adv7482_reg_value *)adv7482_power_up_txb_1lane;
		config->power_down =
		(struct adv7482_reg_value *)adv7482_power_down_txb_1lane;
		config->init_controls =
		(int (*)(void *))adv7482_sdp_init_controls;
	}

	ret = of_property_read_string(np, "adi,input-hdmi", &str);
	if (ret < 0)
		return ret;

	if (!strcmp(str, "on"))
		config->hdmi_in = 1;
	else
		config->hdmi_in = 0;

	ret = of_property_read_string(np, "adi,input-sdp", &str);
	if (ret < 0)
		return ret;

	if (!strcmp(str, "on"))
		config->sdp_in = 1;
	else
		config->sdp_in = 0;

	ret = of_property_read_string(np, "adi,sw-reset", &str);
	if (ret < 0)
		return ret;

	if (!strcmp(str, "on"))
		config->sw_reset = 1;
	else
		config->sw_reset = 0;

	ret = of_property_read_u32(np, "adi,virtual-channel", &ch);
	if (ret < 0)
		return ret;

	if ((ch < 0) || (ch > 3))
		return -EINVAL;

	config->vc_ch = ch;

	config->init_device    = NULL;
	config->s_power        = NULL;
	config->s_ctrl         = NULL;
	config->enum_mbus_code = NULL;
	config->set_pad_format = NULL;
	config->get_pad_format = NULL;
	config->s_std          = NULL;
	config->querystd       = NULL;
	config->g_input_status = NULL;
	config->s_routing      = NULL;
	config->g_mbus_config  = NULL;

	return 0;
}

/*
 * adv7482_probe - Probe a ADV7482 device
 * @client: pointer to i2c_client structure
 * @id: pointer to i2c_device_id structure
 *
 * Initialize the ADV7482 device
 */
static int adv7482_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct adv7482_state *state;
	struct adv7482_link_config link_config;
	struct device *dev = &client->dev;
	struct v4l2_subdev *sd;
	int ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	state = kzalloc(sizeof(struct adv7482_state), GFP_KERNEL);
	if (state == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	state->client = client;
	state->irq = client->irq;

	ret = adv7482_parse_dt(dev->of_node, &link_config);
	if (ret) {
		dev_err(&client->dev, "adv7482 parse error\n");
		return ret;
	}

	state->mipi_csi2_link[0].input_interface = link_config.input_interface;

	mutex_init(&state->mutex);
	state->curr_norm = V4L2_STD_NTSC;
	sd = &state->sd;
	state->width = ADV7482_MAX_WIDTH;
	state->height = ADV7482_MAX_HEIGHT;
	state->field = V4L2_FIELD_NONE;

	v4l2_i2c_subdev_init(sd, client, &adv7482_ops);

	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	state->dev		= dev;
	state->mipi_csi2_link[0].dev = dev;

	/* SW reset ADV7482 to its default values */
	if (link_config.sw_reset) {
		ret = adv7482_write_registers(client, adv7482_sw_reset);
		if (ret < 0)
			goto err_unreg_subdev;

		/* check rd_info */
		{
			u8 msb;
			u8 lsb;

			ret = adv7482_read_register(client, ADV7482_I2C_IO,
					ADV7482_IO_RD_INFO1_REG, &lsb);
			if (ret < 0)
				return ret;

			ret = adv7482_read_register(client, ADV7482_I2C_IO,
					ADV7482_IO_RD_INFO2_REG, &msb);
			if (ret < 0)
				return ret;

			v4l_info(client, "adv7482 revision is %02x%02x\n",
					lsb, msb);
		}
	}

	if (link_config.hdmi_in) {
		ret = adv7482_write_registers(client,
				adv7482_init_txa_4lane);
		if (ret < 0)
			goto err_unreg_subdev;

		/* Power down */
		ret = adv7482_write_registers(client,
				adv7482_power_down_txa_4lane);
		if (ret < 0)
			goto err_unreg_subdev;

		v4l_info(client, "adv7482 txa power down\n");
	} else
		v4l_info(client, "adv7482 hdmi_in is disabled.\n");

	/* Initializes ADV7482 to its default values */
	if (link_config.sdp_in) {
		ret = adv7482_write_registers(client,
						adv7482_init_txb_1lane);
		if (ret < 0)
			goto err_unreg_subdev;

		/* Power down */
		ret = adv7482_write_registers(client,
						adv7482_power_down_txb_1lane);
		if (ret < 0)
			goto err_unreg_subdev;

		v4l_info(client, "adv7482 txb power down\n");
	} else
		v4l_info(client, "adv7482 sdp_in is disabled.\n");

	if (link_config.sdp_in && link_config.hdmi_in) {
		/* Power up hdmi rx */
		ret = adv7482_write_registers(client,
						adv7482_power_up_hdmi_rx);
		if (ret < 0)
			goto err_unreg_subdev;

		/* Enable csi4 and sci1 */
		ret = adv7482_write_registers(client,
						adv7482_enable_csi4_csi1);
		if (ret < 0)
			goto err_unreg_subdev;

		v4l_info(client, "adv7482 enable csi1 and csi4\n");
	}

	if (link_config.input_interface == DECODER_INPUT_INTERFACE_YCBCR422)
		ret = adv7482_sdp_init_controls(state);
	else
		ret = adv7482_cp_init_controls(state);
	if (ret)
		goto err_unreg_subdev;

	/* Setting virtual channel for ADV7482 */
	if (link_config.vc_ch == 0)
		ret = adv7482_write_registers(client,
					adv7482_set_virtual_channel0);
	else if (link_config.vc_ch == 1)
		ret = adv7482_write_registers(client,
					adv7482_set_virtual_channel1);
	else if (link_config.vc_ch == 2)
		ret = adv7482_write_registers(client,
					adv7482_set_virtual_channel2);
	else if (link_config.vc_ch == 3)
		ret = adv7482_write_registers(client,
					adv7482_set_virtual_channel3);
	if (ret < 0)
		goto err_unreg_subdev;

	state->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.flags |= MEDIA_ENT_F_ATV_DECODER;
	sd->entity.function = MEDIA_ENT_F_ATV_DECODER;
	ret = media_entity_pads_init(&sd->entity, 1, &state->pad);
	if (ret)
		goto err_free_ctrl;

	ret = v4l2_async_register_subdev(sd);
	if (ret)
		goto err_free_ctrl;

	return 0;

err_free_ctrl:
	adv7482_exit_controls(state);

err_unreg_subdev:
	mutex_destroy(&state->mutex);
	v4l2_device_unregister_subdev(sd);
	kfree(state);
err:
	dev_err(&client->dev, ": Failed to probe: %d\n", ret);
	return ret;
}

/*
 * adv7482_remove - Remove ADV7482 device support
 * @client: pointer to i2c_client structure
 *
 * Reset the ADV7482 device
 */
static int adv7482_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7482_state *state = to_state(sd);

	v4l2_async_unregister_subdev(sd);

	media_entity_cleanup(&sd->entity);
	adv7482_exit_controls(state);

	mutex_destroy(&state->mutex);
	kfree(to_state(sd));
	return 0;
}

static const struct i2c_device_id adv7482_id[] = {
	{DRIVER_NAME, 0},
	{},
};

#ifdef CONFIG_PM_SLEEP
/*
 * adv7482_suspend - Suspend ADV7482 device
 * @client: pointer to i2c_client structure
 * @state: power management state
 *
 * Power down the ADV7482 device
 */
static int adv7482_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	ret = adv7482_write_register(client, ADV7482_I2C_IO,
				ADV7482_IO_PWR_MAN_REG, ADV7482_IO_PWR_OFF);

	return ret;
}

/*
 * adv7482_resume - Resume ADV7482 device
 * @client: pointer to i2c_client structure
 *
 * Power on and initialize the ADV7482 device
 */
static int adv7482_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adv7482_link_config link_config;
	int ret;

	ret = adv7482_parse_dt(dev->of_node, &link_config);
	if (ret)
		return ret;

	/* SW reset ADV7482 to its default values */
	if (link_config.sw_reset) {
		ret = adv7482_write_registers(client, adv7482_sw_reset);

		/* check rd_info */
		{
			u8 msb;
			u8 lsb;

			ret = adv7482_read_register(client, ADV7482_I2C_IO,
					ADV7482_IO_RD_INFO1_REG, &lsb);
			if (ret < 0)
				return ret;

			ret = adv7482_read_register(client, ADV7482_I2C_IO,
					ADV7482_IO_RD_INFO2_REG, &msb);
			if (ret < 0)
				return ret;
		}
	}

	if (link_config.hdmi_in) {
		ret = adv7482_write_registers(client,
				adv7482_init_txa_4lane);
		/* Power down */
		ret = adv7482_write_registers(client,
				adv7482_power_down_txa_4lane);
	}

	/* Initializes ADV7482 to its default values */
	if (link_config.sdp_in) {
		ret = adv7482_write_registers(client,
						adv7482_init_txb_1lane);
		/* Power down */
		ret = adv7482_write_registers(client,
						adv7482_power_down_txb_1lane);
	}

	if (link_config.sdp_in && link_config.hdmi_in) {
		/* Power up hdmi rx */
		ret = adv7482_write_registers(client,
						adv7482_power_up_hdmi_rx);
		/* Enable csi4 and sci1 */
		ret = adv7482_write_registers(client,
						adv7482_enable_csi4_csi1);
	}

	return ret;
}

const struct dev_pm_ops adv7482_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(adv7482_suspend, adv7482_resume)
};
#else
#define ADV7482_PM_OPS NULL
#endif

MODULE_DEVICE_TABLE(i2c, adv7482_id);

static const struct of_device_id adv7482_of_ids[] = {
	{ .compatible = "adi,adv7482", },
	{ }
};
MODULE_DEVICE_TABLE(of, adv7482_of_ids);

static struct i2c_driver adv7482_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.pm = &adv7482_pm_ops,
		.of_match_table = adv7482_of_ids,
	},
	.probe		= adv7482_probe,
	.remove		= adv7482_remove,
	.id_table	= adv7482_id,
};

module_i2c_driver(adv7482_driver);

MODULE_DESCRIPTION("HDMI Receiver ADV7482 video decoder driver");
MODULE_ALIAS("platform:adv7482");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Koji Matsuoka <koji.matsuoka.xm@renesas.com>");
