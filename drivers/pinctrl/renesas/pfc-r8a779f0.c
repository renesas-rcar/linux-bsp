// SPDX-License-Identifier: GPL-2.0
/*
 * R8A779F0 processor support - PFC hardware block.
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

#define CPU_ALL_GP(fn, sfx)	\
	PORT_GP_CFG_21(0, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_25(1, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_17(2, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_19(3, fn, sfx, CFG_FLAGS | SH_PFC_PIN_CFG_IO_VOLTAGE_18_33),	\
	PORT_GP_CFG_31(4, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_20(5, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_32(6, fn, sfx, CFG_FLAGS),	\
	PORT_GP_CFG_32(7, fn, sfx, CFG_FLAGS)

#define CPU_ALL_NOGP(fn)								\
	PIN_NOGP_CFG(PRESETOUT_N, "PRESETOUT#", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN),	\
	PIN_NOGP_CFG(PRESETOUT0_N, "PRESETOUT0#", fn, SH_PFC_PIN_CFG_PULL_DOWN),	\
	PIN_NOGP_CFG(PRESETOUT1_N, "PRESETOUT1#", fn, SH_PFC_PIN_CFG_PULL_DOWN),	\
	PIN_NOGP_CFG(EXTALR, "EXTALR", fn, SH_PFC_PIN_CFG_PULL_UP_DOWN),		\
	PIN_NOGP_CFG(DCUTRST0_N, "DCUTRST0#", fn, SH_PFC_PIN_CFG_PULL_DOWN),		\
	PIN_NOGP_CFG(DCUTCK0, "DCUTCK0", fn, SH_PFC_PIN_CFG_PULL_UP),			\
	PIN_NOGP_CFG(DCUTMS0, "DCUTMS0", fn, SH_PFC_PIN_CFG_PULL_UP),			\
	PIN_NOGP_CFG(DCUTDI0, "DCUTDI0", fn, SH_PFC_PIN_CFG_PULL_UP),			\
	PIN_NOGP_CFG(DCUTRST1_N, "DCUTRST1#", fn, SH_PFC_PIN_CFG_PULL_DOWN),		\
	PIN_NOGP_CFG(DCUTCK1, "DCUTCK1", fn, SH_PFC_PIN_CFG_PULL_UP),			\
	PIN_NOGP_CFG(DCUTMS1, "DCUTMS1", fn, SH_PFC_PIN_CFG_PULL_UP),			\
	PIN_NOGP_CFG(DCUTDI1, "DCUTDI1", fn, SH_PFC_PIN_CFG_PULL_UP),			\
	PIN_NOGP_CFG(EVTI_N, "EVTI#", fn, SH_PFC_PIN_CFG_PULL_UP),			\
	PIN_NOGP_CFG(MSYN_N, "MSYN#", fn, SH_PFC_PIN_CFG_PULL_UP)

/*
 * F_() : just information
 * FM() : macro for FN_xxx / xxx_MARK
 */

/* GPSR0 */
#define GPSR0_20	F_(IRQ3,	IP2SR0_19_16)
#define GPSR0_19	F_(IRQ2,	IP2SR0_15_12)
#define GPSR0_18	F_(IRQ1,	IP2SR0_11_8)
#define GPSR0_17	F_(IRQ0,	IP2SR0_7_4)
#define GPSR0_16	F_(MSIOF0_SS2,	IP2SR0_3_0)
#define GPSR0_15	F_(MSIOF0_SS1,	IP1SR0_31_28)
#define GPSR0_14	F_(MSIOF0_SCK,	IP1SR0_27_24)
#define GPSR0_13	F_(MSIOF0_TXD,	IP1SR0_23_20)
#define GPSR0_12	F_(MSIOF0_RXD,	IP1SR0_19_16)
#define GPSR0_11	F_(MSIOF0_SYNC,	IP1SR0_15_12)
#define GPSR0_10	F_(CTS0_N,	IP1SR0_11_8)
#define GPSR0_9		F_(RTS0_N,	IP1SR0_7_4)
#define GPSR0_8		F_(SCK0,	IP1SR0_3_0)
#define GPSR0_7		F_(TX0,		IP0SR0_31_28)
#define GPSR0_6		F_(RX0,		IP0SR0_27_24)
#define GPSR0_5		F_(HRTS0_N,	IP0SR0_23_20)
#define GPSR0_4		F_(HCTS0_N,	IP0SR0_19_16)
#define GPSR0_3		F_(HTX0,	IP0SR0_15_12)
#define GPSR0_2		F_(HRX0,	IP0SR0_11_8)
#define GPSR0_1		F_(HSCK0,	IP0SR0_7_4)
#define GPSR0_0		F_(SCIF_CLK,	IP0SR0_3_0)

/* GPSR1 */
#define GPSR1_24	FM(SD_WP)
#define GPSR1_23	FM(SD_CD)
#define GPSR1_22	FM(MMC_SD_CMD)
#define GPSR1_21	FM(MMC_D7)
#define GPSR1_20	FM(MMC_DS)
#define GPSR1_19	FM(MMC_D6)
#define GPSR1_18	FM(MMC_D4)
#define GPSR1_17	FM(MMC_D5)
#define GPSR1_16	FM(MMC_SD_D3)
#define GPSR1_15	FM(MMC_SD_D2)
#define GPSR1_14	FM(MMC_SD_D1)
#define GPSR1_13	FM(MMC_SD_D0)
#define GPSR1_12	FM(MMC_SD_CLK)
#define GPSR1_11	FM(GP1_11)
#define GPSR1_10	FM(GP1_10)
#define GPSR1_9		FM(GP1_09)
#define GPSR1_8		FM(GP1_08)
#define GPSR1_7		F_(GP1_07,	IP0SR1_31_28)
#define GPSR1_6		F_(GP1_06,	IP0SR1_27_24)
#define GPSR1_5		F_(GP1_05,	IP0SR1_23_20)
#define GPSR1_4		F_(GP1_04,	IP0SR1_19_16)
#define GPSR1_3		F_(GP1_03,	IP0SR1_15_12)
#define GPSR1_2		F_(GP1_02,	IP0SR1_11_8)
#define GPSR1_1		F_(GP1_01,	IP0SR1_7_4)
#define GPSR1_0		F_(GP1_00,	IP0SR1_3_0)

/* GPSR2 */
#define GPSR2_16	FM(PCIE1_CLKREQ_N)
#define GPSR2_15	FM(PCIE0_CLKREQ_N)
#define GPSR2_14	FM(QSPI0_IO3)
#define GPSR2_13	FM(QSPI0_SSL)
#define GPSR2_12	FM(QSPI0_MISO_IO1)
#define GPSR2_11	FM(QSPI0_IO2)
#define GPSR2_10	FM(QSPI0_SPCLK)
#define GPSR2_9		FM(QSPI0_MOSI_IO0)
#define GPSR2_8		FM(QSPI1_SPCLK)
#define GPSR2_7		FM(QSPI1_MOSI_IO0)
#define GPSR2_6		FM(QSPI1_IO2)
#define GPSR2_5		FM(QSPI1_MISO_IO1)
#define GPSR2_4		FM(QSPI1_IO3)
#define GPSR2_3		FM(QSPI1_SSL)
#define GPSR2_2		FM(RPC_RESET_N)
#define GPSR2_1		FM(RPC_WP_N)
#define GPSR2_0		FM(RPC_INT_N)

/* GPSR3 */
#define GPSR3_18	FM(TSN0_AVTP_CAPTURE)
#define GPSR3_17	FM(TSN0_AVTP_MATCH)
#define GPSR3_16	FM(TSN0_AVTP_PPS)
#define GPSR3_15	FM(TSN1_AVTP_CAPTURE)
#define GPSR3_14	FM(TSN1_AVTP_MATCH)
#define GPSR3_13	FM(TSN1_AVTP_PPS)
#define GPSR3_12	FM(TSN0_MAGIC)
#define GPSR3_11	FM(TSN1_PHY_INT)
#define GPSR3_10	FM(TSN0_PHY_INT)
#define GPSR3_9		FM(TSN2_PHY_INT)
#define GPSR3_8		FM(TSN0_LINK)
#define GPSR3_7		FM(TSN2_LINK)
#define GPSR3_6		FM(TSN1_LINK)
#define GPSR3_5		FM(TSN1_MDC)
#define GPSR3_4		FM(TSN0_MDC)
#define GPSR3_3		FM(TSN2_MDC)
#define GPSR3_2		FM(TSN0_MDIO)
#define GPSR3_1		FM(TSN2_MDIO)
#define GPSR3_0		FM(TSN1_MDIO)

/* GPSR4 */
#define GPSR4_30	F_(MSPI1CSS1,		IP3SR4_27_24)
#define GPSR4_29	F_(MSPI1CSS2,		IP3SR4_23_20)
#define GPSR4_28	F_(MSPI1SC,		IP3SR4_19_16)
#define GPSR4_27	F_(MSPI1CSS0,		IP3SR4_15_12)
#define GPSR4_26	F_(MSPI1SO_MSPI1DCS,	IP3SR4_11_8)
#define GPSR4_25	F_(MSPI1SI,		IP3SR4_7_4)
#define GPSR4_24	F_(MSPI0CSS0,		IP3SR4_3_0)
#define GPSR4_23	F_(MSPI0CSS1,		IP2SR4_31_28)
#define GPSR4_22	F_(MSPI0SO_MSPI0DCS,	IP2SR4_27_24)
#define GPSR4_21	F_(MSPI0SI,		IP2SR4_23_20)
#define GPSR4_20	F_(MSPI0SC,		IP2SR4_19_16)
#define GPSR4_19	F_(GP4_19,		IP2SR4_15_12)
#define GPSR4_18	F_(GP4_18,		IP2SR4_11_8)
#define GPSR4_17	F_(GP4_17,		IP2SR4_7_4)
#define GPSR4_16	F_(GP4_16,		IP2SR4_3_0)
#define GPSR4_15	F_(GP4_15,		IP1SR4_31_28)
#define GPSR4_14	F_(GP4_14,		IP1SR4_27_24)
#define GPSR4_13	FM(GP4_13)
#define GPSR4_12	F_(GP4_12,		IP1SR4_19_16)
#define GPSR4_11	F_(GP4_11,		IP1SR4_15_12)
#define GPSR4_10	F_(GP4_10,		IP1SR4_11_8)
#define GPSR4_9		F_(GP4_09,		IP1SR4_7_4)
#define GPSR4_8		F_(GP4_08,		IP1SR4_3_0)
#define GPSR4_7		F_(GP4_07,		IP0SR4_31_28)
#define GPSR4_6		F_(GP4_06,		IP0SR4_27_24)
#define GPSR4_5		F_(GP4_05,		IP0SR4_23_20)
#define GPSR4_4		F_(GP4_04,		IP0SR4_19_16)
#define GPSR4_3		F_(GP4_03,		IP0SR4_15_12)
#define GPSR4_2		F_(GP4_02,		IP0SR4_11_8)
#define GPSR4_1		F_(GP4_01,		IP0SR4_7_4)
#define GPSR4_0		F_(GP4_00,		IP0SR4_3_0)

/* GPSR5 */
#define GPSR5_19	FM(ETNB0TXD0)
#define GPSR5_18	FM(ETNB0TXEN)
#define GPSR5_17	FM(ETNB0TXD2)
#define GPSR5_16	FM(ETNB0TXD1)
#define GPSR5_15	F_(ETNB0TXCLK,		IP0SR5_31_28)
#define GPSR5_14	FM(ETNB0TXD3)
#define GPSR5_13	FM(ETNB0TXER)
#define GPSR5_12	F_(ETNB0RXCLK,		IP0SR5_27_24)
#define GPSR5_11	FM(ETNB0RXD0)
#define GPSR5_10	FM(ETNB0RXDV)
#define GPSR5_9		FM(ETNB0RXD2)
#define GPSR5_8		FM(ETNB0RXD1)
#define GPSR5_7		FM(ETNB0RXD3)
#define GPSR5_6		FM(ETNB0RXER)
#define GPSR5_5		F_(ETNB0MDC,		IP0SR5_23_20)
#define GPSR5_4		F_(ETNB0LINKSTA,	IP0SR5_19_16)
#define GPSR5_3		F_(ETNB0WOL,		IP0SR5_15_12)
#define GPSR5_2		F_(ETNB0MD,		IP0SR5_11_8)
#define GPSR5_1		F_(RIIC0SDA,		IP0SR5_7_4)
#define GPSR5_0		F_(RIIC0SCL,		IP0SR5_3_0)

/* GPSR6 */
#define GPSR6_31	FM(PRESETOUT1_N)
#define GPSR6_22	FM(NMI1)
#define GPSR6_21	F_(INTP32,		IP2SR6_11_8)
#define GPSR6_20	FM(INTP33)
#define GPSR6_19	FM(INTP34)
#define GPSR6_18	FM(INTP35)
#define GPSR6_17	F_(INTP36,		IP2SR6_7_4)
#define GPSR6_16	F_(INTP37,		IP2SR6_3_0)
#define GPSR6_15	F_(RLIN30RX_INTP16,	IP1SR6_31_28)
#define GPSR6_14	F_(RLIN30TX,		IP1SR6_27_24)
#define GPSR6_13	F_(RLIN31RX_INTP17,	IP1SR6_23_20)
#define GPSR6_12	F_(RLIN31TX,		IP1SR6_19_16)
#define GPSR6_11	F_(RLIN32RX_INTP18,	IP1SR6_15_12)
#define GPSR6_10	F_(RLIN32TX,		IP1SR6_11_8)
#define GPSR6_9		F_(RLIN33RX_INTP19,	IP1SR6_7_4)
#define GPSR6_8		F_(RLIN33TX,		IP1SR6_3_0)
#define GPSR6_7		F_(RLIN34RX_INTP20,	IP0SR6_31_28)
#define GPSR6_6		F_(RLIN34TX,		IP0SR6_27_24)
#define GPSR6_5		F_(RLIN35RX_INTP21,	IP0SR6_23_20)
#define GPSR6_4		F_(RLIN35TX,		IP0SR6_19_16)
#define GPSR6_3		F_(RLIN36RX_INTP22,	IP0SR6_15_12)
#define GPSR6_2		F_(RLIN36TX,		IP0SR6_11_8)
#define GPSR6_1		F_(RLIN37RX_INTP23,	IP0SR6_7_4)
#define GPSR6_0		F_(RLIN37TX,		IP0SR6_3_0)

/* GPSR7 */
#define GPSR7_31	F_(CAN15RX_INTP15,	IP3SR7_31_28)
#define GPSR7_30	F_(CAN15TX,		IP3SR7_27_24)
#define GPSR7_29	F_(CAN14RX_INTP14,	IP3SR7_23_20)
#define GPSR7_28	F_(CAN14TX,		IP3SR7_19_16)
#define GPSR7_27	F_(CAN13RX_INTP13,	IP3SR7_15_12)
#define GPSR7_26	F_(CAN13TX,		IP3SR7_11_8)
#define GPSR7_25	F_(CAN12RX_INTP12,	IP3SR7_7_4)
#define GPSR7_24	F_(CAN12TX,		IP3SR7_3_0)
#define GPSR7_23	F_(CAN11RX_INTP11,	IP2SR7_31_28)
#define GPSR7_22	F_(CAN11TX,		IP2SR7_27_24)
#define GPSR7_21	F_(CAN10RX_INTP10,	IP2SR7_23_20)
#define GPSR7_20	F_(CAN10TX,		IP2SR7_19_16)
#define GPSR7_19	F_(CAN9RX_INTP9,	IP2SR7_15_12)
#define GPSR7_18	F_(CAN9TX,		IP2SR7_11_8)
#define GPSR7_17	F_(CAN8RX_INTP8,	IP2SR7_7_4)
#define GPSR7_16	F_(CAN8TX,		IP2SR7_3_0)
#define GPSR7_15	F_(CAN7RX_INTP7,	IP1SR7_31_28)
#define GPSR7_14	F_(CAN7TX,		IP1SR7_27_24)
#define GPSR7_13	F_(CAN6RX_INTP6,	IP1SR7_23_20)
#define GPSR7_12	F_(CAN6TX,		IP1SR7_19_16)
#define GPSR7_11	F_(CAN5RX_INTP5,	IP1SR7_15_12)
#define GPSR7_10	F_(CAN5TX,		IP1SR7_11_8)
#define GPSR7_9		F_(CAN4RX_INTP4,	IP1SR7_7_4)
#define GPSR7_8		F_(CAN4TX,		IP1SR7_3_0)
#define GPSR7_7		F_(CAN3RX_INTP3,	IP0SR7_31_28)
#define GPSR7_6		F_(CAN3TX,		IP0SR7_27_24)
#define GPSR7_5		F_(CAN2RX_INTP2,	IP0SR7_23_20)
#define GPSR7_4		F_(CAN2TX,		IP0SR7_19_16)
#define GPSR7_3		F_(CAN1RX_INTP1,	IP0SR7_15_12)
#define GPSR7_2		F_(CAN1TX,		IP0SR7_11_8)
#define GPSR7_1		F_(CAN0RX_INTP0,	IP0SR7_7_4)
#define GPSR7_0		F_(CAN0TX,		IP0SR7_3_0)

