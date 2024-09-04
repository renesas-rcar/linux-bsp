// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car I3C driver
 *
 * Copyright (C) 2023 Renesas Electronics Corp.
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define PRTS			0x00
#define PRTS_PRTMD		BIT(0)

#define CECTL			0x10
#define CECTL_CLKE		BIT(0)

#define BCTL			0x14
#define BCTL_INCBA		BIT(0)
#define BAEXITMD		BIT(1)
#define BMDS			BIT(7)
#define BCTL_HJACKCTL		BIT(8)
#define BCTL_ABT		BIT(29)
#define BCTL_RSM		BIT(30)
#define BCTL_BUSE		BIT(31)

#define MSDVAD			0x18
#define MSDVAD_MDYAD(x)		(((x) << 16) & GENMASK(22, 16))
#define MSDVAD_MDYADV		BIT(31)

#define RSTCTL			0x20
#define RSTCTL_RI3CRST		BIT(0)
#define RSTCTL_CMDQRST		BIT(1)
#define RSTCTL_RSPQRST		BIT(2)
#define RSTCTL_TDBRST		BIT(3)
#define RSTCTL_RDBRST		BIT(4)
#define RSTCTL_IBIQRST		BIT(5)
#define RSTCTL_RSQRST		BIT(6)
#define RSTCTL_INTLRST		BIT(16)

#define PRSST			0x24
#define	PRSST_CRMS		BIT(2)
#define PRSST_TRMD		BIT(4)
#define PRSST_PRSSTWP		BIT(7)

#define INST			0x30
#define INST_INEF		BIT(10)

#define	INSTE			0x34
#define INSTE_INEE		BIT(10)

#define INIE			0x38
#define INIE_INEIE		BIT(10)

#define INSTFC			0x3C
#define INSTFC_INEFC		BIT(10)

#define DVCT			0x44
#define DVCT_IDX(x)		(((x) << 19) & GENMASK(23, 19))

#define IBINCTL			0x58
#define IBINCTL_NRHJCTL		BIT(0)
#define IBINCTL_NRMRCTL		BIT(1)
#define IBINCTL_NRSIRCTL	BIT(3)

#define BFCTL			0x60
#define BFCTL_MALE		BIT(0)
#define BFCTL_NALE		BIT(1)
#define BFCTL_SALE		BIT(2)
#define BFCTL_SCSYNE		BIT(8)
#define BFCTL_SMBS		BIT(12)
#define BFCTL_FMPE		BIT(14)
#define BFCTL_HSME		BIT(15)

#define SVCTL			0x64
#define SVCTL_GCAE		BIT(0)
#define SVCTL_HSMCE		BIT(5)
#define SVCTL_DVIDE		BIT(6)
#define SVCTL_HOAE		BIT(15)
#define SVCTL_SVAE0		BIT(16)
#define SVCTL_SVAE1		BIT(17)
#define SVCTL_SVAE2		BIT(18)

#define REFCKCTL		0x70
#define REFCKCTL_IREFCKS(x)	((x) & GENMASK(2, 0))
#define REFCKCTL_MSKCYC(x)	(((x) << 16) & GENMASK(17, 16))

#define STDBR			0x74
#define STDBR_DSBRPO		BIT(31)
#define STDBR_SBRLO(cond, x)	\
				({ \
				 typeof(x) _x = (x); \
				 (((cond) ? _x / 2 : _x) & 0xff) << 0; \
				 })

#define STDBR_SBRHO(cond, x)	\
				({ \
				 typeof(x) _x = (x); \
				 (((cond) ? _x / 2 : _x) & 0xff) << 8; \
				 })
#define STDBR_SBRLP(x)		(((x) & 0x3f) << 16)
#define STDBR_SBRHP(x)		(((x) & 0x3f) << 24)

#define EXTBR			0x78
#define EXTBR_EBRLO(x)		(((x) & 0xff) << 0)
#define EXTBR_EBRHO(x)		(((x) & 0xff) << 8)
#define EXTBR_EBRLP(x)		(((x) & 0x3f) << 16)
#define EXTBR_EBRHP(x)		(((x) & 0x3f) << 24)

#define BFRECDT			0x7c
#define BFRECDT_FRECYC(x)	(((x) & 0x1ff) << 0)

#define BAVLCDT			0x80
#define BAVLCDT_AVLCYC(x)	(((x) & 0x1ff) << 0)

#define BIDLCDT			0x84
#define BIDLCDT_IDLCYC(x)	(((x) & 0x3ffff) << 0)

#define ACKCTL			0xa0
#define ACKCTL_ACKR		BIT(0)
#define ACKCTL_ACKT		BIT(1)
#define ACKCTL_ACKTWP		BIT(2)

#define SCSTRCTL		0xa4
#define SCSTRCTL_ACKTWE		BIT(0)
#define SCSTRCTL_RWE		BIT(1)

#define SCSTLCTL		0xb0
#define SCSTLCTL_STLCYC(x)	(((x) & 0xffff) << 0)
#define SCSTLCTL_AAPE		BIT(28)
#define SCSTLCTL_PARPE		BIT(29)
#define SCSTLCTL_ACKPE		BIT(30)

#define CNDCTL			0x140
#define CNDCTL_STCND		BIT(0)
#define CNDCTL_SRCND		BIT(1)
#define CNDCTL_SPCND		BIT(2)

#define NCMDQP			0x150 /* Normal Command Queue */
#define NCMDQP_CMD_ATTR(x)	(((x) & 0x7) << 0)
#define NCMDQP_XFER		0x00
#define NCMDQP_IMMED_XFER	0x01
#define NCMDQP_ADDR_ASSGN	0x02
#define NCMDQP_TID(x)		(((x) & 0xf) << 3)
#define NCMDQP_CMD(x)		(((x) & 0xff) << 7)
#define NCMDQP_CP		BIT(15)
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
#define NRSPQP_ERROR_ADDR_HEADER	4
#define NRSPQP_ERROR_ADDRESS_NACK	5
#define NRSPQP_ERROR_OVER_UNDER_FLOW	6
#define NRSPQP_ERROR_TRANSF_ABORT	8
#define NRSPQP_ERROR_I2C_W_NACK_ERR	9
#define NRSPQP_ERR_STATUS(x)		(((x) & GENMASK(31, 28)) >> 28)
#define NRSPQP_TID(x)			(((x) & GENMASK(27, 24)) >> 24)
#define NRSPQP_DATA_LEN(x)		((x) & GENMASK(15, 0))

#define NTDTBP0			0x158 /* Normal Transfer Data Buffer */
#define NIBIQP			0x17c /* Normal IBI Queue */
#define NRSQP			0x180 /* Normal Receive Status Queue */

