/*
 * linux/drivers/mmc/tmio_mmc_dma_gen3.c
 *
 * Copyright (C) 2015-2017 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * R-Car Gen3 DMA function for TMIO MMC implementations
 */

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>

#include "tmio_mmc.h"

/* DM_CM_DTRAN_MODE */
#define DTRAN_MODE_CH_NUM_CH0	0	/* "downstream" = for write commands */
#define DTRAN_MODE_CH_NUM_CH1	BIT(16)	/* "uptream" = for read commands */
#define DTRAN_MODE_BUS_WID_TH	(BIT(5) | BIT(4))
#define DTRAN_MODE_ADDR_MODE	BIT(0)	/* 1 = Increment address */

/* DM_CM_DTRAN_CTRL */
#define DTRAN_CTRL_DM_START	BIT(0)

/* DM_CM_RST */
#define RST_DTRANRST1		BIT(9)
#define RST_DTRANRST0		BIT(8)
#define RST_SEQRST		BIT(0)
#define RST_RESERVED_BITS	GENMASK_ULL(32, 0)

/* DM_CM_INFO1 and DM_CM_INFO1_MASK */
#define INFO1_CLEAR		0

/* DM_CM_INFO2 and DM_CM_INFO2_MASK */
#define INFO2_DTRANERR1		BIT(17)
#define INFO2_DTRANERR0		BIT(16)

void tmio_mmc_enable_dma(struct tmio_mmc_host *host, bool enable)
{
	if (!host->chan_tx || !host->chan_rx)
		return;

	if (!enable)
		tmio_dm_write(host, DM_CM_INFO1, INFO1_CLEAR);

	if (host->dma->enable) {
		host->dma_irq_mask =
			~(host->dma_tranend1 | DM_CM_INFO1_DTRAEND0);
		host->dma->enable(host, enable);
		tmio_dm_write(host, DM_CM_INFO1_MASK, host->dma_irq_mask);
	}
}

void tmio_mmc_abort_dma(struct tmio_mmc_host *host)
{
	u64 val = RST_DTRANRST1 | RST_DTRANRST0;

	dev_dbg(&host->pdev->dev, "%s\n", __func__);

	tmio_mmc_enable_dma(host, false);

	if (host->sequencer_enabled)
		val |= RST_SEQRST;
	tmio_dm_write(host, DM_CM_RST, RST_RESERVED_BITS & ~val);
	tmio_dm_write(host, DM_CM_RST, RST_RESERVED_BITS | val);

	tmio_mmc_enable_dma(host, true);

	if (host->bounce_sg_mapped) {
		dma_unmap_sg(&host->pdev->dev, &host->bounce_sg, 1,
			     DMA_FROM_DEVICE);
		host->bounce_sg_mapped = false;
	}
}

void tmio_mmc_reset_dma(struct tmio_mmc_host *host)
{
	u64 val = RST_DTRANRST1 | RST_DTRANRST0;

	if (host->sequencer_enabled)
		val |= RST_SEQRST;
	tmio_dm_write(host, DM_CM_RST, RST_RESERVED_BITS & ~val);
	tmio_dm_write(host, DM_CM_RST, RST_RESERVED_BITS | val);
}

