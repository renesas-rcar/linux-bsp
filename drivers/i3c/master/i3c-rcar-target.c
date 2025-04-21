// iPDX-License-Identifier: GPL-2.0
/*
* Copyright 2023 NXP.
*
* Author: Frank Li <Frank.Li@nxp.com>
*/

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/i3c/target.h>
#include <linux/i3c/device.h>
#include <linux/i3c/target.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/reset.h>

#include "i3c-rcar.h"

#define PRTS			0x00
#define PRTS_PRTMD		BIT(0)

#define CECTL                   0x10
#define CECTL_CLKE              BIT(0)

#define BCTL			0x14
#define BCTL_INCBA		BIT(0)
#define BCTL_HJACKCTL		BIT(8)
#define BCTL_ABT		BIT(29)
#define BCTL_RSM		BIT(30)
#define BCTL_BUSE		BIT(31)

#define RSTCTL			0x20
#define RSTCTL_RI3CTST		BIT(0)
#define RSTCTL_CMDQRST		BIT(1)
#define RSTCTL_RSPQRST		BIT(2)
#define RSTCTL_TDBRST		BIT(3)
#define RSTCTL_RDBRST		BIT(4)
#define RSTCTL_IBIQRST		BIT(5)
#define RSTCTL_RSQRST		BIT(6)
#define RSTCTL_INTLRST		BIT(16)

#define INST			0x30
#define INST_INEF		BIT(10)

#define INSTE			0x34
#define INSTE_INEE		BIT(10)

#define INIE			0x38
#define INIE_INEIE		BIT(10)

#define TMOCTL			0x90

#define SVCTL			0x64
#define SVCTL_GCAE		BIT(0)
#define SVCTL_HSMCE		BIT(5)
#define SVCTL_DVIDE		BIT(6)
#define SVCTL_HOAE		BIT(15)
#define SVCTL_SVAEn(x)		(BIT(16) << (x))

#define REFCKCTL		0x70
#define REFCKCTL_IREFCKS(x)	(((x) & 0x7) << 0)

#define STDBR			0x74
#define STDBR_SBRLO(cond, x)	((((cond) ? (x)/2 : (x)) & 0xff) << 0)
#define STDBR_SBRHO(cond, x)	((((cond) ? (x)/2 : (x)) & 0xff) << 8)
#define STDBR_SBRLP(x)		(((x) & 0x3f) << 16)
#define STDBR_SBRHP(x)		(((x) & 0x3f) << 24)
#define STDBR_DSBRPO		BIT(31)

#define BFRECDT			0x7c
#define BFRECDT_FRECYC(x)	(((x) & 0x1ff) << 0)

#define BAVLCDT			0x80
#define BAVLCDT_AVLCYC(x)	(((x) & 0x1ff) << 0)

#define BIDLCDT			0x84
#define BIDLCDT_IDLCYC(x)	(((x) & 0x3ffff) << 0)

#define SVTDLG0			0xC0
#define SVTDLG0_STDLG(x)	(((x) & 0xffff) << 16)
#define NCMDQP			0x150 /* Normal Command Queue */
#define NCMDQP_CMD_ATTR(x)	(((x) & 0x7) << 0)
#define NCMDQP_IMMED_XFER	0x01
#define NCMDQP_ADDR_ASSGN	0x02
#define NCMDQP_TID(x)		(((x) & 0xf) << 3)
#define NCMDQP_CMD(x)		(((x) & 0xff) << 7)
#define NCMDQP_CP		BIT(15)
#define NCMDQP_HJ		BIT(15)
#define NCMDQP_DEV_INDEX(x)	(((x) & 0x1f) << 16)
#define NCMDQP_EXT_DEVICE	BIT(21)
#define NCMDQP_BYTE_CNT(x)	(((x) & 0x7) << 23)
#define NCMDQP_DEV_COUNT(x)	(((x) & 0xf) << 26)
#define NCMDQP_MODE(x)		(((x) & 0x7) << 26)
#define NCMDQP_RNW(x)		(((x) & 0x1) << 29)
#define NCMDQP_ROC		BIT(30)
#define NCMDQP_TOC		BIT(31)
#define NCMDQP_DATA_LENGTH(x)	(((x) & 0xffff) << 16)

#define NRSPQP			0x154 /* Normal Respone Queue */
#define NRSPQP_NO_ERROR			0
#define NRSPQP_ERROR_CRC		1
#define NRSPQP_ERROR_PARITY		2
#define NRSPQP_ERROR_FRAME		3
#define NRSPQP_ERROR_IBA_NACK		4
#define NRSPQP_ERROR_ADDRESS_NACK	5
#define NRSPQP_ERROR_OVER_UNDER_FLOW	6
#define NRSPQP_ERROR_TRANSF_ABORT	8
#define NRSPQP_ERROR_I2C_W_NACK_ERR	9
#define NRSPQP_ERR_STATUS(x)	(((x) & GENMASK(31, 28)) >> 28)
#define NRSPQP_TID(x)		(((x) & GENMASK(27, 24)) >> 24)
#define NRSPQP_DATA_LEN(x)	((x) & GENMASK(15, 0))

#define NTDTBP0			0x158 /* Normal Transfer Data Buffer */
#define NIBIQP			0x17c /* Normal IBI Queue */
#define NRSQP			0x180 /* Normal Receive Status Queue */
#define NRSQP_DATA_LEN(x)	((x) & GENMASK(15, 0))
#define NRSQP_CMD(x)		(((x) & GENMASK(23, 16)) >> 16)
#define NRSQP_SDR_R_W_TYPE	BIT(23)
#define NRSQP_ERR_STATUS(x)	(((x) & GENMASK(26, 24)) >> 24)
#define NRSQP_XFER_TYPE(x)	(((x) & GENMASK(28, 27)) >> 27)
#define NRSQP_DEV_INDEX(x)	(((x) & GENMASK(31, 29)) >> 29)

#define NQTHCTL			0x190
#define NQTHCTL_CMDQTH(x)	(((x) & 0x3) << 0)
#define NQTHCTL_RSPQTH(x)	(((x) & 0x3) << 8)
#define NQTHCTL_IBIDSSZ(x)	(((x) & 0xff) << 16)
#define NQTHCTL_IBIQTH(x)	(((x) & 0x7) << 24)