#define NQTHCTL			0x190
#define NQTHCTL_CMDQTH(x)	(((x) & 0x3) << 0)
#define NQTHCTL_RSPQTH(x)	(((x) & 0x3) << 8)
#define NQTHCTL_IBIDSSZ(x)	(((x) & 0xff) << 16)
#define NQTHCTL_IBIQTH(x)	(((x) & 0x7) << 24)

#define NTBTHCTL0		0x194
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
#define BSTE_ALL_FLAG		(BSTE_STCNDDE | BSTE_SPCNDDE |\
				BSTE_NACKDE | BSTE_TENDE |\
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
#define NTSTE_ALL_FLAG		(NTSTE_TDBEE0 | NTSTE_RDBFE0 |\
				NTSTE_IBIQEFE | NTSTE_CMDQEE |\
				NTSTE_RSPQFE | NTSTE_TABTE |\
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
#define BCST_BIDFL		BIT(2)

#define DATBAS(x)		(0x224 + 0x8 * (x))
#define DATBAS_DVSTAD(x)	(((x) & 0x7f) << 0)
#define DATBAS_DVIBIPL		BIT(12)
#define DATBAS_DVSIRRJ		BIT(13)
#define DATBAS_DVMRRJ		BIT(14)
#define DATBAS_DVIBITS		BIT(15)
#define DATBAS_DVDYAD(x)	(((x) & 0xff) << 16)
#define DATBAS_DVNACK(x)	(((x) & 0x3) << 29)
#define DATBAS_DVTYP		BIT(31)

#define NQSTLV			0x394
#define NQSTLV_CMDQFLV(x)	(((x) & 0xff) << 0)
#define NQSTLV_RSPQLV(x)	(((x) & 0xff) << 8)
#define NQSTLV_IBIQLV(x)	(((x) & 0xff) << 16)
#define NQSTLV_IBISCNT(x)	(((x) & 0x1f) << 24)

#define NDBSTLV0		0x398
#define NDBSTLV0_TDBFLV(x)	(((x) >> 0) & 0xff)
#define NDBSTLV0_RDBLV(x)	(((x) >> 8) & 0xff)

#define I3C_BUS_THIGH_MIXED_NS	40		/* 40ns */
#define I3C_BUS_FREE_TIME_NS	1300		/* 1.3us for Mixed Bus with I2C FM Device*/
#define I3C_BUS_AVAL_TIME_NS	1000		/* 1us */
#define I3C_BUS_IDLE_TIME_NS	200000		/* 200us */

#define RCAR_I3C_MAX_DEVS	8
#define I2C_INIT_MSG		-1
#define XFER_TIMEOUT		(msecs_to_jiffies(1000))
#define NTDTBP0_DEPTH		16

enum i3c_internal_state {
	I3C_INTERNAL_STATE_DISABLED,
	I3C_INTERNAL_STATE_MASTER_IDLE,
	I3C_INTERNAL_STATE_MASTER_ENTDAA,
	I3C_INTERNAL_STATE_MASTER_SETDASA,
	I3C_INTERNAL_STATE_MASTER_WRITE,
	I3C_INTERNAL_STATE_MASTER_READ,
	I3C_INTERNAL_STATE_MASTER_COMMAND_WRITE,
	I3C_INTERNAL_STATE_MASTER_COMMAND_READ,
	I3C_INTERNAL_STATE_SLAVE_IDLE,
	I3C_INTERNAL_STATE_SLAVE_IBI,
};

enum i3c_event {
	I3C_COMMAND_ADDRESS_ASSIGNMENT,
	I3C_WRITE,
	I3C_READ,
	I3C_COMMAND_WRITE,
	I3C_COMMAND_READ,
	I3C_IBI_WRITE,
};

struct rcar_i3c_cmd {
	u32 cmd0;
	u32 len;
	const void *tx_buf;
	u32 tx_count;
	void *rx_buf;
	u32 rx_count;
	u32 err;
	u8 rnw;
	/* i2c xfer */
	int i2c_bytes_left;
	int i2c_is_last;
	u8 *i2c_buf;
	const struct i2c_msg *msg;
};

struct rcar_i3c_xfer {
	struct list_head node;
	struct completion comp;
	int ret;
	bool is_i2c_xfer;
	unsigned int ncmds;
	struct rcar_i3c_cmd cmds[];
};

struct rcar_i3c_master {
	struct work_struct hj_work;
	struct i3c_master_controller base;
	u16 maxdevs;
	u32 free_pos;
	u32 STDBR_I2C_MODE;
	u32 STDBR_I3C_MODE;
	u8 addrs[RCAR_I3C_MAX_DEVS];
	enum i3c_internal_state internal_state;

	struct {
		struct list_head list;
		struct rcar_i3c_xfer *cur;
		/* Lock used to protect the critical section */
		spinlock_t lock;
	} xferqueue;
	void __iomem *regs;
	struct clk *tclk;
	struct clk *pclk;

};

struct rcar_i3c_i2c_dev_data {
	u8 index;
};

static inline void i3c_reg_update(u32 mask, u32 val, u32 __iomem *reg)
{
	u32 data = readl(reg);

	data &= ~mask;
	data |= (val & mask);
	writel(data, reg);
}

static u8 i3c_address_parity_cal(u8 addr)
{
	u8 par = addr | BIT(7);
	u8 i;

	for (i = 1; i < 8; i++)
		par ^= ((addr << i) & BIT(7));

	return par;
}

static inline void i3c_reg_write(void __iomem *base, u32 offset, u32 val)
{
	writel(val, base + (offset));
}

static inline u32 i3c_reg_read(void __iomem *base, u32 offset)
{
	return readl(base + (offset));
}

static void i3c_reg_set_bit(void __iomem *base, u32 reg, u32 val)
{
	i3c_reg_update(val, val, base + (reg));
}

static void i3c_reg_clear_bit(void __iomem *base, u32 reg, u32 val)
{
	i3c_reg_update(val, 0, base + (reg));
}

static void i3c_reg_update_bit(void __iomem *base, u32 reg,
			       u32 mask, u32 val)
{
	i3c_reg_update(mask, val, base + (reg));
}

static inline struct rcar_i3c_master *
to_rcar_i3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct rcar_i3c_master, base);
}

static int rcar_i3c_master_get_addr_pos(struct rcar_i3c_master *master, u8 addr)
{
	int pos;

	for (pos = 0; pos < master->maxdevs; pos++) {
		if (addr == master->addrs[pos])
			return pos;
	}

	return -EINVAL;
}