void tmio_mmc_start_dma(struct tmio_mmc_host *host, struct mmc_data *data)
{
	struct scatterlist *sg = host->sg_ptr;
	u32 dtran_mode = DTRAN_MODE_BUS_WID_TH | DTRAN_MODE_ADDR_MODE;
	enum dma_data_direction dir;
	int ret;
	u32 irq_mask;

	if (!host->chan_rx || !host->chan_tx)
		return;

	/* This DMAC cannot handle if sg_len is not 1 */
	WARN_ON(host->sg_len > 1);

	dev_dbg(&host->pdev->dev, "%s: %d, %x\n", __func__, host->sg_len,
		data->flags);

	/* This DMAC cannot handle if buffer is not 8-bytes alignment */
	if (!IS_ALIGNED(sg->offset, 8)) {
		host->force_pio = true;
		tmio_mmc_enable_dma(host, false);
		return;
	}

	if (data->flags & MMC_DATA_READ) {
		dtran_mode |= DTRAN_MODE_CH_NUM_CH1;
		dir = DMA_FROM_DEVICE;
		irq_mask = TMIO_STAT_RXRDY;
	} else {
		dtran_mode |= DTRAN_MODE_CH_NUM_CH0;
		dir = DMA_TO_DEVICE;
		irq_mask = TMIO_STAT_TXRQ;
	}

	if (host->data->host_cookie != COOKIE_PRE_MAPPED) {
		ret = dma_map_sg(&host->pdev->dev, sg, host->sg_len, dir);
		if (ret < 0) {
			dev_err(&host->pdev->dev,
				"%s: dma_map_sg failed\n", __func__);
			return;
		}
	}

	tmio_clear_transtate(host);
	tmio_mmc_enable_dma(host, true);

	/* disable PIO irqs to avoid "PIO IRQ in DMA mode!" */
	tmio_mmc_disable_mmc_irqs(host, irq_mask);

	/* set dma parameters */
	tmio_dm_write(host, DM_CM_DTRAN_MODE, dtran_mode);
	tmio_dm_write(host, DM_DTRAN_ADDR, sg->dma_address);
}

#ifndef CONFIG_MMC_SDHI_PIO
static void tmio_mmc_issue_tasklet_fn(unsigned long arg)
{
	struct tmio_mmc_host *host = (struct tmio_mmc_host *)arg;

	dev_dbg(&host->pdev->dev, "%s\n", __func__);

	tmio_mmc_enable_mmc_irqs(host, TMIO_MASK_DMA);

	/* start the DMAC */
	tmio_dm_write(host, DM_CM_DTRAN_CTRL, DTRAN_CTRL_DM_START);
}

static void tmio_mmc_complete_tasklet_fn(unsigned long arg)
{
	struct tmio_mmc_host *host = (struct tmio_mmc_host *)arg;
	enum dma_data_direction dir;

	dev_dbg(&host->pdev->dev, "%s: %p\n", __func__, host->data);

	if (!host->data)
		return;

	if (host->data->flags & MMC_DATA_READ)
		dir = DMA_FROM_DEVICE;
	else
		dir = DMA_TO_DEVICE;

	tmio_mmc_enable_dma(host, false);
	if (host->data->host_cookie != COOKIE_PRE_MAPPED)
		dma_unmap_sg(&host->pdev->dev, host->sg_ptr, host->sg_len, dir);

	if (host->bounce_sg_mapped) {
		dma_unmap_sg(&host->pdev->dev, &host->bounce_sg, 1,
			     DMA_FROM_DEVICE);
		host->bounce_sg_mapped = false;
	}

	tmio_mmc_do_data_irq(host);
}

static void tmio_mmc_seq_complete_tasklet_fn(unsigned long arg)
{
	tmio_mmc_complete_tasklet_fn(arg);
}
#endif

/* DM_CM_SEQ_REGSET bits */
#define DM_CM_SEQ_REGSET_TABLE_NUM	BIT(8)

/* DM_CM_SEQ_CTRL bits */
#define DM_CM_SEQ_CTRL_SEQ_TABLE	BIT(28)
#define DM_CM_SEQ_CTRL_T_NUM		BIT(24)
#define DM_CM_SEQ_CTRL_SEQ_TYPE_SD	BIT(16)
#define DM_CM_SEQ_CTRL_START_NUM(x)	((x) << 12)
#define DM_CM_SEQ_CTRL_END_NUM(x)	((x) << 8)
#define DM_CM_SEQ_CTRL_SEQ_START	BIT(0)

/* DM_SEQ_CMD bits */
#define DM_SEQ_CMD_MULTI		BIT(13)
#define DM_SEQ_CMD_DIO			BIT(12)
#define DM_SEQ_CMD_CMDTYP		BIT(11)
#define DM_SEQ_CMD_RSP_NONE		(BIT(9) | BIT(8))
#define DM_SEQ_CMD_RSP_R1		BIT(10)
#define DM_SEQ_CMD_RSP_R1B		(BIT(10) | BIT(8))
#define DM_SEQ_CMD_RSP_R2		(BIT(10) | BIT(9))
#define DM_SEQ_CMD_RSP_R3		(BIT(10) | BIT(9) | BIT(8))
#define DM_SEQ_CMD_NONAUTOSTP		BIT(7)
#define DM_SEQ_CMD_APP			BIT(6)

