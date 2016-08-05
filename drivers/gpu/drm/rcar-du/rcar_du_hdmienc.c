/*
 * R-Car Display Unit HDMI Encoder
 *
 * Copyright (C) 2014-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/slab.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include <linux/soc/renesas/s2ram_ddr_backup.h>
#include <linux/of_platform.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_hdmienc.h"
#include "rcar_du_lvdsenc.h"

struct rcar_du_hdmienc {
	struct rcar_du_encoder *renc;
	struct device *dev;
	bool enabled;
	unsigned int index;
};

#define to_rcar_hdmienc(e)	(to_rcar_encoder(e)->hdmi)

#ifdef CONFIG_RCAR_DDR_BACKUP
static struct hw_register hdmi0_ip_regs[] = {
/* Interrupt Registers */
	{"HDMI_IH_MUTE_FC_STAT0", 0x0180, 8, 0},
	{"HDMI_IH_MUTE_FC_STAT1", 0x0181, 8, 0},
	{"HDMI_IH_MUTE_FC_STAT2", 0x0182, 8, 0},
	{"HDMI_IH_MUTE_AS_STAT0", 0x0183, 8, 0},
	{"HDMI_IH_MUTE_PHY_STAT0", 0x0184, 8, 0},
	{"HDMI_IH_MUTE_I2CM_STAT0", 0x0185, 8, 0},
	{"HDMI_IH_MUTE_CEC_STAT0", 0x0186, 8, 0},
	{"HDMI_IH_MUTE_VP_STAT0", 0x0187, 8, 0},
	{"HDMI_IH_MUTE_I2CMPHY_STAT0", 0x0188, 8, 0},
	{"HDMI_IH_MUTE", 0x01FF, 8, 0},

/* Video Packetizer Registers */
	{"HDMI_VP_PR_CD", 0x0801, 8, 0},
	{"HDMI_VP_STUFF", 0x0802, 8, 0},
	{"HDMI_VP_REMAP", 0x0803, 8, 0},
	{"HDMI_VP_CONF", 0x0804, 8, 0},
	{"HDMI_VP_STAT", 0x0805, 8, 0},
	{"HDMI_VP_INT", 0x0806, 8, 0},
	{"HDMI_VP_MASK", 0x0807, 8, 0},
	{"HDMI_VP_POL", 0x0808, 8, 0},

/* Video Sample Registers */
	{"HDMI_TX_INVID0", 0x0200, 8, 0},
	{"HDMI_TX_INSTUFFING", 0x0201, 8, 0},
	{"HDMI_TX_GYDATA0", 0x0202, 8, 0},
	{"HDMI_TX_GYDATA1", 0x0203, 8, 0},
	{"HDMI_TX_RCRDATA0", 0x0204, 8, 0},
	{"HDMI_TX_RCRDATA1", 0x0205, 8, 0},
	{"HDMI_TX_BCBDATA0", 0x0206, 8, 0},
	{"HDMI_TX_BCBDATA1", 0x0207, 8, 0},