static int rcar_i3c_master_get_free_pos(struct rcar_i3c_master *master)
{
	if (!(master->free_pos & GENMASK(master->maxdevs - 1, 0)))
		return -ENOSPC;

	return ffs(master->free_pos) - 1;
}

static struct rcar_i3c_xfer *
rcar_i3c_master_alloc_xfer(struct rcar_i3c_master *master, unsigned int ncmds)
{
	struct rcar_i3c_xfer *xfer;

	xfer = kzalloc(struct_size(xfer, cmds, ncmds), GFP_KERNEL);
	if (!xfer)
		return NULL;

	INIT_LIST_HEAD(&xfer->node);
	xfer->ncmds = ncmds;
	xfer->ret = -ETIMEDOUT;

	return xfer;
}

static void rcar_i3c_master_free_xfer(struct rcar_i3c_xfer *xfer)
{
	kfree(xfer);
}

static void rcar_i3c_master_write_to_tx_fifo(struct rcar_i3c_master *master,
					     const u8 *data, int nbytes)
{
	writesl(master->regs + NTDTBP0, data, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp = 0;

		memcpy(&tmp, data + (nbytes & ~3), nbytes & 3);
		writesl(master->regs + NTDTBP0, &tmp, 1);
	}
}

static void rcar_i3c_master_read_from_rx_fifo(struct rcar_i3c_master *master,
					      u8 *data, int nbytes)
{
	readsl(master->regs + NTDTBP0, data, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp;

		readsl(master->regs + NTDTBP0, &tmp, 1);
		memcpy(data + (nbytes & ~3), &tmp, nbytes & 3);
	}
}

static void rcar_i3c_master_start_xfer_locked(struct rcar_i3c_master *master)
{
	struct rcar_i3c_xfer *xfer = master->xferqueue.cur;
	struct rcar_i3c_cmd *cmd;
	u32 cmd1;

	if (!xfer)
		return;

	cmd = xfer->cmds;
	switch (master->internal_state) {
	case I3C_INTERNAL_STATE_MASTER_ENTDAA:
	case I3C_INTERNAL_STATE_MASTER_SETDASA:
	{
		i3c_reg_set_bit(master->regs, NTIE, NTIE_RSPQFIE);
		i3c_reg_write(master->regs, NCMDQP, cmd->cmd0);
		i3c_reg_write(master->regs, NCMDQP, 0);
		break;
	}
	case I3C_INTERNAL_STATE_MASTER_WRITE:
	case I3C_INTERNAL_STATE_MASTER_COMMAND_WRITE:
	{
		i3c_reg_set_bit(master->regs, NTIE, NTIE_RSPQFIE);
		if (cmd->len <= 4) {
			cmd->cmd0 |= NCMDQP_CMD_ATTR(NCMDQP_IMMED_XFER);
			cmd->cmd0 |= NCMDQP_BYTE_CNT(cmd->len);
			cmd->tx_count = cmd->len;
			cmd1 = cmd->len == 0 ? 0 : *(u32 *)cmd->tx_buf;
		} else {
			cmd1 = NCMDQP_DATA_LENGTH(cmd->len);
		}
		i3c_reg_write(master->regs, NCMDQP, cmd->cmd0);
		i3c_reg_write(master->regs, NCMDQP, cmd1);
		break;
	}
	case I3C_INTERNAL_STATE_MASTER_READ:
	case I3C_INTERNAL_STATE_MASTER_COMMAND_READ:
	{
		i3c_reg_set_bit(master->regs, NTIE, NTIE_RDBFIE0);
		cmd1 = NCMDQP_DATA_LENGTH(cmd->len);
		i3c_reg_write(master->regs, NCMDQP, cmd->cmd0);
		i3c_reg_write(master->regs, NCMDQP, cmd1);
		break;
	}
	default:
		break;
	}

	/* Clear the command queue empty flag. line 935 r_i3c.c*/
	i3c_reg_clear_bit(master->regs, NTST, NTST_CMDQEF);
	if (cmd->len > 4 && master->internal_state == I3C_INTERNAL_STATE_MASTER_COMMAND_WRITE) {
		rcar_i3c_master_write_to_tx_fifo(master, cmd->tx_buf, cmd->len);
		/*
		 * If there is still data remaining in the transfer
		 * then it will be written in the Write Buffer Empty IRQ.
		 */
		if (cmd->len > NTDTBP0_DEPTH * sizeof(u32)) {
			/* Enable the Write Buffer Empty IRQ. */
			i3c_reg_set_bit(master->regs, NTIE, NTIE_TDBEIE0);
		}
	}
}

static void rcar_i3c_master_dequeue_xfer_locked(struct rcar_i3c_master *master,
						struct rcar_i3c_xfer *xfer)
{
	if (master->xferqueue.cur == xfer)
		master->xferqueue.cur = NULL;
	else
		list_del_init(&xfer->node);
}

static void rcar_i3c_master_dequeue_xfer(struct rcar_i3c_master *master,
					 struct rcar_i3c_xfer *xfer)
{
	unsigned long flags;

	spin_lock_irqsave(&master->xferqueue.lock, flags);
	rcar_i3c_master_dequeue_xfer_locked(master, xfer);
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void rcar_i3c_master_enqueue_xfer(struct rcar_i3c_master *master,
					 struct rcar_i3c_xfer *xfer)
{
	unsigned long flags;

	reinit_completion(&xfer->comp);
	spin_lock_irqsave(&master->xferqueue.lock, flags);
	if (master->xferqueue.cur) {
		list_add_tail(&xfer->node, &master->xferqueue.list);
	} else {
		master->xferqueue.cur = xfer;
		if (!xfer->is_i2c_xfer)
			rcar_i3c_master_start_xfer_locked(master);
	}
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void rcar_i3c_master_bus_enable(struct i3c_master_controller *m, bool i3c)
{
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);

	if (i3c) {
		/* I3C protocol mode */
		i3c_reg_write(master->regs, PRTS, 0);
		i3c_reg_set_bit(master->regs, BCTL, BCTL_HJACKCTL | BCTL_INCBA);
		i3c_reg_set_bit(master->regs, MSDVAD, MSDVAD_MDYADV);
		i3c_reg_write(master->regs, STDBR, master->STDBR_I3C_MODE);
	} else {
		/* I2C protocol mode */
		i3c_reg_write(master->regs, PRTS, 1);
		i3c_reg_write(master->regs, STDBR, master->STDBR_I3C_MODE);
	}

