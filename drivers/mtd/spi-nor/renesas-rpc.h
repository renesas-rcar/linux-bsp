/*
 * Renesas RPC driver
 *
 * Copyright (C) 2019, Cogent Embedded Inc.
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

#ifndef __RENESAS_RPC_H__
#define __RENESAS_RPC_H__

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

/* RPC */
#define RPC_CMNCR		0x0000	/* R/W */
#define RPC_CMNCR_MD		(0x1 << 31)
#define RPC_CMNCR_MOIIO0(val)	(((val) & 0x3) << 16)
#define RPC_CMNCR_MOIIO1(val)	(((val) & 0x3) << 18)
#define RPC_CMNCR_MOIIO2(val)	(((val) & 0x3) << 20)
#define RPC_CMNCR_MOIIO3(val)	(((val) & 0x3) << 22)
#define RPC_CMNCR_MOIIO_HIZ	(RPC_CMNCR_MOIIO0(3) | RPC_CMNCR_MOIIO1(3) | \
				 RPC_CMNCR_MOIIO2(3) | RPC_CMNCR_MOIIO3(3))
#define RPC_CMNCR_IO0FV(val)	(((val) & 0x3) << 8)
#define RPC_CMNCR_IO2FV(val)	(((val) & 0x3) << 12)
#define RPC_CMNCR_IO3FV(val)	(((val) & 0x3) << 14)
#define RPC_CMNCR_IOFV_HIZ	(RPC_CMNCR_IO0FV(3) | RPC_CMNCR_IO2FV(3) | \
				 RPC_CMNCR_IO3FV(3))
#define RPC_CMNCR_BSZ(val)	(((val) & 0x3) << 0)

#define RPC_SSLDR		0x0004	/* R/W */
#define RPC_SSLDR_SPNDL(d)	(((d) & 0x7) << 16)
#define RPC_SSLDR_SLNDL(d)	(((d) & 0x7) << 8)
#define RPC_SSLDR_SCKDL(d)	(((d) & 0x7) << 0)

#define RPC_DRCR		0x000C	/* R/W */
#define RPC_DRCR_SSLN		(0x1 << 24)
#define RPC_DRCR_RBURST(v)	(((v) & 0x1F) << 16)
#define RPC_DRCR_RCF		(0x1 << 9)
#define RPC_DRCR_RBE		(0x1 << 8)
#define RPC_DRCR_SSLE		(0x1 << 0)

#define RPC_DRCMR		0x0010	/* R/W */
#define RPC_DRCMR_CMD(c)	(((c) & 0xFF) << 16)
#define RPC_DRCMR_OCMD(c)	(((c) & 0xFF) << 0)

#define RPC_DREAR		0x0014	/* R/W */
#define RPC_DREAR_EAV(v)	(((v) & 0xFF) << 16)
#define RPC_DREAR_EAC(v)	(((v) & 0x7) << 0)

#define RPC_DROPR		0x0018	/* R/W */
#define RPC_DROPR_OPD3(o)	(((o) & 0xFF) << 24)
#define RPC_DROPR_OPD2(o)	(((o) & 0xFF) << 16)
#define RPC_DROPR_OPD1(o)	(((o) & 0xFF) << 8)
#define RPC_DROPR_OPD0(o)	(((o) & 0xFF) << 0)

#define RPC_DRENR		0x001C	/* R/W */
#define RPC_DRENR_CDB(o)	(((o) & 0x3) << 30)
#define RPC_DRENR_OCDB(o)	(((o) & 0x3) << 28)
#define RPC_DRENR_ADB(o)	(((o) & 0x3) << 24)
#define RPC_DRENR_OPDB(o)	(((o) & 0x3) << 20)
#define RPC_DRENR_DRDB(o)	(((o) & 0x3) << 16)
#define RPC_DRENR_DME		(0x1 << 15)
#define RPC_DRENR_CDE		(0x1 << 14)
#define RPC_DRENR_OCDE		(0x1 << 12)
#define RPC_DRENR_ADE(v)	(((v) & 0xF) << 8)
#define RPC_DRENR_OPDE(v)	(((v) & 0xF) << 4)

#define RPC_SMCR		0x0020	/* R/W */
#define RPC_SMCR_SSLKP		(0x1 << 8)
#define RPC_SMCR_SPIRE		(0x1 << 2)
#define RPC_SMCR_SPIWE		(0x1 << 1)
#define RPC_SMCR_SPIE		(0x1 << 0)

#define RPC_SMCMR		0x0024	/* R/W */
#define RPC_SMCMR_CMD(c)	(((c) & 0xFF) << 16)
#define RPC_SMCMR_OCMD(c)	(((c) & 0xFF) << 0)