/* Frame Composer Registers */
	{"HDMI_FC_INVIDCONF", 0x1000, 8, 0},
	{"HDMI_FC_INHACTV0", 0x1001, 8, 0},
	{"HDMI_FC_INHACTV1", 0x1002, 8, 0},
	{"HDMI_FC_INHBLANK0", 0x1003, 8, 0},
	{"HDMI_FC_INHBLANK1", 0x1004, 8, 0},
	{"HDMI_FC_INVACTV0", 0x1005, 8, 0},
	{"HDMI_FC_INVACTV1", 0x1006, 8, 0},
	{"HDMI_FC_INVBLANK", 0x1007, 8, 0},
	{"HDMI_FC_HSYNCINDELAY0", 0x1008, 8, 0},
	{"HDMI_FC_HSYNCINDELAY1", 0x1009, 8, 0},
	{"HDMI_FC_HSYNCINWIDTH0", 0x100A, 8, 0},
	{"HDMI_FC_HSYNCINWIDTH1", 0x100B, 8, 0},
	{"HDMI_FC_VSYNCINDELAY", 0x100C, 8, 0},
	{"HDMI_FC_VSYNCINWIDTH", 0x100D, 8, 0},
	{"HDMI_FC_INFREQ0", 0x100E, 8, 0},
	{"HDMI_FC_INFREQ1", 0x100F, 8, 0},
	{"HDMI_FC_INFREQ2", 0x1010, 8, 0},
	{"HDMI_FC_CTRLDUR", 0x1011, 8, 0},
	{"HDMI_FC_EXCTRLDUR", 0x1012, 8, 0},
	{"HDMI_FC_EXCTRLSPAC", 0x1013, 8, 0},
	{"HDMI_FC_CH0PREAM", 0x1014, 8, 0},
	{"HDMI_FC_CH1PREAM", 0x1015, 8, 0},
	{"HDMI_FC_CH2PREAM", 0x1016, 8, 0},
	{"HDMI_FC_AVICONF3", 0x1017, 8, 0},
	{"HDMI_FC_GCP", 0x1018, 8, 0},
	{"HDMI_FC_AVICONF0", 0x1019, 8, 0},
	{"HDMI_FC_AVICONF1", 0x101A, 8, 0},
	{"HDMI_FC_AVICONF2", 0x101B, 8, 0},
	{"HDMI_FC_AVIVID", 0x101C, 8, 0},
	{"HDMI_FC_AVIETB0", 0x101D, 8, 0},
	{"HDMI_FC_AVIETB1", 0x101E, 8, 0},
	{"HDMI_FC_AVISBB0", 0x101F, 8, 0},
	{"HDMI_FC_AVISBB1", 0x1020, 8, 0},
	{"HDMI_FC_AVIELB0", 0x1021, 8, 0},
	{"HDMI_FC_AVIELB1", 0x1022, 8, 0},
	{"HDMI_FC_AVISRB0", 0x1023, 8, 0},
	{"HDMI_FC_AVISRB1", 0x1024, 8, 0},
	{"HDMI_FC_AUDICONF0", 0x1025, 8, 0},
	{"HDMI_FC_AUDICONF1", 0x1026, 8, 0},
	{"HDMI_FC_AUDICONF2", 0x1027, 8, 0},
	{"HDMI_FC_AUDICONF3", 0x1028, 8, 0},
	{"HDMI_FC_VSDIEEEID0", 0x1029, 8, 0},
	{"HDMI_FC_VSDSIZE", 0x102A, 8, 0},
	{"HDMI_FC_VSDIEEEID1", 0x1030, 8, 0},
	{"HDMI_FC_VSDIEEEID2", 0x1031, 8, 0},
	{"HDMI_FC_VSDPAYLOAD0", 0x1032, 8, 0},
	{"HDMI_FC_VSDPAYLOAD1", 0x1033, 8, 0},
	{"HDMI_FC_VSDPAYLOAD2", 0x1034, 8, 0},
	{"HDMI_FC_VSDPAYLOAD3", 0x1035, 8, 0},
	{"HDMI_FC_VSDPAYLOAD4", 0x1036, 8, 0},
	{"HDMI_FC_VSDPAYLOAD5", 0x1037, 8, 0},
	{"HDMI_FC_VSDPAYLOAD6", 0x1038, 8, 0},
	{"HDMI_FC_VSDPAYLOAD7", 0x1039, 8, 0},
	{"HDMI_FC_VSDPAYLOAD8", 0x103A, 8, 0},
	{"HDMI_FC_VSDPAYLOAD9", 0x103B, 8, 0},
	{"HDMI_FC_VSDPAYLOAD10", 0x103C, 8, 0},
	{"HDMI_FC_VSDPAYLOAD11", 0x103D, 8, 0},
	{"HDMI_FC_VSDPAYLOAD12", 0x103E, 8, 0},
	{"HDMI_FC_VSDPAYLOAD13", 0x103F, 8, 0},
	{"HDMI_FC_VSDPAYLOAD14", 0x1040, 8, 0},
	{"HDMI_FC_VSDPAYLOAD15", 0x1041, 8, 0},
	{"HDMI_FC_VSDPAYLOAD16", 0x1042, 8, 0},
	{"HDMI_FC_VSDPAYLOAD17", 0x1043, 8, 0},
	{"HDMI_FC_VSDPAYLOAD18", 0x1044, 8, 0},
	{"HDMI_FC_VSDPAYLOAD19", 0x1045, 8, 0},
	{"HDMI_FC_VSDPAYLOAD20", 0x1046, 8, 0},
	{"HDMI_FC_VSDPAYLOAD21", 0x1047, 8, 0},
	{"HDMI_FC_VSDPAYLOAD22", 0x1048, 8, 0},
	{"HDMI_FC_VSDPAYLOAD23", 0x1049, 8, 0},
	{"HDMI_FC_SPDVENDORNAME0", 0x104A, 8, 0},
	{"HDMI_FC_SPDVENDORNAME1", 0x104B, 8, 0},
	{"HDMI_FC_SPDVENDORNAME2", 0x104C, 8, 0},
	{"HDMI_FC_SPDVENDORNAME3", 0x104D, 8, 0},
	{"HDMI_FC_SPDVENDORNAME4", 0x104E, 8, 0},
	{"HDMI_FC_SPDVENDORNAME5", 0x104F, 8, 0},
	{"HDMI_FC_SPDVENDORNAME6", 0x1050, 8, 0},
	{"HDMI_FC_SPDVENDORNAME7", 0x1051, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME0", 0x1052, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME1", 0x1053, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME2", 0x1054, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME3", 0x1055, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME4", 0x1056, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME5", 0x1057, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME6", 0x1058, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME7", 0x1059, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME8", 0x105A, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME9", 0x105B, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME10", 0x105C, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME11", 0x105D, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME12", 0x105E, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME13", 0x105F, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME14", 0x1060, 8, 0},
	{"HDMI_FC_SPDPRODUCTNAME15", 0x1061, 8, 0},
	{"HDMI_FC_SPDDEVICEINF", 0x1062, 8, 0},
	{"HDMI_FC_AUDSCONF", 0x1063, 8, 0},
	{"HDMI_FC_AUDSSTAT", 0x1064, 8, 0},
	{"HDMI_FC_DATACH0FILL", 0x1070, 8, 0},
	{"HDMI_FC_DATACH1FILL", 0x1071, 8, 0},
	{"HDMI_FC_DATACH2FILL", 0x1072, 8, 0},
	{"HDMI_FC_CTRLQHIGH", 0x1073, 8, 0},
	{"HDMI_FC_CTRLQLOW", 0x1074, 8, 0},
	{"HDMI_FC_ACP0", 0x1075, 8, 0},
	{"HDMI_FC_ACP28", 0x1076, 8, 0},
	{"HDMI_FC_ACP27", 0x1077, 8, 0},
	{"HDMI_FC_ACP26", 0x1078, 8, 0},
	{"HDMI_FC_ACP25", 0x1079, 8, 0},
	{"HDMI_FC_ACP24", 0x107A, 8, 0},
	{"HDMI_FC_ACP23", 0x107B, 8, 0},
	{"HDMI_FC_ACP22", 0x107C, 8, 0},
	{"HDMI_FC_ACP21", 0x107D, 8, 0},
	{"HDMI_FC_ACP20", 0x107E, 8, 0},
	{"HDMI_FC_ACP19", 0x107F, 8, 0},
	{"HDMI_FC_ACP18", 0x1080, 8, 0},
	{"HDMI_FC_ACP17", 0x1081, 8, 0},
	{"HDMI_FC_ACP16", 0x1082, 8, 0},
	{"HDMI_FC_ACP15", 0x1083, 8, 0},
	{"HDMI_FC_ACP14", 0x1084, 8, 0},
	{"HDMI_FC_ACP13", 0x1085, 8, 0},
	{"HDMI_FC_ACP12", 0x1086, 8, 0},
	{"HDMI_FC_ACP11", 0x1087, 8, 0},
	{"HDMI_FC_ACP10", 0x1088, 8, 0},
	{"HDMI_FC_ACP9", 0x1089, 8, 0},
	{"HDMI_FC_ACP8", 0x108A, 8, 0},
	{"HDMI_FC_ACP7", 0x108B, 8, 0},
	{"HDMI_FC_ACP6", 0x108C, 8, 0},
	{"HDMI_FC_ACP5", 0x108D, 8, 0},
	{"HDMI_FC_ACP4", 0x108E, 8, 0},
	{"HDMI_FC_ACP3", 0x108F, 8, 0},
	{"HDMI_FC_ACP2", 0x1090, 8, 0},
	{"HDMI_FC_ACP1", 0x1091, 8, 0},
	{"HDMI_FC_ISCR1_0", 0x1092, 8, 0},
	{"HDMI_FC_ISCR1_16", 0x1093, 8, 0},
	{"HDMI_FC_ISCR1_15", 0x1094, 8, 0},
	{"HDMI_FC_ISCR1_14", 0x1095, 8, 0},
	{"HDMI_FC_ISCR1_13", 0x1096, 8, 0},
	{"HDMI_FC_ISCR1_12", 0x1097, 8, 0},
	{"HDMI_FC_ISCR1_11", 0x1098, 8, 0},
	{"HDMI_FC_ISCR1_10", 0x1099, 8, 0},
	{"HDMI_FC_ISCR1_9", 0x109A, 8, 0},
	{"HDMI_FC_ISCR1_8", 0x109B, 8, 0},
	{"HDMI_FC_ISCR1_7", 0x109C, 8, 0},
	{"HDMI_FC_ISCR1_6", 0x109D, 8, 0},
	{"HDMI_FC_ISCR1_5", 0x109E, 8, 0},
	{"HDMI_FC_ISCR1_4", 0x109F, 8, 0},
	{"HDMI_FC_ISCR1_3", 0x10A0, 8, 0},
	{"HDMI_FC_ISCR1_2", 0x10A1, 8, 0},
	{"HDMI_FC_ISCR1_1", 0x10A2, 8, 0},
	{"HDMI_FC_ISCR2_15", 0x10A3, 8, 0},
	{"HDMI_FC_ISCR2_14", 0x10A4, 8, 0},
	{"HDMI_FC_ISCR2_13", 0x10A5, 8, 0},
	{"HDMI_FC_ISCR2_12", 0x10A6, 8, 0},
	{"HDMI_FC_ISCR2_11", 0x10A7, 8, 0},
	{"HDMI_FC_ISCR2_10", 0x10A8, 8, 0},
	{"HDMI_FC_ISCR2_9", 0x10A9, 8, 0},
	{"HDMI_FC_ISCR2_8", 0x10AA, 8, 0},
	{"HDMI_FC_ISCR2_7", 0x10AB, 8, 0},
	{"HDMI_FC_ISCR2_6", 0x10AC, 8, 0},
	{"HDMI_FC_ISCR2_5", 0x10AD, 8, 0},
	{"HDMI_FC_ISCR2_4", 0x10AE, 8, 0},
	{"HDMI_FC_ISCR2_3", 0x10AF, 8, 0},
	{"HDMI_FC_ISCR2_2", 0x10B0, 8, 0},
	{"HDMI_FC_ISCR2_1", 0x10B1, 8, 0},
	{"HDMI_FC_ISCR2_0", 0x10B2, 8, 0},
	{"HDMI_FC_DATAUTO0", 0x10B3, 8, 0},
	{"HDMI_FC_DATAUTO1", 0x10B4, 8, 0},
	{"HDMI_FC_DATAUTO2", 0x10B5, 8, 0},
	{"HDMI_FC_DATMAN", 0x10B6, 8, 0},
	{"HDMI_FC_DATAUTO3", 0x10B7, 8, 0},
	{"HDMI_FC_RDRB0", 0x10B8, 8, 0},
	{"HDMI_FC_RDRB1", 0x10B9, 8, 0},
	{"HDMI_FC_RDRB2", 0x10BA, 8, 0},
	{"HDMI_FC_RDRB3", 0x10BB, 8, 0},
	{"HDMI_FC_RDRB4", 0x10BC, 8, 0},
	{"HDMI_FC_RDRB5", 0x10BD, 8, 0},
	{"HDMI_FC_RDRB6", 0x10BE, 8, 0},
	{"HDMI_FC_RDRB7", 0x10BF, 8, 0},
	{"HDMI_FC_STAT0", 0x10D0, 8, 0},
	{"HDMI_FC_INT0", 0x10D1, 8, 0},
	{"HDMI_FC_MASK0", 0x10D2, 8, 0},
	{"HDMI_FC_POL0", 0x10D3, 8, 0},
	{"HDMI_FC_STAT1", 0x10D4, 8, 0},
	{"HDMI_FC_INT1", 0x10D5, 8, 0},
	{"HDMI_FC_MASK1", 0x10D6, 8, 0},
	{"HDMI_FC_POL1", 0x10D7, 8, 0},
	{"HDMI_FC_STAT2", 0x10D8, 8, 0},
	{"HDMI_FC_INT2", 0x10D9, 8, 0},
	{"HDMI_FC_MASK2", 0x10DA, 8, 0},
	{"HDMI_FC_POL2", 0x10DB, 8, 0},
	{"HDMI_FC_PRCONF", 0x10E0, 8, 0},

	{"HDMI_FC_GMD_EN", 0x1101, 8, 0},
	{"HDMI_FC_GMD_UP", 0x1102, 8, 0},
	{"HDMI_FC_GMD_CONF", 0x1103, 8, 0},
	{"HDMI_FC_GMD_HB", 0x1104, 8, 0},
	{"HDMI_FC_GMD_PB0", 0x1105, 8, 0},
	{"HDMI_FC_GMD_PB1", 0x1106, 8, 0},
	{"HDMI_FC_GMD_PB2", 0x1107, 8, 0},
	{"HDMI_FC_GMD_PB3", 0x1108, 8, 0},
	{"HDMI_FC_GMD_PB4", 0x1109, 8, 0},
	{"HDMI_FC_GMD_PB5", 0x110A, 8, 0},
	{"HDMI_FC_GMD_PB6", 0x110B, 8, 0},
	{"HDMI_FC_GMD_PB7", 0x110C, 8, 0},
	{"HDMI_FC_GMD_PB8", 0x110D, 8, 0},
	{"HDMI_FC_GMD_PB9", 0x110E, 8, 0},
	{"HDMI_FC_GMD_PB10", 0x110F, 8, 0},
	{"HDMI_FC_GMD_PB11", 0x1110, 8, 0},
	{"HDMI_FC_GMD_PB12", 0x1111, 8, 0},
	{"HDMI_FC_GMD_PB13", 0x1112, 8, 0},
	{"HDMI_FC_GMD_PB14", 0x1113, 8, 0},
	{"HDMI_FC_GMD_PB15", 0x1114, 8, 0},
	{"HDMI_FC_GMD_PB16", 0x1115, 8, 0},
	{"HDMI_FC_GMD_PB17", 0x1116, 8, 0},
	{"HDMI_FC_GMD_PB18", 0x1117, 8, 0},
	{"HDMI_FC_GMD_PB19", 0x1118, 8, 0},
	{"HDMI_FC_GMD_PB20", 0x1119, 8, 0},
	{"HDMI_FC_GMD_PB21", 0x111A, 8, 0},
	{"HDMI_FC_GMD_PB22", 0x111B, 8, 0},
	{"HDMI_FC_GMD_PB23", 0x111C, 8, 0},
	{"HDMI_FC_GMD_PB24", 0x111D, 8, 0},
	{"HDMI_FC_GMD_PB25", 0x111E, 8, 0},
	{"HDMI_FC_GMD_PB26", 0x111F, 8, 0},
	{"HDMI_FC_GMD_PB27", 0x1120, 8, 0},

	{"HDMI_FC_DBGFORCE", 0x1200, 8, 0},
	{"HDMI_FC_DBGAUD0CH0", 0x1201, 8, 0},
	{"HDMI_FC_DBGAUD1CH0", 0x1202, 8, 0},
	{"HDMI_FC_DBGAUD2CH0", 0x1203, 8, 0},
	{"HDMI_FC_DBGAUD0CH1", 0x1204, 8, 0},
	{"HDMI_FC_DBGAUD1CH1", 0x1205, 8, 0},
	{"HDMI_FC_DBGAUD2CH1", 0x1206, 8, 0},
	{"HDMI_FC_DBGAUD0CH2", 0x1207, 8, 0},
	{"HDMI_FC_DBGAUD1CH2", 0x1208, 8, 0},
	{"HDMI_FC_DBGAUD2CH2", 0x1209, 8, 0},
	{"HDMI_FC_DBGAUD0CH3", 0x120A, 8, 0},
	{"HDMI_FC_DBGAUD1CH3", 0x120B, 8, 0},
	{"HDMI_FC_DBGAUD2CH3", 0x120C, 8, 0},
	{"HDMI_FC_DBGAUD0CH4", 0x120D, 8, 0},
	{"HDMI_FC_DBGAUD1CH4", 0x120E, 8, 0},
	{"HDMI_FC_DBGAUD2CH4", 0x120F, 8, 0},
	{"HDMI_FC_DBGAUD0CH5", 0x1210, 8, 0},
	{"HDMI_FC_DBGAUD1CH5", 0x1211, 8, 0},
	{"HDMI_FC_DBGAUD2CH5", 0x1212, 8, 0},
	{"HDMI_FC_DBGAUD0CH6", 0x1213, 8, 0},
	{"HDMI_FC_DBGAUD1CH6", 0x1214, 8, 0},
	{"HDMI_FC_DBGAUD2CH6", 0x1215, 8, 0},
	{"HDMI_FC_DBGAUD0CH7", 0x1216, 8, 0},
	{"HDMI_FC_DBGAUD1CH7", 0x1217, 8, 0},
	{"HDMI_FC_DBGAUD2CH7", 0x1218, 8, 0},
	{"HDMI_FC_DBGTMDS0", 0x1219, 8, 0},
	{"HDMI_FC_DBGTMDS1", 0x121A, 8, 0},
	{"HDMI_FC_DBGTMDS2", 0x121B, 8, 0},

/* HDMI Source PHY Registers */
	{"HDMI_PHY_CONF0", 0x3000, 8, 0},
	{"HDMI_PHY_TST0", 0x3001, 8, 0},
	{"HDMI_PHY_TST1", 0x3002, 8, 0},
	{"HDMI_PHY_TST2", 0x3003, 8, 0},
	{"HDMI_PHY_MASK0", 0x3006, 8, 0},
	{"HDMI_PHY_POL0", 0x3007, 8, 0},

/* HDMI Master PHY Registers */
	{"HDMI_PHY_I2CM_SLAVE_ADDR", 0x3020, 8, 0},
	{"HDMI_PHY_I2CM_ADDRESS_ADDR", 0x3021, 8, 0},
	{"HDMI_PHY_I2CM_DATAO_1_ADDR", 0x3022, 8, 0},
	{"HDMI_PHY_I2CM_DATAO_0_ADDR", 0x3023, 8, 0},
	{"HDMI_PHY_I2CM_OPERATION_ADDR", 0x3026, 8, 0},
	{"HDMI_PHY_I2CM_INT_ADDR", 0x3027, 8, 0},
	{"HDMI_PHY_I2CM_CTLINT_ADDR", 0x3028, 8, 0},
	{"HDMI_PHY_I2CM_DIV_ADDR", 0x3029, 8, 0},
	{"HDMI_PHY_I2CM_SOFTRSTZ_ADDR", 0x302a, 8, 0},
	{"HDMI_PHY_I2CM_SS_SCL_HCNT_1_ADDR", 0x302b, 8, 0},
	{"HDMI_PHY_I2CM_SS_SCL_HCNT_0_ADDR", 0x302c, 8, 0},
	{"HDMI_PHY_I2CM_SS_SCL_LCNT_1_ADDR", 0x302d, 8, 0},
	{"HDMI_PHY_I2CM_SS_SCL_LCNT_0_ADDR", 0x302e, 8, 0},
	{"HDMI_PHY_I2CM_FS_SCL_HCNT_1_ADDR", 0x302f, 8, 0},
	{"HDMI_PHY_I2CM_FS_SCL_HCNT_0_ADDR", 0x3030, 8, 0},
	{"HDMI_PHY_I2CM_FS_SCL_LCNT_1_ADDR", 0x3031, 8, 0},
	{"HDMI_PHY_I2CM_FS_SCL_LCNT_0_ADDR", 0x3032, 8, 0},
};