#define NTBTHCTL0		0x194
#define NTBTHCTL0_TXDBTH_VAL(x)	(((x) & GENMASK(2, 0)) >> 0)
#define NTBTHCTL0_RXDBTH_VAL(x)	(((x) & GENMASK(10, 8)) >> 8)
#define NTBTHCTL0_TXDBTH(x)	(((x) & 0x7) << 0)
#define NTBTHCTL0_RXDBTH(x)	(((x) & 0x7) << 8)
#define NTBTHCTL0_TXSTTH(x)	(((x) & 0x7) << 16)
#define NTBTHCTL0_RXSTTH(x)	(((x) & 0x7) << 24)

#define NRQTHCTL		0x1c0
#define NRQTHCTL_RSQTH		BIT(0)

#define BST			0x1d0
#define BST_STCNDDF		BIT(0)
#define BST_SPCNDDF		BIT(1)
#define BST_NACKDF		BIT(4)
#define BST_TENDF		BIT(8)
#define BST_ALF			BIT(16)
#define BST_TODF		BIT(20)
#define BST_WUCNDDF		BIT(24)

#define BSTE			0x1d4
#define BSTE_STCNDDE		BIT(0)
#define BSTE_SPCNDDE		BIT(1)
#define BSTE_NACKDE		BIT(4)
#define BSTE_TENDE		BIT(8)
#define BSTE_ALE		BIT(16)
#define BSTE_TODE		BIT(20)
#define BSTE_WUCNDDE		BIT(24)
#define BSTE_ALL_FLAG		(BSTE_STCNDDE | BSTE_SPCNDDE | \
				 BSTE_NACKDE | BSTE_TENDE |    \
				 BSTE_ALE | BSTE_TODE | BSTE_WUCNDDE)

#define BIE			0x1d8
#define BIE_STCNDDIE		BIT(0)
#define BIE_SPCNDDIE		BIT(1)
#define BIE_NACKDIE		BIT(4)
#define BIE_TENDIE		BIT(8)
#define BIE_ALIE		BIT(16)
#define BIE_TODIE		BIT(20)
#define BIE_WUCNDDIE		BIT(24)

#define NTST			0x1e0
#define NTST_TDBEF0		BIT(0)
#define NTST_RDBFF0		BIT(1)
#define NTST_IBIQEFF		BIT(2)
#define NTST_CMDQEF		BIT(3)
#define NTST_RSPQFF		BIT(4)
#define NTST_TABTF		BIT(5)
#define NTST_TEF		BIT(9)
#define NTST_RSQFF		BIT(20)

#define NTSTE			0x1e4
#define NTSTE_TDBEE0		BIT(0)
#define NTSTE_RDBFE0		BIT(1)
#define NTSTE_IBIQEFE		BIT(2)
#define NTSTE_CMDQEE		BIT(3)
#define NTSTE_RSPQFE		BIT(4)
#define NTSTE_TABTE		BIT(5)
#define NTSTE_TEE		BIT(9)
#define NTSTE_RSQFE		BIT(20)
#define NTSTE_ALL_FLAG		(NTSTE_TDBEE0 | NTSTE_RDBFE0 |  \
				 NTSTE_IBIQEFE | NTSTE_CMDQEE | \
				 NTSTE_RSPQFE | NTSTE_TABTE |   \
				 NTSTE_TEE | NTSTE_RSQFE)

#define NTIE			0x1e8
#define NTIE_TDBEIE0		BIT(0)
#define NTIE_RDBFIE0		BIT(1)
#define NTIE_IBIQEFIE		BIT(2)
#define NTIE_CMDQEIE		BIT(3)
#define NTIE_RSPQFIE		BIT(4)
#define NTIE_TABTIE		BIT(5)
#define NTIE_TEIE		BIT(9)
#define NTIE_RSQFIE		BIT(20)

#define BCST			0x210
#define BCST_BFREF		BIT(0)
#define BCST_BAVLF		BIT(1)
#define BCST_BIDLF		BIT(2)

#define SVST			0x214
#define SVST_GCAF		BIT(0)
#define SVST_HSMCF		BIT(5)
#define SVST_DVIDF		BIT(6)
#define SVST_HOAF		BIT(15)
#define SVST_SVAF0		BIT(16)
#define SVST_SVAF1		BIT(17)
#define SVST_SVAF2		BIT(18)

#define DATBAS(x)		(0x224 + 0x8 * (x))
#define DATBAS_DVSTAD(x)	(((x) & 0x7f) << 0)
#define DATBAS_DVIBIPL		BIT(12)
#define DATBAS_DVSIRRJ		BIT(13)
#define DATBAS_DVMRRJ		BIT(14)
#define DATBAS_DVIBITS		BIT(15)
#define DATBAS_DVDYAD(x)	(((x) & 0xff) << 16)
#define DATBAS_DVNACK(x)	(((x) & 0x3) << 29)
#define DATBAS_DVTYP		BIT(31)

#define SDATBAS(x)		(0x2b0 + 0x8 * x)
#define SDATBAS_SDSTAD(x)	(((x) & 0x3ff) << 0)
#define SDATBAS_SDADLS		BIT(10)
#define SDATBAS_SDIBIPL		BIT(12)
#define SDATBAS_SDDYAD(x)	(((x) & GENMASK(22, 16)) >> 16)

#define BCR_MAX_DATA_RATE(x)	(((x) & BIT(0)) >> 0)
#define BCR_IBI_REQ_CAP(x)	(((x) & BIT(1)) >> 1)
#define BCR_IBI_PL(x)		(((x) & BIT(2)) >> 2)
#define BCR_OFFLINE_CAP(x)	(((x) & BIT(3)) >> 3)
#define BCR_DEVICE_ROLE(x)	(((x) &	GENMASK(7, 6)) >> 6)

