/*
 * Renesas RPC driver
 *
 * Copyright (C) 2018, Cogent Embedded Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mtd/spi-nor.h>

/* regs offsets */
#define CMNCR		0x0000
#define SSLDR		0x0004
#define DRCR		0x000C
#define DRCMR		0x0010
#define DREAR		0x0014
#define DROPR		0x0018
#define DRENR		0x001C
#define SMCR		0x0020
#define SMCMR		0x0024
#define SMADR		0x0028
#define SMOPR		0x002C
#define SMENR		0x0030
#define SMRDR0		0x0038
#define SMRDR1		0x003C
#define SMWDR0		0x0040
#define SMWDR1		0x0044
#define CMNSR		0x0048
#define DRDMCR		0x0058
#define DRDRENR		0x005C
#define SMDMCR		0x0060
#define SMDRENR		0x0064
#define PHYCNT		0x007C
#define PHYOFFSET1	0x0080
#define PHYOFFSET2	0x0084
#define PHYINT		0x0088
#define DIV_REG		0x00A8

/* CMNCR */
#define CMNCR_BSZ_MASK		(0x03)
#define CMNCR_BSZ_4x1		(0x0)
#define CMNCR_BSZ_8x1		(0x1)
#define CMNCR_BSZ_4x2		(0x1)
#define CMNCR_MD		(0x1 << 31)
#define CMNCR_MOIIO3_MASK	(0x3 << 22)
#define CMNCR_MOIIO3_HIZ	(0x3 << 22)
#define CMNCR_MOIIO2_MASK	(0x3 << 20)
#define CMNCR_MOIIO2_HIZ	(0x3 << 20)
#define CMNCR_MOIIO1_MASK	(0x3 << 18)
#define CMNCR_MOIIO1_HIZ	(0x3 << 18)
#define CMNCR_MOIIO0_MASK	(0x3 << 16)
#define CMNCR_MOIIO0_HIZ	(0x3 << 16)
#define CMNCR_IO0FV_MASK	(0x3 << 8)
#define CMNCR_IO0FV_HIZ		(0x3 << 8)

/* DRCR */
#define DRCR_RBURST_MASK	(0x1f << 16)
#define DRCR_RBURST(v)		(((v) & 0x1f) << 16)
#define DRCR_SSLE		(0x1)
#define DRCR_RBE		(0x1 << 8)
#define DRCR_RCF		(0x1 << 9)
#define DRCR_RBURST_32		(0x1f)

/* SMENR */
#define SMENR_CDB_MASK		(0x03 << 30)
#define SMENR_CDB(v)		(((v) & 0x03) << 30)
#define SMENR_CDB_1B		(0)
#define SMENR_CDB_2B		(0x1 << 30)
#define SMENR_CDB_4B		(0x2 << 30)
#define SMENR_OCDB_MASK		(0x03 << 28)
#define SMENR_OCDB_1B		(0)
#define SMENR_OCDB_2B		(0x1 << 28)
#define SMENR_OCDB_4B		(0x2 << 28)
#define SMENR_ADB_MASK		(0x03 << 24)
#define SMENR_ADB(v)		(((v) & 0x03) << 24)
#define SMENR_ADB_1B		(0)
#define SMENR_ADB_2B		(0x1 << 24)
#define SMENR_ADB_4B		(0x2 << 24)
#define SMENR_OPDB_MASK		(0x03 << 20)
#define SMENR_OPDB_1B		(0)
#define SMENR_OPDB_2B		(0x1 << 20)
#define SMENR_OPDB_4B		(0x2 << 20)
#define SMENR_SPIDB_MASK	(0x03 << 16)
#define SMENR_SPIDB(v)		(((v) & 0x03) << 16)
#define SMENR_SPIDB_1B		(0)
#define SMENR_SPIDB_2B		(0x1 << 16)
#define SMENR_SPIDB_4B		(0x2 << 16)
#define SMENR_OPDE_MASK		(0xf << 4)
#define SMENR_OPDE_DISABLE	(0)
#define SMENR_OPDE3		(0x8 << 4)
#define SMENR_OPDE32		(0xC << 4)
#define SMENR_OPDE321		(0xE << 4)
#define SMENR_OPDE3210		(0xF << 4)
#define SMENR_SPIDE_MASK	(0x0F)
#define SMENR_SPIDE_DISABLE	(0)
#define SMENR_SPIDE_8B		(0x08)
#define SMENR_SPIDE_16B		(0x0C)
#define SMENR_SPIDE_32B		(0x0F)
#define SMENR_DME		(1<<15)
#define SMENR_CDE		(1<<14)
#define SMENR_OCDE		(1<<12)
#define SMENR_ADE_MASK		(0xf << 8)
#define SMENR_ADE_DISABLE	(0)
#define SMENR_ADE_23_16		(0x4 << 8)
#define SMENR_ADE_23_8		(0x6 << 8)
#define SMENR_ADE_23_0		(0x7 << 8)
#define SMENR_ADE_31_0		(0xf << 8)

/* SMCMR */
#define SMCMR_CMD(cmd)		(((cmd) & 0xff) << 16)
#define SMCMR_CMD_MASK		(0xff << 16)
#define SMCMR_OCMD(cmd)		(((cmd) & 0xff))
#define SMCMR_OCMD_MASK		(0xff)

/* SMDRENR */
#define SMDRENR_HYPE_MASK	(0x7 << 12)
#define SMDRENR_HYPE_SPI_FLASH	(0x0)
#define SMDRENR_ADDRE		(0x1 << 8)
#define SMDRENR_OPDRE		(0x1 << 4)
#define SMDRENR_SPIDRE		(0x1)