/* IP0SR0 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
/* TSN0_*  definitions are removed due to SW conflict */
#define IP0SR0_3_0	FM(SCIF_CLK)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_7_4	FM(HSCK0)		FM(SCK3)		FM(MSIOF3_SCK)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_11_8	FM(HRX0)		FM(RX3)			FM(MSIOF3_RXD)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_15_12	FM(HTX0)		FM(TX3)			FM(MSIOF3_TXD)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_19_16	FM(HCTS0_N)		FM(CTS3_N)		FM(MSIOF3_SS1)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_23_20	FM(HRTS0_N)		FM(RTS3_N)		FM(MSIOF3_SS2)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_27_24	FM(RX0)			FM(HRX1)		F_(0, 0)		FM(MSIOF1_RXD)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR0_31_28	FM(TX0)			FM(HTX1)		F_(0, 0)		FM(MSIOF1_TXD)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP1SR0 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
/* TSN1_*  definitions are removed due to SW conflict */
#define IP1SR0_3_0	FM(SCK0)		FM(HSCK1)		F_(0, 0)		FM(MSIOF1_SCK)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_7_4	FM(RTS0_N)		FM(HRTS1_N)		FM(MSIOF3_SYNC)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_11_8	FM(CTS0_N)		FM(HCTS1_N)		F_(0, 0)		FM(MSIOF1_SYNC)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_15_12	FM(MSIOF0_SYNC)		FM(HCTS3_N)		FM(CTS1_N)		FM(IRQ4)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_19_16	FM(MSIOF0_RXD)		FM(HRX3)		FM(RX1)			F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_23_20	FM(MSIOF0_TXD)		FM(HTX3)		FM(TX1)			F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_27_24	FM(MSIOF0_SCK)		FM(HSCK3)		FM(SCK1)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR0_31_28	FM(MSIOF0_SS1)		FM(HRTS3_N)		FM(RTS1_N)		FM(IRQ5)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP2SR0 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
/* TSN2_LINK, TSN0_MAGIC, TSN0_PHY_INT, TSN1_PHY_INT, TSN2_PHY_INT definitions are removed due to SW conflict */
#define IP2SR0_3_0	FM(MSIOF0_SS2)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_7_4	FM(IRQ0)		F_(0, 0)		F_(0, 0)		FM(MSIOF1_SS1)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_11_8	FM(IRQ1)		F_(0, 0)		F_(0, 0)		FM(MSIOF1_SS2)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_15_12	FM(IRQ2)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_19_16	FM(IRQ3)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR0_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP0SR1 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
/* TSN2_MDC, TSN2_MDIO  definitions are removed due to SW conflict */
#define IP0SR1_3_0	FM(GP1_00)		FM(TCLK1)		FM(HSCK2)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_7_4	FM(GP1_01)		FM(TCLK4)		FM(HRX2)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_11_8	FM(GP1_02)		F_(0, 0)		FM(HTX2)		FM(MSIOF2_SS1)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_15_12	FM(GP1_03)		FM(TCLK2)		FM(HCTS2_N)		FM(MSIOF2_SS2)		FM(CTS4_N)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_19_16	FM(GP1_04)		FM(TCLK3)		FM(HRTS2_N)		FM(MSIOF2_SYNC)		FM(RTS4_N)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_23_20	FM(GP1_05)		FM(MSIOF2_SCK)		FM(SCK4)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_27_24	FM(GP1_06)		FM(MSIOF2_RXD)		FM(RX4)			F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR1_31_28	FM(GP1_07)		FM(MSIOF2_TXD)		FM(TX4)			F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP0SR4 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
#define IP0SR4_3_0	FM(GP4_00)		FM(MSPI4SC)		F_(0, 0)		FM(TAUD0I2)		FM(TAUD0O2)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_7_4	FM(GP4_01)		FM(MSPI4SI)		F_(0, 0)		FM(TAUD0I4)		FM(TAUD0O4)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_11_8	FM(GP4_02)		FM(MSPI4SO_MSPI4DCS)	F_(0, 0)		FM(TAUD0I3)		FM(TAUD0O3)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_15_12	FM(GP4_03)		FM(MSPI4CSS1)		F_(0, 0)		FM(TAUD0I6)		FM(TAUD0O6)	FM(MSPI5SO_MSPI5DCS)	F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_19_16	FM(GP4_04)		FM(MSPI4CSS0)		FM(MSPI4SSI_N)		FM(TAUD0I5)		FM(TAUD0O5)	FM(MSPI5SC)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_23_20	FM(GP4_05)		FM(MSPI4CSS3)		F_(0, 0)		FM(TAUD0I8)		FM(TAUD0O8)	FM(MSPI5SSI_N)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_27_24	FM(GP4_06)		FM(MSPI4CSS2)		F_(0, 0)		FM(TAUD0I7)		FM(TAUD0O7)	FM(MSPI5SI)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR4_31_28	FM(GP4_07)		FM(MSPI4CSS5)		F_(0, 0)		FM(TAUD0I10)		FM(TAUD0O10)	FM(MSPI5CSS1)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP1SR4 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
#define IP1SR4_3_0	FM(GP4_08)		FM(MSPI4CSS4)		F_(0, 0)		FM(TAUD0I9)		FM(TAUD0O9)	FM(MSPI5CSS0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_7_4	FM(GP4_09)		FM(MSPI4CSS7)		F_(0, 0)		FM(TAUD0I12)		FM(TAUD0O12)	FM(MSPI5CSS3)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_11_8	FM(GP4_10)		FM(MSPI4CSS6)		F_(0, 0)		FM(TAUD0I11)		FM(TAUD0O11)	FM(MSPI5CSS2)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_15_12	FM(GP4_11)		FM(ERRORIN0_N)		F_(0, 0)		FM(TAUD0I14)		FM(TAUD0O14)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_19_16	FM(GP4_12)		FM(ERROROUT_C_N)	F_(0, 0)		FM(TAUD0I13)		FM(TAUD0O13)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_27_24	FM(GP4_14)		FM(ERRORIN1_N)		F_(0, 0)		FM(TAUD0I15)		FM(TAUD0O15)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR4_31_28	FM(GP4_15)		FM(MSPI1CSS3)		F_(0, 0)		FM(TAUD1I1)		FM(TAUD1O1)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP2SR4 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
#define IP2SR4_3_0	FM(GP4_16)		F_(0, 0)		F_(0, 0)		FM(TAUD1I0)		FM(TAUD1O0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_7_4	FM(GP4_17)		FM(MSPI1CSS5)		F_(0, 0)		FM(TAUD1I3)		FM(TAUD1O3)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_11_8	FM(GP4_18)		FM(MSPI1CSS4)		F_(0, 0)		FM(TAUD1I2)		FM(TAUD1O2)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_15_12	FM(GP4_19)		FM(MSPI1CSS6)		F_(0, 0)		FM(TAUD1I4)		FM(TAUD1O4)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_19_16	FM(MSPI0SC)		FM(MSPI1CSS7)		F_(0, 0)		FM(TAUD1I5)		FM(TAUD1O5)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_23_20	FM(MSPI0SI)		F_(0, 0)		F_(0, 0)		FM(TAUD1I7)		FM(TAUD1O7)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_27_24	FM(MSPI0SO_MSPI0DCS)	F_(0, 0)		F_(0, 0)		FM(TAUD1I6)		FM(TAUD1O6)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR4_31_28	FM(MSPI0CSS1)		F_(0, 0)		F_(0, 0)		FM(TAUD1I9)		FM(TAUD1O9)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP3SR4 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
#define IP3SR4_3_0	FM(MSPI0CSS0)		FM(MSPI0SSI_N)		F_(0, 0)		FM(TAUD1I8)		FM(TAUD1O8)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR4_7_4	FM(MSPI1SI)		F_(0, 0)		FM(MSPI0CSS4)		FM(TAUD1I12)		FM(TAUD1O12)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR4_11_8	FM(MSPI1SO_MSPI1DCS)	F_(0, 0)		FM(MSPI0CSS3)		FM(TAUD1I11)		FM(TAUD1O11)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR4_15_12	FM(MSPI1CSS0)		FM(MSPI1SSI_N)		FM(MSPI0CSS5)		FM(TAUD1I13)		FM(TAUD1O13)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR4_19_16	FM(MSPI1SC)		F_(0, 0)		FM(MSPI0CSS2)		FM(TAUD1I10)		FM(TAUD1O10)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR4_23_20	FM(MSPI1CSS2)		F_(0, 0)		FM(MSPI0CSS7)		FM(TAUD1I15)		FM(TAUD1O15)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR4_27_24	FM(MSPI1CSS1)		F_(0, 0)		FM(MSPI0CSS6)		FM(TAUD1I14)		FM(TAUD1O14)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR4_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP0SR5 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
/* MSPI*, TAUD1*  definitions are removed due to SW conflict */
#define IP0SR5_3_0	FM(RIIC0SCL)		F_(0, 0)		F_(0, 0)		FM(TAUD0I0)		FM(TAUD0O0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_7_4	FM(RIIC0SDA)		F_(0, 0)		F_(0, 0)		FM(TAUD0I1)		FM(TAUD0O1)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_11_8	FM(ETNB0MD)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_15_12	FM(ETNB0WOL)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_19_16	FM(ETNB0LINKSTA)	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_23_20	FM(ETNB0MDC)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_27_24	FM(ETNB0RXCLK)		FM(ETNB0CRS_DV)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR5_31_28	FM(ETNB0TXCLK)		FM(ETNB0REFCLK)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP0SR6 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
/* MSPI*  definitions are removed due to SW conflict */
#define IP0SR6_3_0	FM(RLIN37TX)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_7_4	FM(RLIN37RX_INTP23)	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_11_8	FM(RLIN36TX)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_15_12	FM(RLIN36RX_INTP22)	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_19_16	FM(RLIN35TX)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_23_20	FM(RLIN35RX_INTP21)	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_27_24	FM(RLIN34TX)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR6_31_28	FM(RLIN34RX_INTP20)	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP1SR6 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
/* NMI1, INTP*, CAN*  definitions are removed due to SW conflict */
#define IP1SR6_3_0	FM(RLIN33TX)		F_(0, 0)		F_(0, 0)		FM(TAUJ3O3)		FM(TAUJ3I3)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_7_4	FM(RLIN33RX_INTP19)	F_(0, 0)		F_(0, 0)		FM(TAUJ3O2)		FM(TAUJ3I2)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_11_8	FM(RLIN32TX)		F_(0, 0)		F_(0, 0)		FM(TAUJ3O1)		FM(TAUJ3I1)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_15_12	FM(RLIN32RX_INTP18)	F_(0, 0)		F_(0, 0)		FM(TAUJ3O0)		FM(TAUJ3I0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_19_16	FM(RLIN31TX)		F_(0, 0)		F_(0, 0)		FM(TAUJ1I3)		FM(TAUJ1O3)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_23_20	FM(RLIN31RX_INTP17)	F_(0, 0)		F_(0, 0)		FM(TAUJ1I2)		FM(TAUJ1O2)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_27_24	FM(RLIN30TX)		F_(0, 0)		F_(0, 0)		FM(TAUJ1I1)		FM(TAUJ1O1)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR6_31_28	FM(RLIN30RX_INTP16)	F_(0, 0)		F_(0, 0)		FM(TAUJ1I0)		FM(TAUJ1O0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP2SR6 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
#define IP2SR6_3_0	FM(INTP37)		F_(0, 0)		FM(EXTCLK0O)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_7_4	FM(INTP36)		FM(RTCA0OUT)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_11_8	FM(INTP32)		F_(0, 0)		FM(FLXA0STPWT)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_15_12	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_19_16	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_23_20	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_27_24	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR6_31_28	F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

/* IP0SR7 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
/* RLIN*  definitions are removed due to SW conflict */
#define IP0SR7_3_0	FM(CAN0TX)		FM(RSENT0SPCO)		F_(0, 0)		FM(MSPI2SO_MSPI2DCS)	F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_7_4	FM(CAN0RX_INTP0)	FM(RSENT0RX)		FM(RSENT0RX_RSENT0SPCO)	FM(MSPI2SC)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_11_8	FM(CAN1TX)		FM(RSENT1SPCO)		F_(0, 0)		FM(MSPI2SSI_N)		FM(MSPI2CSS0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_15_12	FM(CAN1RX_INTP1)	FM(RSENT1RX)		FM(RSENT1RX_RSENT1SPCO)	FM(MSPI2SI)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_19_16	FM(CAN2TX)		FM(RSENT2SPCO)		F_(0, 0)		F_(0, 0)		FM(MSPI2CSS2)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_23_20	FM(CAN2RX_INTP2)	FM(RSENT2RX)		FM(RSENT2RX_RSENT2SPCO)	F_(0, 0)		FM(MSPI2CSS1)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_27_24	FM(CAN3TX)		FM(RSENT3SPCO)		F_(0, 0)		F_(0, 0)		FM(MSPI2CSS4)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP0SR7_31_28	FM(CAN3RX_INTP3)	FM(RSENT3RX)		FM(RSENT3RX_RSENT3SPCO)	F_(0, 0)		FM(MSPI2CSS3)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP1SR7 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
#define IP1SR7_3_0	FM(CAN4TX)		FM(RSENT4SPCO)		F_(0, 0)		F_(0, 0)		FM(MSPI2CSS6)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_7_4	FM(CAN4RX_INTP4)	FM(RSENT4RX)		FM(RSENT4RX_RSENT4SPCO)	F_(0, 0)		FM(MSPI2CSS5)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_11_8	FM(CAN5TX)		FM(RSENT5SPCO)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_15_12	FM(CAN5RX_INTP5)	FM(RSENT5RX)		FM(RSENT5RX_RSENT5SPCO)	F_(0, 0)		FM(MSPI2CSS7)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_19_16	FM(CAN6TX)		FM(RSENT6SPCO)		F_(0, 0)		FM(MSPI3SO_MSPI3DCS)	F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_23_20	FM(CAN6RX_INTP6)	FM(RSENT6RX)		FM(RSENT6RX_RSENT6SPCO)	FM(MSPI3SC)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_27_24	FM(CAN7TX)		FM(RSENT7SPCO)		F_(0, 0)		FM(MSPI3SSI_N)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP1SR7_31_28	FM(CAN7RX_INTP7)	FM(RSENT7RX)		FM(RSENT7RX_RSENT7SPCO)	FM(MSPI3SI)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP2SR7 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
/* RTCA0OUT,  EXTCLK0O definitions are removed due to SW conflict */
#define IP2SR7_3_0	FM(CAN8TX)		FM(RLIN38TX)		F_(0, 0)		FM(MSPI3CSS1)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_7_4	FM(CAN8RX_INTP8)	FM(RLIN38RX_INTP24)	F_(0, 0)		FM(MSPI3CSS0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_11_8	FM(CAN9TX)		FM(RLIN39TX)		F_(0, 0)		FM(MSPI3CSS3)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_15_12	FM(CAN9RX_INTP9)	FM(RLIN39RX_INTP25)	F_(0, 0)		FM(MSPI3CSS2)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_19_16	FM(CAN10TX)		FM(RLIN310TX)		F_(0, 0)		FM(MSPI3CSS5)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_23_20	FM(CAN10RX_INTP10)	FM(RLIN310RX_INTP26)	F_(0, 0)		FM(MSPI3CSS4)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_27_24	FM(CAN11TX)		FM(RLIN311TX)		F_(0, 0)		FM(MSPI3CSS7)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP2SR7_31_28	FM(CAN11RX_INTP11)	FM(RLIN311RX_INTP27)	F_(0, 0)		FM(MSPI3CSS6)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
/* IP3SR7 */		/* 0 */			/* 1 */			/* 2 */			/* 3 */			/* 4 */		/* 5 */			/* 6 - F */
#define IP3SR7_3_0	FM(CAN12TX)		FM(RLIN312TX)		F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_7_4	FM(CAN12RX_INTP12)	FM(RLIN312RX_INTP28)	F_(0, 0)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_11_8	FM(CAN13TX)		FM(RLIN313TX)		FM(FLXA0RXDB)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_15_12	FM(CAN13RX_INTP13)	FM(RLIN313RX_INTP29)	FM(FLXA0RXDA)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_19_16	FM(CAN14TX)		FM(RLIN314TX)		FM(FLXA0TXDB)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_23_20	FM(CAN14RX_INTP14)	FM(RLIN314RX_INTP30)	FM(FLXA0TXDA)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_27_24	FM(CAN15TX)		FM(RLIN315TX)		FM(FLXA0TXENB)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)
#define IP3SR7_31_28	FM(CAN15RX_INTP15)	FM(RLIN315RX_INTP31)	FM(FLXA0TXENA)		F_(0, 0)		F_(0, 0)	F_(0, 0)		F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0) F_(0, 0)

#define PINMUX_GPSR	\
												GPSR6_31	GPSR7_31	\
								GPSR4_30					GPSR7_30	\
								GPSR4_29					GPSR7_29	\
								GPSR4_28					GPSR7_28	\
								GPSR4_27					GPSR7_27	\
								GPSR4_26					GPSR7_26	\
								GPSR4_25					GPSR7_25	\
		GPSR1_24					GPSR4_24					GPSR7_24	\
		GPSR1_23					GPSR4_23					GPSR7_23	\
		GPSR1_22					GPSR4_22			GPSR6_22	GPSR7_22	\
		GPSR1_21					GPSR4_21			GPSR6_21	GPSR7_21	\
GPSR0_20	GPSR1_20					GPSR4_20			GPSR6_20	GPSR7_20	\
GPSR0_19	GPSR1_19					GPSR4_19	GPSR5_19	GPSR6_19	GPSR7_19	\
GPSR0_18	GPSR1_18			GPSR3_18	GPSR4_18	GPSR5_18	GPSR6_18	GPSR7_18	\
GPSR0_17	GPSR1_17			GPSR3_17	GPSR4_17	GPSR5_17	GPSR6_17	GPSR7_17	\
GPSR0_16	GPSR1_16	GPSR2_16	GPSR3_16	GPSR4_16	GPSR5_16	GPSR6_16	GPSR7_16	\
GPSR0_15	GPSR1_15	GPSR2_15	GPSR3_15	GPSR4_15	GPSR5_15	GPSR6_15	GPSR7_15	\
GPSR0_14	GPSR1_14	GPSR2_14	GPSR3_14	GPSR4_14	GPSR5_14	GPSR6_14	GPSR7_14	\
GPSR0_13	GPSR1_13	GPSR2_13	GPSR3_13	GPSR4_13	GPSR5_13	GPSR6_13	GPSR7_13	\
GPSR0_12	GPSR1_12	GPSR2_12	GPSR3_12	GPSR4_12	GPSR5_12	GPSR6_12	GPSR7_12	\
GPSR0_11	GPSR1_11	GPSR2_11	GPSR3_11	GPSR4_11	GPSR5_11	GPSR6_11	GPSR7_11	\
GPSR0_10	GPSR1_10	GPSR2_10	GPSR3_10	GPSR4_10	GPSR5_10	GPSR6_10	GPSR7_10	\
GPSR0_9		GPSR1_9		GPSR2_9		GPSR3_9		GPSR4_9		GPSR5_9		GPSR6_9		GPSR7_9		\
GPSR0_8		GPSR1_8		GPSR2_8		GPSR3_8		GPSR4_8		GPSR5_8		GPSR6_8		GPSR7_8		\
GPSR0_7		GPSR1_7		GPSR2_7		GPSR3_7		GPSR4_7		GPSR5_7		GPSR6_7		GPSR7_7		\
GPSR0_6		GPSR1_6		GPSR2_6		GPSR3_6		GPSR4_6		GPSR5_6		GPSR6_6		GPSR7_6		\
GPSR0_5		GPSR1_5		GPSR2_5		GPSR3_5		GPSR4_5		GPSR5_5		GPSR6_5		GPSR7_5		\
GPSR0_4		GPSR1_4		GPSR2_4		GPSR3_4		GPSR4_4		GPSR5_4		GPSR6_4		GPSR7_4		\
GPSR0_3		GPSR1_3		GPSR2_3		GPSR3_3		GPSR4_3		GPSR5_3		GPSR6_3		GPSR7_3		\
GPSR0_2		GPSR1_2		GPSR2_2		GPSR3_2		GPSR4_2		GPSR5_2		GPSR6_2		GPSR7_2		\
GPSR0_1		GPSR1_1		GPSR2_1		GPSR3_1		GPSR4_1		GPSR5_1		GPSR6_1		GPSR7_1		\
GPSR0_0		GPSR1_0		GPSR2_0		GPSR3_0		GPSR4_0		GPSR5_0		GPSR6_0		GPSR7_0

#define PINMUX_IPSR	\
\
FM(IP0SR0_3_0)		IP0SR0_3_0	FM(IP1SR0_3_0)		IP1SR0_3_0	FM(IP2SR0_3_0)		IP2SR0_3_0	\
FM(IP0SR0_7_4)		IP0SR0_7_4	FM(IP1SR0_7_4)		IP1SR0_7_4	FM(IP2SR0_7_4)		IP2SR0_7_4	\
FM(IP0SR0_11_8)		IP0SR0_11_8	FM(IP1SR0_11_8)		IP1SR0_11_8	FM(IP2SR0_11_8)		IP2SR0_11_8	\
FM(IP0SR0_15_12)	IP0SR0_15_12	FM(IP1SR0_15_12)	IP1SR0_15_12	FM(IP2SR0_15_12)	IP2SR0_15_12	\
FM(IP0SR0_19_16)	IP0SR0_19_16	FM(IP1SR0_19_16)	IP1SR0_19_16	FM(IP2SR0_19_16)	IP2SR0_19_16	\
FM(IP0SR0_23_20)	IP0SR0_23_20	FM(IP1SR0_23_20)	IP1SR0_23_20	FM(IP2SR0_23_20)	IP2SR0_23_20	\
FM(IP0SR0_27_24)	IP0SR0_27_24	FM(IP1SR0_27_24)	IP1SR0_27_24	FM(IP2SR0_27_24)	IP2SR0_27_24	\
FM(IP0SR0_31_28)	IP0SR0_31_28	FM(IP1SR0_31_28)	IP1SR0_31_28	FM(IP2SR0_31_28)	IP2SR0_31_28	\
\
FM(IP0SR1_3_0)		IP0SR1_3_0	\
FM(IP0SR1_7_4)		IP0SR1_7_4	\
FM(IP0SR1_11_8)		IP0SR1_11_8	\
FM(IP0SR1_15_12)	IP0SR1_15_12	\
FM(IP0SR1_19_16)	IP0SR1_19_16	\
FM(IP0SR1_23_20)	IP0SR1_23_20	\
FM(IP0SR1_27_24)	IP0SR1_27_24	\
FM(IP0SR1_31_28)	IP0SR1_31_28	\
\
FM(IP0SR4_3_0)		IP0SR4_3_0	FM(IP1SR4_3_0)		IP1SR4_3_0	FM(IP2SR4_3_0)		IP2SR4_3_0	FM(IP3SR4_3_0)		IP3SR4_3_0	\
FM(IP0SR4_7_4)		IP0SR4_7_4	FM(IP1SR4_7_4)		IP1SR4_7_4	FM(IP2SR4_7_4)		IP2SR4_7_4	FM(IP3SR4_7_4)		IP3SR4_7_4	\
FM(IP0SR4_11_8)		IP0SR4_11_8	FM(IP1SR4_11_8)		IP1SR4_11_8	FM(IP2SR4_11_8)		IP2SR4_11_8	FM(IP3SR4_11_8)		IP3SR4_11_8	\
FM(IP0SR4_15_12)	IP0SR4_15_12	FM(IP1SR4_15_12)	IP1SR4_15_12	FM(IP2SR4_15_12)	IP2SR4_15_12	FM(IP3SR4_15_12)	IP3SR4_15_12	\
FM(IP0SR4_19_16)	IP0SR4_19_16	FM(IP1SR4_19_16)	IP1SR4_19_16	FM(IP2SR4_19_16)	IP2SR4_19_16	FM(IP3SR4_19_16)	IP3SR4_19_16	\
FM(IP0SR4_23_20)	IP0SR4_23_20	FM(IP1SR4_23_20)	IP1SR4_23_20	FM(IP2SR4_23_20)	IP2SR4_23_20	FM(IP3SR4_23_20)	IP3SR4_23_20	\
FM(IP0SR4_27_24)	IP0SR4_27_24	FM(IP1SR4_27_24)	IP1SR4_27_24	FM(IP2SR4_27_24)	IP2SR4_27_24	FM(IP3SR4_27_24)	IP3SR4_27_24	\
FM(IP0SR4_31_28)	IP0SR4_31_28	FM(IP1SR4_31_28)	IP1SR4_31_28	FM(IP2SR4_31_28)	IP2SR4_31_28	FM(IP3SR4_31_28)	IP3SR4_31_28	\
\
FM(IP0SR5_3_0)		IP0SR5_3_0	\
FM(IP0SR5_7_4)		IP0SR5_7_4	\
FM(IP0SR5_11_8)		IP0SR5_11_8	\
FM(IP0SR5_15_12)	IP0SR5_15_12	\
FM(IP0SR5_19_16)	IP0SR5_19_16	\
FM(IP0SR5_23_20)	IP0SR5_23_20	\
FM(IP0SR5_27_24)	IP0SR5_27_24	\
FM(IP0SR5_31_28)	IP0SR5_31_28	\
\
FM(IP0SR6_3_0)		IP0SR6_3_0	FM(IP1SR6_3_0)		IP1SR6_3_0	FM(IP2SR6_3_0)		IP2SR6_3_0	\
FM(IP0SR6_7_4)		IP0SR6_7_4	FM(IP1SR6_7_4)		IP1SR6_7_4	FM(IP2SR6_7_4)		IP2SR6_7_4	\
FM(IP0SR6_11_8)		IP0SR6_11_8	FM(IP1SR6_11_8)		IP1SR6_11_8	FM(IP2SR6_11_8)		IP2SR6_11_8	\
FM(IP0SR6_15_12)	IP0SR6_15_12	FM(IP1SR6_15_12)	IP1SR6_15_12	FM(IP2SR6_15_12)	IP2SR6_15_12	\
FM(IP0SR6_19_16)	IP0SR6_19_16	FM(IP1SR6_19_16)	IP1SR6_19_16	FM(IP2SR6_19_16)	IP2SR6_19_16	\
FM(IP0SR6_23_20)	IP0SR6_23_20	FM(IP1SR6_23_20)	IP1SR6_23_20	FM(IP2SR6_23_20)	IP2SR6_23_20	\
FM(IP0SR6_27_24)	IP0SR6_27_24	FM(IP1SR6_27_24)	IP1SR6_27_24	FM(IP2SR6_27_24)	IP2SR6_27_24	\
FM(IP0SR6_31_28)	IP0SR6_31_28	FM(IP1SR6_31_28)	IP1SR6_31_28	FM(IP2SR6_31_28)	IP2SR6_31_28	\
\
FM(IP0SR7_3_0)		IP0SR7_3_0	FM(IP1SR7_3_0)		IP1SR7_3_0	FM(IP2SR7_3_0)		IP2SR7_3_0	FM(IP3SR7_3_0)		IP3SR7_3_0	\
FM(IP0SR7_7_4)		IP0SR7_7_4	FM(IP1SR7_7_4)		IP1SR7_7_4	FM(IP2SR7_7_4)		IP2SR7_7_4	FM(IP3SR7_7_4)		IP3SR7_7_4	\
FM(IP0SR7_11_8)		IP0SR7_11_8	FM(IP1SR7_11_8)		IP1SR7_11_8	FM(IP2SR7_11_8)		IP2SR7_11_8	FM(IP3SR7_11_8)		IP3SR7_11_8	\
FM(IP0SR7_15_12)	IP0SR7_15_12	FM(IP1SR7_15_12)	IP1SR7_15_12	FM(IP2SR7_15_12)	IP2SR7_15_12	FM(IP3SR7_15_12)	IP3SR7_15_12	\
FM(IP0SR7_19_16)	IP0SR7_19_16	FM(IP1SR7_19_16)	IP1SR7_19_16	FM(IP2SR7_19_16)	IP2SR7_19_16	FM(IP3SR7_19_16)	IP3SR7_19_16	\
FM(IP0SR7_23_20)	IP0SR7_23_20	FM(IP1SR7_23_20)	IP1SR7_23_20	FM(IP2SR7_23_20)	IP2SR7_23_20	FM(IP3SR7_23_20)	IP3SR7_23_20	\
FM(IP0SR7_27_24)	IP0SR7_27_24	FM(IP1SR7_27_24)	IP1SR7_27_24	FM(IP2SR7_27_24)	IP2SR7_27_24	FM(IP3SR7_27_24)	IP3SR7_27_24	\
FM(IP0SR7_31_28)	IP0SR7_31_28	FM(IP1SR7_31_28)	IP1SR7_31_28	FM(IP2SR7_31_28)	IP2SR7_31_28	FM(IP3SR7_31_28)	IP3SR7_31_28