#define SVDCT			0x320
#define SVDCT_TDCR(x)		(((x) & 0xff) << 0)
#define SVDCT_TBCR(x)		(((x) & 0xff) << 8)
#define SVDCT_TBCR0		BIT(8)
#define SVDCT_TBCR1		BIT(9)
#define SVDCT_TBCR2		BIT(10)
#define SVDCT_TBCR3		BIT(11)
#define SVDCT_TBCR76(x)		(((x) & 0x3) << 14)

#define PID_EXTRA_ID(id)	(((id) & 0xfff) << 0)
#define PID_INSTANCE_ID(id)	(((id) & 0xf) << 12)
#define PID_PART_ID(id)		(((id) & 0xffff) << 0)
#define PID_VENDOR_ID(id)	(((id) & 0x7fff) << 17)

#define SDCTPIDL		0x324		/* Store Extra ID and Instance ID */
#define SDCTPIDH		0x328		/* Store Part ID and Vendor ID */

#define SVDVAD(x)		(0x330 + 0x08 * x)
#define SVDVAD_SVAD(x)		(((x) & GENMASK(25, 16)) >> 16)
#define SVDVAD_SADLG		BIT(27)
#define SVDVAD_SSTADV		BIT(30)
#define SVDVAD_SDYADV		BIT(31)

#define BITCNT			0x380
#define BITCNT_BCNT(x)		(((x) & 0x1f) << 0)

#define CSECMD			0x350
#define CSECMD_SVIRQE		BIT(0)
#define CSECMD_MSRQE		BIT(1)
#define CSECMD_HJEVE		BIT(3)

#define CEACTST			0x354
#define CEACTST_ACTST(x)	(((x) & 0xf) << 0)
#define ENTAS0			0x1	/* 1us: Latency-free operation */
#define ENTAS1			0x2	/* 100us */
#define ENTAS2			0x4	/* 2ms */
#define ENTAS3			0x8	/* 50ms: Lowest activity operation */

#define CMWLG			0x358
#define CMWLG_MWLG(x)		(((x) & 0xffff) << 0)

#define CMRLG			0x35C
#define CMRLG_MRLG(x)		(((x) & 0xffff) << 0)
#define CMRLG_IBIPSZ		(((x) & 0xff) << 16)

#define CETSTMD			0x360
#define CGDVST			0x364
#define CMDSPW			0x368
#define CMDSPR			0x36C
#define CMDSPT			0x370
#define CMDSPT_MRTTIM(x)	(((x) & 0xffffff) << 0)
#define CMDSPT_MRTE(x)		(((x) & 0x1) << 31)
#define CETSM			0x374

#define NQSTLV			0x394
#define NQSTLV_CMDQFLV(x)	(((x) & 0xff) << 0)
#define NQSTLV_RSPQLV(x)	(((x) & 0xff) << 8)
#define NQSTLV_IBIQLV(x)	(((x) & 0xff) << 16)
#define NQSTLV_IBISCNT(x)	(((x) & 0x1f) << 24)

#define NDBSTLV0		0x398
#define NDBSTLV0_TDBFLV(x)	(((x) & GENMASK(7, 0)) >> 0)
#define NDBSTLV0_RDBLV(x)	(((x) & GENMASK(15, 8)) >> 8)

#define NRSQSTLV		0x3c0
#define NRSQSTLV_RSQLV(x)	(((x) & GENMASK(7, 0)) >> 0)

/* Bus condition timing */
#define I3C_BUS_THIGH_MIXED_NS	40		/* 40ns */
#define I3C_BUS_FREE_TIME_NS	1300		/* 1.3us for Mixed Bus with I2C FM Device*/
#define I3C_BUS_AVAL_TIME_NS	1000		/* 1us */
#define I3C_BUS_IDEL_TIME_NS	200000		/* 200us */

#define XFER_TIMEOUT		(msecs_to_jiffies(1000))
#define NTDTBP0_DEPTH		32
#define RCAR_I3C_MAX_SLVS	3

struct rcar_i3c_target {
	struct device *dev;
	struct i3c_target_ctrl *base;
	enum i3c_internal_state internal_state;
	void __iomem *regs;
	int irq;
	u16 maxdevs;
	u32 free_pos;
	u8 addrs[RCAR_I3C_MAX_SLVS];
	struct list_head txq;
	spinlock_t txq_lock; /* protect tx queue */
	struct list_head rxq;
	spinlock_t rxq_lock; /* protect rx queue */
	struct list_head cq;
	spinlock_t cq_lock; /* protect complete queue */

	struct work_struct work;
	struct workqueue_struct *workqueue; /* workqueue to handle complete in i3c_request */
	struct i3c_request *complete;

	struct completion comp; /* completion on hotjoin success */
	struct i3c_target_ctrl_features features;
	struct clk *tclk;
	struct clk *pclk;
	struct clk *pclkrw;
};

struct i3c_irq_desc {
	int res_num;
	irq_handler_t isr;
	char *name;
};

static inline struct rcar_i3c_target *to_rcar_i3c_target(struct i3c_target_ctrl *ctrl)
{
	return dev_get_drvdata(&ctrl->dev);
}

static int rcar_i3c_target_set_config(struct i3c_target_ctrl *ctrl,
					 struct i3c_target_func *func)
{
	struct rcar_i3c_target *target;
	u32 val;
	int ret;

	target = to_rcar_i3c_target(ctrl);
	if (func->static_addr > 0x7F)
		return -EINVAL;

	/* Other roles than target role are not supported */
	if (BCR_DEVICE_ROLE(func->bcr)) {
		dev_info(target->dev, "unsupported device role");
		return -EINVAL;
	}

	/* Reset the I3C. */
	i3c_reg_write(target->regs, BCTL, 0);
	i3c_reg_set_bit(target->regs, RSTCTL, RSTCTL_RI3CTST);