/* PHYCNT */
#define PHYCNT_CAL		(0x1 << 31)
#define PHYCNT_OCTA_MASK	(0x3 << 22)
#define PHYCNT_EXDS		(0x1 << 21)
#define PHYCNT_OCT		(0x1 << 20)
#define PHYCNT_DDRCAL		(0x1 << 19)
#define PHYCNT_HS		(0x1 << 18)
#define PHYCNT_STREAM_MASK	(0x7 << 15)
#define PHYCNT_STREAM(o)	(((o) & 0x7) << 15)
#define PHYCNT_WBUF2		(0x1 << 4)
#define PHYCNT_WBUF		(0x1 << 2)
#define PHYCNT_PHYMEM_MASK	(0x3)

/* SMCR */
#define SMCR_SSLKP		(0x1 << 8)
#define SMCR_SPIRE		(0x1 << 2)
#define SMCR_SPIWE		(0x1 << 1)
#define SMCR_SPIE		(0x1)

/* CMNSR */
#define	CMNSR_TEND		(0x1 << 0)

/* SSLDR */
#define SSLDR_SPNDL(v)		(((v) & 0x7) << 16)
#define SSLDR_SLNDL(v)		((((v) | 0x4) & 0x7) << 8)
#define SSLDR_SCKDL(v)		((v) & 0x7)

/* DREAR */
#define DREAR_EAV_MASK		(0xff << 16)
#define DREAR_EAV(v)		(((v) & 0xff) << 16)
#define DREAR_EAC_MASK		(0x7)
#define DREAR_24B		(0)
#define DREAR_25B		(1)

/* DRENR */
#define DRENR_CDB_MASK		(0x03 << 30)
#define DRENR_CDB(v)		(((v) & 0x3) << 30)
#define DRENR_CDB_1B		(0)
#define DRENR_CDB_2B		(0x1 << 30)
#define DRENR_CDB_4B		(0x2 << 30)
#define DRENR_OCDB_MASK		(0x03 << 28)
#define DRENR_OCDB_1B		(0)
#define DRENR_OCDB_2B		(0x1 << 28)
#define DRENR_OCDB_4B		(0x2 << 28)
#define DRENR_ADB_MASK		(0x03 << 24)
#define DRENR_ADB(v)		(((v) & 0x3) << 24)
#define DRENR_ADB_1B		(0)
#define DRENR_ADB_2B		(0x1 << 24)
#define DRENR_ADB_4B		(0x2 << 24)
#define DRENR_OPDB_MASK		(0x03 << 20)
#define DRENR_OPDB_1B		(0)
#define DRENR_OPDB_2B		(0x1 << 20)
#define DRENR_OPDB_4B		(0x2 << 20)
#define DRENR_DRDB_MASK		(0x03 << 16)
#define DRENR_DRDB(v)		(((v) & 0x3) << 16)
#define DRENR_DRDB_1B		(0)
#define DRENR_DRDB_2B		(0x1 << 16)
#define DRENR_DRDB_4B		(0x2 << 16)
#define DRENR_OPDE_MASK		(0xf << 4)
#define DRENR_OPDE_DISABLE	(0)
#define DRENR_OPDE3		(0x8 << 4)
#define DRENR_OPDE32		(0xC << 4)
#define DRENR_OPDE321		(0xE << 4)
#define DRENR_OPDE3210		(0xF << 4)
#define DRENR_DME		(1<<15)
#define DRENR_CDE		(1<<14)
#define DRENR_OCDE		(1<<12)
#define DRENR_ADE_MASK		(0xf << 8)
#define DRENR_ADE_DISABLE	(0)
#define DRENR_ADE_23_0		(0x7 << 8)
#define DRENR_ADE_31_0		(0xf << 8)

/* DRCMR */
#define DRCMR_CMD(cmd)		(((cmd) & 0xff) << 16)
#define DRCMR_CMD_MASK		(0xff << 16)
#define DRCMR_OCMD(cmd)		(((cmd) & 0xff))
#define DRCMR_OCMD_MASK		(0xff)

/* DRCMR */
#define DRDMCR_DMCYC(v)		((v) & 0x1f)
#define DRDMCR_DMCYC_MASK	(0x1f)

/* SMDMCR */
#define SMDMCR_DMCYC(v)		((v) & 0x0f)
#define SMDMCR_DMCYC_MASK	(0x0f)

/* PHYOFFSET1 */
#define PHYOFFSET1_DDRTMG	(1 << 28)

/* DIVREG */
#define DIVREG_RATIO_MASK	(0x03)
#define DIVREG_RATIO(v)		((v) & 0x03)
#define DIVREG_RATIO_MAX	(0x2)


#define DEFAULT_TO		(100)
#define WRITE_BUF_SIZE		(0x100)
#define WRITE_BUF_ADR_MASK	(0xff)

#define REPEAT_MAX		(20)
#define REPEAT_TIME		(10)

struct rpc_spi {
	struct platform_device *pdev;
	void __iomem *base;
	void __iomem *read_area;
	void __iomem *write_area;
	dma_addr_t read_area_dma;
	struct completion comp;
	struct dma_chan	*dma_chan;
	struct clk *clk;
	unsigned int irq;
	struct spi_nor	spi_nor;

#define MTD_QSPI_1x	0
#define MTD_QSPI_2x	1

	u32 mtdtype;
};

/* IP block use it's own clock divigion register */
#define OWN_CLOCK_DIVIDER	BIT(0)

#define RPC_DMA_BURST		((DRCR_RBURST_32 + 1) << 3)
#define RPC_DMA_SIZE_MIN	(RPC_DMA_BURST << 3)

static bool use_dma = true;
module_param(use_dma, bool, 0);
MODULE_PARM_DESC(use_dma, "DMA support. 0 = Disable, 1 = Enable");

