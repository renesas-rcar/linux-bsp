// SPDX-License-Identifier: GPL-2.0+
/* Renesas R-Car CAN XL device driver
 *
 * Copyright (C) 2023 Renesas Electronics Corp.
 */

/* The R-Car CAN XL controller can operate in one mode only
 *  - CAN XL only mode
 *
 * This driver puts the controller in CAN XL only mode by default.
 *
 * Note: The h/w manual register naming convention is clumsy and not acceptable
 * to use as it is in the driver. However, those names are added as comments
 * wherever it is modified to a readable name.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/can/led.h>
#include <linux/can/dev.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/iopoll.h>

#define RCANXL_DRV_NAME		"rcar_canxl"

/* CAN-XL register bits */

/* CXLGIPV */
#define CXLGIPV_IPV(x)		((x) & 0xff)
#define CXLGIPV_IPT(x)		(((x) >> 8) & 0x3)
#define CXLGIPV_XCANV(x)	(((x) >> 16) & 0xff)
#define CXLGIPV_PSI(x)		(((x) >> 24) & 0xf)

/* CXLGSTS */
#define CXLGSTS_LRAMINIT	BIT(0)

/* CXLGRSTC */
#define CXLGRSTC_SRST		BIT(0)
#define CXLGRSTC_KEY(x)		(((x) & 0xffffff) << 8)

/* CXLGRESPC */
#define CXLGRESPC_RESPC(x)	((x) & 0x3)
#define CXLGRESPC_KEY(x)	(((x) & 0xff) << 8)

/* CXLGLRAPC */
#define CXLGLRAPC_LRAPC		BIT(0)
#define CXLGLRAPC_KEY(x)	(((x) & 0xff) << 8)

/* CXLCCLKC */
#define CXLCCLKC_HCLKC		BIT(0)
#define CXLCCLKC_CCLKC		BIT(1)
#define CXLCCLKC_TCLKC		BIT(2)
#define CXLCCLKC_KEY(x)		(((x) & 0xff) << 8)

/* Message Handler (MH) register bits */

/* VERSION */
#define VERSION_DAY(x)		((x) & 0xff)
#define VERSION_MON(x)		(((x) >> 8) & 0xff)
#define VERSION_YEAR(x)		(((x) >> 16) & 0xf)
#define VERSION_SUBSTEP(x)	(((x) >> 20) & 0xf)
#define VERSION_STEP(x)		(((x) >> 24) & 0xf)
#define VERSION_REL(x)		(((x) >> 28) & 0xf)

/* MH_CTRL */
#define MH_CTRL_START		BIT(0)

/* MH_CFG */
#define MH_CFG_RX_CONT_DC	BIT(0)
#define MH_CFG_MAX_RETRANS(x)	(((x) & 0x7) << 8)
#define MH_CFG_INST_NUM(x)	(((x) & 0x7) << 16)

/* MH_STS */
#define MH_STS_BUSY		BIT(0)
#define MH_STS_ENABLE		BIT(4)
#define MH_STS_CLOCK_ACTIVE	BIT(8)

/* MH_SFTY_CFG */
#define MH_SFTY_CFG_DMA_TO_VAL(x)	((x) & 0xff)
#define MH_SFTY_CFG_MEM_TO_VAL(x)	(((x) & 0xff) << 8)
#define MH_SFTY_CFG_PRT_TO_VAL(x)	(((x) & 0x3ff) << 16)
#define MH_SFTY_CFG_PRESCALER(x)	(((x) & 0x3) << 30)

/* MH_SFTY_CFG */
#define MH_SFTY_CTRL_TX_DESC_CRC_EN	BIT(0)
#define MH_SFTY_CTRL_RX_DESC_CRC_EN	BIT(1)
#define MH_SFTY_CTRL_MEM_PROT_EN	BIT(2)
#define MH_SFTY_CTRL_RX_DP_PARITY_EN	BIT(3)
#define MH_SFTY_CTRL_TX_DP_PARITY_EN	BIT(4)
#define MH_SFTY_CTRL_TX_AP_PARITY_EN	BIT(5)
#define MH_SFTY_CTRL_RX_AP_PARITY_EN	BIT(6)
#define MH_SFTY_CTRL_DMA_CH_CHK_EN	BIT(7)
#define MH_SFTY_CTRL_DMA_TO_EN		BIT(8)
#define MH_SFTY_CTRL_MEM_TO_EN		BIT(9)
#define MH_SFTY_CTRL_PRT_TO_EN		BIT(10)

/* TX_DESC_MEM_ADD */
#define TX_DESC_MEM_ADD_FQ_BASE_ADDR(x)	((x) & 0xffff)
#define TX_DESC_MEM_ADD_PQ_BASE_ADDR(x) (((x) & 0xffff) << 16)

/* AXI_PARAMS */
#define AXI_PARAMS_AR_MAX_PEND(x)	((x) & 0x3)
#define AXI_PARAMS_AW_MAX_PEND(x)	(((x) & 0x3) << 4)

/* MH_LOCK */
#define MH_LOCK_ULK(x)		((x) & 0xffff)
#define MH_LOCK_TMK(x)		(((x) & 0xffff) << 16)

/* TX_STATISTICS and RX_STATISTICS */
#define STATISTICS_SUCC(x)	((x) & 0xfff)
#define STATISTICS_UNSUCC_R(x)	(((x) >> 16) & 0xfff)
#define STATISTICS_UNSUCC_W(x)	(((x) & 0xfff) << 16)

/* TX_FQ_STS */
#define TX_FQ_STS0_BUSY(x)	((x) & 0xff)
#define TX_FQ_STS0_STOP(x)	(((x) >> 16) & 0xff)
#define TX_FQ_STS1_UNVAL(x)	((x) & 0xff)
#define TX_FQ_STS1_ERROR(x)	(((x) >> 16) & 0xff)

/* TX_FQ_CTRL */
#define TX_FQ_CTRL0_START(x)	((x) & 0xff)
#define TX_FQ_CTRL1_ABORT(x)	((x) & 0xff)
#define TX_FQ_CTRL2_ENABLE(x)	((x) & 0xff)

/* TX_FQ_SIZE */
#define TX_FQ_SIZE_MAX_DESC(x)	((x) & 0x3ff)

/* RX_FQ_STS */
#define RX_FQ_STS0_BUSY(x)	((x) & 0xff)
#define RX_FQ_STS0_STOP(x)	(((x) >> 16) & 0xff)
#define RX_FQ_STS1_UNVAL(x)	((x) & 0xff)
#define RX_FQ_STS1_ERR(x)	(((x) >> 16) & 0xff)
#define RX_FQ_STS2_DC_FULL(x)	((x) & 0xff)

/* RX_FQ_CTRL */
#define RX_FQ_CTRL0_START(x)	((x) & 0xff)
#define RX_FQ_CTRL1_ABORT(x)	((x) & 0xff)
#define RX_FQ_CTRL2_ENABLE(x)	((x) & 0xff)

/* RX_FQ_SIZE */
#define RX_FQ_SIZE_MAX_DESC(x)	((x) & 0x3ff)
#define RX_FQ_SIZE_DC_SIZE(x)	(((x) & 0xfff) << 16)

/* TX_FILTER_CTRL */
#define TX_FILTER_CTRL0_COMB(x)		((x) & 0xff)
#define TX_FILTER_CTRL0_MASK(x)		(((x) & 0xff) << 8)
#define TX_FILTER_CTRL0_MODE		BIT(16)
#define TX_FILTER_CTRL0_CAN_FD		BIT(17)
#define TX_FILTER_CTRL0_CC_CAN		BIT(18)
#define TX_FILTER_CTRL0_EN		BIT(19)
#define TX_FILTER_CTRL0_IRQ_EN		BIT(20)

#define TX_FILTER_CTRL1_VALID(x)	((x) & 0xffff)
#define TX_FILTER_CTRL1_FIELD(x)	(((x) & 0xffff) << 16)

/* TX_FILTER_REFVAL */
#define TX_FILTER_REFVAL_REF_VAL0(x)	((x) & 0xff)
#define TX_FILTER_REFVAL_REF_VAL1(x)	(((x) & 0xff) << 8)
#define TX_FILTER_REFVAL_REF_VAL2(x)	(((x) & 0xff) << 16)
#define TX_FILTER_REFVAL_REF_VAL3(x)	(((x) & 0xff) << 24)

/* RX_FILTER_CTRL */
#define RX_FILTER_CTRL_NB_FE(x)		((x) & 0xff)
#define RX_FILTER_CTRL_THRESHOLD(x)	(((x) & 0x1f) << 8)
#define RX_FILTER_CTRL_ANMF_FQ(x)	(((x) & 0x7) << 16)
#define RX_FILTER_CTRL_ANMF		BIT(20)
#define RX_FILTER_CTRL_ANFF		BIT(21)

/* TX_FQ_INT_STS */
#define TX_FQ_INT_STS_SENT(x)		((x) & 0xff)
#define TX_FQ_INT_STS_UNVAL(x)		(((x) >> 16) & 0xff)

/* RX_FQ_INT_STS */
#define RX_FQ_INT_STS_RECEIVED(x)	((x) & 0xff)
#define RX_FQ_INT_STS_UNVAL(x)		(((x) >> 16) & 0xff)

/* STATS_INT_STS */
#define STATS_INT_STS_TX_SUCC		BIT(0)
#define STATS_INT_STS_TX_UNSUCC		BIT(1)
#define STATS_INT_STS_RX_SUCC		BIT(2)
#define STATS_INT_STS_RX_UNSUCC		BIT(3)

/* ERR_INT_STS */
#define ERR_INT_STS_DP_TX_ACK_DO_ERR	BIT(0)
#define ERR_INT_STS_DP_RX_FIFO_DO_ER	BIT(1)
#define ERR_INT_STS_DP_RX_ACK_DO_ERR	BIT(2)
#define ERR_INT_STS_DP_TX_SEQ_ERR	BIT(3)
#define ERR_INT_STS_DP_RX_SEQ_ERR	BIT(4)

/* SFTY_INT_STS */
#define SFTY_INT_STS_DMA_AXI_WR_TO_ERR		BIT(0)
#define SFTY_INT_STS_DMA_AXI_RD_TO_ERR		BIT(1)
#define SFTY_INT_STS_DP_PRT_TX_TO_ERR		BIT(2)
#define SFTY_INT_STS_DP_PRT_RX_TO_ERR		BIT(3)
#define SFTY_INT_STS_MEM_AXI_WR_TO_ERR		BIT(4)
#define SFTY_INT_STS_MEM_AXI_RD_TO_ERR		BIT(5)
#define SFTY_INT_STS_DP_TX_PARITY_ERR		BIT(6)
#define SFTY_INT_STS_DP_RX_PARITY_ERR		BIT(7)
#define SFTY_INT_STS_AP_TX_PARITY_ERR		BIT(8)
#define SFTY_INT_STS_AP_RX_PARITY_ERR		BIT(9)
#define SFTY_INT_STS_TX_DESC_REQ_ERR		BIT(10)
#define SFTY_INT_STS_TX_DESC_CRC_ERR		BIT(11)
#define SFTY_INT_STS_RX_DESC_REQ_ERR		BIT(12)
#define SFTY_INT_STS_RX_DESC_CRC_ERR		BIT(13)
#define SFTY_INT_STS_MEM_SFTY_UE		BIT(14)
#define SFTY_INT_STS_MEM_SFTY_CE		BIT(15)
#define SFTY_INT_STS_ACK_TX_PARITY_ERR		BIT(16)
#define SFTY_INT_STS_ACK_RX_PARITY_ERR		BIT(17)

/* AXI_ERR_INFO */
#define AXI_ERR_INFO_DMA_ID(x)		((x) & 0x3)
#define AXI_ERR_INFO_DMA_RESP(x)	(((x) >> 2) & 0x3)
#define AXI_ERR_INFO_MEM_ID(x)		(((x) >> 4) & 0x3)
#define AXI_ERR_INFO_MEM_RESP(x)	(((x) >> 6) & 0x3)

/* DESC_ERR_INFO1 */
#define DESC_ERR_INFO1_FQN_PQSN(x)	((x) & 0x1f)
#define DESC_ERR_INFO1_IN(x)		(((x) >> 5) & 0x7)
#define DESC_ERR_INFO1_PQ		BIT(8)
#define DESC_ERR_INFO1_RC(x)		(((x) >> 9) & 0x1f)
#define DESC_ERR_INFO1_RX_TX		BIT(15)
#define DESC_ERR_INFO1_CRC(x)		(((x) >> 16) & 0x1ff)

/* TX_FILTER_ERR_INFO */
#define TX_FILTER_ERR_INFO_FQ		BIT(0)
#define TX_FILTER_ERR_INFO_FQN_PQS(x)	(((x) >> 1) & 0x1f)

/* TX_FQ_DESC_VALID */
#define TX_FQ_DESC_VALID_DESC_CN_VALID(x)	((x) & 0xff)
#define TX_FQ_DESC_VALID_DESC_NC_VALID(x)	(((x) >> 16) & 0xff)

