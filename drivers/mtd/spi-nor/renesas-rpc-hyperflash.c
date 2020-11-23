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

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mtd/mtd.h>

#include "renesas-rpc.h"

/* RPC HyperFlash */
#define RPC_HF_CMD_CA47		(0x1 << 7)	/* Read */
#define RPC_HF_CMD_CA46		(0x1 << 6)	/* Register space */
#define RPC_HF_CMD_CA45		(0x1 << 5)	/* Liner burst */

#define RPC_HF_CMD_READ_REG	(RPC_HF_CMD_CA47 | RPC_HF_CMD_CA46)
#define RPC_HF_CMD_READ_MEM	RPC_HF_CMD_CA47
#define RPC_HF_CMD_WRITE_REG	RPC_HF_CMD_CA46
#define RPC_HF_CMD_WRITE_MEM	0x0

#define RPC_HF_ERASE_SIZE	0x40000

#define RPC_CFI_STATUS_DRB	(0x1 << 7)
#define RPC_CFI_STATUS_ESSB	(0x1 << 6)
#define RPC_CFI_STATUS_ESB	(0x1 << 5)
#define RPC_CFI_STATUS_PSB	(0x1 << 4)
#define RPC_CFI_STATUS_WBASB	(0x1 << 3)
#define RPC_CFI_STATUS_PSSB	(0x1 << 2)
#define RPC_CFI_STATUS_SLSB	(0x1 << 1)
#define RPC_CFI_STATUS_ESTAT	(0x1 << 0)

#define RPC_CFI_UNLOCK1		(0x555 << 1)
#define RPC_CFI_UNLOCK2		(0x2AA << 1)

#define RPC_CFI_CMD_UNLOCK_START	0xAA
#define RPC_CFI_CMD_UNLOCK_ACK		0x55
#define	RPC_CFI_CMD_RESET		0xF0
#define	RPC_CFI_CMD_READ_STATUS		0x70
#define	RPC_CFI_CMD_READ_ID		0x90
#define	RPC_CFI_CMD_WRITE		0xA0
#define	RPC_CFI_CMD_ERASE_START		0x80
#define	RPC_CFI_CMD_ERASE_SECTOR	0x30

#define RPC_CFI_ID_MASK			0x000F
#define RPC_CFI_ID_MAN_SPANSION		0x0001
#define RPC_CFI_ID_TYPE_HYPERFLASH	0x000E

struct rpc_hf_info {
	struct mtd_info	mtd;
	struct mutex	lock;
	void		*priv;
};

static void rpc_hf_mode_man(struct rpc_info *rpc)
{
	rpc_wait(rpc, RPC_TIMEOUT);

	/*
	 * RPC_PHYCNT         = 0x80000263
	 * bit31  CAL         =  1 : PHY calibration
	 * bit1-0 PHYMEM[1:0] = 11 : HyperFlash
	 */
	rpc_clrsetl(rpc, RPC_PHYCNT,
		    RPC_PHYCNT_WBUF | RPC_PHYCNT_WBUF2 |
		    RPC_PHYCNT_CAL | RPC_PHYCNT_MEM(3),
		    RPC_PHYCNT_CAL | RPC_PHYCNT_MEM(3));

	/*
	 * RPC_CMNCR       = 0x81FFF301
	 * bit31  MD       =  1 : Manual mode
	 * bit1-0 BSZ[1:0] = 01 : QSPI Flash x 2 or HyperFlash
	 */
	rpc_clrsetl(rpc, RPC_CMNCR,
		    RPC_CMNCR_MD | RPC_CMNCR_BSZ(3),
		    RPC_CMNCR_MOIIO_HIZ | RPC_CMNCR_IOFV_HIZ |
		    RPC_CMNCR_MD | RPC_CMNCR_BSZ(1));
}