/* debug */
static void __maybe_unused regs_dump(struct rpc_spi *rpc)
{
	static u32 regs[] = {
		CMNCR, SSLDR, DRCR, DRCMR, DREAR,
		DROPR, DRENR, SMCR, SMCMR, SMADR,
		SMOPR, SMENR, SMRDR0, SMRDR1, SMWDR0,
		SMWDR1, CMNSR, DRDMCR, DRDRENR, SMDMCR,
		SMDRENR, PHYCNT, PHYOFFSET1, PHYOFFSET2,
		PHYINT
	};

	static const char *const names[] = {
		"CMNCR", "SSLDR", "DRCR", "DRCMR", "DREAR",
		"DROPR", "DRENR", "SMCR", "SMCMR", "SMADR",
		"SMOPR", "SMENR", "SMRDR0", "SMRDR1", "SMWDR0",
		"SMWDR1", "CMNSR", "DRDMCR", "DRDRENR", "SMDMCR",
		"SMDRENR", "PHYCNT", "PHYOFFSET1", "PHYOFFSET2",
		"PHYINT"
	};

	int i;

	dev_dbg(&rpc->pdev->dev, "RPC regs dump:\n");
	for (i = 0; i < ARRAY_SIZE(regs); i++)
		dev_dbg(&rpc->pdev->dev, "%s = 0x%08x\n", names[i],
			readl(rpc->base + regs[i]));
}

static void rpc_dma_complete_func(void *completion)
{
	complete(completion);
}

static int rpc_dma_read(struct rpc_spi *rpc, void *buf,
			loff_t from, ssize_t *plen)
{
	struct dma_device *dma_dev;
	enum dma_ctrl_flags flags;
	dma_addr_t dma_dst_addr;
	struct dma_async_tx_descriptor *tx = NULL;
	dma_cookie_t cookie;
	int retval = 0;
	ssize_t len;

	len = *plen;

	if (!rpc->dma_chan || len < RPC_DMA_SIZE_MIN)
		return -ENODEV;

	dma_dev = rpc->dma_chan->device;

	/* Align size to RBURST */
	len -= len % RPC_DMA_BURST;

	dma_dst_addr = dma_map_single(dma_dev->dev, buf, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(dma_dev->dev, dma_dst_addr)) {
		dev_err(&rpc->pdev->dev, "Failed to dma_map_single\n");
		return -ENXIO;
	}

	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	tx = dma_dev->device_prep_dma_memcpy(rpc->dma_chan, dma_dst_addr,
					     rpc->read_area_dma + from,
					     len, flags);
	if (!tx) {
		dev_err(&rpc->pdev->dev, "Failed to prepare DMA memcpy\n");
		retval = -EIO;
		goto out_dma;
	}

	init_completion(&rpc->comp);
	tx->callback = rpc_dma_complete_func;
	tx->callback_param = &rpc->comp;

	cookie = tx->tx_submit(tx);
	retval = dma_submit_error(cookie);
	if (retval) {
		dev_err(&rpc->pdev->dev, "Failed to do DMA tx_submit\n");
		goto out_dma;
	}

	dma_async_issue_pending(rpc->dma_chan);
	wait_for_completion(&rpc->comp);

	/* Update length with actual transfer size */
	*plen = len;

out_dma:
	dma_unmap_single(dma_dev->dev, dma_dst_addr, len, DMA_FROM_DEVICE);
	return retval;
}

/* register acces */
static u32 rpc_read(struct rpc_spi *rpc, unsigned int reg)
{
	u32 val;

	val = readl(rpc->base + reg);
	return val;
}

static void rpc_write(struct rpc_spi *rpc, unsigned int reg, u32 val)
{
	writel(val, rpc->base + reg);
}

static int rpc_wait(struct rpc_spi *rpc, u32 to)
{
	u32 val;
	int i;

	for (i = 0; i < to; i++) {
		val = rpc_read(rpc, CMNSR);
		val &= CMNSR_TEND;
		if (val)
			break;

		udelay(100);
	}

	if (i == to) {
		dev_err(&rpc->pdev->dev, "timeout waiting for operation end %d\n",
			rpc_read(rpc, CMNSR));
		return -ETIMEDOUT;
	}

	return 0;
}

static int rpc_setup_clk_ratio(struct rpc_spi *rpc, u32 max_clk_rate)
{
	unsigned long rate = clk_get_rate(rpc->clk);
	u32 ratio;
	u32 val;

	ratio = DIV_ROUND_UP(rate, max_clk_rate * 2) >> 1;
	if (ratio > DIVREG_RATIO_MAX)
		ratio = DIVREG_RATIO_MAX;

	val = rpc_read(rpc, DIV_REG);
	val &= DIVREG_RATIO_MASK;
	val |= DIVREG_RATIO(ratio);
	rpc_write(rpc, DIV_REG, val);

	return 0;
}

static int rpc_endisable_write_buf(struct rpc_spi *rpc, bool en)
{
	u32 val;

	val = rpc_read(rpc, PHYCNT);

	if (en)
		val |= PHYCNT_WBUF | PHYCNT_WBUF2;
	else
		val &= ~(PHYCNT_WBUF | PHYCNT_WBUF2);

	rpc_write(rpc, PHYCNT, val);

	return 0;
}

static int rpc_begin(struct rpc_spi *rpc,
		     bool rx, bool tx, bool last)
{
	u32 val = SMCR_SPIE;

	if (rx)
		val |= SMCR_SPIRE;

	if (tx)
		val |= SMCR_SPIWE;

	if (!last)
		val |= SMCR_SSLKP;

	rpc_write(rpc, SMCR, val);

	return 0;
}