/* CRC_CTRL */
#define CRC_CTRL_START		BIT(0)

/* Protocol register bits */

/* PREL */
#define PREL_DAY(x)		((x) & 0xff)
#define PREL_MON(x)		(((x) >> 8) & 0xff)
#define PREL_YEAR(x)		(((x) >> 16) & 0xf)
#define PREL_SUBSTEP(x)		(((x) >> 20) & 0xf)
#define PREL_STEP(x)		(((x) >> 24) & 0xf)
#define PREL_REL(x)		(((x) >> 28) & 0xf)

/* STAT */
#define STAT_ACT(x)		((x) & 0x3)
#define STAT_INT		BIT(2)
#define STAT_STP		BIT(3)
#define STAT_CLKA		BIT(4)
#define STAT_FIMA		BIT(5)
#define STAT_EP			BIT(6)
#define STAT_BO			BIT(7)
#define STAT_TDCV(x)		(((x) >> 8) & 0xff)
#define STAT_REC(x)		(((x) >> 16) & 0x7f)
#define STAT_RP			BIT(23)
#define STAT_TEC(x)		(((x) >> 24) & 0xff)

/* EVNT */
#define EVNT_CRE		BIT(0)
#define EVNT_B0E		BIT(1)
#define EVNT_B1E		BIT(2)
#define EVNT_AKE		BIT(3)
#define EVNT_FRE		BIT(4)
#define EVNT_STE		BIT(5)
#define EVNT_DO			BIT(6)
#define EVNT_RXF		BIT(7)
#define EVNT_TXF		BIT(8)
#define EVNT_PXE		BIT(9)
#define EVNT_DU			BIT(10)
#define EVNT_USO		BIT(11)
#define EVNT_IFR		BIT(12)
#define EVNT_ABO		BIT(13)

/* LOCK */
#define LOCK_ULK(x)		((x) & 0xffff)
#define LOCK_TMK(x)		(((x) & 0xffff) << 16)

/* CTRL */
#define CTRL_STOP		BIT(0)
#define CTRL_IMMD		BIT(1)
#define CTRL_STRT		BIT(4)
#define CTRL_SRES		BIT(8)
#define CTRL_TEST		BIT(12)

/* MODE */
#define MODE_FDOE		BIT(0)
#define MODE_XLOE		BIT(1)
#define MODE_TDCE		BIT(2)
#define MODE_PXHD		BIT(3)
#define MODE_EFBI		BIT(4)
#define MODE_TXP		BIT(5)
#define MODE_MON		BIT(6)
#define MODE_RSTR		BIT(7)
#define MODE_SFS		BIT(8)
#define MODE_XLTR		BIT(9)
#define MODE_EFDI		BIT(10)
#define MODE_FIME		BIT(11)

/* NBTP */
#define NBTP_NSJW(x)		((x) & 0x7f)
#define NBTP_NTSEG2(x)		(((x) & 0x7f) << 8)
#define NBTP_NTSEG1(x)		(((x) & 0x1ff) << 16)
#define NBTP_BRP(x)		(((x) & 0x1f) << 25)

/* DBTP */
#define DBTP_DSJW(x)		((x) & 0x7f)
#define DBTP_DTSEG2(x)		(((x) & 0x7f) << 8)
#define DBTP_DTSEG1(x)		(((x) & 0xff) << 16)
#define DBTP_DTDCO(x)		(((x) & 0xff) << 24)

/* XBTP */
#define XBTP_XSJW(x)		((x) & 0x7f)
#define XBTP_XTSEG2(x)		(((x) & 0x7f) << 8)
#define XBTP_XTSEG1(x)		(((x) & 0xff) << 16)
#define XBTP_XTDCO		(((x) & 0xff) << 24)

/* PCFG */
#define PCFG_PWMS(x)		((x) & 0x1f)
#define PCFG_PWML(x)		(((x) & 0x1f) << 8)
#define PCFG_PWMO(x)		(((x) & 0x1f) << 16)

/* Interrupt register bits */

/* FUNC_RAW and FUNC_CLR */
/* FUNC_ENA */
#define MH_TX_FQ0_IRQ		BIT(0)
#define MH_TX_FQ1_IRQ		BIT(1)
#define MH_TX_FQ2_IRQ		BIT(2)
#define MH_TX_FQ3_IRQ		BIT(3)
#define MH_TX_FQ4_IRQ		BIT(4)
#define MH_TX_FQ5_IRQ		BIT(5)
#define MH_TX_FQ6_IRQ		BIT(6)
#define MH_TX_FQ7_IRQ		BIT(7)
#define MH_RX_FQ0_IRQ		BIT(8)
#define MH_RX_FQ1_IRQ		BIT(9)
#define MH_RX_FQ2_IRQ		BIT(10)
#define MH_RX_FQ3_IRQ		BIT(11)
#define MH_RX_FQ4_IRQ		BIT(12)
#define MH_RX_FQ5_IRQ		BIT(13)
#define MH_RX_FQ6_IRQ		BIT(14)
#define MH_RX_FQ7_IRQ		BIT(15)
#define MH_TX_PQ_IRQ		BIT(16)
#define MH_STOP_IRQ		BIT(17)
#define MH_RX_FILTER_IRQ	BIT(18)
#define MH_TX_FILTER_IRQ	BIT(19)
#define MH_TX_ABORT_IRQ		BIT(20)
#define MH_RX_ABORT_IRQ		BIT(21)
#define MH_STATS_IRQ		BIT(22)
#define PRT_E_ACTIVE		BIT(24)
#define PRT_BUS_ON		BIT(25)
#define PRT_TX_EVT		BIT(26)
#define PRT_RX_EVT		BIT(27)

/* ERR_RAW and ERR_CLR */
/* SAFETY_RAW and SAFETY_CLR */
/* ERR_ENA and SAFETY_ENA*/
#define MH_RX_FILTER_ERR	BIT(0)
#define MH_MEM_SFTY_ERR		BIT(1)
#define MH_REG_CRC_ERR		BIT(2)
#define MH_DESC_ERR		BIT(3)
#define MH_AP_PARITY_ERR	BIT(4)
#define MH_DP_PARITY_ERR	BIT(5)
#define MH_DP_SEQ_ERR		BIT(6)
#define MH_DP_DO_ERR		BIT(7)
#define MH_DP_TO_ERR		BIT(8)
#define MH_DMA_TO_ERR		BIT(9)
#define MH_DMA_CH_ERR		BIT(10)
#define MH_RD_RESP_ERR		BIT(11)
#define MH_WR_RESP_ERR		BIT(12)
#define MH_MEM_TO_ERR		BIT(13)
#define PRT_ABORTED		BIT(16)
#define PRT_USOS		BIT(17)
#define PRT_TX_DU		BIT(18)
#define PRT_RX_DO		BIT(19)
#define PRT_IFF_RQ		BIT(20)
#define PRT_BUS_ERR		BIT(21)
#define PRT_E_PASSIVE		BIT(22)
#define PRT_BUS_OFF		BIT(23)
#define TOP_MUX_TO_ERR		BIT(28)

/* This controller supports CAN XL only mode. */

#define TX_FIFO_QUEUE_BASE_ADD  0x2000
#define TX_PR_QUEUE_BASE_ADD	0x2200
#define RX_FILTER_BASE_ADD	0x2600

/* Define start address of queues and data containers */
/* 1023(max descriptor in Queue)*8(element in TX descriptor)*4(byte) = H'7FE0 */
#define TX_FQ_STADD(base, n)		(base + (n * 0x7FE0))

/* Last FIFO Queue + H'7FE0 */
#define TX_PQ_STADD(base)		(TX_FQ_STADD(base, 7) + 0x7FE0)

/* Index 0 = TX_PQ_START_ADD + 32(max descriptor in PQ Queue)*8(element in TX descriptor)*4(byte) = H'400 */
/* Index 1 = Index 0 + 1023(max descriptor in FQ Queue)*4(element in RX descriptor)*4(byte) = H'3FF0 */
#define RX_FQ_STADD_BASE(base)		(TX_PQ_STADD(base) + 0x400)
#define RX_FQ_STADD(base, n)		(RX_FQ_STADD_BASE(base) + (n * 0x3FF0))

/* This bit field is relevant only when the MH is configured in Continuous Mode */
/* Index 0 = Last RX FIFO Queue + H'3FF0 */
/* Index 1 = Index 0 + RX_FQ_SIZE{n}.DC_SIZE[16:27]*4(byte) = H'3FFC */
#define RX_FQ_DC_CON_BASE(base)		(RX_FQ_STADD(base, 7) + 0x3FF0)
#define RX_FQ_DC_CON_STADD(base, n)	(RX_FQ_DC_CON_BASE(base) + (n * 0x3FFC))

/* TX FIFO data container address */
/* Index 0 = Last RX_FQ_DC_CON_STADD + H'3FFC */
/* Index 1 = Index 0 + [1023(max descriptor in Queue)*50(byte)] = H'C7CE */
#define TX_FQ_DC_BASE(base)		(RX_FQ_DC_CON_STADD(base, 7) + 0x3FFC)
#define TX_FQ_DC_STADD(base, n)		(TX_FQ_DC_BASE(base) + (n * 0xC7CE))

/* TX Priority data container address */
/* Index 0 = Last TX_FQ_DC_STADD + H'C7CE */
#define TX_PQ_DC_STADD(base)		(TX_FQ_DC_STADD(base, 7) + 0xC7CE)

/* RX FIFO data container address */
/* Index 0 = Last TX_PQ_DC_STADD + [32(max descriptor in PQ Queue)*50(byte)]=H'640 */
/* Index 1 = Index 0 + [1023(max descriptor in Queue)*CANXL_MAXIMUM_RX_DC_SIZE*32(byte)])= H'FFC0 */
#define RX_FQ_DC_BASE(base)		(TX_PQ_DC_STADD(base) + 0x640)
#define RX_FQ_DC_STADD(base, n)		(RX_FQ_DC_BASE(base) + (n * 0xFFC0))

/* System memory size */
/* = Last RX_FQ_DC_STADD + H'FFC0 */
#define SYS_MEM_SIZE			0x165000

/* The read address pointer used by the SW to read an RX message in the data container
 * For an initial start, it is mandatory to set VAL[1:0] to 0b11, to avoid RX_FQ_RD_ADD_PT{n}
 * register to be equal to the RX_FQ_START_ADD{n} registers
 */
#define RX_FQ_RD_ADD_PT_VAL(n)		((n == 2) ? 0x00000001 : 0x00000000)

/* CAN-XL registers */
#define CXLGIPV			0x20000
#define CXLGSTS			0x20008
#define CXLGGPT			0x20010
#define CXLGRSTC		0x20080
#define CXLGRESPC		0x20084
#define CXLGLRAPC		0x20088
#define CXLCCLKC		0x20100

/* Message Handler (MH) registers */
#define VERSION			0x000
#define MH_CTRL			0x004
#define MH_CFG			0x008
#define MH_STS			0x00C
#define MH_SFTY_CFG		0x010
#define MH_SFTY_CTRL		0x014
#define RX_FILTER_MEM_ADD	0x018
#define TX_DESC_MEM_ADD		0x01c
#define AXI_ADD_EXT		0x020
#define AXI_PARAMS		0x024
#define MH_LOCK			0x028
#define TX_DESC_ADD_PT		0x100
#define TX_STATISTICS		0x104

/* m is 0,1*/
/* n is 0,1,2 */
/* y is 0-7 */
/* x is 0-3 */
#define TX_FQ_STS(m)		(0x108 + (0x04 * (m)))
#define TX_FQ_CTRL(n)		(0x110 + (0x04 * (n)))
#define TX_FQ_ADD_PT(y)		(0x120 + (0x10 * (y)))
#define TX_FQ_START_ADD(y)	(0x124 + (0x10 * (y)))
#define TX_FQ_SIZE(y)		(0x128 + (0x10 * (y)))
#define TX_PQ_STS(m)		(0x300 + (0x04 * (m)))
#define TX_PQ_CTRL(n)		(0x30c + (0x04 * (n)))
#define TX_PQ_START_ADD		0x318
#define RX_DESC_ADD_PT		0x400
#define RX_STATISTICS		0x404
#define RX_FQ_STS(n)		(0x408 + (0x04 * (n)))
#define RX_FQ_CTRL(n)		(0x414 + (0x04 * (n)))
#define RX_FQ_ADD_PT(y)		(0x420 + (0x18 * (y)))
#define RX_FQ_START_ADD(y)	(0x424 + (0x18 * (y)))
#define RX_FQ_SIZE(y)		(0x428 + (0x18 * (y)))
#define RX_FQ_DC_START_ADD(y)	(0x42c + (0x18 * (y)))
#define RX_FQ_RD_ADD_PT(y)	(0x430 + (0x18 * (y)))
#define TX_FILTER_CTRL(m)	(0x600 + (0x04 * (m)))
#define TX_FILTER_REFVAL(x)	(0x608 + (0x04 * (x)))
#define RX_FILTER_CTRL		0x680
#define TX_FQ_INT_STS		0x700
#define RX_FQ_INT_STS		0x704
#define TX_PQ_INT_STS(m)	(0x708 + (0x04 * (m)))
#define STATS_INT_STS		0x710
#define ERR_INT_STS		0x714
#define SFTY_INT_STS		0x718
#define AXI_ERR_INFO		0x71c
#define DESC_ERR_INFO(m)	(0x720 + (0x04 * (m)))
#define TX_FILTER_ERR_INFO	0x728
#define DEBUG_TEST_CTRL		0x800
#define INT_TEST(m)		(0x804 + (0x04 * (m)))
#define TX_SCAN_FC		0x810
#define TX_SCAN_BC		0x814
#define TX_FQ_DESC_VALID	0x818
#define TX_PQ_DESC_VALID	0x81c
#define CRC_CTRL		0x880
#define CRC_REG			0x884

