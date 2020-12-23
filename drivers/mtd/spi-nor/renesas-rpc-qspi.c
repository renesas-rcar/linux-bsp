/*
 * Renesas RPC QSPI driver
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

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-nor.h>

#include "renesas-rpc.h"

static void rpc_endisable_write_buf(struct rpc_info *rpc, bool en)
{
	rpc_clrsetl(rpc, RPC_PHYCNT,
		    RPC_PHYCNT_WBUF | RPC_PHYCNT_WBUF2,
		    en ? RPC_PHYCNT_WBUF | RPC_PHYCNT_WBUF2 : 0);
}

static int rpc_begin(struct rpc_info *rpc,
		     bool rx, bool tx, bool last)
{
	u32 val = RPC_SMCR_SPIE;

	if (rx)
		val |= RPC_SMCR_SPIRE;

	if (tx)
		val |= RPC_SMCR_SPIWE;

	if (!last)
		val |= RPC_SMCR_SSLKP;

	rpc_writel(rpc, RPC_SMCR, val);

	return 0;
}

static int rpc_setup_reg_mode(struct rpc_info *rpc)
{
	rpc_wait(rpc, RPC_TIMEOUT);

	rpc_endisable_write_buf(rpc, false);

	/* ...setup manual mode */
	rpc_clrsetl(rpc, RPC_CMNCR, 0, RPC_CMNCR_MD);

	/* disable ddr */
	rpc_clrsetl(rpc, RPC_SMDRENR,
		    RPC_SMDRENR_ADDRE | RPC_SMDRENR_OPDRE | RPC_SMDRENR_SPIDRE,
		    0);

	/* enable 1bit command */
	rpc_clrsetl(rpc, RPC_SMENR,
		    RPC_SMENR_CDB(3) | RPC_SMENR_OCDB(3) | RPC_SMENR_DME |
		    RPC_SMENR_OCDE | RPC_SMENR_SPIDB(3) |
		    RPC_SMENR_ADE(0xF) | RPC_SMENR_ADB(3) |
		    RPC_SMENR_OPDE(0xF) | RPC_SMENR_SPIDE(0xF),
		    RPC_SMENR_CDB(0) | RPC_SMENR_CDE | RPC_SIZE_SINGLE_32BIT);

	return 0;
}

static inline void rpc_flush_cache(struct rpc_info *rpc)
{
	rpc_clrsetl(rpc, RPC_DRCR, 0, RPC_DRCR_RCF);
}

static int rpc_setup_ext_mode(struct rpc_info *rpc)
{
	u32 val;
	u32 cmncr;

	rpc_wait(rpc, RPC_TIMEOUT);

	rpc_endisable_write_buf(rpc, false);

	/* ...setup ext mode */
	val = rpc_readl(rpc, RPC_CMNCR);
	cmncr = val;
	val &= ~(RPC_CMNCR_MD);
	rpc_writel(rpc, RPC_CMNCR, val);

	/* ...enable burst and clear cache */
	val = rpc_readl(rpc, RPC_DRCR);
	val &= ~(RPC_DRCR_RBURST(0x1F) | RPC_DRCR_RBE | RPC_DRCR_SSLE);
	val |= RPC_DRCR_RBURST(0x1F) | RPC_DRCR_RBE;

	if (cmncr & RPC_CMNCR_MD)
		val |= RPC_DRCR_RCF;

	rpc_writel(rpc, RPC_DRCR, val);

	return 0;
}

static int rpc_setup_data_size(struct rpc_info *rpc, u32 size, bool copy)
{
	u32 val;

	val = rpc_readl(rpc, RPC_SMENR);
	val &= ~(RPC_SMENR_SPIDE(0xF));

	if (rpc->mtdtype == RPC_DUAL && !copy)
		size >>= 1;

	switch (size) {
	case 0:
		break;
	case 1:
		val |= RPC_SIZE_SINGLE_8BIT;
		break;
	case 2:
		val |= RPC_SIZE_SINGLE_16BIT;
		break;
	case 4:
		val |= RPC_SIZE_SINGLE_32BIT;
		break;
	default:
		dev_err(&rpc->pdev->dev, "Unsupported data width %d\n", size);
		return -EINVAL;
	}
	rpc_writel(rpc, RPC_SMENR, val);

	return 0;
}