static int rpc_setup_reg_mode(struct rpc_spi *rpc)
{
	u32 val;

	rpc_wait(rpc, DEFAULT_TO);

	rpc_endisable_write_buf(rpc, false);

	/* ...setup manual mode */
	val = rpc_read(rpc, CMNCR);
	val |=  CMNCR_MD;
	rpc_write(rpc, CMNCR, val);

	/* disable ddr */
	val = rpc_read(rpc, SMDRENR);
	val &= ~(SMDRENR_ADDRE | SMDRENR_OPDRE | SMDRENR_SPIDRE);
	rpc_write(rpc, SMDRENR, val);

	/* enable 1bit command */
	val = rpc_read(rpc, SMENR);
	val &= ~(SMENR_CDB_MASK | SMENR_OCDB_MASK | SMENR_DME
		 | SMENR_OCDE | SMENR_SPIDB_MASK
		 | SMENR_ADE_MASK | SMENR_ADB_MASK
		 | SMENR_OPDE_MASK | SMENR_SPIDE_MASK);
	val |= SMENR_CDB_1B | SMENR_CDE | SMENR_SPIDE_32B;
	rpc_write(rpc, SMENR, val);


	return 0;
}

static void rpc_flush_cache(struct rpc_spi *rpc)
{
	u32 val;

	val = rpc_read(rpc, DRCR);
	val |= DRCR_RCF;
	rpc_write(rpc, DRCR, val);
}

static int rpc_setup_ext_mode(struct rpc_spi *rpc)
{
	u32 val;
	u32 cmncr;

	rpc_wait(rpc, DEFAULT_TO);

	rpc_endisable_write_buf(rpc, false);

	/* ...setup ext mode */
	val = rpc_read(rpc, CMNCR);
	cmncr = val;
	val &=  ~(CMNCR_MD);
	rpc_write(rpc, CMNCR, val);

	/* ...enable burst and clear cache */
	val = rpc_read(rpc, DRCR);
	val &= ~(DRCR_RBURST_MASK | DRCR_RBE | DRCR_SSLE);
	val |= DRCR_RBURST(DRCR_RBURST_32) | DRCR_RBE;

	if (cmncr & CMNCR_MD)
		val |= DRCR_RCF;

	rpc_write(rpc, DRCR, val);

	return 0;
}

static int rpc_setup_data_size(struct rpc_spi *rpc, u32 size, bool copy)
{
	u32 val;

	val = rpc_read(rpc, SMENR);
	val &= ~(SMENR_SPIDE_MASK);

	if (rpc->mtdtype == MTD_QSPI_2x && !copy)
		size >>= 1;

	switch (size) {
	case 0:
		break;
	case 1:
		val |= SMENR_SPIDE_8B;
		break;
	case 2:
		val |= SMENR_SPIDE_16B;
		break;
	case 4:
		val |= SMENR_SPIDE_32B;
		break;
	default:
		dev_err(&rpc->pdev->dev, "Unsupported data width %d\n", size);
		return -EINVAL;
	}
	rpc_write(rpc, SMENR, val);

	return 0;
}

static int rpc_setup_extmode_read_addr(struct rpc_spi *rpc,
				       int adr_width, loff_t adr)
{
	u32 val;
	u32 v;

	val = rpc_read(rpc, DREAR);
	val &= ~(DREAR_EAV_MASK | DREAR_EAC_MASK);

	if (adr_width == 4) {
		v = adr >> 25;
		val |= DREAR_EAV(v) | DREAR_25B;
	}
	rpc_write(rpc, DREAR, val);

	val = rpc_read(rpc, DRENR);
	val &= ~(DRENR_ADE_MASK);
	if (adr_width == 4)
		val |= DRENR_ADE_31_0;
	else
		val |= DRENR_ADE_23_0;
	rpc_write(rpc, DRENR, val);

	return 0;
}

static inline int rpc_get_read_addr_nbits(u8 opcode)
{
	if (opcode == SPINOR_OP_READ_1_4_4_4B)
		return 4;
	return 1;
}

#define NBITS_TO_VAL(v)	((v >> 1) & 3)
static int rpc_setup_extmode_nbits(struct rpc_spi *rpc, int cnb,
				   int anb, int dnb)
{
	u32 val;

	val = rpc_read(rpc, DRENR);
	val &= ~(DRENR_CDB_MASK | DRENR_ADB_MASK | DRENR_DRDB_MASK);
	val |= DRENR_CDB(NBITS_TO_VAL(cnb))
		| DRENR_ADB(NBITS_TO_VAL(anb))
		| DRENR_DRDB(NBITS_TO_VAL(dnb));
	rpc_write(rpc, DRENR, val);

	return 0;
}

static int rpc_setup_writemode_nbits(struct rpc_spi *rpc, int cnb,
				     int anb, int dnb)
{
	u32 val;

	val = rpc_read(rpc, SMENR);
	val &= ~(SMENR_CDB_MASK | SMENR_ADB_MASK | SMENR_SPIDB_MASK);
	val |= SMENR_CDB(NBITS_TO_VAL(cnb))
		| SMENR_ADB(NBITS_TO_VAL(anb))
		| SMENR_SPIDB(NBITS_TO_VAL(dnb));
	rpc_write(rpc, SMENR, val);

	return 0;
}

static void rpc_setup_write_mode_command_and_adr(struct rpc_spi *rpc,
						 int adr_width, bool ena)
{
	u32 val;

	val = rpc_read(rpc, SMENR);
	val &= ~(SMENR_CDB_MASK | SMENR_CDE | SMENR_ADE_MASK);

	if (ena) {
		/* enable 1bit command */
		val |= SMENR_CDB_1B | SMENR_CDE;

		if (adr_width == 4)
			val |= SMENR_ADE_31_0;
		else
			val |= SMENR_ADE_23_0;
	}
	rpc_write(rpc, SMENR, val);
}