static void rpc_hf_mode_ext(struct rpc_info *rpc)
{
	rpc_wait(rpc, RPC_TIMEOUT);

	/*
	 * RPC_PHYCNT         = 0x80000263
	 * bit31  CAL         =  1 : PHY calibration
	 * bit1-0 PHYMEM[1:0] = 11 : HyperFlash
	 */
	rpc_clrsetl(rpc, RPC_PHYCNT,
		    RPC_PHYCNT_WBUF | RPC_PHYCNT_WBUF2 |
		    RPC_PHYCNT_CAL | RPC_PHYCNT_MEM(3),
		    RPC_PHYCNT_CAL | RPC_PHYCNT_MEM(3));

	/*
	 * RPC_CMNCR       = 0x81FFF301
	 * bit31  MD       =  1 : Manual mode
	 * bit1-0 BSZ[1:0] = 01 : QSPI Flash x 2 or HyperFlash
	 */
	rpc_clrsetl(rpc, RPC_CMNCR,
		    RPC_CMNCR_MD | RPC_CMNCR_BSZ(3),
		    RPC_CMNCR_MOIIO_HIZ | RPC_CMNCR_IOFV_HIZ |
		    RPC_CMNCR_BSZ(1));

	/*
	 * RPC_DRCR             = 0x001F0100
	 * bit21-16 RBURST[4:0] = 11111 : Read burst 32 64-bit data units
	 * bit9 RCF             = 1     : Clear cache
	 * bit8 RBE             = 1     : Read burst enable
	 */
	rpc_writel(rpc, RPC_DRCR,
		   rpc->flags & RPC_HF_ZERO_READ_BURST ?
		   RPC_DRCR_RCF | RPC_DRCR_RBE | RPC_DRCR_RBURST(0x0) :
		   RPC_DRCR_RCF | RPC_DRCR_RBE | RPC_DRCR_RBURST(0x1F));

	rpc_writel(rpc, RPC_DRCMR, RPC_DRCMR_CMD(0xA0));
	rpc_writel(rpc, RPC_DRENR,
		   RPC_DRENR_CDB(2) | RPC_DRENR_OCDB(2) |
		   RPC_DRENR_ADB(2) | RPC_DRENR_DRDB(2) |
		   RPC_DRENR_CDE | RPC_DRENR_OCDE |
		   RPC_DRENR_DME | RPC_DRENR_ADE(4));
	rpc_writel(rpc, RPC_DRDMCR, RPC_DRDMCR_DMCYC(0xE));
	rpc_writel(rpc, RPC_DRDRENR,
		   RPC_DRDRENR_HYPE | RPC_DRDRENR_ADDRE | RPC_DRDRENR_DRDRE);

	/* Dummy read */
	rpc_readl(rpc, RPC_DRCR);
}

static void rpc_hf_xfer(struct rpc_info *rpc, u32 addr, u16 *data,
			enum rpc_size size, u8 cmd)
{
	u32 val;

	rpc_hf_mode_man(rpc);

	/*
	 * bit23-21 CMD[7:5] : CA47-45
	 * CA47 = 0/1 : Write/Read
	 * CA46 = 0/1 : Memory Space/Register Space
	 * CA45 = 0/1 : Wrapped Burst/Linear Burst
	 */
	rpc_writel(rpc, RPC_SMCMR, RPC_SMCMR_CMD(cmd));

	rpc_writel(rpc, RPC_SMADR, addr >> 1);

	rpc_writel(rpc, RPC_SMOPR, 0x0);

	/*
	 * RPC_SMDRENR     = 0x00005101
	 * bit14-12 HYPE   = 101: Hyperflash mode
	 * bit8     ADDRE  = 1 : Address DDR transfer
	 * bit0     SPIDRE = 1 : DATA DDR transfer
	 */
	rpc_writel(rpc, RPC_SMDRENR,
		   RPC_SMDRENR_HYPE_HF | RPC_SMDRENR_ADDRE | RPC_SMDRENR_SPIDRE);

	val = RPC_SMENR_CDB(2) | RPC_SMENR_OCDB(2) |
		RPC_SMENR_ADB(2) | RPC_SMENR_SPIDB(2) |
		RPC_SMENR_CDE | RPC_SMENR_OCDE | RPC_SMENR_ADE(4) | size;

	if (cmd & RPC_HF_CMD_CA47)
		goto read_transfer;