static inline int rpc_get_read_addr_nbits(u8 opcode)
{
	if (opcode == SPINOR_OP_READ_1_4_4_4B)
		return 4;
	return 1;
}

#define RPC_NBITS_TO_VAL(v)	((v >> 1) & 3)
static void rpc_setup_extmode_nbits(struct rpc_info *rpc,
				    int cnb, int anb, int dnb)
{
	rpc_clrsetl(rpc, RPC_DRENR,
		    RPC_DRENR_CDB(3) | RPC_DRENR_ADB(3) | RPC_DRENR_DRDB(3),
		    RPC_DRENR_CDB(RPC_NBITS_TO_VAL(cnb)) |
		    RPC_DRENR_ADB(RPC_NBITS_TO_VAL(anb)) |
		    RPC_DRENR_DRDB(RPC_NBITS_TO_VAL(dnb)));
}

static void rpc_setup_writemode_nbits(struct rpc_info *rpc,
				      int cnb, int anb, int dnb)
{
	rpc_clrsetl(rpc, RPC_SMENR,
		    RPC_SMENR_CDB(3) | RPC_SMENR_ADB(3) | RPC_SMENR_SPIDB(3),
		    RPC_SMENR_CDB(RPC_NBITS_TO_VAL(cnb)) |
		    RPC_SMENR_ADB(RPC_NBITS_TO_VAL(anb)) |
		    RPC_SMENR_SPIDB(RPC_NBITS_TO_VAL(dnb)));
}

static void rpc_setup_write_mode_command_and_adr(struct rpc_info *rpc,
						 int adr_width, bool ena)
{
	u32 val;

	val = rpc_readl(rpc, RPC_SMENR);
	val &= ~(RPC_SMENR_CDB(3) | RPC_SMENR_CDE | RPC_SMENR_ADE(0xF));

	if (ena) {
		/* enable 1bit command */
		val |= RPC_SMENR_CDB(0) | RPC_SMENR_CDE;

		if (adr_width == 4)
			val |= RPC_SMENR_ADE(0xF); /* bits 31-0 */
		else
			val |= RPC_SMENR_ADE(0x7); /* bits 23-0 */
	}
	rpc_writel(rpc, RPC_SMENR, val);
}

static void rpc_setup_write_mode(struct rpc_info *rpc, u8 opcode)
{
	rpc_wait(rpc, RPC_TIMEOUT);

	rpc_endisable_write_buf(rpc, true);

	/* ...setup manual mode */
	rpc_clrsetl(rpc, RPC_CMNCR, 0, RPC_CMNCR_MD);

	/* disable ddr */
	rpc_clrsetl(rpc, RPC_SMDRENR,
		    RPC_SMDRENR_ADDRE | RPC_SMDRENR_OPDRE | RPC_SMDRENR_SPIDRE,
		    0);

	rpc_clrsetl(rpc, RPC_SMENR,
		    RPC_SMENR_OCDB(3) | RPC_SMENR_DME | RPC_SMENR_OCDE |
		    RPC_SMENR_SPIDB(3) | RPC_SMENR_ADB(3) |
		    RPC_SMENR_OPDE(0xF) | RPC_SMENR_SPIDE(0xF),
		    opcode != SPINOR_OP_PP ? RPC_SIZE_SINGLE_32BIT :
		    RPC_SIZE_SINGLE_8BIT);
}