static int rpc_setup_write_mode(struct rpc_spi *rpc, u8 opcode)
{
	u32 val;

	rpc_wait(rpc, DEFAULT_TO);

	rpc_endisable_write_buf(rpc, true);

	/* ...setup manual mode */
	val = rpc_read(rpc, CMNCR);
	val |=  CMNCR_MD;
	rpc_write(rpc, CMNCR, val);

	/* disable ddr */
	val = rpc_read(rpc, SMDRENR);
	val &= ~(SMDRENR_ADDRE | SMDRENR_OPDRE | SMDRENR_SPIDRE);
	rpc_write(rpc, SMDRENR, val);

	val = rpc_read(rpc, SMENR);
	val &= ~(SMENR_OCDB_MASK | SMENR_DME | SMENR_OCDE | SMENR_SPIDB_MASK
		 | SMENR_ADB_MASK | SMENR_OPDE_MASK | SMENR_SPIDE_MASK);
	if (opcode != SPINOR_OP_PP)
		val |= SMENR_SPIDE_32B;
	else
		val |= SMENR_SPIDE_8B;

	rpc_write(rpc, SMENR, val);

	return 0;
}

static void rpc_read_manual_data(struct rpc_spi *rpc, u32 *pv0, u32 *pv1)
{
	u32 val0, val1, rd0, rd1;

	val0 = rpc_read(rpc, SMRDR0);
	val1 = rpc_read(rpc, SMRDR1);

	if (rpc->mtdtype == MTD_QSPI_2x) {
		rd1 = (val0 & 0xff000000) | ((val0 << 8) & 0xff0000) |
			((val1 >> 16) & 0xff00) | ((val1 >> 8) & 0xff);
		rd0 = ((val0 & 0xff0000) << 8) | ((val0 << 16) & 0xff0000) |
			((val1 >> 8) & 0xff00) | (val1 & 0xff);
	} else
		rd0 = val0;

	if (pv0)
		*pv0 = rd0;

	if (pv1 && rpc->mtdtype == MTD_QSPI_2x)
		*pv1 = rd1;
}

static int rpc_datalen2trancfersize(struct rpc_spi *rpc, int len, bool copy)
{
	int sz = len;

	if (len >= 2)
		sz = 2;

	if (len >= 4)
		sz = 4;

	if (rpc->mtdtype == MTD_QSPI_2x && len >= 8 && !copy)
		sz = 8;

	return sz;
}

static int __rpc_write_data2reg(struct rpc_spi *rpc, int off,
				const u8 *buf, int sz)
{
	const u32 *b32 = (const u32 *)buf;
	const u16 *b16 = (const u16 *)buf;

	if (sz == 4)
		rpc_write(rpc, off, *b32);
	else if (sz == 2)
		writew(*b16, rpc->base + off);
	else if (sz == 1)
		writeb(*buf, rpc->base + off);
	else if (sz != 0) {
		dev_err(&rpc->pdev->dev, "incorrect data size %d\n", sz);
		return -EINVAL;
	}

	return 0;
}

#define __SETVAL(x)	((((x) & 0xff) << 8) | ((x) & 0xff))
static int rpc_write_data2reg(struct rpc_spi *rpc, const u8 *buf,
			      int sz, bool copy)
{
	int i, ret;
	u32 v = 0;

	if (rpc->mtdtype == MTD_QSPI_2x) {
		if (copy) {
			for (i = 0; i < sz && i < 2; i++)
				v |= (__SETVAL(buf[i]) << 16*i);

			ret = __rpc_write_data2reg(rpc,
						   sz == 4 ? SMWDR1 : SMWDR0,
						   (u8 *)&v,
						   sz == 4 ? sz : sz * 2);
			if (ret)
				return ret;

			v = 0;
			for (; i < sz; i++)
				v |= (__SETVAL(buf[i]) << 16*i);


			ret = __rpc_write_data2reg(rpc,
						   sz == 4 ? SMWDR0 : SMWDR1,
						   (u8 *)&v,
						   sz == 4 ? sz : sz * 2);
			if (ret)
				return ret;

			return 0;
		}

		sz >>= 1;
		ret = __rpc_write_data2reg(rpc,
					   sz == 4 ? SMWDR1 : SMWDR0,
					   buf,
					   sz == 4 ? sz : sz * 2);
		if (ret)
			return ret;
		buf += sz;

		return __rpc_write_data2reg(rpc,
					    sz == 4 ? SMWDR0 : SMWDR1,
					    buf, sz == 4 ? sz : sz * 2);
	}

	return __rpc_write_data2reg(rpc, SMWDR0, buf, sz);
}

static ssize_t rpc_write_unaligned(struct spi_nor *nor, loff_t to, size_t len,
				   const u_char *buf, size_t fullen)
{
	int ret = len, dsize;
	struct rpc_spi *rpc = nor->priv;
	bool copy = false, last;
	loff_t _to;

	rpc_endisable_write_buf(rpc, false);

	while (len > 0) {
		_to = to;
		if (rpc->mtdtype == MTD_QSPI_2x)
			_to >>= 1;
		rpc_write(rpc, SMADR, _to);
		dsize = rpc_datalen2trancfersize(rpc, len, copy);

		if (rpc_setup_data_size(rpc, dsize, copy))
			return -EINVAL;

		rpc_write_data2reg(rpc, buf, dsize, copy);

		last = (len <= dsize && fullen <= ret);
		rpc_begin(rpc, false, true, last);
		if (rpc_wait(rpc, DEFAULT_TO))
			return -ETIMEDOUT;

		/* ...disable command */
		rpc_setup_write_mode_command_and_adr(rpc,
					nor->addr_width, false);

		buf += dsize;
		len -= dsize;
		to += dsize;
	}

	return ret;
}

