/*
 * R-Car Gen3 processor support - PFC hardware block.
 *
 * Copyright (C) 2015  Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/kernel.h>
#include <linux/platform_data/gpio-rcar.h>

#include "core.h"
#include "sh_pfc.h"

#define CPU_ALL_PORT(fn, sfx)						\
	PORT_GP_32(0, fn, sfx),						\
	PORT_GP_32(1, fn, sfx),						\
	PORT_GP_32(2, fn, sfx),						\
	PORT_GP_32(3, fn, sfx),						\
	PORT_GP_32(4, fn, sfx),						\
	PORT_GP_32(5, fn, sfx),						\
	PORT_GP_32(6, fn, sfx),						\
	PORT_GP_32(7, fn, sfx)

enum {
	PINMUX_RESERVED = 0,

	PINMUX_DATA_BEGIN,
	GP_ALL(DATA),
	PINMUX_DATA_END,

	PINMUX_FUNCTION_BEGIN,
	GP_ALL(FN),

	/* IPSR0 */	/* IPSR1 */	/* IPSR2 */	/* IPSR3 */
	FN_IP0_3_0,	FN_IP1_3_0,	FN_IP2_3_0,	FN_IP3_3_0,
	FN_IP0_7_4,	FN_IP1_7_4,	FN_IP2_7_4,	FN_IP3_7_4,
	FN_IP0_11_8,	FN_IP1_11_8,	FN_IP2_11_8,	FN_IP3_11_8,
	FN_IP0_15_12,	FN_IP1_15_12,	FN_IP2_15_12,	FN_IP3_15_12,
	FN_IP0_19_16,	FN_IP1_19_16,	FN_IP2_19_16,	FN_IP3_19_16,
	FN_IP0_23_20,	FN_IP1_23_20,	FN_IP2_23_20,	FN_IP3_23_20,
	FN_IP0_27_24,	FN_IP1_27_24,	FN_IP2_27_24,	FN_IP3_27_24,
	FN_IP0_31_28,	FN_IP1_31_28,	FN_IP2_31_28,	FN_IP3_31_28,

	/* IPSR4 */	/* IPSR5 */	/* IPSR6 */	/* IPSR7 */
	FN_IP4_3_0,	FN_IP5_3_0,	FN_IP6_3_0,	FN_IP7_3_0,
	FN_IP4_7_4,	FN_IP5_7_4,	FN_IP6_7_4,	FN_IP7_7_4,
	FN_IP4_11_8,	FN_IP5_11_8,	FN_IP6_11_8,	FN_IP7_11_8,
	FN_IP4_15_12,	FN_IP5_15_12,	FN_IP6_15_12,	FN_IP7_15_12,
	FN_IP4_19_16,	FN_IP5_19_16,	FN_IP6_19_16,	FN_IP7_19_16,
	FN_IP4_23_20,	FN_IP5_23_20,	FN_IP6_23_20,	FN_IP7_23_20,
	FN_IP4_27_24,	FN_IP5_27_24,	FN_IP6_27_24,	FN_IP7_27_24,
	FN_IP4_31_28,	FN_IP5_31_28,	FN_IP6_31_28,	FN_IP7_31_28,

	/* IPSR8 */	/* IPSR9 */	/* IPSR10 */	/* IPSR11 */
	FN_IP8_3_0,	FN_IP9_3_0,	FN_IP10_3_0,	FN_IP11_3_0,
	FN_IP8_7_4,	FN_IP9_7_4,	FN_IP10_7_4,	FN_IP11_7_4,
	FN_IP8_11_8,	FN_IP9_11_8,	FN_IP10_11_8,	FN_IP11_11_8,
	FN_IP8_15_12,	FN_IP9_15_12,	FN_IP10_15_12,	FN_IP11_15_12,
	FN_IP8_19_16,	FN_IP9_19_16,	FN_IP10_19_16,	FN_IP11_19_16,
	FN_IP8_23_20,	FN_IP9_23_20,	FN_IP10_23_20,	FN_IP11_23_20,
	FN_IP8_27_24,	FN_IP9_27_24,	FN_IP10_27_24,	FN_IP11_27_24,
	FN_IP8_31_28,	FN_IP9_31_28,	FN_IP10_31_28,	FN_IP11_31_28,

	/* IPSR12 */	/* IPSR13 */	/* IPSR14 */	/* IPSR15 */
	FN_IP12_3_0,	FN_IP13_3_0,	FN_IP14_3_0,	FN_IP15_3_0,
	FN_IP12_7_4,	FN_IP13_7_4,	FN_IP14_7_4,	FN_IP15_7_4,
	FN_IP12_11_8,	FN_IP13_11_8,	FN_IP14_11_8,	FN_IP15_11_8,
	FN_IP12_15_12,	FN_IP13_15_12,	FN_IP14_15_12,	FN_IP15_15_12,
	FN_IP12_19_16,	FN_IP13_19_16,	FN_IP14_19_16,	FN_IP15_19_16,
	FN_IP12_23_20,	FN_IP13_23_20,	FN_IP14_23_20,	FN_IP15_23_20,
	FN_IP12_27_24,	FN_IP13_27_24,	FN_IP14_27_24,	FN_IP15_27_24,
	FN_IP12_31_28,	FN_IP13_31_28,	FN_IP14_31_28,	FN_IP15_31_28,

	/* IPSR16 */	/* IPSR17 */
	FN_IP16_3_0,	FN_IP17_3_0,
	FN_IP16_7_4,	FN_IP17_7_4,
	FN_IP16_11_8,
	FN_IP16_15_12,
	FN_IP16_19_16,
	FN_IP16_23_20,
	FN_IP16_27_24,
	FN_IP16_31_28,

	/* MOD_SEL0 */
	FN_SEL_MSIOF3_0,	FN_SEL_MSIOF3_1,	FN_SEL_MSIOF3_2,	FN_SEL_MSIOF3_3,
	FN_SEL_MSIOF2_0,	FN_SEL_MSIOF2_1,	FN_SEL_MSIOF2_2,	FN_SEL_MSIOF2_3,
	FN_SEL_MSIOF1_0,	FN_SEL_MSIOF1_1,	FN_SEL_MSIOF1_2,	FN_SEL_MSIOF1_3,
	FN_SEL_MSIOF1_4,	FN_SEL_MSIOF1_5,	FN_SEL_MSIOF1_6,	FN_SEL_MSIOF1_7,
	FN_SEL_LBSC_0,		FN_SEL_LBSC_1,
	FN_SEL_IEBUS_0,		FN_SEL_IEBUS_1,
	FN_SEL_I2C6_0,		FN_SEL_I2C6_1,		FN_SEL_I2C6_2,		FN_SEL_I2C6_3,
	FN_SEL_I2C2_0,		FN_SEL_I2C2_1,
	FN_SEL_I2C1_0,		FN_SEL_I2C1_1,
	FN_SEL_HSCIF4_0,	FN_SEL_HSCIF4_1,
	FN_SEL_HSCIF3_0,	FN_SEL_HSCIF3_1,	FN_SEL_HSCIF3_2,	FN_SEL_HSCIF3_3,
	FN_SEL_HSCIF2_0,	FN_SEL_HSCIF2_1,
	FN_SEL_HSCIF1_0,	FN_SEL_HSCIF1_1,
	FN_SEL_FSO_0,		FN_SEL_FSO_1,
	FN_SEL_FM_0,		FN_SEL_FM_1,
	FN_SEL_ETHERAVB_0,	FN_SEL_ETHERAVB_1,
	FN_SEL_DRIF3_0,		FN_SEL_DRIF3_1,
	FN_SEL_DRIF2_0,		FN_SEL_DRIF2_1,
	FN_SEL_DRIF1_0,		FN_SEL_DRIF1_1,		FN_SEL_DRIF1_2,		FN_SEL_DRIF1_3,
	FN_SEL_DRIF0_0,		FN_SEL_DRIF0_1,		FN_SEL_DRIF0_2,		FN_SEL_DRIF0_3,
	FN_SEL_CANFD0_0,	FN_SEL_CANFD0_1,
	FN_SEL_ADG_0,		FN_SEL_ADG_1,		FN_SEL_ADG_2,		FN_SEL_ADG_3,

	/* MOD_SEL1 */
	FN_SEL_TSIF1_0,		FN_SEL_TSIF1_1,		FN_SEL_TSIF1_2,		FN_SEL_TSIF1_3,
	FN_SEL_TSIF0_0,		FN_SEL_TSIF0_1,		FN_SEL_TSIF0_2,		FN_SEL_TSIF0_3,
	FN_SEL_TSIF0_4,		FN_SEL_TSIF0_5,		FN_SEL_TSIF0_6,		FN_SEL_TSIF0_7,
	FN_SEL_TIMER_TMU_0,	FN_SEL_TIMER_TMU_1,
	FN_SEL_SSP1_1_0,	FN_SEL_SSP1_1_1,	FN_SEL_SSP1_1_2,	FN_SEL_SSP1_1_3,
	FN_SEL_SSP1_0_0,	FN_SEL_SSP1_0_1,	FN_SEL_SSP1_0_2,	FN_SEL_SSP1_0_3,
	FN_SEL_SSP1_0_4,	FN_SEL_SSP1_0_5,	FN_SEL_SSP1_0_6,	FN_SEL_SSP1_0_7,
	FN_SEL_SSI_0,		FN_SEL_SSI_1,
	FN_SEL_SPEED_PULSE_0,	FN_SEL_SPEED_PULSE_1,
	FN_SEL_SIMCARD_0,	FN_SEL_SIMCARD_1,	FN_SEL_SIMCARD_2,	FN_SEL_SIMCARD_3,
	FN_SEL_SDHI2_0,		FN_SEL_SDHI2_1,
	FN_SEL_SCIF4_0,		FN_SEL_SCIF4_1,		FN_SEL_SCIF4_2,		FN_SEL_SCIF4_3,
	FN_SEL_SCIF3_0,		FN_SEL_SCIF3_1,
	FN_SEL_SCIF2_0,		FN_SEL_SCIF2_1,
	FN_SEL_SCIF1_0,		FN_SEL_SCIF1_1,
	FN_SEL_SCIF_0,		FN_SEL_SCIF_1,
	FN_SEL_REMOCON_0,	FN_SEL_REMOCON_1,
	FN_SEL_RCAN0_0,		FN_SEL_RCAN0_1,
	FN_SEL_PWM6_0,		FN_SEL_PWM6_1,
	FN_SEL_PWM5_0,		FN_SEL_PWM5_1,
	FN_SEL_PWM4_0,		FN_SEL_PWM4_1,
	FN_SEL_PWM3_0,		FN_SEL_PWM3_1,
	FN_SEL_PWM2_0,		FN_SEL_PWM2_1,
	FN_SEL_PWM1_0,		FN_SEL_PWM1_1,

	/* MOD_SEL2 */
	FN_SEL_I2C_5_0,		FN_SEL_I2C_5_1,
	FN_SEL_I2C_3_0,		FN_SEL_I2C_3_1,
	FN_SEL_I2C_0_0,		FN_SEL_I2C_0_1,
	FN_SEL_VSP_0,		FN_SEL_VSP_1,		FN_SEL_VSP_2,		FN_SEL_VSP_3,
	FN_SEL_VIN4_0,		FN_SEL_VIN4_1,

	/* EthernetAVB */
	FN_AVB_MDC,		FN_AVB_MAGIC,		FN_AVB_PHY_INT,		FN_AVB_LINK,
	FN_AVB_AVTP_PPS,
	FN_AVB_AVTP_MATCH_A,	FN_AVB_AVTP_CAPTURE_A,
	FN_AVB_AVTP_MATCH_B,	FN_AVB_AVTP_CAPTURE_B,

	/* DU */
	FN_DU_DR7,				FN_DU_DR6,
	FN_DU_DR5,				FN_DU_DR4,
	FN_DU_DR3,				FN_DU_DR2,
	FN_DU_DR1,				FN_DU_DR0,
	FN_DU_DG7,				FN_DU_DG6,
	FN_DU_DG5,				FN_DU_DG4,
	FN_DU_DG3,				FN_DU_DG2,
	FN_DU_DG1,				FN_DU_DG0,
	FN_DU_DB7,				FN_DU_DB6,
	FN_DU_DB5,				FN_DU_DB4,
	FN_DU_DB3,				FN_DU_DB2,
	FN_DU_DB1,				FN_DU_DB0,
	FN_DU_DOTCLKOUT0,			FN_DU_DOTCLKOUT1,
	FN_DU_DISP,				FN_DU_CDE,
	FN_DU_EXVSYNC_DU_VSYNC,			FN_DU_EXHSYNC_DU_HSYNC,
	FN_DU_EXODDF_DU_ODDF_DISP_CDE,

	/* HDMI */
	FN_HDMI0_CEC,		FN_HDMI1_CEC,

	/* SCIF0 */
	FN_RX0,		FN_TX0,		FN_SCK0,	FN_RTS0_N_TANS,		FN_CTS0_N,
	/* SCIF1 */
	FN_RX1_A,	FN_TX1_A,	FN_SCK1,	FN_RTS1_N_TANS,		FN_CTS1_N,
	FN_RX1_B,	FN_TX1_B,
	/* SCIF2 */
	FN_RX2_A,	FN_TX2_A,	FN_SCK2,
	FN_RX2_B,	FN_TX2_B,
	/* SCIF3 */
	FN_RX3_A,	FN_TX3_A,	FN_SCK3,	FN_RTS3_N_TANS,		FN_CTS3_N,
	FN_RX3_B,	FN_TX3_B,
	/* SCIF4 */
	FN_RX4_A,	FN_TX4_A,	FN_SCK4_A,	FN_RTS4_N_TANS_A,	FN_CTS4_N_A,
	FN_RX4_B,	FN_TX4_B,	FN_SCK4_B,	FN_RTS4_N_TANS_B,	FN_CTS4_N_B,
	FN_RX4_C,	FN_TX4_C,	FN_SCK4_C,	FN_RTS4_N_TANS_C,	FN_CTS4_N_C,
	/* SCIF5 */
	FN_RX5,		FN_TX5,		FN_SCK5,

	/* SDHI0 */
	FN_SD0_CLK,	FN_SD0_CMD,
	FN_SD0_DAT0,	FN_SD0_DAT1,
	FN_SD0_DAT2,	FN_SD0_DAT3,
	FN_SD0_CD,	FN_SD0_WP,
	/* SDHI1 */
	FN_SD1_CLK,	FN_SD1_CMD,
	FN_SD1_DAT0,	FN_SD1_DAT1,
	FN_SD1_DAT2,	FN_SD1_DAT3,
	FN_SD1_CD,	FN_SD1_WP,
	/* SDHI2 */
	FN_SD2_CMD,	FN_SD2_CLK,
	FN_SD2_DAT0,	FN_SD2_DAT1,
	FN_SD2_DAT2,	FN_SD2_DAT3,
	FN_SD2_DAT4,	FN_SD2_DAT5,
	FN_SD2_DAT6,	FN_SD2_DAT7,
	FN_SD2_DS,
	FN_SD2_CD_A,	FN_SD2_WP_A,
	FN_SD2_CD_B,	FN_SD2_WP_B,
	/* SDHI3 */
	FN_SD3_CMD,	FN_SD3_CLK,
	FN_SD3_DAT0,	FN_SD3_DAT1,
	FN_SD3_DAT2,	FN_SD3_DAT3,
	FN_SD3_DAT4,	FN_SD3_DAT5,
	FN_SD3_DAT6,	FN_SD3_DAT7,
	FN_SD3_DS,
	FN_SD3_CD,	FN_SD3_WP,

	/* USB0 */
	FN_USB0_PWEN,	FN_USB0_OVC,
	/* USB1 */
	FN_USB1_PWEN,	FN_USB1_OVC,
	/* USB2 */
	FN_USB2_PWEN,	FN_USB2_OVC,

	PINMUX_FUNCTION_END,

	PINMUX_MARK_BEGIN,

	/* IPSR0 */		/* IPSR1 */  		/* IPSR2 */		/* IPSR3 */
	IP0_3_0_MARK,		IP1_3_0_MARK,		IP2_3_0_MARK,		IP3_3_0_MARK,
	IP0_7_4_MARK,		IP1_7_4_MARK,		IP2_7_4_MARK,		IP3_7_4_MARK,
	IP0_11_8_MARK,		IP1_11_8_MARK,		IP2_11_8_MARK,		IP3_11_8_MARK,
	IP0_15_12_MARK,		IP1_15_12_MARK,		IP2_15_12_MARK,		IP3_15_12_MARK,
	IP0_19_16_MARK,		IP1_19_16_MARK,		IP2_19_16_MARK,		IP3_19_16_MARK,
	IP0_23_20_MARK,		IP1_23_20_MARK,		IP2_23_20_MARK,		IP3_23_20_MARK,
	IP0_27_24_MARK,		IP1_27_24_MARK,		IP2_27_24_MARK,		IP3_27_24_MARK,
	IP0_31_28_MARK,		IP1_31_28_MARK,		IP2_31_28_MARK,		IP3_31_28_MARK,

	/* IPSR4 */		/* IPSR5 */  		/* IPSR6 */		/* IPSR7 */
	IP4_3_0_MARK,		IP5_3_0_MARK,		IP6_3_0_MARK,		IP7_3_0_MARK,
	IP4_7_4_MARK,		IP5_7_4_MARK,		IP6_7_4_MARK,		IP7_7_4_MARK,
	IP4_11_8_MARK,		IP5_11_8_MARK,		IP6_11_8_MARK,		IP7_11_8_MARK,
	IP4_15_12_MARK,		IP5_15_12_MARK,		IP6_15_12_MARK,		IP7_15_12_MARK,
	IP4_19_16_MARK,		IP5_19_16_MARK,		IP6_19_16_MARK,		IP7_19_16_MARK,
	IP4_23_20_MARK,		IP5_23_20_MARK,		IP6_23_20_MARK,		IP7_23_20_MARK,
	IP4_27_24_MARK,		IP5_27_24_MARK,		IP6_27_24_MARK,		IP7_27_24_MARK,
	IP4_31_28_MARK,		IP5_31_28_MARK,		IP6_31_28_MARK,		IP7_31_28_MARK,

	/* IPSR8 */		/* IPSR9 */		/* IPSR10 */		/* IPSR11 */
	IP8_3_0_MARK,		IP9_3_0_MARK,		IP10_3_0_MARK,		IP11_3_0_MARK,
	IP8_7_4_MARK,		IP9_7_4_MARK,		IP10_7_4_MARK,		IP11_7_4_MARK,
	IP8_11_8_MARK,		IP9_11_8_MARK,		IP10_11_8_MARK,		IP11_11_8_MARK,
	IP8_15_12_MARK,		IP9_15_12_MARK,		IP10_15_12_MARK,	IP11_15_12_MARK,
	IP8_19_16_MARK,		IP9_19_16_MARK,		IP10_19_16_MARK,	IP11_19_16_MARK,
	IP8_23_20_MARK,		IP9_23_20_MARK,		IP10_23_20_MARK,	IP11_23_20_MARK,
	IP8_27_24_MARK,		IP9_27_24_MARK,		IP10_27_24_MARK,	IP11_27_24_MARK,
	IP8_31_28_MARK,		IP9_31_28_MARK,		IP10_31_28_MARK,	IP11_31_28_MARK,

	/* IPSR12 */		/* IPSR13 */		/* IPSR14 */		/* IPSR15 */
	IP12_3_0_MARK,		IP13_3_0_MARK,		IP14_3_0_MARK,		IP15_3_0_MARK,
	IP12_7_4_MARK,		IP13_7_4_MARK,		IP14_7_4_MARK,		IP15_7_4_MARK,
	IP12_11_8_MARK,		IP13_11_8_MARK,		IP14_11_8_MARK,		IP15_11_8_MARK,
	IP12_15_12_MARK,	IP13_15_12_MARK,	IP14_15_12_MARK,	IP15_15_12_MARK,
	IP12_19_16_MARK,	IP13_19_16_MARK,	IP14_19_16_MARK,	IP15_19_16_MARK,
	IP12_23_20_MARK,	IP13_23_20_MARK,	IP14_23_20_MARK,	IP15_23_20_MARK,
	IP12_27_24_MARK,	IP13_27_24_MARK,	IP14_27_24_MARK,	IP15_27_24_MARK,
	IP12_31_28_MARK,	IP13_31_28_MARK,	IP14_31_28_MARK,	IP15_31_28_MARK,

	/* IPSR16 */		/* IPSR17 */
	IP16_3_0_MARK,		IP17_3_0_MARK,
	IP16_7_4_MARK,		IP17_7_4_MARK,
	IP16_11_8_MARK,
	IP16_15_12_MARK,
	IP16_19_16_MARK,
	IP16_23_20_MARK,
	IP16_27_24_MARK,
	IP16_31_28_MARK,

	/* MOD_SEL0 */
	SEL_MSIOF3_0_MARK,	SEL_MSIOF3_1_MARK,	SEL_MSIOF3_2_MARK,	SEL_MSIOF3_3_MARK,
	SEL_MSIOF2_0_MARK,	SEL_MSIOF2_1_MARK,	SEL_MSIOF2_2_MARK,	SEL_MSIOF2_3_MARK,
	SEL_MSIOF1_0_MARK,	SEL_MSIOF1_1_MARK,	SEL_MSIOF1_2_MARK,	SEL_MSIOF1_3_MARK,
	SEL_MSIOF1_4_MARK,	SEL_MSIOF1_5_MARK,	SEL_MSIOF1_6_MARK,	SEL_MSIOF1_7_MARK,
	SEL_LBSC_0_MARK,	SEL_LBSC_1_MARK,
	SEL_IEBUS_0_MARK,	SEL_IEBUS_1_MARK,
	SEL_I2C6_0_MARK,	SEL_I2C6_1_MARK,	SEL_I2C6_2_MARK,	SEL_I2C6_3_MARK,
	SEL_I2C2_0_MARK,	SEL_I2C2_1_MARK,
	SEL_I2C1_0_MARK,	SEL_I2C1_1_MARK,
	SEL_HSCIF4_0_MARK,	SEL_HSCIF4_1_MARK,
	SEL_HSCIF3_0_MARK,	SEL_HSCIF3_1_MARK,	SEL_HSCIF3_2_MARK,	SEL_HSCIF3_3_MARK,
	SEL_HSCIF2_0_MARK,	SEL_HSCIF2_1_MARK,
	SEL_HSCIF1_0_MARK,	SEL_HSCIF1_1_MARK,
	SEL_FSO_0_MARK,		SEL_FSO_1_MARK,
	SEL_FM_0_MARK,		SEL_FM_1_MARK,
	SEL_ETHERAVB_0_MARK,	SEL_ETHERAVB_1_MARK,
	SEL_DRIF3_0_MARK,	SEL_DRIF3_1_MARK,
	SEL_DRIF2_0_MARK,	SEL_DRIF2_1_MARK,
	SEL_DRIF1_0_MARK,	SEL_DRIF1_1_MARK,	SEL_DRIF1_2_MARK,	SEL_DRIF1_3_MARK,
	SEL_DRIF0_0_MARK,	SEL_DRIF0_1_MARK,	SEL_DRIF0_2_MARK,	SEL_DRIF0_3_MARK,
	SEL_CANFD0_0_MARK,	SEL_CANFD0_1_MARK,
	SEL_ADG_0_MARK,		SEL_ADG_1_MARK,		SEL_ADG_2_MARK,		SEL_ADG_3_MARK,

	/* MOD_SEL1 */
	SEL_TSIF1_0_MARK,	SEL_TSIF1_1_MARK,	SEL_TSIF1_2_MARK,	SEL_TSIF1_3_MARK,
	SEL_TSIF0_0_MARK,	SEL_TSIF0_1_MARK,	SEL_TSIF0_2_MARK,	SEL_TSIF0_3_MARK,
	SEL_TSIF0_4_MARK,	SEL_TSIF0_5_MARK,	SEL_TSIF0_6_MARK,	SEL_TSIF0_7_MARK,
	SEL_TIMER_TMU_0_MARK,	SEL_TIMER_TMU_1_MARK,
	SEL_SSP1_1_0_MARK,	SEL_SSP1_1_1_MARK,	SEL_SSP1_1_2_MARK,	SEL_SSP1_1_3_MARK,
	SEL_SSP1_0_0_MARK,	SEL_SSP1_0_1_MARK,	SEL_SSP1_0_2_MARK,	SEL_SSP1_0_3_MARK,
	SEL_SSP1_0_4_MARK,	SEL_SSP1_0_5_MARK,	SEL_SSP1_0_6_MARK,	SEL_SSP1_0_7_MARK,
	SEL_SSI_0_MARK,		SEL_SSI_1_MARK,
	SEL_SPEED_PULSE_0_MARK,	SEL_SPEED_PULSE_1_MARK,
	SEL_SIMCARD_0_MARK,	SEL_SIMCARD_1_MARK,	SEL_SIMCARD_2_MARK,	SEL_SIMCARD_3_MARK,
	SEL_SDHI2_0_MARK,	SEL_SDHI2_1_MARK,
	SEL_SCIF4_0_MARK,	SEL_SCIF4_1_MARK,	SEL_SCIF4_2_MARK,	SEL_SCIF4_3_MARK,
	SEL_SCIF3_0_MARK,	SEL_SCIF3_1_MARK,
	SEL_SCIF2_0_MARK,	SEL_SCIF2_1_MARK,
	SEL_SCIF1_0_MARK,	SEL_SCIF1_1_MARK,
	SEL_SCIF_0_MARK,	SEL_SCIF_1_MARK,
	SEL_REMOCON_0_MARK,	SEL_REMOCON_1_MARK,
	SEL_RCAN0_0_MARK,	SEL_RCAN0_1_MARK,
	SEL_PWM6_0_MARK,	SEL_PWM6_1_MARK,
	SEL_PWM5_0_MARK,	SEL_PWM5_1_MARK,
	SEL_PWM4_0_MARK,	SEL_PWM4_1_MARK,
	SEL_PWM3_0_MARK,	SEL_PWM3_1_MARK,
	SEL_PWM2_0_MARK,	SEL_PWM2_1_MARK,
	SEL_PWM1_0_MARK,	SEL_PWM1_1_MARK,

	/* MOD_SEL2 */
	SEL_I2C_5_0_MARK,	SEL_I2C_5_1_MARK,
	SEL_I2C_3_0_MARK,	SEL_I2C_3_1_MARK,
	SEL_I2C_0_0_MARK,	SEL_I2C_0_1_MARK,
	SEL_VSP_0_MARK,		SEL_VSP_1_MARK,		SEL_VSP_2_MARK,		SEL_VSP_3_MARK,
	SEL_VIN4_0_MARK,	SEL_VIN4_1_MARK,

	/* EthernetAVB */
	AVB_MDC_MARK,		AVB_MAGIC_MARK,		AVB_PHY_INT_MARK,	AVB_LINK_MARK,
	AVB_AVTP_PPS_MARK,
	AVB_AVTP_MATCH_A_MARK,	AVB_AVTP_CAPTURE_A_MARK,
	AVB_AVTP_MATCH_B_MARK,	AVB_AVTP_CAPTURE_B_MARK,

	/* DU */
	DU_DR7_MARK,				DU_DR6_MARK,
	DU_DR5_MARK,				DU_DR4_MARK,
	DU_DR3_MARK,				DU_DR2_MARK,
	DU_DR1_MARK,				DU_DR0_MARK,
	DU_DG7_MARK,				DU_DG6_MARK,
	DU_DG5_MARK,				DU_DG4_MARK,
	DU_DG3_MARK,				DU_DG2_MARK,
	DU_DG1_MARK,				DU_DG0_MARK,
	DU_DB7_MARK,				DU_DB6_MARK,
	DU_DB5_MARK,				DU_DB4_MARK,
	DU_DB3_MARK,				DU_DB2_MARK,
	DU_DB1_MARK,				DU_DB0_MARK,
	DU_DOTCLKOUT0_MARK,			DU_DOTCLKOUT1_MARK,
	DU_DISP_MARK,				DU_CDE_MARK,
	DU_EXVSYNC_DU_VSYNC_MARK,		DU_EXHSYNC_DU_HSYNC_MARK,
	DU_EXODDF_DU_ODDF_DISP_CDE_MARK,

	/* HDMI */
	HDMI0_CEC_MARK,		HDMI1_CEC_MARK,

	/* SCIF0 */
	RX0_MARK,	TX0_MARK,	SCK0_MARK,	RTS0_N_TANS_MARK,	CTS0_N_MARK,
	/* SCIF1 */
	RX1_A_MARK,	TX1_A_MARK,	SCK1_MARK,	RTS1_N_TANS_MARK,	CTS1_N_MARK,
	RX1_B_MARK,	TX1_B_MARK,
	/* SCIF2 */
	RX2_A_MARK,	TX2_A_MARK,	SCK2_MARK,
	RX2_B_MARK,	TX2_B_MARK,
	/* SCIF3 */
	RX3_A_MARK,	TX3_A_MARK,	SCK3_MARK,	RTS3_N_TANS_MARK,	CTS3_N_MARK,
	RX3_B_MARK,	TX3_B_MARK,
	/* SCIF4 */
	RX4_A_MARK,	TX4_A_MARK,	SCK4_A_MARK,	RTS4_N_TANS_A_MARK,	CTS4_N_A_MARK,
	RX4_B_MARK,	TX4_B_MARK,	SCK4_B_MARK,	RTS4_N_TANS_B_MARK,	CTS4_N_B_MARK,
	RX4_C_MARK,	TX4_C_MARK,	SCK4_C_MARK,	RTS4_N_TANS_C_MARK,	CTS4_N_C_MARK,
	/* SCIF5 */
	RX5_MARK,	TX5_MARK,	SCK5_MARK,

	/* SDHI0 */
	SD0_CLK_MARK,	SD0_CMD_MARK,
	SD0_DAT0_MARK,	SD0_DAT1_MARK,
	SD0_DAT2_MARK,	SD0_DAT3_MARK,
	SD0_CD_MARK,	SD0_WP_MARK,
	/* SDHI1 */
	SD1_CLK_MARK,	SD1_CMD_MARK,
	SD1_DAT0_MARK,	SD1_DAT1_MARK,
	SD1_DAT2_MARK,	SD1_DAT3_MARK,
	SD1_CD_MARK,	SD1_WP_MARK,
	/* SDHI2 */
	SD2_CMD_MARK,	SD2_CLK_MARK,
	SD2_DAT0_MARK,	SD2_DAT1_MARK,
	SD2_DAT2_MARK,	SD2_DAT3_MARK,
	SD2_DAT4_MARK,	SD2_DAT5_MARK,
	SD2_DAT6_MARK,	SD2_DAT7_MARK,
	SD2_DS_MARK,
	SD2_CD_A_MARK,	SD2_WP_A_MARK,
	SD2_CD_B_MARK,	SD2_WP_B_MARK,
	/* SDHI3 */
	SD3_CMD_MARK,	SD3_CLK_MARK,
	SD3_DAT0_MARK,	SD3_DAT1_MARK,
	SD3_DAT2_MARK,	SD3_DAT3_MARK,
	SD3_DAT4_MARK,	SD3_DAT5_MARK,
	SD3_DAT6_MARK,	SD3_DAT7_MARK,
	SD3_DS_MARK,
	SD3_CD_MARK,	SD3_WP_MARK,

	/* USB0 */
	USB0_PWEN_MARK,	USB0_OVC_MARK,
	/* USB1 */
	USB1_PWEN_MARK,	USB1_OVC_MARK,
	/* USB2 */
	USB2_PWEN_MARK,	USB2_OVC_MARK,

	PINMUX_MARK_END,
};

