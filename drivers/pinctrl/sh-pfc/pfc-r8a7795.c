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

	PINMUX_MARK_END,
};

static const u16 pinmux_data[] = {
	PINMUX_DATA_GP_ALL(),

	/* IPSR0 */

	/* IPSR1 */

	/* IPSR2 */

	/* IPSR3 */

	/* IPSR4 */

	/* IPSR5 */

	/* IPSR6 */

	/* IPSR7 */

	/* IPSR8 */

	/* IPSR9 */

	/* IPSR10 */

	/* IPSR11 */

	/* IPSR12 */

	/* IPSR13 */

	/* IPSR14 */

	/* IPSR15 */

	/* IPSR16 */

	/* IPSR17 */
};

static const struct sh_pfc_pin pinmux_pins[] = {
	PINMUX_GPIO_GP_ALL(),
};

static const struct sh_pfc_pin_group pinmux_groups[] = {
};

static const struct sh_pfc_function pinmux_functions[] = {
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP0_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR1", 0xe6060204, 32, 4) {
		/* IP1_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP1_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR2", 0xe6060208, 32, 4) {
		/* IP2_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP2_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR3", 0xe606020c, 32, 4) {
		/* IP3_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP3_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR4", 0xe6060210, 32, 4) {
		/* IP4_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_23_20 [4] */
		0, 0, 0, 0,
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP4_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP5_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP5_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR6", 0xe6060218, 32, 4) {
		/* IP6_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP6_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_19_16 [4] */
		0, 0, 0, 0,
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP7_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR8", 0xe6060220, 32, 4) {
		/* IP8_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP8_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR9", 0xe6060224, 32, 4) {
		/* IP9_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP9_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR10", 0xe6060228, 32, 4) {
		/* IP10_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP10_3_0 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0, }
	},
	{ PINMUX_CFG_REG("IPSR11", 0xe606022c, 32, 4) {
		/* IP11_31_28 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_27_24 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_23_20 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP11_3_0 [4] */
		0, 0, 0, 0,
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP12_23_20 [4] */
		0, 0, 0, 0,
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP12_3_0 [4] */
		0, 0, 0, 0,
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_11_8 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_7_4 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP13_3_0 [4] */
		0, 0, 0, 0,
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
		0, 0, 0, 0,
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP15_3_0 [4] */
		0, 0, 0, 0,
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
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP16_19_16 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP16_15_12 [4] */
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		/* IP16_11_8 [4] */
		0, 0, 0, 0,
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