	/* Enable I3C bus */
	i3c_reg_set_bit(master->regs, BCTL, BCTL_BUSE);
}

static int rcar_i3c_master_bus_init(struct i3c_master_controller *m)
{
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	struct i3c_device_info info = {};
	unsigned long rate;
	u32 val, scl_falling_ns, scl_rising_ns;
	int cks, pp_high_ticks, pp_low_ticks, i3c_total_ticks;
	int od_high_ticks, od_low_ticks, i2c_total_ticks;
	int ret;
	bool double_SBR;

	rate = clk_get_rate(master->tclk);
	if (!rate)
		return -EINVAL;

	/* Reset the I3C */
	i3c_reg_write(master->regs, BCTL, 0);
	i3c_reg_set_bit(master->regs, RSTCTL, RSTCTL_RI3CRST);

	/* Waiting for reset completion  */
	ret = readl_relaxed_poll_timeout(master->regs + RSTCTL, val,
					 !(val & RSTCTL_RI3CRST), 0, 1000);
	if (ret)
		return ret;

	/* Enable clock function */
	i3c_reg_set_bit(master->regs, CECTL, CECTL_CLKE);

	/* Set present state to master mode */
	i3c_reg_write(master->regs, PRSST, PRSST_PRSSTWP | PRSST_CRMS);

	i2c_total_ticks = DIV_ROUND_UP(rate, bus->scl_rate.i2c);
	i3c_total_ticks = DIV_ROUND_UP(rate, bus->scl_rate.i3c);
	/* Internal Reference Clock Selection */
	scl_rising_ns = (bus->scl_rate.i2c <= I2C_MAX_STANDARD_MODE_FREQ) ? 1000 :
					(bus->scl_rate.i2c <= I2C_MAX_FAST_MODE_FREQ) ? 300 : 120;
	scl_falling_ns = (bus->scl_rate.i2c <= I2C_MAX_FAST_MODE_FREQ) ? 300 : 120;

	for (cks = 0; cks < 7; cks++) {
		/* SCL low-period calculation in Open-drain mode */
		od_low_ticks = ((i2c_total_ticks * 6) / 10);

		/* SCL clock calculation in Push-Pull mode */
		if (bus->mode == I3C_BUS_MODE_PURE) {
			pp_high_ticks = ((i3c_total_ticks * 5) / 10);
		} else {
			pp_high_ticks = DIV_ROUND_UP(I3C_BUS_THIGH_MIXED_NS,
						     1000000000 / rate);
		}
		pp_low_ticks = i3c_total_ticks - pp_high_ticks;

		if ((od_low_ticks / 2) <= 0xFF && pp_low_ticks < 0x3F)
			break;

		i2c_total_ticks /= 2;
		i3c_total_ticks /= 2;
		rate /= 2;
	}

	/* SCL clock period calculation in Open-drain mode */
	if ((od_low_ticks / 2) > 0xFF || pp_low_ticks > 0x3F) {
		dev_err(&m->dev, "invalid speed (i2c-scl = %lu Hz, i3c-scl = %lu Hz). Too slow.\n",
			(unsigned long)bus->scl_rate.i2c, (unsigned long)bus->scl_rate.i3c);
		ret = -EINVAL;
		return ret;
	}

	/* SCL high-period calculation in Open-drain mode */
	od_high_ticks = i2c_total_ticks - od_low_ticks;

	/* Setting Standard bit rate */
	double_SBR = od_low_ticks > 0xFF ? true : false;
	master->STDBR_I3C_MODE = (double_SBR ? STDBR_DSBRPO : 0) |
			STDBR_SBRLO(double_SBR, od_low_ticks) |
			STDBR_SBRHO(double_SBR, od_high_ticks) |
			STDBR_SBRLP(pp_low_ticks) |
			STDBR_SBRHP(pp_high_ticks);

	od_low_ticks -= scl_falling_ns / (1000000000 / rate) + 1;
	od_high_ticks -= scl_rising_ns / (1000000000 / rate) + 1;
	master->STDBR_I2C_MODE = (double_SBR ? STDBR_DSBRPO : 0) |
			STDBR_SBRLO(double_SBR, od_low_ticks) |
			STDBR_SBRHO(double_SBR, od_high_ticks) |
			STDBR_SBRLP(pp_low_ticks) |
			STDBR_SBRHP(pp_high_ticks);
	i3c_reg_write(master->regs, STDBR, master->STDBR_I3C_MODE);

	/* Extended Bit Rate setting */
	i3c_reg_write(master->regs, EXTBR, EXTBR_EBRLO(od_low_ticks) |
					   EXTBR_EBRHO(od_high_ticks) |
					   EXTBR_EBRLP(pp_low_ticks) |
					   EXTBR_EBRHP(pp_high_ticks));

	i3c_reg_write(master->regs, REFCKCTL, REFCKCTL_IREFCKS(cks));

	/* Disable Slave Mode */
	i3c_reg_write(master->regs, SVCTL, 0);

	/* Setting Queue/Buffer threshold. */
	i3c_reg_write(master->regs, NQTHCTL, NQTHCTL_IBIDSSZ(6) |
					     NQTHCTL_CMDQTH(1));

	/* The only supported configuration is two entries*/
	i3c_reg_write(master->regs, NTBTHCTL0, 0);
	/* Interrupt when there is one entry in the queue */
	i3c_reg_write(master->regs, NRQTHCTL, 0);

	/* Enable all Bus/Transfer Status Flags. */
	i3c_reg_write(master->regs, BSTE, BSTE_ALL_FLAG);
	i3c_reg_write(master->regs, NTSTE, NTSTE_ALL_FLAG);

	i3c_reg_write(master->regs, INSTE, INSTE_INEE);

	/* Interrupt enable settings */
	i3c_reg_write(master->regs, INIE, INIE_INEIE);
	i3c_reg_write(master->regs, BIE, BIE_NACKDIE | BIE_TENDIE);
	i3c_reg_write(master->regs, NTIE, NTIE_RSQFIE |
		      NTIE_IBIQEFIE | NTIE_RDBFIE0);

	/* Clear Status register */
	i3c_reg_write(master->regs, NTST, 0);
	i3c_reg_write(master->regs, INST, 0);
	i3c_reg_write(master->regs, BST, 0);

	/* Hot-Join Acknowlege setting. */
	i3c_reg_update_bit(master->regs, BCTL, BCTL_HJACKCTL, BCTL_HJACKCTL);

	i3c_reg_write(master->regs, IBINCTL, IBINCTL_NRHJCTL | IBINCTL_NRMRCTL |
		      IBINCTL_NRSIRCTL);

	i3c_reg_write(master->regs, SCSTLCTL, 0);
	i3c_reg_set_bit(master->regs, SCSTRCTL, SCSTRCTL_ACKTWE);

	/* Setting bus condition detection timing */
	val = DIV_ROUND_UP(I3C_BUS_FREE_TIME_NS, 1000000000 / rate);
	i3c_reg_write(master->regs, BFRECDT, BFRECDT_FRECYC(val));

	val = DIV_ROUND_UP(I3C_BUS_AVAL_TIME_NS, 1000000000 / rate);
	i3c_reg_write(master->regs, BAVLCDT, BAVLCDT_AVLCYC(val));

	val = DIV_ROUND_UP(I3C_BUS_IDLE_TIME_NS, 1000000000 / rate);
	i3c_reg_write(master->regs, BIDLCDT, BIDLCDT_IDLCYC(val));

	/* Get an address for I3C master. */
	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;

	/* Setting Master Dynamic Address. */
	i3c_reg_write(master->regs, MSDVAD,
		      MSDVAD_MDYAD(ret) | MSDVAD_MDYADV);

	memset(&info, 0, sizeof(info));
	info.dyn_addr = ret;
	ret = i3c_master_set_info(&master->base, &info);

	return 0;
}

static void rcar_i3c_master_bus_cleanup(struct i3c_master_controller *m)
{
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);
	int ret;
	u32 val;