#define MAX_CONTEXT_NUM			8

struct tmio_mmc_context {
	u64	seq_cmd;
	u64	seq_arg;
	u64	seq_size;
	u64	seq_seccnt;
	u64	seq_rsp;
	u64	seq_rsp_chk;
	u64	seq_addr;
};

static void tmio_mmc_set_seq_context(struct tmio_mmc_host *host, int ctxt_num,
				     struct tmio_mmc_context *ctxt)
{
	u64 val;

	WARN_ON(ctxt_num >= MAX_CONTEXT_NUM);

	/* set sequencer table/context number */
	if (ctxt_num < 4)
		val = ctxt_num;
	else
		val = DM_CM_SEQ_REGSET_TABLE_NUM | (ctxt_num - 4);
	tmio_dm_write(host, DM_CM_SEQ_REGSET, val);

	/* set command parameter */
	tmio_dm_write(host, DM_SEQ_CMD, ctxt->seq_cmd);
	tmio_dm_write(host, DM_SEQ_ARG, ctxt->seq_arg);
	tmio_dm_write(host, DM_SEQ_SIZE, ctxt->seq_size);
	tmio_dm_write(host, DM_SEQ_SECCNT, ctxt->seq_seccnt);
	tmio_dm_write(host, DM_SEQ_RSP, ctxt->seq_rsp);
	tmio_dm_write(host, DM_SEQ_RSP_CHK, ctxt->seq_rsp_chk);
	tmio_dm_write(host, DM_SEQ_ADDR, ctxt->seq_addr);
}