/* Protocol registers */
#define ENDN		0x900
#define PREL		0x904
#define STAT		0x908
#define EVNT		0x920
#define LOCK		0x940
#define CTRL		0x944
#define FIMC		0x948
#define TEST		0x94c
#define MODE		0x960
#define NBTP		0x964
#define DBTP		0x968
#define XBTP		0x96c
#define PCFG		0x970

/* Interrupt registers */
#define FUNC_RAW	0xa00
#define ERR_RAW		0xa04
#define SAFETY_RAW	0xa08
#define FUNC_CLR	0xa10
#define ERR_CLR		0xa14
#define SAFETY_CLR	0xa18
#define FUNC_ENA	0xa20
#define ERR_ENA		0xa24
#define SAFETY_ENA	0xa28
#define CAPTURING_MODE	0xa30
#define HDP		0xa40

/* DMA Info Ctrl 1 for Tx Queue */
#define CANXL_BIT_VALID(x)	(x << 31)
#define CANXL_BIT_HD		(0x01 << 30)
#define CANXL_BIT_WRAP		(0x00 << 29)
#define CANXL_BIT_NEXT		(0x00 << 28)
#define CANXL_BIT_IRQ(x)	(x << 27)
#define CANXL_BIT_PQ		(0x01 << 26)
#define CANXL_BIT_FQ		(0x00 << 26)
#define CANXL_BIT_RESERVED1	(0x00 << 25)
#define CANXL_BIT_END		(0x01 << 25)
#define CANXL_BIT_CRC(x)	(x << 16)
#define CANXL_BIT_PQSN(x)	(x << 11)
#define CANXL_BIT_FQN(x)	(x << 12)
#define CANXL_BIT_RESERVED2	(0x00 << 11)
#define CANXL_BIT_RESERVED3	(0x00 << 9)
#define CANXL_BIT_RC(x)		((x & 0x1F) << 4)
#define CANXL_BIT_STS		(0x00)

/* DMA Info Ctrl 2 for Tx Queue */
#define CANXL_BIT_RESERVED4	(0X00 << 27)
#define CANXL_BIT_PLSRC(x)	((x) << 26)
#define CANXL_BIT_SIZE(x)	(x << 16)
#define CANXL_BIT_IN(x)		(x << 13)
#define CANXL_BIT_RESERVED5	(0x00 << 12)
#define CANXL_BIT_TDO		(0x000 << 2)
#define CANXL_BIT_NHDO		(0x3FF << 2)
#define CANXL_BIT_RESERVED6	(0x00)

#define CANXL_DMA1_FIXED_PQ		(CANXL_BIT_HD | CANXL_BIT_WRAP | CANXL_BIT_NEXT | \
					 CANXL_BIT_PQ | CANXL_BIT_RESERVED1 | \
					 CANXL_BIT_RESERVED3 | CANXL_BIT_STS)

#define CANXL_DMA2_FIXED_PQ		(CANXL_BIT_RESERVED4 | CANXL_BIT_RESERVED5 | \
					 CANXL_BIT_TDO | CANXL_BIT_RESERVED6)

#define CANXL_DMA1_FIXED_FQ		(CANXL_BIT_HD | CANXL_BIT_WRAP | CANXL_BIT_NEXT | \
					 CANXL_BIT_FQ | CANXL_BIT_RESERVED1 | \
					 CANXL_BIT_RESERVED2 | CANXL_BIT_RESERVED3 | CANXL_BIT_STS)

#define CANXL_DMA2_FIXED_FQ		(CANXL_BIT_RESERVED4 | CANXL_BIT_RESERVED5 | \
					 CANXL_BIT_NHDO | CANXL_BIT_RESERVED6)

/* T0 for Tx Queue */
#define CANXL_BIT_FDF			(0x01 << 31)
#define CANXL_BIT_XLF(x)		(x << 30)
#define CANXL_BIT_XTD			(0x00 << 29)
#define CANXL_BIT_PRID(x)		(x << 18)
#define CANXL_BIT_RRS			(0x00 << 17)
#define CANXL_BIT_SEC(x)		(x << 16)
#define CANXL_BIT_VCID			(0x00 << 8)
#define CANXL_BIT_SDT(x)		(x)

#define CANXL_T0_FIXED			(CANXL_BIT_FDF | CANXL_BIT_XTD | \
					 CANXL_BIT_RRS | CANXL_BIT_VCID)

#define CANFD_BIT_FDF			(0x01 << 31)
#define CANFD_BIT_XLF(x)		((x) << 30)
#define CANFD_BIT_XTD(x)		((x) << 29)
#define CANFD_BIT_BAID(x)		((x) << 18)
#define CANFD_BIT_EXTID(x)		((x))

#define CANFD_T0_FIXED			(CANFD_BIT_FDF | CANFD_BIT_XLF(0))

/* T1 for Tx Queue */
#define CANXL_BIT_RESERVED7		(0x00 << 31)
#define CANXL_BIT_FIR			(0x00 << 30)
#define CANXL_BIT_RESERVED8		(0x00 << 27)
#define CANXL_BIT_DLCXL(x)		(x << 16)
#define CANXL_BIT_RESERVED9		(0x00)

#define CANXL_T1_FIXED			(CANXL_BIT_RESERVED7 | CANXL_BIT_FIR | \
					 CANXL_BIT_RESERVED8 | CANXL_BIT_RESERVED9)

#define CANFD_BIT_RESERVED1		(0x00 << 31)
#define CANFD_BIT_FIR			(0x00 << 30)
#define CANFD_BIT_RESERVED2		(0x00 << 27)
#define CANFD_BIT_RESERVED3		(0x00 << 26)
#define CANFD_BIT_BRS(x)		((x) << 25)
#define CANFD_BIT_RESERVED4		(0x00 << 21)
#define CANFD_BIT_ESI(x)		((x) << 20)
#define CANFD_BIT_DLC(x)		((x) << 16)
#define CANFD_BIT_RESERVED5		(0x00)

#define CANFD_T1_FIXED			(CANFD_BIT_RESERVED1 | CANFD_BIT_FIR | \
					 CANFD_BIT_RESERVED2 | CANFD_BIT_RESERVED3 | \
					 CANFD_BIT_RESERVED4 | CANFD_BIT_RESERVED5)

/* DMA Info Ctrl 1 for Rx Queue */
#define CANXL_RX_BIT_VALID(x)		(x << 31)
#define CANXL_RX_BIT_HD			(0x01 << 30)
#define CANXL_RX_BIT_RESERVED1		(0x00 << 29)
#define CANXL_RX_BIT_NEXT		(0x00 << 28)
#define CANXL_RX_BIT_IRQ(x)		(x << 27)
#define CANXL_RX_BIT_RESERVED2		(0x00 << 25)
#define CANXL_RX_BIT_CRC(x)		(x << 16)
#define CANXL_RX_BIT_FQN(x)		(x << 12)
#define CANXL_RX_BIT_IN(x)		(x << 9)
#define CANXL_RX_BIT_RC(x)		((x & 0x1F) << 4)
#define CANXL_RX_BIT_STS		(0x00 << 0)

#define CANXL_RX_DMA1_FIXED		(CANXL_RX_BIT_VALID(0) | CANXL_RX_BIT_HD | \
					 CANXL_RX_BIT_RESERVED1 | CANXL_RX_BIT_NEXT | \
					 CANXL_RX_BIT_RESERVED2 | CANXL_RX_BIT_STS)

/* R0 for Rx Queue */
#define CANXL_RX_BIT_PRIO(x)	(x >> 18)
#define CANXL_RX_BIT_SEC(x)	(x >> 16)
#define CANXL_RX_BIT_VCID(x)	(x >> 8)
#define CANXL_RX_BIT_SDT(x)	(x >> 0)

#define CANFD_RX_BIT_XTD(x)	((x) >> 29)
#define CANFD_RX_BIT_BAID(x)	((x) >> 18)
#define CANFD_RX_BIT_EXTID(x)	((x))

/* R1 for Rx Queue */
#define CANXL_RX_BIT_DLCXL(x)	(x >> 16)

#define CANFD_RX_BIT_BRS(x)	((x) >> 25)
#define CANFD_RX_BIT_ESI(x)	((x) >> 20)
#define CANFD_RX_BIT_DLC(x)	((x) >> 16)

/* Tx Descriptors m  */
#define TXElement0(m)		(0x0 + (0x20 * m))	/* SW/MH: DMA Info Ctrl 1 */
#define TXElement1(m)		(0x4 + (0x20 * m))	/* SW/MH: DMA Info Ctrl 2 */
#define TXElement2TS0(m)	(0x8 + (0x20 * m))	/* MH: TimeStamp 0 */
#define TXElement3TS1(m)	(0xc + (0x20 * m))	/* MH: TimeStamp 1 */
#define TXElement4T0(m)		(0x10 + (0x20 * m))	/* SW: TX Message Header Information */
#define TXElement5T1(m)		(0x14 + (0x20 * m))	/* SW: TX Message Header Information */
#define TXElement6T2TD0(m)	(0x18 + (0x20 * m))	/* SW: TX Message Header Information */
#define TXElement7TX_APTD1(m)	(0x1c + (0x20 * m))	/* SW: TX Payload Data Address Pointer */

/* Rx Descriptors m */
#define RXElement0(m)		(0x0 + (0x10 * m))	/* DMA info Ctrl 1 */
#define RXElement1(m)		(0x4 + (0x10 * m))	/* RX Address Pointer */
#define RXElement2TS0(m)	(0x8 + (0x10 * m))	/* TimeStamp 0 */
#define RXElement3TS1(m)	(0xc + (0x10 * m))	/* TimeStamp 1 */

/* Constants */
#define RCANXL_FIFO_DEPTH		8	/* Tx FIFO depth */
#define RCANXL_NAPI_WEIGHT		8	/* Rx poll quota */
#define CANXL_MAXIMUM_FQ_TX_DESCRIPTOR	1	/* Define maximum TX descriptors*/
#define CANXL_MAXIMUM_PQ_TX_DESCRIPTOR	32	/* Define maximum Priority TX descriptors */
#define CANXL_MAXIMUM_RX_DESCRIPTOR	1023	/* Define maximum RX descriptors*/
#define CANXL_MAXIMUM_RX_DC_SIZE	2	/* Define data container size of RX descriptors */
#define CANXL_TX_PQ_SLOT_ENABLE		0xFFFFFFFF

#define QUEUE(x)			BIT(x)

/* fCAN clock select register settings */
enum rcar_canxl_fcanclk {
	RCANXL_CANXLCLK = 0,		/* CANXL clock */
	RCANXL_EXTCLK,			/* Externally input clock */
};

struct rcar_canxl_global;

/* Channel priv data */
struct rcar_canxl_channel {
	struct can_priv can;			/* Must be the first member */
	struct net_device *ndev;
	struct rcar_canxl_global *gpriv;	/* Controller reference */
	void __iomem *base;			/* Register base address */
	struct napi_struct napi;
	u8 tx_len[RCANXL_FIFO_DEPTH];		/* For net stats */
	u32 tx_head;				/* Incremented on xmit */
	u32 tx_tail;				/* Incremented on xmit done */
	spinlock_t tx_lock;			/* To protect tx path */
};

enum rcar_canxl_chip_id {
	GEN5,
};

struct rcar_canxl_of_data {
	enum rcar_canxl_chip_id chip_id;
};

/* Global priv data */
struct rcar_canxl_global {
	struct rcar_canxl_channel *ch;
	void __iomem *base;		/* Register base address */
	u8 *sys_base;			/* System memory base address */
	phys_addr_t phys_sys_base;	/* System memory physical base address */
	struct platform_device *pdev;	/* Respective platform device */
	struct clk *clkp;		/* Peripheral clock */
	struct clk *can_clk;		/* fCAN clock */
	enum rcar_canxl_fcanclk fcan;	/* CANXL or Ext clock */
	bool xlmode;			/* CANXL mode */
	enum rcar_canxl_chip_id chip_id;
	u32 channel;
};

/* CAN XL mode nominal rate constants */
static const struct can_bittiming_const rcar_canxl_nom_bittiming_const = {
	.name = RCANXL_DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 511,
	.tseg2_min = 1,
	.tseg2_max = 127,
	.sjw_max = 127,
	.brp_min = 0,
	.brp_max = 31,
	.brp_inc = 1,
};