static const u16 pinmux_data[] = {
	PINMUX_DATA_GP_ALL(),

	/* NOIP */
	PINMUX_IPSR_NOGP(0, SD2_CMD),	/* GP_4_1  */
	PINMUX_IPSR_NOGP(0, SD3_CLK),	/* GP_4_7  */
	PINMUX_IPSR_NOGP(0, SD3_CMD),	/* GP_4_8  */
	PINMUX_IPSR_NOGP(0, SD3_DAT0),	/* GP_4_9  */
	PINMUX_IPSR_NOGP(0, SD3_DAT1),	/* GP_4_10 */
	PINMUX_IPSR_NOGP(0, SD3_DAT2),	/* GP_4_11 */
	PINMUX_IPSR_NOGP(0, SD3_DAT3),	/* GP_4_12 */
	PINMUX_IPSR_NOGP(0, SD3_DS),	/* GP_4_17 */

	/* IPSR0 */
	PINMUX_IPSR_DATA(IP0_3_0,	AVB_MDC),

	PINMUX_IPSR_DATA(IP0_7_4,	AVB_MAGIC),
	PINMUX_IPSR_MODS(IP0_7_4,	SCK4_A,			SEL_SCIF4_0),

	PINMUX_IPSR_DATA(IP0_11_8,	AVB_PHY_INT),
	PINMUX_IPSR_MODS(IP0_11_8,	RX4_A,			SEL_SCIF4_0),

	PINMUX_IPSR_DATA(IP0_15_12,	AVB_LINK),
	PINMUX_IPSR_MODS(IP0_15_12,	TX4_A,			SEL_SCIF4_0),

	PINMUX_IPSR_MODS(IP0_19_16,	AVB_AVTP_MATCH_A,	SEL_ETHERAVB_0),
	PINMUX_IPSR_MODS(IP0_19_16,	CTS4_N_A,		SEL_SCIF4_0),

	PINMUX_IPSR_MODS(IP0_23_20,	AVB_AVTP_CAPTURE_A,	SEL_ETHERAVB_0),
	PINMUX_IPSR_MODS(IP0_23_20,	RTS4_N_TANS_A,		SEL_SCIF4_0),

	PINMUX_IPSR_DATA(IP0_27_24,	DU_CDE),

	PINMUX_IPSR_DATA(IP0_31_28,	DU_DISP),

	/* IPSR1 */
	PINMUX_IPSR_DATA(IP1_3_0,	DU_EXODDF_DU_ODDF_DISP_CDE),

	PINMUX_IPSR_DATA(IP1_7_4,	DU_DOTCLKOUT1),

	PINMUX_IPSR_DATA(IP1_19_16,	AVB_AVTP_PPS),

	PINMUX_IPSR_DATA(IP1_11_8,	DU_EXHSYNC_DU_HSYNC),

	PINMUX_IPSR_DATA(IP1_15_12,	DU_EXVSYNC_DU_VSYNC),

	PINMUX_IPSR_DATA(IP1_31_28,	DU_DB0),

	/* IPSR2 */
	PINMUX_IPSR_DATA(IP2_3_0,	DU_DB1),

	PINMUX_IPSR_DATA(IP2_7_4,	DU_DB2),

	PINMUX_IPSR_DATA(IP2_11_8,	DU_DB3),

	PINMUX_IPSR_DATA(IP2_15_12,	DU_DB4),

	PINMUX_IPSR_MODS(IP2_19_16,	SCK4_B,			SEL_SCIF4_1),
	PINMUX_IPSR_DATA(IP2_19_16,	DU_DB5),

	PINMUX_IPSR_MODS(IP2_23_20,	RX4_B,			SEL_SCIF4_1),
	PINMUX_IPSR_DATA(IP2_23_20,	DU_DB6),

	PINMUX_IPSR_MODS(IP2_27_24,	TX4_B,			SEL_SCIF4_1),
	PINMUX_IPSR_DATA(IP2_27_24,	DU_DB7),

	PINMUX_IPSR_MODS(IP2_31_28,	RX3_B,			SEL_SCIF3_1),
	PINMUX_IPSR_MODS(IP2_31_28,	AVB_AVTP_MATCH_B,	SEL_ETHERAVB_1),

	/* IPSR3 */
	PINMUX_IPSR_MODS(IP3_3_0,	CTS4_N_B,		SEL_SCIF4_1),

	PINMUX_IPSR_MODS(IP3_7_4,	RTS4_N_TANS_B,		SEL_SCIF4_1),

	PINMUX_IPSR_MODS(IP3_11_8,	TX3_B,			SEL_SCIF3_1),
	PINMUX_IPSR_MODS(IP3_11_8,	AVB_AVTP_CAPTURE_B,	SEL_ETHERAVB_1),

	PINMUX_IPSR_DATA(IP3_15_12,	DU_DG4),

	PINMUX_IPSR_DATA(IP3_19_16,	DU_DG5),

	PINMUX_IPSR_DATA(IP3_23_20,	DU_DG6),

	PINMUX_IPSR_DATA(IP3_27_24,	DU_DG7),

	PINMUX_IPSR_DATA(IP3_31_28,	DU_DG0),

	/* IPSR4 */
	PINMUX_IPSR_DATA(IP4_3_0,	DU_DG1),

	PINMUX_IPSR_DATA(IP4_7_4,	DU_DG2),

	PINMUX_IPSR_DATA(IP4_11_8,	DU_DG3),

	PINMUX_IPSR_DATA(IP4_23_20,	SCK3),

	PINMUX_IPSR_MODS(IP4_27_24,	RX3_A,		SEL_SCIF3_0),

	PINMUX_IPSR_MODS(IP4_31_28,	TX3_A,		SEL_SCIF3_0),

	/* IPSR5 */
	PINMUX_IPSR_DATA(IP5_3_0,	CTS3_N),

	PINMUX_IPSR_DATA(IP5_7_4,	RTS3_N_TANS),

	PINMUX_IPSR_DATA(IP5_11_8,	DU_DOTCLKOUT0),

	/* IPSR6 */
	PINMUX_IPSR_MODS(IP6_15_12,	SCK4_C,		SEL_SCIF4_2),
	PINMUX_IPSR_DATA(IP6_15_12,	DU_DR0),

	PINMUX_IPSR_DATA(IP6_19_16,	DU_DR1),

	PINMUX_IPSR_MODS(IP6_23_20,	CTS4_N_C,	SEL_SCIF4_2),
	PINMUX_IPSR_DATA(IP6_23_20,	DU_DR2),

	PINMUX_IPSR_MODS(IP6_27_24,	RTS4_N_TANS_C,	SEL_SCIF4_2),
	PINMUX_IPSR_DATA(IP6_27_24,	DU_DR3),

	PINMUX_IPSR_MODS(IP6_31_28,	RX4_C,		SEL_SCIF4_2),
	PINMUX_IPSR_DATA(IP6_31_28,	DU_DR4),

	/* IPSR7 */
	PINMUX_IPSR_MODS(IP7_3_0,	TX4_C,		SEL_SCIF4_2),
	PINMUX_IPSR_DATA(IP7_3_0,	DU_DR5),

	PINMUX_IPSR_DATA(IP7_7_4,	DU_DR6),

	PINMUX_IPSR_DATA(IP7_11_8,	DU_DR7),

	PINMUX_IPSR_DATA(IP7_19_16,	SD0_CLK),

	PINMUX_IPSR_DATA(IP7_23_20,	SD0_CMD),

	PINMUX_IPSR_DATA(IP7_27_24,	SD0_DAT0),

	PINMUX_IPSR_DATA(IP7_31_28,	SD0_DAT1),

	/* IPSR8 */
	PINMUX_IPSR_DATA(IP8_3_0,	SD0_DAT2),

	PINMUX_IPSR_DATA(IP8_7_4,	SD0_DAT3),

	PINMUX_IPSR_DATA(IP8_11_8,	SD1_CLK),

	PINMUX_IPSR_DATA(IP8_15_12,	SD1_CMD),

	PINMUX_IPSR_DATA(IP8_19_16,	SD1_DAT0),
	PINMUX_IPSR_DATA(IP8_19_16,	SD2_DAT4),

	PINMUX_IPSR_DATA(IP8_23_20,	SD1_DAT1),
	PINMUX_IPSR_DATA(IP8_23_20,	SD2_DAT5),

	PINMUX_IPSR_DATA(IP8_27_24,	SD1_DAT2),
	PINMUX_IPSR_DATA(IP8_27_24,	SD2_DAT6),

	PINMUX_IPSR_DATA(IP8_31_28,	SD1_DAT3),
	PINMUX_IPSR_DATA(IP8_31_28,	SD2_DAT7),

	/* IPSR9 */
	PINMUX_IPSR_DATA(IP9_7_4,	SD2_DAT0),

	PINMUX_IPSR_DATA(IP9_11_8,	SD2_DAT1),

	PINMUX_IPSR_DATA(IP9_15_12,	SD2_DAT2),

	PINMUX_IPSR_DATA(IP9_19_16,	SD2_DAT3),

	PINMUX_IPSR_DATA(IP9_23_20,	SD2_DS),

	PINMUX_IPSR_DATA(IP9_27_24,	SD3_DAT4),
	PINMUX_IPSR_MODS(IP9_27_24,	SD2_CD_A,	SEL_SDHI2_0),

	PINMUX_IPSR_DATA(IP9_31_28,	SD3_DAT5),
	PINMUX_IPSR_MODS(IP9_31_28,	SD2_WP_A,	SEL_SDHI2_0),

	/* IPSR10 */
	PINMUX_IPSR_DATA(IP10_3_0,	SD3_DAT6),
	PINMUX_IPSR_DATA(IP10_3_0,	SD3_CD),

	PINMUX_IPSR_DATA(IP10_7_4,	SD3_DAT7),
	PINMUX_IPSR_DATA(IP10_7_4,	SD3_WP),

	PINMUX_IPSR_DATA(IP10_11_8,	SD0_CD),

	PINMUX_IPSR_DATA(IP10_15_12,	SD0_WP),

	PINMUX_IPSR_DATA(IP10_19_16,	SD1_CD),

	PINMUX_IPSR_DATA(IP10_23_20,	SD1_WP),

	PINMUX_IPSR_DATA(IP10_27_24,	SCK0),

	PINMUX_IPSR_DATA(IP10_31_28,	RX0),

	/* IPSR11 */
	PINMUX_IPSR_DATA(IP11_3_0,	TX0),

	PINMUX_IPSR_DATA(IP11_7_4,	CTS0_N),

	PINMUX_IPSR_DATA(IP11_11_8,	RTS0_N_TANS),

	PINMUX_IPSR_MODS(IP11_15_12,	RX1_A,		SEL_SCIF1_0),

	PINMUX_IPSR_MODS(IP11_19_16,	TX1_A,		SEL_SCIF1_0),

	PINMUX_IPSR_DATA(IP11_23_20,	CTS1_N),

	PINMUX_IPSR_DATA(IP11_27_24,	RTS1_N_TANS),

	PINMUX_IPSR_DATA(IP11_31_28,	SCK2),

	/* IPSR12 */
	PINMUX_IPSR_MODS(IP12_3_0,	TX2_A,		SEL_SCIF2_0),
	PINMUX_IPSR_MODS(IP12_3_0,	SD2_CD_B,	SEL_SDHI2_1),

	PINMUX_IPSR_MODS(IP12_7_4,	RX2_A,		SEL_SCIF2_0),
	PINMUX_IPSR_MODS(IP12_7_4,	SD2_WP_B,	SEL_SDHI2_1),

	PINMUX_IPSR_MODS(IP12_23_20,	RX2_B,		SEL_SCIF2_1),

	PINMUX_IPSR_MODS(IP12_27_24,	TX2_B,		SEL_SCIF2_1),

	/* IPSR13 */
	PINMUX_IPSR_DATA(IP13_3_0,	RX5),

	PINMUX_IPSR_DATA(IP13_7_4,	TX5),

	PINMUX_IPSR_MODS(IP13_15_12,	RX1_B,		SEL_SCIF1_1),

	PINMUX_IPSR_MODS(IP13_19_16,	TX1_B,		SEL_SCIF1_1),

	/* IPSR14 */

	/* IPSR15 */
	PINMUX_IPSR_DATA(IP15_3_0,	USB2_PWEN),

	PINMUX_IPSR_DATA(IP15_7_4,	USB2_OVC),

	PINMUX_IPSR_DATA(IP15_31_28,	SCK1),
	PINMUX_IPSR_DATA(IP15_31_28,	SCK5),

	/* IPSR16 */
	PINMUX_IPSR_DATA(IP16_11_8,	USB0_PWEN),

	PINMUX_IPSR_DATA(IP16_15_12,	USB0_OVC),

	PINMUX_IPSR_DATA(IP16_19_16,	USB1_PWEN),

	PINMUX_IPSR_DATA(IP16_23_20,	USB1_OVC),

	/* IPSR17 */
};