	/* Wait for completion */
	ret = readl_relaxed_poll_timeout(target->regs + RSTCTL, val,
		!(val & RSTCTL_RI3CTST), 0, 1000);
	if (ret)
		return ret;
	/* Select I3C protocol mode */
	i3c_reg_write(target->regs, PRTS, 0);
	/* Set the SDATBAS0 */
	i3c_reg_write(target->regs, SDATBAS(0), SDATBAS_SDSTAD(func->static_addr) |
		      (BCR_IBI_PL(func->bcr) ? SDATBAS_SDIBIPL : ~SDATBAS_SDIBIPL));
	/* Set the slave address to valid */
	i3c_reg_write(target->regs, SVCTL, SVCTL_SVAEn(0));
	/* Write the BCR and DCR */
	i3c_reg_write(target->regs, SVDCT, SVDCT_TDCR(func->dcr) | SVDCT_TBCR(func->bcr));
	/* Write the PID */
	i3c_reg_write(target->regs, SDCTPIDL, PID_EXTRA_ID(func->ext_id) |
		      PID_INSTANCE_ID(func->instance_id));
	i3c_reg_write(target->regs, SDCTPIDH, PID_PART_ID(func->part_id) |
		      PID_VENDOR_ID(func->vendor_id));

	/* Write the maxlength of read and write */
	i3c_reg_write(target->regs, CMWLG, 0xFF);
	i3c_reg_write(target->regs, CMRLG, 0xFF);

	return 0;

}

static int rcar_i3c_target_enable(struct i3c_target_ctrl *ctrl)
{
	struct rcar_i3c_target *target = to_rcar_i3c_target(ctrl);
	unsigned long rate;
	u32 val;

	rate = clk_get_rate(target->tclk);
	if (!rate)
		return -EINVAL;

	/* Enable clock function */
	i3c_reg_set_bit(target->regs, CECTL, CECTL_CLKE);

	/* Configure the Normal IBI Data Segment Size and Threshold */
	i3c_reg_write(target->regs, NQTHCTL, NQTHCTL_IBIDSSZ(6) | NQTHCTL_IBIQTH(1));

	/* Enable transfer interrupt */
	i3c_reg_write(target->regs, BIE, 0);
	i3c_reg_write(target->regs, INIE, INIE_INEIE);
	i3c_reg_write(target->regs, NTIE, NTIE_RSQFIE |	NTIE_RSPQFIE);
	/* Enable Status logging */
	i3c_reg_write(target->regs, BSTE, BSTE_ALL_FLAG);

	/* Enable all interrupt flags */
	i3c_reg_write(target->regs, NTSTE, NTSTE_ALL_FLAG);

	/* Enable internal error status flag */
	i3c_reg_write(target->regs, INSTE, INSTE_INEE);

	/* Clear Status register */
	i3c_reg_write(target->regs, NTST, 0);
	i3c_reg_write(target->regs, INST, 0);
	i3c_reg_write(target->regs, BST, 0);

	/* Baudrate setting is not used */
	i3c_reg_write(target->regs, STDBR, 0x3f000000);

	/* Configure Normal Queue Threshold */
	i3c_reg_write(target->regs, NTBTHCTL0,  NTBTHCTL0_TXDBTH(0) | NTBTHCTL0_RXDBTH(0) |
		      NTBTHCTL0_TXSTTH(0) | NTBTHCTL0_RXSTTH(0));

	i3c_reg_write(target->regs, NRQTHCTL, 0);

	/* Bus condition timing */
	val = 0x5;
	i3c_reg_write(target->regs, BFRECDT, BFRECDT_FRECYC(val));
	i3c_reg_write(target->regs, BAVLCDT, BAVLCDT_AVLCYC(val));
	i3c_reg_write(target->regs, BIDLCDT, BIDLCDT_IDLCYC(val));

	/* Disable Timeout Detection */
	i3c_reg_write(target->regs, TMOCTL, 0);

	/* CCC settings */
	i3c_reg_write(target->regs, CSECMD, 0); /* Let the target enable */
	i3c_reg_write(target->regs, CEACTST, CEACTST_ACTST(ENTAS0));
	i3c_reg_write(target->regs, CMDSPW, 0);
	i3c_reg_write(target->regs, CMDSPR, 0);
	i3c_reg_write(target->regs, CMDSPT, CMDSPT_MRTTIM(0xf40000) | CMDSPT_MRTE(0));
	i3c_reg_write(target->regs, CETSM, 0);

	/* Enable I3C Bus. */
	i3c_reg_set_bit(target->regs, BCTL, BCTL_BUSE);

	return 0;
}

static int rcar_i3c_target_disable(struct i3c_target_ctrl *ctrl)
{
	struct rcar_i3c_target *target = to_rcar_i3c_target(ctrl);
	int ret;
	u32 val;

	i3c_reg_write(target->regs, BCTL, 0);
	i3c_reg_update_bit(target->regs, RSTCTL, RSTCTL_RI3CTST, RSTCTL_RI3CTST);

	/* Wait for reset completion  */
	ret = readl_relaxed_poll_timeout(target->regs + RSTCTL, val,
					 !(val & RSTCTL_RI3CTST), 0, 1000);
	if (!ret)
		return -ETIME;

	return 0;
}

static const struct i3c_target_ctrl_features
*rcar_i3c_target_get_features(struct i3c_target_ctrl *ctrl)
{
	struct rcar_i3c_target *target = to_rcar_i3c_target(ctrl);
	u32 val;

	val = i3c_reg_read(target->regs, NTBTHCTL0);
	target->features.tx_fifo_sz = NTDTBP0_DEPTH * 4;
	target->features.rx_fifo_sz = NTDTBP0_DEPTH * 4;

	return &target->features;
}

static void rcar_i3c_queue_complete(struct rcar_i3c_target *target, struct i3c_request *complete)
{
	unsigned long flags;

	spin_lock_irqsave(&target->cq_lock, flags);
	list_add_tail(&complete->list, &target->cq);
	spin_unlock_irqrestore(&target->cq_lock, flags);
	queue_work(target->workqueue, &target->work);
}