	/*
	 * RPC_SMENR           = 0xA222540x
	 * bit31-30 CDB[1:0]   =   10 : 4bit width command
	 * bit25-24 ADB[1:0]   =   10 : 4bit width address
	 * bit17-16 SPIDB[1:0] =   10 : 4bit width transfer data
	 * bit15    DME        =    0 : dummy cycle disable
	 * bit14    CDE        =    1 : Command enable
	 * bit12    OCDE       =    1 : Option Command enable
	 * bit11-8  ADE[3:0]   = 0100 : ADR[23:0] output
	 * bit7-4   OPDE[3:0]  = 0000 : Option data disable
	 * bit3-0   SPIDE[3:0] = xxxx : Transfer size
	 */
	rpc_writel(rpc, RPC_SMENR, val);

	switch (size) {
	case RPC_SIZE_DUAL_64BIT:
		val = cmd & RPC_HF_CMD_CA46 ?
			cpu_to_be16(data[0]) | cpu_to_be16(data[1]) << 16 :
			data[0] | data[1] << 16;
		rpc_writel(rpc, RPC_SMWDR1, val);
		val = cmd & RPC_HF_CMD_CA46 ?
			cpu_to_be16(data[2]) | cpu_to_be16(data[3]) << 16 :
			data[2] | data[3] << 16;
		break;
	case RPC_SIZE_DUAL_32BIT:
		val = cmd & RPC_HF_CMD_CA46 ?
			cpu_to_be16(data[0]) | cpu_to_be16(data[1]) << 16 :
			data[0] | data[1] << 16;
		break;
	default:
		val = cmd & RPC_HF_CMD_CA46 ?
			cpu_to_be16(data[0]) << 16 :
			data[0] << 16;
		break;
	}

	rpc_writel(rpc, RPC_SMWDR0, val);
	/*
	 * RPC_SMCR       = 0x00000003
	 * bit1     SPIWE = 1 : Data write enable
	 * bit0     SPIE  = 1 : SPI transfer start
	 */
	rpc_writel(rpc, RPC_SMCR, RPC_SMCR_SPIWE | RPC_SMCR_SPIE);
	return;

read_transfer:
	rpc_writel(rpc, RPC_SMDMCR, RPC_SMDMCR_DMCYC(0xE));
	val |= RPC_SMENR_DME;

	/*
	 * RPC_SMENR           = 0xA222D40x
	 * bit31-30 CDB[1:0]   =   10 : 4bit width command
	 * bit25-24 ADB[1:0]   =   10 : 4bit width address
	 * bit17-16 SPIDB[1:0] =   10 : 4bit width transfer data
	 * bit15    DME        =    1 : dummy cycle disable
	 * bit14    CDE        =    1 : Command enable
	 * bit12    OCDE       =    1 : Option Command enable
	 * bit11-8  ADE[3:0]   = 0100 : ADR[23:0] output (24 Bit Address)
	 * bit7-4   OPDE[3:0]  = 0000 : Option data disable
	 * bit3-0   SPIDE[3:0] = xxxx : Transfer size
	 */
	rpc_writel(rpc, RPC_SMENR, val);

	/*
	 * RPC_SMCR   = 0x00000005
	 * bit2 SPIRE = 1 : Data read disable
	 * bit0 SPIE  = 1 : SPI transfer start
	 */
	rpc_writel(rpc, RPC_SMCR, RPC_SMCR_SPIRE | RPC_SMCR_SPIE);

	rpc_wait(rpc, RPC_TIMEOUT);
	val = rpc_readl(rpc, RPC_SMRDR0);