static const struct sh_pfc_pin pinmux_pins[] = {
	PINMUX_GPIO_GP_ALL(),
};

/* - EtherAVB --------------------------------------------------------------- */
static const unsigned int avb_link_pins[] = {
	/* AVB_LINK */
	RCAR_GP_PIN(2, 12),
};
static const unsigned int avb_link_mux[] = {
	AVB_LINK_MARK,
};
static const unsigned int avb_magic_pins[] = {
	/* AVB_MAGIC_ */
	RCAR_GP_PIN(2, 10),
};
static const unsigned int avb_magic_mux[] = {
	AVB_MAGIC_MARK,
};
static const unsigned int avb_phy_int_pins[] = {
	/* AVB_PHY_INT */
	RCAR_GP_PIN(2, 11),
};
static const unsigned int avb_phy_int_mux[] = {
	AVB_PHY_INT_MARK,
};
static const unsigned int avb_mdc_pins[] = {
	/* AVB_MDC */
	RCAR_GP_PIN(2, 9),
};
static const unsigned int avb_mdc_mux[] = {
	AVB_MDC_MARK,
};
static const unsigned int avb_avtp_pps_pins[] = {
	/* AVB_AVTP_PPS */
	RCAR_GP_PIN(2, 6),
};
static const unsigned int avb_avtp_pps_mux[] = {
	AVB_AVTP_PPS_MARK,
};
static const unsigned int avb_avtp_match_a_pins[] = {
	/* AVB_AVTP_MATCH_A */
	RCAR_GP_PIN(2, 14),
};
static const unsigned int avb_avtp_match_a_mux[] = {
	AVB_AVTP_MATCH_A_MARK,
};
static const unsigned int avb_avtp_capture_a_pins[] = {
	/* AVB_AVTP_CAPTURE_A */
	RCAR_GP_PIN(2, 13),
};
static const unsigned int avb_avtp_capture_a_mux[] = {
	AVB_AVTP_CAPTURE_A_MARK,
};
static const unsigned int avb_avtp_match_b_pins[] = {
	/*  AVB_AVTP_MATCH_B */
	RCAR_GP_PIN(1, 8),
};
static const unsigned int avb_avtp_match_b_mux[] = {
	AVB_AVTP_MATCH_B_MARK,
};
static const unsigned int avb_avtp_capture_b_pins[] = {
	/* AVB_AVTP_CAPTURE_B */
	RCAR_GP_PIN(1, 11),
};
static const unsigned int avb_avtp_capture_b_mux[] = {
	AVB_AVTP_CAPTURE_B_MARK,
};
/* - DU --------------------------------------------------------------------- */
static const unsigned int du_rgb888_pins[] = {
	/* R[7:0] */
	RCAR_GP_PIN(0, 15), RCAR_GP_PIN(0, 14), RCAR_GP_PIN(0, 13),
	RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 11), RCAR_GP_PIN(0, 10),
	RCAR_GP_PIN(0, 9),  RCAR_GP_PIN(3, 8),

	/* G[7:0] */
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 14), RCAR_GP_PIN(1, 13),
	RCAR_GP_PIN(1, 12), RCAR_GP_PIN(1, 19), RCAR_GP_PIN(1, 18),
	RCAR_GP_PIN(1, 17), RCAR_GP_PIN(1, 16),

	/* B[7:0] */
	RCAR_GP_PIN(1, 7),  RCAR_GP_PIN(1, 6),  RCAR_GP_PIN(1, 5),
	RCAR_GP_PIN(1, 4),  RCAR_GP_PIN(1, 3),  RCAR_GP_PIN(1, 2),
	RCAR_GP_PIN(1, 1),  RCAR_GP_PIN(1, 0),
};
static const unsigned int du_rgb888_mux[] = {
	DU_DR7_MARK, DU_DR6_MARK, DU_DR5_MARK, DU_DR4_MARK,
	DU_DR3_MARK, DU_DR2_MARK, DU_DR1_MARK, DU_DR0_MARK,
	DU_DG7_MARK, DU_DG6_MARK, DU_DG5_MARK, DU_DG4_MARK,
	DU_DG3_MARK, DU_DG2_MARK, DU_DG1_MARK, DU_DG0_MARK,
	DU_DB7_MARK, DU_DB6_MARK, DU_DB5_MARK, DU_DB4_MARK,
	DU_DB3_MARK, DU_DB2_MARK, DU_DB1_MARK, DU_DB0_MARK,
};
static const unsigned int du_clk_out_0_pins[] = {
	/* CLKOUT */
	RCAR_GP_PIN(1, 27),
};
static const unsigned int du_clk_out_0_mux[] = {
	DU_DOTCLKOUT0_MARK
};
static const unsigned int du_clk_out_1_pins[] = {
	/* CLKOUT */
	RCAR_GP_PIN(2, 3),
};
static const unsigned int du_clk_out_1_mux[] = {
	DU_DOTCLKOUT1_MARK
};
static const unsigned int du_sync_pins[] = {
	/* EXVSYNC/VSYNC, EXHSYNC/HSYNC */
	RCAR_GP_PIN(2, 5), RCAR_GP_PIN(2, 4),
};
static const unsigned int du_sync_mux[] = {
	DU_EXVSYNC_DU_VSYNC_MARK, DU_EXHSYNC_DU_HSYNC_MARK
};
static const unsigned int du_oddf_pins[] = {
	/* EXDISP/EXODDF/EXCDE */
	RCAR_GP_PIN(2, 2),
};
static const unsigned int du_oddf_mux[] = {
	DU_EXODDF_DU_ODDF_DISP_CDE_MARK,
};
static const unsigned int du_cde_pins[] = {
	/* CDE */
	RCAR_GP_PIN(2, 0),
};
static const unsigned int du_cde_mux[] = {
	DU_CDE_MARK,
};
static const unsigned int du_disp_pins[] = {
	/* DISP */
	RCAR_GP_PIN(2, 1),
};
static const unsigned int du_disp_mux[] = {
	DU_DISP_MARK,
};
/* - HDMI ------------------------------------------------------------------- */
static const unsigned int hdmi0_cec_pins[] = {
	/* HDMI0_CEC */
	RCAR_GP_PIN(7, 2),
};
static const unsigned int hdmi0_cec_mux[] = {
	HDMI0_CEC_MARK,
};
static const unsigned int hdmi1_cec_pins[] = {
	/* HDMI0_CEC */
	RCAR_GP_PIN(7, 3),
};
static const unsigned int hdmi1_cec_mux[] = {
	HDMI1_CEC_MARK,
};
/* - SCIF0 ------------------------------------------------------------------ */
static const unsigned int scif0_data_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 1), RCAR_GP_PIN(5, 2),
};
static const unsigned int scif0_data_mux[] = {
	RX0_MARK, TX0_MARK,
};
static const unsigned int scif0_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 0),
};
static const unsigned int scif0_clk_mux[] = {
	SCK0_MARK,
};
static const unsigned int scif0_ctrl_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(5, 4), RCAR_GP_PIN(5, 3),
};
static const unsigned int scif0_ctrl_mux[] = {
	RTS0_N_TANS_MARK, CTS0_N_MARK,
};
/* - SCIF1 ------------------------------------------------------------------ */
static const unsigned int scif1_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 5), RCAR_GP_PIN(5, 6),
};
static const unsigned int scif1_data_a_mux[] = {
	RX1_A_MARK, TX1_A_MARK,
};
static const unsigned int scif1_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 21),
};
static const unsigned int scif1_clk_mux[] = {
	SCK1_MARK,
};
static const unsigned int scif1_ctrl_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(5, 8), RCAR_GP_PIN(5, 7),
};
static const unsigned int scif1_ctrl_mux[] = {
	RTS1_N_TANS_MARK, CTS1_N_MARK,
};