/* MOD_SEL1 */			/* 0 */		/* 1 */		/* 2 */		/* 3 */
#define MOD_SEL1_10_11		FM(SEL_I2C5_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C5_3)
#define MOD_SEL1_8_9		FM(SEL_I2C4_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C4_3)
#define MOD_SEL1_6_7		FM(SEL_I2C3_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C3_3)
#define MOD_SEL1_4_5		FM(SEL_I2C2_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C2_3)
#define MOD_SEL1_2_3		FM(SEL_I2C1_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C1_3)
#define MOD_SEL1_0_1		FM(SEL_I2C0_0)	F_(0, 0)	F_(0, 0)	FM(SEL_I2C0_3)

#define PINMUX_MOD_SELS \
\
MOD_SEL1_10_11 \
MOD_SEL1_8_9 \
MOD_SEL1_6_7 \
MOD_SEL1_4_5 \
MOD_SEL1_2_3 \
MOD_SEL1_0_1

#define PINMUX_PHYS \
	FM(SCL0) FM(SDA0) FM(SCL1) FM(SDA1) FM(SCL2) FM(SDA2) FM(SCL3) FM(SDA3) \
	FM(SCL4) FM(SDA4) FM(SCL5) FM(SDA5)

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
	PINMUX_MOD_SELS
	PINMUX_FUNCTION_END,
#undef F_
#undef FM

#define F_(x, y)
#define FM(x)	x##_MARK,
	PINMUX_MARK_BEGIN,
	PINMUX_GPSR
	PINMUX_IPSR
	PINMUX_MOD_SELS
	PINMUX_PHYS
	PINMUX_MARK_END,
#undef F_
#undef FM
};