	/*
	 * Read data from either register or memory space.
	 * Register space is always big-endian.
	 */
	switch (size) {
	case RPC_SIZE_DUAL_64BIT:
		if (cmd & RPC_HF_CMD_CA46) {
			data[3] = be16_to_cpu((val >> 16) & 0xFFFF);
			data[2] = be16_to_cpu(val & 0xFFFF);
		} else {
			data[3] = (val >> 16) & 0xFFFF;
			data[2] = val & 0xFFFF;
		}
		val = rpc_readl(rpc, RPC_SMRDR1);
		if (cmd & RPC_HF_CMD_CA46) {
			data[1] = be16_to_cpu((val >> 16) & 0xFFFF);
			data[0] = be16_to_cpu(val & 0xFFFF);
		} else {
			data[1] = (val >> 16) & 0xFFFF;
			data[0] = val & 0xFFFF;
		}
		break;
	case RPC_SIZE_DUAL_32BIT:
		if (cmd & RPC_HF_CMD_CA46) {
			data[1] = be16_to_cpu((val >> 16) & 0xFFFF);
			data[0] = be16_to_cpu(val & 0xFFFF);
		} else {
			data[1] = (val >> 16) & 0xFFFF;
			data[0] = val & 0xFFFF;
		}
		break;
	default:
		data[0] = cmd & RPC_HF_CMD_CA46 ?
			be16_to_cpu((val >> 16) & 0xFFFF) :
			(val >> 16) & 0xFFFF;
		break;
	}
}

static void rpc_hf_wbuf_enable(struct rpc_info *rpc, u32 addr)
{
	rpc_wait(rpc, RPC_TIMEOUT);

	/*
	 * RPC_PHYCNT         = 0x80000277
	 * bit31  CAL         =  1 : PHY calibration
	 * bit4 WBUF2         =  1 : Write buffer enable 2
	 * bit2 WBUF          =  1 : Write buffer enable
	 * bit1-0 PHYMEM[1:0] = 11 : HyperFlash
	 */
	rpc_clrsetl(rpc, RPC_PHYCNT,
		    RPC_PHYCNT_WBUF2 | RPC_PHYCNT_WBUF |
		    RPC_PHYCNT_CAL | RPC_PHYCNT_MEM(3),
		    RPC_PHYCNT_WBUF2 | RPC_PHYCNT_WBUF |
		    RPC_PHYCNT_CAL | RPC_PHYCNT_MEM(3));

	/*
	 * RPC_DRCR             = 0x001F0100
	 * bit21-16 RBURST[4:0] = 11111 : Read burst 32 64-bit data units
	 * bit9 RCF             = 1     : Clear cache
	 * bit8 RBE             = 1     : Read burst enable
	 */
	rpc_writel(rpc, RPC_DRCR,
		   RPC_DRCR_RBURST(0x1F) | RPC_DRCR_RCF | RPC_DRCR_RBE);

	rpc_writel(rpc, RPC_SMCMR, RPC_SMCMR_CMD(RPC_HF_CMD_WRITE_MEM));

	rpc_writel(rpc, RPC_SMADR, addr >> 1);

	rpc_writel(rpc, RPC_SMOPR, 0x0);

	/*
	 * RPC_SMDRENR   = 0x00005101
	 * bit14-12 HYPE = 101:Hyperflash mode
	 * bit8 ADDRE    = 1 : Address DDR transfer
	 * bit0 SPIDRE   = 1 : DATA DDR transfer
	 */
	rpc_writel(rpc, RPC_SMDRENR,
		   RPC_SMDRENR_HYPE_HF | RPC_SMDRENR_ADDRE | RPC_SMDRENR_SPIDRE);

	/*
	 * RPC_SMENR           = 0xA222540F
	 * bit31-30 CDB[1:0]   =   10 : 4bit width command
	 * bit25-24 ADB[1:0]   =   10 : 4bit width address
	 * bit17-16 SPIDB[1:0] =   10 : 4bit width transfer data
	 * bit15    DME        =    0 : dummy cycle disable
	 * bit14    CDE        =    1 : Command enable
	 * bit12    OCDE       =    1 : Option Command enable
	 * bit11-8  ADE[3:0]   = 0100 : ADR[23:0] output (24 Bit Address)
	 * bit7-4   OPDE[3:0]  = 0000 : Option data disable
	 * bit3-0   SPIDE[3:0] = 1111 : 64-bit transfer size
	 */
	rpc_writel(rpc, RPC_SMENR,
		   RPC_SMENR_CDB(2) | RPC_SMENR_OCDB(2) |
		   RPC_SMENR_ADB(2) | RPC_SMENR_SPIDB(2) |
		   RPC_SMENR_CDE | RPC_SMENR_OCDE |
		   RPC_SMENR_ADE(4) | RPC_SIZE_DUAL_64BIT);

	/* Dummy read */
	rpc_readl(rpc, RPC_DRCR);
}