static ssize_t rpc_write_flash(struct spi_nor *nor, loff_t to, size_t len,
			       const u_char *buf)
{
	ssize_t res = len, full = len;
	u32 val;
	u8 rval[2];
	struct rpc_spi *rpc = nor->priv;
	loff_t bo;
	loff_t offset;
	bool is_rounded = false;

	/* ...len should be rounded to 2 bytes */
	if (rpc->mtdtype == MTD_QSPI_2x && (len & 1)) {
		is_rounded = true;
		len &= ~(1);
	}

	bo = to & (WRITE_BUF_ADR_MASK);

	rpc_flush_cache(rpc);
	rpc_setup_write_mode(rpc, nor->program_opcode);
	rpc_setup_write_mode_command_and_adr(rpc, nor->addr_width, true);
	rpc_setup_writemode_nbits(rpc, 1, 1, 1);

	/* ...setup command */
	val = rpc_read(rpc, SMCMR);
	val &= ~(SMCMR_CMD_MASK);
	val |= SMCMR_CMD(nor->program_opcode);
	rpc_write(rpc, SMCMR, val);

	offset = (to & (~WRITE_BUF_ADR_MASK));

	/* ...write unaligned first bytes */
	if (bo) {
		size_t min = (len < (WRITE_BUF_SIZE - bo)) ? len : (WRITE_BUF_SIZE - bo);

		rpc_write_unaligned(nor, to, min, buf, full);
		rpc_setup_write_mode(rpc, nor->program_opcode);

		len -= min;
		buf += min;
		to += min;
		full -= min;
	}

	/*
	 * TODO: Unfortunately RPC does not write properly in write buf mode
	 * without transferring command. Investigate this.
	 */

	if (len) {
		rpc_write_unaligned(nor, to, len, buf, full);
		buf += len;
		to += len;
		full -= len;
		len = 0;
	}

	if (is_rounded) {
		rval[0] = *buf;
		rval[1] = 0xFF;
		rpc_write_unaligned(nor, to, 2, rval, full);
	}

	rpc_flush_cache(rpc);

	return res;
}

static inline unsigned int rpc_rx_nbits(struct spi_nor *nor)
{
	return spi_nor_get_protocol_data_nbits(nor->read_proto);
}

#define READ_ADR_MASK		(BIT(26) - 1)
static ssize_t rpc_read_flash(struct spi_nor *nor, loff_t from, size_t len,
			      u_char *buf)
{
	u32 val;
	struct rpc_spi *rpc = nor->priv;
	int adr_width = nor->addr_width;
	int opcode_nbits = 1;
	int addr_nbits = rpc_get_read_addr_nbits(nor->read_opcode);
	int data_nbits = rpc_rx_nbits(nor);
	int dummy = nor->read_dummy - 1;
	ssize_t ret = len;
	ssize_t readlen;
	loff_t _from;

	rpc_setup_ext_mode(rpc);
	/* ...setup n bits */
	rpc_setup_extmode_nbits(rpc, opcode_nbits, addr_nbits, data_nbits);

	/* TODO: setup DDR */

	/* ...setup command */
	val = rpc_read(rpc, DRCMR);
	val &= ~(DRCMR_CMD_MASK);
	val |= DRCMR_CMD(nor->read_opcode);
	rpc_write(rpc, DRCMR, val);

	/* ...setup dummy cycles */
	val = rpc_read(rpc, DRDMCR);
	val &= ~(DRDMCR_DMCYC_MASK);
	val |= DRDMCR_DMCYC(dummy);
	rpc_write(rpc, DRDMCR, val);

	/* ...setup read sequence */
	val = rpc_read(rpc, DRENR);
	val |= DRENR_DME | DRENR_CDE;
	rpc_write(rpc, DRENR, val);

	while (len > 0) {
		int retval;

		/* ...setup address */
		rpc_setup_extmode_read_addr(rpc, adr_width, from);
		/* ...use adr [25...0] */
		_from = from & READ_ADR_MASK;

		readlen = READ_ADR_MASK - _from + 1;
		readlen = readlen > len ? len : readlen;

		retval = rpc_dma_read(rpc, buf, _from, &readlen);
		if (retval)
			memcpy_fromio(buf, rpc->read_area + _from, readlen);

		buf += readlen;
		from += readlen;
		len -= readlen;
	}

	return ret;
}

static int __rpc_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	u32 val;
	u32 val2 = 0;
	u32 *buf32;
	int i;
	u32 mask = 0, type;
	struct rpc_spi *rpc = nor->priv;

	type = rpc->mtdtype;

	rpc_setup_reg_mode(rpc);
	val = rpc_read(rpc, SMCMR);
	val &= ~(SMCMR_CMD_MASK);
	val |= SMCMR_CMD(opcode);
	rpc_write(rpc, SMCMR, val);

	rpc_begin(rpc, true, false, len <= 4);
	if (rpc_wait(rpc, DEFAULT_TO))
		return -ETIMEDOUT;

	/* ...disable command */
	val = rpc_read(rpc, SMENR);
	val &= ~(SMENR_CDE);
	rpc_write(rpc, SMENR, val);

	buf32 = (u32 *)buf;

	while (len > 0) {
		rpc_read_manual_data(rpc, &val, &val2);

		if (mask) {
			dev_warn(&rpc->pdev->dev,
				"Using mask workaround (0x%x)\n", mask);
			val &= ~(mask);
			val2 &= ~(mask);
		}

		/* ... spi flashes should be the same */
		if (type == MTD_QSPI_2x && val != val2) {
			/* clear cs */
			rpc_begin(rpc, true, false, true);
			return -EAGAIN;
		}

		if (len > 4) {
			*buf32 = val;
			buf32++;
			len -= 4;
		} else {
			buf = (u8 *)buf32;
			for (i = 0; i < len; i++) {
				*buf = (val >> (8 * i)) & 0x000000ff;
				buf++;
			}
			len = 0;
		}

		if (!len)
			break;

		mask = 0xff;

		rpc_begin(rpc, true, false, len <= 4);
		if (rpc_wait(rpc, DEFAULT_TO))
			return -ETIMEDOUT;

	}

	return 0;
}