static struct hw_register hdmi1_ip_regs[] = {
/* Interrupt Registers */
	{"HDMI_IH_MUTE_FC_STAT0", 0x0180, 8, 0},
	{"HDMI_IH_MUTE_FC_STAT1", 0x0181, 8, 0},
	{"HDMI_IH_MUTE_FC_STAT2", 0x0182, 8, 0},
	{"HDMI_IH_MUTE_AS_STAT0", 0x0183, 8, 0},
	{"HDMI_IH_MUTE_PHY_STAT0", 0x0184, 8, 0},
	{"HDMI_IH_MUTE_I2CM_STAT0", 0x0185, 8, 0},
	{"HDMI_IH_MUTE_CEC_STAT0", 0x0186, 8, 0},
	{"HDMI_IH_MUTE_VP_STAT0", 0x0187, 8, 0},
	{"HDMI_IH_MUTE_I2CMPHY_STAT0", 0x0188, 8, 0},
	{"HDMI_IH_MUTE", 0x01FF, 8, 0},

/* Video Packetizer Registers */
	{"HDMI_VP_PR_CD", 0x0801, 8, 0},
	{"HDMI_VP_STUFF", 0x0802, 8, 0},
	{"HDMI_VP_REMAP", 0x0803, 8, 0},
	{"HDMI_VP_CONF", 0x0804, 8, 0},
	{"HDMI_VP_STAT", 0x0805, 8, 0},
	{"HDMI_VP_INT", 0x0806, 8, 0},
	{"HDMI_VP_MASK", 0x0807, 8, 0},
	{"HDMI_VP_POL", 0x0808, 8, 0},

/* Video Sample Registers */
	{"HDMI_TX_INVID0", 0x0200, 8, 0},
	{"HDMI_TX_INSTUFFING", 0x0201, 8, 0},
	{"HDMI_TX_GYDATA0", 0x0202, 8, 0},
	{"HDMI_TX_GYDATA1", 0x0203, 8, 0},
	{"HDMI_TX_RCRDATA0", 0x0204, 8, 0},
	{"HDMI_TX_RCRDATA1", 0x0205, 8, 0},
	{"HDMI_TX_BCBDATA0", 0x0206, 8, 0},
	{"HDMI_TX_BCBDATA1", 0x0207, 8, 0},


/* Frame Composer Registers */
	{"HDMI_FC_INVIDCONF", 0x1000, 8, 0},
	{"HDMI_FC_INHACTV0", 0x1001, 8, 0},
	{"HDMI_FC_INHACTV1", 0x1002, 8, 0},
	{"HDMI_FC_INHBLANK0", 0x1003, 8, 0},
	{"HDMI_FC_INHBLANK1", 0x1004, 8, 0},
	{"HDMI_FC_INVACTV0", 0x1005, 8, 0},
	{"HDMI_FC_INVACTV1", 0x1006, 8, 0},
	{"HDMI_FC_INVBLANK", 0x1007, 8, 0},
	{"HDMI_FC_HSYNCINDELAY0", 0x1008, 8, 0},
	{"HDMI_FC_HSYNCINDELAY1", 0x1009, 8, 0},
	{"HDMI_FC_HSYNCINWIDTH0", 0x100A, 8, 0},
	{"HDMI_FC_HSYNCINWIDTH1", 0x100B, 8, 0},
	{"HDMI_FC_VSYNCINDELAY", 0x100C, 8, 0},
	{"HDMI_FC_VSYNCINWIDTH", 0x100D, 8, 0},
	{"HDMI_FC_INFREQ0", 0x100E, 8, 0},
	{"HDMI_FC_INFREQ1", 0x100F, 8, 0},
	{"HDMI_FC_INFREQ2", 0x1010, 8, 0},
	{"HDMI_FC_CTRLDUR", 0x1011, 8, 0},
	{"HDMI_FC_EXCTRLDUR", 0x1012, 8, 0},
	{"HDMI_FC_EXCTRLSPAC", 0x1013, 8, 0},
	{"HDMI_FC_CH0PREAM", 0x1014, 8, 0},
	{"HDMI_FC_CH1PREAM", 0x1015, 8, 0},
	{"HDMI_FC_CH2PREAM", 0x1016, 8, 0},
	{"HDMI_FC_AVICONF3", 0x1017, 8, 0},
	{"HDMI_FC_GCP", 0x1018, 8, 0},
	{"HDMI_FC_AVICONF0", 0x1019, 8, 0},
	{"HDMI_FC_AVICONF1", 0x101A, 8, 0},
	{"HDMI_FC_AVICONF2", 0x101B, 8, 0},
	{"HDMI_FC_AVIVID", 0x101C, 8, 0},
	{"HDMI_FC_AVIETB0", 0x101D, 8, 0},
	{"HDMI_FC_AVIETB1", 0x101E, 8, 0},
	{"HDMI_FC_AVISBB0", 0x101F, 8, 0},
	{"HDMI_FC_AVISBB1", 0x1020, 8, 0},
	{"HDMI_FC_AVIELB0", 0x1021, 8, 0},
	{"HDMI_FC_AVIELB1", 0x1022, 8, 0},
	{"HDMI_FC_AVISRB0", 0x1023, 8, 0},
	{"HDMI_FC_AVISRB1", 0x1024, 8, 0},
	{"HDMI_FC_AUDICONF0", 0x1025, 8, 0},
	{"HDMI_FC_AUDICONF1", 0x1026, 8, 0},
	{"HDMI_FC_AUDICONF2", 0x1027, 8, 0},
	{"HDMI_FC_AUDICONF3", 0x1028, 8, 0},
	{"HDMI_FC_VSDIEEEID0", 0x1029, 8, 0},
	{"HDMI_FC_VSDSIZE", 0x102A, 8, 0},
	{"HDMI_FC_VSDIEEEID1", 0x1030, 8, 0},
	{"HDMI_FC_VSDIEEEID2", 0x1031, 8, 0},
	{"HDMI_FC_VSDPAYLOAD0", 0x1032, 8, 0},
	{"HDMI_FC_VSDPAYLOAD1", 0x1033, 8, 0},
	{"HDMI_FC_VSDPAYLOAD2", 0x1034, 8, 0},
	{"HDMI_FC_VSDPAYLOAD3", 0x1035, 8, 0},
	{"HDMI_FC_VSDPAYLOAD4", 0x1036, 8, 0},
	{"HDMI_FC_VSDPAYLOAD5", 0x1037, 8, 0},
	{"HDMI_FC_VSDPAYLOAD6", 0x1038, 8, 0},
	{"HDMI_FC_VSDPAYLOAD7", 0x1039, 8, 0},
	{"HDMI_FC_VSDPAYLOAD8", 0x103A, 8, 0},
	{"HDMI_FC_VSDPAYLOAD9", 0x103B, 8, 0},
	{"HDMI_FC_VSDPAYLOAD10", 0x103C, 8, 0},
	{"HDMI_FC_VSDPAYLOAD11", 0x103D, 8, 0},
	{"HDMI_FC_VSDPAYLOAD12", 0x103E, 8, 0},
	{"HDMI_FC_VSDPAYLOAD13", 0x103F, 8, 0},
	{"HDMI_FC_VSDPAYLOAD14", 0x1040, 8, 0},
	{"HDMI_FC_VSDPAYLOAD15", 0x1041, 8, 0},
	{"HDMI_FC_VSDPAYLOAD16", 0x1042, 8, 0},
	{"HDMI_FC_VSDPAYLOAD17", 0x1043, 8, 0},
	{"HDMI_FC_VSDPAYLOAD18", 0x1044, 8, 0},
	{"HDMI_FC_VSDPAYLOAD19", 0x1045, 8, 0},
	{"HDMI_FC_VSDPAYLOAD20", 0x1046, 8, 0},
	{"HDMI_FC_VSDPAYLOAD21", 0x1047, 8, 0},
	{"HDMI_FC_VSDPAYLOAD22", 0x1048, 8, 0},
	{"HDMI_FC_VSDPAYLOAD23", 0x1049, 8, 0},
	{"HDMI_FC_SPDVENDORNAME0", 0x104A, 8, 0},
	{"HDMI_FC_SPDVENDORNAME1", 0x104B, 8, 0},
	{"HDMI_FC_SPDVENDORNAME2", 0x104C, 8, 0},
	{"HDMI_FC_SPDVENDORNAME3", 0x104D, 8, 0},
	{"HDMI_FC_SPDVENDORNAME4", 0x104E, 8, 0},
	{"HDMI_FC_SPDVENDORNAME5", 0x104F, 8, 0},
	{"HDMI_FC_SPDVENDORNAME6", 0x1050, 8, 0},
	{"HDMI_FC_SPDVENDORNAME7", 0x1051, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME0", 0x1052, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME1", 0x1053, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME2", 0x1054, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME3", 0x1055, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME4", 0x1056, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME5", 0x1057, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME6", 0x1058, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME7", 0x1059, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME8", 0x105A, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME9", 0x105B, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME10", 0x105C, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME11", 0x105D, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME12", 0x105E, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME13", 0x105F, 8, 0},
	{"HDMI_FC_SDPPRODUCTNAME14", 0x1060, 8, 0},
	{"HDMI_FC_SPDPRODUCTNAME15", 0x1061, 8, 0},
	{"HDMI_FC_SPDDEVICEINF", 0x1062, 8, 0},
	{"HDMI_FC_AUDSCONF", 0x1063, 8, 0},
	{"HDMI_FC_AUDSSTAT", 0x1064, 8, 0},
	{"HDMI_FC_DATACH0FILL", 0x1070, 8, 0},
	{"HDMI_FC_DATACH1FILL", 0x1071, 8, 0},
	{"HDMI_FC_DATACH2FILL", 0x1072, 8, 0},
	{"HDMI_FC_CTRLQHIGH", 0x1073, 8, 0},
	{"HDMI_FC_CTRLQLOW", 0x1074, 8, 0},
	{"HDMI_FC_ACP0", 0x1075, 8, 0},
	{"HDMI_FC_ACP28", 0x1076, 8, 0},
	{"HDMI_FC_ACP27", 0x1077, 8, 0},
	{"HDMI_FC_ACP26", 0x1078, 8, 0},
	{"HDMI_FC_ACP25", 0x1079, 8, 0},
	{"HDMI_FC_ACP24", 0x107A, 8, 0},
	{"HDMI_FC_ACP23", 0x107B, 8, 0},
	{"HDMI_FC_ACP22", 0x107C, 8, 0},
	{"HDMI_FC_ACP21", 0x107D, 8, 0},
	{"HDMI_FC_ACP20", 0x107E, 8, 0},
	{"HDMI_FC_ACP19", 0x107F, 8, 0},
	{"HDMI_FC_ACP18", 0x1080, 8, 0},
	{"HDMI_FC_ACP17", 0x1081, 8, 0},
	{"HDMI_FC_ACP16", 0x1082, 8, 0},
	{"HDMI_FC_ACP15", 0x1083, 8, 0},
	{"HDMI_FC_ACP14", 0x1084, 8, 0},
	{"HDMI_FC_ACP13", 0x1085, 8, 0},
	{"HDMI_FC_ACP12", 0x1086, 8, 0},
	{"HDMI_FC_ACP11", 0x1087, 8, 0},
	{"HDMI_FC_ACP10", 0x1088, 8, 0},
	{"HDMI_FC_ACP9", 0x1089, 8, 0},
	{"HDMI_FC_ACP8", 0x108A, 8, 0},
	{"HDMI_FC_ACP7", 0x108B, 8, 0},
	{"HDMI_FC_ACP6", 0x108C, 8, 0},
	{"HDMI_FC_ACP5", 0x108D, 8, 0},
	{"HDMI_FC_ACP4", 0x108E, 8, 0},
	{"HDMI_FC_ACP3", 0x108F, 8, 0},
	{"HDMI_FC_ACP2", 0x1090, 8, 0},
	{"HDMI_FC_ACP1", 0x1091, 8, 0},
	{"HDMI_FC_ISCR1_0", 0x1092, 8, 0},
	{"HDMI_FC_ISCR1_16", 0x1093, 8, 0},
	{"HDMI_FC_ISCR1_15", 0x1094, 8, 0},
	{"HDMI_FC_ISCR1_14", 0x1095, 8, 0},
	{"HDMI_FC_ISCR1_13", 0x1096, 8, 0},
	{"HDMI_FC_ISCR1_12", 0x1097, 8, 0},
	{"HDMI_FC_ISCR1_11", 0x1098, 8, 0},
	{"HDMI_FC_ISCR1_10", 0x1099, 8, 0},
	{"HDMI_FC_ISCR1_9", 0x109A, 8, 0},
	{"HDMI_FC_ISCR1_8", 0x109B, 8, 0},
	{"HDMI_FC_ISCR1_7", 0x109C, 8, 0},
	{"HDMI_FC_ISCR1_6", 0x109D, 8, 0},
	{"HDMI_FC_ISCR1_5", 0x109E, 8, 0},
	{"HDMI_FC_ISCR1_4", 0x109F, 8, 0},
	{"HDMI_FC_ISCR1_3", 0x10A0, 8, 0},
	{"HDMI_FC_ISCR1_2", 0x10A1, 8, 0},
	{"HDMI_FC_ISCR1_1", 0x10A2, 8, 0},
	{"HDMI_FC_ISCR2_15", 0x10A3, 8, 0},
	{"HDMI_FC_ISCR2_14", 0x10A4, 8, 0},
	{"HDMI_FC_ISCR2_13", 0x10A5, 8, 0},
	{"HDMI_FC_ISCR2_12", 0x10A6, 8, 0},
	{"HDMI_FC_ISCR2_11", 0x10A7, 8, 0},
	{"HDMI_FC_ISCR2_10", 0x10A8, 8, 0},
	{"HDMI_FC_ISCR2_9", 0x10A9, 8, 0},
	{"HDMI_FC_ISCR2_8", 0x10AA, 8, 0},
	{"HDMI_FC_ISCR2_7", 0x10AB, 8, 0},
	{"HDMI_FC_ISCR2_6", 0x10AC, 8, 0},
	{"HDMI_FC_ISCR2_5", 0x10AD, 8, 0},
	{"HDMI_FC_ISCR2_4", 0x10AE, 8, 0},
	{"HDMI_FC_ISCR2_3", 0x10AF, 8, 0},
	{"HDMI_FC_ISCR2_2", 0x10B0, 8, 0},
	{"HDMI_FC_ISCR2_1", 0x10B1, 8, 0},
	{"HDMI_FC_ISCR2_0", 0x10B2, 8, 0},
	{"HDMI_FC_DATAUTO0", 0x10B3, 8, 0},
	{"HDMI_FC_DATAUTO1", 0x10B4, 8, 0},
	{"HDMI_FC_DATAUTO2", 0x10B5, 8, 0},
	{"HDMI_FC_DATMAN", 0x10B6, 8, 0},
	{"HDMI_FC_DATAUTO3", 0x10B7, 8, 0},
	{"HDMI_FC_RDRB0", 0x10B8, 8, 0},
	{"HDMI_FC_RDRB1", 0x10B9, 8, 0},
	{"HDMI_FC_RDRB2", 0x10BA, 8, 0},
	{"HDMI_FC_RDRB3", 0x10BB, 8, 0},
	{"HDMI_FC_RDRB4", 0x10BC, 8, 0},
	{"HDMI_FC_RDRB5", 0x10BD, 8, 0},
	{"HDMI_FC_RDRB6", 0x10BE, 8, 0},
	{"HDMI_FC_RDRB7", 0x10BF, 8, 0},
	{"HDMI_FC_STAT0", 0x10D0, 8, 0},
	{"HDMI_FC_INT0", 0x10D1, 8, 0},
	{"HDMI_FC_MASK0", 0x10D2, 8, 0},
	{"HDMI_FC_POL0", 0x10D3, 8, 0},
	{"HDMI_FC_STAT1", 0x10D4, 8, 0},
	{"HDMI_FC_INT1", 0x10D5, 8, 0},
	{"HDMI_FC_MASK1", 0x10D6, 8, 0},
	{"HDMI_FC_POL1", 0x10D7, 8, 0},
	{"HDMI_FC_STAT2", 0x10D8, 8, 0},
	{"HDMI_FC_INT2", 0x10D9, 8, 0},
	{"HDMI_FC_MASK2", 0x10DA, 8, 0},
	{"HDMI_FC_POL2", 0x10DB, 8, 0},
	{"HDMI_FC_PRCONF", 0x10E0, 8, 0},