static inline void rpc_hf_write_cmd(struct rpc_info *rpc, u32 addr, u16 cmd)
{
	rpc_hf_xfer(rpc, addr, &cmd, RPC_SIZE_DUAL_16BIT, RPC_HF_CMD_WRITE_REG);
}

static inline void rpc_hf_read_reg(struct rpc_info *rpc, u32 addr, u16 *data,
				   enum rpc_size size)
{
	rpc_hf_xfer(rpc, addr, data, size, RPC_HF_CMD_READ_REG);
}

static inline void rpc_hf_write_reg(struct rpc_info *rpc, u32 addr, u16 *data,
				    enum rpc_size size)
{
	rpc_hf_xfer(rpc, addr, data, size, RPC_HF_CMD_WRITE_REG);
}

static inline void rpc_hf_read_mem(struct rpc_info *rpc, u32 addr, u16 *data,
				   enum rpc_size size)
{
	rpc_hf_xfer(rpc, addr, data, size, RPC_HF_CMD_READ_MEM);
}

static inline void rpc_hf_write_mem(struct rpc_info *rpc, u32 addr, u16 *data,
				    enum rpc_size size)
{
	rpc_hf_xfer(rpc, addr, data, size, RPC_HF_CMD_WRITE_MEM);
}

static void rpc_hf_wp(struct rpc_info *rpc, int enable)
{
	rpc_clrsetl(rpc, RPC_PHYINT, RPC_PHYINT_WP, enable ? RPC_PHYINT_WP : 0);
}

static void rpc_hf_unlock(struct rpc_info *rpc, u32 addr)
{
	rpc_hf_write_cmd(rpc, addr + RPC_CFI_UNLOCK1,
			 RPC_CFI_CMD_UNLOCK_START);
	rpc_hf_write_cmd(rpc, addr + RPC_CFI_UNLOCK2,
			 RPC_CFI_CMD_UNLOCK_ACK);
}

static inline int rpc_hf_status(struct rpc_info *rpc, u32 addr,
				int iterations, int delay)
{
	int ret;
	u16 status = 0;

	while (iterations-- > 0) {
		rpc_hf_write_cmd(rpc, addr + RPC_CFI_UNLOCK1,
				 RPC_CFI_CMD_READ_STATUS);
		rpc_hf_read_reg(rpc, addr, &status, RPC_SIZE_DUAL_16BIT);

		if (status & RPC_CFI_STATUS_DRB)
			break;

		if (delay < 10000)
			usleep_range(delay, delay * 2);
		else
			msleep(delay / 1000);
	}

	if (!(status & RPC_CFI_STATUS_DRB)) {
		ret = -ETIMEDOUT;
		goto out;
	}

	if (status & (RPC_CFI_STATUS_PSB | RPC_CFI_STATUS_ESB)) {
		ret = -EIO;
		goto out;
	}

	return 0;

out:
	/* Reset the flash */
	rpc_hf_write_cmd(rpc, 0, RPC_CFI_CMD_RESET);
	return ret;
}

static int rpc_hf_sector_erase(struct rpc_info *rpc, u32 addr)
{
	rpc_hf_unlock(rpc, addr);
	rpc_hf_write_cmd(rpc, addr + RPC_CFI_UNLOCK1, RPC_CFI_CMD_ERASE_START);
	rpc_hf_unlock(rpc, addr);
	rpc_hf_write_cmd(rpc, addr, RPC_CFI_CMD_ERASE_SECTOR);

	return rpc_hf_status(rpc, addr, 1000, 10000);
}

/* Flash read */
static int rpc_hf_mtd_read(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf)
{
	struct rpc_hf_info *hf = mtd->priv;
	struct rpc_info *rpc = hf->priv;

	mutex_lock(&hf->lock);
	rpc_do_read_flash(rpc, from, len, buf, mtd->size > RPC_READ_ADDR_SIZE);
	mutex_unlock(&hf->lock);

	*retlen = len;
	return 0;
}