static int rpc_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	int i, ret;

	/* A few read commands like read status can
	 * generate different answers. We repeat reading
	 * in that case
	 */
	for (i = 0; i < REPEAT_MAX; i++) {
		ret = __rpc_read_reg(nor, opcode, buf, len);
		if (!ret || ret != -EAGAIN)
			break;
		mdelay(REPEAT_TIME);
	}

	return ret;
}

static int rpc_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct rpc_spi *rpc = nor->priv;
	u32 val;
	int dsize;
	bool copy = true;

	rpc_setup_reg_mode(rpc);

	val = rpc_read(rpc, SMCMR);
	val &= ~(SMCMR_CMD_MASK);
	val |= SMCMR_CMD(opcode);
	rpc_write(rpc, SMCMR, val);

	dsize = rpc_datalen2trancfersize(rpc, len, copy);

	if (rpc_setup_data_size(rpc, dsize, copy))
		return -EINVAL;

	if (rpc_write_data2reg(rpc, buf, dsize, copy))
		return -EINVAL;
	buf += dsize;
	len -= dsize;
	rpc_begin(rpc, false, dsize > 0, len == 0);

	if (rpc_wait(rpc, DEFAULT_TO))
		return -ETIMEDOUT;

	/* ...disable command */
	val = rpc_read(rpc, SMENR);
	val &= ~(SMENR_CDE);
	rpc_write(rpc, SMENR, val);

	while (len > 0) {
		dsize = rpc_datalen2trancfersize(rpc, len, copy);
		if (rpc_setup_data_size(rpc, dsize, copy))
			return -EINVAL;
		rpc_write_data2reg(rpc, buf, dsize, copy);
		buf += dsize;
		len -= dsize;

		rpc_begin(rpc, false, dsize, len == 0);

		if (rpc_wait(rpc, DEFAULT_TO))
			return -ETIMEDOUT;

	}

	return 0;
}

/* hw init for spi-nor flashes */
static int rpc_hw_init_1x2x(struct rpc_spi *rpc)
{
	u32 val;

	/* Exec calibration */
	val = rpc_read(rpc, PHYCNT);
	val &= ~(PHYCNT_OCTA_MASK | PHYCNT_EXDS | PHYCNT_OCT
		 | PHYCNT_DDRCAL | PHYCNT_HS | PHYCNT_STREAM_MASK
		 | PHYCNT_WBUF2 | PHYCNT_WBUF | PHYCNT_PHYMEM_MASK);
	val |= (PHYCNT_CAL) | PHYCNT_STREAM(6);
	rpc_write(rpc, PHYCNT, val);

	/* disable rpc_* pins  */
	val = rpc_read(rpc, PHYINT);
	val &= ~((1<<24) | (7<<16));
	rpc_write(rpc, PHYINT, val);

	val = rpc_read(rpc, SMDRENR);
	val &= ~(SMDRENR_HYPE_MASK);
	val |= SMDRENR_HYPE_SPI_FLASH;
	rpc_write(rpc, SMDRENR, val);

	val = rpc_read(rpc, CMNCR);
	val &=  ~(CMNCR_BSZ_MASK);
	if (rpc->mtdtype != MTD_QSPI_1x)
		val |= CMNCR_BSZ_4x2;
	rpc_write(rpc, CMNCR, val);

	val = rpc_read(rpc, PHYOFFSET1);
	val |= PHYOFFSET1_DDRTMG;
	rpc_write(rpc, PHYOFFSET1, val);

	val = SSLDR_SPNDL(0) | SSLDR_SLNDL(4) | SSLDR_SCKDL(0);
	rpc_write(rpc, SSLDR, val);

	return 0;
}

static int rpc_hw_init(struct rpc_spi *rpc)
{
	switch (rpc->mtdtype) {
	case MTD_QSPI_1x:
	case MTD_QSPI_2x:
		return rpc_hw_init_1x2x(rpc);

	default:
		dev_err(&rpc->pdev->dev, "Unsupported connection mode\n");
		return -ENODEV;
	}
}

static int rpc_erase_sector(struct spi_nor *nor, loff_t addr)
{
	struct rpc_spi *rpc = nor->priv;
	u8 buf[6];
	int i;

	if (rpc->mtdtype == MTD_QSPI_2x)
		addr >>= 1;

	for (i = nor->addr_width - 1; i >= 0; i--) {
		buf[i] = addr & 0xff;
		addr >>= 8;
	}

	return nor->write_reg(nor, nor->erase_opcode, buf, nor->addr_width);
}

static const struct of_device_id rpc_of_match[] = {
	{ .compatible = "renesas,qspi-rpc-r8a77980" },
	{ .compatible = "renesas,qspi-rpc-r8a77970", .data = (void *)OWN_CLOCK_DIVIDER },
	{ },
};

MODULE_DEVICE_TABLE(of, rpc_of_match);