#define RPC_SMADR		0x0028	/* R/W */
#define RPC_SMOPR		0x002C	/* R/W */
#define RPC_SMOPR_OPD0(o)	(((o) & 0xFF) << 0)
#define RPC_SMOPR_OPD1(o)	(((o) & 0xFF) << 8)
#define RPC_SMOPR_OPD2(o)	(((o) & 0xFF) << 16)
#define RPC_SMOPR_OPD3(o)	(((o) & 0xFF) << 24)

#define RPC_SMENR		0x0030	/* R/W */
#define RPC_SMENR_CDB(o)	(((o) & 0x3) << 30)
#define RPC_SMENR_OCDB(o)	(((o) & 0x3) << 28)
#define RPC_SMENR_ADB(o)	(((o) & 0x3) << 24)
#define RPC_SMENR_OPDB(o)	(((o) & 0x3) << 20)
#define RPC_SMENR_SPIDB(o)	(((o) & 0x3) << 16)
#define RPC_SMENR_DME		(0x1 << 15)
#define RPC_SMENR_CDE		(0x1 << 14)
#define RPC_SMENR_OCDE		(0x1 << 12)
#define RPC_SMENR_ADE(v)	(((v) & 0xF) << 8)
#define RPC_SMENR_OPDE(v)	(((v) & 0xF) << 4)
#define RPC_SMENR_SPIDE(v)	(((v) & 0xF) << 0)

#define RPC_SMRDR0		0x0038	/* R */
#define RPC_SMRDR1		0x003C	/* R */
#define RPC_SMWDR0		0x0040	/* R/W */
#define RPC_SMWDR1		0x0044	/* R/W */
#define RPC_CMNSR		0x0048	/* R */
#define RPC_CMNSR_SSLF		(0x1 << 1)
#define	RPC_CMNSR_TEND		(0x1 << 0)

#define RPC_DRDMCR		0x0058	/* R/W */
#define RPC_DRDMCR_DMCYC(v)	(((v) & 0xF) << 0)

#define RPC_DRDRENR		0x005C	/* R/W */
#define RPC_DRDRENR_HYPE	(0x5 << 12)
#define RPC_DRDRENR_ADDRE	(0x1 << 0x8)
#define RPC_DRDRENR_OPDRE	(0x1 << 0x4)
#define RPC_DRDRENR_DRDRE	(0x1 << 0x0)

#define RPC_SMDMCR		0x0060	/* R/W */
#define RPC_SMDMCR_DMCYC(v)	(((v) & 0xF) << 0)

#define RPC_SMDRENR		0x0064	/* R/W */
#define RPC_SMDRENR_HYPE(v)	(((v) & 0x7) << 12)
#define RPC_SMDRENR_HYPE_HF	RPC_SMDRENR_HYPE(0x5)
#define RPC_SMDRENR_HYPE_SPI	RPC_SMDRENR_HYPE(0)
#define RPC_SMDRENR_ADDRE	(0x1 << 0x8)
#define RPC_SMDRENR_OPDRE	(0x1 << 0x4)
#define RPC_SMDRENR_SPIDRE	(0x1 << 0x0)

#define RPC_PHYCNT		0x007C	/* R/W */
#define RPC_PHYCNT_CAL		(0x1 << 31)
#define RPC_PHYCNT_OCTA(v)	(((v) & 0x3) << 22)
#define RPC_PHYCNT_OCTA_AA	(0x1 << 22)
#define RPC_PHYCNT_OCTA_SA	(0x2 << 22)
#define RPC_PHYCNT_EXDS		(0x1 << 21)
#define RPC_PHYCNT_OCT		(0x1 << 20)
#define RPC_PHYCNT_DDRCAL	(0x1 << 19)
#define RPC_PHYCNT_HS		(0x1 << 18)
#define RPC_PHYCNT_STRTIM(v)	(((v) & 0x7) << 15)
#define RPC_PHYCNT_WBUF2	(0x1 << 4)
#define RPC_PHYCNT_WBUF		(0x1 << 2)
#define RPC_PHYCNT_MEM(v)	(((v) & 0x3) << 0)

#define RPC_PHYINT		0x0088	/* R/W */
#define RPC_PHYINT_INTIE	(0x1 << 24)
#define RPC_PHYINT_RSTEN	(0x1 << 18)
#define RPC_PHYINT_WPEN		(0x1 << 17)
#define RPC_PHYINT_INTEN	(0x1 << 16)
#define RPC_PHYINT_RST		(0x1 << 2)
#define RPC_PHYINT_WP		(0x1 << 1)
#define RPC_PHYINT_INT		(0x1 << 0)