static const unsigned int scif1_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 24), RCAR_GP_PIN(5, 25),
};
static const unsigned int scif1_data_b_mux[] = {
	RX1_B_MARK, TX1_B_MARK,
};
/* - SCIF2 ------------------------------------------------------------------ */
static const unsigned int scif2_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 11), RCAR_GP_PIN(5, 10),
};
static const unsigned int scif2_data_a_mux[] = {
	RX2_A_MARK, TX2_A_MARK,
};
static const unsigned int scif2_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(5, 9),
};
static const unsigned int scif2_clk_mux[] = {
	SCK2_MARK,
};
static const unsigned int scif2_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 15), RCAR_GP_PIN(5, 16),
};
static const unsigned int scif2_data_b_mux[] = {
	RX2_B_MARK, TX2_B_MARK,
};
/* - SCIF3 ------------------------------------------------------------------ */
static const unsigned int scif3_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 23), RCAR_GP_PIN(1, 24),
};
static const unsigned int scif3_data_a_mux[] = {
	RX3_A_MARK, TX3_A_MARK,
};
static const unsigned int scif3_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 22),
};
static const unsigned int scif3_clk_mux[] = {
	SCK3_MARK,
};
static const unsigned int scif3_ctrl_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(1, 26), RCAR_GP_PIN(1, 25),
};
static const unsigned int scif3_ctrl_mux[] = {
	RTS3_N_TANS_MARK, CTS3_N_MARK,
};
static const unsigned int scif3_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 8), RCAR_GP_PIN(1, 11),
};
static const unsigned int scif3_data_b_mux[] = {
	RX3_B_MARK, TX3_B_MARK,
};
/* - SCIF4 ------------------------------------------------------------------ */
static const unsigned int scif4_data_a_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(2, 11), RCAR_GP_PIN(2, 12),
};
static const unsigned int scif4_data_a_mux[] = {
	RX4_A_MARK, TX4_A_MARK,
};
static const unsigned int scif4_clk_a_pins[] = {
	/* SCK */
	RCAR_GP_PIN(2, 10),
};
static const unsigned int scif4_clk_a_mux[] = {
	SCK4_A_MARK,
};
static const unsigned int scif4_ctrl_a_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(2, 14), RCAR_GP_PIN(2, 13),
};
static const unsigned int scif4_ctrl_a_mux[] = {
	RTS4_N_TANS_A_MARK, CTS4_N_A_MARK,
};
static const unsigned int scif4_data_b_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(1, 6), RCAR_GP_PIN(1, 7),
};
static const unsigned int scif4_data_b_mux[] = {
	RX4_B_MARK, TX4_B_MARK,
};
static const unsigned int scif4_clk_b_pins[] = {
	/* SCK */
	RCAR_GP_PIN(1, 5),
};
static const unsigned int scif4_clk_b_mux[] = {
	SCK4_B_MARK,
};
static const unsigned int scif4_ctrl_b_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(1, 10), RCAR_GP_PIN(1, 9),
};
static const unsigned int scif4_ctrl_b_mux[] = {
	RTS4_N_TANS_B_MARK, CTS4_N_B_MARK,
};
static const unsigned int scif4_data_c_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 13),
};
static const unsigned int scif4_data_c_mux[] = {
	RX4_C_MARK, TX4_C_MARK,
};
static const unsigned int scif4_clk_c_pins[] = {
	/* SCK */
	RCAR_GP_PIN(0, 8),
};
static const unsigned int scif4_clk_c_mux[] = {
	SCK4_C_MARK,
};
static const unsigned int scif4_ctrl_c_pins[] = {
	/* RTS, CTS */
	RCAR_GP_PIN(0, 11), RCAR_GP_PIN(0, 10),
};
static const unsigned int scif4_ctrl_c_mux[] = {
	RTS4_N_TANS_C_MARK, CTS4_N_C_MARK,
};
/* - SCIF5 ------------------------------------------------------------------ */
static const unsigned int scif5_data_pins[] = {
	/* RX, TX */
	RCAR_GP_PIN(5, 19), RCAR_GP_PIN(5, 21),
};
static const unsigned int scif5_data_mux[] = {
	RX5_MARK, TX5_MARK,
};
static const unsigned int scif5_clk_pins[] = {
	/* SCK */
	RCAR_GP_PIN(6, 21),
};
static const unsigned int scif5_clk_mux[] = {
	SCK5_MARK,
};
/* - SDHI0 ------------------------------------------------------------------ */
static const unsigned int sdhi0_data1_pins[] = {
	/* D0 */
	RCAR_GP_PIN(3, 2),
};
static const unsigned int sdhi0_data1_mux[] = {
	SD0_DAT0_MARK,
};
static const unsigned int sdhi0_data4_pins[] = {
	/* D[0:3] */
	RCAR_GP_PIN(3, 2), RCAR_GP_PIN(3, 3),
	RCAR_GP_PIN(3, 4), RCAR_GP_PIN(3, 5),
};
static const unsigned int sdhi0_data4_mux[] = {
	SD0_DAT0_MARK, SD0_DAT1_MARK,
	SD0_DAT2_MARK, SD0_DAT3_MARK,
};
static const unsigned int sdhi0_ctrl_pins[] = {
	/* CLK, CMD */
	RCAR_GP_PIN(3, 0), RCAR_GP_PIN(3, 1),
};
static const unsigned int sdhi0_ctrl_mux[] = {
	SD0_CLK_MARK, SD0_CMD_MARK,
};
static const unsigned int sdhi0_cd_pins[] = {
	/* CD */
	RCAR_GP_PIN(3, 12),
};
static const unsigned int sdhi0_cd_mux[] = {
	SD0_CD_MARK,
};
static const unsigned int sdhi0_wp_pins[] = {
	/* WP */
	RCAR_GP_PIN(3, 13),
};
static const unsigned int sdhi0_wp_mux[] = {
	SD0_WP_MARK,
};
/* - SDHI1 ------------------------------------------------------------------ */
static const unsigned int sdhi1_data1_pins[] = {
	/* D0 */
	RCAR_GP_PIN(3, 8),
};
static const unsigned int sdhi1_data1_mux[] = {
	SD1_DAT0_MARK,
};
static const unsigned int sdhi1_data4_pins[] = {
	/* D[0:3] */
	RCAR_GP_PIN(3, 8),  RCAR_GP_PIN(3, 9),
	RCAR_GP_PIN(3, 10), RCAR_GP_PIN(3, 11),
};
static const unsigned int sdhi1_data4_mux[] = {
	SD1_DAT0_MARK, SD1_DAT1_MARK,
	SD1_DAT2_MARK, SD1_DAT3_MARK,
};
static const unsigned int sdhi1_ctrl_pins[] = {
	/* CLK, CMD */
	RCAR_GP_PIN(3, 6), RCAR_GP_PIN(3, 7),
};
static const unsigned int sdhi1_ctrl_mux[] = {
	SD1_CLK_MARK, SD1_CMD_MARK,
};
static const unsigned int sdhi1_cd_pins[] = {
	/* CD */
	RCAR_GP_PIN(3, 14),
};
static const unsigned int sdhi1_cd_mux[] = {
	SD1_CD_MARK,
};
static const unsigned int sdhi1_wp_pins[] = {
	/* WP */
	RCAR_GP_PIN(3, 15),
};
static const unsigned int sdhi1_wp_mux[] = {
	SD1_WP_MARK,
};
/* - SDHI2 ------------------------------------------------------------------ */
static const unsigned int sdhi2_data1_pins[] = {
	/* D0 */
	RCAR_GP_PIN(4, 2),
};
static const unsigned int sdhi2_data1_mux[] = {
	SD2_DAT0_MARK,
};
static const unsigned int sdhi2_data4_pins[] = {
	/* D[0:3] */
	RCAR_GP_PIN(4, 2), RCAR_GP_PIN(4, 3),
	RCAR_GP_PIN(4, 4), RCAR_GP_PIN(4, 5),
};
static const unsigned int sdhi2_data4_mux[] = {
	SD2_DAT0_MARK, SD2_DAT1_MARK,
	SD2_DAT2_MARK, SD2_DAT3_MARK,
};
static const unsigned int sdhi2_data8_pins[] = {
	/* D[0:7] */
	RCAR_GP_PIN(4, 2),  RCAR_GP_PIN(4, 3),
	RCAR_GP_PIN(4, 4),  RCAR_GP_PIN(4, 5),
	RCAR_GP_PIN(3, 8),  RCAR_GP_PIN(3, 9),
	RCAR_GP_PIN(3, 10), RCAR_GP_PIN(3, 11),
};
static const unsigned int sdhi2_data8_mux[] = {
	SD2_DAT0_MARK, SD2_DAT1_MARK,
	SD2_DAT2_MARK, SD2_DAT3_MARK,
	SD2_DAT4_MARK, SD2_DAT5_MARK,
	SD2_DAT6_MARK, SD2_DAT7_MARK,
};
static const unsigned int sdhi2_ctrl_pins[] = {
	/* CLK, CMD */
	RCAR_GP_PIN(4, 0), RCAR_GP_PIN(4, 1),
};
static const unsigned int sdhi2_ctrl_mux[] = {
	SD2_CLK_MARK, SD2_CMD_MARK,
};
static const unsigned int sdhi2_cd_a_pins[] = {
	/* CD */
	RCAR_GP_PIN(4, 13),
};
static const unsigned int sdhi2_cd_a_mux[] = {
	SD2_CD_A_MARK,
};
static const unsigned int sdhi2_cd_b_pins[] = {
	/* CD */
	RCAR_GP_PIN(5, 10),
};
static const unsigned int sdhi2_cd_b_mux[] = {
	SD2_CD_B_MARK,
};
static const unsigned int sdhi2_wp_a_pins[] = {
	/* WP */
	RCAR_GP_PIN(4, 14),
};
static const unsigned int sdhi2_wp_a_mux[] = {
	SD2_WP_A_MARK,
};
static const unsigned int sdhi2_wp_b_pins[] = {
	/* WP */
	RCAR_GP_PIN(5, 11),
};
static const unsigned int sdhi2_wp_b_mux[] = {
	SD2_WP_B_MARK,
};
static const unsigned int sdhi2_ds_pins[] = {
	/* DS */
	RCAR_GP_PIN(4, 6),
};
static const unsigned int sdhi2_ds_mux[] = {
	SD2_DS_MARK,
};
/* - SDHI3 ------------------------------------------------------------------ */
static const unsigned int sdhi3_data1_pins[] = {
	/* D0 */
	RCAR_GP_PIN(4, 9),
};
static const unsigned int sdhi3_data1_mux[] = {
	SD3_DAT0_MARK,
};
static const unsigned int sdhi3_data4_pins[] = {
	/* D[0:3] */
	RCAR_GP_PIN(4, 9),  RCAR_GP_PIN(4, 10),
	RCAR_GP_PIN(4, 11), RCAR_GP_PIN(4, 12),
};
static const unsigned int sdhi3_data4_mux[] = {
	SD3_DAT0_MARK, SD3_DAT1_MARK,
	SD3_DAT2_MARK, SD3_DAT3_MARK,
};
static const unsigned int sdhi3_data8_pins[] = {
	/* D[0:7] */
	RCAR_GP_PIN(4, 9),  RCAR_GP_PIN(4, 10),
	RCAR_GP_PIN(4, 11), RCAR_GP_PIN(4, 12),
	RCAR_GP_PIN(4, 13), RCAR_GP_PIN(4, 14),
	RCAR_GP_PIN(4, 15), RCAR_GP_PIN(4, 16),
};
static const unsigned int sdhi3_data8_mux[] = {
	SD3_DAT0_MARK, SD3_DAT1_MARK,
	SD3_DAT2_MARK, SD3_DAT3_MARK,
	SD3_DAT4_MARK, SD3_DAT5_MARK,
	SD3_DAT6_MARK, SD3_DAT7_MARK,
};
static const unsigned int sdhi3_ctrl_pins[] = {
	/* CLK, CMD */
	RCAR_GP_PIN(4, 7), RCAR_GP_PIN(4, 8),
};
static const unsigned int sdhi3_ctrl_mux[] = {
	SD3_CLK_MARK, SD3_CMD_MARK,
};
static const unsigned int sdhi3_cd_pins[] = {
	/* CD */
	RCAR_GP_PIN(4, 15),
};
static const unsigned int sdhi3_cd_mux[] = {
	SD3_CD_MARK,
};
static const unsigned int sdhi3_wp_pins[] = {
	/* WP */
	RCAR_GP_PIN(4, 16),
};
static const unsigned int sdhi3_wp_mux[] = {
	SD3_WP_MARK,
};
static const unsigned int sdhi3_ds_pins[] = {
	/* DS */
	RCAR_GP_PIN(4, 17),
};
static const unsigned int sdhi3_ds_mux[] = {
	SD3_DS_MARK,
};
/* - USB0 ------------------------------------------------------------------- */
static const unsigned int usb0_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 24), RCAR_GP_PIN(6, 25),
};
static const unsigned int usb0_mux[] = {
	USB0_PWEN_MARK, USB0_OVC_MARK,
};
/* - USB1 ------------------------------------------------------------------- */
static const unsigned int usb1_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 26), RCAR_GP_PIN(6, 27),
};
static const unsigned int usb1_mux[] = {
	USB1_PWEN_MARK, USB1_OVC_MARK,
};
/* - USB2 ------------------------------------------------------------------- */
static const unsigned int usb2_pins[] = {
	/* PWEN, OVC */
	RCAR_GP_PIN(6, 14), RCAR_GP_PIN(6, 15),
};
static const unsigned int usb2_mux[] = {
	USB2_PWEN_MARK, USB2_OVC_MARK,
};