static void rcar_i3c_target_fill_txfifo(struct rcar_i3c_target *target)
{
	struct i3c_request *req, *complete = NULL;
	unsigned long flags;
	u32 data_word = 0;

	spin_lock_irqsave(&target->txq_lock, flags);
	req = list_first_entry_or_null(&target->txq, struct i3c_request, list);
	if (req) {
		while (NDBSTLV0_TDBFLV(i3c_reg_read(target->regs, NDBSTLV0)) <= NTDTBP0_DEPTH) {
			if (req->length - req->actual < 4) {
				data_word = 0;
				memcpy(&data_word, req->buf + req->actual, req->length & 3);
				writel(data_word, target->regs + NTDTBP0);
				req->actual += (req->length & 3);
			} else {
				data_word = *(u32 *) (req->buf + req->actual);
				writel(data_word, target->regs + NTDTBP0);
				req->actual += 4;
			}

			if (req->actual == req->length) {
				i3c_reg_write(target->regs, SVTDLG0, SVTDLG0_STDLG(req->length));
				i3c_reg_clear_bit(target->regs, NTIE, NTIE_TDBEIE0);

				list_del(&req->list);
				complete = req;
				rcar_i3c_queue_complete(target, complete);
				break;
			}
		}

		i3c_reg_clear_bit(target->regs, NTST, NTST_TDBEF0);
	}
	spin_unlock_irqrestore(&target->txq_lock, flags);
}

static int rcar_i3c_target_queue(struct i3c_request *req, gfp_t gfp)
{
	struct rcar_i3c_target *target = to_rcar_i3c_target(req->ctrl);
	struct list_head *q;
	spinlock_t *lk;
	unsigned long flags;

	if (req->tx) {
		q = &target->txq;
		lk = &target->txq_lock;
	} else {
		q = &target->rxq;
		lk = &target->rxq_lock;
	}

	spin_lock_irqsave(lk, flags);
	list_add_tail(&req->list, q);
	spin_unlock_irqrestore(lk, flags);

	if (req->tx) {
		rcar_i3c_target_fill_txfifo(target);
		i3c_reg_set_bit(target->regs, NTIE, NTIE_TDBEIE0);
	} else {
		i3c_reg_set_bit(target->regs, NTIE, NTIE_RDBFIE0);
	}

	return 0;
}

static int rcar_i3c_target_dequeue(struct i3c_request *req)
{
	struct rcar_i3c_target *target = to_rcar_i3c_target(req->ctrl);
	unsigned long flags;
	spinlock_t *lk;

	if (req->tx)
		lk = &target->txq_lock;
	else
		lk = &target->rxq_lock;

	spin_lock_irqsave(lk, flags);
	list_del(&req->list);
	spin_unlock_irqrestore(lk, flags);

	return 0;
	}

	static void rcar_i3c_target_complete(struct work_struct *work)
	{
	struct rcar_i3c_target *target = container_of(work, struct rcar_i3c_target, work);
	struct i3c_request *req;
	unsigned long flags;

	spin_lock_irqsave(&target->cq_lock, flags);
	while (!list_empty(&target->cq)) {
		req = list_first_entry(&target->cq, struct i3c_request, list);
		list_del(&req->list);
		spin_unlock_irqrestore(&target->cq_lock, flags);
		req->complete(req);

		spin_lock_irqsave(&target->cq_lock, flags);
	}
	spin_unlock_irqrestore(&target->cq_lock, flags);
}

static void rcar_i3c_target_cancel_all_reqs(struct i3c_target_ctrl *ctrl, bool tx)
{
	struct rcar_i3c_target *target;
	struct i3c_request *req;
	struct list_head *q;
	unsigned long flags;
	spinlock_t *lk;

	target = to_rcar_i3c_target(ctrl);

	if (tx) {
		q = &target->txq;
		lk = &target->txq_lock;
	} else {
		q = &target->rxq;
		lk = &target->rxq_lock;
	}

	spin_lock_irqsave(lk, flags);
	while (!list_empty(q)) {
		req = list_first_entry(q, struct i3c_request, list);
		list_del(&req->list);
		spin_unlock_irqrestore(lk, flags);

		req->status = I3C_REQUEST_CANCEL;
		req->complete(req);
		spin_lock_irqsave(lk, flags);
	}
	spin_unlock_irqrestore(lk, flags);
}

static void rcar_i3c_target_fifo_flush(struct i3c_target_ctrl *ctrl, bool tx)
{
	struct rcar_i3c_target *target;
	int ret;
	u32 val;

	target = to_rcar_i3c_target(ctrl);
	if (tx) {
		/* Clear Tx FIFO */
		i3c_reg_set_bit(target->regs, RSTCTL, RSTCTL_TDBRST);
		/* Wait for reset completion  */
		ret = readl_relaxed_poll_timeout(target->regs + RSTCTL, val,
			!(val & RSTCTL_TDBRST), 0, 1000);
	} else {
		/* Clear Rx FIFO */
		i3c_reg_set_bit(target->regs, RSTCTL, RSTCTL_RDBRST);
		/* Wait for reset completion  */
		ret = readl_relaxed_poll_timeout(target->regs + RSTCTL, val,
					!(val & RSTCTL_RDBRST), 0, 1000);
	}
}

static u8 rcar_i3c_target_get_addr(struct i3c_target_ctrl *ctrl)
{
	struct rcar_i3c_target *target;
	u8 addr;

	target = to_rcar_i3c_target(ctrl);
	addr =  SDATBAS_SDDYAD(i3c_reg_read(target->regs, SDATBAS(0)));

	return addr;
}