	{"HDMI_FC_GMD_EN", 0x1101, 8, 0},
	{"HDMI_FC_GMD_UP", 0x1102, 8, 0},
	{"HDMI_FC_GMD_CONF", 0x1103, 8, 0},
	{"HDMI_FC_GMD_HB", 0x1104, 8, 0},
	{"HDMI_FC_GMD_PB0", 0x1105, 8, 0},
	{"HDMI_FC_GMD_PB1", 0x1106, 8, 0},
	{"HDMI_FC_GMD_PB2", 0x1107, 8, 0},
	{"HDMI_FC_GMD_PB3", 0x1108, 8, 0},
	{"HDMI_FC_GMD_PB4", 0x1109, 8, 0},
	{"HDMI_FC_GMD_PB5", 0x110A, 8, 0},
	{"HDMI_FC_GMD_PB6", 0x110B, 8, 0},
	{"HDMI_FC_GMD_PB7", 0x110C, 8, 0},
	{"HDMI_FC_GMD_PB8", 0x110D, 8, 0},
	{"HDMI_FC_GMD_PB9", 0x110E, 8, 0},
	{"HDMI_FC_GMD_PB10", 0x110F, 8, 0},
	{"HDMI_FC_GMD_PB11", 0x1110, 8, 0},
	{"HDMI_FC_GMD_PB12", 0x1111, 8, 0},
	{"HDMI_FC_GMD_PB13", 0x1112, 8, 0},
	{"HDMI_FC_GMD_PB14", 0x1113, 8, 0},
	{"HDMI_FC_GMD_PB15", 0x1114, 8, 0},
	{"HDMI_FC_GMD_PB16", 0x1115, 8, 0},
	{"HDMI_FC_GMD_PB17", 0x1116, 8, 0},
	{"HDMI_FC_GMD_PB18", 0x1117, 8, 0},
	{"HDMI_FC_GMD_PB19", 0x1118, 8, 0},
	{"HDMI_FC_GMD_PB20", 0x1119, 8, 0},
	{"HDMI_FC_GMD_PB21", 0x111A, 8, 0},
	{"HDMI_FC_GMD_PB22", 0x111B, 8, 0},
	{"HDMI_FC_GMD_PB23", 0x111C, 8, 0},
	{"HDMI_FC_GMD_PB24", 0x111D, 8, 0},
	{"HDMI_FC_GMD_PB25", 0x111E, 8, 0},
	{"HDMI_FC_GMD_PB26", 0x111F, 8, 0},
	{"HDMI_FC_GMD_PB27", 0x1120, 8, 0},