/* Flash erase */
static int rpc_hf_mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct rpc_hf_info *hf = mtd->priv;
	struct rpc_info *rpc = hf->priv;
	u32 addr, end;
	int ret = 0;

	if (mtd_mod_by_eb(instr->addr, mtd)) {
		dev_dbg(mtd->dev.parent, "%s: unaligned address\n", __func__);
		return -EINVAL;
	}

	if (mtd_mod_by_eb(instr->len, mtd)) {
		dev_dbg(mtd->dev.parent, "%s: unaligned length\n", __func__);
		return -EINVAL;
	}

	end = instr->addr + instr->len;

	mutex_lock(&hf->lock);
	for (addr = instr->addr; addr < end; addr += mtd->erasesize) {
		ret = rpc_hf_sector_erase(rpc, addr);

		if (ret)
			break;
	}

	rpc_hf_mode_ext(rpc);
	mutex_unlock(&hf->lock);

	return ret;
}

/* Copy memory to flash */
static int rpc_hf_mtd_write(struct mtd_info *mtd, loff_t offset, size_t len,
			    size_t *retlen, const u_char *src)
{
	struct rpc_hf_info *hf = mtd->priv;
	struct rpc_info *rpc = hf->priv;
	union {
		u8 b[4];
		u16 w[2];
		u32 d;
	} data;
	loff_t addr;
	size_t size, cnt;
	int ret, idx;
	u8 last;

	ret = 0;
	*retlen = 0;
	cnt = len;
	idx = 0;

	mutex_lock(&hf->lock);

	/* Handle unaligned start */
	if (offset & 0x1) {
		offset--;
		data.b[idx] = readb(rpc->read_area + offset);
		idx++;
	}

	/* Handle unaligned end */
	addr = offset + idx + len;
	last = addr & 0x1 ? readb(rpc->read_area + addr) : 0xFF;

	addr = offset - mtd_mod_by_eb(offset, mtd);
	size = mtd->erasesize - (offset - addr);

	while (cnt) {
		if (size > cnt)
			size = cnt;

		cnt -= size;
		while (size) {
			rpc_hf_unlock(rpc, addr);
			rpc_hf_write_cmd(rpc,
					 addr + RPC_CFI_UNLOCK1,
					 RPC_CFI_CMD_WRITE);

			if (rpc_wbuf_available(rpc) && (size > 0x7)) {
				u32 wbuf = 0;
				int block = size >= RPC_WBUF_SIZE ?
					RPC_WBUF_SIZE : size & ~0x7;

				rpc_hf_wbuf_enable(rpc, offset);
				offset += block;

				block >>= 3;
				while (block--) {
					while (idx < 4) {
						data.b[idx++] = *src++;
						size--;
					}
					rpc_wbuf_writel(rpc, wbuf, data.d);
					wbuf += 4;

					idx = 0;
					while (idx < 4) {
						data.b[idx++] = *src++;
						size--;
					}
					rpc_wbuf_writel(rpc, wbuf, data.d);
					wbuf += 4;

					idx = 0;
				}

				rpc_writel(rpc, RPC_SMCR,
					   RPC_SMCR_SPIWE | RPC_SMCR_SPIE);
			} else {
				enum rpc_size bits;

				while (idx < 4) {
					data.b[idx++] = *src++;
					size--;

					if (!size)
						break;
				}

				if (idx & 0x1)
					data.b[idx++] = last;

				switch (idx) {
				case 2:
					bits = RPC_SIZE_DUAL_16BIT;
					break;
				default:
					bits = RPC_SIZE_DUAL_32BIT;
					break;
				}

				rpc_hf_write_mem(rpc, offset, data.w, bits);
				offset += idx;
				idx = 0;
			}

			ret = rpc_hf_status(rpc, addr, 1000000, 10);
			if (ret)
				goto out;
		}

		size = mtd->erasesize;
		addr += size;
		offset = addr;
		*retlen = len - cnt;
	}

out:
	rpc_hf_mode_ext(rpc);
	mutex_unlock(&hf->lock);
	return ret;
}

