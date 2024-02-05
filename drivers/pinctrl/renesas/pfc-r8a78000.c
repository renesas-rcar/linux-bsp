// SPDX-License-Identifier: GPL-2.0
/*
 * R8A779A0 processor support - PFC hardware block.
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 * This file is based on the drivers/pinctrl/renesas/pfc-r8a779a0.c
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include "sh_pfc.h"

#define CFG_FLAGS (SH_PFC_PIN_CFG_DRIVE_STRENGTH | SH_PFC_PIN_CFG_PULL_UP_DOWN)

#define CPU_ALL_GP(fn, sfx)										\
	PORT_GP_CFG_25(0,	fn, sfx, CFG_FLAGS ),			\
	PORT_GP_CFG_32(1,	fn, sfx, CFG_FLAGS ),			\
	PORT_GP_CFG_29(2,	fn, sfx, CFG_FLAGS ),			\
	PORT_GP_CFG_17(3,	fn, sfx, CFG_FLAGS ),			\
	PORT_GP_CFG_14(4,	fn, sfx, CFG_FLAGS ),			\
	PORT_GP_CFG_23(5,	fn, sfx, CFG_FLAGS ),			\
	PORT_GP_CFG_31(6,	fn, sfx, CFG_FLAGS ),			\
	PORT_GP_CFG_31(7,	fn, sfx, CFG_FLAGS ),			\
	PORT_GP_CFG_22(8,	fn, sfx, CFG_FLAGS ),			\
	PORT_GP_CFG_17(9,	fn, sfx, CFG_FLAGS ),			\
	PORT_GP_CFG_14(10,	fn, sfx, CFG_FLAGS )

/*
 * F_() : just information
 * FM() : macro for FN_xxx / xxx_MARK
 */

/* GPSR0 */
#define GPSR0_24	F_(MSIOF1_SS2,		IP3SR0_3_0)
#define GPSR0_23	F_(MSIOF1_SS1,		IP2SR0_31_28)
#define GPSR0_22	F_(MSIOF1_SYNC,		IP2SR0_27_24)
#define GPSR0_21	F_(MSIOF1_RXD,		IP2SR0_23_20)
#define GPSR0_20	F_(MSIOF1_TXD,		IP2SR0_19_16)
#define GPSR0_19	F_(MSIOF1_SCK,		IP2SR0_15_12)
#define GPSR0_18	F_(MSIOF0_SS2,		IP2SR0_11_8)
#define GPSR0_17	F_(MSIOF0_SS1,		IP2SR0_7_4)
#define GPSR0_16	F_(MSIOF0_SYNC,		IP2SR0_3_0)
#define GPSR0_15	F_(MSIOF0_RXD,		IP1SR0_31_28)
#define GPSR0_14	F_(MSIOF0_TXD,		IP1SR0_27_24)
#define GPSR0_13	F_(MSIOF0_SCK,		IP1SR0_23_20)
#define GPSR0_12	F_(GP0_12,		IP1SR0_19_16)
#define GPSR0_11	F_(GP0_11,		IP1SR0_15_12)
#define GPSR0_10	F_(GP0_10,		IP1SR0_11_8)
#define GPSR0_9		F_(GP0_09,		IP1SR0_7_4)
#define GPSR0_8		F_(GP0_08,		IP1SR0_3_0)
#define GPSR0_7		F_(GP0_07,		IP0SR0_31_28)
#define GPSR0_6		F_(GP0_06,		IP0SR0_27_24)
#define GPSR0_5		F_(GP0_05,		IP0SR0_23_20)
#define GPSR0_4		F_(GP0_04,		IP0SR0_19_16)
#define GPSR0_3		F_(GP0_03,		IP0SR0_15_12)
#define GPSR0_2		F_(GP0_02,		IP0SR0_11_8)
#define GPSR0_1		F_(GP0_01,		IP0SR0_7_4)
#define GPSR0_0		F_(GP0_00,		IP0SR0_3_0)

/* GPSR1 */
#define GPSR1_31	F_(RLIN33TX,		IP3SR1_31_28)
#define GPSR1_30	F_(RLIN33RX,		IP3SR1_27_24)
#define GPSR1_29	F_(RLIN32TX,		IP3SR1_23_20)
#define GPSR1_28	F_(RLIN32RX,		IP3SR1_19_16)
#define GPSR1_27	F_(RLIN31TX,		IP3SR1_15_12)
#define GPSR1_26	F_(RLIN31RX,		IP3SR1_11_8)
#define GPSR1_25	F_(RLIN30TX,		IP3SR1_7_4)
#define GPSR1_24	F_(RLIN30RX,		IP3SR1_3_0)
#define GPSR1_23	F_(CAN11TX,		IP2SR1_31_28)
#define GPSR1_22	F_(CAN11RX,		IP2SR1_27_24)
#define GPSR1_21	F_(CAN10TX,		IP2SR1_23_20)
#define GPSR1_20	F_(CAN10RX,		IP2SR1_19_16)
#define GPSR1_19	F_(CAN9TX,		IP2SR1_15_12)
#define GPSR1_18	F_(CAN9RX,		IP2SR1_11_8)
#define GPSR1_17	F_(CAN8TX,		IP2SR1_7_4)
#define GPSR1_16	F_(CAN8RX,		IP2SR1_3_0)
#define GPSR1_15	F_(CAN7TX,		IP1SR1_31_28)
#define GPSR1_14	F_(CAN7RX,		IP1SR1_27_24)
#define GPSR1_13	F_(CAN6TX,		IP1SR1_23_20)
#define GPSR1_12	F_(CAN6RX,		IP1SR1_19_16)
#define GPSR1_11	F_(CAN5TX,		IP1SR1_15_12)
#define GPSR1_10	F_(CAN5RX,		IP1SR1_11_8)
#define GPSR1_9		F_(CAN4TX,		IP1SR1_7_4)
#define GPSR1_8		F_(CAN4RX,		IP1SR1_3_0)
#define GPSR1_7		F_(CAN3TX,		IP0SR1_31_28)
#define GPSR1_6		F_(CAN3RX,		IP0SR1_27_24)
#define GPSR1_5		F_(CAN2TX,		IP0SR1_23_20)
#define GPSR1_4		F_(CAN2RX,		IP0SR1_19_16)
#define GPSR1_3		F_(CAN1TX,		IP0SR1_15_12)
#define GPSR1_2		F_(CAN1RX,		IP0SR1_11_8)
#define GPSR1_1		F_(CAN0TX,		IP0SR1_7_4)
#define GPSR1_0		F_(CAN0RX,		IP0SR1_3_0)

/* GPSR2 */
#define GPSR2_28	F_(INTP34,		IP3SR2_15_12)
#define GPSR2_27	F_(TAUD1I3,		IP3SR2_11_8)
#define GPSR2_26	F_(TAUD1I2,		IP3SR2_7_4)
#define GPSR2_25	F_(TAUD1I1,		IP2SR2_3_0)
#define GPSR2_24	F_(TAUD1I0,		IP2SR2_31_28)
#define GPSR2_23	F_(EXTCLK0O,		IP2SR2_27_24)
#define GPSR2_22	F_(AVS1,		IP2SR2_23_20)
#define GPSR2_21	F_(AVS0,		IP2SR2_15_12)
#define GPSR2_20	F_(SDA0,		IP2SR2_19_16)
#define GPSR2_19	F_(SCL0,		IP2SR2_15_12)
#define GPSR2_18	F_(INTP33,		IP2SR2_11_8)
#define GPSR2_17	F_(INTP32,		IP2SR2_7_4)
#define GPSR2_16	F_(CAN_CLK,		IP2SR2_3_0)
#define GPSR2_15	F_(CAN15TX,		IP1SR2_31_28)
#define GPSR2_14	F_(CAN15RX,		IP1SR2_27_24)
#define GPSR2_13	F_(CAN14TX,		IP1SR2_23_20)
#define GPSR2_12	F_(CAN14RX,		IP1SR2_19_16)
#define GPSR2_11	F_(CAN13TX,		IP1SR2_15_12)
#define GPSR2_10	F_(CAN13RX,		IP1SR2_11_8)
#define GPSR2_9		F_(CAN12TX,		IP1SR2_7_4)
#define GPSR2_8		F_(CAN12RX,		IP1SR2_3_0)
#define GPSR2_7		F_(RLIN37TX,		IP0SR2_31_28)
#define GPSR2_6		F_(RLIN37RX,		IP0SR2_27_24)
#define GPSR2_5		F_(RLIN36TX,		IP0SR2_23_20)
#define GPSR2_4		F_(RLIN36RX,		IP0SR2_19_16)
#define GPSR2_3		F_(RLIN35TX,		IP0SR2_15_12)
#define GPSR2_2		F_(RLIN35RX,		IP0SR2_11_8)
#define GPSR2_1		F_(RLIN34TX,		IP0SR2_7_4)
#define GPSR2_0		F_(RLIN34RX,		IP0SR2_3_0)

/* GPSR3 */
#define GPSR3_16	F_(ERRORIN#,		IP2SR3_3_0)
#define GPSR3_15	F_(ERROROUT#,		IP1SR3_31_28)
#define GPSR3_14	F_(QSPI1_SSL,		IP1SR3_27_24)
#define GPSR3_13	F_(QSPI1_IO3,		IP1SR3_23_20)
#define GPSR3_12	F_(QSPI1_IO2,		IP1SR3_19_16)
#define GPSR3_11	F_(QSPI1_MOSI_IO1,	IP1SR3_15_12)
#define GPSR3_10	F_(QSPI1_MOSI_IO0,	IP1SR3_11_8)
#define GPSR3_9		F_(QSPI1_SPCLK,		IP1SR3_7_4)
#define GPSR3_8		F_(RPC_INT#,		IP1SR3_3_0)
#define GPSR3_7		F_(RPC_WP#,		IP0SR3_31_28)
#define GPSR3_6		F_(RPC_RESET#,		IP0SR3_27_24)
#define GPSR3_5		F_(QSPI0_SSL,		IP0SR3_23_20)
#define GPSR3_4		F_(QSPI0_IO3,		IP0SR3_19_16)
#define GPSR3_3		F_(QSPI0_IO2,		IP0SR3_15_12)
#define GPSR3_2		F_(QSPI0_MISO_IO1,	IP0SR3_11_8)
#define GPSR3_1		F_(QSPI0_MOSI_IO0,	IP0SR3_7_4)
#define GPSR3_0		F_(QSPI0_SPCLK,		IP0SR3_3_0)

/* GPSR4 */
#define GPSR4_13	F_(ERRORIN#,		IP1SR4_23_20)
#define GPSR4_12	F_(SD0_CD,		IP1SR4_19_16)
#define GPSR4_11	F_(SD0_WP,		IP1SR4_15_12)
#define GPSR4_10	F_(MMC0_DS,		IP1SR4_11_8)
#define GPSR4_9		F_(MMC0_D7,		IP1SR4_7_4)
#define GPSR4_8		F_(MMC0_D6,		IP1SR4_3_0)
#define GPSR4_7		F_(MMC0_D5,		IP0SR4_31_28)
#define GPSR4_6		F_(MMC0_D4,		IP0SR4_27_24)
#define GPSR4_5		F_(MMC0_SD_D3,		IP0SR4_23_20)
#define GPSR4_4		F_(MMC0_SD_D2,		IP0SR4_19_16)
#define GPSR4_3		F_(MMC0_SD_D1,		IP0SR4_15_12)
#define GPSR4_2		F_(MMC0_SD_D0,		IP0SR4_11_8)
#define GPSR4_1		F_(MMC0_SD_CMD,		IP0SR4_7_4)
#define GPSR4_0		F_(MMC0_SD_CLK,		IP0SR4_3_0)

/* GPSR5 */
#define GPSR5_22	F_(TPU0TO3,		IP2SR5_23_20)
#define GPSR5_21	F_(TPU0TO2,		IP2SR5_15_12)
#define GPSR5_20	F_(TPU0TO1,		IP2SR5_19_16)
#define GPSR5_19	F_(TPU0TO0,		IP2SR5_15_12)
#define GPSR5_18	F_(TCLK4,		IP2SR5_11_8)
#define GPSR5_17	F_(TCLK3,		IP2SR5_7_4)
#define GPSR5_16	F_(TCLK2,		IP2SR5_3_0)
#define GPSR5_15	F_(TCLK1,		IP1SR5_31_28)
#define GPSR5_14	F_(IRQ3,		IP1SR5_27_24)
#define GPSR5_13	F_(IRQ2,		IP1SR5_23_20)
#define GPSR5_12	F_(IRQ1,		IP1SR5_19_16)
#define GPSR5_11	F_(IRQ0,		IP1SR5_15_12)
#define GPSR5_10	F_(HSCK1,		IP1SR5_11_8)
#define GPSR5_9		F_(HCTS1#,		IP1SR5_7_4)
#define GPSR5_8		F_(HRTS1#,		IP1SR5_3_0)
#define GPSR5_7		F_(HRX1,		IP0SR5_31_28)
#define GPSR5_6		F_(HTX1,		IP0SR5_27_24)
#define GPSR5_5		F_(SCIF_CLK0,		IP0SR5_23_20)
#define GPSR5_4		F_(HSCK0,		IP0SR5_19_16)
#define GPSR5_3		F_(HCTS0#,		IP0SR5_15_12)
#define GPSR5_2		F_(HRTS0#,		IP0SR5_11_8)
#define GPSR5_1		F_(HRX0,		IP0SR5_7_4)
#define GPSR5_0		F_(HTX0,		IP0SR5_3_0)

/* GPSR6 */
#define GPSR6_30	F_(AUDIO1_CLKOUT1,	IP3SR6_27_24)
#define GPSR6_29	F_(AUDIO1_CLKOUT0,	IP3SR6_23_20)
#define GPSR6_28	F_(SSI2_SD,		IP3SR6_19_16)
#define GPSR6_27	F_(SSI2_WS,		IP3SR6_15_12)
#define GPSR6_26	F_(SSI2_SCK,		IP3SR6_11_8)
#define GPSR6_25	F_(AUDIO0_CLKOUT3,	IP3SR6_7_4)
#define GPSR6_24	F_(AUDIO0_CLKOUT2,	IP3SR6_3_0)
#define GPSR6_23	F_(SSI1_SD,		IP2SR6_31_28)
#define GPSR6_22	F_(SSI1_WS,		IP2SR6_27_24)
#define GPSR6_21	F_(SSI1_SCK,		IP2SR6_23_20)
#define GPSR6_20	F_(AUDIO0_CLKOUT1,	IP2SR6_19_16)
#define GPSR6_19	F_(AUDIO0_CLKOUT0,	IP2SR6_15_12)
#define GPSR6_18	F_(SSI0_SD,		IP2SR6_11_8)
#define GPSR6_17	F_(SSI0_WS,		IP2SR6_7_4)
#define GPSR6_16	F_(SSI0_SCK,		IP2SR6_3_0)
#define GPSR6_15	F_(MSIOF4_SS2,		IP1SR6_31_28)
#define GPSR6_14	F_(MSIOF4_SS1,		IP1SR6_27_24)
#define GPSR6_13	F_(MSIOF4_SYNC,		IP1SR6_23_20)
#define GPSR6_12	F_(MSIOF4_RXD,		IP1SR6_19_16)
#define GPSR6_11	F_(MSIOF4_TXD,		IP1SR6_15_12)
#define GPSR6_10	F_(MSIOF4_SCK,		IP1SR6_11_8)
#define GPSR6_9		F_(MSIOF7_SS2,		IP1SR6_7_4)
#define GPSR6_8		F_(MSIOF7_SS1,		IP1SR6_3_0)
#define GPSR6_7		F_(MSIOF7_SYNC,		IP0SR6_31_28)
#define GPSR6_6		F_(MSIOF7_RXD,		IP0SR6_27_24)
#define GPSR6_5		F_(MSIOF7_TXD,		IP0SR6_23_20)
#define GPSR6_4		F_(MSIOF7_SCK,		IP0SR6_19_16)
#define GPSR6_3		F_(RIF6_CLK,		IP0SR6_15_12)
#define GPSR6_2		F_(RIF6_SYNC,		IP0SR6_11_8)
#define GPSR6_1		F_(RIF6_D1,		IP0SR6_7_4)
#define GPSR6_0		F_(RIF6_D0,		IP0SR6_3_0)