	i3c_reg_write(master->regs, BCTL, 0);
	i3c_reg_update_bit(master->regs, RSTCTL, RSTCTL_RI3CRST,
			   RSTCTL_RI3CRST);

	/* Wait for reset completion  */
	ret = readl_relaxed_poll_timeout(master->regs + RSTCTL, val,
					 !(val & RSTCTL_RI3CRST), 0, 1000);
}

static int rcar_i3c_master_daa(struct i3c_master_controller *m)
{
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);
	struct rcar_i3c_xfer *xfer;
	struct rcar_i3c_cmd *cmd;
	u32 olddevs, newdevs;
	u8 last_addr = 0, pos;
	int ret;

	if (master->maxdevs == 8) {
		/* Temporary skipping ENTDAA due to lack of slave device */
		goto end;
	}

	/* Enable I3C bus. */
	rcar_i3c_master_bus_enable(m, true);

	olddevs = ~(master->free_pos);
	master->internal_state = I3C_INTERNAL_STATE_MASTER_ENTDAA;

	/* Setting DATBASn registers for slave devices. */
	for (pos = 0; pos < master->maxdevs; pos++) {
		if (olddevs & BIT(pos))
			continue;

		ret = i3c_master_get_free_addr(m, last_addr + 1);
		if (ret < 0)
			return -ENOSPC;

		master->addrs[pos] = ret;
		last_addr = ret;

		i3c_reg_write(master->regs, DATBAS(pos),
			      DATBAS_DVDYAD(i3c_address_parity_cal(ret)));
	}

	xfer = rcar_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	init_completion(&xfer->comp);
	cmd = xfer->cmds;
	cmd->rx_count = 0;

	pos = rcar_i3c_master_get_free_pos(master);
	if (pos < 0) {
		rcar_i3c_master_free_xfer(xfer);
		return pos;
	}

	/*
	 * Setup the command descriptor to start the ENTDAA command
	 * and starting at the selected device index.
	 */
	cmd->cmd0 = NCMDQP_CMD_ATTR(NCMDQP_ADDR_ASSGN) | NCMDQP_ROC |
		    NCMDQP_TID(I3C_COMMAND_ADDRESS_ASSIGNMENT) |
		    NCMDQP_CMD(I3C_CCC_ENTDAA) | NCMDQP_DEV_INDEX(pos) |
		    NCMDQP_DEV_COUNT(master->maxdevs - pos) | NCMDQP_TOC;
	rcar_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000)))
		rcar_i3c_master_dequeue_xfer(master, xfer);

	newdevs = GENMASK(master->maxdevs - cmd->rx_count - 1, 0);
	newdevs &= ~olddevs;

	for (pos = 0; pos < master->maxdevs; pos++) {
		if (newdevs & BIT(pos))
			i3c_master_add_i3c_dev_locked(m, master->addrs[pos]);
	}

	rcar_i3c_master_free_xfer(xfer);

end:
	return 0;
}

static bool rcar_i3c_master_supports_ccc_cmd(struct i3c_master_controller *m,
					     const struct i3c_ccc_cmd *ccc)
{
	if (ccc->ndests > 1)
		return false;

	switch (ccc->id) {
	case I3C_CCC_ENEC(true):
	case I3C_CCC_ENEC(false):
	case I3C_CCC_DISEC(true):
	case I3C_CCC_DISEC(false):
	case I3C_CCC_ENTAS(0, true):
	case I3C_CCC_ENTAS(0, false):
	case I3C_CCC_RSTDAA(true):
	case I3C_CCC_RSTDAA(false):
	case I3C_CCC_ENTDAA:
	case I3C_CCC_SETMWL(true):
	case I3C_CCC_SETMWL(false):
	case I3C_CCC_SETMRL(true):
	case I3C_CCC_SETMRL(false):
	case I3C_CCC_ENTHDR(0):
	case I3C_CCC_SETDASA:
	case I3C_CCC_SETNEWDA:
	case I3C_CCC_GETMWL:
	case I3C_CCC_GETMRL:
	case I3C_CCC_GETPID:
	case I3C_CCC_GETBCR:
	case I3C_CCC_GETDCR:
	case I3C_CCC_GETSTATUS:
	case I3C_CCC_GETMXDS:
	case I3C_CCC_GETHDRCAP:
		return true;
	default:
		return false;
	}
}