#define RPC_PHYOFFSET1		0x0080
#define RPC_PHYOFFSET1_DDRTMG(v)	(((v) & 0x3) << 28)
#define RPC_PHYOFFSET1_DDRTMG_SDR	RPC_PHYOFFSET1_DDRTMG(3)
#define RPC_PHYOFFSET1_DDRTMG_DDR	RPC_PHYOFFSET1_DDRTMG(2)

#define RPC_PHYOFFSET2		0x0084
#define RPC_PHYOFFSET2_OCTTMG(v)	(((v) & 0x7) << 8)
#define RPC_PHYOFFSET2_OCTAL	RPC_PHYOFFSET2_OCTTMG(3)
#define RPC_PHYOFFSET2_SERIAL	RPC_PHYOFFSET2_OCTTMG(4)

#define RPC_DIVREG		0x00A8
#define RPC_DIVREG_RATIO(v)	((v) & 0x03)
#define RPC_DIVREG_RATIO_MAX	(0x2)

#define RPC_WBUF		0x8000	/* R/W size=4/8/16/32/64Bytes */
#define RPC_WBUF_SIZE		0x100
#define RPC_WBUF_MASK		(RPC_WBUF_SIZE - 1)

/* DMA transfer */
#define RPC_DMA_BURST		(0x20 << 3)
#define RPC_DMA_SIZE_MIN	(RPC_DMA_BURST << 3)

#define RPC_READ_ADDR_SIZE	BIT(26)
#define RPC_READ_ADDR_MASK	(RPC_READ_ADDR_SIZE - 1)

/* Default timeout in mS */
#define RPC_TIMEOUT		5000

/* Device flags */
#define RPC_OWN_CLOCK_DIVIDER	BIT(0)

enum rpc_size {
	/* singe flash: 8 bit; dual flash: 16 bit */
	RPC_SIZE_SINGLE_8BIT = RPC_SMENR_SPIDE(0x8),
	RPC_SIZE_DUAL_16BIT = RPC_SMENR_SPIDE(0x8),
	/* singe flash: 16 bit; dual flash: 32 bit */
	RPC_SIZE_SINGLE_16BIT = RPC_SMENR_SPIDE(0xC),
	RPC_SIZE_DUAL_32BIT = RPC_SMENR_SPIDE(0xC),
	/* singe flash: 32 bit; dual flash: 64 bit */
	RPC_SIZE_SINGLE_32BIT = RPC_SMENR_SPIDE(0xF),
	RPC_SIZE_DUAL_64BIT = RPC_SMENR_SPIDE(0xF),
};

enum rpc_type {
	RPC_SINGLE = 0,
	RPC_DUAL,
};

struct rpc_info {
	struct platform_device *pdev;
	struct device_node *flash;
	void __iomem *base;
	void __iomem *read_area;
	void __iomem *write_area;
	dma_addr_t read_area_dma;
	struct completion comp;
	struct dma_chan	*dma_chan;
	struct clk *clk;
	unsigned int irq;
	enum rpc_type mtdtype;
};

/* Register access */
static inline u32 rpc_readl(struct rpc_info *rpc, u32 offset)
{
	return readl(rpc->base + offset);
}

static inline void rpc_writel(struct rpc_info *rpc, u32 offset, u32 val)
{
	writel(val, rpc->base + offset);
}

static inline void rpc_clrsetl(struct rpc_info *rpc, u32 offset,
			       u32 clr, u32 set)
{
	void __iomem *addr = rpc->base + offset;
	u32 val = readl(addr);

	val &= ~clr;
	val |= set;
	writel(val, addr);
}

static inline void rpc_wbuf_writel(struct rpc_info *rpc, u32 offset, u32 val)
{
	writel(val, rpc->write_area + offset);
}

/* Check whether write buffer should be used */
static inline bool rpc_wbuf_available(struct rpc_info *rpc)
{
	return rpc->write_area;
}

#ifdef DEBUG
extern void rpc_regs_dump(struct rpc_info *rpc);
#else
static inline void rpc_regs_dump(struct rpc_info *rpc) { }
#endif

extern int rpc_wait(struct rpc_info *rpc, int timeout);
extern int rpc_dma_read(struct rpc_info *rpc, void *buf,
			loff_t from, ssize_t *plen);
extern void rpc_do_read_flash(struct rpc_info *rpc, loff_t from,
			      size_t len, u_char *buf, bool addr32);
#endif