/* GPSR7 */
#define GPSR7_30	F_(RIF0_SYNC,		IP3SR7_27_24)
#define GPSR7_29	F_(SSI10_SD,		IP3SR7_23_20)
#define GPSR7_28	F_(SSI10_WS,		IP3SR7_19_16)
#define GPSR7_27	F_(SSI10_SCK,		IP3SR7_15_12)
#define GPSR7_26	F_(RIF0_D1,		IP3SR7_11_8)
#define GPSR7_25	F_(SSI9_SD,		IP3SR7_7_4)
#define GPSR7_24	F_(SSI9_WS,		IP3SR7_3_0)
#define GPSR7_23	F_(SSI9_SCK,		IP2SR7_31_28)
#define GPSR7_22	F_(RIF0_D0,		IP2SR7_27_24)
#define GPSR7_21	F_(SSI8_SD,		IP2SR7_23_20)
#define GPSR7_20	F_(RIF0_CLK,		IP2SR7_19_16)
#define GPSR7_19	F_(SSI7_SD,		IP2SR7_15_12)
#define GPSR7_18	F_(SSI7_WS,		IP2SR7_11_8)
#define GPSR7_17	F_(SSI7_SCK,		IP2SR7_7_4)
#define GPSR7_16	F_(AUDIO_CLKC,		IP2SR7_3_0)
#define GPSR7_15	F_(SSI6_SD,		IP1SR7_31_28)
#define GPSR7_14	F_(SSI6_WS,		IP1SR7_27_24)
#define GPSR7_13	F_(SSI6_SCK,		IP1SR7_23_20)
#define GPSR7_12	F_(AUDIO_CLKB,		IP1SR7_19_16)
#define GPSR7_11	F_(SSI5_SD,		IP1SR7_15_12)
#define GPSR7_10	F_(SSI5_WS,		IP1SR7_11_8)
#define GPSR7_9		F_(SSI5_SCK,		IP1SR7_7_4)
#define GPSR7_8		F_(AUDIO_CLKA,		IP1SR7_3_0)
#define GPSR7_7		F_(SSI4_SD,		IP0SR7_31_28)
#define GPSR7_6		F_(SSI4_WS,		IP0SR7_27_24)
#define GPSR7_5		F_(SSI4_SCK,		IP0SR7_23_20)
#define GPSR7_4		F_(AUDIO1_CLKOUT3,	IP0SR7_19_16)
#define GPSR7_3		F_(AUDIO1_CLKOUT2,	IP0SR7_15_12)
#define GPSR7_2		F_(SSI3_SD,		IP0SR7_11_8)
#define GPSR7_1		F_(SSI3_WS,		IP0SR7_7_4)
#define GPSR7_0		F_(SSI3_SCK,		IP0SR7_3_0)

/* GPSR8 */
#define GPSR8_21	F_(S3DA2,		IP2SR8_23_20)
#define GPSR8_20	F_(S3CL2,		IP2SR8_19_16)
#define GPSR8_19	F_(S3DA1,		IP2SR8_15_12)
#define GPSR8_18	F_(S3CL1,		IP2SR8_11_8)
#define GPSR8_17	F_(S3DA0,		IP2SR8_7_4)
#define GPSR8_16	F_(S3CL0,		IP2SR8_3_0)
#define GPSR8_15	F_(SDA8,		IP1SR8_31_28)
#define GPSR8_14	F_(SCL8,		IP1SR8_27_24)
#define GPSR8_13	F_(SDA7,		IP1SR8_23_20)
#define GPSR8_12	F_(SCL7,		IP1SR8_19_16)
#define GPSR8_11	F_(SDA6,		IP1SR8_15_12)
#define GPSR8_10	F_(SCL6,		IP1SR8_11_8)
#define GPSR8_9		F_(SDA5,		IP1SR8_7_4)
#define GPSR8_8		F_(SCL5,		IP1SR8_3_0)
#define GPSR8_7		F_(SDA4,		IP0SR8_31_28)
#define GPSR8_6		F_(SCL4,		IP0SR8_27_24)
#define GPSR8_5		F_(SDA3,		IP0SR8_23_20)
#define GPSR8_4		F_(SCL3,		IP0SR8_19_16)
#define GPSR8_3		F_(SDA2,		IP0SR8_15_12)
#define GPSR8_2		F_(SCL2,		IP0SR8_11_8)
#define GPSR8_1		F_(SDA1,		IP0SR8_7_4)
#define GPSR8_0		F_(SCL1,		IP0SR8_3_0)

/* GPSR9 */
#define GPSR9_16	F_(RSW3_MATCH,		IP2SR9_3_0)
#define GPSR9_15	F_(RSW3_CAPTURE,	IP1SR9_31_28)
#define GPSR9_14	F_(RSW3_PPS,		IP1SR9_27_24)
#define GPSR9_13	F_(ETH10G0_PHYINT,	IP1SR9_23_20)
#define GPSR9_12	F_(ETH10G0_LINK,	IP1SR9_19_16)
#define GPSR9_11	F_(EHT10G0_MDC,		IP1SR9_15_12)
#define GPSR9_10	F_(ETH10G0_MDIO,	IP1SR9_11_8)
#define GPSR9_9		F_(ETH25G0_PHYINT,	IP1SR9_7_4)
#define GPSR9_8		F_(ETH25G0_LINK,	IP1SR9_3_0)
#define GPSR9_7		F_(EHT25G0_MDC,		IP0SR9_31_28)
#define GPSR9_6		F_(ETH25G0_MDIO,	IP0SR9_27_24)
#define GPSR9_5		F_(ETHES4_MATCH,	IP0SR9_23_20)
#define GPSR9_4		F_(ETHES4_CAPTURE,	IP0SR9_19_16)
#define GPSR9_3		F_(ETHES4_PPS,		IP0SR9_15_12)
#define GPSR9_2		F_(ETHES0_MATCH,	IP0SR9_11_8)
#define GPSR9_1		F_(ETHES0_CAPTURE,	IP0SR9_7_4)
#define GPSR9_0		F_(ETHES0_PPS,		IP0SR9_3_0)

/* GPSR10 */
#define GPSR10_13	F_(UFS1_CD,		IP1SR10_23_20)
#define GPSR10_12	F_(UFS0_CD,		IP1SR10_19_16)
#define GPSR10_11	F_(PCIE41_CLKREQ#,	IP1SR10_15_12)
#define GPSR10_10	F_(PCIE40_CLKREQ#,	IP1SR10_11_8)
#define GPSR10_9	F_(PCIE61_CLKREQ#,	IP1SR10_7_4)
#define GPSR10_8	F_(PCIE60_CLKREQ#,	IP1SR10_3_0)
#define GPSR10_7	F_(USB3_OVC,		IP0SR10_31_28)
#define GPSR10_6	F_(USB3_PWEN,		IP0SR10_27_24)
#define GPSR10_5	F_(USB2_OVC,		IP0SR10_23_20)
#define GPSR10_4	F_(USB2_PWEN,		IP0SR10_19_16)
#define GPSR10_3	F_(USB1_OVC,		IP0SR10_15_12)
#define GPSR10_2	F_(USB1_PWEN,		IP0SR10_11_8)
#define GPSR10_1	F_(USB0_OVC,		IP0SR10_7_4)
#define GPSR10_0	F_(USB0_PWEN,		IP0SR10_3_0)

/* SR0 */
/* IP0SR0 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR0_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR0 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR0_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR0 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR0_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR0 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP3SR0_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR1 */
/* IP0SR1 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR1_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR1 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR1_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR1_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR1 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR1_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR1_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP3SR1 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP3SR1_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR1_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR2 */
/* IP0SR2 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR2_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR2_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR2 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR2_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR2_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR2 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR2_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR2_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP3SR2 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP3SR2_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR2_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR2_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR2_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR3 */
/* IP0SR3 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR3_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR3_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR3 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR3_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR3_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR3 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR3_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR4 */
/* IP0SR4 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR4_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR4 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR4_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR5 */
/* IP0SR5 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR5_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR5 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR5_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR5_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR5 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR5_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR5_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR6 */
/* IP0SR6 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR6_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR6 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR6_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR6 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR6_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP3SR6 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP3SR6_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR6_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR6_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR6_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR6_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR6_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR6_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR7 */
/* IP0SR7 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR7_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR7 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR7_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR7 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR7_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP3SR7 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP3SR7_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR8 */
/* IP1SR8 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR8_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR8_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR8_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR8_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR8_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR8_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR8_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR8_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR8 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR8_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR8_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR8_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR8_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR8_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR8_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR8_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR8_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP3SR8 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR8_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR8_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR8_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR8_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR8_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR8_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR9 */
/* IP0SR9 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR9_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR9_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR9_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR9_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR9_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR9_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR9_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR9_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR9 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR9_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR9_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR9_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR9_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR9_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR9_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR9_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR9_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR9 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP2SR9_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* SR10 */
/* IP1SR10 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP0SR10_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR10_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR10_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR10_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR10_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR10_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR10_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR10_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR10 */		/* 0 */			/* 1 */			/* 2 */		/* 3		4	 5	  6	   7	    8	     9	      A	       B	C	 D	  E	   F */
#define IP1SR10_3_0	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR10_7_4	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR10_11_8	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR10_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR10_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR10_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

#define PINMUX_GPSR	\
		GPSR1_31	\
		GPSR1_30									GPSR6_30	GPSR7_30	\
		GPSR1_29									GPSR6_29	GPSR7_29	\
		GPSR1_28	GPSR2_28							GPSR6_28	GPSR7_28	\
		GPSR1_27	GPSR2_27							GPSR6_27	GPSR7_27	\
		GPSR1_26	GPSR2_26	                          			GPSR6_26	GPSR7_26	\
		GPSR1_25	GPSR2_25	                              			GPSR6_25	GPSR7_25	\
GPSR0_24	GPSR1_24	GPSR2_24							GPSR6_24	GPSR7_24	\
GPSR0_23	GPSR1_23	GPSR2_23							GPSR6_23	GPSR7_23	\
GPSR0_22	GPSR1_22	GPSR2_22					GPSR5_22	GPSR6_22	GPSR7_22	\
GPSR0_21	GPSR1_21	GPSR2_21					GPSR5_21	GPSR6_21	GPSR7_21	GPSR8_21	\
GPSR0_20	GPSR1_20	GPSR2_20					GPSR5_20	GPSR6_20	GPSR7_20	GPSR8_20	\
GPSR0_19	GPSR1_19	GPSR2_19					GPSR5_19	GPSR6_19	GPSR7_19	GPSR8_19	\
GPSR0_18	GPSR1_18	GPSR2_18					GPSR5_18	GPSR6_18	GPSR7_18	GPSR8_18	\
GPSR0_17	GPSR1_17	GPSR2_17					GPSR5_17	GPSR6_17	GPSR7_17	GPSR8_17	\
GPSR0_16	GPSR1_16	GPSR2_16	GPSR3_16			GPSR5_16	GPSR6_16	GPSR7_16	GPSR8_16	GPSR9_16	\
GPSR0_15	GPSR1_15	GPSR2_15	GPSR3_15			GPSR5_15	GPSR6_15	GPSR7_15	GPSR8_15	GPSR9_15	\
GPSR0_14	GPSR1_14	GPSR2_14	GPSR3_14			GPSR5_14	GPSR6_14	GPSR7_14	GPSR8_14	GPSR9_14	\
GPSR0_13	GPSR1_13	GPSR2_13	GPSR3_13	GPSR4_13	GPSR5_13	GPSR6_13	GPSR7_13	GPSR8_13	GPSR9_13	GPSR10_13	\
GPSR0_12	GPSR1_12	GPSR2_12	GPSR3_12	GPSR4_12	GPSR5_12	GPSR6_12	GPSR7_12	GPSR8_12	GPSR9_12	GPSR10_12	\
GPSR0_11	GPSR1_11	GPSR2_11	GPSR3_11	GPSR4_11	GPSR5_11	GPSR6_11	GPSR7_11	GPSR8_11	GPSR9_11	GPSR10_11	\
GPSR0_10	GPSR1_10	GPSR2_10	GPSR3_10	GPSR4_10	GPSR5_10	GPSR6_10	GPSR7_10	GPSR8_10	GPSR9_10	GPSR10_10	\
GPSR0_9		GPSR1_9		GPSR2_9		GPSR3_9		GPSR4_9		GPSR5_9		GPSR6_9		GPSR7_9		GPSR8_9		GPSR9_9		GPSR10_9	\
GPSR0_8		GPSR1_8		GPSR2_8		GPSR3_8		GPSR4_8		GPSR5_8		GPSR6_8		GPSR7_8		GPSR8_8		GPSR9_8		GPSR10_8	\
GPSR0_7		GPSR1_7		GPSR2_7		GPSR3_7		GPSR4_7		GPSR5_7		GPSR6_7		GPSR7_7		GPSR8_7		GPSR9_7		GPSR10_7	\
GPSR0_6		GPSR1_6		GPSR2_6		GPSR3_6		GPSR4_6		GPSR5_6		GPSR6_6		GPSR7_6		GPSR8_6		GPSR9_6		GPSR10_6	\
GPSR0_5		GPSR1_5		GPSR2_5		GPSR3_5		GPSR4_5		GPSR5_5		GPSR6_5		GPSR7_5		GPSR8_5		GPSR9_5		GPSR10_5	\
GPSR0_4		GPSR1_4		GPSR2_4		GPSR3_4		GPSR4_4		GPSR5_4		GPSR6_4		GPSR7_4		GPSR8_4		GPSR9_4		GPSR10_4	\
GPSR0_3		GPSR1_3		GPSR2_3		GPSR3_3		GPSR4_3		GPSR5_3		GPSR6_3		GPSR7_3		GPSR8_3		GPSR9_3		GPSR10_3	\
GPSR0_2		GPSR1_2		GPSR2_2		GPSR3_2		GPSR4_2		GPSR5_2		GPSR6_2		GPSR7_2		GPSR8_2		GPSR9_2		GPSR10_2	\
GPSR0_1		GPSR1_1		GPSR2_1		GPSR3_1		GPSR4_1		GPSR5_1		GPSR6_1		GPSR7_1		GPSR8_1		GPSR9_1		GPSR10_1	\
GPSR0_0		GPSR1_0		GPSR2_0		GPSR3_0		GPSR4_0         GPSR5_0		GPSR6_0		GPSR7_0		GPSR8_0		GPSR9_0		GPSR10_0