static int rcar_i3c_master_send_ccc_cmd(struct i3c_master_controller *m,
					struct i3c_ccc_cmd *ccc)
{
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);
	struct rcar_i3c_xfer *xfer;
	struct rcar_i3c_cmd *cmd;
	int ret, pos;

	/* Enable I3C bus. */
	rcar_i3c_master_bus_enable(m, true);
	xfer = rcar_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	init_completion(&xfer->comp);

	if (ccc->id & I3C_CCC_DIRECT) {
		pos = rcar_i3c_master_get_addr_pos(master, ccc->dests[0].addr);
		if (pos < 0)
			return pos;
	} else {
		xfer->ret = 0;
	}
	cmd = xfer->cmds;
	cmd->rnw = ccc->rnw;
	cmd->cmd0 = 0;

	/* Calculate the command descriptor. */
	switch (ccc->id) {
	case I3C_CCC_SETDASA:
		i3c_reg_write(master->regs, DATBAS(pos),
			      DATBAS_DVSTAD(ccc->dests[0].addr) |
			      DATBAS_DVDYAD(*(u8 *)ccc->dests[0].payload.data >> 1));
		cmd->cmd0 = NCMDQP_CMD_ATTR(NCMDQP_ADDR_ASSGN) | NCMDQP_ROC |
				NCMDQP_TID(I3C_COMMAND_ADDRESS_ASSIGNMENT) |
			NCMDQP_CMD(I3C_CCC_SETDASA) | NCMDQP_DEV_INDEX(pos) |
			NCMDQP_DEV_COUNT(0) | NCMDQP_TOC;
		master->internal_state = I3C_INTERNAL_STATE_MASTER_SETDASA;
		break;
	default:
		cmd->cmd0 = NCMDQP_TID(I3C_COMMAND_WRITE) | NCMDQP_CP |
			NCMDQP_CMD(ccc->id) |  NCMDQP_DEV_INDEX(pos) |
			NCMDQP_MODE(0) | NCMDQP_RNW(ccc->rnw) | NCMDQP_ROC | NCMDQP_TOC;

		if (ccc->rnw) {
			cmd->rx_buf = ccc->dests[0].payload.data;
			cmd->len = ccc->dests[0].payload.len;
			cmd->rx_count = 0;
			master->internal_state = I3C_INTERNAL_STATE_MASTER_COMMAND_READ;
		} else {
			cmd->tx_buf = ccc->dests[0].payload.data;
			cmd->len = ccc->dests[0].payload.len;
			cmd->tx_count = 0;
			master->internal_state = I3C_INTERNAL_STATE_MASTER_COMMAND_WRITE;
		}
	}

	rcar_i3c_master_enqueue_xfer(master, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000)))
		rcar_i3c_master_dequeue_xfer(master, xfer);

	ret = xfer->ret;
	rcar_i3c_master_free_xfer(xfer);

	return ret;
}

static int rcar_i3c_master_priv_xfers(struct i3c_dev_desc *dev,
				      struct i3c_priv_xfer *i3c_xfers,
				      int i3c_nxfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct rcar_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);
	struct rcar_i3c_xfer *xfer;
	int i;

	/* Enable I3C bus. */
	rcar_i3c_master_bus_enable(m, true);
	xfer = rcar_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	init_completion(&xfer->comp);

	for (i = 0; i < i3c_nxfers; i++) {
		struct rcar_i3c_cmd *cmd = &xfer->cmds[i];

		/* Calculate the Transfer Command Descriptor */
		cmd->rnw = i3c_xfers[i].rnw;
		cmd->cmd0 = NCMDQP_DEV_INDEX(data->index) | NCMDQP_MODE(0) |
					     NCMDQP_RNW(cmd->rnw) | NCMDQP_ROC | NCMDQP_TOC;

		if (i3c_xfers[i].rnw) {
			cmd->rx_count = 0;
			cmd->rx_buf = i3c_xfers[i].data.in;
			cmd->len = i3c_xfers[i].len;
			cmd->cmd0 |= NCMDQP_TID(I3C_READ);
			master->internal_state = I3C_INTERNAL_STATE_MASTER_READ;
		} else {
			cmd->tx_count = 0;
			cmd->tx_buf = i3c_xfers[i].data.out;
			cmd->len = i3c_xfers[i].len;
			cmd->cmd0 |= NCMDQP_TID(I3C_WRITE);
			master->internal_state = I3C_INTERNAL_STATE_MASTER_WRITE;
		}

		if (!i3c_xfers[i].rnw && i3c_xfers[i].len > 4) {
			rcar_i3c_master_write_to_tx_fifo(master, cmd->tx_buf, cmd->len);
			if (cmd->len > NTDTBP0_DEPTH * sizeof(u32))
				i3c_reg_set_bit(master->regs, NTIE, NTIE_TDBEIE0);
		}
		rcar_i3c_master_enqueue_xfer(master, xfer);
		if (!wait_for_completion_timeout(&xfer->comp, msecs_to_jiffies(1000)))
			rcar_i3c_master_dequeue_xfer(master, xfer);
	}

	return 0;
}

static int rcar_i3c_master_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);
	struct rcar_i3c_i2c_dev_data *data;
	int pos;

	pos = rcar_i3c_master_get_free_pos(master);
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = pos;
	master->addrs[pos] = dev->info.dyn_addr ? : dev->info.static_addr;
	master->free_pos &= ~BIT(pos);

	i3c_reg_write(master->regs, DATBAS(pos),
		      DATBAS_DVSTAD(dev->info.static_addr) |
		      DATBAS_DVDYAD(i3c_address_parity_cal(master->addrs[pos])));
	i3c_dev_set_master_data(dev, data);

	return 0;
}

static int rcar_i3c_master_reattach_i3c_dev(struct i3c_dev_desc *dev,
					    u8 old_dyn_addr)
{
	struct rcar_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);

	master->addrs[data->index] = dev->info.dyn_addr ? dev->info.dyn_addr :
							dev->info.static_addr;

	return 0;
}

static void rcar_i3c_master_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct rcar_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);

	i3c_dev_set_master_data(dev, NULL);
	master->addrs[data->index] = 0;
	master->free_pos |= BIT(data->index);
	kfree(data);
}

static int rcar_i3c_master_i2c_xfers(struct i2c_dev_desc *dev,
				     const struct i2c_msg *i2c_xfers,
				     int i2c_nxfers)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);
	struct rcar_i3c_xfer *xfer;
	struct rcar_i3c_cmd *cmd;
	int i, ret;
	u8 start_bit;

	if (!i2c_nxfers)
		return 0;
	/* Enable I3C bus, I2C protocol mode */
	rcar_i3c_master_bus_enable(m, false);
	xfer = rcar_i3c_master_alloc_xfer(master, 1);
	if (!xfer)
		return -ENOMEM;

	init_completion(&xfer->comp);
	xfer->is_i2c_xfer = true;
	cmd = xfer->cmds;

	if (!(i3c_reg_read(master->regs, BCST) & BCST_BFREF)) {
		cmd->err = -EBUSY;
		goto out;
	}
	i3c_reg_write(master->regs, BST, 0);

	rcar_i3c_master_enqueue_xfer(master, xfer);

	for (i = 0; i < i2c_nxfers; i++) {
		cmd->i2c_bytes_left = I2C_INIT_MSG;
		cmd->i2c_buf = i2c_xfers[i].buf;
		cmd->msg = &i2c_xfers[i];
		cmd->i2c_is_last = (i == i2c_nxfers - 1);

		i3c_reg_set_bit(master->regs, BIE, BIE_NACKDIE);
		i3c_reg_set_bit(master->regs, NTIE, NTIE_TDBEIE0);
		i3c_reg_set_bit(master->regs, BIE, BIE_STCNDDIE);

		/* Issue Start condition */
		i3c_reg_set_bit(master->regs, CNDCTL, start_bit);

		i3c_reg_set_bit(master->regs, NTSTE, NTSTE_TDBEE0);

		wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT);

		if (cmd->err)
			break;

		start_bit = CNDCTL_SRCND;
	}

	ret = xfer->ret;