static const u16 pinmux_data[] = {
	PINMUX_DATA_GP_ALL(),

	PINMUX_SINGLE(SD_WP),
	PINMUX_SINGLE(SD_CD),
	PINMUX_SINGLE(MMC_SD_CMD),
	PINMUX_SINGLE(MMC_D7),
	PINMUX_SINGLE(MMC_DS),
	PINMUX_SINGLE(MMC_D6),
	PINMUX_SINGLE(MMC_D4),
	PINMUX_SINGLE(MMC_D5),
	PINMUX_SINGLE(MMC_SD_D3),
	PINMUX_SINGLE(MMC_SD_D2),
	PINMUX_SINGLE(MMC_SD_D1),
	PINMUX_SINGLE(MMC_SD_D0),
	PINMUX_SINGLE(MMC_SD_CLK),
	PINMUX_SINGLE(PCIE1_CLKREQ_N),
	PINMUX_SINGLE(PCIE0_CLKREQ_N),
	PINMUX_SINGLE(QSPI0_IO3),
	PINMUX_SINGLE(QSPI0_SSL),
	PINMUX_SINGLE(QSPI0_MISO_IO1),
	PINMUX_SINGLE(QSPI0_IO2),
	PINMUX_SINGLE(QSPI0_SPCLK),
	PINMUX_SINGLE(QSPI0_MOSI_IO0),
	PINMUX_SINGLE(QSPI1_SPCLK),
	PINMUX_SINGLE(QSPI1_MOSI_IO0),
	PINMUX_SINGLE(QSPI1_IO2),
	PINMUX_SINGLE(QSPI1_MISO_IO1),
	PINMUX_SINGLE(QSPI1_IO3),
	PINMUX_SINGLE(QSPI1_SSL),
	PINMUX_SINGLE(RPC_RESET_N),
	PINMUX_SINGLE(RPC_WP_N),
	PINMUX_SINGLE(RPC_INT_N),

	PINMUX_SINGLE(TSN0_AVTP_CAPTURE),
	PINMUX_SINGLE(TSN0_AVTP_MATCH),
	PINMUX_SINGLE(TSN0_AVTP_PPS),
	PINMUX_SINGLE(TSN1_AVTP_CAPTURE),
	PINMUX_SINGLE(TSN1_AVTP_MATCH),
	PINMUX_SINGLE(TSN1_AVTP_PPS),
	PINMUX_SINGLE(TSN0_MAGIC),
	PINMUX_SINGLE(TSN1_PHY_INT),
	PINMUX_SINGLE(TSN0_PHY_INT),
	PINMUX_SINGLE(TSN2_PHY_INT),
	PINMUX_SINGLE(TSN0_LINK),
	PINMUX_SINGLE(TSN2_LINK),
	PINMUX_SINGLE(TSN1_LINK),
	PINMUX_SINGLE(TSN1_MDC),
	PINMUX_SINGLE(TSN0_MDC),
	PINMUX_SINGLE(TSN2_MDC),
	PINMUX_SINGLE(TSN0_MDIO),
	PINMUX_SINGLE(TSN2_MDIO),
	PINMUX_SINGLE(TSN1_MDIO),

	PINMUX_SINGLE(GP4_13),

	PINMUX_SINGLE(ETNB0TXD0),
	PINMUX_SINGLE(ETNB0TXEN),
	PINMUX_SINGLE(ETNB0TXD2),
	PINMUX_SINGLE(ETNB0TXD1),
	PINMUX_SINGLE(ETNB0TXD3),
	PINMUX_SINGLE(ETNB0TXER),
	PINMUX_SINGLE(ETNB0RXD0),
	PINMUX_SINGLE(ETNB0RXDV),
	PINMUX_SINGLE(ETNB0RXD2),
	PINMUX_SINGLE(ETNB0RXD1),
	PINMUX_SINGLE(ETNB0RXD3),
	PINMUX_SINGLE(ETNB0RXER),

	PINMUX_SINGLE(PRESETOUT1_N),
	PINMUX_SINGLE(NMI1),
	PINMUX_SINGLE(INTP33),
	PINMUX_SINGLE(INTP34),
	PINMUX_SINGLE(INTP35),

	/* IP0SR0 */
	PINMUX_IPSR_GPSR(IP0SR0_3_0,	SCIF_CLK),

	PINMUX_IPSR_GPSR(IP0SR0_7_4,	HSCK0),
	PINMUX_IPSR_GPSR(IP0SR0_7_4,	SCK3),
	PINMUX_IPSR_GPSR(IP0SR0_7_4,	MSIOF3_SCK),

	PINMUX_IPSR_GPSR(IP0SR0_11_8,	HRX0),
	PINMUX_IPSR_GPSR(IP0SR0_11_8,	RX3),
	PINMUX_IPSR_GPSR(IP0SR0_11_8,	MSIOF3_RXD),

	PINMUX_IPSR_GPSR(IP0SR0_15_12,	HTX0),
	PINMUX_IPSR_GPSR(IP0SR0_15_12,	TX3),
	PINMUX_IPSR_GPSR(IP0SR0_15_12,	MSIOF3_TXD),

	PINMUX_IPSR_GPSR(IP0SR0_19_16,	HCTS0_N),
	PINMUX_IPSR_GPSR(IP0SR0_19_16,	CTS3_N),
	PINMUX_IPSR_GPSR(IP0SR0_19_16,	MSIOF3_SS1),

	PINMUX_IPSR_GPSR(IP0SR0_23_20,	HRTS0_N),
	PINMUX_IPSR_GPSR(IP0SR0_23_20,	RTS3_N),
	PINMUX_IPSR_GPSR(IP0SR0_23_20,	MSIOF3_SS2),

	PINMUX_IPSR_GPSR(IP0SR0_27_24,	RX0),
	PINMUX_IPSR_GPSR(IP0SR0_27_24,	HRX1),
	PINMUX_IPSR_GPSR(IP0SR0_27_24,	MSIOF1_RXD),

	PINMUX_IPSR_GPSR(IP0SR0_31_28,	TX0),
	PINMUX_IPSR_GPSR(IP0SR0_31_28,	HTX1),
	PINMUX_IPSR_GPSR(IP0SR0_31_28,	MSIOF1_TXD),

	/* IP1SR0 */
	PINMUX_IPSR_GPSR(IP1SR0_3_0,	SCK0),
	PINMUX_IPSR_GPSR(IP1SR0_3_0,	HSCK1),
	PINMUX_IPSR_GPSR(IP1SR0_3_0,	MSIOF1_SCK),

	PINMUX_IPSR_GPSR(IP1SR0_7_4,	RTS0_N),
	PINMUX_IPSR_GPSR(IP1SR0_7_4,	HRTS1_N),
	PINMUX_IPSR_GPSR(IP1SR0_7_4,	MSIOF3_SYNC),

	PINMUX_IPSR_GPSR(IP1SR0_11_8,	CTS0_N),
	PINMUX_IPSR_GPSR(IP1SR0_11_8,	HCTS1_N),
	PINMUX_IPSR_GPSR(IP1SR0_11_8,	MSIOF1_SYNC),

	PINMUX_IPSR_GPSR(IP1SR0_15_12,	MSIOF0_SYNC),
	PINMUX_IPSR_GPSR(IP1SR0_15_12,	HCTS3_N),
	PINMUX_IPSR_GPSR(IP1SR0_15_12,	CTS1_N),
	PINMUX_IPSR_GPSR(IP1SR0_15_12,	IRQ4),

	PINMUX_IPSR_GPSR(IP1SR0_19_16,	MSIOF0_RXD),
	PINMUX_IPSR_GPSR(IP1SR0_19_16,	HRX3),
	PINMUX_IPSR_GPSR(IP1SR0_19_16,	RX1),

	PINMUX_IPSR_GPSR(IP1SR0_23_20,	MSIOF0_TXD),
	PINMUX_IPSR_GPSR(IP1SR0_23_20,	HTX3),
	PINMUX_IPSR_GPSR(IP1SR0_23_20,	TX1),

	PINMUX_IPSR_GPSR(IP1SR0_27_24,	MSIOF0_SCK),
	PINMUX_IPSR_GPSR(IP1SR0_27_24,	HSCK3),
	PINMUX_IPSR_GPSR(IP1SR0_27_24,	SCK1),

	PINMUX_IPSR_GPSR(IP1SR0_31_28,	MSIOF0_SS1),
	PINMUX_IPSR_GPSR(IP1SR0_31_28,	HRTS3_N),
	PINMUX_IPSR_GPSR(IP1SR0_31_28,	RTS1_N),
	PINMUX_IPSR_GPSR(IP1SR0_31_28,	IRQ5),

	/* IP2SR0 */
	PINMUX_IPSR_GPSR(IP2SR0_3_0,	MSIOF0_SS2),

	PINMUX_IPSR_GPSR(IP2SR0_7_4,	IRQ0),
	PINMUX_IPSR_GPSR(IP2SR0_7_4,	MSIOF1_SS1),

	PINMUX_IPSR_GPSR(IP2SR0_11_8,	IRQ1),
	PINMUX_IPSR_GPSR(IP2SR0_11_8,	MSIOF1_SS2),

	PINMUX_IPSR_GPSR(IP2SR0_15_12,	IRQ2),

	PINMUX_IPSR_GPSR(IP2SR0_19_16,	IRQ3),

	/* IP0SR1 */
	/* GP1_00 = SCL0 */
	PINMUX_IPSR_MSEL(IP0SR1_3_0,	GP1_00,	SEL_I2C0_0),
	PINMUX_IPSR_MSEL(IP0SR1_3_0,	TCLK1,	SEL_I2C0_0),
	PINMUX_IPSR_MSEL(IP0SR1_3_0,	HSCK2,	SEL_I2C0_0),
	PINMUX_IPSR_PHYS(IP0SR1_3_0,	SCL0,	SEL_I2C0_3),

	/* GP1_01 = SDA0 */
	PINMUX_IPSR_MSEL(IP0SR1_7_4,	GP1_01,	SEL_I2C0_0),
	PINMUX_IPSR_MSEL(IP0SR1_7_4,	TCLK4,	SEL_I2C0_0),
	PINMUX_IPSR_MSEL(IP0SR1_7_4,	HRX2,	SEL_I2C0_0),
	PINMUX_IPSR_PHYS(IP0SR1_7_4,	SDA0,	SEL_I2C0_3),

	/* GP1_02 = SCL1 */
	PINMUX_IPSR_MSEL(IP0SR1_11_8,	GP1_02,		SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR1_11_8,	HTX2,		SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR1_11_8,	MSIOF2_SS1,	SEL_I2C1_0),
	PINMUX_IPSR_PHYS(IP0SR1_11_8,	SCL1,		SEL_I2C1_3),

	/* GP1_03 = SDA1 */
	PINMUX_IPSR_MSEL(IP0SR1_15_12,	GP1_03,		SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR1_15_12,	TCLK2,		SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR1_15_12,	HCTS2_N,	SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR1_15_12,	MSIOF2_SS2,	SEL_I2C1_0),
	PINMUX_IPSR_MSEL(IP0SR1_15_12,	CTS4_N,		SEL_I2C1_0),
	PINMUX_IPSR_PHYS(IP0SR1_15_12,	SDA1,		SEL_I2C1_3),

	/* GP1_04 = SCL2 */
	PINMUX_IPSR_MSEL(IP0SR1_19_16,	GP1_04,		SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR1_19_16,	TCLK3,		SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR1_19_16,	HRTS2_N,	SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR1_19_16,	MSIOF2_SYNC,	SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR1_19_16,	RTS4_N,		SEL_I2C2_0),
	PINMUX_IPSR_PHYS(IP0SR1_19_16,	SCL2,		SEL_I2C2_3),

	/* GP1_05 = SDA2 */
	PINMUX_IPSR_MSEL(IP0SR1_23_20,	GP1_05,		SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR1_23_20,	MSIOF2_SCK,	SEL_I2C2_0),
	PINMUX_IPSR_MSEL(IP0SR1_23_20,	SCK4,		SEL_I2C2_0),
	PINMUX_IPSR_PHYS(IP0SR1_23_20,	SDA2,		SEL_I2C2_3),

	/* GP1_06 = SCL3 */
	PINMUX_IPSR_MSEL(IP0SR1_27_24,	GP1_06,		SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP0SR1_27_24,	MSIOF2_RXD,	SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP0SR1_27_24,	RX4,		SEL_I2C3_0),
	PINMUX_IPSR_PHYS(IP0SR1_27_24,	SCL3,		SEL_I2C3_3),

	/* GP1_07 = SDA3 */
	PINMUX_IPSR_MSEL(IP0SR1_31_28,	GP1_07,		SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP0SR1_31_28,	MSIOF2_TXD,	SEL_I2C3_0),
	PINMUX_IPSR_MSEL(IP0SR1_31_28,	TX4,		SEL_I2C3_0),
	PINMUX_IPSR_PHYS(IP0SR1_31_28,	SDA3,		SEL_I2C3_3),

	/* GP1_08 = SCL4 */
	PINMUX_IPSR_NOGM(0, GP1_08,	SEL_I2C4_0),
	PINMUX_IPSR_PHYS_NOFN(SCL4,	SEL_I2C4_3),

	/* GP1_09 = SDA4 */
	PINMUX_IPSR_NOGM(0, GP1_09,	SEL_I2C4_0),
	PINMUX_IPSR_PHYS_NOFN(SDA4,	SEL_I2C4_3),

	/* GP1_10 = SCL5 */
	PINMUX_IPSR_NOGM(0, GP1_10,	SEL_I2C5_0),
	PINMUX_IPSR_PHYS_NOFN(SCL5,	SEL_I2C5_3),

	/* GP1_11 = SDA5 */
	PINMUX_IPSR_NOGM(0, GP1_11,	SEL_I2C5_0),
	PINMUX_IPSR_PHYS_NOFN(SDA5,	SEL_I2C5_3),

	/* IP0SR4 */
	PINMUX_IPSR_GPSR(IP0SR4_3_0,	GP4_00),
	PINMUX_IPSR_GPSR(IP0SR4_3_0,	MSPI4SC),
	PINMUX_IPSR_GPSR(IP0SR4_3_0,	TAUD0I2),
	PINMUX_IPSR_GPSR(IP0SR4_3_0,	TAUD0O2),

	PINMUX_IPSR_GPSR(IP0SR4_7_4,	GP4_01),
	PINMUX_IPSR_GPSR(IP0SR4_7_4,	MSPI4SI),
	PINMUX_IPSR_GPSR(IP0SR4_7_4,	TAUD0I4),
	PINMUX_IPSR_GPSR(IP0SR4_7_4,	TAUD0O4),

	PINMUX_IPSR_GPSR(IP0SR4_11_8,	GP4_02),
	PINMUX_IPSR_GPSR(IP0SR4_11_8,	MSPI4SO_MSPI4DCS),
	PINMUX_IPSR_GPSR(IP0SR4_11_8,	TAUD0I3),
	PINMUX_IPSR_GPSR(IP0SR4_11_8,	TAUD0O3),

	PINMUX_IPSR_GPSR(IP0SR4_15_12,	GP4_03),
	PINMUX_IPSR_GPSR(IP0SR4_15_12,	MSPI4CSS1),
	PINMUX_IPSR_GPSR(IP0SR4_15_12,	TAUD0I6),
	PINMUX_IPSR_GPSR(IP0SR4_15_12,	TAUD0O6),
	PINMUX_IPSR_GPSR(IP0SR4_15_12,	MSPI5SO_MSPI5DCS),

	PINMUX_IPSR_GPSR(IP0SR4_19_16,	GP4_04),
	PINMUX_IPSR_GPSR(IP0SR4_19_16,	MSPI4CSS0),
	PINMUX_IPSR_GPSR(IP0SR4_19_16,	MSPI4SSI_N),
	PINMUX_IPSR_GPSR(IP0SR4_19_16,	TAUD0I5),
	PINMUX_IPSR_GPSR(IP0SR4_19_16,	TAUD0O5),
	PINMUX_IPSR_GPSR(IP0SR4_19_16,	MSPI5SC),

	PINMUX_IPSR_GPSR(IP0SR4_23_20,	GP4_05),
	PINMUX_IPSR_GPSR(IP0SR4_23_20,	MSPI4CSS3),
	PINMUX_IPSR_GPSR(IP0SR4_23_20,	TAUD0I8),
	PINMUX_IPSR_GPSR(IP0SR4_23_20,	TAUD0O8),
	PINMUX_IPSR_GPSR(IP0SR4_23_20,	MSPI5SSI_N),

	PINMUX_IPSR_GPSR(IP0SR4_27_24,	GP4_06),
	PINMUX_IPSR_GPSR(IP0SR4_27_24,	MSPI4CSS2),
	PINMUX_IPSR_GPSR(IP0SR4_27_24,	TAUD0I7),
	PINMUX_IPSR_GPSR(IP0SR4_27_24,	TAUD0O7),
	PINMUX_IPSR_GPSR(IP0SR4_27_24,	MSPI5SI),

	PINMUX_IPSR_GPSR(IP0SR4_31_28,	GP4_07),
	PINMUX_IPSR_GPSR(IP0SR4_31_28,	MSPI4CSS5),
	PINMUX_IPSR_GPSR(IP0SR4_31_28,	TAUD0I10),
	PINMUX_IPSR_GPSR(IP0SR4_31_28,	TAUD0O10),
	PINMUX_IPSR_GPSR(IP0SR4_31_28,	MSPI5CSS1),

	/* IP1SR4 */
	PINMUX_IPSR_GPSR(IP1SR4_3_0,	GP4_08),
	PINMUX_IPSR_GPSR(IP1SR4_3_0,	MSPI4CSS4),
	PINMUX_IPSR_GPSR(IP1SR4_3_0,	TAUD0I9),
	PINMUX_IPSR_GPSR(IP1SR4_3_0,	TAUD0O9),
	PINMUX_IPSR_GPSR(IP1SR4_3_0,	MSPI5CSS0),

	PINMUX_IPSR_GPSR(IP1SR4_7_4,	GP4_09),
	PINMUX_IPSR_GPSR(IP1SR4_7_4,	MSPI4CSS7),
	PINMUX_IPSR_GPSR(IP1SR4_7_4,	TAUD0I12),
	PINMUX_IPSR_GPSR(IP1SR4_7_4,	TAUD0O12),
	PINMUX_IPSR_GPSR(IP1SR4_7_4,	MSPI5CSS3),

	PINMUX_IPSR_GPSR(IP1SR4_11_8,	GP4_10),
	PINMUX_IPSR_GPSR(IP1SR4_11_8,	MSPI4CSS6),
	PINMUX_IPSR_GPSR(IP1SR4_11_8,	TAUD0I11),
	PINMUX_IPSR_GPSR(IP1SR4_11_8,	TAUD0O11),
	PINMUX_IPSR_GPSR(IP1SR4_11_8,	MSPI5CSS2),

	PINMUX_IPSR_GPSR(IP1SR4_15_12,	GP4_11),
	PINMUX_IPSR_GPSR(IP1SR4_15_12,	ERRORIN0_N),
	PINMUX_IPSR_GPSR(IP1SR4_15_12,	TAUD0I14),
	PINMUX_IPSR_GPSR(IP1SR4_15_12,	TAUD0O14),

	PINMUX_IPSR_GPSR(IP1SR4_19_16,	GP4_12),
	PINMUX_IPSR_GPSR(IP1SR4_19_16,	ERROROUT_C_N),
	PINMUX_IPSR_GPSR(IP1SR4_19_16,	TAUD0I13),
	PINMUX_IPSR_GPSR(IP1SR4_19_16,	TAUD0O13),

	PINMUX_IPSR_GPSR(IP1SR4_27_24,	GP4_14),
	PINMUX_IPSR_GPSR(IP1SR4_27_24,	ERRORIN1_N),
	PINMUX_IPSR_GPSR(IP1SR4_27_24,	TAUD0I15),
	PINMUX_IPSR_GPSR(IP1SR4_27_24,	TAUD0O15),

	PINMUX_IPSR_GPSR(IP1SR4_31_28,	GP4_15),
	PINMUX_IPSR_GPSR(IP1SR4_31_28,	MSPI1CSS3),
	PINMUX_IPSR_GPSR(IP1SR4_31_28,	TAUD1I1),
	PINMUX_IPSR_GPSR(IP1SR4_31_28,	TAUD1O1),

	/* IP2SR4 */
	PINMUX_IPSR_GPSR(IP2SR4_3_0,	GP4_16),
	PINMUX_IPSR_GPSR(IP2SR4_3_0,	TAUD1I0),
	PINMUX_IPSR_GPSR(IP2SR4_3_0,	TAUD1O0),

	PINMUX_IPSR_GPSR(IP2SR4_7_4,	GP4_17),
	PINMUX_IPSR_GPSR(IP2SR4_7_4,	MSPI1CSS5),
	PINMUX_IPSR_GPSR(IP2SR4_7_4,	TAUD1I3),
	PINMUX_IPSR_GPSR(IP2SR4_7_4,	TAUD1O3),

	PINMUX_IPSR_GPSR(IP2SR4_11_8,	GP4_18),
	PINMUX_IPSR_GPSR(IP2SR4_11_8,	MSPI1CSS4),
	PINMUX_IPSR_GPSR(IP2SR4_11_8,	TAUD1I2),
	PINMUX_IPSR_GPSR(IP2SR4_11_8,	TAUD1O2),

	PINMUX_IPSR_GPSR(IP2SR4_15_12,	GP4_19),
	PINMUX_IPSR_GPSR(IP2SR4_15_12,	MSPI1CSS6),
	PINMUX_IPSR_GPSR(IP2SR4_15_12,	TAUD1I4),
	PINMUX_IPSR_GPSR(IP2SR4_15_12,	TAUD1O4),

	PINMUX_IPSR_GPSR(IP2SR4_19_16,	MSPI0SC),
	PINMUX_IPSR_GPSR(IP2SR4_19_16,	MSPI1CSS7),
	PINMUX_IPSR_GPSR(IP2SR4_19_16,	TAUD1I5),
	PINMUX_IPSR_GPSR(IP2SR4_19_16,	TAUD1O5),

	PINMUX_IPSR_GPSR(IP2SR4_23_20,	MSPI0SI),
	PINMUX_IPSR_GPSR(IP2SR4_23_20,	TAUD1I7),
	PINMUX_IPSR_GPSR(IP2SR4_23_20,	TAUD1O7),

	PINMUX_IPSR_GPSR(IP2SR4_27_24,	MSPI0SO_MSPI0DCS),
	PINMUX_IPSR_GPSR(IP2SR4_27_24,	TAUD1I6),
	PINMUX_IPSR_GPSR(IP2SR4_27_24,	TAUD1O6),

	PINMUX_IPSR_GPSR(IP2SR4_31_28,	MSPI0CSS1),
	PINMUX_IPSR_GPSR(IP2SR4_31_28,	TAUD1I9),
	PINMUX_IPSR_GPSR(IP2SR4_31_28,	TAUD1O9),

	/* IP3SR4 */
	PINMUX_IPSR_GPSR(IP3SR4_3_0,	MSPI0CSS0),
	PINMUX_IPSR_GPSR(IP3SR4_3_0,	MSPI0SSI_N),
	PINMUX_IPSR_GPSR(IP3SR4_3_0,	TAUD1I8),
	PINMUX_IPSR_GPSR(IP3SR4_3_0,	TAUD1O8),

	PINMUX_IPSR_GPSR(IP3SR4_7_4,	MSPI1SI),
	PINMUX_IPSR_GPSR(IP3SR4_7_4,	MSPI0CSS4),
	PINMUX_IPSR_GPSR(IP3SR4_7_4,	TAUD1I12),
	PINMUX_IPSR_GPSR(IP3SR4_7_4,	TAUD1O12),

	PINMUX_IPSR_GPSR(IP3SR4_11_8,	MSPI1SO_MSPI1DCS),
	PINMUX_IPSR_GPSR(IP3SR4_11_8,	MSPI0CSS3),
	PINMUX_IPSR_GPSR(IP3SR4_11_8,	TAUD1I11),
	PINMUX_IPSR_GPSR(IP3SR4_11_8,	TAUD1O11),

	PINMUX_IPSR_GPSR(IP3SR4_15_12,	MSPI1CSS0),
	PINMUX_IPSR_GPSR(IP3SR4_15_12,	MSPI1SSI_N),
	PINMUX_IPSR_GPSR(IP3SR4_15_12,	MSPI0CSS5),
	PINMUX_IPSR_GPSR(IP3SR4_15_12,	TAUD1I13),
	PINMUX_IPSR_GPSR(IP3SR4_15_12,	TAUD1O13),

	PINMUX_IPSR_GPSR(IP3SR4_19_16,	MSPI1SC),
	PINMUX_IPSR_GPSR(IP3SR4_19_16,	MSPI0CSS2),
	PINMUX_IPSR_GPSR(IP3SR4_19_16,	TAUD1I10),
	PINMUX_IPSR_GPSR(IP3SR4_19_16,	TAUD1O10),

	PINMUX_IPSR_GPSR(IP3SR4_23_20,	MSPI1CSS2),
	PINMUX_IPSR_GPSR(IP3SR4_23_20,	MSPI0CSS7),
	PINMUX_IPSR_GPSR(IP3SR4_23_20,	TAUD1I15),
	PINMUX_IPSR_GPSR(IP3SR4_23_20,	TAUD1O15),

	PINMUX_IPSR_GPSR(IP3SR4_27_24,	MSPI1CSS1),
	PINMUX_IPSR_GPSR(IP3SR4_27_24,	MSPI0CSS6),
	PINMUX_IPSR_GPSR(IP3SR4_27_24,	TAUD1I14),
	PINMUX_IPSR_GPSR(IP3SR4_27_24,	TAUD1O14),

	/* IP0SR5 */
	PINMUX_IPSR_GPSR(IP0SR5_3_0,	RIIC0SCL),
	PINMUX_IPSR_GPSR(IP0SR5_3_0,	TAUD0I0),
	PINMUX_IPSR_GPSR(IP0SR5_3_0,	TAUD0O0),

	PINMUX_IPSR_GPSR(IP0SR5_7_4,	RIIC0SDA),
	PINMUX_IPSR_GPSR(IP0SR5_7_4,	TAUD0I1),
	PINMUX_IPSR_GPSR(IP0SR5_7_4,	TAUD0O1),

	PINMUX_IPSR_GPSR(IP0SR5_11_8,	ETNB0MD),

	PINMUX_IPSR_GPSR(IP0SR5_15_12,	ETNB0WOL),

	PINMUX_IPSR_GPSR(IP0SR5_19_16,	ETNB0LINKSTA),

	PINMUX_IPSR_GPSR(IP0SR5_23_20,	ETNB0MDC),

	PINMUX_IPSR_GPSR(IP0SR5_27_24,	ETNB0RXCLK),
	PINMUX_IPSR_GPSR(IP0SR5_27_24,	ETNB0CRS_DV),

	PINMUX_IPSR_GPSR(IP0SR5_31_28,	ETNB0TXCLK),
	PINMUX_IPSR_GPSR(IP0SR5_31_28,	ETNB0REFCLK),

	/* IP0SR6 */
	PINMUX_IPSR_GPSR(IP1SR6_3_0,	RLIN37TX),

	PINMUX_IPSR_GPSR(IP1SR6_7_4,	RLIN37RX_INTP23),

	PINMUX_IPSR_GPSR(IP1SR6_11_8,	RLIN36TX),

	PINMUX_IPSR_GPSR(IP1SR6_15_12,	RLIN36RX_INTP22),

	PINMUX_IPSR_GPSR(IP1SR6_19_16,	RLIN35TX),

	PINMUX_IPSR_GPSR(IP1SR6_23_20,	RLIN35RX_INTP21),

	PINMUX_IPSR_GPSR(IP1SR6_27_24,	RLIN34TX),

	PINMUX_IPSR_GPSR(IP1SR6_31_28,	RLIN34RX_INTP20),

	/* IP1SR6 */
	PINMUX_IPSR_GPSR(IP1SR6_3_0,	RLIN33TX),
	PINMUX_IPSR_GPSR(IP1SR6_3_0,	TAUJ3O3),
	PINMUX_IPSR_GPSR(IP1SR6_3_0,	TAUJ3I3),

	PINMUX_IPSR_GPSR(IP1SR6_7_4,	RLIN33RX_INTP19),
	PINMUX_IPSR_GPSR(IP1SR6_7_4,	TAUJ3O2),
	PINMUX_IPSR_GPSR(IP1SR6_7_4,	TAUJ3I2),

	PINMUX_IPSR_GPSR(IP1SR6_11_8,	RLIN32TX),
	PINMUX_IPSR_GPSR(IP1SR6_11_8,	TAUJ3O1),
	PINMUX_IPSR_GPSR(IP1SR6_11_8,	TAUJ3I1),

	PINMUX_IPSR_GPSR(IP1SR6_15_12,	RLIN32RX_INTP18),
	PINMUX_IPSR_GPSR(IP1SR6_15_12,	TAUJ3O0),
	PINMUX_IPSR_GPSR(IP1SR6_15_12,	TAUJ3I0),

	PINMUX_IPSR_GPSR(IP1SR6_19_16,	RLIN31TX),
	PINMUX_IPSR_GPSR(IP1SR6_19_16,	TAUJ1I3),
	PINMUX_IPSR_GPSR(IP1SR6_19_16,	TAUJ1O3),

	PINMUX_IPSR_GPSR(IP1SR6_23_20,	RLIN31RX_INTP17),
	PINMUX_IPSR_GPSR(IP1SR6_23_20,	TAUJ1I2),
	PINMUX_IPSR_GPSR(IP1SR6_23_20,	TAUJ1O2),

	PINMUX_IPSR_GPSR(IP1SR6_27_24,	RLIN30TX),
	PINMUX_IPSR_GPSR(IP1SR6_27_24,	TAUJ1I1),
	PINMUX_IPSR_GPSR(IP1SR6_27_24,	TAUJ1O1),

	PINMUX_IPSR_GPSR(IP1SR6_31_28,	RLIN30RX_INTP16),
	PINMUX_IPSR_GPSR(IP1SR6_31_28,	TAUJ1I0),
	PINMUX_IPSR_GPSR(IP1SR6_31_28,	TAUJ1O0),

	/* IP2SR6 */
	PINMUX_IPSR_GPSR(IP2SR6_3_0,	INTP37),
	PINMUX_IPSR_GPSR(IP2SR6_3_0,	EXTCLK0O),

	PINMUX_IPSR_GPSR(IP2SR6_7_4,	INTP36),
	PINMUX_IPSR_GPSR(IP2SR6_7_4,	RTCA0OUT),

	PINMUX_IPSR_GPSR(IP2SR6_11_8,	INTP32),
	PINMUX_IPSR_GPSR(IP2SR6_11_8,	FLXA0STPWT),

	/* IP0SR7 */
	PINMUX_IPSR_GPSR(IP0SR7_3_0,	CAN0TX),
	PINMUX_IPSR_GPSR(IP0SR7_3_0,	RSENT0SPCO),
	PINMUX_IPSR_GPSR(IP0SR7_3_0,	MSPI2SO_MSPI2DCS),

	PINMUX_IPSR_GPSR(IP0SR7_7_4,	CAN0RX_INTP0),
	PINMUX_IPSR_GPSR(IP0SR7_7_4,	RSENT0RX),
	PINMUX_IPSR_GPSR(IP0SR7_7_4,	RSENT0RX_RSENT0SPCO),
	PINMUX_IPSR_GPSR(IP0SR7_7_4,	MSPI2SC),

	PINMUX_IPSR_GPSR(IP0SR7_11_8,	CAN1TX),
	PINMUX_IPSR_GPSR(IP0SR7_11_8,	RSENT1SPCO),
	PINMUX_IPSR_GPSR(IP0SR7_11_8,	MSPI2SSI_N),
	PINMUX_IPSR_GPSR(IP0SR7_11_8,	MSPI2CSS0),

	PINMUX_IPSR_GPSR(IP0SR7_15_12,	CAN1RX_INTP1),
	PINMUX_IPSR_GPSR(IP0SR7_15_12,	RSENT1RX),
	PINMUX_IPSR_GPSR(IP0SR7_15_12,	RSENT1RX_RSENT1SPCO),
	PINMUX_IPSR_GPSR(IP0SR7_15_12,	MSPI2SI),

	PINMUX_IPSR_GPSR(IP0SR7_19_16,	CAN2TX),
	PINMUX_IPSR_GPSR(IP0SR7_19_16,	RSENT2SPCO),
	PINMUX_IPSR_GPSR(IP0SR7_19_16,	MSPI2CSS2),

	PINMUX_IPSR_GPSR(IP0SR7_23_20,	CAN2RX_INTP2),
	PINMUX_IPSR_GPSR(IP0SR7_23_20,	RSENT2RX),
	PINMUX_IPSR_GPSR(IP0SR7_23_20,	RSENT2RX_RSENT2SPCO),
	PINMUX_IPSR_GPSR(IP0SR7_23_20,	MSPI2CSS1),

	PINMUX_IPSR_GPSR(IP0SR7_27_24,	CAN3TX),
	PINMUX_IPSR_GPSR(IP0SR7_27_24,	RSENT3SPCO),
	PINMUX_IPSR_GPSR(IP0SR7_27_24,	MSPI2CSS4),

	PINMUX_IPSR_GPSR(IP0SR7_31_28,	CAN3RX_INTP3),
	PINMUX_IPSR_GPSR(IP0SR7_31_28,	RSENT3RX),
	PINMUX_IPSR_GPSR(IP0SR7_31_28,	RSENT3RX_RSENT3SPCO),
	PINMUX_IPSR_GPSR(IP0SR7_31_28,	MSPI2CSS3),

	/* IP1SR7 */
	PINMUX_IPSR_GPSR(IP1SR7_3_0,	CAN4TX),
	PINMUX_IPSR_GPSR(IP1SR7_3_0,	RSENT4SPCO),
	PINMUX_IPSR_GPSR(IP1SR7_3_0,	MSPI2CSS6),

	PINMUX_IPSR_GPSR(IP1SR7_7_4,	CAN4RX_INTP4),
	PINMUX_IPSR_GPSR(IP1SR7_7_4,	RSENT4RX),
	PINMUX_IPSR_GPSR(IP1SR7_7_4,	RSENT4RX_RSENT4SPCO),
	PINMUX_IPSR_GPSR(IP1SR7_7_4,	MSPI2CSS5),

	PINMUX_IPSR_GPSR(IP1SR7_11_8,	CAN5TX),
	PINMUX_IPSR_GPSR(IP1SR7_11_8,	RSENT5SPCO),

	PINMUX_IPSR_GPSR(IP1SR7_15_12,	CAN5RX_INTP5),
	PINMUX_IPSR_GPSR(IP1SR7_15_12,	RSENT5RX),
	PINMUX_IPSR_GPSR(IP1SR7_15_12,	RSENT5RX_RSENT5SPCO),
	PINMUX_IPSR_GPSR(IP1SR7_15_12,	MSPI2CSS7),

	PINMUX_IPSR_GPSR(IP1SR7_19_16,	CAN6TX),
	PINMUX_IPSR_GPSR(IP1SR7_19_16,	RSENT6SPCO),
	PINMUX_IPSR_GPSR(IP1SR7_19_16,	MSPI3SO_MSPI3DCS),

	PINMUX_IPSR_GPSR(IP1SR7_23_20,	CAN6RX_INTP6),
	PINMUX_IPSR_GPSR(IP1SR7_23_20,	RSENT6RX),
	PINMUX_IPSR_GPSR(IP1SR7_23_20,	RSENT6RX_RSENT6SPCO),
	PINMUX_IPSR_GPSR(IP1SR7_23_20,	MSPI3SC),

	PINMUX_IPSR_GPSR(IP1SR7_27_24,	CAN7TX),
	PINMUX_IPSR_GPSR(IP1SR7_27_24,	RSENT7SPCO),
	PINMUX_IPSR_GPSR(IP1SR7_27_24,	MSPI3SSI_N),

	PINMUX_IPSR_GPSR(IP1SR7_31_28,	CAN7RX_INTP7),
	PINMUX_IPSR_GPSR(IP1SR7_31_28,	RSENT7RX),
	PINMUX_IPSR_GPSR(IP1SR7_31_28,	RSENT7RX_RSENT7SPCO),
	PINMUX_IPSR_GPSR(IP1SR7_31_28,	MSPI3SI),

	/* IP2SR7 */
	PINMUX_IPSR_GPSR(IP2SR7_3_0,	CAN8TX),
	PINMUX_IPSR_GPSR(IP2SR7_3_0,	RLIN38TX),
	PINMUX_IPSR_GPSR(IP2SR7_3_0,	MSPI3CSS1),

	PINMUX_IPSR_GPSR(IP2SR7_7_4,	CAN8RX_INTP8),
	PINMUX_IPSR_GPSR(IP2SR7_7_4,	RLIN38RX_INTP24),
	PINMUX_IPSR_GPSR(IP2SR7_7_4,	MSPI3CSS0),

	PINMUX_IPSR_GPSR(IP2SR7_11_8,	CAN9TX),
	PINMUX_IPSR_GPSR(IP2SR7_11_8,	RLIN39TX),
	PINMUX_IPSR_GPSR(IP2SR7_11_8,	MSPI3CSS3),

	PINMUX_IPSR_GPSR(IP2SR7_15_12,	CAN9RX_INTP9),
	PINMUX_IPSR_GPSR(IP2SR7_15_12,	RLIN39RX_INTP25),
	PINMUX_IPSR_GPSR(IP2SR7_15_12,	MSPI3CSS2),

	PINMUX_IPSR_GPSR(IP2SR7_19_16,	CAN10TX),
	PINMUX_IPSR_GPSR(IP2SR7_19_16,	RLIN310TX),
	PINMUX_IPSR_GPSR(IP2SR7_19_16,	MSPI3CSS5),

	PINMUX_IPSR_GPSR(IP2SR7_23_20,	CAN10RX_INTP10),
	PINMUX_IPSR_GPSR(IP2SR7_23_20,	RLIN310RX_INTP26),
	PINMUX_IPSR_GPSR(IP2SR7_23_20,	MSPI3CSS4),

	PINMUX_IPSR_GPSR(IP2SR7_27_24,	CAN11TX),
	PINMUX_IPSR_GPSR(IP2SR7_27_24,	RLIN311TX),
	PINMUX_IPSR_GPSR(IP2SR7_27_24,	MSPI3CSS7),

	PINMUX_IPSR_GPSR(IP2SR7_31_28,	CAN11RX_INTP11),
	PINMUX_IPSR_GPSR(IP2SR7_31_28,	RLIN311RX_INTP27),
	PINMUX_IPSR_GPSR(IP2SR7_31_28,	MSPI3CSS6),

	/* IP3SR7 */
	PINMUX_IPSR_GPSR(IP3SR7_3_0,	CAN12TX),
	PINMUX_IPSR_GPSR(IP3SR7_3_0,	RLIN312TX),

	PINMUX_IPSR_GPSR(IP3SR7_7_4,	CAN12RX_INTP12),
	PINMUX_IPSR_GPSR(IP3SR7_7_4,	RLIN312RX_INTP28),

	PINMUX_IPSR_GPSR(IP3SR7_11_8,	CAN13TX),
	PINMUX_IPSR_GPSR(IP3SR7_11_8,	RLIN313TX),
	PINMUX_IPSR_GPSR(IP3SR7_11_8,	FLXA0RXDB),

	PINMUX_IPSR_GPSR(IP3SR7_15_12,	CAN13RX_INTP13),
	PINMUX_IPSR_GPSR(IP3SR7_15_12,	RLIN313RX_INTP29),
	PINMUX_IPSR_GPSR(IP3SR7_15_12,	FLXA0RXDA),

	PINMUX_IPSR_GPSR(IP3SR7_19_16,	CAN14TX),
	PINMUX_IPSR_GPSR(IP3SR7_19_16,	RLIN314TX),
	PINMUX_IPSR_GPSR(IP3SR7_19_16,	FLXA0TXDB),

	PINMUX_IPSR_GPSR(IP3SR7_23_20,	CAN14RX_INTP14),
	PINMUX_IPSR_GPSR(IP3SR7_23_20,	RLIN314RX_INTP30),
	PINMUX_IPSR_GPSR(IP3SR7_23_20,	FLXA0TXDA),

	PINMUX_IPSR_GPSR(IP3SR7_27_24,	CAN15TX),
	PINMUX_IPSR_GPSR(IP3SR7_27_24,	RLIN315TX),
	PINMUX_IPSR_GPSR(IP3SR7_27_24,	FLXA0TXENB),

	PINMUX_IPSR_GPSR(IP3SR7_31_28,	CAN15RX_INTP15),
	PINMUX_IPSR_GPSR(IP3SR7_31_28,	RLIN315RX_INTP31),
	PINMUX_IPSR_GPSR(IP3SR7_31_28,	FLXA0TXENA),
};