	{"HDMI_FC_DBGFORCE", 0x1200, 8, 0},
	{"HDMI_FC_DBGAUD0CH0", 0x1201, 8, 0},
	{"HDMI_FC_DBGAUD1CH0", 0x1202, 8, 0},
	{"HDMI_FC_DBGAUD2CH0", 0x1203, 8, 0},
	{"HDMI_FC_DBGAUD0CH1", 0x1204, 8, 0},
	{"HDMI_FC_DBGAUD1CH1", 0x1205, 8, 0},
	{"HDMI_FC_DBGAUD2CH1", 0x1206, 8, 0},
	{"HDMI_FC_DBGAUD0CH2", 0x1207, 8, 0},
	{"HDMI_FC_DBGAUD1CH2", 0x1208, 8, 0},
	{"HDMI_FC_DBGAUD2CH2", 0x1209, 8, 0},
	{"HDMI_FC_DBGAUD0CH3", 0x120A, 8, 0},
	{"HDMI_FC_DBGAUD1CH3", 0x120B, 8, 0},
	{"HDMI_FC_DBGAUD2CH3", 0x120C, 8, 0},
	{"HDMI_FC_DBGAUD0CH4", 0x120D, 8, 0},
	{"HDMI_FC_DBGAUD1CH4", 0x120E, 8, 0},
	{"HDMI_FC_DBGAUD2CH4", 0x120F, 8, 0},
	{"HDMI_FC_DBGAUD0CH5", 0x1210, 8, 0},
	{"HDMI_FC_DBGAUD1CH5", 0x1211, 8, 0},
	{"HDMI_FC_DBGAUD2CH5", 0x1212, 8, 0},
	{"HDMI_FC_DBGAUD0CH6", 0x1213, 8, 0},
	{"HDMI_FC_DBGAUD1CH6", 0x1214, 8, 0},
	{"HDMI_FC_DBGAUD2CH6", 0x1215, 8, 0},
	{"HDMI_FC_DBGAUD0CH7", 0x1216, 8, 0},
	{"HDMI_FC_DBGAUD1CH7", 0x1217, 8, 0},
	{"HDMI_FC_DBGAUD2CH7", 0x1218, 8, 0},
	{"HDMI_FC_DBGTMDS0", 0x1219, 8, 0},
	{"HDMI_FC_DBGTMDS1", 0x121A, 8, 0},
	{"HDMI_FC_DBGTMDS2", 0x121B, 8, 0},

/* HDMI Source PHY Registers */
	{"HDMI_PHY_CONF0", 0x3000, 8, 0},
	{"HDMI_PHY_TST0", 0x3001, 8, 0},
	{"HDMI_PHY_TST1", 0x3002, 8, 0},
	{"HDMI_PHY_TST2", 0x3003, 8, 0},
	{"HDMI_PHY_MASK0", 0x3006, 8, 0},
	{"HDMI_PHY_POL0", 0x3007, 8, 0},

/* HDMI Master PHY Registers */
	{"HDMI_PHY_I2CM_SLAVE_ADDR", 0x3020, 8, 0},
	{"HDMI_PHY_I2CM_ADDRESS_ADDR", 0x3021, 8, 0},
	{"HDMI_PHY_I2CM_DATAO_1_ADDR", 0x3022, 8, 0},
	{"HDMI_PHY_I2CM_DATAO_0_ADDR", 0x3023, 8, 0},
	{"HDMI_PHY_I2CM_OPERATION_ADDR", 0x3026, 8, 0},
	{"HDMI_PHY_I2CM_INT_ADDR", 0x3027, 8, 0},
	{"HDMI_PHY_I2CM_CTLINT_ADDR", 0x3028, 8, 0},
	{"HDMI_PHY_I2CM_DIV_ADDR", 0x3029, 8, 0},
	{"HDMI_PHY_I2CM_SOFTRSTZ_ADDR", 0x302a, 8, 0},
	{"HDMI_PHY_I2CM_SS_SCL_HCNT_1_ADDR", 0x302b, 8, 0},
	{"HDMI_PHY_I2CM_SS_SCL_HCNT_0_ADDR", 0x302c, 8, 0},
	{"HDMI_PHY_I2CM_SS_SCL_LCNT_1_ADDR", 0x302d, 8, 0},
	{"HDMI_PHY_I2CM_SS_SCL_LCNT_0_ADDR", 0x302e, 8, 0},
	{"HDMI_PHY_I2CM_FS_SCL_HCNT_1_ADDR", 0x302f, 8, 0},
	{"HDMI_PHY_I2CM_FS_SCL_HCNT_0_ADDR", 0x3030, 8, 0},
	{"HDMI_PHY_I2CM_FS_SCL_LCNT_1_ADDR", 0x3031, 8, 0},
	{"HDMI_PHY_I2CM_FS_SCL_LCNT_0_ADDR", 0x3032, 8, 0},
};