/* CAN XL mode data rate constants */
static const struct can_bittiming_const rcar_canxl_data_bittiming_const = {
	.name = RCANXL_DRV_NAME,
	.tseg1_min = 0,
	.tseg1_max = 255,
	.tseg2_min = 1,
	.tseg2_max = 127,
	.sjw_max = 127,
	.brp_min = 0,
	.brp_max = 31,
	.brp_inc = 1,
};

/* Helper functions */
static inline void rcar_canxl_update(u32 mask, u32 val, u32 __iomem *reg)
{
	u32 data = readl(reg);

	data &= ~mask;
	data |= (val & mask);
	writel(data, reg);
}

static inline u32 rcar_canxl_read(void __iomem *base, u32 offset)
{
	return readl(base + (offset));
}

static inline u32 rcar_canxl_read_desc(u32 desc_addr, u32 offset)
{
	void __iomem *addr;
	u32 val;

	addr = ioremap_cache(desc_addr, offset);
	val = ioread32(addr + (offset));
	iounmap(addr);

	return val;
}

static inline void rcar_canxl_write(void __iomem *base, u32 offset, u32 val)
{
	writel(val, base + (offset));
}

static inline void rcar_canxl_write_desc(u32 desc_addr, u32 offset, u32 val)
{
	void __iomem *addr;

	addr = ioremap_cache(desc_addr, offset);
	iowrite32(val, addr + offset);

	iounmap(addr);
}

static void rcar_canxl_set_bit(void __iomem *base, u32 reg, u32 val)
{
	rcar_canxl_update(val, val, base + (reg));
}

static void rcar_canxl_clear_bit(void __iomem *base, u32 reg, u32 val)
{
	rcar_canxl_update(val, 0, base + (reg));
}

static void rcar_canxl_get_data(struct canxl_frame *cxl, u32 container)
{
	u32 i;

	for (i = 0; i < cxl->len; i++)
		*((u32 *)cxl->data + i) =
			rcar_canxl_read_desc(container, i * sizeof(u32));
}

static void rcar_canxl_put_data(struct canxl_frame *cxl, u32 container)
{
	u32 i;

	for (i = 0; i < cxl->len; i++)
		rcar_canxl_write_desc(container, i * sizeof(u32), *((u32 *)cxl->data + i));
}

static void rcar_canfd_get_data(struct canfd_frame *cfd, u32 container)
{
	u32 i;

	for (i = 0; i < cfd->len; i++)
		*((u32 *)cfd->data + i) =
			rcar_canxl_read_desc(container, i * sizeof(u32));
}

static void rcar_canfd_put_data(struct canfd_frame *cfd, u32 container)
{
	u32 i;

	for (i = 0; i < cfd->len; i++)
		rcar_canxl_write_desc(container, i * sizeof(u32), *((u32 *)cfd->data + i));
}

static void rcar_canfd_put_first_payload(struct canfd_frame *cfd, u32 container)
{
	u32 i;

	for (i = 0; i < 4; i++)
		rcar_canxl_write_desc(container, i * sizeof(u32), *((u32 *)cfd->data + i));
}

static void rcar_canxl_tx_failure_cleanup(struct net_device *ndev)
{
	u32 i;

	for (i = 0; i < RCANXL_FIFO_DEPTH; i++)
		can_free_echo_skb(ndev, i);
}

static void rcar_canxl_descriptor_init(struct rcar_canxl_global *gpriv)
{
	u16 desc, queue, desc_rc, ch;
	u32 base;

	ch = gpriv->channel;
	base = gpriv->phys_sys_base;

	/* Initialize common parts of Tx Descriptors (8 FIFO Queues) */
	for (queue = 0 ; queue < 8; queue++) {
		for (desc = 0; desc < CANXL_MAXIMUM_FQ_TX_DESCRIPTOR; desc++) {
			u32 ele0, ele1, ele4_t0, ele5_t1;

			desc_rc = (desc % 32);
			ele0 = CANXL_DMA1_FIXED_FQ | CANXL_BIT_RC(desc_rc);
			ele1 = CANXL_DMA2_FIXED_FQ | CANXL_BIT_IN(ch);
			if (gpriv->xlmode) {
				ele4_t0 = CANXL_T0_FIXED;
				ele5_t1 = CANXL_T1_FIXED;
			} else {
				ele4_t0 = CANFD_T0_FIXED;
				ele5_t1 = CANFD_T1_FIXED;
			}

			rcar_canxl_write_desc(TX_FQ_STADD(base, queue),
					      TXElement0(desc), ele0);
			rcar_canxl_write_desc(TX_FQ_STADD(base, queue),
					      TXElement1(desc), ele1);
			rcar_canxl_write_desc(TX_FQ_STADD(base, queue),
					      TXElement4T0(desc), ele4_t0);
			rcar_canxl_write_desc(TX_FQ_STADD(base, queue),
					      TXElement5T1(desc), ele5_t1);
		}
	}

	/* Initialize common parts of Tx Descriptors (Priority Queue) */
	for (desc = 0; desc < CANXL_MAXIMUM_PQ_TX_DESCRIPTOR; desc++) {
		u32 ele0, ele1, ele4_t0, ele5_t1;

		ele0 = CANXL_DMA1_FIXED_PQ | CANXL_BIT_RC(desc);
		ele1 = CANXL_DMA2_FIXED_PQ | CANXL_BIT_IN(ch);
		if (gpriv->xlmode) {
			ele4_t0 = CANXL_T0_FIXED;
			ele5_t1 = CANXL_T1_FIXED;
		} else {
			ele4_t0 = CANFD_T0_FIXED;
			ele5_t1 = CANFD_T1_FIXED;
		}

		rcar_canxl_write_desc(TX_PQ_STADD(base),
				      TXElement0(desc), ele0);
		rcar_canxl_write_desc(TX_PQ_STADD(base),
				      TXElement1(desc), ele1);
		rcar_canxl_write_desc(TX_PQ_STADD(base),
				      TXElement4T0(desc), ele4_t0);
		rcar_canxl_write_desc(TX_PQ_STADD(base),
				      TXElement5T1(desc), ele5_t1);
	}

	/* Initialize common parts of Rx Descriptors (8 FIFO Queues) */
	for (queue = 0 ; queue < 8; queue++) {
		for (desc = 0; desc < CANXL_MAXIMUM_RX_DESCRIPTOR; desc++) {
			u32 ele0, ele1, ele2_ts0, ele3_ts1;

			desc_rc = (desc % 32);
			ele0 = CANXL_RX_DMA1_FIXED | CANXL_RX_BIT_IRQ(0x1) |
			       CANXL_RX_BIT_RC(desc_rc) | CANXL_RX_BIT_IN(ch) |
			       CANXL_RX_BIT_CRC(0) | CANXL_RX_BIT_FQN(0);
			/* Size is 64 byte data container for each descriptor */
			ele1 = RX_FQ_DC_STADD(base, queue)
			       + (desc * CANXL_MAXIMUM_RX_DC_SIZE * 32);
			ele2_ts0 = 0;
			ele3_ts1 = 0;

			rcar_canxl_write_desc(RX_FQ_STADD(base, queue),
					      RXElement0(desc), ele0);
			rcar_canxl_write_desc(RX_FQ_STADD(base, queue),
					      RXElement1(desc), ele1);
			rcar_canxl_write_desc(RX_FQ_STADD(base, queue),
					      RXElement2TS0(desc), ele2_ts0);
			rcar_canxl_write_desc(RX_FQ_STADD(base, queue),
					      RXElement3TS1(desc), ele3_ts1);
		}
	}
}

static int rcar_canxl_check_queue(struct rcar_canxl_global *gpriv,
				  int queue, u32 *target_desc_index)
{
	int ret, desc_index = 0;
	u32 size, start_desc, current_desc, desc;

	/* Descriptor size of Queue 0 */
	size = rcar_canxl_read(gpriv->base, TX_FQ_SIZE(queue));

	/* Start address of Queue 0 */
	start_desc = rcar_canxl_read(gpriv->base, TX_FQ_START_ADD(queue));

	/* Current address pointer of Queue 0 */
	current_desc = rcar_canxl_read(gpriv->base, TX_FQ_ADD_PT(queue));

	if (current_desc == 0)
		current_desc = start_desc;
	else
		current_desc = current_desc + 0x20;

	if (current_desc == (start_desc + (size * 0x20)))
		current_desc = start_desc;

	while (desc_index <= size) {
		u32 ele0;

		desc = ((current_desc - start_desc) / 0x20);
		ele0 = rcar_canxl_read_desc(TX_FQ_STADD(gpriv->phys_sys_base, queue),
					    TXElement0(desc));
		if (!(ele0 & CANXL_BIT_VALID(1))) {
			/* Set the found vacancy as the
			 * descriptor place for this Tx message
			 */
			*target_desc_index = desc;
			ret = 0;
			break;
		}

		/* If it is not reach last descriptor yet, move to next descriptor */
		if ((start_desc + (size * 0x20)) != current_desc)
			current_desc = current_desc + 0x20;
		/* Or wrap to first descriptor if reach the last one */
		else
			current_desc = start_desc;

		desc_index++;
	}

	/* If Tx FIFO Queue is full, return as 1 */
	if (desc_index > size)
		ret = 1;

	return ret;
}

static int rcar_canxl_local_ram_init(struct rcar_canxl_global *gpriv)
{
	u32 sts;
	int err;

	/* Check LRAMINIT flag as Local RAM initialization */
	err = readl_poll_timeout(gpriv->base + CXLGSTS, sts,
				 !(sts & CXLGSTS_LRAMINIT), 2, 500000);
	if (err) {
		dev_dbg(&gpriv->pdev->dev, "Local ram init failed\n");
		return err;
	}

	return 0;
}

static void rcar_canxl_enable_clock(struct rcar_canxl_global *gpriv)
{
	u32 val, mh_sts, stat;

	/* Write 32'h0000_c407 to CXLCCLKC */
	val = 0x0000C407;
	rcar_canxl_write(gpriv->base, CXLCCLKC, val);

	/* Check that the XCAN clock is valid or not */
	mh_sts = rcar_canxl_read(gpriv->base, MH_STS);
	stat = rcar_canxl_read(gpriv->base, STAT);

	if (!((mh_sts & MH_STS_CLOCK_ACTIVE) && (stat & STAT_CLKA)))
		dev_dbg(&gpriv->pdev->dev, "XCAN clock is invalid\n");
}

static void rcar_canxl_reset_controller(struct rcar_canxl_global *gpriv)
{
	u32 cfg;

	/* Reset protocol controller */
	rcar_canxl_write(gpriv->base, CTRL, CTRL_SRES);

	/* Set the controller into CAN-XL only mode
	 * MODE.FDOE = 1, MODE.XLOE = 1, MODE.XLTR = 0, MODE.EFDI = 0
	 * MODE.SFS = 1: Timestamps captured at the start of a frame
	 * or CAN-FD mode
	 */
	if (gpriv->xlmode)
		cfg = (MODE_FDOE | MODE_XLOE | MODE_SFS);
	else
		cfg = (MODE_FDOE | MODE_SFS);
	rcar_canxl_write(gpriv->base, MODE, cfg);
}

static void rcar_canxl_configure_tx_filter(struct rcar_canxl_global *gpriv)
{
	u32 cfg;

	/* MODE set as 1 (Accept on match)
	 * CAN_FD and CC_CAN set as 1 (Reject Classic CAN and CAN-FD messages)
	 * EN set as 1 (Enable TX filter for all TX message)
	 * IRQ_EN set as 1 (Enable interrupt tx_filter_irq)
	 */
	if (gpriv->xlmode) {
		cfg = (TX_FILTER_CTRL0_MODE | TX_FILTER_CTRL0_CAN_FD |
		       TX_FILTER_CTRL0_CC_CAN | TX_FILTER_CTRL0_EN |
		       TX_FILTER_CTRL0_IRQ_EN);
	} else {
		cfg = (TX_FILTER_CTRL0_MODE | TX_FILTER_CTRL0_CC_CAN |
		       TX_FILTER_CTRL0_EN | TX_FILTER_CTRL0_IRQ_EN);
	}
	rcar_canxl_write(gpriv->base, TX_FILTER_CTRL(0), cfg);

	/* Enable one of the 16 TX Filters and to select the right bit field
	 * in the TX message header to compare with
	 * When FIELD[n] = 1 the TX filter element n is considering SDT, otherwise VCID
	 */
	cfg = (TX_FILTER_CTRL1_VALID(0xFFFF) | TX_FILTER_CTRL1_FIELD(0xFFFF));
	rcar_canxl_write(gpriv->base, TX_FILTER_CTRL(1), cfg);

	/* Set the TX_FILTER_REFVAL0, TX_FILTER_REFVAL1, TX_FILTER_REFVAL2
	 * and TX_FILTER_REFVAL3 registers to define value or value/mask
	 * pair to perform the comparison
	 */
	rcar_canxl_write(gpriv->base, TX_FILTER_REFVAL(0), 1);
	rcar_canxl_write(gpriv->base, TX_FILTER_REFVAL(1), 0);
	rcar_canxl_write(gpriv->base, TX_FILTER_REFVAL(2), 0);
	rcar_canxl_write(gpriv->base, TX_FILTER_REFVAL(3), 0);
}