/*
 * Pins not associated with a GPIO port.
 */
enum {
	GP_ASSIGN_LAST(),
	NOGP_ALL(),
};

static const struct sh_pfc_pin pinmux_pins[] = {
	PINMUX_GPIO_GP_ALL(),
};

/* - TSN0 ------------------------------------------------ */
static const unsigned int tsn0_link_pins[] = {
	/* TSN0_LINK */
	RCAR_GP_PIN(3, 8),
};
static const unsigned int tsn0_link_mux[] = {
	TSN0_LINK_MARK,
};
static const unsigned int tsn0_magic_pins[] = {
	/* TSN0_MAGIC */
	RCAR_GP_PIN(3, 12),
};
static const unsigned int tsn0_magic_mux[] = {
	TSN0_MAGIC_MARK,
};
static const unsigned int tsn0_phy_int_pins[] = {
	/* TSN0_PHY_INT */
	RCAR_GP_PIN(3, 10),
};
static const unsigned int tsn0_phy_int_mux[] = {
	TSN0_PHY_INT_MARK,
};
static const unsigned int tsn0_mdio_pins[] = {
	/* TSN0_MDC, TSN0_MDIO */
	RCAR_GP_PIN(3, 4), RCAR_GP_PIN(3, 2),
};
static const unsigned int tsn0_mdio_mux[] = {
	TSN0_MDC_MARK, TSN0_MDIO_MARK,
};

static const unsigned int tsn0_avtp_pps_pins[] = {
	/* TSN0_AVTP_PPS */
	RCAR_GP_PIN(3, 16),
};
static const unsigned int tsn0_avtp_pps_mux[] = {
	TSN0_AVTP_PPS_MARK,
};
static const unsigned int tsn0_avtp_capture_pins[] = {
	/* TSN0_AVTP_CAPTURE */
	RCAR_GP_PIN(3, 18),
};
static const unsigned int tsn0_avtp_capture_mux[] = {
	TSN0_AVTP_CAPTURE_MARK,
};
static const unsigned int tsn0_avtp_match_pins[] = {
	/* TSN0_AVTP_MATCH */
	RCAR_GP_PIN(3, 17),
};
static const unsigned int tsn0_avtp_match_mux[] = {
	TSN0_AVTP_MATCH_MARK,
};

/* - TSN1 ------------------------------------------------ */
static const unsigned int tsn1_link_pins[] = {
	/* TSN1_LINK */
	RCAR_GP_PIN(3, 6),
};
static const unsigned int tsn1_link_mux[] = {
	TSN1_LINK_MARK,
};
static const unsigned int tsn1_phy_int_pins[] = {
	/* TSN1_PHY_INT */
	RCAR_GP_PIN(3, 11),
};
static const unsigned int tsn1_phy_int_mux[] = {
	TSN1_PHY_INT_MARK,
};
static const unsigned int tsn1_mdio_pins[] = {
	/* TSN1_MDC, TSN1_MDIO */
	RCAR_GP_PIN(3, 5), RCAR_GP_PIN(3, 0),
};
static const unsigned int tsn1_mdio_mux[] = {
	TSN1_MDC_MARK, TSN1_MDIO_MARK,
};
static const unsigned int tsn1_avtp_pps_pins[] = {
	/* TSN1_AVTP_PPS */
	RCAR_GP_PIN(3, 13),
};
static const unsigned int tsn1_avtp_pps_mux[] = {
	TSN0_AVTP_PPS_MARK,
};
static const unsigned int tsn1_avtp_capture_pins[] = {
	/* TSN1_AVTP_CAPTURE */
	RCAR_GP_PIN(3, 15),
};
static const unsigned int tsn1_avtp_capture_mux[] = {
	TSN1_AVTP_CAPTURE_MARK,
};
static const unsigned int tsn1_avtp_match_pins[] = {
	/* TSN1_AVTP_MATCH */
	RCAR_GP_PIN(3, 14),
};
static const unsigned int tsn1_avtp_match_mux[] = {
	TSN1_AVTP_MATCH_MARK,
};

/* - TSN2 ------------------------------------------------ */
static const unsigned int tsn2_link_pins[] = {
	/* TSN2_LINK */
	RCAR_GP_PIN(3, 7),
};
static const unsigned int tsn2_link_mux[] = {
	TSN2_LINK_MARK,
};
static const unsigned int tsn2_phy_int_pins[] = {
	/* TSN2_PHY_INT */
	RCAR_GP_PIN(3, 9),
};
static const unsigned int tsn2_phy_int_mux[] = {
	TSN2_PHY_INT_MARK,
};
static const unsigned int tsn2_mdio_pins[] = {
	/* TSN2_MDC, TSN2_MDIO */
	RCAR_GP_PIN(3, 3), RCAR_GP_PIN(3, 1),
};
static const unsigned int tsn2_mdio_mux[] = {
	TSN2_MDC_MARK, TSN2_MDIO_MARK,
};

/* - HSCIF0 ----------------------------------------------------------------- */
static const unsigned int hscif0_data_pins[] = {
	/* HRX0, HTX0 */
	RCAR_GP_PIN(0, 2), RCAR_GP_PIN(0, 3),
};
static const unsigned int hscif0_data_mux[] = {
	HRX0_MARK, HTX0_MARK,
};
static const unsigned int hscif0_clk_pins[] = {
	/* HSCK0 */
	RCAR_GP_PIN(0, 1),
};
static const unsigned int hscif0_clk_mux[] = {
	HSCK0_MARK,
};
static const unsigned int hscif0_ctrl_pins[] = {
	/* HRTS0#, HCTS0# */
	RCAR_GP_PIN(0, 5), RCAR_GP_PIN(0, 4),
};
static const unsigned int hscif0_ctrl_mux[] = {
	HRTS0_N_MARK, HCTS0_N_MARK,
};

/* - HSCIF1 ----------------------------------------------------------------- */
static const unsigned int hscif1_data_pins[] = {
	/* HRX1, HTX1 */
	RCAR_GP_PIN(0, 6), RCAR_GP_PIN(0, 7),
};
static const unsigned int hscif1_data_mux[] = {
	HRX1_MARK, HTX1_MARK,
};
static const unsigned int hscif1_clk_pins[] = {
	/* HSCK1 */
	RCAR_GP_PIN(0, 8),
};
static const unsigned int hscif1_clk_mux[] = {
	HSCK1_MARK,
};
static const unsigned int hscif1_ctrl_pins[] = {
	/* HRTS1#, HCTS1# */
	RCAR_GP_PIN(0, 9), RCAR_GP_PIN(0, 10),
};
static const unsigned int hscif1_ctrl_mux[] = {
	HRTS1_N_MARK, HCTS1_N_MARK,
};

/* - I2C0 ------------------------------------------------------------------- */
static const unsigned int i2c0_pins[] = {
	/* SDA0, SCL0 */
	RCAR_GP_PIN(1, 1), RCAR_GP_PIN(1, 0),
};
static const unsigned int i2c0_mux[] = {
	SDA0_MARK, SCL0_MARK,
};

/* - I2C1 ------------------------------------------------------------------- */
static const unsigned int i2c1_pins[] = {
	/* SDA1, SCL1 */
	RCAR_GP_PIN(1, 3), RCAR_GP_PIN(1, 2),
};
static const unsigned int i2c1_mux[] = {
	SDA1_MARK, SCL1_MARK,
};

/* - I2C2 ------------------------------------------------------------------- */
static const unsigned int i2c2_pins[] = {
	/* SDA2, SCL2 */
	RCAR_GP_PIN(1, 5), RCAR_GP_PIN(1, 4),
};
static const unsigned int i2c2_mux[] = {
	SDA2_MARK, SCL2_MARK,
};

/* - I2C3 ------------------------------------------------------------------- */
static const unsigned int i2c3_pins[] = {
	/* SDA3, SCL3 */
	RCAR_GP_PIN(1, 7), RCAR_GP_PIN(1, 6),
};
static const unsigned int i2c3_mux[] = {
	SDA3_MARK, SCL3_MARK,
};

/* - I2C4 ------------------------------------------------------------------- */
static const unsigned int i2c4_pins[] = {
	/* SDA4, SCL4 */
	RCAR_GP_PIN(1, 9), RCAR_GP_PIN(1, 8),
};
static const unsigned int i2c4_mux[] = {
	SDA4_MARK, SCL4_MARK,
};

/* - I2C5 ------------------------------------------------------------------- */
static const unsigned int i2c5_pins[] = {
	/* SDA5, SCL5 */
	RCAR_GP_PIN(1, 11), RCAR_GP_PIN(1, 10),
};
static const unsigned int i2c5_mux[] = {
	SDA5_MARK, SCL5_MARK,
};


/* - INTC-EX ---------------------------------------------------------------- */
static const unsigned int intc_ex_irq0_pins[] = {
	/* IRQ0 */
	RCAR_GP_PIN(0, 17),
};
static const unsigned int intc_ex_irq0_mux[] = {
	IRQ0_MARK,
};
static const unsigned int intc_ex_irq1_pins[] = {
	/* IRQ1 */
	RCAR_GP_PIN(0, 18),
};
static const unsigned int intc_ex_irq1_mux[] = {
	IRQ1_MARK,
};
static const unsigned int intc_ex_irq2_pins[] = {
	/* IRQ2 */
	RCAR_GP_PIN(0, 19),
};
static const unsigned int intc_ex_irq2_mux[] = {
	IRQ2_MARK,
};
static const unsigned int intc_ex_irq3_pins[] = {
	/* IRQ3 */
	RCAR_GP_PIN(0, 20),
};
static const unsigned int intc_ex_irq3_mux[] = {
	IRQ3_MARK,
};

/* - MMC -------------------------------------------------------------------- */
static const unsigned int mmc_data1_pins[] = {
	/* MMC_SD_D0 */
	RCAR_GP_PIN(1, 13),
};
static const unsigned int mmc_data1_mux[] = {
	MMC_SD_D0_MARK,
};
static const unsigned int mmc_data4_pins[] = {
	/* MMC_SD_D[0:3] */
	RCAR_GP_PIN(1, 13), RCAR_GP_PIN(1, 14),
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 16),
};
static const unsigned int mmc_data4_mux[] = {
	MMC_SD_D0_MARK, MMC_SD_D1_MARK,
	MMC_SD_D2_MARK, MMC_SD_D3_MARK,
};
static const unsigned int mmc_data8_pins[] = {
	/* MMC_SD_D[0:3], MMC_D[4:7] */
	RCAR_GP_PIN(1, 13), RCAR_GP_PIN(1, 14),
	RCAR_GP_PIN(1, 15), RCAR_GP_PIN(1, 16),
	RCAR_GP_PIN(1, 18), RCAR_GP_PIN(1, 17),
	RCAR_GP_PIN(1, 19), RCAR_GP_PIN(1, 21),
};
static const unsigned int mmc_data8_mux[] = {
	MMC_SD_D0_MARK, MMC_SD_D1_MARK,
	MMC_SD_D2_MARK, MMC_SD_D3_MARK,
	MMC_D4_MARK, MMC_D5_MARK,
	MMC_D6_MARK, MMC_D7_MARK,
};
static const unsigned int mmc_ctrl_pins[] = {
	/* MMC_SD_CLK, MMC_SD_CMD */
	RCAR_GP_PIN(1, 12), RCAR_GP_PIN(1, 22),
};
static const unsigned int mmc_ctrl_mux[] = {
	MMC_SD_CLK_MARK, MMC_SD_CMD_MARK,
};
static const unsigned int mmc_cd_pins[] = {
	/* SD_CD */
	RCAR_GP_PIN(1, 23),
};
static const unsigned int mmc_cd_mux[] = {
	SD_CD_MARK,
};
static const unsigned int mmc_wp_pins[] = {
	/* SD_WP */
	RCAR_GP_PIN(1, 24),
};
static const unsigned int mmc_wp_mux[] = {
	SD_WP_MARK,
};
static const unsigned int mmc_ds_pins[] = {
	/* MMC_DS */
	RCAR_GP_PIN(1, 20),
};
static const unsigned int mmc_ds_mux[] = {
	MMC_DS_MARK,
};

/* - QSPI0 ------------------------------------------------------------------ */
static const unsigned int qspi0_ctrl_pins[] = {
	/* SPCLK, SSL */
	RCAR_GP_PIN(2, 10), RCAR_GP_PIN(2, 13),
};
static const unsigned int qspi0_ctrl_mux[] = {
	QSPI0_SPCLK_MARK, QSPI0_SSL_MARK,
};
static const unsigned int qspi0_data2_pins[] = {
	/* MOSI_IO0, MISO_IO1 */
	RCAR_GP_PIN(2, 9), RCAR_GP_PIN(2, 12),
};
static const unsigned int qspi0_data2_mux[] = {
	QSPI0_MOSI_IO0_MARK, QSPI0_MISO_IO1_MARK,
};
static const unsigned int qspi0_data4_pins[] = {
	/* MOSI_IO0, MISO_IO1, IO2, IO3 */
	RCAR_GP_PIN(2, 9), RCAR_GP_PIN(2, 12),
	RCAR_GP_PIN(2, 11), RCAR_GP_PIN(2, 14),
};
static const unsigned int qspi0_data4_mux[] = {
	QSPI0_MOSI_IO0_MARK, QSPI0_MISO_IO1_MARK,
	QSPI0_IO2_MARK, QSPI0_IO3_MARK
};

/* - QSPI1 ------------------------------------------------------------------ */
static const unsigned int qspi1_ctrl_pins[] = {
	/* SPCLK, SSL */
	RCAR_GP_PIN(2, 8), RCAR_GP_PIN(2, 3),
};
static const unsigned int qspi1_ctrl_mux[] = {
	QSPI1_SPCLK_MARK, QSPI1_SSL_MARK,
};
static const unsigned int qspi1_data2_pins[] = {
	/* MOSI_IO0, MISO_IO1 */
	RCAR_GP_PIN(2, 7), RCAR_GP_PIN(2, 5),
};
static const unsigned int qspi1_data2_mux[] = {
	QSPI1_MOSI_IO0_MARK, QSPI1_MISO_IO1_MARK,
};
static const unsigned int qspi1_data4_pins[] = {
	/* MOSI_IO0, MISO_IO1, IO2, IO3 */
	RCAR_GP_PIN(2, 7), RCAR_GP_PIN(2, 5),
	RCAR_GP_PIN(2, 6), RCAR_GP_PIN(2, 4),
};
static const unsigned int qspi1_data4_mux[] = {
	QSPI1_MOSI_IO0_MARK, QSPI1_MISO_IO1_MARK,
	QSPI1_IO2_MARK, QSPI1_IO3_MARK
};

/* - SCIF0 ------------------------------------------------------------------ */
static const unsigned int scif0_data_pins[] = {
	/* RX0, TX0 */
	RCAR_GP_PIN(0, 6), RCAR_GP_PIN(0, 7),
};
static const unsigned int scif0_data_mux[] = {
	RX0_MARK, TX0_MARK,
};
static const unsigned int scif0_clk_pins[] = {
	/* SCK0 */
	RCAR_GP_PIN(0, 8),
};
static const unsigned int scif0_clk_mux[] = {
	SCK0_MARK,
};
static const unsigned int scif0_ctrl_pins[] = {
	/* RTS0#, CTS0# */
	RCAR_GP_PIN(0, 9), RCAR_GP_PIN(0, 10),
};
static const unsigned int scif0_ctrl_mux[] = {
	RTS0_N_MARK, CTS0_N_MARK,
};

/* - SCIF1 ------------------------------------------------------------------ */
static const unsigned int scif1_data_pins[] = {
	/* RX1, TX1 */
	RCAR_GP_PIN(0, 12), RCAR_GP_PIN(0, 13),
};
static const unsigned int scif1_data_mux[] = {
	RX1_MARK, TX1_MARK,
};
static const unsigned int scif1_clk_pins[] = {
	/* SCK1 */
	RCAR_GP_PIN(0, 14),
};
static const unsigned int scif1_clk_mux[] = {
	SCK1_MARK,
};
static const unsigned int scif1_ctrl_pins[] = {
	/* RTS1#, CTS1# */
	RCAR_GP_PIN(0, 15), RCAR_GP_PIN(0, 11),
};
static const unsigned int scif1_ctrl_mux[] = {
	RTS1_N_MARK, CTS1_N_MARK,
};

/* - SCIF3 ------------------------------------------------------------------ */
static const unsigned int scif3_data_pins[] = {
	/* RX3, TX3 */
	RCAR_GP_PIN(0, 2), RCAR_GP_PIN(0, 3),
};
static const unsigned int scif3_data_mux[] = {
	RX3_MARK, TX3_MARK,
};
static const unsigned int scif3_clk_pins[] = {
	/* SCK3 */
	RCAR_GP_PIN(0, 1),
};
static const unsigned int scif3_clk_mux[] = {
	SCK3_MARK,
};
static const unsigned int scif3_ctrl_pins[] = {
	/* RTS3#, CTS3# */
	RCAR_GP_PIN(0, 5), RCAR_GP_PIN(0, 4),
};
static const unsigned int scif3_ctrl_mux[] = {
	RTS3_N_MARK, CTS3_N_MARK,
};

/* - SCIF4 ------------------------------------------------------------------ */
static const unsigned int scif4_data_pins[] = {
	/* RX4, TX4 */
	RCAR_GP_PIN(1, 6), RCAR_GP_PIN(1, 7),
};
static const unsigned int scif4_data_mux[] = {
	RX4_MARK, TX4_MARK,
};
static const unsigned int scif4_clk_pins[] = {
	/* SCK4 */
	RCAR_GP_PIN(1, 5),
};
static const unsigned int scif4_clk_mux[] = {
	SCK4_MARK,
};
static const unsigned int scif4_ctrl_pins[] = {
	/* RTS4#, CTS4# */
	RCAR_GP_PIN(1, 4), RCAR_GP_PIN(1, 3),
};
static const unsigned int scif4_ctrl_mux[] = {
	RTS4_N_MARK, CTS4_N_MARK,
};