static int rcar_i3c_target_raise_ibi(struct i3c_target_ctrl *ctrl, void *p, u8 size)
{
	struct rcar_i3c_target *target;
	u32 command_descriptor = 0;

	target = to_rcar_i3c_target(ctrl);
	if (size && !p)
		return -EINVAL;
	/* If Slave Interrupt Request disabled by Master, return Error */
	if (0 == (i3c_reg_read(target->regs, CSECMD) & CSECMD_SVIRQE)) {
		dev_err(&ctrl->dev, "SIR disabled by i3c target\n");
		return -EINVAL;
	}
	/* Check the Bus Available Condition */
	if (!(i3c_reg_read(target->regs, BCST) & BCST_BAVLF)) {
		dev_err(&ctrl->dev, "Can not detect bus available condition\n");
		return -EINVAL;
	}

	reinit_completion(&target->comp);

	target->internal_state = I3C_INTERNAL_STATE_SLAVE_IBI;
	/* Use regular transfer command for IBI request */
	command_descriptor = NCMDQP_CMD_ATTR(0) | NCMDQP_TID(I3C_IBI_WRITE) |
			     NCMDQP_RNW(1) | NCMDQP_ROC;
	/* Write data to IBI Data Queue */
	/*
	 * FIX ME: If the data to be written exceed IBI Queue Depth,
	 * continue writing the remaining data in IBI Empty ISR
	 */
	if (size > 0) {
		writesl(target->regs + NIBIQP, (u32 *)p, size / 4);
		if (size & 3) {
			u32 tmp = 0;

			memcpy(&tmp, (u32 *)p + (size & ~3), size & 3);
			writesl(target->regs + NIBIQP, &tmp, 1);
		}
		i3c_reg_clear_bit(target->regs, NTST, NTST_IBIQEFF);
	}

	/* Write command descriptor to command queue */
	i3c_reg_write(target->regs, NCMDQP, command_descriptor);
	i3c_reg_write(target->regs, NCMDQP, NCMDQP_DATA_LENGTH(size));

	i3c_reg_clear_bit(target->regs, NTST, NTST_CMDQEF);

	if (!wait_for_completion_timeout(&target->comp, msecs_to_jiffies(2000))) {
		dev_err(&ctrl->dev, "wait for IBI completed: TIMEOUT\n");
		target->internal_state = I3C_INTERNAL_STATE_SLAVE_IDLE;
		return -EIO;
	}

	return 0;
}

static int rcar_i3c_target_set_status_format1(struct i3c_target_ctrl *ctrl, u16 status)
{
	struct rcar_i3c_target *target;

	target = to_rcar_i3c_target(ctrl);
	i3c_reg_write(target->regs, CGDVST, status);

	return 0;
}

static u16 rcar_i3c_target_get_status_format1(struct i3c_target_ctrl *ctrl)
{
	struct rcar_i3c_target *target;

	target = to_rcar_i3c_target(ctrl);

	return (u16)i3c_reg_read(target->regs, CGDVST);
}

static int rcar_i3c_target_fifo_status(struct i3c_target_ctrl *ctrl, bool tx)
{
	struct rcar_i3c_target *target;
	u32 val;

	target = to_rcar_i3c_target(ctrl);
	val = i3c_reg_read(target->regs, NDBSTLV0);
	if (tx)
		return (NTDTBP0_DEPTH - NDBSTLV0_TDBFLV(val)) * 4;
	else
		return NDBSTLV0_RDBLV(val) * 4;
}

static int rcar_i3c_target_hotjoin(struct i3c_target_ctrl *ctrl)
{
	struct rcar_i3c_target *target;
	u8 addr;
	u32 command_descriptor;

	target = to_rcar_i3c_target(ctrl);
	reinit_completion(&target->comp);

	i3c_reg_set_bit(target->regs, CSECMD, CSECMD_HJEVE);
	/* Check the Bus Idle Condition */
	if (!(i3c_reg_read(target->regs, BCST) & BCST_BIDLF)) {
		dev_err(&ctrl->dev, "Can not detect bus idle condition\n");
		return -EINVAL;
	}

	target->internal_state = I3C_INTERNAL_STATE_SLAVE_IBI;

	/* Use regular transfer command for IBI request */
	command_descriptor = NCMDQP_CMD_ATTR(0) | NCMDQP_TID(I3C_IBI_WRITE) |
			     NCMDQP_HJ | NCMDQP_ROC;

	/* Write command descriptor to command queue */
	i3c_reg_write(target->regs, NCMDQP, command_descriptor);
	i3c_reg_write(target->regs, NCMDQP, 0);

	i3c_reg_clear_bit(target->regs, NTST, NTST_CMDQEF);

	if (!wait_for_completion_timeout(&target->comp, msecs_to_jiffies(2000))) {
		dev_err(&ctrl->dev, " Hot-join. Wait for DAA from target: TIMEOUT\n");
		return -EIO;
	}

	target->internal_state = I3C_INTERNAL_STATE_SLAVE_IDLE;

	addr = rcar_i3c_target_get_addr(ctrl);
	dev_info(&ctrl->dev, "Hot-join: successfully");

	return 0;
}

static struct i3c_target_ctrl_ops rcar_i3c_target_ops = {
	.set_config = rcar_i3c_target_set_config,
	.enable = rcar_i3c_target_enable,
	.disable = rcar_i3c_target_disable,
	.queue = rcar_i3c_target_queue,
	.dequeue = rcar_i3c_target_dequeue,
	.raise_ibi = rcar_i3c_target_raise_ibi,
	.fifo_flush = rcar_i3c_target_fifo_flush,
	.cancel_all_reqs = rcar_i3c_target_cancel_all_reqs,
	.get_features = rcar_i3c_target_get_features,
	.hotjoin = rcar_i3c_target_hotjoin,
	.fifo_status = rcar_i3c_target_fifo_status,
	.set_status_format1 = rcar_i3c_target_set_status_format1,
	.get_status_format1 = rcar_i3c_target_get_status_format1,
	.get_addr = rcar_i3c_target_get_addr,
};

static irqreturn_t rcar_i3c_target_tx_isr(struct rcar_i3c_target *target, u32 isr)
{
	unsigned long flags;

	rcar_i3c_target_fill_txfifo(target);
	spin_lock_irqsave(&target->txq_lock, flags);
	if (list_empty(&target->txq))
		i3c_reg_clear_bit(target->regs, NTIE, NTIE_TDBEIE0);

	spin_unlock_irqrestore(&target->txq_lock, flags);

	return IRQ_HANDLED;
}