static int tmio_mmc_set_seq_table(struct tmio_mmc_host *host,
				  struct mmc_request *mrq,
				  struct scatterlist *sg,
				  bool ipmmu_on)
{
	struct mmc_card *card = host->mmc->card;
	struct mmc_data *data = mrq->data;
	struct scatterlist *sg_tmp;
	struct tmio_mmc_context ctxt;
	unsigned int blksz, blocks;
	u32 cmd_opcode, cmd_flag, cmd_arg;
	u32 sbc_opcode = 0, sbc_arg = 0;
	int i, ctxt_cnt = 0;

	/* SD_COMBO media not tested */
	cmd_opcode = (mrq->cmd->opcode & 0x3f);
	cmd_flag = DM_SEQ_CMD_CMDTYP;
	if (data->flags & MMC_DATA_READ)
		cmd_flag |= DM_SEQ_CMD_DIO;
	if (mmc_op_multi(mrq->cmd->opcode) ||
	    (cmd_opcode == SD_IO_RW_EXTENDED && mrq->cmd->arg & 0x08000000))
		cmd_flag |= DM_SEQ_CMD_MULTI;
	if (mrq->sbc || cmd_opcode == SD_IO_RW_EXTENDED)
		cmd_flag |= DM_SEQ_CMD_NONAUTOSTP;

	switch (mmc_resp_type(mrq->cmd)) {
	case MMC_RSP_NONE:
		cmd_flag |= DM_SEQ_CMD_RSP_NONE;
		break;
	case MMC_RSP_R1:
	case MMC_RSP_R1 & ~MMC_RSP_CRC:
		cmd_flag |= DM_SEQ_CMD_RSP_R1;
		break;
	case MMC_RSP_R1B:
		cmd_flag |= DM_SEQ_CMD_RSP_R1B;
		break;
	case MMC_RSP_R2:
		cmd_flag |= DM_SEQ_CMD_RSP_R2;
		break;
	case MMC_RSP_R3:
		cmd_flag |= DM_SEQ_CMD_RSP_R3;
		break;
	default:
		pr_debug("Unknown response type %d\n", mmc_resp_type(mrq->cmd));
		return -EINVAL;
	}

	cmd_arg = mrq->cmd->arg;
	if (cmd_opcode == SD_IO_RW_EXTENDED && cmd_arg & 0x08000000) {
		/* SDIO CMD53 block mode */
		cmd_arg &= ~0x1ff;
	}

	if (mrq->sbc) {
		sbc_opcode = (mrq->sbc->opcode & 0x3f) | DM_SEQ_CMD_RSP_R1;
		sbc_arg = mrq->sbc->arg & (MMC_CMD23_ARG_REL_WR |
			  MMC_CMD23_ARG_PACKED | MMC_CMD23_ARG_TAG_REQ);
	}

	blksz = data->blksz;
	if (ipmmu_on) {
		blocks = data->blocks;
		memset(&ctxt, 0, sizeof(ctxt));

		if (sbc_opcode) {
			/* set CMD23 */
			ctxt.seq_cmd = sbc_opcode;
			ctxt.seq_arg = sbc_arg | blocks;
			tmio_mmc_set_seq_context(host, ctxt_cnt, &ctxt);
			ctxt_cnt++;
		}

		/* set CMD */
		ctxt.seq_cmd = cmd_opcode | cmd_flag;
		ctxt.seq_arg = cmd_arg;
		if (cmd_opcode == SD_IO_RW_EXTENDED && cmd_arg & 0x08000000) {
			/* SDIO CMD53 block mode */
			ctxt.seq_arg |= blocks;
		}
		ctxt.seq_size = blksz;
		ctxt.seq_seccnt = blocks;
		ctxt.seq_addr = sg_dma_address(sg);
		tmio_mmc_set_seq_context(host, ctxt_cnt, &ctxt);
	} else {
		for_each_sg(sg, sg_tmp, host->sg_len, i) {
			blocks = sg_tmp->length / blksz;
			memset(&ctxt, 0, sizeof(ctxt));

			if (sbc_opcode) {
				/* set CMD23 */
				ctxt.seq_cmd = sbc_opcode;
				ctxt.seq_arg = sbc_arg | blocks;
				if (sbc_arg & MMC_CMD23_ARG_TAG_REQ && card &&
				    card->ext_csd.data_tag_unit_size &&
				    blksz * blocks <
				    card->ext_csd.data_tag_unit_size)
					ctxt.seq_arg &= ~MMC_CMD23_ARG_TAG_REQ;
				tmio_mmc_set_seq_context(host, ctxt_cnt, &ctxt);
				ctxt_cnt++;
			}

			/* set CMD */
			ctxt.seq_cmd = cmd_opcode | cmd_flag;
			ctxt.seq_arg = cmd_arg;
			if (cmd_opcode == SD_IO_RW_EXTENDED &&
			    cmd_arg & 0x08000000) {
				/* SDIO CMD53 block mode */
				ctxt.seq_arg |= blocks;
			}
			ctxt.seq_size = blksz;
			ctxt.seq_seccnt = blocks;
			ctxt.seq_addr = sg_dma_address(sg_tmp);
			tmio_mmc_set_seq_context(host, ctxt_cnt, &ctxt);

			if (i < (host->sg_len - 1)) {
				/* increment address */
				if (cmd_opcode == SD_IO_RW_EXTENDED) {
					/*
					 * sg_len should be 1 in SDIO CMD53
					 * byte mode
					 */
					WARN_ON(!(cmd_arg & 0x08000000));
					if (cmd_arg & 0x04000000) {
						/*
						 * SDIO CMD53 address
						 * increment mode
						 */
						cmd_arg +=
							(blocks * blksz) << 9;
					}
				} else {
					if (card && !mmc_card_blockaddr(card))
						cmd_arg += blocks * blksz;
					else
						cmd_arg += blocks;
				}
				ctxt_cnt++;
			}
		}
	}

	if (data->flags & MMC_DATA_READ) {
		/* dummy read */
		if (cmd_opcode == MMC_READ_MULTIPLE_BLOCK && card &&
		    blksz == 512 && data->blocks > 1) {
			memset(&ctxt, 0, sizeof(ctxt));
			if (sbc_opcode) {
				/* set CMD23 */
				ctxt.seq_cmd = sbc_opcode;
				ctxt.seq_arg = sbc_arg | 2;
				if (sbc_arg & MMC_CMD23_ARG_TAG_REQ &&
				    card->ext_csd.data_tag_unit_size &&
				    blksz * 2 <
				      card->ext_csd.data_tag_unit_size)
					ctxt.seq_arg &= ~MMC_CMD23_ARG_TAG_REQ;
				ctxt_cnt++;
				tmio_mmc_set_seq_context(host, ctxt_cnt, &ctxt);
			}

			/* set CMD18 */
			ctxt.seq_cmd = cmd_opcode | cmd_flag;
			ctxt.seq_arg = mrq->cmd->arg;
			if (!mmc_card_blockaddr(card))
				ctxt.seq_arg += (data->blocks - 2) * 512;
			else
				ctxt.seq_arg += data->blocks - 2;
			ctxt.seq_size = 512;
			ctxt.seq_seccnt = 2;
			ctxt.seq_addr = sg_dma_address(&host->bounce_sg);
			ctxt_cnt++;
			tmio_mmc_set_seq_context(host, ctxt_cnt, &ctxt);
		} else {
			if (cmd_opcode == SD_SWITCH) {
				/* set SD CMD6 twice  */
				ctxt.seq_addr =
					sg_dma_address(&host->bounce_sg);
			} else if ((card && (mmc_card_sdio(card) ||
				    card->type == MMC_TYPE_SD_COMBO)) ||
				   cmd_opcode == SD_IO_RW_EXTENDED) {
				/*
				 * In case of SDIO/SD_COMBO,
				 * read Common I/O Area 0x0-0x1FF twice.
				 */
				memset(&ctxt, 0, sizeof(ctxt));
				ctxt.seq_cmd = SD_IO_RW_EXTENDED |
					       DM_SEQ_CMD_CMDTYP |
					       DM_SEQ_CMD_DIO |
					       DM_SEQ_CMD_NONAUTOSTP |
					       DM_SEQ_CMD_RSP_R1;
				/*
				 * SD_IO_RW_EXTENDED argument format:
				 * [31] R/W flag -> 0
				 * [30:28] Function number -> 0x0 selects
				 *			      Common I/O Area
				 * [27] Block mode -> 0
				 * [26] Increment address -> 1
				 * [25:9] Regiser address -> 0x0
				 * [8:0] Byte/block count -> 0x0 -> 512Bytes
				 */
				ctxt.seq_arg = 0x04000000;
				ctxt.seq_size = 512;
				ctxt.seq_seccnt = 1;
				ctxt.seq_addr =
					sg_dma_address(&host->bounce_sg);
			} else {
				/* set CMD17 twice */
				memset(&ctxt, 0, sizeof(ctxt));
				ctxt.seq_cmd = MMC_READ_SINGLE_BLOCK |
					       DM_SEQ_CMD_CMDTYP |
					       DM_SEQ_CMD_DIO |
					       DM_SEQ_CMD_RSP_R1;
				if ((cmd_opcode == MMC_READ_SINGLE_BLOCK ||
				     cmd_opcode == MMC_READ_MULTIPLE_BLOCK) &&
				    blksz == 512)
					ctxt.seq_arg = mrq->cmd->arg;
				else
					ctxt.seq_arg = 0;
				ctxt.seq_size = 512;
				ctxt.seq_seccnt = 1;
				ctxt.seq_addr =
					sg_dma_address(&host->bounce_sg);
			}

			for (i = 0; i < 2; i++) {
				ctxt_cnt++;
				tmio_mmc_set_seq_context(host, ctxt_cnt, &ctxt);
			}
		}
	}

	return ctxt_cnt;
}