static const struct sh_pfc_pin_group pinmux_groups[] = {
	SH_PFC_PIN_GROUP(avb_link),
	SH_PFC_PIN_GROUP(avb_magic),
	SH_PFC_PIN_GROUP(avb_phy_int),
	SH_PFC_PIN_GROUP(avb_mdc),
	SH_PFC_PIN_GROUP(avb_avtp_pps),
	SH_PFC_PIN_GROUP(avb_avtp_match_a),
	SH_PFC_PIN_GROUP(avb_avtp_capture_a),
	SH_PFC_PIN_GROUP(avb_avtp_match_b),
	SH_PFC_PIN_GROUP(avb_avtp_capture_b),
	SH_PFC_PIN_GROUP(du_rgb888),
	SH_PFC_PIN_GROUP(du_clk_out_0),
	SH_PFC_PIN_GROUP(du_clk_out_1),
	SH_PFC_PIN_GROUP(du_sync),
	SH_PFC_PIN_GROUP(du_oddf),
	SH_PFC_PIN_GROUP(du_cde),
	SH_PFC_PIN_GROUP(du_disp),
	SH_PFC_PIN_GROUP(hdmi0_cec),
	SH_PFC_PIN_GROUP(hdmi1_cec),
	SH_PFC_PIN_GROUP(scif0_data),
	SH_PFC_PIN_GROUP(scif0_clk),
	SH_PFC_PIN_GROUP(scif0_ctrl),
	SH_PFC_PIN_GROUP(scif1_data_a),
	SH_PFC_PIN_GROUP(scif1_clk),
	SH_PFC_PIN_GROUP(scif1_ctrl),
	SH_PFC_PIN_GROUP(scif1_data_b),
	SH_PFC_PIN_GROUP(scif2_data_a),
	SH_PFC_PIN_GROUP(scif2_clk),
	SH_PFC_PIN_GROUP(scif2_data_b),
	SH_PFC_PIN_GROUP(scif3_data_a),
	SH_PFC_PIN_GROUP(scif3_clk),
	SH_PFC_PIN_GROUP(scif3_ctrl),
	SH_PFC_PIN_GROUP(scif3_data_b),
	SH_PFC_PIN_GROUP(scif4_data_a),
	SH_PFC_PIN_GROUP(scif4_clk_a),
	SH_PFC_PIN_GROUP(scif4_ctrl_a),
	SH_PFC_PIN_GROUP(scif4_data_b),
	SH_PFC_PIN_GROUP(scif4_clk_b),
	SH_PFC_PIN_GROUP(scif4_ctrl_b),
	SH_PFC_PIN_GROUP(scif4_data_c),
	SH_PFC_PIN_GROUP(scif4_clk_c),
	SH_PFC_PIN_GROUP(scif4_ctrl_c),
	SH_PFC_PIN_GROUP(scif5_data),
	SH_PFC_PIN_GROUP(scif5_clk),
	SH_PFC_PIN_GROUP(sdhi0_data1),
	SH_PFC_PIN_GROUP(sdhi0_data4),
	SH_PFC_PIN_GROUP(sdhi0_ctrl),
	SH_PFC_PIN_GROUP(sdhi0_cd),
	SH_PFC_PIN_GROUP(sdhi0_wp),
	SH_PFC_PIN_GROUP(sdhi1_data1),
	SH_PFC_PIN_GROUP(sdhi1_data4),
	SH_PFC_PIN_GROUP(sdhi1_ctrl),
	SH_PFC_PIN_GROUP(sdhi1_cd),
	SH_PFC_PIN_GROUP(sdhi1_wp),
	SH_PFC_PIN_GROUP(sdhi2_data1),
	SH_PFC_PIN_GROUP(sdhi2_data4),
	SH_PFC_PIN_GROUP(sdhi2_data8),
	SH_PFC_PIN_GROUP(sdhi2_ctrl),
	SH_PFC_PIN_GROUP(sdhi2_cd_a),
	SH_PFC_PIN_GROUP(sdhi2_wp_a),
	SH_PFC_PIN_GROUP(sdhi2_cd_b),
	SH_PFC_PIN_GROUP(sdhi2_wp_b),
	SH_PFC_PIN_GROUP(sdhi2_ds),
	SH_PFC_PIN_GROUP(sdhi3_data1),
	SH_PFC_PIN_GROUP(sdhi3_data4),
	SH_PFC_PIN_GROUP(sdhi3_data8),
	SH_PFC_PIN_GROUP(sdhi3_ctrl),
	SH_PFC_PIN_GROUP(sdhi3_cd),
	SH_PFC_PIN_GROUP(sdhi3_wp),
	SH_PFC_PIN_GROUP(sdhi3_ds),
	SH_PFC_PIN_GROUP(usb0),
	SH_PFC_PIN_GROUP(usb1),
	SH_PFC_PIN_GROUP(usb2),
};