static void rcar_canxl_configure_rx_fifo_queue(struct rcar_canxl_global *gpriv)
{
	u32 cfg;
	u16 max_desc, dc_size;
	int i;

	max_desc = CANXL_MAXIMUM_RX_DESCRIPTOR;	/* Define maximum MAX_DESC */
	dc_size = CANXL_MAXIMUM_RX_DC_SIZE;	/* Size = CANXL_MAXIMUM_RX_DC_SIZE * 32bytes */

	/* Set MH as Normal mode */
	cfg = rcar_canxl_read(gpriv->base, MH_CFG);
	cfg &= ~MH_CFG_RX_CONT_DC;
	rcar_canxl_write(gpriv->base, MH_CFG, cfg);

	/* Define base address of the RX Filter */
	rcar_canxl_write(gpriv->base, RX_FILTER_MEM_ADD,
			 RX_FILTER_BASE_ADD);

	/* Define MAX_DESC and DC_SIZE */
	cfg = (RX_FQ_SIZE_MAX_DESC(max_desc) | RX_FQ_SIZE_DC_SIZE(dc_size));

	/* Define start address RX FIFO Queues */
	for (i = 0; i < 8; i++) {
		rcar_canxl_write(gpriv->base, RX_FQ_START_ADD(i),
				 RX_FQ_STADD(gpriv->phys_sys_base, i));
		rcar_canxl_write(gpriv->base, RX_FQ_SIZE(i), cfg);
	}
}

static void rcar_canxl_configure_tx_fifo_queue(struct rcar_canxl_global *gpriv)
{
	u32 cfg;
	u16 max_retrans, max_desc;
	int i;

	max_retrans = 0x07;				/* Define MAX_RETRANS as 0x07 */
	max_desc = CANXL_MAXIMUM_FQ_TX_DESCRIPTOR;	/* Define maximum MAX_DESC */

	cfg = rcar_canxl_read(gpriv->base, MH_CFG);
	cfg |= MH_CFG_MAX_RETRANS(max_retrans);
	rcar_canxl_write(gpriv->base, MH_CFG, cfg);

	/* Define base address of the TX Fifo/Priority */
	cfg = (TX_DESC_MEM_ADD_FQ_BASE_ADDR(TX_FIFO_QUEUE_BASE_ADD) |
	       TX_DESC_MEM_ADD_PQ_BASE_ADDR(TX_PR_QUEUE_BASE_ADD));
	rcar_canxl_write(gpriv->base, TX_DESC_MEM_ADD, cfg);

	/* Define MAX_DESC */
	cfg = TX_FQ_SIZE_MAX_DESC(max_desc);

	/* Define start address TX FIFO Queues */
	for (i = 0; i < 8; i++) {
		rcar_canxl_write(gpriv->base, TX_FQ_START_ADD(i),
				 TX_FQ_STADD(gpriv->phys_sys_base, i));
		rcar_canxl_write(gpriv->base, TX_FQ_SIZE(i), cfg);
	}
}

static void rcar_canxl_configure_tx_prior_queue(struct rcar_canxl_global *gpriv)
{
	/* Define start address of the TX Priority Queue */
	rcar_canxl_write(gpriv->base, TX_PQ_START_ADD,
			 TX_PQ_STADD(gpriv->phys_sys_base));
}

static void rcar_canxl_abort_rx_fifo_queue(struct rcar_canxl_global *gpriv, u16 rx_fifo)
{
	struct platform_device *pdev = gpriv->pdev;
	int err;
	u32 sts;

	/* Abort a RX FIFO Queue */
	rcar_canxl_write(gpriv->base, RX_FQ_CTRL(1),
			 RX_FQ_CTRL1_ABORT(rx_fifo));

	/* Verify mode change */
	err = readl_poll_timeout(gpriv->base + RX_FQ_STS(0), sts,
				 !(sts & RX_FQ_STS0_BUSY(rx_fifo)), 2, 500000);
	if (err)
		dev_err(&pdev->dev, "RX FIFO Queue is busy\n");

	err = readl_poll_timeout(gpriv->base + RX_FQ_STS(0), sts,
				 !(sts & RX_FQ_STS0_STOP(rx_fifo)), 2, 500000);
	if (err)
		dev_err(&pdev->dev, "RX FIFO Queue stop failed\n");

	err = readl_poll_timeout(gpriv->base + RX_FQ_STS(1), sts,
				 !(sts & RX_FQ_STS1_ERR(rx_fifo)), 2, 500000);
	if (err)
		dev_err(&pdev->dev, "An inconsistent RX descriptor being loaded\n");

	err = readl_poll_timeout(gpriv->base + RX_FQ_STS(1), sts,
				 !(sts & RX_FQ_STS1_UNVAL(rx_fifo)), 2, 500000);
	if (err)
		dev_err(&pdev->dev, "RX descriptor detected with VALID=0\n");

	rcar_canxl_write(gpriv->base, RX_FQ_CTRL(1), 0);
	rcar_canxl_write(gpriv->base, RX_FQ_CTRL(2), 0);
}

static void rcar_canxl_abort_tx_fifo_queue(struct rcar_canxl_global *gpriv, u16 tx_fifo)
{
	struct platform_device *pdev = gpriv->pdev;
	int err;
	u32 sts;

	/* Abort a TX FIFO Queue */
	rcar_canxl_write(gpriv->base, TX_FQ_CTRL(1),
			 TX_FQ_CTRL1_ABORT(tx_fifo));

	/* Verify mode change */
	err = readl_poll_timeout(gpriv->base + TX_FQ_STS(0), sts,
				 !(sts & TX_FQ_STS0_BUSY(tx_fifo)), 2, 500000);
	if (err)
		dev_err(&pdev->dev, "TX FIFO Queue is busy\n");

	err = readl_poll_timeout(gpriv->base + TX_FQ_STS(0), sts,
				 !(sts & TX_FQ_STS0_STOP(tx_fifo)), 2, 500000);
	if (err)
		dev_err(&pdev->dev, "TX FIFO Queue stop failed\n");

	rcar_canxl_write(gpriv->base, TX_FQ_CTRL(1), 0);
	rcar_canxl_write(gpriv->base, TX_FQ_CTRL(2), 0);
}

static void rcar_canxl_abort_tx_prior_queue(struct rcar_canxl_global *gpriv)
{
	struct platform_device *pdev = gpriv->pdev;
	int err;
	u32 sts;

	rcar_canxl_write(gpriv->base, MH_LOCK, MH_LOCK_ULK(0x1234)); /* Write 0x1234 to ULK */
	rcar_canxl_write(gpriv->base, MH_LOCK, MH_LOCK_ULK(0x4321)); /* Write 0x4321 to ULK */

	/* Abort all slots in TX Priority Queue */
	rcar_canxl_write(gpriv->base, TX_PQ_CTRL(1),
			 CANXL_TX_PQ_SLOT_ENABLE);

	/* Verify mode change */
	err = readl_poll_timeout(gpriv->base + TX_PQ_STS(0), sts,
				 !(sts & CANXL_TX_PQ_SLOT_ENABLE), 2, 500000);
	if (err)
		dev_err(&pdev->dev, "TX Priority Queue is busy\n");

	rcar_canxl_write(gpriv->base, TX_PQ_CTRL(1), 0);
	rcar_canxl_write(gpriv->base, TX_PQ_CTRL(2), 0);
}

static void rcar_canxl_configure_mh_global(struct rcar_canxl_global *gpriv, u32 ch)
{
	u32 cfg;

	/* Indicate the X_CAN instance number */
	cfg = MH_CFG_INST_NUM(ch);
	rcar_canxl_write(gpriv->base, MH_CFG, cfg);

	/* Initialize MH_SFTY register */
	rcar_canxl_write(gpriv->base, MH_SFTY_CFG, 0xFFFFFFFF);
	/* Enable all event except CRC checking */
	rcar_canxl_write(gpriv->base, MH_SFTY_CTRL, 0x00FC);

	/* Initialize AXI register */
	/* Define the MSB of the read/write AXI address bus used on the DMA_AXI interface
	 * If the AXI address is up to 64bit this register is used
	 */
	rcar_canxl_write(gpriv->base, AXI_ADD_EXT, 0);
	/* Define the maximum read/write pending transactions on DMA_AXI interface */
	rcar_canxl_write(gpriv->base, AXI_PARAMS, 0x33);

	/* The RX_STATISTICS and TX_STATISTICS registers must be set to 0 to
	 * ensure status of new transmissions and receptions
	 */
	cfg = (STATISTICS_SUCC(0) | STATISTICS_UNSUCC_W(0));
	rcar_canxl_write(gpriv->base, TX_STATISTICS, cfg);
	rcar_canxl_write(gpriv->base, RX_STATISTICS, cfg);
}

static void rcar_canxl_configure_rx_filter(struct rcar_canxl_global *gpriv)
{
	u32 cfg;
	u16 nb_fe, anmf_fq, threshold;

	/* Number of RX filter is 0 */
	nb_fe = 0;
	/* Define the latest point in time to wait for
	 * the result of the RX filtering process
	 */
	threshold = 0x1F;
	/* Default RX FIFO Queue 0 */
	anmf_fq = 0;

	cfg = (RX_FILTER_CTRL_NB_FE(nb_fe) | RX_FILTER_CTRL_ANMF_FQ(anmf_fq) |
	       RX_FILTER_CTRL_THRESHOLD(threshold) | RX_FILTER_CTRL_ANMF);
	rcar_canxl_write(gpriv->base, RX_FILTER_CTRL, cfg);
}

static void rcar_canxl_enable_interrupts(struct rcar_canxl_global *gpriv)
{
	u32 cfg;

	/* Clear interrupt flags of FUNC, ERR */
	rcar_canxl_write(gpriv->base, FUNC_CLR, 0xFFFFFFFF);
	rcar_canxl_write(gpriv->base, ERR_CLR, 0xFFFFFFFF);

	/* FUNC interrupts setup */
	cfg = (MH_TX_FQ0_IRQ | MH_RX_FQ0_IRQ |
	       MH_STOP_IRQ | MH_RX_FILTER_IRQ |
	       MH_TX_FILTER_IRQ | MH_TX_ABORT_IRQ |
	       MH_RX_ABORT_IRQ | MH_STATS_IRQ |
	       PRT_E_ACTIVE | PRT_BUS_ON |
	       PRT_TX_EVT | PRT_RX_EVT);
	rcar_canxl_set_bit(gpriv->base, FUNC_ENA, cfg);

	/* ERR interrupts setup */
	cfg = (MH_RX_FILTER_ERR | MH_MEM_SFTY_ERR |
	       MH_REG_CRC_ERR | MH_DESC_ERR |
	       MH_AP_PARITY_ERR | MH_DP_PARITY_ERR |
	       MH_DP_SEQ_ERR | MH_DP_DO_ERR |
	       MH_DP_TO_ERR | MH_DMA_TO_ERR |
	       MH_DMA_CH_ERR | MH_RD_RESP_ERR |
	       MH_WR_RESP_ERR | MH_MEM_TO_ERR |
	       PRT_ABORTED | PRT_USOS | PRT_TX_DU |
	       PRT_RX_DO | PRT_IFF_RQ | PRT_BUS_ERR |
	       PRT_E_PASSIVE | PRT_BUS_OFF | TOP_MUX_TO_ERR);
	rcar_canxl_set_bit(gpriv->base, ERR_ENA, cfg);
}

static void rcar_canxl_disable_interrupts(struct rcar_canxl_global
						 *gpriv)
{
	/* Disable all interrupts */
	rcar_canxl_write(gpriv->base, FUNC_ENA, 0);
	rcar_canxl_write(gpriv->base, ERR_ENA, 0);

	/* Clear interrupt flags of FUNC, ERR */
	rcar_canxl_write(gpriv->base, FUNC_CLR, 0xFFFFFFFF);
	rcar_canxl_write(gpriv->base, ERR_CLR, 0xFFFFFFFF);
}

static void rcar_canxl_func_raw(struct net_device *ndev)
{
	struct rcar_canxl_channel *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	u32 func_raw, sts;

	func_raw = rcar_canxl_read(priv->base, FUNC_RAW);
	sts = rcar_canxl_read(priv->base, TX_FQ_INT_STS);
	if ((func_raw & MH_TX_FQ0_IRQ) && TX_FQ_INT_STS_UNVAL(sts)) {
		netdev_dbg(ndev, "Invalid TX descriptor\n");
		stats->tx_dropped++;
	}

	sts = rcar_canxl_read(priv->base, RX_FQ_INT_STS);
	if ((func_raw & MH_RX_FQ0_IRQ) && RX_FQ_INT_STS_UNVAL(sts)) {
		netdev_dbg(ndev, "Invalid RX descriptor\n");
		stats->rx_dropped++;
	}
	if (func_raw & MH_STOP_IRQ)
		netdev_dbg(ndev, "PRT is stopped\n");
	if (func_raw & MH_STATS_IRQ)
		netdev_dbg(ndev, "RX/TX counters have reached the threshold\n");
	if (func_raw & PRT_E_ACTIVE)
		netdev_dbg(ndev,
			   "Switched from Error-Passive to Error-Active state\n");
	if (func_raw & PRT_BUS_ON)
		netdev_dbg(ndev, "Started CAN communication\n");
}