static int rpc_hf_hw_init(struct rpc_info *rpc, u32 *id, u32 *size)
{
	u16 data[2] = { 0, 0 };
	u32 sz;
	int ret = 0;

	rpc_hf_mode_ext(rpc);

	rpc_hf_wp(rpc, 0);

	rpc_hf_unlock(rpc, 0);
	rpc_hf_write_cmd(rpc, RPC_CFI_UNLOCK1, RPC_CFI_CMD_READ_ID);

	rpc_hf_read_reg(rpc, 0x0, data, RPC_SIZE_DUAL_32BIT);
	if  ((data[0] & RPC_CFI_ID_MASK) != RPC_CFI_ID_MAN_SPANSION ||
	     (data[1] & RPC_CFI_ID_MASK) != RPC_CFI_ID_TYPE_HYPERFLASH) {
		ret = -ENXIO;
		goto out;
	}

	if (id)
		*id = data[0] | data[1] << 16;

	rpc_hf_read_reg(rpc, 0x27 << 1, data, RPC_SIZE_DUAL_16BIT);
	sz = 1 << data[0];

	if (sz & (RPC_HF_ERASE_SIZE - 1)) {
		ret = -EINVAL;
		goto out;
	}

	if (size)
		*size = sz;
out:
	rpc_hf_write_cmd(rpc, 0, RPC_CFI_CMD_RESET);
	rpc_hf_mode_ext(rpc);
	return ret;
}

int rpc_hf_probe(struct platform_device *pdev)
{
	struct rpc_info *rpc;
	struct rpc_hf_info *hf;
	struct mtd_info *mtd;
	u32 flash_id, flash_size;
	int ret;

	rpc = dev_get_drvdata(pdev->dev.parent);
	if (!rpc) {
		dev_err(&pdev->dev, "invalid data\n");
		return -EINVAL;
	}

	hf = devm_kzalloc(&pdev->dev, sizeof(*hf), GFP_KERNEL);
	if (!hf) {
		dev_err(&pdev->dev, "allocation failed\n");
		return -ENOMEM;
	}

	mutex_init(&hf->lock);
	mtd_set_of_node(&hf->mtd, rpc->flash);
	hf->priv = rpc;

	ret = clk_prepare_enable(rpc->clk);
	if (ret) {
		dev_err(&pdev->dev, "cannot prepare clock\n");
		return ret;
	}

	ret = rpc_hf_hw_init(rpc, &flash_id, &flash_size);
	if (ret) {
		dev_err(&pdev->dev, "initialization failed\n");
		goto error;
	}

	mtd = &hf->mtd;
	mtd->name = "HyperFlash";
	mtd->dev.parent = &pdev->dev;
	mtd->type = MTD_NORFLASH;
	mtd->flags = MTD_CAP_NORFLASH;
	mtd->size = flash_size;
	mtd->writesize = 1;
	mtd->writebufsize = RPC_WBUF_SIZE;
	mtd->erasesize = RPC_HF_ERASE_SIZE;
	mtd->owner = THIS_MODULE;
	mtd->priv = hf;
	mtd->_erase = rpc_hf_mtd_erase;
	mtd->_write = rpc_hf_mtd_write;
	mtd->_read = rpc_hf_mtd_read;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "MTD registration failed\n");
		goto error;
	}

	platform_set_drvdata(pdev, hf);
	dev_info(&pdev->dev, "probed flash id:%x\n", flash_id);
	return 0;

error:
	clk_disable_unprepare(rpc->clk);
	return ret;
}

static int rpc_hf_remove(struct platform_device *pdev)
{
	struct rpc_hf_info *hf = platform_get_drvdata(pdev);
	struct rpc_info *rpc = hf->priv;

	mtd_device_unregister(&hf->mtd);
	clk_disable_unprepare(rpc->clk);
	return 0;
}

/* platform driver interface */
static struct platform_driver rpc_hf_platform_driver = {
	.probe		= rpc_hf_probe,
	.remove		= rpc_hf_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "renesas-rpc-hyperflash",
	},
};

module_platform_driver(rpc_hf_platform_driver);

MODULE_ALIAS("renesas-rpc-hyperflash");
MODULE_AUTHOR("Cogent Embedded Inc. <sources@cogentembedded.com>");
MODULE_DESCRIPTION("Renesas RPC HyperFlash Driver");
MODULE_LICENSE("GPL");