static void rcar_i3c_target_error_recovery(struct rcar_i3c_target *target)
{
	int count = 0;
	u32 level, val;
	int ret, i;

	/* Read all descriptors */
	level = i3c_reg_read(target->regs, NQSTLV);
	count = NQSTLV_RSPQLV(level);
	for (i = 0; i < count; i++)
		i3c_reg_read(target->regs, NRSPQP);

	level = i3c_reg_read(target->regs, NRSQSTLV);
	count = NRSQSTLV_RSQLV(level);
	for (i = 0; i < count; i++)
		i3c_reg_read(target->regs, NRSQP);

	level = i3c_reg_read(target->regs, NDBSTLV0);
	count = NDBSTLV0_RDBLV(level);
	for (i = 0; i < count; i++)
		i3c_reg_read(target->regs, NTDTBP0);

	/* Clear Command and Tx Rx data FIFO */
	i3c_reg_set_bit(target->regs, RSTCTL, RSTCTL_CMDQRST |
			RSTCTL_TDBRST |	RSTCTL_RDBRST);
	/* Wait for reset completion  */
	ret = readl_relaxed_poll_timeout(target->regs + RSTCTL, val,
					 !(val & (RSTCTL_CMDQRST | RSTCTL_TDBRST |
					 RSTCTL_RDBRST)), 0, 1000);
	/* Resume the operation */
	i3c_reg_set_bit(target->regs, BCTL, BCTL_RSM);
	ret = readl_relaxed_poll_timeout(target->regs + BCTL, val,
					 !(val & BCTL_RSM), 0, 1000);
	if (!ret)
		dev_err(&target->base->dev, "Resume operation timeout");

	/* Clear error flags bit */
	i3c_reg_clear_bit(target->regs, INST, INST_INEF);
}

static void rcar_i3c_target_drain_rx_queue(struct rcar_i3c_target *target)
{
	int nwords = NDBSTLV0_RDBLV(i3c_reg_read(target->regs, NDBSTLV0));

	for (int i = 0; i < nwords; i++) {
		i3c_reg_read(target->regs, NTDTBP0);
	}
}

static irqreturn_t rcar_i3c_target_rx_isr(struct rcar_i3c_target *target, u32 isr)
{
	struct i3c_request *req, *complete = NULL;
	int read_bytes = 0;
	unsigned long flags;

	spin_lock_irqsave(&target->rxq_lock, flags);
	req = list_first_entry_or_null(&target->rxq, struct i3c_request, list);
	if (!req)
		goto out;
	/*
	 * If the transfer is complete, the remaining data must be read in Receive Status Full
	 * or Respond Status Full ISRs.
	 * This is because in order to read the remaining data, the driver must know exactly
	 * the total number of bytes were read during the transfer.
	 */
	if (0 == (i3c_reg_read(target->regs, NTST) & (NTST_RSQFF | NTSTE_RSPQFE))) {
		read_bytes = NDBSTLV0_RDBLV(i3c_reg_read(target->regs, NDBSTLV0)) * sizeof(u32);
		if (read_bytes) {
			readsl(target->regs + NTDTBP0, req->buf + req->actual, read_bytes / 4);
			req->actual += read_bytes;
		}
		/*
		 * If the actual length reaches the required length,
		 * stop read and complete the request.
		 */
		if (req->actual == req->length) {
			complete = req;
			list_del(&req->list);
			rcar_i3c_queue_complete(target, complete);
			complete = NULL;
		}
	}
	spin_unlock_irqrestore(&target->rxq_lock, flags);

	i3c_reg_clear_bit(target->regs, NTST, NTST_RDBFF0);

	return IRQ_HANDLED;
out:
	spin_unlock_irqrestore(&target->rxq_lock, flags);
	rcar_i3c_target_drain_rx_queue(target);

	i3c_reg_clear_bit(target->regs, NTST, NTST_RDBFF0);

	return IRQ_HANDLED;
}

static irqreturn_t rcar_i3c_target_rcv_isr(struct rcar_i3c_target *target, u32 isr)
{
	struct i3c_request *req, *complete = NULL;
	int read_bytes;
	unsigned long flags;
	u32 receive_status_descriptor, ntst;
	u8 command_code;

	receive_status_descriptor = i3c_reg_read(target->regs, NRSQP);
	/* Clear the Receive Status Queue Full Flag. */
	i3c_reg_clear_bit(target->regs, NTST, NTST_RSQFF);
	/* Read the data length */
	read_bytes = NRSQP_DATA_LEN(receive_status_descriptor);

	if (NRSQP_XFER_TYPE(receive_status_descriptor)) { /* The transfer type is CCC command. */
		command_code = NRSQP_CMD(receive_status_descriptor);
		if (command_code == I3C_CCC_BROADCAST_ENTDAA ||
		    command_code == I3C_CCC_DIRECT_SETDASA) {
			/* Dummy read */
			int nwords = DIV_ROUND_UP(read_bytes, sizeof(u32));

			for (int i = 0; i < nwords; i++) {
				i3c_reg_read(target->regs, NTDTBP0);
			}
			/* Verify that the assigned dynamic address is valid */
			if (i3c_reg_read(target->regs, SVDVAD(0)) & SVDVAD_SDYADV)
				dev_dbg(&target->base->dev, "I3C target 0's address is valid");
			else
				dev_err(&target->base->dev, "I3C target 0's address is invalid");
			/* If DAA following Hot-join request */
			if (target->internal_state == I3C_INTERNAL_STATE_SLAVE_IBI) {
				complete_all(&target->comp);
			}
		} else {
			rcar_i3c_target_drain_rx_queue(target);
		}
	} else {
		/* The transfer type is SDR */
		if (NRSQP_SDR_R_W_TYPE & receive_status_descriptor) {
			/* If the transfer is a read transfer.*/
			i3c_reg_clear_bit(target->regs, NTIE, NTIE_TDBEIE0);
		} else {
			/* If the transfer is a write transfer */
			i3c_reg_clear_bit(target->regs, NTIE, NTIE_RDBFIE0);

			spin_lock_irqsave(&target->rxq_lock, flags);
			req = list_first_entry_or_null(&target->rxq, struct i3c_request, list);
			if (req) {
				int bytes_remaining = 0;

				if (NDBSTLV0_RDBLV(i3c_reg_read(target->regs, NDBSTLV0)))
					bytes_remaining = read_bytes - req->actual;

				if (bytes_remaining > 0) {
					readsl(target->regs + NTDTBP0, req->buf + req->actual, bytes_remaining / 4);
					req->actual += (bytes_remaining / 4) * sizeof(u32);
				}

				if (bytes_remaining & 3) {
					u32 tmp;

					readsl(target->regs + NTDTBP0, &tmp, 1);
					memcpy(req->buf + req->actual, &tmp, bytes_remaining & 3);
					req->actual += (bytes_remaining & 3);
				}
				complete = req;
				list_del(&req->list);

				rcar_i3c_queue_complete(target, complete);
				complete = NULL;
			} else {
				/*
				 * If no request available, dummy read to empty
				 * the Receive Data Buffer for next transfer
				 */
				rcar_i3c_target_drain_rx_queue(target);
			}

			spin_unlock_irqrestore(&target->rxq_lock, flags);
		}
}

	ntst = i3c_reg_read(target->regs, NTST);
	if (0 != (ntst & (NTST_TABTF | NTST_TEF))) {
		rcar_i3c_target_error_recovery(target);
		i3c_reg_clear_bit(target->regs, BCTL, BCTL_ABT);
	}

	/* Clear error status flags. */
	i3c_reg_clear_bit(target->regs, NTST, NTST_TEF | NTST_TABTF);

	return IRQ_HANDLED;
}