static void rpc_read_manual_data(struct rpc_info *rpc, u32 *pv0, u32 *pv1)
{
	u32 val0, val1, rd0, rd1;

	val0 = rpc_readl(rpc, RPC_SMRDR0);
	val1 = rpc_readl(rpc, RPC_SMRDR1);

	if (rpc->mtdtype == RPC_DUAL) {
		rd1 = (val0 & 0xFf000000) | ((val0 << 8) & 0xFf0000) |
			((val1 >> 16) & 0xff00) | ((val1 >> 8) & 0xff);
		rd0 = ((val0 & 0xff0000) << 8) | ((val0 << 16) & 0xff0000) |
			((val1 >> 8) & 0xff00) | (val1 & 0xff);
	} else
		rd0 = val0;

	if (pv0)
		*pv0 = rd0;

	if (pv1 && rpc->mtdtype == RPC_DUAL)
		*pv1 = rd1;
}

static int rpc_datalen2trancfersize(struct rpc_info *rpc, int len, bool copy)
{
	int sz = len;

	if (len >= 2)
		sz = 2;

	if (len >= 4)
		sz = 4;

	if (rpc->mtdtype == RPC_DUAL && len >= 8 && !copy)
		sz = 8;

	return sz;
}

static int __rpc_write_data2reg(struct rpc_info *rpc, int off,
				const u8 *buf, int sz)
{
	const u32 *b32 = (const u32 *)buf;
	const u16 *b16 = (const u16 *)buf;

	if (sz == 4)
		rpc_writel(rpc, off, *b32);
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

#define RPC_SETVAL(x)	((((x) & 0xff) << 8) | ((x) & 0xff))
static int rpc_write_data2reg(struct rpc_info *rpc, const u8 *buf,
			      int sz, bool copy)
{
	int i, ret;
	u32 v = 0;

	if (rpc->mtdtype == RPC_DUAL) {
		if (copy) {
			for (i = 0; i < sz && i < 2; i++)
				v |= (RPC_SETVAL(buf[i]) << 16*i);

			ret = __rpc_write_data2reg(rpc,
						   sz == 4 ? RPC_SMWDR1 : RPC_SMWDR0,
						   (u8 *)&v,
						   sz == 4 ? sz : sz * 2);
			if (ret)
				return ret;

			v = 0;
			for (; i < sz; i++)
				v |= (RPC_SETVAL(buf[i]) << 16*i);


			ret = __rpc_write_data2reg(rpc,
						   sz == 4 ? RPC_SMWDR0 : RPC_SMWDR1,
						   (u8 *)&v,
						   sz == 4 ? sz : sz * 2);
			if (ret)
				return ret;

			return 0;
		}

		sz >>= 1;
		ret = __rpc_write_data2reg(rpc,
					   sz == 4 ? RPC_SMWDR1 : RPC_SMWDR0,
					   buf,
					   sz == 4 ? sz : sz * 2);
		if (ret)
			return ret;
		buf += sz;

		return __rpc_write_data2reg(rpc,
					    sz == 4 ? RPC_SMWDR0 : RPC_SMWDR1,
					    buf, sz == 4 ? sz : sz * 2);
	}

	return __rpc_write_data2reg(rpc, RPC_SMWDR0, buf, sz);
}

static ssize_t rpc_write_unaligned(struct spi_nor *nor, loff_t to, size_t len,
				   const u_char *buf, size_t fullen)
{
	int ret = len, dsize;
	struct rpc_info *rpc = nor->priv;
	bool copy = false, last;
	loff_t _to;

	rpc_endisable_write_buf(rpc, false);

	while (len > 0) {
		_to = to;
		if (rpc->mtdtype == RPC_DUAL)
			_to >>= 1;
		rpc_writel(rpc, RPC_SMADR, _to);
		dsize = rpc_datalen2trancfersize(rpc, len, copy);

		if (rpc_setup_data_size(rpc, dsize, copy))
			return -EINVAL;

		rpc_write_data2reg(rpc, buf, dsize, copy);

		last = (len <= dsize && fullen <= ret);
		rpc_begin(rpc, false, true, last);
		if (rpc_wait(rpc, RPC_TIMEOUT))
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
	struct rpc_info *rpc = nor->priv;
	loff_t bo;
	loff_t offset;
	bool is_rounded = false;

	/* ...len should be rounded to 2 bytes */
	if (rpc->mtdtype == RPC_DUAL && (len & 1)) {
		is_rounded = true;
		len &= ~(1);
	}

	bo = to & RPC_WBUF_MASK;

	rpc_flush_cache(rpc);
	rpc_setup_write_mode(rpc, nor->program_opcode);
	rpc_setup_write_mode_command_and_adr(rpc, nor->addr_width, true);
	rpc_setup_writemode_nbits(rpc, 1, 1, 1);

	/* ...setup command */
	val = rpc_readl(rpc, RPC_SMCMR);
	val &= ~RPC_SMCMR_CMD(0xFF);
	val |= RPC_SMCMR_CMD(nor->program_opcode);
	rpc_writel(rpc, RPC_SMCMR, val);

	offset = to & ~RPC_WBUF_MASK;

	/* ...write unaligned first bytes */
	if (bo) {
		size_t min = (len < (RPC_WBUF_SIZE - bo)) ? len : (RPC_WBUF_SIZE - bo);

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

static ssize_t rpc_read_flash(struct spi_nor *nor, loff_t from, size_t len,
			      u_char *buf)
{
	u32 val;
	struct rpc_info *rpc = nor->priv;
	int opcode_nbits = 1;
	int addr_nbits = rpc_get_read_addr_nbits(nor->read_opcode);
	int data_nbits = rpc_rx_nbits(nor);
	int dummy = nor->read_dummy - 1;
	ssize_t ret = len;

	rpc_setup_ext_mode(rpc);
	/* ...setup n bits */
	rpc_setup_extmode_nbits(rpc, opcode_nbits, addr_nbits, data_nbits);

	/* TODO: setup DDR */

	/* ...setup command */
	val = rpc_readl(rpc, RPC_DRCMR);
	val &= ~RPC_DRCMR_CMD(0xFF);
	val |= RPC_DRCMR_CMD(nor->read_opcode);
	rpc_writel(rpc, RPC_DRCMR, val);

	/* ...setup dummy cycles */
	val = rpc_readl(rpc, RPC_DRDMCR);
	val &= ~RPC_DRDMCR_DMCYC(0x1F);
	val |= RPC_DRDMCR_DMCYC(dummy);
	rpc_writel(rpc, RPC_DRDMCR, val);

	/* ...setup read sequence */
	val = rpc_readl(rpc, RPC_DRENR);
	if (nor->read_dummy)
		val |= RPC_DRENR_DME;
	else
		val &= ~RPC_DRENR_DME;
	val |= RPC_DRENR_CDE;
	rpc_writel(rpc, RPC_DRENR, val);

	rpc_do_read_flash(rpc, from, len, buf, nor->addr_width > 3);

	return ret;
}

static int __rpc_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	u32 val;
	u32 val2 = 0;
	u32 *buf32;
	int i;
	u32 mask = 0, type;
	struct rpc_info *rpc = nor->priv;

	type = rpc->mtdtype;

	rpc_setup_reg_mode(rpc);
	val = rpc_readl(rpc, RPC_SMCMR);
	val &= ~RPC_SMCMR_CMD(0xFF);
	val |= RPC_SMCMR_CMD(opcode);
	rpc_writel(rpc, RPC_SMCMR, val);

	rpc_begin(rpc, true, false, len <= 4);
	if (rpc_wait(rpc, RPC_TIMEOUT))
		return -ETIMEDOUT;

	/* ...disable command */
	val = rpc_readl(rpc, RPC_SMENR);
	val &= ~(RPC_SMENR_CDE);
	rpc_writel(rpc, RPC_SMENR, val);

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
		if (type == RPC_DUAL && val != val2) {
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
		if (rpc_wait(rpc, RPC_TIMEOUT))
			return -ETIMEDOUT;

	}

	return 0;
}

#define RPC_REPEAT_TIMEOUT	200
static int rpc_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	unsigned long end = jiffies + msecs_to_jiffies(RPC_REPEAT_TIMEOUT);

	/* A few read commands like read status can
	 * generate different answers. We repeat reading
	 * in that case
	 */
	while (true) {
		int ret = __rpc_read_reg(nor, opcode, buf, len);

		if (!ret || ret != -EAGAIN)
			return ret;

		if (time_after(jiffies, end))
			return -ETIMEDOUT;

		msleep(20);
	}
}

static int rpc_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct rpc_info *rpc = nor->priv;
	u32 val;
	int dsize;
	bool copy = true;

	rpc_setup_reg_mode(rpc);

	val = rpc_readl(rpc, RPC_SMCMR);
	val &= ~RPC_SMCMR_CMD(0xFF);
	val |= RPC_SMCMR_CMD(opcode);
	rpc_writel(rpc, RPC_SMCMR, val);

	dsize = rpc_datalen2trancfersize(rpc, len, copy);

	if (rpc_setup_data_size(rpc, dsize, copy))
		return -EINVAL;

	if (rpc_write_data2reg(rpc, buf, dsize, copy))
		return -EINVAL;
	buf += dsize;
	len -= dsize;
	rpc_begin(rpc, false, dsize > 0, len == 0);

	if (rpc_wait(rpc, RPC_TIMEOUT))
		return -ETIMEDOUT;

	/* ...disable command */
	val = rpc_readl(rpc, RPC_SMENR);
	val &= ~(RPC_SMENR_CDE);
	rpc_writel(rpc, RPC_SMENR, val);

	while (len > 0) {
		dsize = rpc_datalen2trancfersize(rpc, len, copy);
		if (rpc_setup_data_size(rpc, dsize, copy))
			return -EINVAL;
		rpc_write_data2reg(rpc, buf, dsize, copy);
		buf += dsize;
		len -= dsize;

		rpc_begin(rpc, false, dsize, len == 0);

		if (rpc_wait(rpc, RPC_TIMEOUT))
			return -ETIMEDOUT;

	}

	return 0;
}

/* hw init for spi-nor flashes */
static int rpc_spi_hw_init(struct rpc_info *rpc)
{
	/* Exec calibration */
	rpc_clrsetl(rpc, RPC_PHYCNT,
		    RPC_PHYCNT_OCTA(3) | RPC_PHYCNT_EXDS | RPC_PHYCNT_OCT |
		    RPC_PHYCNT_DDRCAL | RPC_PHYCNT_HS | RPC_PHYCNT_STRTIM(7) |
		    RPC_PHYCNT_WBUF2 | RPC_PHYCNT_WBUF | RPC_PHYCNT_MEM(3),
		    RPC_PHYCNT_CAL | RPC_PHYCNT_STRTIM(6));

	/* Disable RPC pins  */
	rpc_clrsetl(rpc, RPC_PHYINT,
		    RPC_PHYINT_INTIE | RPC_PHYINT_RSTEN |
		    RPC_PHYINT_WPEN | RPC_PHYINT_INTEN,
		    0);

	rpc_clrsetl(rpc, RPC_SMDRENR,
		    RPC_SMDRENR_HYPE(7),
		    RPC_SMDRENR_HYPE_SPI);

	rpc_clrsetl(rpc, RPC_CMNCR,
		    RPC_CMNCR_BSZ(3),
		    rpc->mtdtype != RPC_SINGLE ?
		    RPC_CMNCR_BSZ(1) :
		    RPC_CMNCR_BSZ(0));

	rpc_clrsetl(rpc, RPC_PHYOFFSET1,
		    RPC_PHYOFFSET1_DDRTMG(3),
		    RPC_PHYOFFSET1_DDRTMG_SDR);

	rpc_writel(rpc, RPC_SSLDR,
		   RPC_SSLDR_SPNDL(0) | RPC_SSLDR_SLNDL(4) |
		   RPC_SSLDR_SCKDL(0));

	return 0;
}

static int rpc_erase_sector(struct spi_nor *nor, loff_t addr)
{
	struct rpc_info *rpc = nor->priv;
	u8 buf[6];
	int i;

	if (rpc->mtdtype == RPC_DUAL)
		addr >>= 1;

	for (i = nor->addr_width - 1; i >= 0; i--) {
		buf[i] = addr & 0xff;
		addr >>= 8;
	}

	return nor->write_reg(nor, nor->erase_opcode, buf, nor->addr_width);
}

int rpc_qspi_probe(struct platform_device *pdev)
{
	struct rpc_info *rpc;
	struct spi_nor *nor;
	struct spi_nor_hwcaps hwcaps = {
		.mask = SNOR_HWCAPS_READ |
			SNOR_HWCAPS_READ_FAST |
			SNOR_HWCAPS_PP,
	};
	u32 property;
	int ret;

	rpc = dev_get_drvdata(pdev->dev.parent);
	if (!rpc || !rpc->flash) {
		dev_err(&pdev->dev, "invalid data\n");
		return -EINVAL;
	}

	if (!of_property_read_u32(rpc->flash, "spi-rx-bus-width", &property)) {
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

	/* ... setup nor hooks */
	nor = devm_kzalloc(&pdev->dev, sizeof(*nor), GFP_KERNEL);
	if (!nor) {
		dev_err(&pdev->dev, "allocation failed\n");
		return -ENOMEM;
	}

	nor->dev = &pdev->dev;
	spi_nor_set_flash_node(nor, rpc->flash);
	nor->read  = rpc_read_flash;
	nor->write = rpc_write_flash;
	nor->read_reg  = rpc_read_reg;
	nor->write_reg = rpc_write_reg;
	nor->priv = rpc;

	/* ... enable clk */
	ret = clk_prepare_enable(rpc->clk);
	if (ret) {
		dev_err(&pdev->dev, "cannot prepare clock\n");
		return ret;
	}

	/* ...init device */
	ret = rpc_spi_hw_init(rpc);
	if (ret < 0) {
		dev_err(&pdev->dev, "rpc_spi_hw_init error.\n");
		goto error;
	}

	ret = spi_nor_scan(nor, NULL, &hwcaps);
	if (ret) {
		dev_err(&pdev->dev, "spi_nor_scan error.\n");
		goto error;
	}

	/* Dual mode support */
	if (rpc->mtdtype == RPC_DUAL) {
		nor->page_size <<= 1;
		nor->mtd.erasesize <<= 1;
		nor->mtd.size <<= 1;
		nor->mtd.writebufsize <<= 1;
		nor->erase = rpc_erase_sector;
	}

	/* Workaround data size limitation */
	if (nor->page_size > RPC_WBUF_SIZE) {
		nor->page_size = RPC_WBUF_SIZE;
		nor->mtd.writebufsize = RPC_WBUF_SIZE;
	}

	ret = mtd_device_register(&nor->mtd, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "MTD registration failed\n");
		goto error;
	}

	dev_info(&pdev->dev, "probed as %s\n",
		 rpc->mtdtype == RPC_SINGLE ? "single" : "dual");

	platform_set_drvdata(pdev, nor);
	return 0;

error:
	clk_disable_unprepare(rpc->clk);
	return ret;
}

static int rpc_qspi_remove(struct platform_device *pdev)
{
	struct spi_nor *nor = platform_get_drvdata(pdev);
	struct rpc_info *rpc = nor->priv;

	mtd_device_unregister(&nor->mtd);
	clk_disable_unprepare(rpc->clk);
	return 0;
}

/* platform driver interface */
static struct platform_driver rpc_qspi_platform_driver = {
	.probe		= rpc_qspi_probe,
	.remove		= rpc_qspi_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "renesas-rpc-qspi",
	},
};

module_platform_driver(rpc_qspi_platform_driver);

MODULE_ALIAS("renesas-rpc-qspi");
MODULE_AUTHOR("Cogent Embedded Inc. <sources@cogentembedded.com>");
MODULE_DESCRIPTION("Renesas RPC QSPI Driver");
MODULE_LICENSE("GPL");