void tmio_mmc_start_sequencer(struct tmio_mmc_host *host)
{
	struct mmc_card *card = host->mmc->card;
	struct scatterlist *sg = host->sg_ptr, *sg_tmp;
	struct mmc_host *mmc = host->mmc;
	struct mmc_request *mrq = host->mrq;
	struct mmc_data *data = mrq->data;
	enum dma_data_direction dir;
	int ret, i, ctxt_num;
	u64 val;
	bool ipmmu_on = false;

	/* This DMAC cannot handle if sg_len larger than max_segs */
	if (mmc->max_segs == 1 || mmc->max_segs == 3)
		WARN_ON(host->sg_len > mmc->max_segs);
	else
		ipmmu_on = true;

	dev_dbg(&host->pdev->dev, "%s: %d, %x\n", __func__, host->sg_len,
		data->flags);

	if (!card && host->mrq->cmd->opcode == MMC_SEND_TUNING_BLOCK) {
		/*
		 * workaround: if card is NULL,
		 * we can not decide a dummy read command to be added
		 * to the CMD19.
		 */
		goto force_pio;
	}

	if (ipmmu_on) {
		if (!IS_ALIGNED(sg->offset, 8) ||
		    ((sg_dma_address(sg) + data->blksz * data->blocks) >
		    GENMASK_ULL(32, 0))) {
			dev_dbg(&host->pdev->dev, "%s: force pio\n", __func__);
			goto force_pio;
		}
		/*
		 * workaround: if we use IPMMU, sometimes unhandled error
		 * happened
		 */
		switch (host->mrq->cmd->opcode) {
		case MMC_SEND_TUNING_BLOCK_HS200:
		case MMC_SEND_TUNING_BLOCK:
			goto force_pio;
		default:
			break;
		}
	} else {
		for_each_sg(sg, sg_tmp, host->sg_len, i) {
			/*
			 * This DMAC cannot handle if buffer is not 8-bytes
			 * alignment
			 */
			if (!IS_ALIGNED(sg_tmp->offset, 8) ||
			    !IS_ALIGNED(sg_tmp->length, data->blksz) ||
			    ((sg_dma_address(sg_tmp) + sg_tmp->length) >
			    GENMASK_ULL(32, 0))) {
				dev_dbg(&host->pdev->dev, "%s: force pio\n",
					__func__);
				goto force_pio;
			}
		}
	}

	if (host->data->host_cookie != COOKIE_PRE_MAPPED) {
		if (data->flags & MMC_DATA_READ)
			dir = DMA_FROM_DEVICE;
		else
			dir = DMA_TO_DEVICE;

		ret = dma_map_sg(&host->pdev->dev, sg, host->sg_len, dir);
		if (ret <= 0) {
			dev_err(&host->pdev->dev,
				"%s: dma_map_sg failed\n", __func__);
			goto force_pio;
		}
	}

	if (data->flags & MMC_DATA_READ && !host->bounce_sg_mapped) {
		if (dma_map_sg(&host->pdev->dev, &host->bounce_sg, 1,
			       DMA_FROM_DEVICE) <= 0) {
			dev_err(&host->pdev->dev, "%s: bounce_sg map failed\n",
				__func__);
			goto unmap_sg;
		}
		host->bounce_sg_mapped = true;
	}

	tmio_mmc_enable_dma(host, true);
	/* set context */
	ctxt_num = tmio_mmc_set_seq_table(host, mrq, sg, ipmmu_on);
	if (ctxt_num < 0)
		goto unmap_sg;
	/* set dma mode */
	tmio_dm_write(host, DM_CM_DTRAN_MODE,
		      DTRAN_MODE_BUS_WID_TH);
	/* enable SEQEND irq */
	tmio_dm_write(host, DM_CM_INFO1_MASK,
		      GENMASK_ULL(32, 0) & ~DM_CM_INFO1_SEQEND);

	if (ctxt_num < 4) {
		/* issue table0 commands */
		val = DM_CM_SEQ_CTRL_SEQ_TYPE_SD |
		      DM_CM_SEQ_CTRL_START_NUM(0) |
		      DM_CM_SEQ_CTRL_END_NUM(ctxt_num) |
		      DM_CM_SEQ_CTRL_SEQ_START;
		tmio_dm_write(host, DM_CM_SEQ_CTRL, val);
	} else {
		/* issue table0 commands */
		val = DM_CM_SEQ_CTRL_SEQ_TYPE_SD |
		      DM_CM_SEQ_CTRL_T_NUM |
		      DM_CM_SEQ_CTRL_START_NUM(0) |
		      DM_CM_SEQ_CTRL_END_NUM(3) |
		      DM_CM_SEQ_CTRL_SEQ_START;
		tmio_dm_write(host, DM_CM_SEQ_CTRL, val);
		/* issue table1 commands */
		val = DM_CM_SEQ_CTRL_SEQ_TABLE |
		      DM_CM_SEQ_CTRL_SEQ_TYPE_SD |
		      DM_CM_SEQ_CTRL_T_NUM |
		      DM_CM_SEQ_CTRL_START_NUM(0) |
		      DM_CM_SEQ_CTRL_END_NUM(ctxt_num - 4) |
		      DM_CM_SEQ_CTRL_SEQ_START;
		tmio_dm_write(host, DM_CM_SEQ_CTRL, val);
	}

	return;

unmap_sg:
	if (host->data->host_cookie != COOKIE_PRE_MAPPED) {
		if (data->flags & MMC_DATA_READ)
			dir = DMA_FROM_DEVICE;
		else
			dir = DMA_TO_DEVICE;
		dma_unmap_sg(&host->pdev->dev, sg, host->sg_len, dir);
	}
	if (host->bounce_sg_mapped) {
		dma_unmap_sg(&host->pdev->dev, &host->bounce_sg, 1,
			     DMA_FROM_DEVICE);
		host->bounce_sg_mapped = false;
	}
force_pio:
	host->force_pio = true;
	tmio_mmc_enable_dma(host, false);

	return; /* return for PIO */
}