static void rcar_canxl_error_raw(struct net_device *ndev, u32 err_raw,
				 u16 txerr, u16 rxerr)
{
	struct rcar_canxl_channel *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;

	netdev_dbg(ndev, "err_raw %x txerr %u rxerr %u\n", err_raw, txerr, rxerr);

	/* Propagate the error condition to the CAN stack */
	skb = alloc_can_err_skb(ndev, &cf);
	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	/* Error interrupts */
	if (err_raw & MH_RX_FILTER_ERR)
		netdev_dbg(ndev, "RX filtering has not finished in time\n");
	if (err_raw & MH_MEM_SFTY_ERR)
		netdev_dbg(ndev, "Error in L_MEM\n");
	if (err_raw & MH_REG_CRC_ERR) {
		netdev_dbg(ndev, "CRC error at the register bank\n");
		cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
	}
	if (err_raw & MH_DESC_ERR) {
		netdev_dbg(ndev, "CRC error detected on RX/TX descriptor\n");
		cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
	}
	if (err_raw & MH_AP_PARITY_ERR)
		netdev_dbg(ndev, "Parity error at address pointers\n");
	if (err_raw & MH_DP_PARITY_ERR) {
		stats->rx_errors++;
		netdev_dbg(ndev, "Parity error at RX message data\n");
	}
	if (err_raw & MH_DP_SEQ_ERR)
		netdev_dbg(ndev, "Incorrect sequence\n");
	if (err_raw & MH_DP_DO_ERR) {
		netdev_dbg(ndev, "Data overflow at RX buffer\n");
		stats->rx_errors++;
		cf->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
	}
	if (err_raw & MH_DP_TO_ERR) {
		netdev_dbg(ndev, "Timeout at TX_MSG interface\n");
		stats->tx_errors++;
		cf->can_id |= CAN_ERR_TX_TIMEOUT;
	}
	if (err_raw & MH_DMA_TO_ERR)
		netdev_dbg(ndev, "Timeout at DMA_AXI interface\n");
	if (err_raw & MH_DMA_CH_ERR)
		netdev_dbg(ndev, "Routing error\n");
	if (err_raw & MH_RD_RESP_ERR) {
		netdev_dbg(ndev, "Bus error caused by a read access\n");
		cf->can_id |= CAN_ERR_BUSERROR | CAN_ERR_PROT;
		cf->data[2] = CAN_ERR_PROT_UNSPEC;
		priv->can.can_stats.bus_error++;
	}
	if (err_raw & MH_WR_RESP_ERR) {
		netdev_dbg(ndev, "Bus error caused by a write access\n");
		cf->can_id |= CAN_ERR_BUSERROR | CAN_ERR_PROT;
		cf->data[2] = CAN_ERR_PROT_UNSPEC;
		priv->can.can_stats.bus_error++;
	}
	if (err_raw & MH_MEM_TO_ERR)
		netdev_dbg(ndev, "Timeout at local memory\n");
	if (err_raw & PRT_ABORTED)
		netdev_dbg(ndev, "Stop of TX_MSG sequence\n");
	if (err_raw & PRT_USOS)
		netdev_dbg(ndev, "Unexpected Start of Sequence\n");
	if (err_raw & PRT_TX_DU) {
		stats->tx_errors++;
		netdev_dbg(ndev, "Underrun condition at TX_MSG\n");
	}
	if (err_raw & PRT_RX_DO) {
		netdev_dbg(ndev, "Overflow condition at RX_MSG\n");
		stats->rx_errors++;
		cf->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
	}
	if (err_raw & PRT_IFF_RQ) {
		netdev_dbg(ndev, "Invalid Frame Format at TX_MSG\n");
		stats->tx_errors++;
		cf->data[2] |= CAN_ERR_PROT_FORM;
	}
	if (err_raw & PRT_BUS_ERR) {
		netdev_dbg(ndev, "Error on the CAN Bus\n");
		cf->can_id |= CAN_ERR_BUSERROR | CAN_ERR_PROT;
		cf->data[2] = CAN_ERR_PROT_UNSPEC;
		priv->can.can_stats.bus_error++;
	}
	if (err_raw & PRT_E_PASSIVE)
		netdev_dbg(ndev,
			   "Switched from Error-Active to Error-Passive state\n");
	if (err_raw & PRT_BUS_OFF) {
		netdev_dbg(ndev, "Entered Bus_Off state\n");
		rcar_canxl_tx_failure_cleanup(ndev);
		priv->can.state = CAN_STATE_BUS_OFF;
		priv->can.can_stats.bus_off++;
		can_bus_off(ndev);
		cf->can_id |= CAN_ERR_BUSOFF;
	}
	if (err_raw & TOP_MUX_TO_ERR)
		netdev_dbg(ndev, "Timeout at top-level multiplexer\n");

	/* Clear error interrupts that are handled */
	rcar_canxl_write(priv->base, ERR_CLR, err_raw);
	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_rx(skb);
}

static void rcar_canxl_tx_done(struct net_device *ndev)
{
	struct rcar_canxl_channel *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	u32 sent;
	unsigned long flags;
	struct rcar_canxl_global *gpriv = priv->gpriv;

	sent = priv->tx_tail % RCANXL_FIFO_DEPTH;
	stats->tx_packets++;
	stats->tx_bytes += priv->tx_len[sent];
	priv->tx_len[sent] = 0;
	can_get_echo_skb(ndev, sent, NULL);

	spin_lock_irqsave(&priv->tx_lock, flags);
	priv->tx_tail++;

	netif_wake_queue(ndev);
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	/* Clear TX FIFO Queue Interrupt Status */
	rcar_canxl_write(gpriv->base, TX_FQ_INT_STS, 0xFFFFFF);

	can_led_event(ndev, CAN_LED_EVENT_TX);
}

static irqreturn_t rcar_canxl_func_interrupt(int irq, void *dev_id)
{
	struct rcar_canxl_global *gpriv = dev_id;
	struct net_device *ndev;
	struct rcar_canxl_channel *priv;
	u32 sts, func_raw;

	/* Function interrupts still indicate a condition specific
	 * Tx/Rx FIFO interrupts is function interrupts.
	 */
	priv = gpriv->ch;
	ndev = priv->ndev;

	/* Function interrupts */
	func_raw = rcar_canxl_read(gpriv->base, FUNC_RAW);
	if (func_raw)
		rcar_canxl_func_raw(ndev);

	/* Handle Tx interrupt */
	if (likely(func_raw & PRT_TX_EVT))
		rcar_canxl_tx_done(ndev);

	/* Handle Rx interrupt */
	sts = rcar_canxl_read(gpriv->base, RX_FQ_INT_STS);
	if (likely(func_raw & PRT_RX_EVT) &&
	    (RX_FQ_INT_STS_RECEIVED(sts) != 0)) {
		if (napi_schedule_prep(&priv->napi)) {
			/* Disable Rx interrupt */
			rcar_canxl_clear_bit(gpriv->base, FUNC_ENA,
					     PRT_RX_EVT);
			__napi_schedule(&priv->napi);
		}
	}

	/* Clear all function interrupts */
	rcar_canxl_write(gpriv->base, FUNC_CLR, func_raw);

	return IRQ_HANDLED;
}

static void rcar_canxl_state_change(struct net_device *ndev,
				    u16 txerr, u16 rxerr)
{
	struct rcar_canxl_channel *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	enum can_state rx_state, tx_state, state = priv->can.state;
	struct can_frame *cf;
	struct sk_buff *skb;

	/* Handle transition from error to normal states */
	if (txerr < 96 && rxerr < 96)
		state = CAN_STATE_ERROR_ACTIVE;
	else if (txerr < 128 && rxerr < 128)
		state = CAN_STATE_ERROR_WARNING;

	if (state != priv->can.state) {
		netdev_dbg(ndev, "state: new %d, old %d: txerr %u, rxerr %u\n",
			   state, priv->can.state, txerr, rxerr);
		skb = alloc_can_err_skb(ndev, &cf);
		if (!skb) {
			stats->rx_dropped++;
			return;
		}
		tx_state = txerr >= rxerr ? state : 0;
		rx_state = txerr <= rxerr ? state : 0;

		can_change_state(ndev, cf, tx_state, rx_state);
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	}
}

static irqreturn_t rcar_canxl_err_interrupt(int irq, void *dev_id)
{
	struct rcar_canxl_global *gpriv = dev_id;
	struct net_device *ndev;
	struct rcar_canxl_channel *priv;
	u32 val, err_raw;
	u16 txerr, rxerr;

	/* Common FIFO resource */
	priv = gpriv->ch;
	ndev = priv->ndev;

	/* Error interrupts */
	err_raw = rcar_canxl_read(gpriv->base, ERR_RAW);
	val = rcar_canxl_read(gpriv->base, STAT);
	txerr = STAT_TEC(val);
	rxerr = STAT_REC(val);
	if (err_raw)
		rcar_canxl_error_raw(ndev, err_raw, txerr, rxerr);

	/* Handle state change to lower states */
	if (unlikely(priv->can.state != CAN_STATE_ERROR_ACTIVE &&
		     priv->can.state != CAN_STATE_BUS_OFF))
		rcar_canxl_state_change(ndev, txerr, rxerr);

	return IRQ_HANDLED;
}

static void rcar_canxl_set_bittiming(struct net_device *dev)
{
	struct rcar_canxl_channel *priv = netdev_priv(dev);
	const struct can_bittiming *bt = &priv->can.bittiming;
	const struct can_bittiming *xbt = &priv->can.data_bittiming;
	u16 brp, sjw, tseg1, tseg2;
	u32 cfg;

	/* Nominal bit timing settings */
	brp = bt->brp - 1;
	sjw = bt->sjw - 1;
	tseg1 = bt->prop_seg + bt->phase_seg1 - 1;
	tseg2 = bt->phase_seg2 - 1;

	cfg = (NBTP_NTSEG1(tseg1) |
	       NBTP_BRP(brp) |
	       NBTP_NSJW(sjw) |
	       NBTP_NTSEG2(tseg2));
	rcar_canxl_write(priv->base, NBTP, cfg);
	netdev_dbg(priv->ndev, "nrate: brp %u, sjw %u, tseg1 %u, tseg2 %u\n",
		   brp, sjw, tseg1, tseg2);

	/* Data bit timing settings */
	sjw = xbt->sjw - 1;
	tseg1 = xbt->prop_seg + xbt->phase_seg1 - 1;
	tseg2 = xbt->phase_seg2 - 1;

	cfg = (XBTP_XTSEG1(tseg1) |
	       XBTP_XSJW(sjw) |
	       XBTP_XTSEG2(tseg2));
	rcar_canxl_write(priv->base, XBTP, cfg);
	netdev_dbg(priv->ndev, "xrate: sjw %u, tseg1 %u, tseg2 %u\n",
		   sjw, tseg1, tseg2);
}

static int rcar_canxl_start(struct net_device *ndev)
{
	struct rcar_canxl_channel *priv = netdev_priv(ndev);
	int err = -EOPNOTSUPP;
	u32 sts;
	u16 cfg;
	struct rcar_canxl_global *gpriv = priv->gpriv;
	struct platform_device *pdev = gpriv->pdev;

	rcar_canxl_set_bittiming(ndev);

	/* Configure common interrupts */
	rcar_canxl_enable_interrupts(gpriv);

	/* Enable RX FIFO Queue 0 */
	cfg = RX_FQ_CTRL2_ENABLE(QUEUE(0));
	rcar_canxl_write(gpriv->base, RX_FQ_CTRL(2), cfg);

	/* Enable TX FIFO Queue 0 */
	cfg = TX_FQ_CTRL2_ENABLE(QUEUE(0));
	rcar_canxl_write(gpriv->base, TX_FQ_CTRL(2), cfg);

	/* Enable all slots in TX Priority Queue */
	rcar_canxl_write(gpriv->base, TX_PQ_CTRL(2),
			 CANXL_TX_PQ_SLOT_ENABLE);

	/* Start the Message Handler */
	rcar_canxl_write(gpriv->base, MH_CTRL, MH_CTRL_START);

	/* Start the RX FIFO Queue */
	rcar_canxl_write(gpriv->base, RX_FQ_CTRL(0),
			 RX_FQ_CTRL0_START(QUEUE(0)));
	err = readl_poll_timeout(gpriv->base + RX_FQ_STS(0), sts,
				 (sts & RX_FQ_STS0_BUSY(QUEUE(0))), 2, 500000);
	if (err) {
		dev_err(&pdev->dev, "Start RX FIFO failed\n");
		goto fail_mode_change;
	}

	/* Start CAN protocol operation */
	rcar_canxl_write(gpriv->base, CTRL, CTRL_STRT);

	/* Check if the ENABLE signal is set high by the PRT */
	err = readl_poll_timeout(gpriv->base + MH_STS, sts,
				 (sts & MH_STS_ENABLE), 2, 500000);
	if (err) {
		dev_err(&pdev->dev, "Start Message Handler failed\n");
		goto fail_mode_change;
	}

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	return 0;

fail_mode_change:
	rcar_canxl_disable_interrupts(gpriv);
	return err;
}