static int rpc_spi_probe(struct platform_device *pdev)
{
	struct device_node *flash_np;
	struct spi_nor *nor;
	struct rpc_spi *rpc;
	struct resource *res;
	struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ |
			SNOR_HWCAPS_READ_FAST |
			SNOR_HWCAPS_PP,
	};
	u32 max_clk_rate = 50000000;
	u32 property;
	int ret;
	int own_clk;


	flash_np = of_get_next_available_child(pdev->dev.of_node, NULL);
	if (!flash_np) {
		dev_err(&pdev->dev, "no SPI flash device to configure\n");
		return -ENODEV;
	}

	if (!of_property_read_u32(flash_np, "spi-rx-bus-width", &property)) {
		switch (property) {
		case 1:
			break;
		case 2:
			hwcaps.mask |= SNOR_HWCAPS_READ_DUAL;
			break;
		case 4:
			hwcaps.mask |= SNOR_HWCAPS_READ_QUAD;
			break;
		default:
			dev_err(&pdev->dev, "unsupported rx-bus-width\n");
			return -EINVAL;
		}
	}

	of_property_read_u32(flash_np, "spi-max-frequency", &max_clk_rate);
	own_clk = (of_device_get_match_data(&pdev->dev) == (void *)OWN_CLOCK_DIVIDER);

	rpc = devm_kzalloc(&pdev->dev, sizeof(*rpc), GFP_KERNEL);
	if (!rpc)
		return -ENOMEM;

	rpc->pdev = pdev;

	/* ... setup nor hooks */
	nor = &rpc->spi_nor;
	nor->dev = &pdev->dev;
	spi_nor_set_flash_node(nor, flash_np);
	nor->read  = rpc_read_flash;
	nor->write = rpc_write_flash;
	nor->read_reg  = rpc_read_reg;
	nor->write_reg = rpc_write_reg;
	nor->priv = rpc;
	rpc->mtdtype = MTD_QSPI_1x;

	if (of_find_property(pdev->dev.of_node, "dual", NULL)) {
		rpc->mtdtype = MTD_QSPI_2x;
		nor->erase = rpc_erase_sector;
	}

	/* ...get memory */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rpc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rpc->base)) {
		dev_err(&pdev->dev, "cannot get resources\n");
		ret = PTR_ERR(rpc->base);
		goto error;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	rpc->read_area_dma = res->start;
	rpc->read_area = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rpc->base)) {
		dev_err(&pdev->dev, "cannot get resources\n");
		ret = PTR_ERR(rpc->base);
		goto error;
	}

	/* ...get memory */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	rpc->write_area = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rpc->base)) {
		dev_err(&pdev->dev, "cannot get resources\n");
		ret = PTR_ERR(rpc->base);
		goto error;
	}

	/* ...get clk */
	rpc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(rpc->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(rpc->clk);
		goto error;
	}

	/* ...set max clk rate */
	if (!own_clk) {
		ret = clk_set_rate(rpc->clk, max_clk_rate);
		if (ret) {
			dev_err(&pdev->dev, "cannot set clock rate\n");
			goto error;
		}
	}

	/* ... enable clk */
	ret = clk_prepare_enable(rpc->clk);
	if (ret) {
		dev_err(&pdev->dev, "cannot prepare clock\n");
		goto error;
	}

	/* ...init device */
	ret = rpc_hw_init(rpc);
	if (ret < 0) {
		dev_err(&pdev->dev, "rpc_hw_init error.\n");
		goto error_clk_disable;
	}

	/* ...set clk ratio */
	if (own_clk) {
		ret = rpc_setup_clk_ratio(rpc, max_clk_rate);
		if (ret) {
			dev_err(&pdev->dev, "cannot set clock ratio\n");
			goto error;
		}
	}

	platform_set_drvdata(pdev, rpc);

	ret = spi_nor_scan(nor, NULL, &hwcaps);
	if (ret) {
		dev_err(&pdev->dev, "spi_nor_scan error.\n");
		goto error_clk_disable;
	}

	/* Dual mode support */
	if (rpc->mtdtype == MTD_QSPI_2x) {
		nor->page_size <<= 1;
		nor->mtd.erasesize <<= 1;
		nor->mtd.size <<= 1;
		nor->mtd.writebufsize <<= 1;
	}

	/* Workaround data size limitation */
	if (nor->page_size > WRITE_BUF_SIZE) {
		nor->page_size = WRITE_BUF_SIZE;
		nor->mtd.writebufsize = WRITE_BUF_SIZE;
	}

	if (use_dma) {
		dma_cap_mask_t mask;

		dma_cap_zero(mask);
		dma_cap_set(DMA_MEMCPY, mask);
		rpc->dma_chan = dma_request_channel(mask, NULL, NULL);
		if (!rpc->dma_chan)
			dev_warn(&pdev->dev, "Failed to request DMA channel\n");
		else
			dev_info(&pdev->dev, "Using DMA read (%s)\n",
				 dma_chan_name(rpc->dma_chan));
	}

	ret = mtd_device_register(&nor->mtd, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "mtd_device_register error.\n");
		goto error_dma;
	}

	dev_info(&pdev->dev, "probed as %s\n",
		rpc->mtdtype == MTD_QSPI_1x ? "single" : "dual");

	return 0;

error_dma:
	if (rpc->dma_chan)
		dma_release_channel(rpc->dma_chan);
error_clk_disable:
	clk_disable_unprepare(rpc->clk);
error:
	return ret;
}

static int rpc_spi_remove(struct platform_device *pdev)
{
	struct rpc_spi *rpc = platform_get_drvdata(pdev);

	/* HW shutdown */
	clk_disable_unprepare(rpc->clk);
	mtd_device_unregister(&rpc->spi_nor.mtd);
	if (rpc->dma_chan)
		dma_release_channel(rpc->dma_chan);
	return 0;
}

/* platform driver interface */
static struct platform_driver rpc_platform_driver = {
	.probe		= rpc_spi_probe,
	.remove		= rpc_spi_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "rpc",
		.of_match_table = of_match_ptr(rpc_of_match),
	},
};

module_platform_driver(rpc_platform_driver);

MODULE_ALIAS("rpc");
MODULE_AUTHOR("Cogent Embedded Inc. <sources@cogentembedded.com>");
MODULE_DESCRIPTION("Renesas RPC Driver");
MODULE_LICENSE("GPL");