bool __tmio_mmc_dma_irq(struct tmio_mmc_host *host)
{
	unsigned int ireg, status;

	status = tmio_dm_read(host, DM_CM_INFO1);
	ireg = status & ~host->dma_irq_mask;

	if (ireg & DM_CM_INFO1_DTRAEND0) {
		tmio_dm_write(host, DM_CM_INFO1, ireg & ~DM_CM_INFO1_DTRAEND0);
		tmio_set_transtate(host, TMIO_TRANSTATE_DEND);
		return true;
	}

	if (ireg & host->dma_tranend1) {
		tmio_dm_write(host, DM_CM_INFO1, ireg & ~host->dma_tranend1);
		tmio_set_transtate(host, TMIO_TRANSTATE_DEND);
		return true;
	}
	return false;
}

void tmio_mmc_request_dma(struct tmio_mmc_host *host,
			  struct tmio_mmc_data *pdata)
{
#ifndef CONFIG_MMC_SDHI_PIO
	/* Each value is set to non-zero to assume "enabling" each DMA */
	host->chan_rx = host->chan_tx = (void *)0xdeadbeaf;

	if (soc_device_match(dma_quirks_match))
		host->dma_tranend1 = DM_CM_INFO1_DTRAEND1_BIT17;
	else /* ES 2.0 */
		host->dma_tranend1 = DM_CM_INFO1_DTRAEND1_BIT20;