out:
	rcar_i3c_master_free_xfer(xfer);
	rcar_i3c_master_dequeue_xfer(master, xfer);
	return cmd->err ? : 0;
}

static int rcar_i3c_master_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);
	struct rcar_i3c_i2c_dev_data *data;
	int pos;

	pos = rcar_i3c_master_get_free_pos(master);
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = pos;
	master->addrs[pos] = dev->addr;
	master->free_pos &= ~BIT(pos);
	i2c_dev_set_master_data(dev, data);
	return 0;
}

static void rcar_i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct rcar_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct rcar_i3c_master *master = to_rcar_i3c_master(m);

	i2c_dev_set_master_data(dev, NULL);
	master->addrs[data->index] = 0;
	master->free_pos |= BIT(data->index);
	kfree(data);
}

static irqreturn_t rcar_i3c_master_resp_isr(struct rcar_i3c_master *master, u32 isr)
{
	struct rcar_i3c_xfer *xfer = master->xferqueue.cur;
	struct rcar_i3c_cmd *cmd = xfer->cmds;
	int ret = 0;
	u32 bytes_remaining = 0;
	u32 resp, data_len;

	/* Get the response status from the Respone status Queue */
	resp = i3c_reg_read(master->regs, NRSPQP);
	/* Clear the Respone Queue Full status flag*/
	i3c_reg_clear_bit(master->regs, NTST, NTST_RSPQFF);

	/* Read the DATA_LENGTH field in the response descriptor. */
	data_len = NRSPQP_DATA_LEN(resp);
	switch (master->internal_state) {
	case I3C_INTERNAL_STATE_MASTER_ENTDAA:
		cmd->rx_count = data_len;
		break;
	case I3C_INTERNAL_STATE_MASTER_WRITE:
	case I3C_INTERNAL_STATE_MASTER_COMMAND_WRITE:
		/* Disable the transmit IRQ if it hasn't been disabled already. */
		i3c_reg_clear_bit(master->regs, NTIE, NTIE_TDBEIE0);
		break;
	case I3C_INTERNAL_STATE_MASTER_READ:
	case I3C_INTERNAL_STATE_MASTER_COMMAND_READ:
		if ((NDBSTLV0_RDBLV(i3c_reg_read(master->regs, NDBSTLV0))) && !cmd->err)
			bytes_remaining = data_len - cmd->rx_count;
		rcar_i3c_master_read_from_rx_fifo(master, cmd->rx_buf, bytes_remaining);
		i3c_reg_clear_bit(master->regs, NTIE, NTIE_RDBFIE0);
		break;
	default:
		break;
	}

	switch (NRSPQP_ERR_STATUS(resp)) {
	case NRSPQP_NO_ERROR:
			break;
	case NRSPQP_ERROR_PARITY:
	case NRSPQP_ERROR_ADDR_HEADER:
	case NRSPQP_ERROR_CRC:
	case NRSPQP_ERROR_FRAME:
	case NRSPQP_ERROR_TRANSF_ABORT:
		ret = -EIO;
		break;
	case NRSPQP_ERROR_OVER_UNDER_FLOW:
		ret = -ENOSPC;
		break;
	case NRSPQP_ERROR_I2C_W_NACK_ERR:
	case NRSPQP_ERROR_ADDRESS_NACK:
	default:
		ret = -EINVAL;
		break;
	}

	/* If the transfer was aborted, then the abort flag must be cleared before
	 * notifying the application that a transfer has completed.
	 */
	if ((NTST_TABTF & isr) != 0)
		i3c_reg_clear_bit(master->regs, BCTL, BCTL_ABT);

	/* Clear error status flags. */
	i3c_reg_clear_bit(master->regs, NTST, NTST_TEF | NTST_TABTF);

	xfer->ret = ret;
	complete(&xfer->comp);
	xfer = list_first_entry_or_null(&master->xferqueue.list,
					struct rcar_i3c_xfer,
					node);
	if (xfer)
		list_del_init(&xfer->node);

	master->xferqueue.cur = xfer;

	return IRQ_HANDLED;
}

static irqreturn_t rcar_i3c_master_rx_isr(struct rcar_i3c_master *master, u32 isr)
{
	struct rcar_i3c_xfer *xfer = master->xferqueue.cur;
	struct rcar_i3c_cmd *cmd = xfer->cmds;
	int read_bytes;

	if (xfer->is_i2c_xfer) {
		if (!cmd->i2c_bytes_left)
			return IRQ_NONE;
		if (cmd->i2c_bytes_left == I2C_INIT_MSG) {
			cmd->i2c_bytes_left = cmd->msg->len;
			i3c_reg_set_bit(master->regs, SCSTRCTL, SCSTRCTL_RWE);
			i3c_reg_read(master->regs, NTDTBP0); /* dummy read */
			if (cmd->i2c_bytes_left == 1)
				i3c_reg_set_bit(master->regs, ACKCTL, ACKCTL_ACKT | ACKCTL_ACKTWP);
			return IRQ_NONE;
		}

		switch (cmd->i2c_bytes_left) {
		case 1:
			if (cmd->i2c_is_last) {
				/* STOP must be set before the last receive byte */
				i3c_reg_set_bit(master->regs, BIE, BIE_SPCNDDIE);
				i3c_reg_clear_bit(master->regs, BST, BST_SPCNDDF);
				i3c_reg_set_bit(master->regs, CNDCTL, CNDCTL_SPCND);
			}
			i3c_reg_set_bit(master->regs, ACKCTL, ACKCTL_ACKT | ACKCTL_ACKTWP);
			break;
		case 2:
			i3c_reg_set_bit(master->regs, ACKCTL, ACKCTL_ACKT | ACKCTL_ACKTWP);
			break;
		default:
			i3c_reg_write(master->regs, ACKCTL, ACKCTL_ACKTWP);
			break;
		}
		/* Reading acks the RIE interrupt */
		*cmd->i2c_buf = i3c_reg_read(master->regs, NTDTBP0);
		cmd->i2c_buf++;
		cmd->i2c_bytes_left--;
	} else {
		u32 resp_descriptor;

		resp_descriptor = i3c_reg_read(master->regs, NRSPQP);
		read_bytes = NDBSTLV0_RDBLV(i3c_reg_read(master->regs, NDBSTLV0)) * sizeof(u32);
		if (master->internal_state == I3C_INTERNAL_STATE_MASTER_ENTDAA && read_bytes == 8) {
			i3c_reg_set_bit(master->regs, NTIE, NTIE_RSPQFIE);
			i3c_reg_read(master->regs, NTDTBP0);
			i3c_reg_read(master->regs, NTDTBP0);
			cmd->rx_count++;
		} else {
			rcar_i3c_master_read_from_rx_fifo(master, cmd->rx_buf, read_bytes);
			cmd->rx_count = read_bytes;
		}
	}
	/* Clear the Read Buffer Full status flag. */
	i3c_reg_clear_bit(master->regs, NTST, NTST_RDBFF0);
	i3c_reg_clear_bit(master->regs, NTST, NTST_TDBEF0);

	return IRQ_HANDLED;
}