static const char * const avb_groups[] = {
	"avb_link",
	"avb_magic",
	"avb_phy_int",
	"avb_mdc",
	"avb_avtp_pps",
	"avb_avtp_match_a",
	"avb_avtp_capture_a",
	"avb_avtp_match_b",
	"avb_avtp_capture_b",
};

static const char * const du_groups[] = {
	"du_rgb888",
	"du_clk_out_0",
	"du_clk_out_1",
	"du_sync",
	"du_oddf",
	"du_cde",
	"du_disp",
};

static const char * const hdmi0_groups[] = {
	"hdmi0_cec",
};

static const char * const hdmi1_groups[] = {
	"hdmi1_cec",
};

static const char * const scif0_groups[] = {
	"scif0_data",
	"scif0_clk",
	"scif0_ctrl",
};

static const char * const scif1_groups[] = {
	"scif1_data_a",
	"scif1_clk",
	"scif1_ctrl",
	"scif1_data_b",
};

static const char * const scif2_groups[] = {
	"scif2_data_a",
	"scif2_clk",
	"scif2_data_b",
};

static const char * const scif3_groups[] = {
	"scif3_data_a",
	"scif3_clk",
	"scif3_ctrl",
	"scif3_data_b",
};

static const char * const scif4_groups[] = {
	"scif4_data_a",
	"scif4_clk_a",
	"scif4_ctrl_a",
	"scif4_data_b",
	"scif4_clk_b",
	"scif4_ctrl_b",
	"scif4_data_c",
	"scif4_clk_c",
	"scif4_ctrl_c",
};

static const char * const scif5_groups[] = {
	"scif5_data",
	"scif5_clk",
};

static const char * const sdhi0_groups[] = {
	"sdhi0_data1",
	"sdhi0_data4",
	"sdhi0_ctrl",
	"sdhi0_cd",
	"sdhi0_wp",
};

static const char * const sdhi1_groups[] = {
	"sdhi1_data1",
	"sdhi1_data4",
	"sdhi1_ctrl",
	"sdhi1_cd",
	"sdhi1_wp",
};

static const char * const sdhi2_groups[] = {
	"sdhi2_data1",
	"sdhi2_data4",
	"sdhi2_data8",
	"sdhi2_ctrl",
	"sdhi2_cd_a",
	"sdhi2_wp_a",
	"sdhi2_cd_b",
	"sdhi2_wp_b",
	"sdhi2_ds",
};

static const char * const sdhi3_groups[] = {
	"sdhi3_data1",
	"sdhi3_data4",
	"sdhi3_data8",
	"sdhi3_ctrl",
	"sdhi3_cd",
	"sdhi3_wp",
	"sdhi3_ds",
};

static const char * const usb0_groups[] = {
	"usb0",
};

static const char * const usb1_groups[] = {
	"usb1",
};

static const char * const usb2_groups[] = {
	"usb2",
};

static const struct sh_pfc_function pinmux_functions[] = {
	SH_PFC_FUNCTION(avb),
	SH_PFC_FUNCTION(du),
	SH_PFC_FUNCTION(hdmi0),
	SH_PFC_FUNCTION(hdmi1),
	SH_PFC_FUNCTION(scif0),
	SH_PFC_FUNCTION(scif1),
	SH_PFC_FUNCTION(scif2),
	SH_PFC_FUNCTION(scif3),
	SH_PFC_FUNCTION(scif4),
	SH_PFC_FUNCTION(scif5),
	SH_PFC_FUNCTION(sdhi0),
	SH_PFC_FUNCTION(sdhi1),
	SH_PFC_FUNCTION(sdhi2),
	SH_PFC_FUNCTION(sdhi3),
	SH_PFC_FUNCTION(usb0),
	SH_PFC_FUNCTION(usb1),
	SH_PFC_FUNCTION(usb2),
};