#define PINMUX_IPSR	\
\
FM(IP0SR0_3_0)		IP0SR0_3_0	FM(IP1SR0_3_0)		IP1SR0_3_0	FM(IP2SR0_3_0)		IP2SR0_3_0	FM(IP3SR0_3_0)		IP3SR0_3_0	\
FM(IP0SR0_7_4)		IP0SR0_7_4	FM(IP1SR0_7_4)		IP1SR0_7_4	FM(IP2SR0_7_4)		IP2SR0_7_4	\
FM(IP0SR0_11_8)		IP0SR0_11_8	FM(IP1SR0_11_8)		IP1SR0_11_8	FM(IP2SR0_11_8)		IP2SR0_11_8	\
FM(IP0SR0_15_12)	IP0SR0_15_12	FM(IP1SR0_15_12)	IP1SR0_15_12	FM(IP2SR0_15_12)	IP2SR0_15_12	\
FM(IP0SR0_19_16)	IP0SR0_19_16	FM(IP1SR0_19_16)	IP1SR0_19_16	FM(IP2SR0_19_16)	IP2SR0_19_16	\
FM(IP0SR0_23_20)	IP0SR0_23_20	FM(IP1SR0_23_20)	IP1SR0_23_20	FM(IP2SR0_23_20)	IP2SR0_23_20	\
FM(IP0SR0_27_24)	IP0SR0_27_24	FM(IP1SR0_27_24)	IP1SR0_27_24	FM(IP2SR0_27_24)	IP2SR0_27_24	\
FM(IP0SR0_31_28)	IP0SR0_31_28	FM(IP1SR0_31_28)	IP1SR0_31_28	FM(IP2SR0_31_28)	IP2SR0_31_28	\
\
FM(IP0SR1_3_0)		IP0SR1_3_0	FM(IP1SR1_3_0)		IP1SR1_3_0	FM(IP2SR1_3_0)		IP2SR1_3_0	FM(IP3SR1_3_0)		IP3SR1_3_0	\
FM(IP0SR1_7_4)		IP0SR1_7_4	FM(IP1SR1_7_4)		IP1SR1_7_4	FM(IP2SR1_7_4)		IP2SR1_7_4	FM(IP3SR1_7_4)		IP3SR1_7_4	\
FM(IP0SR1_11_8)		IP0SR1_11_8	FM(IP1SR1_11_8)		IP1SR1_11_8	FM(IP2SR1_11_8)		IP2SR1_11_8	FM(IP3SR1_11_8)		IP3SR1_11_8	\
FM(IP0SR1_15_12)	IP0SR1_15_12	FM(IP1SR1_15_12)	IP1SR1_15_12	FM(IP2SR1_15_12)	IP2SR1_15_12	FM(IP3SR1_15_12)	IP3SR1_15_12	\
FM(IP0SR1_19_16)	IP0SR1_19_16	FM(IP1SR1_19_16)	IP1SR1_19_16	FM(IP2SR1_19_16)	IP2SR1_19_16	FM(IP3SR1_19_16)	IP3SR1_19_16	\
FM(IP0SR1_23_20)	IP0SR1_23_20	FM(IP1SR1_23_20)	IP1SR1_23_20	FM(IP2SR1_23_20)	IP2SR1_23_20	FM(IP3SR1_23_20)	IP3SR1_23_20	\
FM(IP0SR1_27_24)	IP0SR1_27_24	FM(IP1SR1_27_24)	IP1SR1_27_24	FM(IP2SR1_27_24)	IP2SR1_27_24	FM(IP3SR1_27_24)	IP3SR1_27_24	\
FM(IP0SR1_31_28)	IP0SR1_31_28	FM(IP1SR1_31_28)	IP1SR1_31_28	FM(IP2SR1_31_28)	IP2SR1_31_28	FM(IP3SR1_31_28)	IP3SR1_31_28	\
\
FM(IP0SR2_3_0)		IP0SR2_3_0	FM(IP1SR2_3_0)		IP1SR2_3_0	FM(IP2SR2_3_0)		IP2SR2_3_0	FM(IP3SR2_3_0)		IP3SR1_3_0	\
FM(IP0SR2_7_4)		IP0SR2_7_4	FM(IP1SR2_7_4)		IP1SR2_7_4	FM(IP2SR2_7_4)		IP2SR2_7_4	FM(IP3SR2_7_4)		IP3SR1_7_4	\
FM(IP0SR2_11_8)		IP0SR2_11_8	FM(IP1SR2_11_8)		IP1SR2_11_8	FM(IP2SR2_11_8)		IP2SR2_11_8	FM(IP3SR2_11_8)		IP3SR1_11_8	\
FM(IP0SR2_15_12)	IP0SR2_15_12	FM(IP1SR2_15_12)	IP1SR2_15_12	FM(IP2SR2_15_12)	IP2SR2_15_12	FM(IP3SR2_15_12)	IP3SR2_15_12	\
FM(IP0SR2_19_16)	IP0SR2_19_16	FM(IP1SR2_19_16)	IP1SR2_19_16	FM(IP2SR2_19_16)	IP2SR2_19_16	\
FM(IP0SR2_23_20)	IP0SR2_23_20	FM(IP1SR2_23_20)	IP1SR2_23_20	FM(IP2SR2_23_20)	IP2SR2_23_20	\
FM(IP0SR2_27_24)	IP0SR2_27_24	FM(IP1SR2_27_24)	IP1SR2_27_24	FM(IP2SR2_27_24)	IP2SR2_27_24	\
FM(IP0SR2_31_28)	IP0SR2_31_28	FM(IP1SR2_31_28)	IP1SR2_31_28	FM(IP2SR2_31_28)	IP2SR2_31_28	\
\
FM(IP0SR3_3_0)		IP0SR3_3_0	FM(IP1SR3_3_0)		IP1SR3_3_0	FM(IP2SR3_3_0)		IP2SR3_3_0	\
FM(IP0SR3_7_4)		IP0SR3_7_4	FM(IP1SR3_7_4)		IP1SR3_7_4	\
FM(IP0SR3_11_8)		IP0SR3_11_8	FM(IP1SR3_11_8)		IP1SR3_11_8	\
FM(IP0SR3_15_12)	IP0SR3_15_12	FM(IP1SR3_15_12)	IP1SR3_15_12	\
FM(IP0SR3_19_16)	IP0SR3_19_16	FM(IP1SR3_19_16)	IP1SR3_19_16	\
FM(IP0SR3_23_20)	IP0SR3_23_20	FM(IP1SR3_23_20)	IP1SR3_23_20	\
FM(IP0SR3_27_24)	IP0SR3_27_24	FM(IP1SR3_27_24)	IP1SR3_27_24	\
FM(IP0SR3_31_28)	IP0SR3_31_28	FM(IP1SR3_31_28)	IP1SR3_31_28	\
\
FM(IP0SR4_3_0)		IP0SR4_3_0	FM(IP1SR4_3_0)		IP1SR4_3_0	\
FM(IP0SR4_7_4)		IP0SR4_7_4	FM(IP1SR4_7_4)		IP1SR4_7_4	\
FM(IP0SR4_11_8)		IP0SR4_11_8	FM(IP1SR4_11_8)		IP1SR4_11_8	\
FM(IP0SR4_15_12)	IP0SR4_15_12	FM(IP1SR4_15_12)	IP1SR4_15_12	\
FM(IP0SR4_19_16)	IP0SR4_19_16	FM(IP1SR4_19_16)	IP1SR4_19_16	\
FM(IP0SR4_23_20)	IP0SR4_23_20	FM(IP1SR4_23_20)	IP1SR4_23_20	\
FM(IP0SR4_27_24)	IP0SR4_27_24	\
FM(IP0SR4_31_28)	IP0SR4_31_28	\
\
FM(IP0SR5_3_0)		IP0SR5_3_0	FM(IP1SR5_3_0)		IP1SR5_3_0	FM(IP2SR5_3_0)		IP2SR5_3_0	\
FM(IP0SR5_7_4)		IP0SR5_7_4	FM(IP1SR5_7_4)		IP1SR5_7_4	FM(IP2SR5_7_4)		IP2SR5_7_4	\
FM(IP0SR5_11_8)		IP0SR5_11_8	FM(IP1SR5_11_8)		IP1SR5_11_8	FM(IP2SR5_11_8)		IP2SR5_11_8	\
FM(IP0SR5_15_12)	IP0SR5_15_12	FM(IP1SR5_15_12)	IP1SR5_15_12	FM(IP2SR5_15_12)	IP2SR5_15_12	\
FM(IP0SR5_19_16)	IP0SR5_19_16	FM(IP1SR5_19_16)	IP1SR5_19_16	FM(IP2SR5_19_16)	IP2SR5_19_16	\
FM(IP0SR5_23_20)	IP0SR5_23_20	FM(IP1SR5_23_20)	IP1SR5_23_20	FM(IP2SR5_23_20)	IP2SR5_23_20	\
FM(IP0SR5_27_24)	IP0SR5_27_24	FM(IP1SR5_27_24)	IP1SR5_27_24	\
FM(IP0SR5_31_28)	IP0SR5_31_28	FM(IP1SR5_31_28)	IP1SR5_31_28	\
\
FM(IP0SR6_3_0)		IP0SR6_3_0	FM(IP1SR6_3_0)		IP1SR6_3_0	FM(IP2SR6_3_0)		IP2SR6_3_0	FM(IP3SR6_3_0)		IP3SR6_3_0	\
FM(IP0SR6_7_4)		IP0SR6_7_4	FM(IP1SR6_7_4)		IP1SR6_7_4	FM(IP2SR6_7_4)		IP2SR6_7_4	FM(IP3SR6_7_4)		IP3SR6_7_4	\
FM(IP0SR6_11_8)		IP0SR6_11_8	FM(IP1SR6_11_8)		IP1SR6_11_8	FM(IP2SR6_11_8)		IP2SR6_11_8	FM(IP3SR6_11_8)		IP3SR6_11_8	\
FM(IP0SR6_15_12)	IP0SR6_15_12	FM(IP1SR6_15_12)	IP1SR6_15_12	FM(IP2SR6_15_12)	IP2SR6_15_12	FM(IP3SR6_15_12)	IP3SR6_15_12	\
FM(IP0SR6_19_16)	IP0SR6_19_16	FM(IP1SR6_19_16)	IP1SR6_19_16	FM(IP2SR6_19_16)	IP2SR6_19_16	FM(IP3SR6_19_16)	IP3SR6_19_16	\
FM(IP0SR6_23_20)	IP0SR6_23_20	FM(IP1SR6_23_20)	IP1SR6_23_20	FM(IP2SR6_23_20)	IP2SR6_23_20	FM(IP3SR6_23_20)	IP3SR6_23_20	\
FM(IP0SR6_27_24)	IP0SR6_27_24	FM(IP1SR6_27_24)	IP1SR6_27_24	FM(IP2SR6_27_24)	IP2SR6_27_24	FM(IP3SR6_27_24)	IP3SR6_27_24	\
FM(IP0SR6_31_28)	IP0SR6_31_28	FM(IP1SR6_31_28)	IP1SR6_31_28	FM(IP2SR6_31_28)	IP2SR6_31_28	\
\
FM(IP0SR7_3_0)		IP0SR7_3_0	FM(IP1SR7_3_0)		IP1SR7_3_0	FM(IP2SR7_3_0)		IP2SR7_3_0	FM(IP3SR7_3_0)		IP3SR7_3_0	\
FM(IP0SR7_7_4)		IP0SR7_7_4	FM(IP1SR7_7_4)		IP1SR7_7_4	FM(IP2SR7_7_4)		IP2SR7_7_4	FM(IP3SR7_7_4)		IP3SR7_7_4	\
FM(IP0SR7_11_8)		IP0SR7_11_8	FM(IP1SR7_11_8)		IP1SR7_11_8	FM(IP2SR7_11_8)		IP2SR7_11_8	FM(IP3SR7_11_8)		IP3SR7_11_8	\
FM(IP0SR7_15_12)	IP0SR7_15_12	FM(IP1SR7_15_12)	IP1SR7_15_12	FM(IP2SR7_15_12)	IP2SR7_15_12	FM(IP3SR7_15_12)	IP3SR7_15_12	\
FM(IP0SR7_19_16)	IP0SR7_19_16	FM(IP1SR7_19_16)	IP1SR7_19_16	FM(IP2SR7_19_16)	IP2SR7_19_16	FM(IP3SR7_19_16)	IP3SR7_19_16	\
FM(IP0SR7_23_20)	IP0SR7_23_20	FM(IP1SR7_23_20)	IP1SR7_23_20	FM(IP2SR7_23_20)	IP2SR7_23_20	FM(IP3SR7_23_20)	IP3SR7_23_20	\
FM(IP0SR7_27_24)	IP0SR7_27_24	FM(IP1SR7_27_24)	IP1SR7_27_24	FM(IP2SR7_27_24)	IP2SR7_27_24	FM(IP3SR7_27_24)	IP3SR7_27_24	\
FM(IP0SR7_31_28)	IP0SR7_31_28	FM(IP1SR7_31_28)	IP1SR7_31_28	FM(IP2SR7_31_28)	IP2SR7_31_28	\
\
FM(IP0SR8_3_0)		IP0SR8_3_0	FM(IP1SR8_3_0)		IP1SR8_3_0	FM(IP2SR8_3_0)		IP2SR8_3_0	\
FM(IP0SR8_7_4)		IP0SR8_7_4	FM(IP1SR8_7_4)		IP1SR8_7_4	FM(IP2SR8_7_4)		IP2SR8_7_4	\
FM(IP0SR8_11_8)		IP0SR8_11_8	FM(IP1SR8_11_8)		IP1SR8_11_8	FM(IP2SR8_11_8)		IP2SR8_11_8	\
FM(IP0SR8_15_12)	IP0SR8_15_12	FM(IP1SR8_15_12)	IP1SR8_15_12	FM(IP2SR8_15_12)	IP2SR8_15_12	\
FM(IP0SR8_19_16)	IP0SR8_19_16	FM(IP1SR8_19_16)	IP1SR8_19_16	FM(IP2SR8_19_16)	IP2SR8_19_16	\
FM(IP0SR8_23_20)	IP0SR8_23_20	FM(IP1SR8_23_20)	IP1SR8_23_20	FM(IP2SR8_23_20)	IP2SR8_23_20	\
FM(IP0SR8_27_24)	IP0SR8_27_24	FM(IP1SR8_27_24)	IP1SR8_27_24	\
FM(IP0SR8_31_28)	IP0SR8_31_28	FM(IP1SR8_31_28)	IP1SR8_31_28	\
\
FM(IP0SR9_3_0)		IP0SR9_3_0	FM(IP1SR9_3_0)		IP1SR9_3_0	FM(IP2SR9_3_0)		IP2SR9_3_0	\
FM(IP0SR9_7_4)		IP0SR9_7_4	FM(IP1SR9_7_4)		IP1SR9_7_4	\
FM(IP0SR9_11_8)		IP0SR9_11_8	FM(IP1SR9_11_8)		IP1SR9_11_8	\
FM(IP0SR9_15_12)	IP0SR9_15_12	FM(IP1SR9_15_12)	IP1SR9_15_12	\
FM(IP0SR9_19_16)	IP0SR9_19_16	FM(IP1SR9_19_16)	IP1SR9_19_16	\
FM(IP0SR9_23_20)	IP0SR9_23_20	FM(IP1SR9_23_20)	IP1SR9_23_20	\
FM(IP0SR9_27_24)	IP0SR9_27_24	FM(IP1SR9_27_24)	IP1SR9_27_24	\
FM(IP0SR9_31_28)	IP0SR9_31_28	FM(IP1SR9_31_28)	IP1SR9_31_28	\
\
FM(IP0SR10_3_0)		IP0SR10_3_0	FM(IP1SR10_3_0)		IP1SR10_3_0	\
FM(IP0SR10_7_4)		IP0SR10_7_4	FM(IP1SR10_7_4)		IP1SR10_7_4	\
FM(IP0SR10_11_8)	IP0SR10_11_8	FM(IP1SR10_11_8)	IP1SR10_11_8	\
FM(IP0SR10_15_12)	IP0SR10_15_12	FM(IP1SR10_15_12)	IP1SR10_15_12	\
FM(IP0SR10_19_16)	IP0SR10_19_16	FM(IP1SR10_19_16)	IP1SR10_19_16	\
FM(IP0SR10_23_20)	IP0SR10_23_20	FM(IP1SR10_23_20)	IP1SR10_23_20	\
FM(IP0SR10_27_24)	IP0SR10_27_24	\
FM(IP0SR10_31_28)	IP0SR10_31_28	\

enum {
	PINMUX_RESERVED = 0,

	PINMUX_DATA_BEGIN,
	GP_ALL(DATA),
	PINMUX_DATA_END,

#define F_(x, y)
#define FM(x)   FN_##x,
	PINMUX_FUNCTION_BEGIN,
	GP_ALL(FN),
	PINMUX_GPSR
	PINMUX_IPSR
	PINMUX_FUNCTION_END,
#undef F_
#undef FM

#define F_(x, y)
#define FM(x)	x##_MARK,
	PINMUX_MARK_BEGIN,
	PINMUX_GPSR
	PINMUX_IPSR
	PINMUX_MARK_END,
#undef F_
#undef FM
};

static const u16 pinmux_data[] = {
	PINMUX_DATA_GP_ALL(),
};

/*
 * Pins not associated with a GPIO port.
 */
enum {
	GP_ASSIGN_LAST(),
	//NOGP_ALL(),
};

static const struct sh_pfc_pin pinmux_pins[] = {
	PINMUX_GPIO_GP_ALL(),
	//PINMUX_NOGP_ALL(),
};

static const struct sh_pfc_pin_group pinmux_groups[] = {
};

static const struct sh_pfc_function pinmux_functions[] = {
};