static int rcar_canxl_open(struct net_device *ndev)
{
	struct rcar_canxl_channel *priv = netdev_priv(ndev);
	struct rcar_canxl_global *gpriv = priv->gpriv;
	int err;

	/* Clock is already enabled in probe */
	err = clk_prepare_enable(gpriv->can_clk);
	if (err) {
		netdev_err(ndev, "failed to enable CAN clock, error %d\n", err);
		goto out_clock;
	}

	err = open_candev(ndev);
	if (err) {
		netdev_err(ndev, "open_candev() failed, error %d\n", err);
		goto out_can_clock;
	}

	napi_enable(&priv->napi);
	err = rcar_canxl_start(ndev);
	if (err)
		goto out_close;
	netif_start_queue(ndev);
	can_led_event(ndev, CAN_LED_EVENT_OPEN);
	return 0;
out_close:
	napi_disable(&priv->napi);
	close_candev(ndev);
out_can_clock:
	clk_disable_unprepare(gpriv->can_clk);
out_clock:
	return err;
}

static void rcar_canxl_stop(struct net_device *ndev)
{
	struct rcar_canxl_channel *priv = netdev_priv(ndev);
	struct rcar_canxl_global *gpriv = priv->gpriv;

	rcar_canxl_disable_interrupts(gpriv);

	/* Stop CAN protocol operation */
	rcar_canxl_write(gpriv->base, LOCK, LOCK_ULK(0x1234)); /* Write 0x1234 to LOCK.ULK */
	rcar_canxl_write(gpriv->base, LOCK, LOCK_ULK(0x4321)); /* Write 0x4321 to LOCK.ULK */
	rcar_canxl_write(gpriv->base, CTRL, (CTRL_STOP | CTRL_IMMD));

	/* Abort all TX Priority Queues */
	rcar_canxl_abort_tx_prior_queue(gpriv);

	/* Abort all RX FIFO Queues */
	rcar_canxl_abort_rx_fifo_queue(gpriv, 0xFF);

	/* Abort all TX FIFO Queues */
	rcar_canxl_abort_tx_fifo_queue(gpriv, 0xFF);

	rcar_canxl_write(gpriv->base, MH_CTRL, 0);

	/* Set the state as STOPPED */
	priv->can.state = CAN_STATE_STOPPED;
}

static int rcar_canxl_close(struct net_device *ndev)
{
	struct rcar_canxl_channel *priv = netdev_priv(ndev);
	struct rcar_canxl_global *gpriv = priv->gpriv;

	netif_stop_queue(ndev);
	rcar_canxl_stop(ndev);
	napi_disable(&priv->napi);
	clk_disable_unprepare(gpriv->can_clk);
	close_candev(ndev);
	can_led_event(ndev, CAN_LED_EVENT_STOP);
	return 0;
}

static netdev_tx_t rcar_canxl_start_xmit(struct sk_buff *skb,
					 struct net_device *ndev)
{
	struct rcar_canxl_channel *priv = netdev_priv(ndev);
	struct canxl_frame *cxl = (struct canxl_frame *)skb->data;
	struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	u16 id, sdt, dlc, rc, xtd, xlf = 1, sec = 0, brs = 0, esi = 0, fdf = 0;
	u32 af, pay_load_size, target_desc_index;
	u32 ele0, ele1, ele4_t0, ele5_t1, ele6_td0t2, ele7_txap, cfg;
	int ret;
	unsigned long flags;
	struct rcar_canxl_global *gpriv = priv->gpriv;

	/* Check vacancy place for descriptor at Queue 0 */
	ret = rcar_canxl_check_queue(gpriv, 0, &target_desc_index);
	if (ret)
		netdev_dbg(ndev, "Tx FIFO Queue is full\n");

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	if (gpriv->xlmode) {
		id = cxl->prio & CANXL_PRIO_MASK;
		sdt = cxl->sdt;
		dlc = cxl->len;
		af = cxl->af;

		if (can_is_canxl_skb(skb)) {
			/* CAN XL frame format */
			if (cxl->flags & CANXL_XLF)
				xlf = 1;

			if (cxl->flags & CANXL_SEC)
				sec = 1;
		}
	} else {
		if (can_is_canfd_skb(skb)) {
			/* CAN FD frame format */
			if (cfd->flags & CANFD_BRS)
				brs = 1;

			if (cfd->flags & CANFD_ESI)
				esi = 1;

			if (cfd->flags & CANFD_FDF)
				fdf = 1;
		}

		if (cfd->can_id & CAN_EFF_FLAG) {
			id = cfd->can_id & CAN_EFF_MASK;
			xtd = 1;
		} else {
			id = cfd->can_id & CAN_SFF_MASK;
			xtd = 0;
		}
		dlc = can_len2dlc(cfd->len);
	}

	if (!gpriv->xlmode)
		if (dlc)
			pay_load_size = ((can_dlc2len(dlc) - 1) / 4) + 1;
		else
			pay_load_size = 0;
	else
		pay_load_size = ((dlc - 1) / 4) + 1;

	rc = target_desc_index % 32;
	ele0 = (CANXL_DMA1_FIXED_FQ | CANXL_BIT_VALID(0x01) |
		CANXL_BIT_CRC(0x00) | CANXL_BIT_FQN(0) |
		CANXL_BIT_RC(rc) | CANXL_BIT_IRQ(0x1));

	ele1 = (CANXL_DMA2_FIXED_FQ | CANXL_BIT_SIZE(pay_load_size) |
		CANXL_BIT_IN(gpriv->channel));
	if (gpriv->xlmode || (!gpriv->xlmode && can_dlc2len(dlc) > 4))
		ele1 |= CANXL_BIT_PLSRC(1);
	else if (!gpriv->xlmode && can_dlc2len(dlc) <= 4)
		ele1 |= CANXL_BIT_PLSRC(0);

	if (gpriv->xlmode) {
		ele4_t0 = (CANXL_T0_FIXED | CANXL_BIT_XLF(xlf) | CANXL_BIT_PRID(id) |
			   CANXL_BIT_SEC(sec) | CANXL_BIT_SDT(sdt));
		ele5_t1 = (CANXL_T1_FIXED | CANXL_BIT_DLCXL((dlc - 1)));
		ele6_td0t2 = af;
	} else {
		if (xtd == 1)
			ele4_t0 = (CANFD_T0_FIXED | CANFD_BIT_XTD(xtd) |
				   CANFD_BIT_EXTID(id));
		else
			ele4_t0 = (CANFD_T0_FIXED | CANFD_BIT_XTD(xtd) |
				   CANFD_BIT_BAID(id));
		ele5_t1 = (CANFD_T1_FIXED | CANFD_BIT_BRS(brs) |
			   CANFD_BIT_ESI(esi) | CANFD_BIT_DLC(dlc));
		ele6_td0t2 = TX_FQ_STADD(gpriv->phys_sys_base, 0)
			     + TXElement6T2TD0(target_desc_index);
	}
	/* Size is 50 byte data container for each descriptor */
	ele7_txap = TX_FQ_DC_STADD(gpriv->phys_sys_base, 0) +
		    (target_desc_index * 50);

	rcar_canxl_write_desc(TX_FQ_STADD(gpriv->phys_sys_base, 0),
			      TXElement0(target_desc_index), ele0);
	rcar_canxl_write_desc(TX_FQ_STADD(gpriv->phys_sys_base, 0),
			      TXElement1(target_desc_index), ele1);
	rcar_canxl_write_desc(TX_FQ_STADD(gpriv->phys_sys_base, 0),
			      TXElement2TS0(target_desc_index), 0);
	rcar_canxl_write_desc(TX_FQ_STADD(gpriv->phys_sys_base, 0),
			      TXElement3TS1(target_desc_index), 0);
	rcar_canxl_write_desc(TX_FQ_STADD(gpriv->phys_sys_base, 0),
			      TXElement4T0(target_desc_index), ele4_t0);
	rcar_canxl_write_desc(TX_FQ_STADD(gpriv->phys_sys_base, 0),
			      TXElement5T1(target_desc_index), ele5_t1);

	if (gpriv->xlmode)
		rcar_canxl_write_desc(TX_FQ_STADD(gpriv->phys_sys_base, 0),
				      TXElement6T2TD0(target_desc_index), ele6_td0t2);
	else
		rcar_canfd_put_first_payload(cfd, ele6_td0t2);

	rcar_canxl_write_desc(TX_FQ_STADD(gpriv->phys_sys_base, 0),
			      TXElement7TX_APTD1(target_desc_index), ele7_txap);

	/* Put data into TX container */
	if (gpriv->xlmode) {
		rcar_canxl_put_data(cxl, ele7_txap);
		priv->tx_len[priv->tx_head % RCANXL_FIFO_DEPTH] = cxl->len;
	} else {
		rcar_canfd_put_data(cfd, ele7_txap);
		priv->tx_len[priv->tx_head % RCANXL_FIFO_DEPTH] = cfd->len;
	}

	can_put_echo_skb(skb, ndev, priv->tx_head % RCANXL_FIFO_DEPTH);

	spin_lock_irqsave(&priv->tx_lock, flags);
	priv->tx_head++;

	/* Stop the queue if we've filled all FIFO entries */
	if (priv->tx_head - priv->tx_tail >= RCANXL_FIFO_DEPTH)
		netif_stop_queue(ndev);

	/* Start TX Queue */
	cfg = TX_FQ_CTRL2_ENABLE(QUEUE(0));
	rcar_canxl_write(gpriv->base, TX_FQ_CTRL(2), cfg);

	cfg = TX_FQ_CTRL0_START(QUEUE(0));
	rcar_canxl_write(gpriv->base, TX_FQ_CTRL(0), cfg);

	spin_unlock_irqrestore(&priv->tx_lock, flags);

	return NETDEV_TX_OK;
}

static void rcar_canxl_rx_data(struct rcar_canxl_channel *priv,
			       u32 start_desc, u32 desc)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct canxl_frame *cxl;
	struct sk_buff *skb;
	u32 id, vcid, sdt, sec, af, dlc;
	u32 dc_addr, data_addr, r0, r1, r2;

	/* Get base address of RX data container */
	dc_addr = rcar_canxl_read_desc(start_desc, RXElement1(desc));
	r0 = rcar_canxl_read_desc(dc_addr, 0);
	r1 = rcar_canxl_read_desc(dc_addr, 0x4);
	r2 = rcar_canxl_read_desc(dc_addr, 0x8);

	/* Get address of stored data */
	data_addr = dc_addr + 0xc;

	id = CANXL_RX_BIT_PRIO(r0) & 0x7FF;
	vcid = CANXL_RX_BIT_VCID(r0) & 0xFF;
	sdt = CANXL_RX_BIT_SDT(r0) & 0xFF;
	sec = CANXL_RX_BIT_SEC(r0) & 0x1;
	af = r2;
	dlc = (CANXL_RX_BIT_DLCXL(r1) & 0x7FF) + 1;

	skb = alloc_canxl_skb(priv->ndev, &cxl, dlc);
	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	cxl->prio = id;
	cxl->sdt = sdt;
	cxl->len = dlc;
	cxl->af = af;
	rcar_canxl_get_data(cxl, data_addr);

	can_led_event(priv->ndev, CAN_LED_EVENT_RX);

	stats->rx_bytes += cxl->len;
	stats->rx_packets++;
	netif_receive_skb(skb);
}

static void rcar_canfd_rx_data(struct rcar_canxl_channel *priv,
			       u32 start_desc, u32 desc)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct canfd_frame *cfd;
	struct sk_buff *skb;
	u32 id, brs, esi, dlc, xtd;
	u32 dc_addr, data_addr, r0, r1;

	/* Get base address of RX data container */
	dc_addr = rcar_canxl_read_desc(start_desc, RXElement1(desc));
	r0 = rcar_canxl_read_desc(dc_addr, 0);
	r1 = rcar_canxl_read_desc(dc_addr, 0x4);

	/* Get address of stored data */
	data_addr = dc_addr + 0x8;

	xtd = CANFD_RX_BIT_XTD(r0) & 0x1;
	if (xtd == 1)
		id = CANFD_RX_BIT_EXTID(r0) & 0x3FFFF;
	else
		id = CANFD_RX_BIT_BAID(r0) & 0x7FF;
	brs = CANFD_RX_BIT_BRS(r1) & 0x1;
	esi = CANFD_RX_BIT_ESI(r1) & 0x1;
	dlc = CANFD_RX_BIT_DLC(r1) & 0xF;

	skb = alloc_canfd_skb(priv->ndev, &cfd);
	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	cfd->can_id = id;
	cfd->len = can_dlc2len(dlc);
	rcar_canfd_get_data(cfd, data_addr);

	can_led_event(priv->ndev, CAN_LED_EVENT_RX);

	stats->rx_bytes += cfd->len;
	stats->rx_packets++;
	netif_receive_skb(skb);
}