/* - SCIF Clock ------------------------------------------------------------- */
static const unsigned int scif_clk_pins[] = {
	/* SCIF_CLK */
	RCAR_GP_PIN(0, 0),
};
static const unsigned int scif_clk_mux[] = {
	SCIF_CLK_MARK,
};

/* - PCIE ------------------------------------------------------------------- */
static const unsigned int pcie0_clkreq_n_pins[] = {
	/* PCIE0_CLKREQ# */
	RCAR_GP_PIN(2, 15),
};

static const unsigned int pcie0_clkreq_n_mux[] = {
	PCIE0_CLKREQ_N_MARK,
};

static const unsigned int pcie1_clkreq_n_pins[] = {
	/* PCIE1_CLKREQ# */
	RCAR_GP_PIN(2, 16),
};

static const unsigned int pcie1_clkreq_n_mux[] = {
	PCIE1_CLKREQ_N_MARK,
};

/* - MSIOF0 ----------------------------------------------------------------- */
static const unsigned int msiof0_clk_pins[] = {
	/* MSIOF0_SCK */
	RCAR_GP_PIN(0, 14),
};
static const unsigned int msiof0_clk_mux[] = {
	MSIOF0_SCK_MARK,
};
static const unsigned int msiof0_sync_pins[] = {
	/* MSIOF0_SYNC */
	RCAR_GP_PIN(0, 11),
};
static const unsigned int msiof0_sync_mux[] = {
	MSIOF0_SYNC_MARK,
};
static const unsigned int msiof0_ss1_pins[] = {
	/* MSIOF0_SS1 */
	RCAR_GP_PIN(0, 15),
};
static const unsigned int msiof0_ss1_mux[] = {
	MSIOF0_SS1_MARK,
};
static const unsigned int msiof0_ss2_pins[] = {
	/* MSIOF0_SS2 */
	RCAR_GP_PIN(0, 16),
};
static const unsigned int msiof0_ss2_mux[] = {
	MSIOF0_SS2_MARK,
};
static const unsigned int msiof0_txd_pins[] = {
	/* MSIOF0_TXD */
	RCAR_GP_PIN(0, 13),
};
static const unsigned int msiof0_txd_mux[] = {
	MSIOF0_TXD_MARK,
};
static const unsigned int msiof0_rxd_pins[] = {
	/* MSIOF0_RXD */
	RCAR_GP_PIN(0, 12),
};
static const unsigned int msiof0_rxd_mux[] = {
	MSIOF0_RXD_MARK,
};

/* - TAUD0 - PWM -------------------------------------------------------------*/
static const unsigned int taud0_pwm0_pins[] = {
	RCAR_GP_PIN(5, 1),
};
static const unsigned int taud0_pwm0_mux[] = {
	TAUD0O1_MARK,
};
static const unsigned int taud0_pwm1_pins[] = {
	RCAR_GP_PIN(4, 2),
};
static const unsigned int taud0_pwm1_mux[] = {
	TAUD0O3_MARK,
};
static const unsigned int taud0_pwm2_pins[] = {
	RCAR_GP_PIN(4, 4),
};
static const unsigned int taud0_pwm2_mux[] = {
	TAUD0O5_MARK,
};
static const unsigned int taud0_pwm3_pins[] = {
	RCAR_GP_PIN(4, 6),
};
static const unsigned int taud0_pwm3_mux[] = {
	TAUD0O7_MARK,
};
static const unsigned int taud0_pwm4_pins[] = {
	RCAR_GP_PIN(4, 8),
};
static const unsigned int taud0_pwm4_mux[] = {
	TAUD0O9_MARK,
};
static const unsigned int taud0_pwm5_pins[] = {
	RCAR_GP_PIN(4, 10),
};
static const unsigned int taud0_pwm5_mux[] = {
	TAUD0O11_MARK,
};
static const unsigned int taud0_pwm6_pins[] = {
	RCAR_GP_PIN(4, 12),
};
static const unsigned int taud0_pwm6_mux[] = {
	TAUD0O13_MARK,
};
static const unsigned int taud0_pwm7_pins[] = {
	RCAR_GP_PIN(4, 14),
};
static const unsigned int taud0_pwm7_mux[] = {
	TAUD0O15_MARK,
};

/* - TAUD1 - PWM -------------------------------------------------------------*/
static const unsigned int taud1_pwm0_pins[] = {
	RCAR_GP_PIN(4, 15),
};
static const unsigned int taud1_pwm0_mux[] = {
	TAUD1O1_MARK,
};
static const unsigned int taud1_pwm1_pins[] = {
	RCAR_GP_PIN(4, 17),
};
static const unsigned int taud1_pwm1_mux[] = {
	TAUD1O3_MARK,
};
static const unsigned int taud1_pwm2_pins[] = {
	RCAR_GP_PIN(4, 19),
};
static const unsigned int taud1_pwm2_mux[] = {
	TAUD1O5_MARK,
};
static const unsigned int taud1_pwm3_pins[] = {
	RCAR_GP_PIN(4, 21),
};
static const unsigned int taud1_pwm3_mux[] = {
	TAUD1O7_MARK,
};
static const unsigned int taud1_pwm4_pins[] = {
	RCAR_GP_PIN(4, 23),
};
static const unsigned int taud1_pwm4_mux[] = {
	TAUD1O9_MARK,
};
static const unsigned int taud1_pwm5_pins[] = {
	RCAR_GP_PIN(4, 25),
};
static const unsigned int taud1_pwm5_mux[] = {
	TAUD1O11_MARK,
};
static const unsigned int taud1_pwm6_pins[] = {
	RCAR_GP_PIN(4, 27),
};
static const unsigned int taud1_pwm6_mux[] = {
	TAUD1O13_MARK,
};
static const unsigned int taud1_pwm7_pins[] = {
	RCAR_GP_PIN(4, 29),
};
static const unsigned int taud1_pwm7_mux[] = {
	TAUD1O15_MARK,
};

static const struct sh_pfc_pin_group pinmux_groups[] = {
	SH_PFC_PIN_GROUP(tsn0_link),
	SH_PFC_PIN_GROUP(tsn0_magic),
	SH_PFC_PIN_GROUP(tsn0_phy_int),
	SH_PFC_PIN_GROUP(tsn0_mdio),
	SH_PFC_PIN_GROUP(tsn0_avtp_pps),
	SH_PFC_PIN_GROUP(tsn0_avtp_capture),
	SH_PFC_PIN_GROUP(tsn0_avtp_match),

	SH_PFC_PIN_GROUP(tsn1_link),
	SH_PFC_PIN_GROUP(tsn1_phy_int),
	SH_PFC_PIN_GROUP(tsn1_mdio),
	SH_PFC_PIN_GROUP(tsn1_avtp_pps),
	SH_PFC_PIN_GROUP(tsn1_avtp_capture),
	SH_PFC_PIN_GROUP(tsn1_avtp_match),

	SH_PFC_PIN_GROUP(tsn2_link),
	SH_PFC_PIN_GROUP(tsn2_phy_int),
	SH_PFC_PIN_GROUP(tsn2_mdio),

	SH_PFC_PIN_GROUP(hscif0_data),
	SH_PFC_PIN_GROUP(hscif0_clk),
	SH_PFC_PIN_GROUP(hscif0_ctrl),

	SH_PFC_PIN_GROUP(hscif1_data),
	SH_PFC_PIN_GROUP(hscif1_clk),
	SH_PFC_PIN_GROUP(hscif1_ctrl),

	SH_PFC_PIN_GROUP(i2c0),
	SH_PFC_PIN_GROUP(i2c1),
	SH_PFC_PIN_GROUP(i2c2),
	SH_PFC_PIN_GROUP(i2c3),
	SH_PFC_PIN_GROUP(i2c4),
	SH_PFC_PIN_GROUP(i2c5),

	SH_PFC_PIN_GROUP(intc_ex_irq0),
	SH_PFC_PIN_GROUP(intc_ex_irq1),
	SH_PFC_PIN_GROUP(intc_ex_irq2),
	SH_PFC_PIN_GROUP(intc_ex_irq3),

	SH_PFC_PIN_GROUP(mmc_data1),
	SH_PFC_PIN_GROUP(mmc_data4),
	SH_PFC_PIN_GROUP(mmc_data8),
	SH_PFC_PIN_GROUP(mmc_ctrl),
	SH_PFC_PIN_GROUP(mmc_cd),
	SH_PFC_PIN_GROUP(mmc_wp),
	SH_PFC_PIN_GROUP(mmc_ds),

	SH_PFC_PIN_GROUP(qspi0_ctrl),
	SH_PFC_PIN_GROUP(qspi0_data2),
	SH_PFC_PIN_GROUP(qspi0_data4),
	SH_PFC_PIN_GROUP(qspi1_ctrl),
	SH_PFC_PIN_GROUP(qspi1_data2),
	SH_PFC_PIN_GROUP(qspi1_data4),

	SH_PFC_PIN_GROUP(scif0_data),
	SH_PFC_PIN_GROUP(scif0_clk),
	SH_PFC_PIN_GROUP(scif0_ctrl),

	SH_PFC_PIN_GROUP(scif1_data),
	SH_PFC_PIN_GROUP(scif1_clk),
	SH_PFC_PIN_GROUP(scif1_ctrl),

	SH_PFC_PIN_GROUP(scif3_data),
	SH_PFC_PIN_GROUP(scif3_clk),
	SH_PFC_PIN_GROUP(scif3_ctrl),

	SH_PFC_PIN_GROUP(scif4_data),
	SH_PFC_PIN_GROUP(scif4_clk),
	SH_PFC_PIN_GROUP(scif4_ctrl),

	SH_PFC_PIN_GROUP(scif_clk),

	SH_PFC_PIN_GROUP(pcie0_clkreq_n),
	SH_PFC_PIN_GROUP(pcie1_clkreq_n),

	SH_PFC_PIN_GROUP(msiof0_clk),
	SH_PFC_PIN_GROUP(msiof0_sync),
	SH_PFC_PIN_GROUP(msiof0_ss1),
	SH_PFC_PIN_GROUP(msiof0_ss2),
	SH_PFC_PIN_GROUP(msiof0_txd),
	SH_PFC_PIN_GROUP(msiof0_rxd),

	SH_PFC_PIN_GROUP(taud0_pwm0),
	SH_PFC_PIN_GROUP(taud0_pwm1),
	SH_PFC_PIN_GROUP(taud0_pwm2),
	SH_PFC_PIN_GROUP(taud0_pwm3),
	SH_PFC_PIN_GROUP(taud0_pwm4),
	SH_PFC_PIN_GROUP(taud0_pwm5),
	SH_PFC_PIN_GROUP(taud0_pwm6),
	SH_PFC_PIN_GROUP(taud0_pwm7),

	SH_PFC_PIN_GROUP(taud1_pwm0),
	SH_PFC_PIN_GROUP(taud1_pwm1),
	SH_PFC_PIN_GROUP(taud1_pwm2),
	SH_PFC_PIN_GROUP(taud1_pwm3),
	SH_PFC_PIN_GROUP(taud1_pwm4),
	SH_PFC_PIN_GROUP(taud1_pwm5),
	SH_PFC_PIN_GROUP(taud1_pwm6),
	SH_PFC_PIN_GROUP(taud1_pwm7),
};

static const char * const tsn0_groups[] = {
	"tsn0_link",
	"tsn0_magic",
	"tsn0_phy_int",
	"tsn0_mdio",
	"tsn0_avtp_pps",
	"tsn0_avtp_capture",
	"tsn0_avtp_match",
};

static const char * const tsn1_groups[] = {
	"tsn1_link",
	"tsn1_phy_int",
	"tsn1_mdio",
	"tsn1_avtp_pps",
	"tsn1_avtp_capture",
	"tsn1_avtp_match",
};

static const char * const tsn2_groups[] = {
	"tsn2_link",
	"tsn2_phy_int",
	"tsn2_mdio",
};

static const char * const hscif0_groups[] = {
	"hscif0_data",
	"hscif0_clk",
	"hscif0_ctrl",
};

static const char * const hscif1_groups[] = {
	"hscif1_data",
	"hscif1_clk",
	"hscif1_ctrl",
};

static const char * const i2c0_groups[] = {
	"i2c0",
};

static const char * const i2c1_groups[] = {
	"i2c1",
};

static const char * const i2c2_groups[] = {
	"i2c2",
};

static const char * const i2c3_groups[] = {
	"i2c3",
};

static const char * const i2c4_groups[] = {
	"i2c4",
};

static const char * const i2c5_groups[] = {
	"i2c5",
};

static const char * const intc_ex_groups[] = {
	"intc_ex_irq0",
	"intc_ex_irq1",
	"intc_ex_irq2",
	"intc_ex_irq3",
};

static const char * const mmc_groups[] = {
	"mmc_data1",
	"mmc_data4",
	"mmc_data8",
	"mmc_ctrl",
	"mmc_cd",
	"mmc_wp",
	"mmc_ds",
};

static const char * const qspi0_groups[] = {
	"qspi0_ctrl",
	"qspi0_data2",
	"qspi0_data4",
};

static const char * const qspi1_groups[] = {
	"qspi1_ctrl",
	"qspi1_data2",
	"qspi1_data4",
};

static const char * const scif0_groups[] = {
	"scif0_data",
	"scif0_clk",
	"scif0_ctrl",
};

static const char * const scif1_groups[] = {
	"scif1_data",
	"scif1_clk",
	"scif1_ctrl",
};

static const char * const scif3_groups[] = {
	"scif3_data",
	"scif3_clk",
	"scif3_ctrl",
};

static const char * const scif4_groups[] = {
	"scif4_data",
	"scif4_clk",
	"scif4_ctrl",
};

static const char * const scif_clk_groups[] = {
	"scif_clk",
};

static const char * const pcie_groups[] = {
	"pcie0_clkreq_n",
	"pcie1_clkreq_n",
};

static const char * const msiof0_groups[] = {
	"msiof0_clk",
	"msiof0_sync",
	"msiof0_ss1",
	"msiof0_ss2",
	"msiof0_txd",
	"msiof0_rxd",
};

static const char * const taud0_pwm_groups[] = {
	"taud0_pwm0",
	"taud0_pwm1",
	"taud0_pwm2",
	"taud0_pwm3",
	"taud0_pwm4",
	"taud0_pwm5",
	"taud0_pwm6",
	"taud0_pwm7",
};

static const char * const taud1_pwm_groups[] = {
	"taud1_pwm0",
	"taud1_pwm1",
	"taud1_pwm2",
	"taud1_pwm3",
	"taud1_pwm4",
	"taud1_pwm5",
	"taud1_pwm6",
	"taud1_pwm7",
};

static const struct sh_pfc_function pinmux_functions[] = {
	SH_PFC_FUNCTION(tsn0),
	SH_PFC_FUNCTION(tsn1),
	SH_PFC_FUNCTION(tsn2),

	SH_PFC_FUNCTION(hscif0),
	SH_PFC_FUNCTION(hscif1),

	SH_PFC_FUNCTION(i2c0),
	SH_PFC_FUNCTION(i2c1),
	SH_PFC_FUNCTION(i2c2),
	SH_PFC_FUNCTION(i2c3),
	SH_PFC_FUNCTION(i2c4),
	SH_PFC_FUNCTION(i2c5),

	SH_PFC_FUNCTION(intc_ex),

	SH_PFC_FUNCTION(mmc),

	SH_PFC_FUNCTION(qspi0),
	SH_PFC_FUNCTION(qspi1),

	SH_PFC_FUNCTION(scif0),
	SH_PFC_FUNCTION(scif1),
	SH_PFC_FUNCTION(scif3),
	SH_PFC_FUNCTION(scif4),
	SH_PFC_FUNCTION(scif_clk),

	SH_PFC_FUNCTION(pcie),

	SH_PFC_FUNCTION(msiof0),

	SH_PFC_FUNCTION(taud0_pwm),
	SH_PFC_FUNCTION(taud1_pwm),
};