static struct rcar_ip hdmi0_ip = {
	.ip_name = "HDMI0",
	.base_addr = 0xFEAD0000,
	.reg_count = ARRAY_SIZE(hdmi0_ip_regs),
	.size = 0x3100,
	.ip_reg = hdmi0_ip_regs,
};

static struct rcar_ip hdmi1_ip = {
	.ip_name = "HDMI1",
	.base_addr = 0xFEAE0000,
	.reg_count = ARRAY_SIZE(hdmi1_ip_regs),
	.size = 0x3100,
	.ip_reg = hdmi1_ip_regs,
};

static struct rcar_ip *ip_tbl[] = {
	&hdmi0_ip,
	&hdmi1_ip,
};

void rcar_du_hdmienc_backup(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	struct rcar_ip *ip = ip_tbl[hdmienc->index];
	int ret;

	if (!ip->virt_addr) {
		ret = handle_registers(ip, DO_IOREMAP);
		if (ret)
			pr_err("%s: Failed to map %s register\n",
				__func__, ip->ip_name);
	}

	ret = handle_registers(ip, DO_BACKUP);
	if (ret)
		pr_err("%s: Failed to backup %s register\n",
			__func__, ip->ip_name);
}

#define SMSTPCR7 0xE615014C

void rcar_du_hdmienc_restore(struct drm_encoder *encoder)
{
	void __iomem *smstpcr = ioremap_nocache(SMSTPCR7, 4);
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	struct rcar_ip *ip = ip_tbl[hdmienc->index];
	u32 temp;
	int ret;

	temp = readl(smstpcr);
	writel(temp & ~(0x3 << 28), smstpcr);

	rcar_du_hdmienc_enable(encoder);

	ret = handle_registers(ip, DO_RESTORE);
	if (ret)
		pr_err("%s: Failed to restore %s register\n",
			__func__, ip->ip_name);
}
#endif /* CONFIG_RCAR_DDR_BACKUP */