static void rcar_canxl_rx_pkt(struct rcar_canxl_channel *priv)
{
	u32 start_desc, current_desc, check_desc, max_desc;
	struct rcar_canxl_global *gpriv = priv->gpriv;

	/* Get start address of Queue 0 */
	start_desc = rcar_canxl_read(gpriv->base, RX_FQ_START_ADD(0));

	/* Get current address pointer of Queue 0 */
	current_desc = rcar_canxl_read(gpriv->base, RX_FQ_ADD_PT(0));

	/* Get the maximum number of descriptors in the RX FIFO Queue 0 */
	max_desc = RX_FQ_SIZE_MAX_DESC(rcar_canxl_read(gpriv->base,
				       RX_FQ_SIZE(0)) & 0x3FF);

	check_desc = current_desc;
	while (check_desc != (current_desc + 0x10)) {
		u32 ele0, desc;

		desc = ((check_desc - start_desc) / 0x10);
		ele0 = rcar_canxl_read_desc(start_desc, RXElement0(desc));
		if (ele0 & CANXL_RX_BIT_VALID(0x1)) {
			if (gpriv->xlmode)
				rcar_canxl_rx_data(priv, start_desc, desc);
			else
				rcar_canfd_rx_data(priv, start_desc, desc);
			ele0 &= (CANXL_RX_BIT_VALID(0));
			ele0 &= ~(0xF);
			rcar_canxl_write_desc(start_desc, RXElement0(desc), ele0);
		}

		if (check_desc != start_desc)
			check_desc -= 0x10;
		else
			check_desc = start_desc + (max_desc * 0x10);
	}
}

static int rcar_canxl_rx_poll(struct napi_struct *napi, int quota)
{
	struct rcar_canxl_channel *priv =
		container_of(napi, struct rcar_canxl_channel, napi);
	int num_pkts;
	u32 sts;

	for (num_pkts = 0; num_pkts < quota; num_pkts++) {
		/* No RX message received in the RX FIFO Queue */
		sts = rcar_canxl_read(priv->base, RX_FQ_INT_STS);
		if (!(sts & 0xFF))
			break;

		rcar_canxl_rx_pkt(priv);

		/* Clear RX FIFO Queue Interrupt Status */
		rcar_canxl_write(priv->base, RX_FQ_INT_STS, 0xFFFFFF);
	}

	/* All packets processed */
	if (num_pkts < quota) {
		if (napi_complete_done(napi, num_pkts)) {
			/* Enable Rx interrupt */
			rcar_canxl_set_bit(priv->base, FUNC_ENA,
					   PRT_RX_EVT);
		}
	}
	return num_pkts;
}

static int rcar_canxl_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int err;

	switch (mode) {
	case CAN_MODE_START:
		err = rcar_canxl_start(ndev);
		if (err)
			return err;
		netif_wake_queue(ndev);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int rcar_canxl_get_berr_counter(const struct net_device *dev,
				       struct can_berr_counter *bec)
{
	struct rcar_canxl_channel *priv = netdev_priv(dev);
	u32 val;

	/* Clock is already enabled in probe */
	val = rcar_canxl_read(priv->base, STAT);
	bec->txerr = STAT_TEC(val);
	bec->rxerr = STAT_REC(val);
	return 0;
}

static const struct net_device_ops rcar_canxl_netdev_ops = {
	.ndo_open = rcar_canxl_open,
	.ndo_stop = rcar_canxl_close,
	.ndo_start_xmit = rcar_canxl_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int rcar_canxl_channel_probe(struct rcar_canxl_global *gpriv,
				    u32 fcan_freq)
{
	struct platform_device *pdev = gpriv->pdev;
	struct rcar_canxl_channel *priv;
	struct net_device *ndev;
	int err = -ENODEV;

	ndev = alloc_candev(sizeof(*priv), RCANXL_FIFO_DEPTH);
	if (!ndev) {
		dev_err(&pdev->dev, "alloc_candev() failed\n");
		err = -ENOMEM;
		goto fail;
	}
	priv = netdev_priv(ndev);

	ndev->netdev_ops = &rcar_canxl_netdev_ops;
	ndev->flags |= IFF_ECHO;
	priv->ndev = ndev;
	priv->base = gpriv->base;
	priv->can.clock.freq = fcan_freq;
	dev_info(&pdev->dev, "can_clk rate is %u\n", priv->can.clock.freq);

	priv->can.bittiming_const = &rcar_canxl_nom_bittiming_const;
	priv->can.data_bittiming_const = &rcar_canxl_data_bittiming_const;

	/* Controller starts in CAN FD only mode */
	can_set_static_ctrlmode(ndev, CAN_CTRLMODE_FD);
	priv->can.ctrlmode_supported = CAN_CTRLMODE_BERR_REPORTING;

	priv->can.do_set_mode = rcar_canxl_do_set_mode;
	priv->can.do_get_berr_counter = rcar_canxl_get_berr_counter;
	priv->gpriv = gpriv;
	SET_NETDEV_DEV(ndev, &pdev->dev);

	netif_napi_add(ndev, &priv->napi, rcar_canxl_rx_poll,
		       RCANXL_NAPI_WEIGHT);
	spin_lock_init(&priv->tx_lock);
	devm_can_led_init(ndev);
	gpriv->ch = priv;
	err = register_candev(ndev);
	if (err) {
		dev_err(&pdev->dev,
			"register_candev() failed, error %d\n", err);
		goto fail_candev;
	}
	dev_info(&pdev->dev, "device registered (channel %d)\n", gpriv->channel);
	return 0;

fail_candev:
	netif_napi_del(&priv->napi);
	free_candev(ndev);
fail:
	return err;
}

static void rcar_canxl_channel_remove(struct rcar_canxl_global *gpriv)
{
	struct rcar_canxl_channel *priv = gpriv->ch;

	if (priv) {
		unregister_candev(priv->ndev);
		netif_napi_del(&priv->napi);
		free_candev(priv->ndev);
	}
}

static int rcar_canxl_probe(struct platform_device *pdev)
{
	void __iomem *addr;
	u32 ch, fcan_freq;
	struct rcar_canxl_global *gpriv;
	int err, func_irq, err_irq;
	bool xlmode = true;	/* CAN XL normal mode - default */
	const struct rcar_canxl_of_data *of_data;

	if (of_property_read_bool(pdev->dev.of_node, "channel0"))
		ch = 0;
	else
		ch = 1;

	if (of_property_read_bool(pdev->dev.of_node, "renesas,can-fd-frame"))
		xlmode = false;

	of_data = of_device_get_match_data(&pdev->dev);
	if (!of_data)
		return -EINVAL;

	func_irq = platform_get_irq(pdev, 0);
	if (func_irq < 0) {
		err = func_irq;
		goto fail_dev;
	}

	err_irq = platform_get_irq(pdev, 1);
	if (err_irq < 0) {
		err = err_irq;
		goto fail_dev;
	}

	/* Global controller context */
	gpriv = devm_kzalloc(&pdev->dev, sizeof(*gpriv), GFP_KERNEL);
	if (!gpriv) {
		err = -ENOMEM;
		goto fail_dev;
	}
	gpriv->pdev = pdev;
	gpriv->xlmode = xlmode;
	gpriv->chip_id = of_data->chip_id;
	gpriv->channel = ch;

	/* Peripheral clock */
	gpriv->clkp = devm_clk_get(&pdev->dev, "fck");
	if (IS_ERR(gpriv->clkp)) {
		err = PTR_ERR(gpriv->clkp);
		dev_err(&pdev->dev, "cannot get peripheral clock, error %d\n",
			err);
		goto fail_dev;
	}

	/* fCAN clock: Pick External clock. If not available fallback to
	 * CANXL clock
	 */
	gpriv->can_clk = devm_clk_get(&pdev->dev, "can_clk");
	if (IS_ERR(gpriv->can_clk) || (clk_get_rate(gpriv->can_clk) == 0)) {
		gpriv->can_clk = devm_clk_get(&pdev->dev, "canxl");
		if (IS_ERR(gpriv->can_clk)) {
			err = PTR_ERR(gpriv->can_clk);
			dev_err(&pdev->dev,
				"cannot get canxl clock, error %d\n", err);
			goto fail_dev;
		}
		gpriv->fcan = RCANXL_CANXLCLK;

	} else {
		gpriv->fcan = RCANXL_EXTCLK;
	}
	fcan_freq = clk_get_rate(gpriv->can_clk);

	addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(addr)) {
		err = PTR_ERR(addr);
		goto fail_dev;
	}
	gpriv->base = addr;

	/* Allocate the system memory */
	gpriv->sys_base = kmalloc(sizeof(u8) * SYS_MEM_SIZE, GFP_KERNEL);
	if (!gpriv->sys_base)
		return -ENOMEM;

	gpriv->phys_sys_base = virt_to_phys(gpriv->sys_base);

	/* Request for function and error IRQ */
	err = devm_request_irq(&pdev->dev, func_irq,
			       rcar_canxl_func_interrupt, 0,
			       "canxl.func", gpriv);
	if (err) {
		dev_err(&pdev->dev, "devm_request_irq(%d) failed, error %d\n",
			func_irq, err);
		goto fail_dev;
	}

	err = devm_request_irq(&pdev->dev, err_irq,
			       rcar_canxl_err_interrupt, 0,
			       "canxl.err", gpriv);
	if (err) {
		dev_err(&pdev->dev, "devm_request_irq(%d) failed, error %d\n",
			err_irq, err);
		goto fail_dev;
	}

	/* Enable peripheral clock for register access */
	err = clk_prepare_enable(gpriv->clkp);
	if (err) {
		dev_err(&pdev->dev,
			"failed to enable peripheral clock, error %d\n", err);
		goto fail_dev;
	}

	err = rcar_canxl_local_ram_init(gpriv);
	if (err) {
		dev_err(&pdev->dev, "Local RAM initialization failed\n");
		goto fail_clk;
	}

	/* Enable clock and check XCAN clock is valid or not */
	rcar_canxl_enable_clock(gpriv);

	/* Reset protocol controller and set operation mode */
	rcar_canxl_reset_controller(gpriv);

	/* Configure MH global registers */
	rcar_canxl_configure_mh_global(gpriv, ch);

	/* Configure RX Filter */
	rcar_canxl_configure_rx_filter(gpriv);

	/* Configure TX Filter */
	rcar_canxl_configure_tx_filter(gpriv);

	/* Configure RX FIFO Queue */
	rcar_canxl_configure_rx_fifo_queue(gpriv);

	/* Configure TX FIFO Queue */
	rcar_canxl_configure_tx_fifo_queue(gpriv);

	/* Configure TX Priority Queue */
	rcar_canxl_configure_tx_prior_queue(gpriv);

	/* Initialization of descriptors */
	rcar_canxl_descriptor_init(gpriv);

	err = rcar_canxl_channel_probe(gpriv, fcan_freq);
	if (err)
		goto fail_channel;

	platform_set_drvdata(pdev, gpriv);
	dev_info(&pdev->dev, "Operational state (clk %d, mode %d)\n",
		 gpriv->fcan, gpriv->xlmode);
	return 0;

fail_channel:
	rcar_canxl_channel_remove(gpriv);
fail_clk:
	clk_disable_unprepare(gpriv->clkp);
fail_dev:
	return err;
}

static int rcar_canxl_remove(struct platform_device *pdev)
{
	struct rcar_canxl_global *gpriv = platform_get_drvdata(pdev);

	rcar_canxl_reset_controller(gpriv);
	rcar_canxl_disable_interrupts(gpriv);
	rcar_canxl_channel_remove(gpriv);
	clk_disable_unprepare(gpriv->clkp);
	kfree(gpriv->sys_base);
	return 0;
}

static int __maybe_unused rcar_canxl_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused rcar_canxl_resume(struct device *dev)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(rcar_canxl_pm_ops, rcar_canxl_suspend,
			 rcar_canxl_resume);

static const struct rcar_canxl_of_data of_rcanxl_x5h_compatible = {
	.chip_id = GEN5,
};

static const struct of_device_id rcar_canxl_of_table[] = {
	{
		.compatible = "renesas,r8a78000-canxl",
		.data = &of_rcanxl_x5h_compatible,
	},
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_canxl_of_table);

static struct platform_driver rcar_canxl_driver = {
	.driver = {
		.name = RCANXL_DRV_NAME,
		.of_match_table = of_match_ptr(rcar_canxl_of_table),
		.pm = &rcar_canxl_pm_ops,
	},
	.probe = rcar_canxl_probe,
	.remove = rcar_canxl_remove,
};

module_platform_driver(rcar_canxl_driver);

MODULE_AUTHOR("Duy Nguyen <duy.nguyen.rh@renesas.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CAN XL driver for Renesas R-Car SoC");
MODULE_ALIAS("platform:" RCANXL_DRV_NAME);