static irqreturn_t rcar_i3c_master_tx_isr(struct rcar_i3c_master *master, u32 isr)
{
	struct rcar_i3c_xfer *xfer = master->xferqueue.cur;
	struct rcar_i3c_cmd *cmd = xfer->cmds;
	u8 val;

	if (xfer->is_i2c_xfer) {
		if (!cmd->i2c_bytes_left)
			return IRQ_NONE;

		if (cmd->i2c_bytes_left != I2C_INIT_MSG) {
			val = *cmd->i2c_buf;
			cmd->i2c_buf++;
			cmd->i2c_bytes_left--;
			i3c_reg_write(master->regs, NTDTBP0, val);
		}

		if (cmd->i2c_bytes_left == 0) {
			i3c_reg_clear_bit(master->regs, NTIE, NTIE_TDBEIE0);
			i3c_reg_set_bit(master->regs, BIE, BIE_TENDIE);
		}

		/* Clear the Transmit Buffer Empty status flag. */
		i3c_reg_clear_bit(master->regs, NTST, NTST_TDBEF0);
	} else {
		rcar_i3c_master_write_to_tx_fifo(master, cmd->tx_buf, cmd->len);
	}

	return IRQ_HANDLED;
}

static irqreturn_t rcar_i3c_master_irq_handler(int irq, void *data)
{
	struct rcar_i3c_master *master = data;
	u32 ntst_status, bst_status, inst_status;
	irqreturn_t ret = IRQ_NONE;

	ntst_status = i3c_reg_read(master->regs, NTST);
	bst_status = i3c_reg_read(master->regs, BST);
	inst_status = i3c_reg_read(master->regs, INST);
	i3c_reg_write(master->regs, NTST, 0);

	if (ntst_status & NTST_RSPQFF) {
		ret = rcar_i3c_master_resp_isr(master, ntst_status);
		i3c_reg_clear_bit(master->regs, NTST, NTST_RSPQFF);
	} else if (ntst_status & NTST_RDBFF0) {
		ret = rcar_i3c_master_rx_isr(master, ntst_status);
		i3c_reg_clear_bit(master->regs, NTST, NTST_RDBFF0);
	} else if (ntst_status & NTST_TDBEF0) {
		ret = rcar_i3c_master_tx_isr(master, ntst_status);
		i3c_reg_clear_bit(master->regs, NTST, NTST_TDBEF0);
	} else {
		i3c_reg_clear_bit(master->regs, NTST, ntst_status);
		i3c_reg_clear_bit(master->regs, BST, bst_status);
		i3c_reg_clear_bit(master->regs, INST, inst_status);
	}

	return ret;
}

static const struct i3c_master_controller_ops rcar_i3c_master_ops = {
	.bus_init = rcar_i3c_master_bus_init,
	.bus_cleanup = rcar_i3c_master_bus_cleanup,
	.do_daa = rcar_i3c_master_daa,
	.supports_ccc_cmd = rcar_i3c_master_supports_ccc_cmd,
	.send_ccc_cmd = rcar_i3c_master_send_ccc_cmd,
	.priv_xfers = rcar_i3c_master_priv_xfers,
	.attach_i3c_dev = rcar_i3c_master_attach_i3c_dev,
	.reattach_i3c_dev = rcar_i3c_master_reattach_i3c_dev,
	.detach_i3c_dev = rcar_i3c_master_detach_i3c_dev,
	.i2c_xfers = rcar_i3c_master_i2c_xfers,
	.attach_i2c_dev = rcar_i3c_master_attach_i2c_dev,
	.detach_i2c_dev = rcar_i3c_master_detach_i2c_dev,
};

static int rcar_i3c_master_probe(struct platform_device *pdev)
{
	struct rcar_i3c_master *master;
	int ret, irq;

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(master->regs))
		return PTR_ERR(master->regs);

	master->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(master->pclk))
		return PTR_ERR(master->pclk);

	master->tclk = devm_clk_get(&pdev->dev, "tclk");
	if (IS_ERR(master->tclk))
		return PTR_ERR(master->tclk);
	ret = clk_prepare_enable(master->pclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(master->tclk);
	if (ret)
		goto err_disable_pclk;

	spin_lock_init(&master->xferqueue.lock);
	INIT_LIST_HEAD(&master->xferqueue.list);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, rcar_i3c_master_irq_handler, 0,
			       dev_name(&pdev->dev), master);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, master);

	master->maxdevs = RCAR_I3C_MAX_DEVS;
	master->free_pos = GENMASK(master->maxdevs - 1, 0);

	ret = i3c_master_register(&master->base, &pdev->dev,
				  &rcar_i3c_master_ops, false);
	if (ret)
		goto err_disable_tclk;

	dev_info(&pdev->dev, "register with i2c-scl = %ldHz i3c-scl = %ldHz\n",
		 master->base.bus.scl_rate.i2c,
		 master->base.bus.scl_rate.i3c);
	return 0;

err_disable_tclk:
	clk_disable_unprepare(master->tclk);

err_disable_pclk:
	clk_disable_unprepare(master->pclk);

	return ret;
}

static int rcar_i3c_master_remove(struct platform_device *pdev)
{
	struct rcar_i3c_master *master = platform_get_drvdata(pdev);
	int ret;

	ret = i3c_master_unregister(&master->base);
	if (ret)
		return ret;

	clk_disable_unprepare(master->tclk);
	clk_disable_unprepare(master->pclk);

	return 0;
}

static const struct of_device_id rcar_i3c_master_of_ids[] = {
	{ .compatible = "renesas,rcar-i3c-master"},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rcar_i3c_master_of_match);

static struct platform_driver rcar_i3c_master_driver = {
	.probe = rcar_i3c_master_probe,
	.remove = rcar_i3c_master_remove,
	.driver = {
		.name = "rcar-i3c-master",
		.of_match_table = of_match_ptr(rcar_i3c_master_of_ids),
	},
};
module_platform_driver(rcar_i3c_master_driver);

MODULE_DESCRIPTION("Renesas R-Car I3C master driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rcar-i3c-master");