static const struct pinmux_cfg_reg pinmux_config_regs[] = {
	{ PINMUX_CFG_REG("GPSR0", 0xe6060100, 32, 1) {
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_0_15_FN,	FN_IP7_11_8,
		GP_0_14_FN,	FN_IP7_7_4,
		GP_0_13_FN,	FN_IP7_3_0,
		GP_0_12_FN,	FN_IP6_31_28,
		GP_0_11_FN,	FN_IP6_27_24,
		GP_0_10_FN,	FN_IP6_23_20,
		GP_0_9_FN,	FN_IP6_19_16,
		GP_0_8_FN,	FN_IP6_15_12,
		GP_0_7_FN,	FN_IP6_11_8,
		GP_0_6_FN,	FN_IP6_7_4,
		GP_0_5_FN,	FN_IP6_3_0,
		GP_0_4_FN,	FN_IP5_31_28,
		GP_0_3_FN,	FN_IP5_27_24,
		GP_0_2_FN,	FN_IP5_23_20,
		GP_0_1_FN,	FN_IP5_19_16,
		GP_0_0_FN,	FN_IP5_15_12, }
	},
	{ PINMUX_CFG_REG("GPSR1", 0xe6060104, 32, 1) {
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_1_27_FN,	FN_IP5_11_8,
		GP_1_26_FN,	FN_IP5_7_4,
		GP_1_25_FN,	FN_IP5_3_0,
		GP_1_24_FN,	FN_IP4_31_28,
		GP_1_23_FN,	FN_IP4_27_24,
		GP_1_22_FN,	FN_IP4_23_20,
		GP_1_21_FN,	FN_IP4_19_16,
		GP_1_20_FN,	FN_IP4_15_12,
		GP_1_19_FN,	FN_IP4_11_8,
		GP_1_18_FN,	FN_IP4_7_4,
		GP_1_17_FN,	FN_IP4_3_0,
		GP_1_16_FN,	FN_IP3_31_28,
		GP_1_15_FN,	FN_IP3_27_24,
		GP_1_14_FN,	FN_IP3_23_20,
		GP_1_13_FN,	FN_IP3_19_16,
		GP_1_12_FN,	FN_IP3_15_12,
		GP_1_11_FN,	FN_IP3_11_8,
		GP_1_10_FN,	FN_IP3_7_4,
		GP_1_9_FN,	FN_IP3_3_0,
		GP_1_8_FN,	FN_IP2_31_28,
		GP_1_7_FN,	FN_IP2_27_24,
		GP_1_6_FN,	FN_IP2_23_20,
		GP_1_5_FN,	FN_IP2_19_16,
		GP_1_4_FN,	FN_IP2_15_12,
		GP_1_3_FN,	FN_IP2_11_8,
		GP_1_2_FN,	FN_IP2_7_4,
		GP_1_1_FN,	FN_IP2_3_0,
		GP_1_0_FN,	FN_IP1_31_28, }
	},
	{ PINMUX_CFG_REG("GPSR2", 0xe6060108, 32, 1) {
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_2_14_FN,	FN_IP0_23_20,
		GP_2_13_FN,	FN_IP0_19_16,
		GP_2_12_FN,	FN_IP0_15_12,
		GP_2_11_FN,	FN_IP0_11_8,
		GP_2_10_FN,	FN_IP0_7_4,
		GP_2_9_FN,	FN_IP0_3_0,
		GP_2_8_FN,	FN_IP1_27_24,
		GP_2_7_FN,	FN_IP1_23_20,
		GP_2_6_FN,	FN_IP1_19_16,
		GP_2_5_FN,	FN_IP1_15_12,
		GP_2_4_FN,	FN_IP1_11_8,
		GP_2_3_FN,	FN_IP1_7_4,
		GP_2_2_FN,	FN_IP1_3_0,
		GP_2_1_FN,	FN_IP0_31_28,
		GP_2_0_FN,	FN_IP0_27_24, }
	},
	{ PINMUX_CFG_REG("GPSR3", 0xe606010c, 32, 1) {
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_3_15_FN,	FN_IP10_23_20,
		GP_3_14_FN,	FN_IP10_19_16,
		GP_3_13_FN,	FN_IP10_15_12,
		GP_3_12_FN,	FN_IP10_11_8,
		GP_3_11_FN,	FN_IP8_31_28,
		GP_3_10_FN,	FN_IP8_27_24,
		GP_3_9_FN,	FN_IP8_23_20,
		GP_3_8_FN,	FN_IP8_19_16,
		GP_3_7_FN,	FN_IP8_15_12,
		GP_3_6_FN,	FN_IP8_11_8,
		GP_3_5_FN,	FN_IP8_7_4,
		GP_3_4_FN,	FN_IP8_3_0,
		GP_3_3_FN,	FN_IP7_31_28,
		GP_3_2_FN,	FN_IP7_27_24,
		GP_3_1_FN,	FN_IP7_23_20,
		GP_3_0_FN,	FN_IP7_19_16, }
	},
	{ PINMUX_CFG_REG("GPSR4", 0xe6060110, 32, 1) {
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_4_17_FN,	0, /* SD3_DS */
		GP_4_16_FN,	FN_IP10_7_4,
		GP_4_15_FN,	FN_IP10_3_0,
		GP_4_14_FN,	FN_IP9_31_28,
		GP_4_13_FN,	FN_IP9_27_24,
		GP_4_12_FN,	0, /* SD3_DAT3 */
		GP_4_11_FN,	0, /* SD3_DAT2 */
		GP_4_10_FN,	0, /* SD3_DAT1 */
		GP_4_9_FN,	0, /* SD3_DAT0 */
		GP_4_8_FN,	0, /* SD3_CMD */
		GP_4_7_FN,	0, /* SD3_CLK */
		GP_4_6_FN,	FN_IP9_23_20,
		GP_4_5_FN,	FN_IP9_19_16,
		GP_4_4_FN,	FN_IP9_15_12,
		GP_4_3_FN,	FN_IP9_11_8,
		GP_4_2_FN,	FN_IP9_7_4,
		GP_4_1_FN,	0, /* SD2_CMD */
		GP_4_0_FN,	FN_IP9_3_0, }
	},
	{ PINMUX_CFG_REG("GPSR5", 0xe6060114, 32, 1) {
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_5_25_FN,	FN_IP13_19_16,
		GP_5_24_FN,	FN_IP13_15_12,
		GP_5_23_FN,	FN_IP13_11_8,
		GP_5_22_FN,	0, /* MSIOF0_RXD */
		GP_5_21_FN,	FN_IP13_7_4,
		GP_5_20_FN,	0, /* MSIOF0_TXD */
		GP_5_19_FN,	FN_IP13_3_0,
		GP_5_18_FN,	FN_IP12_31_28,
		GP_5_17_FN,	0, /*MSIOF0_SCK */
		GP_5_16_FN,	FN_IP12_27_24,
		GP_5_15_FN,	FN_IP12_23_20,
		GP_5_14_FN,	FN_IP12_19_16,
		GP_5_13_FN,	FN_IP12_15_12,
		GP_5_12_FN,	FN_IP12_11_8,
		GP_5_11_FN,	FN_IP12_7_4,
		GP_5_10_FN,	FN_IP12_3_0,
		GP_5_9_FN,	FN_IP11_31_28,
		GP_5_8_FN,	FN_IP11_27_24,
		GP_5_7_FN,	FN_IP11_23_20,
		GP_5_6_FN,	FN_IP11_19_16,
		GP_5_5_FN,	FN_IP11_15_12,
		GP_5_4_FN,	FN_IP11_11_8,
		GP_5_3_FN,	FN_IP11_7_4,
		GP_5_2_FN,	FN_IP11_3_0,
		GP_5_1_FN,	FN_IP10_31_28,
		GP_5_0_FN,	FN_IP10_27_24, }
	},
	{ PINMUX_CFG_REG("GPSR6", 0xe6060118, 32, 1) {
		GP_6_31_FN,	FN_IP17_7_4,
		GP_6_30_FN,	FN_IP17_3_0,
		GP_6_29_FN,	FN_IP16_31_28,
		GP_6_28_FN,	FN_IP16_27_24,
		GP_6_27_FN,	FN_IP16_23_20,
		GP_6_26_FN,	FN_IP16_19_16,
		GP_6_25_FN,	FN_IP16_15_12,
		GP_6_24_FN,	FN_IP16_11_8,
		GP_6_23_FN,	FN_IP16_7_4,
		GP_6_22_FN,	FN_IP16_3_0,
		GP_6_21_FN,	FN_IP15_31_28,
		GP_6_20_FN,	FN_IP15_27_24,
		GP_6_19_FN,	FN_IP15_23_20,
		GP_6_18_FN,	FN_IP15_19_16,
		GP_6_17_FN,	FN_IP15_15_12,
		GP_6_16_FN,	FN_IP15_11_8,
		GP_6_15_FN,	FN_IP15_7_4,
		GP_6_14_FN,	FN_IP15_3_0,
		GP_6_13_FN,	0, /* SSI_SDATA5 */
		GP_6_12_FN,	0, /* SSI_WS5 */
		GP_6_11_FN,	0, /* SSI_SCK5 */
		GP_6_10_FN,	FN_IP14_31_28,
		GP_6_9_FN,	FN_IP14_27_24,
		GP_6_8_FN,	FN_IP14_23_20,
		GP_6_7_FN,	FN_IP14_19_16,
		GP_6_6_FN,	FN_IP14_15_12,
		GP_6_5_FN,	FN_IP14_11_8,
		GP_6_4_FN,	FN_IP14_7_4,
		GP_6_3_FN,	FN_IP14_3_0,
		GP_6_2_FN,	FN_IP13_31_28,
		GP_6_1_FN,	FN_IP13_27_24,
		GP_6_0_FN,	FN_IP13_23_20, }
	},
	{ PINMUX_CFG_REG("GPSR7", 0xe606011c, 32, 1) {
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_7_3_FN,	0, /* HDMI1_CEC */
		GP_7_2_FN,	0, /* HDMI0_CEC */
		GP_7_1_FN,	0, /* AVS2 */
		GP_7_0_FN,	0, /* AVS1 */ }
	},
	{ PINMUX_CFG_REG("IPSR0", 0xe6060200, 32, 4) {
		/* IP0_31_28 [4] */
		0, 0, 0, FN_DU_DISP,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_27_24 [4] */
		0, 0, 0, FN_DU_CDE,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_23_20 [4] */
		FN_AVB_AVTP_CAPTURE_A, 0, 0, FN_RTS4_N_TANS_A,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_19_16 [4] */
		FN_AVB_AVTP_MATCH_A, 0, 0, FN_CTS4_N_A,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_15_12 [4] */
		FN_AVB_LINK, 0, 0, FN_TX4_A,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_11_8 [4] */
		FN_AVB_PHY_INT, 0, 0, FN_RX4_A,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_7_4 [4] */
		FN_AVB_MAGIC, 0, 0, FN_SCK4_A,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_3_0 [4] */
		FN_AVB_MDC, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR1", 0xe6060204, 32, 4) {
		/* IP1_31_28 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DB0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_19_16 [4] */
		0, FN_AVB_AVTP_PPS, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_15_12 [4] */
		0, 0, 0, FN_DU_EXVSYNC_DU_VSYNC,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_11_8 [4] */
		0, 0, 0, FN_DU_EXHSYNC_DU_HSYNC,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_7_4 [4] */
		0, 0, 0, FN_DU_DOTCLKOUT1,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_3_0 [4] */
		0, 0, 0, FN_DU_EXODDF_DU_ODDF_DISP_CDE,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR2", 0xe6060208, 32, 4) {
		/* IP2_31_28 [4] */
		0, FN_RX3_B, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_27_24 [4] */
		0, 0, 0, FN_TX4_B,
		0, 0, FN_DU_DB7, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_23_20 [4] */
		0, 0, 0, FN_RX4_B,
		0, 0, FN_DU_DB6, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_19_16 [4] */
		0, 0, 0, FN_SCK4_B,
		0, 0, FN_DU_DB5, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_15_12 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DB4, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_11_8 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DB3, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_7_4 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DB2, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_3_0 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DB1, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR3", 0xe606020c, 32, 4) {
		/* IP3_31_28 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DG0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_27_24 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DG7, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_23_20 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DG6, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_19_16 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DG5, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_15_12 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DG4, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_11_8 [4] */
		0, FN_TX3_B, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_7_4 [4] */
		0, 0, 0, FN_RTS4_N_TANS_B,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_3_0 [4] */
		0, 0, 0, FN_CTS4_N_B,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR4", 0xe6060210, 32, 4) {
		/* IP4_31_28 [4] */
		0, 0, 0, FN_TX3_A,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_27_24 [4] */
		0, 0, 0, FN_RX3_A,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_23_20 [4] */
		0, 0, 0, FN_SCK3,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_11_8 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DG3, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_7_4 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DG2, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_3_0 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DG1, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR5", 0xe6060214, 32, 4) {
		/* IP5_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP5_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP5_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP5_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP5_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP5_11_8 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DOTCLKOUT0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP5_7_4 [4] */
		0, 0, 0, FN_RTS3_N_TANS,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP5_3_0 [4] */
		0, 0, 0, FN_CTS3_N,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR6", 0xe6060218, 32, 4) {
		/* IP6_31_28 [4] */
		0, 0, 0, FN_RX4_C,
		0, 0, FN_DU_DR4, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_27_24 [4] */
		0, 0, 0, 0,
		0, FN_RTS4_N_TANS_C, FN_DU_DR3, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_23_20 [4] */
		0, 0, 0, 0,
		0, FN_CTS4_N_C, FN_DU_DR2, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_19_16 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DR1, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_15_12 [4] */
		0, 0, 0, FN_SCK4_C,
		0, 0, FN_DU_DR0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR7", 0xe606021c, 32, 4) {
		/* IP7_31_28 [4] */
		FN_SD0_DAT1, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_27_24 [4] */
		FN_SD0_DAT0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_23_20 [4] */
		FN_SD0_CMD, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_19_16 [4] */
		FN_SD0_CLK, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_11_8 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DR7, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_7_4 [4] */
		0, 0, 0, 0,
		0, 0, FN_DU_DR6, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_3_0 [4] */
		0, 0, 0, FN_TX4_C,
		0, 0, FN_DU_DR5, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR8", 0xe6060220, 32, 4) {
		/* IP8_31_28 [4] */
		FN_SD1_DAT3, FN_SD2_DAT7, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_27_24 [4] */
		FN_SD1_DAT2, FN_SD2_DAT6, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_23_20 [4] */
		FN_SD1_DAT1, FN_SD2_DAT5, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_19_16 [4] */
		FN_SD1_DAT0, FN_SD2_DAT4, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_15_12 [4] */
		FN_SD1_CMD, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_11_8 [4] */
		FN_SD1_CLK, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_7_4 [4] */
		FN_SD0_DAT3, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_3_0 [4] */
		FN_SD0_DAT2, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR9", 0xe6060224, 32, 4) {
		/* IP9_31_28 [4] */
		FN_SD3_DAT5, FN_SD2_WP_A, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_27_24 [4] */
		FN_SD3_DAT4, FN_SD2_CD_A, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_23_20 [4] */
		FN_SD2_DS, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_19_16 [4] */
		FN_SD2_DAT3, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_15_12 [4] */
		FN_SD2_DAT2, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_11_8 [4] */
		FN_SD2_DAT1, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_7_4 [4] */
		FN_SD2_DAT0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_3_0 [4] */
		FN_SD2_CLK, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR10", 0xe6060228, 32, 4) {
		/* IP10_31_28 [4] */
		FN_RX0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_27_24 [4] */
		FN_SCK0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_23_20 [4] */
		FN_SD1_WP, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_19_16 [4] */
		FN_SD1_CD, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_15_12 [4] */
		FN_SD0_WP, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_11_8 [4] */
		FN_SD0_CD, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_7_4 [4] */
		FN_SD3_DAT7, FN_SD3_WP, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_3_0 [4] */
		FN_SD3_DAT6, FN_SD3_CD, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR11", 0xe606022c, 32, 4) {
		/* IP11_31_28 [4] */
		FN_SCK2, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_27_24 [4] */
		FN_RTS1_N_TANS, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_23_20 [4] */
		FN_CTS1_N, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_19_16 [4] */
		FN_TX1_A, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_15_12 [4] */
		FN_RX1_A, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_11_8 [4] */
		FN_RTS0_N_TANS, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_7_4 [4] */
		FN_CTS0_N, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_3_0 [4] */
		FN_TX0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR12", 0xe6060230, 32, 4) {
		/* IP12_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP12_27_24 [4] */
		0, FN_TX2_B, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP12_23_20 [4] */
		0, FN_RX2_B, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP12_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP12_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP12_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP12_7_4 [4] */
		FN_RX2_A, 0, 0, FN_SD2_WP_B,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP12_3_0 [4] */
		FN_TX2_A, 0, 0, FN_SD2_CD_B,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR13", 0xe6060234, 32, 4) {
		/* IP13_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_19_16 [4] */
		0, FN_TX1_B, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_15_12 [4] */
		0, FN_RX1_B, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_7_4 [4] */
		0, FN_TX5, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_3_0 [4] */
		0, FN_RX5, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR14", 0xe6060238, 32, 4) {
		/* IP14_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP14_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP14_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP14_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP14_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP14_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP14_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP14_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR15", 0xe606023c, 32, 4) {
		/* IP15_31_28 [4] */
		0, 0, 0, 0,
		0, FN_SCK1, 0, FN_SCK5,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP15_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP15_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP15_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP15_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP15_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP15_7_4 [4] */
		0, FN_USB0_OVC, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP15_3_0 [4] */
		0, FN_USB2_PWEN, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR16", 0xe6060240, 32, 4) {
		/* IP16_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP16_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP16_23_20 [4] */
		FN_USB1_OVC, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP16_19_16 [4] */
		FN_USB1_PWEN, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP16_15_12 [4] */
		FN_USB0_OVC, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP16_11_8 [4] */
		FN_USB0_PWEN, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP16_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP16_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR17", 0xe6060244, 32, 4) {
		/* IP17_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP17_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP17_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP17_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP17_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP17_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP17_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP17_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG_VAR("MOD_SEL0", 0xe6060500, 32,
			     1, 2, 2, 3, 1, 1, 2, 1, 1, 1,
			     2, 1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 2, 1) {
		/* RESERVED [1] */
		0, 0,
		/* SEL_MSIOF3 [2] */
		FN_SEL_MSIOF3_0,	FN_SEL_MSIOF3_1,
		FN_SEL_MSIOF3_2,	FN_SEL_MSIOF3_3,
		/* SEL_MSIOF2 [2] */
		FN_SEL_MSIOF2_0,	FN_SEL_MSIOF2_1,
		FN_SEL_MSIOF2_2,	FN_SEL_MSIOF2_3,
		/* SEL_MSIOF1 [3] */
		FN_SEL_MSIOF1_0,	FN_SEL_MSIOF1_1,
		FN_SEL_MSIOF1_2,	FN_SEL_MSIOF1_3,
		FN_SEL_MSIOF1_4,	FN_SEL_MSIOF1_5,
		FN_SEL_MSIOF1_6,	FN_SEL_MSIOF1_7,
		/* SEL_LBSC [1] */
		FN_SEL_LBSC_0,		FN_SEL_LBSC_1,
		/* SEL_IEBUS [1] */
		FN_SEL_IEBUS_0,		FN_SEL_IEBUS_1,
		/* SEL_I2C6 [2] */
		FN_SEL_I2C6_0,		FN_SEL_I2C6_1,
		FN_SEL_I2C6_2,		FN_SEL_I2C6_3,
		/* SEL_I2C2 [1] */
		FN_SEL_I2C2_0,		FN_SEL_I2C2_1,
		/* SEL_I2C1 [1] */
		FN_SEL_I2C1_0,		FN_SEL_I2C1_1,
		/* SEL_HSCIF4 [1] */
		FN_SEL_HSCIF4_0,	FN_SEL_HSCIF4_1,
		/* SEL_HSCIF3 [2] */
		FN_SEL_HSCIF3_0,	FN_SEL_HSCIF3_1,
		FN_SEL_HSCIF3_2,	FN_SEL_HSCIF3_3,
		/* SEL_HSCIF2 [1] */
		FN_SEL_HSCIF2_0,	FN_SEL_HSCIF2_1,
		/* SEL_HSCIF1 [1] */
		FN_SEL_HSCIF1_0,	FN_SEL_HSCIF1_1,
		/* SEL_FSO [1] */
		FN_SEL_FSO_0,		FN_SEL_FSO_1,
		/* SEL_FM [1] */
		FN_SEL_FM_0,		FN_SEL_FM_1,
		/* SEL_ETHERAVB [1] */
		FN_SEL_ETHERAVB_0,	FN_SEL_ETHERAVB_1,
		/* SEL_DRIF3 [1] */
		FN_SEL_DRIF3_0,		FN_SEL_DRIF3_1,
		/* SEL_DRIF2 [1] */
		FN_SEL_DRIF2_0,		FN_SEL_DRIF2_1,
		/* SEL_DRIF1 [2] */
		FN_SEL_DRIF1_0,		FN_SEL_DRIF1_1,
		FN_SEL_DRIF1_2,		FN_SEL_DRIF1_3,
		/* SEL_DRIF0 [2] */
		FN_SEL_DRIF0_0,		FN_SEL_DRIF0_1,
		FN_SEL_DRIF0_2,		FN_SEL_DRIF0_3,
		/* SEL_CANFD [1] */
		FN_SEL_CANFD0_0,	FN_SEL_CANFD0_1,
		/* SEL_ADG [2] */
		FN_SEL_ADG_0,		FN_SEL_ADG_1,
		FN_SEL_ADG_2,		FN_SEL_ADG_3,
		/* RESERVED [1] */
		0, 0, }
	},
	{ PINMUX_CFG_REG_VAR("MOD_SEL1", 0xe6060504, 32,
			     2, 3, 1, 2, 3, 1, 1, 2, 1,
			     2, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1) {
		/* SEL_TSIF1 [2] */
		FN_SEL_TSIF1_0,		FN_SEL_TSIF1_1,
		FN_SEL_TSIF1_2,		FN_SEL_TSIF1_3,
		/* SEL_TSIF0 [3] */
		FN_SEL_TSIF0_0,		FN_SEL_TSIF0_1,
		FN_SEL_TSIF0_2,		FN_SEL_TSIF0_3,
		FN_SEL_TSIF0_4,		FN_SEL_TSIF0_5,
		FN_SEL_TSIF0_6,		FN_SEL_TSIF0_7,
		/* SEL_TIMER_TM	 [1] */
		FN_SEL_TIMER_TMU_0,	FN_SEL_TIMER_TMU_1,
		/* SEL_SSP1_1 [2] */
		FN_SEL_SSP1_1_0,	FN_SEL_SSP1_1_1,
		FN_SEL_SSP1_1_2,	FN_SEL_SSP1_1_3,
		/* SEL_SSP1_0 [3] */
		FN_SEL_SSP1_0_0,	FN_SEL_SSP1_0_1,
		FN_SEL_SSP1_0_2,	FN_SEL_SSP1_0_3,
		FN_SEL_SSP1_0_4,	FN_SEL_SSP1_0_5,
		FN_SEL_SSP1_0_6,	FN_SEL_SSP1_0_7,
		/* SEL_SSI [1] */
		FN_SEL_SSI_0,		FN_SEL_SSI_1,
		/* SEL_SPEED_PULSE [1] */
		FN_SEL_SPEED_PULSE_0,	FN_SEL_SPEED_PULSE_1,
		/* SEL_SIMCARD [2] */
		FN_SEL_SIMCARD_0,	FN_SEL_SIMCARD_1,
		FN_SEL_SIMCARD_2,	FN_SEL_SIMCARD_3,
		/* SEL_SDHI2 [1] */
		FN_SEL_SDHI2_0,		FN_SEL_SDHI2_1,
		/* SEL_SCIF4 [2] */
		FN_SEL_SCIF4_0,		FN_SEL_SCIF4_1,
		FN_SEL_SCIF4_2,		FN_SEL_SCIF4_3,
		/* SEL_SCIF3 [1] */
		FN_SEL_SCIF3_0,		FN_SEL_SCIF3_1,
		/* SEL_SCIF2 [1] */
		FN_SEL_SCIF2_0,		FN_SEL_SCIF2_1,
		/* SEL_SCIF1 [1] */
		FN_SEL_SCIF1_0,		FN_SEL_SCIF1_1,
		/* SEL_SCIF [1] */
		FN_SEL_SCIF_0,		FN_SEL_SCIF_1,
		/* SEL_REMOCON [1] */
		FN_SEL_REMOCON_0,	FN_SEL_REMOCON_1,
		/* RESERVED [2] */
		0, 0, 0, 0,
		/* SEL_RCAN0 [1] */
		FN_SEL_RCAN0_0,		FN_SEL_RCAN0_1,
		/* SEL_PWM6 [1] */
		FN_SEL_PWM6_0,		FN_SEL_PWM6_1,
		/* SEL_PWM5 [1] */
		FN_SEL_PWM5_0,		FN_SEL_PWM5_1,
		/* SEL_PWM4 [1] */
		FN_SEL_PWM4_0,		FN_SEL_PWM4_1,
		/* SEL_PWM3 [1] */
		FN_SEL_PWM3_0,		FN_SEL_PWM3_1,
		/* SEL_PWM2 [1] */
		FN_SEL_PWM2_0,		FN_SEL_PWM2_1,
		/* SEL_PWM1 [1] */
		FN_SEL_PWM1_0,		FN_SEL_PWM1_1, }
	},
	{ PINMUX_CFG_REG_VAR("MOD_SEL2", 0xe6060508, 32,
			     1, 1, 1,
			     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			     2, 1) {
		/* SEL_I2C_5 [1] */
		FN_SEL_I2C_5_0,		FN_SEL_I2C_5_1,
		/* SEL_I2C_3 [1] */
		FN_SEL_I2C_3_0,		FN_SEL_I2C_3_1,
		/* SEL_I2C_0 [1] */
		FN_SEL_I2C_0_0,		FN_SEL_I2C_0_1,
		/* RESERVED [1 x 26] */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* SEL_VSP [2] */
		FN_SEL_VSP_0,		FN_SEL_VSP_1,
		FN_SEL_VSP_2,		FN_SEL_VSP_3,
		/* SEL_VIN4 [1] */
		FN_SEL_VIN4_0,		FN_SEL_VIN4_1, }
	},
	{ },
};

const struct sh_pfc_soc_info r8a7795_pinmux_info = {
	.name = "r8a77950_pfc",
	.unlock_reg = 0xe6060000, /* PMMR */

	.function = { PINMUX_FUNCTION_BEGIN, PINMUX_FUNCTION_END },

	.pins = pinmux_pins,
	.nr_pins = ARRAY_SIZE(pinmux_pins),
	.groups = pinmux_groups,
	.nr_groups = ARRAY_SIZE(pinmux_groups),
	.functions = pinmux_functions,
	.nr_functions = ARRAY_SIZE(pinmux_functions),

	.cfg_regs = pinmux_config_regs,

	.gpio_data = pinmux_data,
	.gpio_data_size = ARRAY_SIZE(pinmux_data),
};