void rcar_du_hdmienc_disable(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;

	if ((bfuncs) && (bfuncs->disable))
		bfuncs->disable(encoder->bridge);

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_enable(hdmienc->renc->lvds, encoder->crtc,
				       false);

	hdmienc->enabled = false;
}

void rcar_du_hdmienc_enable(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_enable(hdmienc->renc->lvds, encoder->crtc,
				       true);
	if ((bfuncs) && (bfuncs->enable))
		bfuncs->enable(encoder->bridge);

	hdmienc->enabled = true;
}

static int rcar_du_hdmienc_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	const struct drm_display_mode *mode = &crtc_state->mode;
	int ret = 0;

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_atomic_check(hdmienc->renc->lvds,
					     adjusted_mode);

	if ((bfuncs) && (bfuncs->mode_fixup))
		ret = bfuncs->mode_fixup(encoder->bridge, mode,
				 adjusted_mode) ? 0 : -EINVAL;
	return ret;
}

static void rcar_du_hdmienc_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	const struct drm_bridge_funcs *bfuncs = encoder->bridge->funcs;

	if ((bfuncs) && (bfuncs->mode_set))
		bfuncs->mode_set(encoder->bridge, mode, adjusted_mode);

	rcar_du_crtc_route_output(encoder->crtc, hdmienc->renc->output);
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.mode_set = rcar_du_hdmienc_mode_set,
	.disable = rcar_du_hdmienc_disable,
	.enable = rcar_du_hdmienc_enable,
	.atomic_check = rcar_du_hdmienc_atomic_check,
};