static const struct pinmux_cfg_reg pinmux_config_regs[] = {
#define F_(x, y)	FN_##y
#define FM(x)		FN_##x
	{ PINMUX_CFG_REG("GPSR0", 0xe6050040, 32, 1, GROUP(
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
		GP_0_20_FN,	GPSR0_20,
		GP_0_19_FN,	GPSR0_19,
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
	{ PINMUX_CFG_REG("GPSR1", 0xe6050840, 32, 1, GROUP(
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
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
	{ PINMUX_CFG_REG("GPSR2", 0xe6051040, 32, 1, GROUP(
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
	{ PINMUX_CFG_REG("GPSR3", 0xe6051840, 32, 1, GROUP(
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
		GP_3_18_FN,	GPSR3_18,
		GP_3_17_FN,	GPSR3_17,
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
	{ PINMUX_CFG_REG("GPSR4", 0xdfd90040, 32, 1, GROUP(
		0, 0,
		GP_4_30_FN,	GPSR4_30,
		GP_4_29_FN,	GPSR4_29,
		GP_4_28_FN,	GPSR4_28,
		GP_4_27_FN,	GPSR4_27,
		GP_4_26_FN,	GPSR4_26,
		GP_4_25_FN,	GPSR4_25,
		GP_4_24_FN,	GPSR4_24,
		GP_4_23_FN,	GPSR4_23,
		GP_4_22_FN,	GPSR4_22,
		GP_4_21_FN,	GPSR4_21,
		GP_4_20_FN,	GPSR4_20,
		GP_4_19_FN,	GPSR4_19,
		GP_4_18_FN,	GPSR4_18,
		GP_4_17_FN,	GPSR4_17,
		GP_4_16_FN,	GPSR4_16,
		GP_4_15_FN,	GPSR4_15,
		GP_4_14_FN,	GPSR4_14,
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
	{ PINMUX_CFG_REG("GPSR5", 0xdfd90840, 32, 1, GROUP(
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
	{ PINMUX_CFG_REG("GPSR6", 0xdfd91040, 32, 1, GROUP(
		GP_6_31_FN,	GPSR6_31,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
		0, 0,
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
	{ PINMUX_CFG_REG("GPSR7", 0xdfd91840, 32, 1, GROUP(
		GP_7_31_FN,	GPSR7_31,
		GP_7_30_FN,	GPSR7_30,
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
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG("IP0SR0", 0xe6050060, 32, 4, GROUP(
		IP0SR0_31_28
		IP0SR0_27_24
		IP0SR0_23_20
		IP0SR0_19_16
		IP0SR0_15_12
		IP0SR0_11_8
		IP0SR0_7_4
		IP0SR0_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR0", 0xe6050064, 32, 4, GROUP(
		IP1SR0_31_28
		IP1SR0_27_24
		IP1SR0_23_20
		IP1SR0_19_16
		IP1SR0_15_12
		IP1SR0_11_8
		IP1SR0_7_4
		IP1SR0_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR0", 0xe6050068, 32, 4, GROUP(
		IP2SR0_31_28
		IP2SR0_27_24
		IP2SR0_23_20
		IP2SR0_19_16
		IP2SR0_15_12
		IP2SR0_11_8
		IP2SR0_7_4
		IP2SR0_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR1", 0xe6050860, 32, 4, GROUP(
		IP0SR1_31_28
		IP0SR1_27_24
		IP0SR1_23_20
		IP0SR1_19_16
		IP0SR1_15_12
		IP0SR1_11_8
		IP0SR1_7_4
		IP0SR1_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR4", 0xdfd90060, 32, 4, GROUP(
		IP0SR4_31_28
		IP0SR4_27_24
		IP0SR4_23_20
		IP0SR4_19_16
		IP0SR4_15_12
		IP0SR4_11_8
		IP0SR4_7_4
		IP0SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR4", 0xdfd90064, 32, 4, GROUP(
		IP1SR4_31_28
		IP1SR4_27_24
		IP1SR4_23_20
		IP1SR4_19_16
		IP1SR4_15_12
		IP1SR4_11_8
		IP1SR4_7_4
		IP1SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR4", 0xdfd90068, 32, 4, GROUP(
		IP2SR4_31_28
		IP2SR4_27_24
		IP2SR4_23_20
		IP2SR4_19_16
		IP2SR4_15_12
		IP2SR4_11_8
		IP2SR4_7_4
		IP2SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP3SR4", 0xdfd9006c, 32, 4, GROUP(
		IP3SR4_31_28
		IP3SR4_27_24
		IP3SR4_23_20
		IP3SR4_19_16
		IP3SR4_15_12
		IP3SR4_11_8
		IP3SR4_7_4
		IP3SR4_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR5", 0xdfd90860, 32, 4, GROUP(
		IP0SR5_31_28
		IP0SR5_27_24
		IP0SR5_23_20
		IP0SR5_19_16
		IP0SR5_15_12
		IP0SR5_11_8
		IP0SR5_7_4
		IP0SR5_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR6", 0xdfd91060, 32, 4, GROUP(
		IP0SR6_31_28
		IP0SR6_27_24
		IP0SR6_23_20
		IP0SR6_19_16
		IP0SR6_15_12
		IP0SR6_11_8
		IP0SR6_7_4
		IP0SR6_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR6", 0xdfd91064, 32, 4, GROUP(
		IP1SR6_31_28
		IP1SR6_27_24
		IP1SR6_23_20
		IP1SR6_19_16
		IP1SR6_15_12
		IP1SR6_11_8
		IP1SR6_7_4
		IP1SR6_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR6", 0xdfd91068, 32, 4, GROUP(
		IP2SR6_31_28
		IP2SR6_27_24
		IP2SR6_23_20
		IP2SR6_19_16
		IP2SR6_15_12
		IP2SR6_11_8
		IP2SR6_7_4
		IP2SR6_3_0))
	},
	{ PINMUX_CFG_REG("IP0SR7", 0xdfd91860, 32, 4, GROUP(
		IP0SR7_31_28
		IP0SR7_27_24
		IP0SR7_23_20
		IP0SR7_19_16
		IP0SR7_15_12
		IP0SR7_11_8
		IP0SR7_7_4
		IP0SR7_3_0))
	},
	{ PINMUX_CFG_REG("IP1SR7", 0xdfd91864, 32, 4, GROUP(
		IP1SR7_31_28
		IP1SR7_27_24
		IP1SR7_23_20
		IP1SR7_19_16
		IP1SR7_15_12
		IP1SR7_11_8
		IP1SR7_7_4
		IP1SR7_3_0))
	},
	{ PINMUX_CFG_REG("IP2SR7", 0xdfd91868, 32, 4, GROUP(
		IP2SR7_31_28
		IP2SR7_27_24
		IP2SR7_23_20
		IP2SR7_19_16
		IP2SR7_15_12
		IP2SR7_11_8
		IP2SR7_7_4
		IP2SR7_3_0))
	},
	{ PINMUX_CFG_REG("IP3SR7", 0xdfd9186c, 32, 4, GROUP(
		IP3SR7_31_28
		IP3SR7_27_24
		IP3SR7_23_20
		IP3SR7_19_16
		IP3SR7_15_12
		IP3SR7_11_8
		IP3SR7_7_4
		IP3SR7_3_0))
	},
#undef F_
#undef FM

#define F_(x, y)	x,
#define FM(x)		FN_##x,
	{ PINMUX_CFG_REG_VAR("MOD_SEL1", 0xe6050900, 32,
			     GROUP(4, 4, 4, 4, 4, 2, 2, 2, 2, 2, 2),
			     GROUP(
		/* RESERVED 31, 30, 29, 28 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 27, 26, 25, 24 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 23, 22, 21, 20 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 19, 18, 17, 16 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		/* RESERVED 15, 14, 13, 12 */
		0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0, 0, 0,
		MOD_SEL1_10_11
		MOD_SEL1_8_9
		MOD_SEL1_6_7
		MOD_SEL1_4_5
		MOD_SEL1_2_3
		MOD_SEL1_0_1))
	},
	{ },
};

static const struct pinmux_drive_reg pinmux_drive_regs[] = {
	{ PINMUX_DRIVE_REG("DRV0CTRL0", 0xe6050080) {
		{ RCAR_GP_PIN(0,  7), 28, 3 },	/* TX0 */
		{ RCAR_GP_PIN(0,  6), 24, 3 },	/* RX0 */
		{ RCAR_GP_PIN(0,  5), 20, 3 },	/* HRTS0_N */
		{ RCAR_GP_PIN(0,  4), 16, 3 },	/* HCTS0_N */
		{ RCAR_GP_PIN(0,  3), 12, 3 },	/* HTX0 */
		{ RCAR_GP_PIN(0,  2),  8, 3 },	/* HRX0 */
		{ RCAR_GP_PIN(0,  1),  4, 3 },	/* HSCK0 */
		{ RCAR_GP_PIN(0,  0),  0, 3 },	/* SCIF_CLK */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL0", 0xe6050084) {
		{ RCAR_GP_PIN(0, 15), 28, 3 },	/* MSIOF0_SS1 */
		{ RCAR_GP_PIN(0, 14), 24, 3 },	/* MSIOF0_SCK */
		{ RCAR_GP_PIN(0, 13), 20, 3 },	/* MSIOF0_TXD */
		{ RCAR_GP_PIN(0, 12), 16, 3 },	/* MSIOF0_RXD */
		{ RCAR_GP_PIN(0, 11), 12, 3 },	/* MSIOF0_SYNC */
		{ RCAR_GP_PIN(0, 10),  8, 3 },	/* CTS0_N */
		{ RCAR_GP_PIN(0,  9),  4, 3 },	/* RTS0_N */
		{ RCAR_GP_PIN(0,  8),  0, 3 },	/* SCK0 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL0", 0xe6050088) {
		{ RCAR_GP_PIN(0, 20), 16, 3 },	/* IRQ3 */
		{ RCAR_GP_PIN(0, 19), 12, 3 },	/* IRQ2 */
		{ RCAR_GP_PIN(0, 18),  8, 3 },	/* IRQ1 */
		{ RCAR_GP_PIN(0, 17),  4, 3 },	/* IRQ0 */
		{ RCAR_GP_PIN(0, 16),  0, 3 },	/* MSIOF0_SS2 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL1", 0xe6050880) {
		{ RCAR_GP_PIN(1,  7), 28, 3 },	/* GP1_07 */
		{ RCAR_GP_PIN(1,  6), 24, 3 },	/* GP1_06 */
		{ RCAR_GP_PIN(1,  5), 20, 3 },	/* GP1_05 */
		{ RCAR_GP_PIN(1,  4), 16, 3 },	/* GP1_04 */
		{ RCAR_GP_PIN(1,  3), 12, 3 },	/* GP1_03 */
		{ RCAR_GP_PIN(1,  2),  8, 3 },	/* GP1_02 */
		{ RCAR_GP_PIN(1,  1),  4, 3 },	/* GP1_01 */
		{ RCAR_GP_PIN(1,  0),  0, 3 },	/* GP1_00 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL1", 0xe6050884) {
		{ RCAR_GP_PIN(1, 15), 28, 3 },	/* MMC_SD_D2 */
		{ RCAR_GP_PIN(1, 14), 24, 3 },	/* MMC_SD_D1 */
		{ RCAR_GP_PIN(1, 13), 20, 3 },	/* MMC_SD_D0 */
		{ RCAR_GP_PIN(1, 12), 16, 3 },	/* MMC_SD_CLK */
		{ RCAR_GP_PIN(1, 11), 12, 3 },	/* GP1_11 */
		{ RCAR_GP_PIN(1, 10),  8, 3 },	/* GP1_10 */
		{ RCAR_GP_PIN(1,  9),  4, 3 },	/* GP1_09 */
		{ RCAR_GP_PIN(1,  8),  0, 3 },	/* GP1_08 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL1", 0xe6050888) {
		{ RCAR_GP_PIN(1, 23), 28, 3 },	/* SD_CD */
		{ RCAR_GP_PIN(1, 22), 24, 3 },	/* MMC_SD_CMD */
		{ RCAR_GP_PIN(1, 21), 20, 3 },	/* MMC_D7 */
		{ RCAR_GP_PIN(1, 20), 16, 3 },	/* MMC_DS */
		{ RCAR_GP_PIN(1, 19), 12, 3 },	/* MMC_D6 */
		{ RCAR_GP_PIN(1, 18),  8, 3 },	/* MMC_D4 */
		{ RCAR_GP_PIN(1, 17),  4, 3 },	/* MMC_D5 */
		{ RCAR_GP_PIN(1, 16),  0, 3 },	/* MMC_SD_D3 */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL1", 0xe605088c) {
		{ RCAR_GP_PIN(1, 24),  0, 3 },	/* SD_WP */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL2", 0xe6051080) {
		{ RCAR_GP_PIN(2,  7), 28, 2 },	/* QSPI1_MOSI_IO0 */
		{ RCAR_GP_PIN(2,  6), 24, 2 },	/* QSPI1_IO2 */
		{ RCAR_GP_PIN(2,  5), 20, 2 },	/* QSPI1_MISO_IO1 */
		{ RCAR_GP_PIN(2,  4), 16, 2 },	/* QSPI1_IO3 */
		{ RCAR_GP_PIN(2,  3), 12, 2 },	/* QSPI1_SSL */
		{ RCAR_GP_PIN(2,  2),  8, 2 },	/* RPC_RESET_N */
		{ RCAR_GP_PIN(2,  1),  4, 2 },	/* RPC_WP_N */
		{ RCAR_GP_PIN(2,  0),  0, 2 },	/* RPC_INT_N */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL2", 0xe6051084) {
		{ RCAR_GP_PIN(2, 15), 28, 3 },	/* PCIE0_CLKREQ_N */
		{ RCAR_GP_PIN(2, 14), 24, 2 },	/* QSPI0_IO3 */
		{ RCAR_GP_PIN(2, 13), 20, 2 },	/* QSPI0_SSL */
		{ RCAR_GP_PIN(2, 12), 16, 2 },	/* QSPI0_MISO_IO1 */
		{ RCAR_GP_PIN(2, 11), 12, 2 },	/* QSPI0_IO2 */
		{ RCAR_GP_PIN(2, 10),  8, 2 },	/* QSPI0_SPCLK */
		{ RCAR_GP_PIN(2,  9),  4, 2 },	/* QSPI0_MOSI_IO0 */
		{ RCAR_GP_PIN(2,  8),  0, 2 },	/* QSPI1_SPCLK */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL2", 0xe6051088) {
		{ RCAR_GP_PIN(2, 16),  0, 3 },	/* PCIE1_CLKREQ_N */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL3", 0xe6051880) {
		{ RCAR_GP_PIN(3,  7), 28, 3 },	/* TSN2_LINK */
		{ RCAR_GP_PIN(3,  6), 24, 3 },	/* TSN1_LINK */
		{ RCAR_GP_PIN(3,  5), 20, 3 },	/* TSN1_MDC */
		{ RCAR_GP_PIN(3,  4), 16, 3 },	/* TSN0_MDC */
		{ RCAR_GP_PIN(3,  3), 12, 3 },	/* TSN2_MDC */
		{ RCAR_GP_PIN(3,  2),  8, 3 },	/* TSN0_MDIO */
		{ RCAR_GP_PIN(3,  1),  4, 3 },	/* TSN2_MDIO */
		{ RCAR_GP_PIN(3,  0),  0, 3 },	/* TSN1_MDIO */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL3", 0xe6051884) {
		{ RCAR_GP_PIN(3, 15), 28, 3 },	/* TSN1_AVTP_CAPTURE */
		{ RCAR_GP_PIN(3, 14), 24, 3 },	/* TSN1_AVTP_MATCH */
		{ RCAR_GP_PIN(3, 13), 20, 3 },	/* TSN1_AVTP_PPS */
		{ RCAR_GP_PIN(3, 12), 16, 3 },	/* TSN0_MAGIC */
		{ RCAR_GP_PIN(3, 11), 12, 3 },	/* TSN1_PHY_INT */
		{ RCAR_GP_PIN(3, 10),  8, 3 },	/* TSN0_PHY_INT */
		{ RCAR_GP_PIN(3,  9),  4, 3 },	/* TSN2_PHY_INT*/
		{ RCAR_GP_PIN(3,  8),  0, 3 },	/* TSN0_LINK */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL3", 0xe6051888) {
		{ RCAR_GP_PIN(3, 18),  8, 3 },	/* TSN0_AVTP_CAPTURE */
		{ RCAR_GP_PIN(3, 17),  4, 3 },	/* TSN0_AVTP_MATCH */
		{ RCAR_GP_PIN(3, 16),  0, 3 },	/* TSN0_AVTP_PPS */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL4", 0xdfd90080) {
		{ RCAR_GP_PIN(4,  7), 28, 3 },	/* GP4_07 */
		{ RCAR_GP_PIN(4,  6), 24, 3 },	/* GP4_06 */
		{ RCAR_GP_PIN(4,  5), 20, 3 },	/* GP4_05 */
		{ RCAR_GP_PIN(4,  4), 16, 3 },	/* GP4_04 */
		{ RCAR_GP_PIN(4,  3), 12, 3 },	/* GP4_03 */
		{ RCAR_GP_PIN(4,  2),  8, 3 },	/* GP4_02 */
		{ RCAR_GP_PIN(4,  1),  4, 3 },	/* GP4_01 */
		{ RCAR_GP_PIN(4,  0),  0, 3 },	/* GP4_00 */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL4", 0xdfd90084) {
		{ RCAR_GP_PIN(4, 15), 28, 3 },	/* GP4_15 */
		{ RCAR_GP_PIN(4, 14), 24, 3 },	/* GP4_14 */
		{ RCAR_GP_PIN(4, 13), 20, 3 },	/* GP4_13 */
		{ RCAR_GP_PIN(4, 12), 16, 3 },	/* GP4_12 */
		{ RCAR_GP_PIN(4, 11), 12, 3 },	/* GP4_11 */
		{ RCAR_GP_PIN(4, 10),  8, 3 },	/* GP4_10 */
		{ RCAR_GP_PIN(4,  9),  4, 3 },	/* GP4_09 */
		{ RCAR_GP_PIN(4,  8),  0, 3 },	/* GP4_08 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL4", 0xdfd90088) {
		{ RCAR_GP_PIN(4, 23), 28, 3 },	/* MSPI0CSS1 */
		{ RCAR_GP_PIN(4, 22), 24, 3 },	/* MSPI0SO_MSPI0DCS */
		{ RCAR_GP_PIN(4, 21), 20, 3 },	/* MSPI0SI */
		{ RCAR_GP_PIN(4, 20), 16, 3 },	/* MSPI0SC */
		{ RCAR_GP_PIN(4, 19), 12, 3 },	/* GP4_19 */
		{ RCAR_GP_PIN(4, 18),  8, 3 },	/* GP4_18 */
		{ RCAR_GP_PIN(4, 17),  4, 3 },	/* GP4_17 */
		{ RCAR_GP_PIN(4, 16),  0, 3 },	/* GP4_16 */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL4", 0xdfd9008c) {
		{ RCAR_GP_PIN(4, 30), 24, 3 },	/* MSPI1CSS1 */
		{ RCAR_GP_PIN(4, 29), 20, 3 },	/* MSPI1CSS2 */
		{ RCAR_GP_PIN(4, 28), 16, 3 },	/* MSPI1SC */
		{ RCAR_GP_PIN(4, 27), 12, 3 },	/* MSPI1CSS0 */
		{ RCAR_GP_PIN(4, 26),  8, 3 },	/* MSPI1SO_MSPI1DCS */
		{ RCAR_GP_PIN(4, 25),  4, 3 },	/* MSPI1SI */
		{ RCAR_GP_PIN(4, 24),  0, 3 },	/* MSPI0CSS0 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL5", 0xdfd90880) {
		{ RCAR_GP_PIN(5,  7), 28, 3 },	/* ETNB0RXD3 */
		{ RCAR_GP_PIN(5,  6), 24, 3 },	/* ETNB0RXER */
		{ RCAR_GP_PIN(5,  5), 20, 3 },	/* ETNB0MDC */
		{ RCAR_GP_PIN(5,  4), 16, 3 },	/* ETNB0LINKSTA */
		{ RCAR_GP_PIN(5,  3), 12, 3 },	/* ETNB0WOL */
		{ RCAR_GP_PIN(5,  2),  8, 3 },	/* ETNB0MD */
		{ RCAR_GP_PIN(5,  1),  4, 3 },	/* RIIC0SDA */
		{ RCAR_GP_PIN(5,  0),  0, 3 },	/* RIIC0SCL */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL5", 0xdfd90884) {
		{ RCAR_GP_PIN(5, 15), 28, 3 },	/* ETNB0TXCLK */
		{ RCAR_GP_PIN(5, 14), 24, 3 },	/* ETNB0TXD3 */
		{ RCAR_GP_PIN(5, 13), 20, 3 },	/* ETNB0TXER */
		{ RCAR_GP_PIN(5, 12), 16, 3 },	/* ETNB0RXCLK */
		{ RCAR_GP_PIN(5, 11), 12, 3 },	/* ETNB0RXD0 */
		{ RCAR_GP_PIN(5, 10),  8, 3 },	/* ETNB0RXDV */
		{ RCAR_GP_PIN(5,  9),  4, 3 },	/* ETNB0RXD2*/
		{ RCAR_GP_PIN(5,  8),  0, 3 },	/* ETNB0RXD1 */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL5", 0xdfd90888) {
		{ RCAR_GP_PIN(5, 19), 12, 3 },	/* ETNB0TXD0 */
		{ RCAR_GP_PIN(5, 18),  8, 3 },	/* ETNB0TXEN */
		{ RCAR_GP_PIN(5, 17),  4, 3 },	/* ETNB0TXD2 */
		{ RCAR_GP_PIN(5, 16),  0, 3 },	/* ETNB0TXD1 */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL6", 0xdfd91080) {
		{ RCAR_GP_PIN(6,  7), 28, 3 },	/* RLIN34RX_INTP20 */
		{ RCAR_GP_PIN(6,  6), 24, 3 },	/* RLIN34TX */
		{ RCAR_GP_PIN(6,  5), 20, 3 },	/* RLIN35RX_INTP21 */
		{ RCAR_GP_PIN(6,  4), 16, 3 },	/* RLIN35TX */
		{ RCAR_GP_PIN(6,  3), 12, 3 },	/* RLIN36RX_INTP22 */
		{ RCAR_GP_PIN(6,  2),  8, 3 },	/* RLIN36TX */
		{ RCAR_GP_PIN(6,  1),  4, 3 },	/* RLIN37RX_INTP23 */
		{ RCAR_GP_PIN(6,  0),  0, 3 },	/* RLIN37TX */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL6", 0xdfd91084) {
		{ RCAR_GP_PIN(6, 15), 28, 3 },	/* RLIN30RX_INTP16 */
		{ RCAR_GP_PIN(6, 14), 24, 3 },	/* RLIN30TX */
		{ RCAR_GP_PIN(6, 13), 20, 3 },	/* RLIN31RX_INTP17 */
		{ RCAR_GP_PIN(6, 12), 16, 3 },	/* RLIN31TX */
		{ RCAR_GP_PIN(6, 11), 12, 3 },	/* RLIN32RX_INTP18 */
		{ RCAR_GP_PIN(6, 10),  8, 3 },	/* RLIN32TX */
		{ RCAR_GP_PIN(6,  9),  4, 3 },	/* RLIN33RX_INTP19*/
		{ RCAR_GP_PIN(6,  8),  0, 3 },	/* RLIN33TX */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL6", 0xdfd91088) {
		{ RCAR_GP_PIN(6, 22), 24, 3 },	/* NMI1 */
		{ RCAR_GP_PIN(6, 21), 20, 3 },	/* INTP32 */
		{ RCAR_GP_PIN(6, 20), 16, 3 },	/* INTP33 */
		{ RCAR_GP_PIN(6, 19), 12, 3 },	/* INTP34 */
		{ RCAR_GP_PIN(6, 18),  8, 3 },	/* INTP35 */
		{ RCAR_GP_PIN(6, 17),  4, 3 },	/* INTP36 */
		{ RCAR_GP_PIN(6, 16),  0, 3 },	/* INTP37 */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL6", 0xdfd9108c) {
		{ RCAR_GP_PIN(6, 31), 28, 3 },	/* PRESETOUT1_N */
	} },
	{ PINMUX_DRIVE_REG("DRV0CTRL7", 0xdfd91880) {
		{ RCAR_GP_PIN(7,  7), 28, 3 },	/* CAN3RX_INTP3 */
		{ RCAR_GP_PIN(7,  6), 24, 3 },	/* CAN3TX */
		{ RCAR_GP_PIN(7,  5), 20, 3 },	/* CAN2RX_INTP2 */
		{ RCAR_GP_PIN(7,  4), 16, 3 },	/* CAN2TX */
		{ RCAR_GP_PIN(7,  3), 12, 3 },	/* CAN1RX_INTP1 */
		{ RCAR_GP_PIN(7,  2),  8, 3 },	/* CAN1TX */
		{ RCAR_GP_PIN(7,  1),  4, 3 },	/* CAN0RX_INTP0 */
		{ RCAR_GP_PIN(7,  0),  0, 3 },	/* CAN0TX */
	} },
	{ PINMUX_DRIVE_REG("DRV1CTRL7", 0xdfd91884) {
		{ RCAR_GP_PIN(7, 15), 28, 3 },	/* CAN7RX_INTP7 */
		{ RCAR_GP_PIN(7, 14), 24, 3 },	/* CAN7TX */
		{ RCAR_GP_PIN(7, 13), 20, 3 },	/* CAN6RX_INTP6 */
		{ RCAR_GP_PIN(7, 12), 16, 3 },	/* CAN6TX */
		{ RCAR_GP_PIN(7, 11), 12, 3 },	/* CAN5RX_INTP5 */
		{ RCAR_GP_PIN(7, 10),  8, 3 },	/* CAN5TX */
		{ RCAR_GP_PIN(7,  9),  4, 3 },	/* CAN4RX_INTP4 */
		{ RCAR_GP_PIN(7,  8),  0, 3 },	/* CAN4TX */
	} },
	{ PINMUX_DRIVE_REG("DRV2CTRL7", 0xdfd91888) {
		{ RCAR_GP_PIN(7, 23), 28, 3 },	/* CAN11RX_INTP11 */
		{ RCAR_GP_PIN(7, 22), 24, 3 },	/* CAN11TX */
		{ RCAR_GP_PIN(7, 21), 20, 3 },	/* CAN10RX_INTP10 */
		{ RCAR_GP_PIN(7, 20), 16, 3 },	/* CAN10TX */
		{ RCAR_GP_PIN(7, 19), 12, 3 },	/* CAN9RX_INTP9 */
		{ RCAR_GP_PIN(7, 18),  8, 3 },	/* CAN9TX */
		{ RCAR_GP_PIN(7, 17),  4, 3 },	/* CAN8RX_INTP8 */
		{ RCAR_GP_PIN(7, 16),  0, 3 },	/* CAN8TX */
	} },
	{ PINMUX_DRIVE_REG("DRV3CTRL7", 0xdfd9188c) {
		{ RCAR_GP_PIN(7, 31), 28, 3 },	/* CAN15RX_INTP15 */
		{ RCAR_GP_PIN(7, 30), 24, 3 },	/* CAN15TX */
		{ RCAR_GP_PIN(7, 29), 20, 3 },	/* CAN14RX_INTP14 */
		{ RCAR_GP_PIN(7, 28), 16, 3 },	/* CAN14TX */
		{ RCAR_GP_PIN(7, 27), 12, 3 },	/* CAN13RX_INTP13 */
		{ RCAR_GP_PIN(7, 26),  8, 3 },	/* CAN13TX */
		{ RCAR_GP_PIN(7, 25),  4, 3 },	/* CAN12RX_INTP12 */
		{ RCAR_GP_PIN(7, 24),  0, 3 },	/* CAN12TX */
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
	TD0SEL1,
};

static const struct pinmux_ioctrl_reg pinmux_ioctrl_regs[] = {
	[POC0] = { 0xe60500a0, },
	[POC1] = { 0xe60508a0, },
	[POC2] = { 0xe60510a0, },
	[POC3] = { 0xe60518a0, },
	[POC4] = { 0xdfd900a0, },
	[POC5] = { 0xdfd908a0, },
	[POC6] = { 0xdfd910a0, },
	[POC7] = { 0xdfd918a0, },
	[TD0SEL1] = { 0xe6058120, },
	{ /* sentinel */ },
};

static int r8a779f0_pin_to_pocctrl(struct sh_pfc *pfc, unsigned int pin,
				   u32 *pocctrl)
{
	int bit = pin & 0x1f;

	*pocctrl = pinmux_ioctrl_regs[POC0].reg;
	if (pin >= RCAR_GP_PIN(0, 0) && pin <= RCAR_GP_PIN(0, 20))
		return bit;

	*pocctrl = pinmux_ioctrl_regs[POC1].reg;
	if (pin >= RCAR_GP_PIN(1, 0) && pin <= RCAR_GP_PIN(1, 24))
		return bit;

	*pocctrl = pinmux_ioctrl_regs[POC3].reg;
	if (pin >= RCAR_GP_PIN(3, 0) && pin <= RCAR_GP_PIN(3, 18))
		return bit;

	return -EINVAL;
}

static const struct pinmux_bias_reg pinmux_bias_regs[] = {
	{ PINMUX_BIAS_REG("PUEN0", 0xe60500c0, "PUD0", 0xe60500e0) {
		[ 0] = RCAR_GP_PIN(0,  0),	/* SCIF_CLK */
		[ 1] = RCAR_GP_PIN(0,  1),	/* HSCK0 */
		[ 2] = RCAR_GP_PIN(0,  2),	/* HRX0 */
		[ 3] = RCAR_GP_PIN(0,  3),	/* HTX0 */
		[ 4] = RCAR_GP_PIN(0,  4),	/* HCTS0_N */
		[ 5] = RCAR_GP_PIN(0,  5),	/* HRTS0_N */
		[ 6] = RCAR_GP_PIN(0,  6),	/* RX0 */
		[ 7] = RCAR_GP_PIN(0,  7),	/* TX0 */
		[ 8] = RCAR_GP_PIN(0,  8),	/* SCK0 */
		[ 9] = RCAR_GP_PIN(0,  9),	/* RTS0_N */
		[10] = RCAR_GP_PIN(0, 10),	/* CTS0_N */
		[11] = RCAR_GP_PIN(0, 11),	/* MSIOF0_SYNC */
		[12] = RCAR_GP_PIN(0, 12),	/* MSIOF0_RXD */
		[13] = RCAR_GP_PIN(0, 13),	/* MSIOF0_TXD */
		[14] = RCAR_GP_PIN(0, 14),	/* MSIOF0_SCK */
		[15] = RCAR_GP_PIN(0, 15),	/* MSIOF0_SS1 */
		[16] = RCAR_GP_PIN(0, 16),	/* MSIOF0_SS2 */
		[17] = RCAR_GP_PIN(0, 17),	/* IRQ0 */
		[18] = RCAR_GP_PIN(0, 18),	/* IRQ1 */
		[19] = RCAR_GP_PIN(0, 19),	/* IRQ2 */
		[20] = RCAR_GP_PIN(0, 20),	/* IRQ3 */
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
	{ PINMUX_BIAS_REG("PUEN1", 0xe60508c0, "PUD1", 0xe60508e0) {
		[ 0] = RCAR_GP_PIN(1,  0),	/* GP1_00 */
		[ 1] = RCAR_GP_PIN(1,  1),	/* GP1_01 */
		[ 2] = RCAR_GP_PIN(1,  2),	/* GP1_02 */
		[ 3] = RCAR_GP_PIN(1,  3),	/* GP1_03 */
		[ 4] = RCAR_GP_PIN(1,  4),	/* GP1_04 */
		[ 5] = RCAR_GP_PIN(1,  5),	/* GP1_05 */
		[ 6] = RCAR_GP_PIN(1,  6),	/* GP1_06 */
		[ 7] = RCAR_GP_PIN(1,  7),	/* GP1_07 */
		[ 8] = RCAR_GP_PIN(1,  8),	/* GP1_08 */
		[ 9] = RCAR_GP_PIN(1,  9),	/* GP1_09 */
		[10] = RCAR_GP_PIN(1, 10),	/* GP1_10 */
		[11] = RCAR_GP_PIN(1, 11),	/* GP1_11 */
		[12] = RCAR_GP_PIN(1, 12),	/* MMC_SD_CLK */
		[13] = RCAR_GP_PIN(1, 13),	/* MMC_SD_D0 */
		[14] = RCAR_GP_PIN(1, 14),	/* MMC_SD_D1 */
		[15] = RCAR_GP_PIN(1, 15),	/* MMC_SD_D2 */
		[16] = RCAR_GP_PIN(1, 16),	/* MMC_SD_D3 */
		[17] = RCAR_GP_PIN(1, 17),	/* MMC_D5 */
		[18] = RCAR_GP_PIN(1, 18),	/* MMC_D4 */
		[19] = RCAR_GP_PIN(1, 19),	/* MMC_D6 */
		[20] = RCAR_GP_PIN(1, 20),	/* MMC_DS */
		[21] = RCAR_GP_PIN(1, 21),	/* MMC_D7 */
		[22] = RCAR_GP_PIN(1, 22),	/* MMC_SD_CMD */
		[23] = RCAR_GP_PIN(1, 23),	/* SD_CD */
		[24] = RCAR_GP_PIN(1, 24),	/* SD_WP */
		[25] = SH_PFC_PIN_NONE,
		[26] = SH_PFC_PIN_NONE,
		[27] = SH_PFC_PIN_NONE,
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN2", 0xe60510c0, "PUD2", 0xe60510e0) {
		[ 0] = RCAR_GP_PIN(2,  0),	/* RPC_INT_N */
		[ 1] = RCAR_GP_PIN(2,  1),	/* RPC_WP_N */
		[ 2] = RCAR_GP_PIN(2,  2),	/* RPC_RESET_N */
		[ 3] = RCAR_GP_PIN(2,  3),	/* QSPI1_SSL */
		[ 4] = RCAR_GP_PIN(2,  4),	/* QSPI1_IO3 */
		[ 5] = RCAR_GP_PIN(2,  5),	/* QSPI1_MISO_IO1 */
		[ 6] = RCAR_GP_PIN(2,  6),	/* QSPI1_IO2 */
		[ 7] = RCAR_GP_PIN(2,  7),	/* QSPI1_MOSI_IO0 */
		[ 8] = RCAR_GP_PIN(2,  8),	/* QSPI1_SPCLK */
		[ 9] = RCAR_GP_PIN(2,  9),	/* QSPI0_MOSI_IO0 */
		[10] = RCAR_GP_PIN(2, 10),	/* QSPI0_SPCLK */
		[11] = RCAR_GP_PIN(2, 11),	/* QSPI0_IO2 */
		[12] = RCAR_GP_PIN(2, 12),	/* QSPI0_MISO_IO1 */
		[13] = RCAR_GP_PIN(2, 13),	/* QSPI0_SSL */
		[14] = RCAR_GP_PIN(2, 14),	/* QSPI0_IO3 */
		[15] = RCAR_GP_PIN(2, 15),	/* PCIE0_CLKREQ_N */
		[16] = RCAR_GP_PIN(2, 16),	/* PCIE1_CLKREQ_N */
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
	{ PINMUX_BIAS_REG("PUEN3", 0xe60518c0, "PUD3", 0xe60518e0) {
		[ 0] = RCAR_GP_PIN(3,  0),	/* TSN1_MDIO */
		[ 1] = RCAR_GP_PIN(3,  1),	/* TSN2_MDIO */
		[ 2] = RCAR_GP_PIN(3,  2),	/* TSN0_MDIO */
		[ 3] = RCAR_GP_PIN(3,  3),	/* TSN2_MDC */
		[ 4] = RCAR_GP_PIN(3,  4),	/* TSN0_MDC */
		[ 5] = RCAR_GP_PIN(3,  5),	/* TSN1_MDC */
		[ 6] = RCAR_GP_PIN(3,  6),	/* TSN1_LINK */
		[ 7] = RCAR_GP_PIN(3,  7),	/* TSN2_LINK */
		[ 8] = RCAR_GP_PIN(3,  8),	/* TSN0_LINK */
		[ 9] = RCAR_GP_PIN(3,  9),	/* TSN2_PHY_INT */
		[10] = RCAR_GP_PIN(3, 10),	/* TSN0_PHY_INT */
		[11] = RCAR_GP_PIN(3, 11),	/* TSN1_PHY_INT */
		[12] = RCAR_GP_PIN(3, 12),	/* TSN0_MAGIC */
		[13] = RCAR_GP_PIN(3, 13),	/* TSN1_AVTP_PPS */
		[14] = RCAR_GP_PIN(3, 14),	/* TSN1_AVTP_MATCH */
		[15] = RCAR_GP_PIN(3, 15),	/* TSN1_AVTP_CAPTURE */
		[16] = RCAR_GP_PIN(3, 16),	/* TSN0_AVTP_PPS */
		[17] = RCAR_GP_PIN(3, 17),	/* TSN0_AVTP_MATCH */
		[18] = RCAR_GP_PIN(3, 18),	/* TSN0_AVTP_CAPTURE */
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
	{ PINMUX_BIAS_REG("PUEN4", 0xdfd900c0, "PUD4", 0xdfd900e0) {
		[ 0] = RCAR_GP_PIN(4,  0),	/* GP4_00 */
		[ 1] = RCAR_GP_PIN(4,  1),	/* GP4_01 */
		[ 2] = RCAR_GP_PIN(4,  2),	/* GP4_02 */
		[ 3] = RCAR_GP_PIN(4,  3),	/* GP4_03 */
		[ 4] = RCAR_GP_PIN(4,  4),	/* GP4_04 */
		[ 5] = RCAR_GP_PIN(4,  5),	/* GP4_05 */
		[ 6] = RCAR_GP_PIN(4,  6),	/* GP4_06 */
		[ 7] = RCAR_GP_PIN(4,  7),	/* GP4_07 */
		[ 8] = RCAR_GP_PIN(4,  8),	/* GP4_08 */
		[ 9] = RCAR_GP_PIN(4,  9),	/* GP4_09 */
		[10] = RCAR_GP_PIN(4, 10),	/* GP4_10 */
		[11] = RCAR_GP_PIN(4, 11),	/* GP4_11 */
		[12] = RCAR_GP_PIN(4, 12),	/* GP4_12 */
		[13] = RCAR_GP_PIN(4, 13),	/* GP4_13 */
		[14] = RCAR_GP_PIN(4, 14),	/* GP4_14 */
		[15] = RCAR_GP_PIN(4, 15),	/* GP4_15 */
		[16] = RCAR_GP_PIN(4, 16),	/* GP4_16 */
		[17] = RCAR_GP_PIN(4, 17),	/* GP4_17 */
		[18] = RCAR_GP_PIN(4, 18),	/* GP4_18 */
		[19] = RCAR_GP_PIN(4, 19),	/* GP4_19 */
		[20] = RCAR_GP_PIN(4, 20),	/* MSPI0SC */
		[21] = RCAR_GP_PIN(4, 21),	/* MSPI0SI */
		[22] = RCAR_GP_PIN(4, 22),	/* MSPI0SO_MSPI0DCS */
		[23] = RCAR_GP_PIN(4, 23),	/* MSPI0CSS1 */
		[24] = RCAR_GP_PIN(4, 24),	/* MSPI0CSS0 */
		[25] = RCAR_GP_PIN(4, 25),	/* MSPI1SI */
		[26] = RCAR_GP_PIN(4, 26),	/* MSPI1SO_MSPI1DCS */
		[27] = RCAR_GP_PIN(4, 27),	/* MSPI1CSS0 */
		[28] = RCAR_GP_PIN(4, 28),	/* MSPI1SC */
		[29] = RCAR_GP_PIN(4, 29),	/* MSPI1CSS2 */
		[30] = RCAR_GP_PIN(4, 30),	/* MSPI1CSS1 */
		[31] = SH_PFC_PIN_NONE,
	} },
	{ PINMUX_BIAS_REG("PUEN5", 0xdfd908c0, "PUD5", 0xdfd908e0) {
		[ 0] = RCAR_GP_PIN(5,  0),	/* RIIC0SCL */
		[ 1] = RCAR_GP_PIN(5,  1),	/* RIIC0SDA */
		[ 2] = RCAR_GP_PIN(5,  2),	/* ETNB0MD */
		[ 3] = RCAR_GP_PIN(5,  3),	/* ETNB0WOL */
		[ 4] = RCAR_GP_PIN(5,  4),	/* ETNB0LINKSTA */
		[ 5] = RCAR_GP_PIN(5,  5),	/* ETNB0MDC */
		[ 6] = RCAR_GP_PIN(5,  6),	/* ETNB0RXER */
		[ 7] = RCAR_GP_PIN(5,  7),	/* ETNB0RXD3 */
		[ 8] = RCAR_GP_PIN(5,  8),	/* ETNB0RXD1 */
		[ 9] = RCAR_GP_PIN(5,  9),	/* ETNB0RXD2 */
		[10] = RCAR_GP_PIN(5, 10),	/* ETNB0RXDV */
		[11] = RCAR_GP_PIN(5, 11),	/* ETNB0RXD0 */
		[12] = RCAR_GP_PIN(5, 12),	/* ETNB0RXCLK */
		[13] = RCAR_GP_PIN(5, 13),	/* ETNB0TXER */
		[14] = RCAR_GP_PIN(5, 14),	/* ETNB0TXD3 */
		[15] = RCAR_GP_PIN(5, 15),	/* ETNB0TXCLK */
		[16] = RCAR_GP_PIN(5, 16),	/* ETNB0TXD1 */
		[17] = RCAR_GP_PIN(5, 17),	/* ETNB0TXD2 */
		[18] = RCAR_GP_PIN(5, 18),	/* ETNB0TXEN */
		[19] = RCAR_GP_PIN(5, 19),	/* ETNB0TXD0 */
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
	{ PINMUX_BIAS_REG("PUEN6", 0xdfd910c0, "PUD6", 0xdfd910e0) {
		[ 0] = RCAR_GP_PIN(6,  0),	/* RLIN37TX */
		[ 1] = RCAR_GP_PIN(6,  1),	/* RLIN37RX_INTP23 */
		[ 2] = RCAR_GP_PIN(6,  2),	/* RLIN36TX */
		[ 3] = RCAR_GP_PIN(6,  3),	/* RLIN36RX_INTP22 */
		[ 4] = RCAR_GP_PIN(6,  4),	/* RLIN35TX */
		[ 5] = RCAR_GP_PIN(6,  5),	/* RLIN35RX_INTP21 */
		[ 6] = RCAR_GP_PIN(6,  6),	/* RLIN34TX */
		[ 7] = RCAR_GP_PIN(6,  7),	/* RLIN34RX_INTP20 */
		[ 8] = RCAR_GP_PIN(6,  8),	/* RLIN33TX */
		[ 9] = RCAR_GP_PIN(6,  9),	/* RLIN33RX_INTP19 */
		[10] = RCAR_GP_PIN(6, 10),	/* RLIN32TX */
		[11] = RCAR_GP_PIN(6, 11),	/* RLIN32RX_INTP18 */
		[12] = RCAR_GP_PIN(6, 12),	/* RLIN31TX */
		[13] = RCAR_GP_PIN(6, 13),	/* RLIN31RX_INTP17 */
		[14] = RCAR_GP_PIN(6, 14),	/* RLIN30TX*/
		[15] = RCAR_GP_PIN(6, 15),	/* RLIN30RX_INTP16 */
		[16] = RCAR_GP_PIN(6, 16),	/* INTP37 */
		[17] = RCAR_GP_PIN(6, 17),	/* INTP36 */
		[18] = RCAR_GP_PIN(6, 18),	/* INTP35 */
		[19] = RCAR_GP_PIN(6, 19),	/* INTP34 */
		[20] = RCAR_GP_PIN(6, 20),	/* INTP33 */
		[21] = RCAR_GP_PIN(6, 21),	/* INTP32 */
		[22] = RCAR_GP_PIN(6, 22),	/* NMI1 */
		[23] = SH_PFC_PIN_NONE,
		[24] = SH_PFC_PIN_NONE,
		[25] = SH_PFC_PIN_NONE,
		[26] = SH_PFC_PIN_NONE,
		[27] = SH_PFC_PIN_NONE,
		[28] = SH_PFC_PIN_NONE,
		[29] = SH_PFC_PIN_NONE,
		[30] = SH_PFC_PIN_NONE,
		[31] = RCAR_GP_PIN(6, 31),	/* PRESETOUT1_N */
	} },
	{ PINMUX_BIAS_REG("PUEN7", 0xdfd918c0, "PUD7", 0xdfd918e0) {
		[ 0] = RCAR_GP_PIN(7,  0),	/* CAN0TX */
		[ 1] = RCAR_GP_PIN(7,  1),	/* CAN0RX_INTP0 */
		[ 2] = RCAR_GP_PIN(7,  2),	/* CAN1TX */
		[ 3] = RCAR_GP_PIN(7,  3),	/* CAN1RX_INTP1 */
		[ 4] = RCAR_GP_PIN(7,  4),	/* CAN2TX */
		[ 5] = RCAR_GP_PIN(7,  5),	/* CAN2RX_INTP2 */
		[ 6] = RCAR_GP_PIN(7,  6),	/* CAN3TX */
		[ 7] = RCAR_GP_PIN(7,  7),	/* CAN3RX_INTP3 */
		[ 8] = RCAR_GP_PIN(7,  8),	/* CAN4TX */
		[ 9] = RCAR_GP_PIN(7,  9),	/* CAN4RX_INTP4 */
		[10] = RCAR_GP_PIN(7, 10),	/* CAN5TX */
		[11] = RCAR_GP_PIN(7, 11),	/* CAN5RX_INTP5 */
		[12] = RCAR_GP_PIN(7, 12),	/* CAN6TX */
		[13] = RCAR_GP_PIN(7, 13),	/* CAN6RX_INTP6 */
		[14] = RCAR_GP_PIN(7, 14),	/* CAN7TX */
		[15] = RCAR_GP_PIN(7, 15),	/* CAN7RX_INTP7 */
		[16] = RCAR_GP_PIN(7, 16),	/* CAN8TX */
		[17] = RCAR_GP_PIN(7, 17),	/* CAN8RX_INTP8 */
		[18] = RCAR_GP_PIN(7, 18),	/* CAN9TX */
		[19] = RCAR_GP_PIN(7, 19),	/* CAN9RX_INTP9 */
		[20] = RCAR_GP_PIN(7, 20),	/* CAN10TX */
		[21] = RCAR_GP_PIN(7, 21),	/* CAN10RX_INTP10 */
		[22] = RCAR_GP_PIN(7, 22),	/* CAN11TX */
		[23] = RCAR_GP_PIN(7, 23),	/* CAN11RX_INTP11 */
		[24] = RCAR_GP_PIN(7, 24),	/* CAN12TX */
		[25] = RCAR_GP_PIN(7, 25),	/* CAN12RX_INTP12 */
		[26] = RCAR_GP_PIN(7, 26),	/* CAN13TX */
		[27] = RCAR_GP_PIN(7, 27),	/* CAN13RX_INTP13 */
		[28] = RCAR_GP_PIN(7, 28),	/* CAN14TX */
		[29] = RCAR_GP_PIN(7, 29),	/* CAN14RX_INTP14 */
		[30] = RCAR_GP_PIN(7, 30),	/* CAN15TX */
		[31] = RCAR_GP_PIN(7, 31),	/* CAN15RX_INTP15 */
	} },
	{ /* sentinel */ },
};

static const struct sh_pfc_soc_operations pinmux_ops = {
	.pin_to_pocctrl = r8a779f0_pin_to_pocctrl,
	.get_bias = rcar_pinmux_get_bias,
	.set_bias = rcar_pinmux_set_bias,
};

const struct sh_pfc_soc_info r8a779f0_pinmux_info = {
	.name = "r8a779f0_pfc",
	.ops = &pinmux_ops,
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