static const struct pinmux_cfg_reg pinmux_config_regs[] = {
#define F_(x, y)	FN_##y
#define FM(x)		FN_##x
	{ PINMUX_CFG_REG("GPSR0", 0xC1080040, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		GP_0_24_FN, 	GPSR0_24,
		GP_0_23_FN, 	GPSR0_23,
		GP_0_22_FN, 	GPSR0_22,
		GP_0_21_FN, 	GPSR0_21,
		GP_0_20_FN, 	GPSR0_20,
		GP_0_19_FN, 	GPSR0_19,
		GP_0_18_FN,	GPSR0_18,
		GP_0_17_FN,	GPSR0_17,
		GP_0_16_FN,	GPSR0_16,
		GP_0_15_FN,	GPSR0_15,
		GP_0_14_FN,	GPSR0_14,
		GP_0_13_FN,	GPSR0_13,
		GP_0_12_FN,	GPSR0_12,
		GP_0_11_FN,	GPSR0_11,
		GP_0_10_FN,	GPSR0_10,
		GP_0_9_FN,	GPSR0_9,
		GP_0_8_FN,	GPSR0_8,
		GP_0_7_FN,	GPSR0_7,
		GP_0_6_FN,	GPSR0_6,
		GP_0_5_FN,	GPSR0_5,
		GP_0_4_FN,	GPSR0_4,
		GP_0_3_FN,	GPSR0_3,
		GP_0_2_FN,	GPSR0_2,
		GP_0_1_FN,	GPSR0_1,
		GP_0_0_FN,	GPSR0_0, ))
	},
	{ PINMUX_CFG_REG("GPSR1", 0xC1080840, 32, 1, GROUP(
		GP_1_31_FN, 	GPSR1_31,
		GP_1_30_FN, 	GPSR1_30,
		GP_1_29_FN,	GPSR1_29,
		GP_1_28_FN,	GPSR1_28,
		GP_1_27_FN,	GPSR1_27,
		GP_1_26_FN,	GPSR1_26,
		GP_1_25_FN,	GPSR1_25,
		GP_1_24_FN,	GPSR1_24,
		GP_1_23_FN,	GPSR1_23,
		GP_1_22_FN,	GPSR1_22,
		GP_1_21_FN,	GPSR1_21,
		GP_1_20_FN,	GPSR1_20,
		GP_1_19_FN,	GPSR1_19,
		GP_1_18_FN,	GPSR1_18,
		GP_1_17_FN,	GPSR1_17,
		GP_1_16_FN,	GPSR1_16,
		GP_1_15_FN,	GPSR1_15,
		GP_1_14_FN,	GPSR1_14,
		GP_1_13_FN,	GPSR1_13,
		GP_1_12_FN,	GPSR1_12,
		GP_1_11_FN,	GPSR1_11,
		GP_1_10_FN,	GPSR1_10,
		GP_1_9_FN,	GPSR1_9,
		GP_1_8_FN,	GPSR1_8,
		GP_1_7_FN,	GPSR1_7,
		GP_1_6_FN,	GPSR1_6,
		GP_1_5_FN,	GPSR1_5,
		GP_1_4_FN,	GPSR1_4,
		GP_1_3_FN,	GPSR1_3,
		GP_1_2_FN,	GPSR1_2,
		GP_1_1_FN,	GPSR1_1,
		GP_1_0_FN,	GPSR1_0, ))
	},
	{ PINMUX_CFG_REG("GPSR2", 0xC1081040, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		GP_2_28_FN,	GPSR2_28,
		GP_2_27_FN,	GPSR2_27,
		GP_2_26_FN,	GPSR2_26,
		GP_2_25_FN,	GPSR2_25,
		GP_2_24_FN,	GPSR2_24,
		GP_2_23_FN,	GPSR2_23,
		GP_2_22_FN,	GPSR2_22,
		GP_2_21_FN,	GPSR2_21,
		GP_2_20_FN,	GPSR2_20,
		GP_2_19_FN,	GPSR2_19,
		GP_2_18_FN,	GPSR2_18,
		GP_2_17_FN,	GPSR2_17,
		GP_2_16_FN,	GPSR2_16,
		GP_2_15_FN,	GPSR2_15,
		GP_2_14_FN,	GPSR2_14,
		GP_2_13_FN,	GPSR2_13,
		GP_2_12_FN,	GPSR2_12,
		GP_2_11_FN,	GPSR2_11,
		GP_2_10_FN,	GPSR2_10,
		GP_2_9_FN,	GPSR2_9,
		GP_2_8_FN,	GPSR2_8,
		GP_2_7_FN,	GPSR2_7,
		GP_2_6_FN,	GPSR2_6,
		GP_2_5_FN,	GPSR2_5,
		GP_2_4_FN,	GPSR2_4,
		GP_2_3_FN,	GPSR2_3,
		GP_2_2_FN,	GPSR2_2,
		GP_2_1_FN,	GPSR2_1,
		GP_2_0_FN,	GPSR2_0, ))
	},
	{ PINMUX_CFG_REG("GPSR3", 0xC0800040, 32, 1, GROUP(
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
		GP_3_16_FN,	GPSR3_16,
		GP_3_15_FN,	GPSR3_15,
		GP_3_14_FN,	GPSR3_14,
		GP_3_13_FN,	GPSR3_13,
		GP_3_12_FN,	GPSR3_12,
		GP_3_11_FN,	GPSR3_11,
		GP_3_10_FN,	GPSR3_10,
		GP_3_9_FN,	GPSR3_9,
		GP_3_8_FN,	GPSR3_8,
		GP_3_7_FN,	GPSR3_7,
		GP_3_6_FN,	GPSR3_6,
		GP_3_5_FN,	GPSR3_5,
		GP_3_4_FN,	GPSR3_4,
		GP_3_3_FN,	GPSR3_3,
		GP_3_2_FN,	GPSR3_2,
		GP_3_1_FN,	GPSR3_1,
		GP_3_0_FN,	GPSR3_0, ))
	},
	{ PINMUX_CFG_REG("GPSR4", 0xC0800840, 32, 1, GROUP(
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
		GP_4_13_FN,	GPSR4_13,
		GP_4_12_FN,	GPSR4_12,
		GP_4_11_FN,	GPSR4_11,
		GP_4_10_FN,	GPSR4_10,
		GP_4_9_FN,	GPSR4_9,
		GP_4_8_FN,	GPSR4_8,
		GP_4_7_FN,	GPSR4_7,
		GP_4_6_FN,	GPSR4_6,
		GP_4_5_FN,	GPSR4_5,
		GP_4_4_FN,	GPSR4_4,
		GP_4_3_FN,	GPSR4_3,
		GP_4_2_FN,	GPSR4_2,
		GP_4_1_FN,	GPSR4_1,
		GP_4_0_FN,	GPSR4_0, ))
	},
	{ PINMUX_CFG_REG("GPSR5", 0xC0400040, 32, 1, GROUP(
		0, 	0,
		0, 	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		GP_5_22_FN,	GPSR5_22,
		GP_5_21_FN,	GPSR5_21,
		GP_5_20_FN,	GPSR5_20,
		GP_5_19_FN,	GPSR5_19,
		GP_5_18_FN,	GPSR5_18,
		GP_5_17_FN,	GPSR5_17,
		GP_5_16_FN,	GPSR5_16,
		GP_5_15_FN,	GPSR5_15,
		GP_5_14_FN,	GPSR5_14,
		GP_5_13_FN,	GPSR5_13,
		GP_5_12_FN,	GPSR5_12,
		GP_5_11_FN,	GPSR5_11,
		GP_5_10_FN,	GPSR5_10,
		GP_5_9_FN,	GPSR5_9,
		GP_5_8_FN,	GPSR5_8,
		GP_5_7_FN,	GPSR5_7,
		GP_5_6_FN,	GPSR5_6,
		GP_5_5_FN,	GPSR5_5,
		GP_5_4_FN,	GPSR5_4,
		GP_5_3_FN,	GPSR5_3,
		GP_5_2_FN,	GPSR5_2,
		GP_5_1_FN,	GPSR5_1,
		GP_5_0_FN,	GPSR5_0, ))
	},
	{ PINMUX_CFG_REG("GPSR6", 0xC0400840, 32, 1, GROUP(
		0, 	0,
		GP_6_30_FN, 	GPSR6_30,
		GP_6_29_FN,	GPSR6_29,
		GP_6_28_FN,	GPSR6_28,
		GP_6_27_FN,	GPSR6_27,
		GP_6_26_FN,	GPSR6_26,
		GP_6_25_FN,	GPSR6_25,
		GP_6_24_FN,	GPSR6_24,
		GP_6_23_FN,	GPSR6_23,
		GP_6_22_FN,	GPSR6_22,
		GP_6_21_FN,	GPSR6_21,
		GP_6_20_FN,	GPSR6_20,
		GP_6_19_FN,	GPSR6_19,
		GP_6_18_FN,	GPSR6_18,
		GP_6_17_FN,	GPSR6_17,
		GP_6_16_FN,	GPSR6_16,
		GP_6_15_FN,	GPSR6_15,
		GP_6_14_FN,	GPSR6_14,
		GP_6_13_FN,	GPSR6_13,
		GP_6_12_FN,	GPSR6_12,
		GP_6_11_FN,	GPSR6_11,
		GP_6_10_FN,	GPSR6_10,
		GP_6_9_FN,	GPSR6_9,
		GP_6_8_FN,	GPSR6_8,
		GP_6_7_FN,	GPSR6_7,
		GP_6_6_FN,	GPSR6_6,
		GP_6_5_FN,	GPSR6_5,
		GP_6_4_FN,	GPSR6_4,
		GP_6_3_FN,	GPSR6_3,
		GP_6_2_FN,	GPSR6_2,
		GP_6_1_FN,	GPSR6_1,
		GP_6_0_FN,	GPSR6_0, ))
	},
	{ PINMUX_CFG_REG("GPSR7", 0xC0401040, 32, 1, GROUP(
		0, 	0,
		GP_7_30_FN, 	GPSR7_30,
		GP_7_29_FN,	GPSR7_29,
		GP_7_28_FN,	GPSR7_28,
		GP_7_27_FN,	GPSR7_27,
		GP_7_26_FN,	GPSR7_26,
		GP_7_25_FN,	GPSR7_25,
		GP_7_24_FN,	GPSR7_24,
		GP_7_23_FN,	GPSR7_23,
		GP_7_22_FN,	GPSR7_22,
		GP_7_21_FN,	GPSR7_21,
		GP_7_20_FN,	GPSR7_20,
		GP_7_19_FN,	GPSR7_19,
		GP_7_18_FN,	GPSR7_18,
		GP_7_17_FN,	GPSR7_17,
		GP_7_16_FN,	GPSR7_16,
		GP_7_15_FN,	GPSR7_15,
		GP_7_14_FN,	GPSR7_14,
		GP_7_13_FN,	GPSR7_13,
		GP_7_12_FN,	GPSR7_12,
		GP_7_11_FN,	GPSR7_11,
		GP_7_10_FN,	GPSR7_10,
		GP_7_9_FN,	GPSR7_9,
		GP_7_8_FN,	GPSR7_8,
		GP_7_7_FN,	GPSR7_7,
		GP_7_6_FN,	GPSR7_6,
		GP_7_5_FN,	GPSR7_5,
		GP_7_4_FN,	GPSR7_4,
		GP_7_3_FN,	GPSR7_3,
		GP_7_2_FN,	GPSR7_2,
		GP_7_1_FN,	GPSR7_1,
		GP_7_0_FN,	GPSR7_0, ))
	},
	{ PINMUX_CFG_REG("GPSR8", 0xC0401840, 32, 1, GROUP(
		0, 	0,
		0, 	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		GP_1_21_FN,	GPSR1_21,
		GP_1_20_FN,	GPSR1_20,
		GP_1_19_FN,	GPSR1_19,
		GP_1_18_FN,	GPSR1_18,
		GP_1_17_FN,	GPSR1_17,
		GP_1_16_FN,	GPSR1_16,
		GP_1_15_FN,	GPSR1_15,
		GP_1_14_FN,	GPSR1_14,
		GP_1_13_FN,	GPSR1_13,
		GP_1_12_FN,	GPSR1_12,
		GP_1_11_FN,	GPSR1_11,
		GP_1_10_FN,	GPSR1_10,
		GP_1_9_FN,	GPSR1_9,
		GP_1_8_FN,	GPSR1_8,
		GP_1_7_FN,	GPSR1_7,
		GP_1_6_FN,	GPSR1_6,
		GP_1_5_FN,	GPSR1_5,
		GP_1_4_FN,	GPSR1_4,
		GP_1_3_FN,	GPSR1_3,
		GP_1_2_FN,	GPSR1_2,
		GP_1_1_FN,	GPSR1_1,
		GP_1_0_FN,	GPSR1_0, ))
	},
	{ PINMUX_CFG_REG("GPSR9", 0xC9B00040, 32, 1, GROUP(
		0, 	0,
		0, 	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		GP_9_16_FN,	GPSR9_16,
		GP_9_15_FN,	GPSR9_15,
		GP_9_14_FN,	GPSR9_14,
		GP_9_13_FN,	GPSR9_13,
		GP_9_12_FN,	GPSR9_12,
		GP_9_11_FN,	GPSR9_11,
		GP_9_10_FN,	GPSR9_10,
		GP_9_9_FN,	GPSR9_9,
		GP_9_8_FN,	GPSR9_8,
		GP_9_7_FN,	GPSR9_7,
		GP_9_6_FN,	GPSR9_6,
		GP_9_5_FN,	GPSR9_5,
		GP_9_4_FN,	GPSR9_4,
		GP_9_3_FN,	GPSR9_3,
		GP_9_2_FN,	GPSR9_2,
		GP_9_1_FN,	GPSR9_1,
		GP_9_0_FN,	GPSR9_0, ))
	},
	{ PINMUX_CFG_REG("GPSR10", 0xC9B00840, 32, 1, GROUP(
		0, 	0,
		0, 	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		0,	0,
		GP_10_13_FN,	GPSR10_13,
		GP_10_12_FN,	GPSR10_12,
		GP_10_11_FN,	GPSR10_11,
		GP_10_10_FN,	GPSR10_10,
		GP_10_9_FN,	GPSR10_9,
		GP_10_8_FN,	GPSR10_8,
		GP_10_7_FN,	GPSR10_7,
		GP_10_6_FN,	GPSR10_6,
		GP_10_5_FN,	GPSR10_5,
		GP_10_4_FN,	GPSR10_4,
		GP_10_3_FN,	GPSR10_3,
		GP_10_2_FN,	GPSR10_2,
		GP_10_1_FN,	GPSR10_1,
		GP_10_0_FN,	GPSR10_0, ))
	},
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG("IP0SR0", 0xC1080060, 32, 4, GROUP(
		IP0SR0_31_28
		IP0SR0_27_24
		IP0SR0_23_20
		IP0SR0_19_16
		IP0SR0_15_12
		IP0SR0_11_8
		IP0SR0_7_4
		IP0SR0_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR0", 0xC1080064, 32, 4, GROUP(
		IP1SR0_31_28
		IP1SR0_27_24
		IP1SR0_23_20
		IP1SR0_19_16
		IP1SR0_15_12
		IP1SR0_11_8
		IP1SR0_7_4
		IP1SR0_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR0", 0xC1080068, 32, 4, GROUP(
		IP2SR0_31_28
		IP2SR0_27_24
		IP2SR0_23_20
		IP2SR0_19_16
		IP2SR0_15_12
		IP2SR0_11_8
		IP2SR0_7_4
		IP2SR0_3_0))
	},
	{ PINMUX_CFG_REG("IP3SR0", 0xC108006C, 32, 4, GROUP(
		/* IP3SR0_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR0_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR0_23_20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR0_19_16 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR0_15_12 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR0_11_8 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR0_7_4 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP3SR0_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR1", 0xC1080860, 32, 4, GROUP(
		IP0SR1_31_28
		IP0SR1_27_24
		IP0SR1_23_20
		IP0SR1_19_16
		IP0SR1_15_12
		IP0SR1_11_8
		IP0SR1_7_4
		IP0SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR1", 0xC1080864, 32, 4, GROUP(
		IP1SR1_31_28
		IP1SR1_27_24
		IP1SR1_23_20
		IP1SR1_19_16
		IP1SR1_15_12
		IP1SR1_11_8
		IP1SR1_7_4
		IP1SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR1", 0xC1080868, 32, 4, GROUP(
		IP2SR1_31_28
		IP2SR1_27_24
		IP2SR1_23_20
		IP2SR1_19_16
		IP2SR1_15_12
		IP2SR1_11_8
		IP2SR1_7_4
		IP2SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP3SR1", 0xC108086C, 32, 4, GROUP(
		IP3SR1_31_28
		IP3SR1_27_24
		IP3SR1_23_20
		IP3SR1_19_16
		IP3SR1_15_12
		IP3SR1_11_8
		IP3SR1_7_4
		IP3SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR2", 0xC1081060, 32, 4, GROUP(
		IP0SR2_31_28
		IP0SR2_27_24
		IP0SR2_23_20
		IP0SR2_19_16
		IP0SR2_15_12
		IP0SR2_11_8
		IP0SR2_7_4
		IP0SR2_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR2", 0xC1081064, 32, 4, GROUP(
		IP1SR2_31_28
		IP1SR2_27_24
		IP1SR2_23_20
		IP1SR2_19_16
		IP1SR2_15_12
		IP1SR2_11_8
		IP1SR2_7_4
		IP1SR2_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR2", 0xC1081068, 32, 4, GROUP(
		IP1SR2_31_28
		IP1SR2_27_24
		IP1SR2_23_20
		IP1SR2_19_16
		IP1SR2_15_12
		IP1SR2_11_8
		IP1SR2_7_4
		IP1SR2_3_0))
	},
	{ PINMUX_CFG_REG("IP3SR2", 0xC108106C, 32, 4, GROUP(
		/* IP3SR2_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR2_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR2_23_20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR2_19_16 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP3SR2_15_12
		IP3SR2_11_8
		IP3SR2_7_4
		IP3SR2_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR3", 0xC0800060, 32, 4, GROUP(
		IP0SR3_31_28
		IP0SR3_27_24
		IP0SR3_23_20
		IP0SR3_19_16
		IP0SR3_15_12
		IP0SR3_11_8
		IP0SR3_7_4
		IP0SR3_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR3", 0xC0800064, 32, 4, GROUP(
		IP1SR3_31_28
		IP1SR3_27_24
		IP1SR3_23_20
		IP1SR3_19_16
		IP1SR3_15_12
		IP1SR3_11_8
		IP1SR3_7_4
		IP1SR3_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR3", 0xC0800068, 32, 4, GROUP(
		/* IP3SR3_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR3_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR3_23_20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR3_19_16 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR3_15_12 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR3_11_8 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR3_7_4 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP2SR3_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR4", 0xC0800860, 32, 4, GROUP(
		IP0SR4_31_28
		IP0SR4_27_24
		IP0SR4_23_20
		IP0SR4_19_16
		IP0SR4_15_12
		IP0SR4_11_8
		IP0SR4_7_4
		IP0SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR4", 0xC0800864, 32, 4, GROUP(
		/* IP3SR4_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR4_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP1SR4_23_20
		IP1SR4_19_16
		IP1SR4_15_12
		IP1SR4_11_8
		IP1SR4_7_4
		IP1SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR5", 0xC0400060, 32, 4, GROUP(
		IP0SR5_31_28
		IP0SR5_27_24
		IP0SR5_23_20
		IP0SR5_19_16
		IP0SR5_15_12
		IP0SR5_11_8
		IP0SR5_7_4
		IP0SR5_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR5", 0xC0400064, 32, 4, GROUP(
		IP1SR5_31_28
		IP1SR5_27_24
		IP1SR5_23_20
		IP1SR5_19_16
		IP1SR5_15_12
		IP1SR5_11_8
		IP1SR5_7_4
		IP1SR5_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR5", 0xC0400068, 32, 4, GROUP(
		/* IP2SR5_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP2SR5_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP2SR5_23_20
		IP2SR5_19_16
		IP2SR5_15_12
		IP2SR5_11_8
		IP2SR5_7_4
		IP2SR5_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR6", 0xC0400860, 32, 4, GROUP(
		IP0SR6_31_28
		IP0SR6_27_24
		IP0SR6_23_20
		IP0SR6_19_16
		IP0SR6_15_12
		IP0SR6_11_8
		IP0SR6_7_4
		IP0SR6_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR6", 0xC0400864, 32, 4, GROUP(
		IP1SR6_31_28
		IP1SR6_27_24
		IP1SR6_23_20
		IP1SR6_19_16
		IP1SR6_15_12
		IP1SR6_11_8
		IP1SR6_7_4
		IP1SR6_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR6", 0xC0400868, 32, 4, GROUP(
		IP2SR6_31_28
		IP2SR6_27_24
		IP2SR6_23_20
		IP2SR6_19_16
		IP2SR6_15_12
		IP2SR6_11_8
		IP2SR6_7_4
		IP2SR6_3_0))
	},
	{ PINMUX_CFG_REG("IP3SR6", 0xC040086C, 32, 4, GROUP(
		/* IP3SR6_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR6_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP3SR6_23_20
		IP3SR6_19_16
		IP3SR6_15_12
		IP3SR6_11_8
		IP3SR6_7_4
		IP3SR6_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR7", 0xC0401060, 32, 4, GROUP(
		IP0SR7_31_28
		IP0SR7_27_24
		IP0SR7_23_20
		IP0SR7_19_16
		IP0SR7_15_12
		IP0SR7_11_8
		IP0SR7_7_4
		IP0SR7_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR7", 0xC0401064, 32, 4, GROUP(
		IP1SR7_31_28
		IP1SR7_27_24
		IP1SR7_23_20
		IP1SR7_19_16
		IP1SR7_15_12
		IP1SR7_11_8
		IP1SR7_7_4
		IP1SR7_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR7", 0xC0401068, 32, 4, GROUP(
		IP2SR7_31_28
		IP2SR7_27_24
		IP2SR7_23_20
		IP2SR7_19_16
		IP2SR7_15_12
		IP2SR7_11_8
		IP2SR7_7_4
		IP2SR7_3_0))
	},
	{ PINMUX_CFG_REG("IP3SR7", 0xC040106C, 32, 4, GROUP(
		/* IP3SR7_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP3SR7_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP3SR7_23_20
		IP3SR7_19_16
		IP3SR7_15_12
		IP3SR7_11_8
		IP3SR7_7_4
		IP3SR7_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR8", 0xC0401860, 32, 4, GROUP(
		IP0SR8_31_28
		IP0SR8_27_24
		IP0SR8_23_20
		IP0SR8_19_16
		IP0SR8_15_12
		IP0SR8_11_8
		IP0SR8_7_4
		IP0SR8_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR8", 0xC0401864, 32, 4, GROUP(
		IP1SR8_31_28
		IP1SR8_27_24
		IP1SR8_23_20
		IP1SR8_19_16
		IP1SR8_15_12
		IP1SR8_11_8
		IP1SR8_7_4
		IP1SR8_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR8", 0xC0401868, 32, 4, GROUP(
		/* IP2SR8_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP2SR8_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP2SR8_23_20
		IP2SR8_19_16
		IP2SR8_15_12
		IP2SR8_11_8
		IP2SR8_7_4
		IP2SR8_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR9", 0xC9B00060, 32, 4, GROUP(
		IP0SR9_31_28
		IP0SR9_27_24
		IP0SR9_23_20
		IP0SR9_19_16
		IP0SR9_15_12
		IP0SR9_11_8
		IP0SR9_7_4
		IP0SR9_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR9", 0xC9B00064, 32, 4, GROUP(
		IP1SR9_31_28
		IP1SR9_27_24
		IP1SR9_23_20
		IP1SR9_19_16
		IP1SR9_15_12
		IP1SR9_11_8
		IP1SR9_7_4
		IP1SR9_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR9", 0xC9B00068, 32, 4, GROUP(
		/* IP2SR8_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP2SR8_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP2SR8_23_20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP2SR8_19_16 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP2SR8_15_12 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP2SR8_11_8 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP2SR8_7_4 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP2SR9_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR10", 0xC9B00860, 32, 4, GROUP(
		IP0SR10_31_28
		IP0SR10_27_24
		IP0SR10_23_20
		IP0SR10_19_16
		IP0SR10_15_12
		IP0SR10_11_8
		IP0SR10_7_4
		IP0SR10_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR10", 0xC9B00064, 32, 4, GROUP(
		/* IP1SR10_31_28 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* IP1SR10_27_24 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		IP1SR10_23_20
		IP1SR10_19_16
		IP1SR10_15_12
		IP1SR10_11_8
		IP1SR10_7_4
		IP1SR10_3_0))
	},
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	/*{ PINMUX_CFG_REG_VAR("MOD_SEL4", 0xE6060100, 32,
			     GROUP(4, 4, 4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1),
			     GROUP(
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		MOD_SEL4_7
		MOD_SEL4_6
		MOD_SEL4_5
		MOD_SEL4_4
		MOD_SEL4_3
		MOD_SEL4_2
		MOD_SEL4_1
		MOD_SEL4_0))
	},*/
	{ },
};

static const struct pinmux_drive_reg pinmux_drive_regs[] = {
	{ PINMUX_DRIVE_REG("DRV0CTRL0", 0xC1080080) {
		{ RCAR_GP_PIN(0,  7), 28, 3 },	/* MSIOF5_SS2 */
		{ RCAR_GP_PIN(0,  6), 24, 3 },	/* IRQ0 */
		{ RCAR_GP_PIN(0,  5), 20, 3 },	/* IRQ1 */
		{ RCAR_GP_PIN(0,  4), 16, 3 },	/* IRQ2 */
		{ RCAR_GP_PIN(0,  3), 12, 3 },	/* IRQ3 */
		{ RCAR_GP_PIN(0,  2),  8, 3 },	/* GP0_02 */
		{ RCAR_GP_PIN(0,  1),  4, 3 },	/* GP0_01 */
		{ RCAR_GP_PIN(0,  0),  0, 3 },	/* GP0_00 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL0", 0xC1080084) {
		{ RCAR_GP_PIN(0, 15), 28, 3 },	/* MSIOF2_SYNC */
		{ RCAR_GP_PIN(0, 14), 24, 3 },	/* MSIOF2_SS1 */
		{ RCAR_GP_PIN(0, 13), 20, 3 },	/* MSIOF2_SS2 */
		{ RCAR_GP_PIN(0, 12), 16, 3 },	/* MSIOF5_RXD */
		{ RCAR_GP_PIN(0, 11), 12, 3 },	/* MSIOF5_SCK */
		{ RCAR_GP_PIN(0, 10),  8, 3 },	/* MSIOF5_TXD */
		{ RCAR_GP_PIN(0,  9),  4, 3 },	/* MSIOF5_SYNC */
		{ RCAR_GP_PIN(0,  8),  0, 3 },	/* MSIOF5_SS1 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL0", 0xC1080088) {
		{ RCAR_GP_PIN(0, 23),  28, 3 },
		{ RCAR_GP_PIN(0, 22),  24, 3 },
		{ RCAR_GP_PIN(0, 21),  20, 3 },
		{ RCAR_GP_PIN(0, 20),  16, 3 },
		{ RCAR_GP_PIN(0, 19),  12, 3 },
		{ RCAR_GP_PIN(0, 18),  8, 3 },	/* MSIOF2_RXD */
		{ RCAR_GP_PIN(0, 17),  4, 3 },	/* MSIOF2_SCK */
		{ RCAR_GP_PIN(0, 16),  0, 3 },	/* MSIOF2_TXD */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL0", 0xC108008C) {
		{ RCAR_GP_PIN(0, 24),  0, 3 },	/* MSIOF2_TXD */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL1", 0xC1080880) {
		{ RCAR_GP_PIN(1,  7), 28, 3 },	/* MSIOF0_SS1 */
		{ RCAR_GP_PIN(1,  6), 24, 3 },	/* MSIOF0_SS2 */
		{ RCAR_GP_PIN(1,  5), 20, 3 },	/* MSIOF1_RXD */
		{ RCAR_GP_PIN(1,  4), 16, 3 },	/* MSIOF1_TXD */
		{ RCAR_GP_PIN(1,  3), 12, 3 },	/* MSIOF1_SCK */
		{ RCAR_GP_PIN(1,  2),  8, 3 },	/* MSIOF1_SYNC */
		{ RCAR_GP_PIN(1,  1),  4, 3 },	/* MSIOF1_SS1 */
		{ RCAR_GP_PIN(1,  0),  0, 3 },	/* MSIOF1_SS2 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL1", 0xC1080884) {
		{ RCAR_GP_PIN(1, 15), 28, 3 },	/* HSCK0 */
		{ RCAR_GP_PIN(1, 14), 24, 3 },	/* HRTS0_N */
		{ RCAR_GP_PIN(1, 13), 20, 3 },	/* HCTS0_N */
		{ RCAR_GP_PIN(1, 12), 16, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(1, 11), 12, 3 },	/* MSIOF0_RXD */
		{ RCAR_GP_PIN(1, 10),  8, 3 },	/* MSIOF0_SCK */
		{ RCAR_GP_PIN(1,  9),  4, 3 },	/* MSIOF0_TXD */
		{ RCAR_GP_PIN(1,  8),  0, 3 },	/* MSIOF0_SYNC */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL1", 0xC1080888) {
		{ RCAR_GP_PIN(1, 23), 28, 3 },	/* GP1_23 */
		{ RCAR_GP_PIN(1, 22), 24, 3 },	/* AUDIO_CLKIN */
		{ RCAR_GP_PIN(1, 21), 20, 3 },	/* AUDIO_CLKOUT */
		{ RCAR_GP_PIN(1, 20), 16, 3 },	/* SSI_SD */
		{ RCAR_GP_PIN(1, 19), 12, 3 },	/* SSI_WS */
		{ RCAR_GP_PIN(1, 18),  8, 3 },	/* SSI_SCK */
		{ RCAR_GP_PIN(1, 17),  4, 3 },	/* SCIF_CLK */
		{ RCAR_GP_PIN(1, 16),  0, 3 },	/* HRX0 */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL1", 0xC108088C) {
		{ RCAR_GP_PIN(1, 31), 28, 2 },
		{ RCAR_GP_PIN(1, 30), 24, 3 },
		{ RCAR_GP_PIN(1, 29), 20, 2 },	/* ERROROUTC_N */
		{ RCAR_GP_PIN(1, 28), 16, 3 },	/* HTX3 */
		{ RCAR_GP_PIN(1, 27), 12, 3 },	/* HCTS3_N */
		{ RCAR_GP_PIN(1, 26),  8, 3 },	/* HRTS3_N */
		{ RCAR_GP_PIN(1, 25),  4, 3 },	/* HSCK3 */
		{ RCAR_GP_PIN(1, 24),  0, 3 },	/* HRX3 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL2", 0xC1081080) {
		{ RCAR_GP_PIN(2,  7), 28, 3 },	/* TPU0TO1 */
		{ RCAR_GP_PIN(2,  6), 24, 3 },	/* FXR_TXDB */
		{ RCAR_GP_PIN(2,  5), 20, 3 },	/* FXR_TXENB_N */
		{ RCAR_GP_PIN(2,  4), 16, 3 },	/* RXDB_EXTFXR */
		{ RCAR_GP_PIN(2,  3), 12, 3 },	/* CLK_EXTFXR */
		{ RCAR_GP_PIN(2,  2),  8, 3 },	/* RXDA_EXTFXR */
		{ RCAR_GP_PIN(2,  1),  4, 3 },	/* FXR_TXENA_N */
		{ RCAR_GP_PIN(2,  0),  0, 3 },	/* FXR_TXDA */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL2", 0xC1081084) {
		{ RCAR_GP_PIN(2, 15), 28, 3 },	/* CANFD3_RX */
		{ RCAR_GP_PIN(2, 14), 24, 3 },	/* CANFD3_TX */
		{ RCAR_GP_PIN(2, 13), 20, 3 },	/* CANFD2_RX */
		{ RCAR_GP_PIN(2, 12), 16, 3 },	/* CANFD2_TX */
		{ RCAR_GP_PIN(2, 11), 12, 3 },	/* CANFD0_RX */
		{ RCAR_GP_PIN(2, 10),  8, 3 },	/* CANFD0_TX */
		{ RCAR_GP_PIN(2,  9),  4, 3 },	/* CAN_CLK */
		{ RCAR_GP_PIN(2,  8),  0, 3 },	/* TPU0TO0 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL2", 0xC1081088) {
		{ RCAR_GP_PIN(2, 23), 28, 3 },	/* CANFD3_RX */
		{ RCAR_GP_PIN(2, 22), 24, 3 },	/* CANFD3_RX */
		{ RCAR_GP_PIN(2, 21), 20, 3 },	/* CANFD3_TX */
		{ RCAR_GP_PIN(2, 20), 16, 3 },	/* CANFD2_RX */
		{ RCAR_GP_PIN(2, 19), 12, 3 },	/* CANFD2_TX */
		{ RCAR_GP_PIN(2, 18),  8, 3 },	/* CANFD0_RX */
		{ RCAR_GP_PIN(2, 17),  4, 3 },	/* CANFD1_RX */
		{ RCAR_GP_PIN(2, 16),  0, 3 },	/* CANFD1_TX */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL2", 0xC108108C) {
		{ RCAR_GP_PIN(3, 28), 20, 3 },	/* CANFD0_RX */
		{ RCAR_GP_PIN(3, 27), 16, 3 },	/* CANFD1_RX */
		{ RCAR_GP_PIN(3, 26), 12, 3 },	/* CANFD0_RX */
		{ RCAR_GP_PIN(3, 25),  8, 3 },	/* CANFD1_RX */
		{ RCAR_GP_PIN(3, 24),  4, 3 },	/* CANFD1_TX */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL3", 0xC080084) {
		{ RCAR_GP_PIN(3, 15), 28, 3 },
		{ RCAR_GP_PIN(3, 14), 24, 3 },
		{ RCAR_GP_PIN(3, 13), 20, 3 },
		{ RCAR_GP_PIN(3, 12), 16, 3 },
		{ RCAR_GP_PIN(3, 11), 12, 3 },
		{ RCAR_GP_PIN(3, 10),  8, 3 },
		{ RCAR_GP_PIN(3,  9),  4, 3 },
		{ RCAR_GP_PIN(3,  8),  0, 3 },
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL3", 0xC0800088) {
		{ RCAR_GP_PIN(3, 23), 28, 3 },
		{ RCAR_GP_PIN(3, 22), 24, 3 },
		{ RCAR_GP_PIN(3, 21), 20, 3 },
		{ RCAR_GP_PIN(3, 20), 16, 3 },
		{ RCAR_GP_PIN(3, 19), 12, 3 },
		{ RCAR_GP_PIN(3, 18),  8, 3 },
		{ RCAR_GP_PIN(3, 17),  4, 3 },
		{ RCAR_GP_PIN(3, 16),  0, 3 },
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL3", 0xC080008C) {
		{ RCAR_GP_PIN(3, 24),  4, 3 },
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL4", 0xC0800884) {
		{ RCAR_GP_PIN(4, 15), 28, 3 },
		{ RCAR_GP_PIN(4, 14), 24, 3 },
		{ RCAR_GP_PIN(4, 13), 20, 3 },
		{ RCAR_GP_PIN(4, 12), 16, 3 },
		{ RCAR_GP_PIN(4, 11), 12, 3 },
		{ RCAR_GP_PIN(4, 10),  8, 3 },
		{ RCAR_GP_PIN(4,  9),  4, 3 },
		{ RCAR_GP_PIN(4,  8),  0, 3 },
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL4", 0xC0800888) {
		{ RCAR_GP_PIN(4, 21), 20, 3 },
		{ RCAR_GP_PIN(4, 20), 16, 3 },
		{ RCAR_GP_PIN(4, 19), 12, 3 },
		{ RCAR_GP_PIN(4, 18),  8, 3 },
		{ RCAR_GP_PIN(4, 17),  4, 3 },
		{ RCAR_GP_PIN(4, 16),  0, 3 },
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL5", 0xC0400080) {
		{ RCAR_GP_PIN(5,  7), 28, 3 },	/* MSIOF0_SS1 */
		{ RCAR_GP_PIN(5,  6), 24, 3 },	/* MSIOF0_SS2 */
		{ RCAR_GP_PIN(5,  5), 20, 3 },	/* MSIOF1_RXD */
		{ RCAR_GP_PIN(5,  4), 16, 3 },	/* MSIOF1_TXD */
		{ RCAR_GP_PIN(5,  3), 12, 3 },	/* MSIOF1_SCK */
		{ RCAR_GP_PIN(5,  2),  8, 3 },	/* MSIOF1_SYNC */
		{ RCAR_GP_PIN(5,  1),  4, 3 },	/* MSIOF1_SS1 */
		{ RCAR_GP_PIN(5,  0),  0, 3 },	/* MSIOF1_SS2 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL5", 0xC0400084) {
		{ RCAR_GP_PIN(5, 15), 28, 3 },	/* HSCK0 */
		{ RCAR_GP_PIN(5, 14), 24, 3 },	/* HRTS0_N */
		{ RCAR_GP_PIN(5, 13), 20, 3 },	/* HCTS0_N */
		{ RCAR_GP_PIN(5, 12), 16, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(5, 11), 12, 3 },	/* MSIOF0_RXD */
		{ RCAR_GP_PIN(5, 10),  8, 3 },	/* MSIOF0_SCK */
		{ RCAR_GP_PIN(5,  9),  4, 3 },	/* MSIOF0_TXD */
		{ RCAR_GP_PIN(5,  8),  0, 3 },	/* MSIOF0_SYNC */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL5", 0xC0400088) {
		{ RCAR_GP_PIN(5, 22), 24, 3 },	/* AUDIO_CLKIN */
		{ RCAR_GP_PIN(5, 21), 20, 3 },	/* AUDIO_CLKOUT */
		{ RCAR_GP_PIN(5, 20), 16, 3 },	/* SSI_SD */
		{ RCAR_GP_PIN(5, 19), 12, 3 },	/* SSI_WS */
		{ RCAR_GP_PIN(5, 18),  8, 3 },	/* SSI_SCK */
		{ RCAR_GP_PIN(5, 17),  4, 3 },	/* SCIF_CLK */
		{ RCAR_GP_PIN(5, 16),  0, 3 },	/* HRX0 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL6", 0xC0400880) {
		{ RCAR_GP_PIN(6,  7), 28, 3 },	/* MSIOF0_SS1 */
		{ RCAR_GP_PIN(6,  6), 24, 3 },	/* MSIOF0_SS2 */
		{ RCAR_GP_PIN(6,  5), 20, 3 },	/* MSIOF1_RXD */
		{ RCAR_GP_PIN(6,  4), 16, 3 },	/* MSIOF1_TXD */
		{ RCAR_GP_PIN(6,  3), 12, 3 },	/* MSIOF1_SCK */
		{ RCAR_GP_PIN(6,  2),  8, 3 },	/* MSIOF1_SYNC */
		{ RCAR_GP_PIN(6,  1),  4, 3 },	/* MSIOF1_SS1 */
		{ RCAR_GP_PIN(6,  0),  0, 3 },	/* MSIOF1_SS2 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL6", 0xC0400884) {
		{ RCAR_GP_PIN(6, 15), 28, 3 },	/* HSCK0 */
		{ RCAR_GP_PIN(6, 14), 24, 3 },	/* HRTS0_N */
		{ RCAR_GP_PIN(6, 13), 20, 3 },	/* HCTS0_N */
		{ RCAR_GP_PIN(6, 12), 16, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(6, 11), 12, 3 },	/* MSIOF0_RXD */
		{ RCAR_GP_PIN(6, 10),  8, 3 },	/* MSIOF0_SCK */
		{ RCAR_GP_PIN(6,  9),  4, 3 },	/* MSIOF0_TXD */
		{ RCAR_GP_PIN(6,  8),  0, 3 },	/* MSIOF0_SYNC */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL6", 0xC0400888) {
		{ RCAR_GP_PIN(6, 23), 28, 3 },	/* GP1_23 */
		{ RCAR_GP_PIN(6, 22), 24, 3 },	/* AUDIO_CLKIN */
		{ RCAR_GP_PIN(6, 21), 20, 3 },	/* AUDIO_CLKOUT */
		{ RCAR_GP_PIN(6, 20), 16, 3 },	/* SSI_SD */
		{ RCAR_GP_PIN(6, 19), 12, 3 },	/* SSI_WS */
		{ RCAR_GP_PIN(6, 18),  8, 3 },	/* SSI_SCK */
		{ RCAR_GP_PIN(6, 17),  4, 3 },	/* SCIF_CLK */
		{ RCAR_GP_PIN(6, 16),  0, 3 },	/* HRX0 */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL6", 0xC040088C) {
		{ RCAR_GP_PIN(6, 30), 24, 3 },
		{ RCAR_GP_PIN(6, 29), 20, 2 },	/* ERROROUTC_N */
		{ RCAR_GP_PIN(6, 28), 16, 3 },	/* HTX3 */
		{ RCAR_GP_PIN(6, 27), 12, 3 },	/* HCTS3_N */
		{ RCAR_GP_PIN(6, 26),  8, 3 },	/* HRTS3_N */
		{ RCAR_GP_PIN(6, 25),  4, 3 },	/* HSCK3 */
		{ RCAR_GP_PIN(6, 24),  0, 3 },	/* HRX3 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL7", 0xC0401080) {
		{ RCAR_GP_PIN(7,  7), 28, 3 },	/* MSIOF0_SS1 */
		{ RCAR_GP_PIN(7,  6), 24, 3 },	/* MSIOF0_SS2 */
		{ RCAR_GP_PIN(7,  5), 20, 3 },	/* MSIOF1_RXD */
		{ RCAR_GP_PIN(7,  4), 16, 3 },	/* MSIOF1_TXD */
		{ RCAR_GP_PIN(7,  3), 12, 3 },	/* MSIOF1_SCK */
		{ RCAR_GP_PIN(7,  2),  8, 3 },	/* MSIOF1_SYNC */
		{ RCAR_GP_PIN(7,  1),  4, 3 },	/* MSIOF1_SS1 */
		{ RCAR_GP_PIN(7,  0),  0, 3 },	/* MSIOF1_SS2 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL7", 0xC0401084) {
		{ RCAR_GP_PIN(7, 15), 28, 3 },	/* HSCK0 */
		{ RCAR_GP_PIN(7, 14), 24, 3 },	/* HRTS0_N */
		{ RCAR_GP_PIN(7, 13), 20, 3 },	/* HCTS0_N */
		{ RCAR_GP_PIN(7, 12), 16, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(7, 11), 12, 3 },	/* MSIOF0_RXD */
		{ RCAR_GP_PIN(7, 10),  8, 3 },	/* MSIOF0_SCK */
		{ RCAR_GP_PIN(7,  9),  4, 3 },	/* MSIOF0_TXD */
		{ RCAR_GP_PIN(7,  8),  0, 3 },	/* MSIOF0_SYNC */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL7", 0xC0401088) {
		{ RCAR_GP_PIN(7, 23), 28, 3 },	/* GP1_23 */
		{ RCAR_GP_PIN(7, 22), 24, 3 },	/* AUDIO_CLKIN */
		{ RCAR_GP_PIN(7, 21), 20, 3 },	/* AUDIO_CLKOUT */
		{ RCAR_GP_PIN(7, 20), 16, 3 },	/* SSI_SD */
		{ RCAR_GP_PIN(7, 19), 12, 3 },	/* SSI_WS */
		{ RCAR_GP_PIN(7, 18),  8, 3 },	/* SSI_SCK */
		{ RCAR_GP_PIN(7, 17),  4, 3 },	/* SCIF_CLK */
		{ RCAR_GP_PIN(7, 16),  0, 3 },	/* HRX0 */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL7", 0xC040108C) {
		{ RCAR_GP_PIN(7, 30), 24, 3 },
		{ RCAR_GP_PIN(7, 29), 20, 2 },	/* ERROROUTC_N */
		{ RCAR_GP_PIN(7, 28), 16, 3 },	/* HTX3 */
		{ RCAR_GP_PIN(7, 27), 12, 3 },	/* HCTS3_N */
		{ RCAR_GP_PIN(7, 26),  8, 3 },	/* HRTS3_N */
		{ RCAR_GP_PIN(7, 25),  4, 3 },	/* HSCK3 */
		{ RCAR_GP_PIN(7, 24),  0, 3 },	/* HRX3 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL8", 0xC0401880) {
		{ RCAR_GP_PIN(8,  7), 28, 3 },	/* MSIOF0_SS1 */
		{ RCAR_GP_PIN(8,  6), 24, 3 },	/* MSIOF0_SS2 */
		{ RCAR_GP_PIN(8,  5), 20, 3 },	/* MSIOF1_RXD */
		{ RCAR_GP_PIN(8,  4), 16, 3 },	/* MSIOF1_TXD */
		{ RCAR_GP_PIN(8,  3), 12, 3 },	/* MSIOF1_SCK */
		{ RCAR_GP_PIN(8,  2),  8, 3 },	/* MSIOF1_SYNC */
		{ RCAR_GP_PIN(8,  1),  4, 3 },	/* MSIOF1_SS1 */
		{ RCAR_GP_PIN(8,  0),  0, 3 },	/* MSIOF1_SS2 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL8", 0xC0401884) {
		{ RCAR_GP_PIN(8, 15), 28, 3 },	/* HSCK0 */
		{ RCAR_GP_PIN(8, 14), 24, 3 },	/* HRTS0_N */
		{ RCAR_GP_PIN(8, 13), 20, 3 },	/* HCTS0_N */
		{ RCAR_GP_PIN(8, 12), 16, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(8, 11), 12, 3 },	/* MSIOF0_RXD */
		{ RCAR_GP_PIN(8, 10),  8, 3 },	/* MSIOF0_SCK */
		{ RCAR_GP_PIN(8,  9),  4, 3 },	/* MSIOF0_TXD */
		{ RCAR_GP_PIN(8,  8),  0, 3 },	/* MSIOF0_SYNC */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL8", 0xC0401888) {
		{ RCAR_GP_PIN(8, 21), 20, 3 },	/* AUDIO_CLKOUT */
		{ RCAR_GP_PIN(8, 20), 16, 3 },	/* SSI_SD */
		{ RCAR_GP_PIN(8, 19), 12, 3 },	/* SSI_WS */
		{ RCAR_GP_PIN(8, 18),  8, 3 },	/* SSI_SCK */
		{ RCAR_GP_PIN(8, 17),  4, 3 },	/* SCIF_CLK */
		{ RCAR_GP_PIN(8, 16),  0, 3 },	/* HRX0 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL9", 0xC9B00080) {
		{ RCAR_GP_PIN(9,  7), 28, 3 },	/* MSIOF0_SS1 */
		{ RCAR_GP_PIN(9,  6), 24, 3 },	/* MSIOF0_SS2 */
		{ RCAR_GP_PIN(9,  5), 20, 3 },	/* MSIOF1_RXD */
		{ RCAR_GP_PIN(9,  4), 16, 3 },	/* MSIOF1_TXD */
		{ RCAR_GP_PIN(9,  3), 12, 3 },	/* MSIOF1_SCK */
		{ RCAR_GP_PIN(9,  2),  8, 3 },	/* MSIOF1_SYNC */
		{ RCAR_GP_PIN(9,  1),  4, 3 },	/* MSIOF1_SS1 */
		{ RCAR_GP_PIN(9,  0),  0, 3 },	/* MSIOF1_SS2 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL9", 0xC9B00084) {
		{ RCAR_GP_PIN(9, 15), 28, 3 },	/* HSCK0 */
		{ RCAR_GP_PIN(9, 14), 24, 3 },	/* HRTS0_N */
		{ RCAR_GP_PIN(9, 13), 20, 3 },	/* HCTS0_N */
		{ RCAR_GP_PIN(9, 12), 16, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(9, 11), 12, 3 },	/* MSIOF0_RXD */
		{ RCAR_GP_PIN(9, 10),  8, 3 },	/* MSIOF0_SCK */
		{ RCAR_GP_PIN(9,  9),  4, 3 },	/* MSIOF0_TXD */
		{ RCAR_GP_PIN(9,  8),  0, 3 },	/* MSIOF0_SYNC */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL9", 0xC9B00088) {
		{ RCAR_GP_PIN(9, 16),  0, 3 },	/* HRX0 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL10", 0xC9B00880) {
		{ RCAR_GP_PIN(10,  7), 28, 3 },	/* MSIOF0_SS1 */
		{ RCAR_GP_PIN(10,  6), 24, 3 },	/* MSIOF0_SS2 */
		{ RCAR_GP_PIN(10,  5), 20, 3 },	/* MSIOF1_RXD */
		{ RCAR_GP_PIN(10,  4), 16, 3 },	/* MSIOF1_TXD */
		{ RCAR_GP_PIN(10,  3), 12, 3 },	/* MSIOF1_SCK */
		{ RCAR_GP_PIN(10,  2),  8, 3 },	/* MSIOF1_SYNC */
		{ RCAR_GP_PIN(10,  1),  4, 3 },	/* MSIOF1_SS1 */
		{ RCAR_GP_PIN(10,  0),  0, 3 },	/* MSIOF1_SS2 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL10", 0xC9B00884) {
		{ RCAR_GP_PIN(10, 13), 20, 3 },	/* HCTS0_N */
		{ RCAR_GP_PIN(10, 12), 16, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(10, 11), 12, 3 },	/* MSIOF0_RXD */
	} },
	{ },
};

enum ioctrl_regs {
	POC0,
	POC1,
	POC2,
	POC3,
	POC4,
	POC5,
	POC6,
	POC7,
	POC8,
	POC9,
	POC10,
};

static const struct pinmux_ioctrl_reg pinmux_ioctrl_regs[] = {
	[POC0]		= { 0xC10800A0, },
	[POC1]		= { 0xC10808A0, },
	[POC2]		= { 0xC10810A0, },
	[POC3]		= { 0xC08000A0, },
	[POC4]		= { 0xC08010A0, },
	[POC5]		= { 0xC04000A0, },
	[POC6]		= { 0xC04008A0, },
	[POC7]		= { 0xC04010A0, },
	[POC8]		= { 0xC04018A0, },
	[POC9]		= { 0xC9B000A0, },
	[POC10]		= { 0xC9B010A0, },
	{ /* sentinel */ },
};

static int r8a78000_pin_to_pocctrl(struct sh_pfc *pfc, unsigned int pin,
				   u32 *pocctrl)
{
	int bit = pin & 0x1f;

	switch (pin) {
	case RCAR_GP_PIN(0, 0) ... RCAR_GP_PIN(0, 24):
		*pocctrl = pinmux_ioctrl_regs[POC0].reg;
		return bit;

	case RCAR_GP_PIN(1, 0) ... RCAR_GP_PIN(1, 31):
		*pocctrl = pinmux_ioctrl_regs[POC1].reg;
		return bit;

	case RCAR_GP_PIN(2, 0) ... RCAR_GP_PIN(2, 28):
		*pocctrl = pinmux_ioctrl_regs[POC2].reg;
		return bit;

	case RCAR_GP_PIN(3, 0) ... RCAR_GP_PIN(3, 16):
		*pocctrl = pinmux_ioctrl_regs[POC3].reg;
		return bit;

	case RCAR_GP_PIN(4, 0) ... RCAR_GP_PIN(4, 13):
		*pocctrl = pinmux_ioctrl_regs[POC4].reg;
		return bit;

	case RCAR_GP_PIN(5, 0) ... RCAR_GP_PIN(5, 22):
		*pocctrl = pinmux_ioctrl_regs[POC0].reg;
		return bit;

	case RCAR_GP_PIN(6, 0) ... RCAR_GP_PIN(6, 30):
		*pocctrl = pinmux_ioctrl_regs[POC1].reg;
		return bit;

	case RCAR_GP_PIN(7, 0) ... RCAR_GP_PIN(7, 30):
		*pocctrl = pinmux_ioctrl_regs[POC2].reg;
		return bit;

	case RCAR_GP_PIN(8, 0) ... RCAR_GP_PIN(8, 21):
		*pocctrl = pinmux_ioctrl_regs[POC3].reg;
		return bit;

	case RCAR_GP_PIN(9, 0) ... RCAR_GP_PIN(9, 16):
		*pocctrl = pinmux_ioctrl_regs[POC4].reg;
		return bit;

	case RCAR_GP_PIN(10, 0) ... RCAR_GP_PIN(10, 13):
		*pocctrl = pinmux_ioctrl_regs[POC4].reg;
		return bit;

	default:
		return -EINVAL;
	}
}

static const struct pinmux_bias_reg pinmux_bias_regs[] = {
	{ PINMUX_BIAS_REG("PUEN0", 0xC10800C0, "PUD0", 0xC10800E0) {
		[ 0] = RCAR_GP_PIN(0,  0),	/* GP0_00 */
		[ 1] = RCAR_GP_PIN(0,  1),	/* GP0_01 */
		[ 2] = RCAR_GP_PIN(0,  2),	/* GP0_02 */
		[ 3] = RCAR_GP_PIN(0,  3),	/* IRQ3 */
		[ 4] = RCAR_GP_PIN(0,  4),	/* IRQ2 */
		[ 5] = RCAR_GP_PIN(0,  5),	/* IRQ1 */
		[ 6] = RCAR_GP_PIN(0,  6),	/* IRQ0 */
		[ 7] = RCAR_GP_PIN(0,  7),	/* MSIOF5_SS2 */
		[ 8] = RCAR_GP_PIN(0,  8),	/* MSIOF5_SS1 */
		[ 9] = RCAR_GP_PIN(0,  9),	/* MSIOF5_SYNC */
		[10] = RCAR_GP_PIN(0, 10),	/* MSIOF5_TXD */
		[11] = RCAR_GP_PIN(0, 11),	/* MSIOF5_SCK */
		[12] = RCAR_GP_PIN(0, 12),	/* MSIOF5_RXD */
		[13] = RCAR_GP_PIN(0, 13),	/* MSIOF2_SS2 */
		[14] = RCAR_GP_PIN(0, 14),	/* MSIOF2_SS1 */
		[15] = RCAR_GP_PIN(0, 15),	/* MSIOF2_SYNC */
		[16] = RCAR_GP_PIN(0, 16),	/* MSIOF2_TXD */
		[17] = RCAR_GP_PIN(0, 17),	/* MSIOF2_SCK */
		[18] = RCAR_GP_PIN(0, 18),	/* MSIOF2_RXD */
		[19] = RCAR_GP_PIN(0, 19),
		[20] = RCAR_GP_PIN(0, 20),
		[21] = RCAR_GP_PIN(0, 21),
		[22] = RCAR_GP_PIN(0, 22),
		[23] = RCAR_GP_PIN(0, 23),
		[24] = RCAR_GP_PIN(0, 24),
		[25] = SH_PFC_PIN_NONE,
		[26] = SH_PFC_PIN_NONE,
		[27] = SH_PFC_PIN_NONE,
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN1", 0xC10808C0, "PUD1", 0xC10808E0) {
		[ 0] = RCAR_GP_PIN(1,  0),	/* MSIOF1_SS2 */
		[ 1] = RCAR_GP_PIN(1,  1),	/* MSIOF1_SS1 */
		[ 2] = RCAR_GP_PIN(1,  2),	/* MSIOF1_SYNC */
		[ 3] = RCAR_GP_PIN(1,  3),	/* MSIOF1_SCK */
		[ 4] = RCAR_GP_PIN(1,  4),	/* MSIOF1_TXD */
		[ 5] = RCAR_GP_PIN(1,  5),	/* MSIOF1_RXD */
		[ 6] = RCAR_GP_PIN(1,  6),	/* MSIOF0_SS2 */
		[ 7] = RCAR_GP_PIN(1,  7),	/* MSIOF0_SS1 */
		[ 8] = RCAR_GP_PIN(1,  8),	/* MSIOF0_SYNC */
		[ 9] = RCAR_GP_PIN(1,  9),	/* MSIOF0_TXD */
		[10] = RCAR_GP_PIN(1, 10),	/* MSIOF0_SCK */
		[11] = RCAR_GP_PIN(1, 11),	/* MSIOF0_RXD */
		[12] = RCAR_GP_PIN(1, 12),	/* HTX0 */
		[13] = RCAR_GP_PIN(1, 13),	/* HCTS0_N */
		[14] = RCAR_GP_PIN(1, 14),	/* HRTS0_N */
		[15] = RCAR_GP_PIN(1, 15),	/* HSCK0 */
		[16] = RCAR_GP_PIN(1, 16),	/* HRX0 */
		[17] = RCAR_GP_PIN(1, 17),	/* SCIF_CLK */
		[18] = RCAR_GP_PIN(1, 18),	/* SSI_SCK */
		[19] = RCAR_GP_PIN(1, 19),	/* SSI_WS */
		[20] = RCAR_GP_PIN(1, 20),	/* SSI_SD */
		[21] = RCAR_GP_PIN(1, 21),	/* AUDIO_CLKOUT */
		[22] = RCAR_GP_PIN(1, 22),	/* AUDIO_CLKIN */
		[23] = RCAR_GP_PIN(1, 23),	/* GP1_23 */
		[24] = RCAR_GP_PIN(1, 24),	/* HRX3 */
		[25] = RCAR_GP_PIN(1, 25),	/* HSCK3 */
		[26] = RCAR_GP_PIN(1, 26),	/* HRTS3_N */
		[27] = RCAR_GP_PIN(1, 27),	/* HCTS3_N */
		[28] = RCAR_GP_PIN(1, 28),	/* HTX3 */
		[29] = RCAR_GP_PIN(1, 29),	/* ERROROUTC_N */
		[30] = RCAR_GP_PIN(1, 30),
		[31] = RCAR_GP_PIN(1, 31),
	} },
	{ PINMUX_BIAS_REG("PUEN2", 0xC10810C0, "PUD2", 0xC10810E0) {
		[ 0] = RCAR_GP_PIN(2,  0),	/* FXR_TXDA */
		[ 1] = RCAR_GP_PIN(2,  1),	/* FXR_TXENA_N */
		[ 2] = RCAR_GP_PIN(2,  2),	/* RXDA_EXTFXR */
		[ 3] = RCAR_GP_PIN(2,  3),	/* CLK_EXTFXR */
		[ 4] = RCAR_GP_PIN(2,  4),	/* RXDB_EXTFXR */
		[ 5] = RCAR_GP_PIN(2,  5),	/* FXR_TXENB_N */
		[ 6] = RCAR_GP_PIN(2,  6),	/* FXR_TXDB */
		[ 7] = RCAR_GP_PIN(2,  7),	/* TPU0TO1 */
		[ 8] = RCAR_GP_PIN(2,  8),	/* TPU0TO0 */
		[ 9] = RCAR_GP_PIN(2,  9),	/* CAN_CLK */
		[10] = RCAR_GP_PIN(2, 10),	/* CANFD0_TX */
		[11] = RCAR_GP_PIN(2, 11),	/* CANFD0_RX */
		[12] = RCAR_GP_PIN(2, 12),	/* CANFD2_TX */
		[13] = RCAR_GP_PIN(2, 13),	/* CANFD2_RX */
		[14] = RCAR_GP_PIN(2, 14),	/* CANFD3_TX */
		[15] = RCAR_GP_PIN(2, 15),	/* CANFD3_RX */
		[16] = RCAR_GP_PIN(2, 16),
		[17] = RCAR_GP_PIN(2, 17),	/* CANFD1_TX */
		[18] = RCAR_GP_PIN(2, 18),
		[19] = RCAR_GP_PIN(2, 19),	/* CANFD1_RX */
		[20] = RCAR_GP_PIN(2, 20),
		[21] = RCAR_GP_PIN(2, 21),
		[22] = RCAR_GP_PIN(2, 22),
		[23] = RCAR_GP_PIN(2, 23),
		[24] = RCAR_GP_PIN(2, 24),
		[25] = RCAR_GP_PIN(2, 25),
		[26] = RCAR_GP_PIN(2, 26),
		[27] = RCAR_GP_PIN(2, 27),
		[28] = RCAR_GP_PIN(2, 28),
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN3", 0xC08000C0, "PUD3", 0xC08000E0) {
		[ 0] = RCAR_GP_PIN(3,  0),	/* FXR_TXDA */
		[ 1] = RCAR_GP_PIN(3,  1),	/* FXR_TXENA_N */
		[ 2] = RCAR_GP_PIN(3,  2),	/* RXDA_EXTFXR */
		[ 3] = RCAR_GP_PIN(3,  3),	/* CLK_EXTFXR */
		[ 4] = RCAR_GP_PIN(3,  4),	/* RXDB_EXTFXR */
		[ 5] = RCAR_GP_PIN(3,  5),	/* FXR_TXENB_N */
		[ 6] = RCAR_GP_PIN(3,  6),	/* FXR_TXDB */
		[ 7] = RCAR_GP_PIN(3,  7),	/* TPU0TO1 */
		[ 8] = RCAR_GP_PIN(3,  8),	/* TPU0TO0 */
		[ 9] = RCAR_GP_PIN(3,  9),	/* CAN_CLK */
		[10] = RCAR_GP_PIN(3, 10),	/* CANFD0_TX */
		[11] = RCAR_GP_PIN(3, 11),	/* CANFD0_RX */
		[12] = RCAR_GP_PIN(3, 12),	/* CANFD2_TX */
		[13] = RCAR_GP_PIN(3, 13),	/* CANFD2_RX */
		[14] = RCAR_GP_PIN(3, 14),	/* CANFD3_TX */
		[15] = RCAR_GP_PIN(3, 15),	/* CANFD3_RX */
		[16] = RCAR_GP_PIN(3, 16),
		[17] = SH_PFC_PIN_NONE,
		[18] = SH_PFC_PIN_NONE,
		[19] = SH_PFC_PIN_NONE,
		[20] = SH_PFC_PIN_NONE,
		[21] = SH_PFC_PIN_NONE,
		[22] = SH_PFC_PIN_NONE,
		[23] = SH_PFC_PIN_NONE,
		[24] = SH_PFC_PIN_NONE,
		[25] = SH_PFC_PIN_NONE,
		[26] = SH_PFC_PIN_NONE,
		[27] = SH_PFC_PIN_NONE,
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN4", 0xC08008C0, "PUD4", 0xC08008E0) {
		[ 0] = RCAR_GP_PIN(4,  0),
		[ 1] = RCAR_GP_PIN(4,  1),
		[ 2] = RCAR_GP_PIN(4,  2),
		[ 3] = RCAR_GP_PIN(4,  3),
		[ 4] = RCAR_GP_PIN(4,  4),
		[ 5] = RCAR_GP_PIN(4,  5),
		[ 6] = RCAR_GP_PIN(4,  6),
		[ 7] = RCAR_GP_PIN(4,  7),
		[ 8] = RCAR_GP_PIN(4,  8),
		[ 9] = RCAR_GP_PIN(4,  9),
		[10] = RCAR_GP_PIN(4, 10),
		[11] = RCAR_GP_PIN(4, 11),
		[12] = RCAR_GP_PIN(4, 12),
		[13] = RCAR_GP_PIN(4, 13),
		[14] = SH_PFC_PIN_NONE,
		[15] = SH_PFC_PIN_NONE,
		[16] = SH_PFC_PIN_NONE,
		[17] = SH_PFC_PIN_NONE,
		[18] = SH_PFC_PIN_NONE,
		[19] = SH_PFC_PIN_NONE,
		[20] = SH_PFC_PIN_NONE,
		[21] = SH_PFC_PIN_NONE,
		[22] = SH_PFC_PIN_NONE,
		[23] = SH_PFC_PIN_NONE,
		[24] = SH_PFC_PIN_NONE,
		[25] = SH_PFC_PIN_NONE,
		[26] = SH_PFC_PIN_NONE,
		[27] = SH_PFC_PIN_NONE,
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN5", 0xC04000C0, "PUD5", 0xC04000E0) {
		[ 0] = RCAR_GP_PIN(5,  0),	/* MSIOF1_SS2 */
		[ 1] = RCAR_GP_PIN(5,  1),	/* MSIOF1_SS1 */
		[ 2] = RCAR_GP_PIN(5,  2),	/* MSIOF1_SYNC */
		[ 3] = RCAR_GP_PIN(5,  3),	/* MSIOF1_SCK */
		[ 4] = RCAR_GP_PIN(5,  4),	/* MSIOF1_TXD */
		[ 5] = RCAR_GP_PIN(5,  5),	/* MSIOF1_RXD */
		[ 6] = RCAR_GP_PIN(5,  6),	/* MSIOF0_SS2 */
		[ 7] = RCAR_GP_PIN(5,  7),	/* MSIOF0_SS1 */
		[ 8] = RCAR_GP_PIN(5,  8),	/* MSIOF0_SYNC */
		[ 9] = RCAR_GP_PIN(5,  9),	/* MSIOF0_TXD */
		[10] = RCAR_GP_PIN(5, 10),	/* MSIOF0_SCK */
		[11] = RCAR_GP_PIN(5, 11),	/* MSIOF0_RXD */
		[12] = RCAR_GP_PIN(5, 12),	/* HTX0 */
		[13] = RCAR_GP_PIN(5, 13),	/* HCTS0_N */
		[14] = RCAR_GP_PIN(5, 14),	/* HRTS0_N */
		[15] = RCAR_GP_PIN(5, 15),	/* HSCK0 */
		[16] = RCAR_GP_PIN(5, 16),	/* HRX0 */
		[17] = RCAR_GP_PIN(5, 17),	/* SCIF_CLK */
		[18] = RCAR_GP_PIN(5, 18),	/* SSI_SCK */
		[19] = RCAR_GP_PIN(5, 19),	/* SSI_WS */
		[20] = RCAR_GP_PIN(5, 20),	/* SSI_SD */
		[21] = RCAR_GP_PIN(5, 21),	/* AUDIO_CLKOUT */
		[22] = RCAR_GP_PIN(5, 22),	/* AUDIO_CLKIN */
		[23] = SH_PFC_PIN_NONE,		/* GP1_23 */
		[24] = SH_PFC_PIN_NONE,		/* HRX3 */
		[25] = SH_PFC_PIN_NONE,		/* HSCK3 */
		[26] = SH_PFC_PIN_NONE,		/* HRTS3_N */
		[27] = SH_PFC_PIN_NONE,		/* HCTS3_N */
		[28] = SH_PFC_PIN_NONE,		/* HTX3 */
		[29] = SH_PFC_PIN_NONE,		/* ERROROUTC_N */
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN6", 0xC04008C0, "PUD6", 0xC04008E0) {
		[ 0] = RCAR_GP_PIN(6,  0),	/* MSIOF1_SS2 */
		[ 1] = RCAR_GP_PIN(6,  1),	/* MSIOF1_SS1 */
		[ 2] = RCAR_GP_PIN(6,  2),	/* MSIOF1_SYNC */
		[ 3] = RCAR_GP_PIN(6,  3),	/* MSIOF1_SCK */
		[ 4] = RCAR_GP_PIN(6,  4),	/* MSIOF1_TXD */
		[ 5] = RCAR_GP_PIN(6,  5),	/* MSIOF1_RXD */
		[ 6] = RCAR_GP_PIN(6,  6),	/* MSIOF0_SS2 */
		[ 7] = RCAR_GP_PIN(6,  7),	/* MSIOF0_SS1 */
		[ 8] = RCAR_GP_PIN(6,  8),	/* MSIOF0_SYNC */
		[ 9] = RCAR_GP_PIN(6,  9),	/* MSIOF0_TXD */
		[10] = RCAR_GP_PIN(6, 10),	/* MSIOF0_SCK */
		[11] = RCAR_GP_PIN(6, 11),	/* MSIOF0_RXD */
		[12] = RCAR_GP_PIN(6, 12),	/* HTX0 */
		[13] = RCAR_GP_PIN(6, 13),	/* HCTS0_N */
		[14] = RCAR_GP_PIN(6, 14),	/* HRTS0_N */
		[15] = RCAR_GP_PIN(6, 15),	/* HSCK0 */
		[16] = RCAR_GP_PIN(6, 16),	/* HRX0 */
		[17] = RCAR_GP_PIN(6, 17),	/* SCIF_CLK */
		[18] = RCAR_GP_PIN(6, 18),	/* SSI_SCK */
		[19] = RCAR_GP_PIN(6, 19),	/* SSI_WS */
		[20] = RCAR_GP_PIN(6, 20),	/* SSI_SD */
		[21] = RCAR_GP_PIN(6, 21),	/* AUDIO_CLKOUT */
		[22] = RCAR_GP_PIN(6, 22),	/* AUDIO_CLKIN */
		[23] = RCAR_GP_PIN(6, 23),	/* GP1_23 */
		[24] = RCAR_GP_PIN(6, 24),	/* HRX3 */
		[25] = RCAR_GP_PIN(6, 25),	/* HSCK3 */
		[26] = RCAR_GP_PIN(6, 26),	/* HRTS3_N */
		[27] = RCAR_GP_PIN(6, 27),	/* HCTS3_N */
		[28] = RCAR_GP_PIN(6, 28),	/* HTX3 */
		[29] = RCAR_GP_PIN(6, 29),	/* ERROROUTC_N */
		[30] = RCAR_GP_PIN(6, 30),
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN7", 0xC04010C0, "PUD7", 0xC04010E0) {
		[ 0] = RCAR_GP_PIN(7,  0),	/* MSIOF1_SS2 */
		[ 1] = RCAR_GP_PIN(7,  1),	/* MSIOF1_SS1 */
		[ 2] = RCAR_GP_PIN(7,  2),	/* MSIOF1_SYNC */
		[ 3] = RCAR_GP_PIN(7,  3),	/* MSIOF1_SCK */
		[ 4] = RCAR_GP_PIN(7,  4),	/* MSIOF1_TXD */
		[ 5] = RCAR_GP_PIN(7,  5),	/* MSIOF1_RXD */
		[ 6] = RCAR_GP_PIN(7,  6),	/* MSIOF0_SS2 */
		[ 7] = RCAR_GP_PIN(7,  7),	/* MSIOF0_SS1 */
		[ 8] = RCAR_GP_PIN(7,  8),	/* MSIOF0_SYNC */
		[ 9] = RCAR_GP_PIN(7,  9),	/* MSIOF0_TXD */
		[10] = RCAR_GP_PIN(7, 10),	/* MSIOF0_SCK */
		[11] = RCAR_GP_PIN(7, 11),	/* MSIOF0_RXD */
		[12] = RCAR_GP_PIN(7, 12),	/* HTX0 */
		[13] = RCAR_GP_PIN(7, 13),	/* HCTS0_N */
		[14] = RCAR_GP_PIN(7, 14),	/* HRTS0_N */
		[15] = RCAR_GP_PIN(7, 15),	/* HSCK0 */
		[16] = RCAR_GP_PIN(7, 16),	/* HRX0 */
		[17] = RCAR_GP_PIN(7, 17),	/* SCIF_CLK */
		[18] = RCAR_GP_PIN(7, 18),	/* SSI_SCK */
		[19] = RCAR_GP_PIN(7, 19),	/* SSI_WS */
		[20] = RCAR_GP_PIN(7, 20),	/* SSI_SD */
		[21] = RCAR_GP_PIN(7, 21),	/* AUDIO_CLKOUT */
		[22] = RCAR_GP_PIN(7, 22),	/* AUDIO_CLKIN */
		[23] = RCAR_GP_PIN(7, 23),	/* GP1_23 */
		[24] = RCAR_GP_PIN(7, 24),	/* HRX3 */
		[25] = RCAR_GP_PIN(7, 25),	/* HSCK3 */
		[26] = RCAR_GP_PIN(7, 26),	/* HRTS3_N */
		[27] = RCAR_GP_PIN(7, 27),	/* HCTS3_N */
		[28] = RCAR_GP_PIN(7, 28),	/* HTX3 */
		[29] = RCAR_GP_PIN(7, 29),	/* ERROROUTC_N */
		[30] = RCAR_GP_PIN(7, 30),
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN8", 0xC04018C0, "PUD8", 0xC04018E0) {
		[ 0] = RCAR_GP_PIN(8,  0),	/* MSIOF1_SS2 */
		[ 1] = RCAR_GP_PIN(8,  1),	/* MSIOF1_SS1 */
		[ 2] = RCAR_GP_PIN(8,  2),	/* MSIOF1_SYNC */
		[ 3] = RCAR_GP_PIN(8,  3),	/* MSIOF1_SCK */
		[ 4] = RCAR_GP_PIN(8,  4),	/* MSIOF1_TXD */
		[ 5] = RCAR_GP_PIN(8,  5),	/* MSIOF1_RXD */
		[ 6] = RCAR_GP_PIN(8,  6),	/* MSIOF0_SS2 */
		[ 7] = RCAR_GP_PIN(8,  7),	/* MSIOF0_SS1 */
		[ 8] = RCAR_GP_PIN(8,  8),	/* MSIOF0_SYNC */
		[ 9] = RCAR_GP_PIN(8,  9),	/* MSIOF0_TXD */
		[10] = RCAR_GP_PIN(8, 10),	/* MSIOF0_SCK */
		[11] = RCAR_GP_PIN(8, 11),	/* MSIOF0_RXD */
		[12] = RCAR_GP_PIN(8, 12),	/* HTX0 */
		[13] = RCAR_GP_PIN(8, 13),	/* HCTS0_N */
		[14] = RCAR_GP_PIN(8, 14),	/* HRTS0_N */
		[15] = RCAR_GP_PIN(8, 15),	/* HSCK0 */
		[16] = RCAR_GP_PIN(8, 16),	/* HRX0 */
		[17] = RCAR_GP_PIN(8, 17),	/* SCIF_CLK */
		[18] = RCAR_GP_PIN(8, 18),	/* SSI_SCK */
		[19] = RCAR_GP_PIN(8, 19),	/* SSI_WS */
		[20] = RCAR_GP_PIN(8, 20),	/* SSI_SD */
		[21] = RCAR_GP_PIN(8, 21),	/* AUDIO_CLKOUT */
		[22] = SH_PFC_PIN_NONE,		/* AUDIO_CLKIN */
		[23] = SH_PFC_PIN_NONE,		/* GP1_23 */
		[24] = SH_PFC_PIN_NONE,		/* HRX3 */
		[25] = SH_PFC_PIN_NONE,		/* HSCK3 */
		[26] = SH_PFC_PIN_NONE,		/* HRTS3_N */
		[27] = SH_PFC_PIN_NONE,		/* HCTS3_N */
		[28] = SH_PFC_PIN_NONE,		/* HTX3 */
		[29] = SH_PFC_PIN_NONE,		/* ERROROUTC_N */
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN9", 0xC9B000C0, "PUD9", 0xC9B000E0) {
		[ 0] = RCAR_GP_PIN(9,  0),	/* MSIOF1_SS2 */
		[ 1] = RCAR_GP_PIN(9,  1),	/* MSIOF1_SS1 */
		[ 2] = RCAR_GP_PIN(9,  2),	/* MSIOF1_SYNC */
		[ 3] = RCAR_GP_PIN(9,  3),	/* MSIOF1_SCK */
		[ 4] = RCAR_GP_PIN(9,  4),	/* MSIOF1_TXD */
		[ 5] = RCAR_GP_PIN(9,  5),	/* MSIOF1_RXD */
		[ 6] = RCAR_GP_PIN(9,  6),	/* MSIOF0_SS2 */
		[ 7] = RCAR_GP_PIN(9,  7),	/* MSIOF0_SS1 */
		[ 8] = RCAR_GP_PIN(9,  8),	/* MSIOF0_SYNC */
		[ 9] = RCAR_GP_PIN(9,  9),	/* MSIOF0_TXD */
		[10] = RCAR_GP_PIN(9, 10),	/* MSIOF0_SCK */
		[11] = RCAR_GP_PIN(9, 11),	/* MSIOF0_RXD */
		[12] = RCAR_GP_PIN(9, 12),	/* HTX0 */
		[13] = RCAR_GP_PIN(9, 13),	/* HCTS0_N */
		[14] = RCAR_GP_PIN(9, 14),	/* HRTS0_N */
		[15] = RCAR_GP_PIN(9, 15),	/* HSCK0 */
		[16] = RCAR_GP_PIN(9, 16),	/* HRX0 */
		[17] = SH_PFC_PIN_NONE,		/* SCIF_CLK */
		[18] = SH_PFC_PIN_NONE,		/* SSI_SCK */
		[19] = SH_PFC_PIN_NONE,		/* SSI_WS */
		[20] = SH_PFC_PIN_NONE,		/* SSI_SD */
		[21] = SH_PFC_PIN_NONE,		/* AUDIO_CLKOUT */
		[22] = SH_PFC_PIN_NONE,		/* AUDIO_CLKIN */
		[23] = SH_PFC_PIN_NONE,		/* GP1_23 */
		[24] = SH_PFC_PIN_NONE,		/* HRX3 */
		[25] = SH_PFC_PIN_NONE,		/* HSCK3 */
		[26] = SH_PFC_PIN_NONE,		/* HRTS3_N */
		[27] = SH_PFC_PIN_NONE,		/* HCTS3_N */
		[28] = SH_PFC_PIN_NONE,		/* HTX3 */
		[29] = SH_PFC_PIN_NONE,		/* ERROROUTC_N */
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN10", 0xC9B008C0, "PUD10", 0xC9B008E0) {
		[ 0] = RCAR_GP_PIN(10,  0),	/* MSIOF1_SS2 */
		[ 1] = RCAR_GP_PIN(10,  1),	/* MSIOF1_SS1 */
		[ 2] = RCAR_GP_PIN(10,  2),	/* MSIOF1_SYNC */
		[ 3] = RCAR_GP_PIN(10,  3),	/* MSIOF1_SCK */
		[ 4] = RCAR_GP_PIN(10,  4),	/* MSIOF1_TXD */
		[ 5] = RCAR_GP_PIN(10,  5),	/* MSIOF1_RXD */
		[ 6] = RCAR_GP_PIN(10,  6),	/* MSIOF0_SS2 */
		[ 7] = RCAR_GP_PIN(10,  7),	/* MSIOF0_SS1 */
		[ 8] = RCAR_GP_PIN(10,  8),	/* MSIOF0_SYNC */
		[ 9] = RCAR_GP_PIN(10,  9),	/* MSIOF0_TXD */
		[10] = RCAR_GP_PIN(10, 10),	/* MSIOF0_SCK */
		[11] = RCAR_GP_PIN(10, 11),	/* MSIOF0_RXD */
		[12] = RCAR_GP_PIN(10, 12),	/* HTX0 */
		[13] = RCAR_GP_PIN(10, 13),	/* HCTS0_N */
		[14] = SH_PFC_PIN_NONE,		/* HRTS0_N */
		[15] = SH_PFC_PIN_NONE,		/* HSCK0 */
		[16] = SH_PFC_PIN_NONE,		/* HRX0 */
		[17] = SH_PFC_PIN_NONE,		/* SCIF_CLK */
		[18] = SH_PFC_PIN_NONE,		/* SSI_SCK */
		[19] = SH_PFC_PIN_NONE,		/* SSI_WS */
		[20] = SH_PFC_PIN_NONE,		/* SSI_SD */
		[21] = SH_PFC_PIN_NONE,		/* AUDIO_CLKOUT */
		[22] = SH_PFC_PIN_NONE,		/* AUDIO_CLKIN */
		[23] = SH_PFC_PIN_NONE,		/* GP1_23 */
		[24] = SH_PFC_PIN_NONE,		/* HRX3 */
		[25] = SH_PFC_PIN_NONE,		/* HSCK3 */
		[26] = SH_PFC_PIN_NONE,		/* HRTS3_N */
		[27] = SH_PFC_PIN_NONE,		/* HCTS3_N */
		[28] = SH_PFC_PIN_NONE,		/* HTX3 */
		[29] = SH_PFC_PIN_NONE,		/* ERROROUTC_N */
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ /* sentinel */ },
};

static const struct sh_pfc_soc_operations r8a78000_pin_ops = {
	.pin_to_pocctrl = r8a78000_pin_to_pocctrl,
	.get_bias = rcar_pinmux_get_bias,
	.set_bias = rcar_pinmux_set_bias,
};

const struct sh_pfc_soc_info r8a78000_pinmux_info = {
	.name = "r8a78000_pfc",
	.ops = &r8a78000_pin_ops,
	.unlock_reg = 0x1ff,	/* PMMRn mask */

	.function = { PINMUX_FUNCTION_BEGIN, PINMUX_FUNCTION_END },

	.pins = pinmux_pins,
	.nr_pins = ARRAY_SIZE(pinmux_pins),
	.groups = pinmux_groups,
	.nr_groups = ARRAY_SIZE(pinmux_groups),
	.functions = pinmux_functions,
	.nr_functions = ARRAY_SIZE(pinmux_functions),

	.cfg_regs = pinmux_config_regs,
	.drive_regs = pinmux_drive_regs,
	.bias_regs = pinmux_bias_regs,
	.ioctrl_regs = pinmux_ioctrl_regs,

	.pinmux_data = pinmux_data,
	.pinmux_data_size = ARRAY_SIZE(pinmux_data),
};