static irqreturn_t rcar_i3c_target_resp_isr(struct rcar_i3c_target *target, u32 isr)
{
	u32 resp_descriptor, ntst;

	resp_descriptor = i3c_reg_read(target->regs, NRSPQP);
	/* Clear the Normal Response Queue status flag. */
	i3c_reg_clear_bit(target->regs, NTST, NTST_RSPQFF);
	target->internal_state = I3C_INTERNAL_STATE_SLAVE_IDLE;

	complete_all(&target->comp);

	ntst = i3c_reg_read(target->regs, NTST);
	if (0 != (ntst & (NTST_TABTF | NTST_TEF))) {
		rcar_i3c_target_error_recovery(target);
		i3c_reg_clear_bit(target->regs, BCTL, BCTL_ABT);
	}

	/* Clear error status flags. */
	i3c_reg_clear_bit(target->regs, NTST, NTST_TEF | NTST_TABTF);

	return IRQ_HANDLED;
}

static irqreturn_t rcar_i3c_target_ibi_isr(struct rcar_i3c_target *target, u32 isr)
{
	return IRQ_HANDLED;
}

static irqreturn_t rcar_i3c_target_irq_handler(int irq, void *data)
{
	struct rcar_i3c_target *target = data;
	u32 ntst, bst, inst;
	irqreturn_t ret = IRQ_NONE;

	ntst = i3c_reg_read(target->regs, NTST);
	bst = i3c_reg_read(target->regs, BST);
	inst = i3c_reg_read(target->regs, INST);

	if (ntst & NTST_RSPQFF) {
		ret = rcar_i3c_target_resp_isr(target, ntst);
		i3c_reg_clear_bit(target->regs, NTST, NTST_RSPQFF);
	} else if (ntst & NTST_RDBFF0) {
		ret = rcar_i3c_target_rx_isr(target, ntst);
		i3c_reg_clear_bit(target->regs, NTST, NTST_RDBFF0);
	} else if (ntst & NTST_TDBEF0) {
		ret = rcar_i3c_target_tx_isr(target, ntst);
		i3c_reg_clear_bit(target->regs, NTST, NTST_TDBEF0);
	} else if (ntst & NTST_IBIQEFF) {
		ret = rcar_i3c_target_ibi_isr(target, ntst);
		i3c_reg_clear_bit(target->regs, NTST, NTST_IBIQEFF);
	} else if (ntst & NTST_RSQFF) {
		ret = rcar_i3c_target_rcv_isr(target, ntst);
		i3c_reg_clear_bit(target->regs, NTST, NTST_RSQFF);
	} else {
		i3c_reg_clear_bit(target->regs, NTST, ntst);
		i3c_reg_clear_bit(target->regs, BST, bst);
		i3c_reg_clear_bit(target->regs, INST, inst);
	}

	return ret;
}

int rcar_i3c_target_probe(struct platform_device *pdev)
{
	struct rcar_i3c_target *target;
	struct device *dev = &pdev->dev;
	int ret, irq;

	target = devm_kzalloc(&pdev->dev, sizeof(*target), GFP_KERNEL);
	if (!target)
		return -ENOMEM;

	target->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(target->regs))
		return PTR_ERR(target->regs);

	/* Bus clock 100 MHz*/
	target->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(target->pclk))
		return PTR_ERR(target->pclk);
	/* Core clock for communications 200MHz */
	target->tclk = devm_clk_get(&pdev->dev, "tclk");
	if (IS_ERR(target->tclk))
		return PTR_ERR(target->tclk);

	ret = clk_prepare_enable(target->pclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(target->tclk);
	if (ret)
		goto err_disable_pclk;

	INIT_LIST_HEAD(&target->txq);
	INIT_LIST_HEAD(&target->rxq);
	INIT_LIST_HEAD(&target->cq);
	spin_lock_init(&target->txq_lock);
	spin_lock_init(&target->rxq_lock);
	spin_lock_init(&target->cq_lock);

	init_completion(&target->comp);

	INIT_WORK(&target->work, rcar_i3c_target_complete);
	target->workqueue = alloc_workqueue("%s-cq", WQ_UNBOUND, 10, dev_name(dev));
	if (!target->workqueue)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, rcar_i3c_target_irq_handler, 0,
			       dev_name(&pdev->dev), target);

	if (ret) {
		dev_err(&pdev->dev, "failed to request irq %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, target);

	target->maxdevs = RCAR_I3C_MAX_SLVS; // Max number of I3C targets is 3
	target->free_pos = GENMASK(target->maxdevs - 1, 0);
	target->dev = dev;
	target->base = devm_i3c_target_ctrl_create(dev, &rcar_i3c_target_ops);

	if (!target->base)
		goto err_disable_tclk;

	dev_set_drvdata(&target->base->dev, target);
	dev_info(&pdev->dev, "register I3C target successfully\n");

	return 0;

err_disable_tclk:
	clk_disable_unprepare(target->tclk);

err_disable_pclk:
	clk_disable_unprepare(target->pclk);

	return ret;
}

int rcar_i3c_target_remove(struct platform_device *pdev)
{
	struct rcar_i3c_target *target = platform_get_drvdata(pdev);

	clk_disable_unprepare(target->tclk);
	clk_disable_unprepare(target->pclk);
	/* clk_disable_unprepare(target->pclkrw);*/

	return 0;
}