static void rcar_du_hdmienc_cleanup(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);

	if (hdmienc->enabled)
		rcar_du_hdmienc_disable(encoder);

	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = rcar_du_hdmienc_cleanup,
};

static const struct dw_hdmi_mpll_config rcar_du_hdmienc_mpll_cfg[] = {
	{
		44900000, {
			{ 0x0003, 0x0000},
			{ 0x0003, 0x0000},
			{ 0x0003, 0x0000}
		},
	}, {
		90000000, {
			{ 0x0002, 0x0000},
			{ 0x0002, 0x0000},
			{ 0x0002, 0x0000}
		},
	}, {
		182750000, {
			{ 0x0001, 0x0000},
			{ 0x0001, 0x0000},
			{ 0x0001, 0x0000}
		},
	}, {
		297000000, {
			{ 0x0000, 0x0000},
			{ 0x0000, 0x0000},
			{ 0x0000, 0x0000}
		},
	}, {
		~0UL, {
			{ 0xFFFF, 0xFFFF },
			{ 0xFFFF, 0xFFFF },
			{ 0xFFFF, 0xFFFF },
		},
	}
};
static const struct dw_hdmi_curr_ctrl rcar_du_hdmienc_cur_ctr[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		35500000,  { 0x0344, 0x0000, 0x0000 },
	}, {
		44900000,  { 0x0285, 0x0000, 0x0000 },
	}, {
		71000000,  { 0x1184, 0x0000, 0x0000 },
	}, {
		90000000,  { 0x1144, 0x0000, 0x0000 },
	}, {
		140250000, { 0x20c4, 0x0000, 0x0000 },
	}, {
		182750000, { 0x2084, 0x0000, 0x0000 },
	}, {
		297000000, { 0x0084, 0x0000, 0x0000 },
	}, {
		~0UL,      { 0x0000, 0x0000, 0x0000 },
	}
};

static const struct dw_hdmi_multi_div rcar_du_hdmienc_multi_div[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		35500000,  { 0x0328, 0x0000, 0x0000 },
	}, {
		44900000,  { 0x0128, 0x0000, 0x0000 },
	}, {
		71000000,  { 0x0314, 0x0000, 0x0000 },
	}, {
		90000000,  { 0x0114, 0x0000, 0x0000 },
	}, {
		140250000, { 0x030a, 0x0000, 0x0000 },
	}, {
		182750000, { 0x010a, 0x0000, 0x0000 },
	}, {
		281250000, { 0x0305, 0x0000, 0x0000 },
	}, {
		297000000, { 0x0105, 0x0000, 0x0000 },
	}, {
		~0UL,      { 0x0000, 0x0000, 0x0000 },
	}
};

static const struct dw_hdmi_phy_config rcar_du_hdmienc_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 74250000,  0x8009, 0x0004, 0x0272},
	{ 148500000, 0x802b, 0x0004, 0x028d},
	{ 297000000, 0x8039, 0x0005, 0x028d},
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static enum drm_mode_status
rcar_du_hdmienc_mode_valid(struct drm_connector *connector,
			    struct drm_display_mode *mode)
{
	if ((mode->hdisplay > 3840) || (mode->vdisplay > 2160))
		return MODE_BAD;

	if (((mode->hdisplay == 3840) && (mode->vdisplay == 2160))
		&& (mode->vrefresh > 30))
		return MODE_BAD;

	if (mode->clock > 297000)
		return MODE_BAD;

	return MODE_OK;
}

static const struct dw_hdmi_plat_data rcar_du_hdmienc_hdmi0_drv_data = {
	.mode_valid = rcar_du_hdmienc_mode_valid,
	.mpll_cfg   = rcar_du_hdmienc_mpll_cfg,
	.cur_ctr    = rcar_du_hdmienc_cur_ctr,
	.multi_div  = rcar_du_hdmienc_multi_div,
	.phy_config = rcar_du_hdmienc_phy_config,
	.dev_type   = RCAR_HDMI,
	.index      = 0,
};

static const struct dw_hdmi_plat_data rcar_du_hdmienc_hdmi1_drv_data = {
	.mode_valid = rcar_du_hdmienc_mode_valid,
	.mpll_cfg   = rcar_du_hdmienc_mpll_cfg,
	.cur_ctr    = rcar_du_hdmienc_cur_ctr,
	.multi_div  = rcar_du_hdmienc_multi_div,
	.phy_config = rcar_du_hdmienc_phy_config,
	.dev_type   = RCAR_HDMI,
	.index      = 1,
};

static const struct of_device_id rcar_du_hdmienc_dt_ids[] = {
	{
		.data = &rcar_du_hdmienc_hdmi0_drv_data
	},
	{
		.data = &rcar_du_hdmienc_hdmi1_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_du_hdmienc_dt_ids);

int rcar_du_hdmienc_init(struct rcar_du_device *rcdu,
			 struct rcar_du_encoder *renc, struct device_node *np)
{
	struct drm_encoder *encoder = rcar_encoder_to_drm_encoder(renc);
	struct rcar_du_hdmienc *hdmienc;
	struct resource *iores;
	struct platform_device *pdev;
	const struct dw_hdmi_plat_data *plat_data;
	int ret, irq;
	bool dw_hdmi_use = false;
	struct drm_bridge *bridge = NULL;

	hdmienc = devm_kzalloc(rcdu->dev, sizeof(*hdmienc), GFP_KERNEL);
	if (hdmienc == NULL)
		return -ENOMEM;

	if (strcmp(renc->device_name, "rockchip,rcar-dw-hdmi") == 0)
		dw_hdmi_use = true;

	if (dw_hdmi_use) {
		if (renc->output == RCAR_DU_OUTPUT_HDMI0)
			hdmienc->index = 0;
		else if (renc->output == RCAR_DU_OUTPUT_HDMI1)
			hdmienc->index = 1;
		else
			return -EINVAL;

		np = of_parse_phandle(rcdu->dev->of_node, "hdmi",
						 hdmienc->index);
		if (!np) {
			dev_err(rcdu->dev, "hdmi node not found\n");
			return -ENXIO;
		}

		pdev = of_find_device_by_node(np);
		of_node_put(np);
		if (!pdev)
			return -ENXIO;

		plat_data = rcar_du_hdmienc_dt_ids[hdmienc->index].data;
		hdmienc->dev = &pdev->dev;

		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;

		iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!iores)
			return -ENXIO;

	} else {
		/* Locate the DRM bridge from the HDMI encoder DT node. */
		bridge = of_drm_find_bridge(np);
		if (!bridge) {
			//dev_dbg(rcdu->dev, "can't get bridge for %s, deferring probe\n",
			printk("===can't get bridge for %s, deferring probe\n",
				of_node_full_name(np));
			return -EPROBE_DEFER;
		}
	}

	ret = drm_encoder_init(rcdu->ddev, encoder, &encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret < 0)
		return ret;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);

	renc->hdmi = hdmienc;
	hdmienc->renc = renc;

	/* Link the bridge to the encoder. */
	if (bridge) {
		bridge->encoder = encoder;
		encoder->bridge = bridge;
	}

	if (dw_hdmi_use)
		ret = dw_hdmi_bind(rcdu->dev, NULL, rcdu->ddev, encoder,
				iores, irq, plat_data);

	if (bridge) {
		ret = drm_bridge_attach(rcdu->ddev, bridge);
		if (ret) {
			drm_encoder_cleanup(encoder);
			return ret;
		}
	}

	return 0;
}