	tasklet_init(&host->dma_complete, tmio_mmc_complete_tasklet_fn,
		     (unsigned long)host);
	tasklet_init(&host->dma_issue, tmio_mmc_issue_tasklet_fn,
		     (unsigned long)host);
	tasklet_init(&host->seq_complete, tmio_mmc_seq_complete_tasklet_fn,
		     (unsigned long)host);
	/* alloc bounce_buf for dummy read */
	host->bounce_buf = (u8 *)__get_free_page(GFP_KERNEL | GFP_DMA);
	if (!host->bounce_buf) {
		host->chan_rx = NULL;
		host->chan_tx = NULL;
		return;
	}
	/* setup bounce_sg for dummy read */
	sg_init_one(&host->bounce_sg, host->bounce_buf, 1024);
	host->bounce_sg_mapped = false;
#endif
}

void tmio_mmc_release_dma(struct tmio_mmc_host *host)
{
	/* Each value is set to zero to assume "disabling" each DMA */
	host->chan_rx = host->chan_tx = NULL;

	/* free bounce_buf for dummy read */
	if (host->bounce_buf) {
		if (host->bounce_sg_mapped) {
			dma_unmap_sg(&host->pdev->dev, &host->bounce_sg, 1,
				     DMA_FROM_DEVICE);
			host->bounce_sg_mapped = false;
		}
		free_pages((unsigned long)host->bounce_buf, 0);
		host->bounce_buf = NULL;
	}
}
