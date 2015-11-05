/*
 * SuperH Mobile SDHI
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 * Copyright (C) 2016 Sang Engineering, Wolfram Sang
 * Copyright (C) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on "Compaq ASIC3 support":
 *
 * Copyright 2001 Compaq Computer Corporation.
 * Copyright 2004-2005 Phil Blundell
 * Copyright 2007-2008 OpenedHand Ltd.
 *
 * Authors: Phil Blundell <pb@handhelds.org>,
 *	    Samuel Ortiz <sameo@openedhand.com>
 *
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/mfd/tmio.h>
#include <linux/sh_dma.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl-state.h>
#include <linux/regulator/consumer.h>

#include "tmio_mmc.h"

#define HOST_MODE           0xe4

#define host_to_priv(host) container_of((host)->pdata, struct sh_mobile_sdhi, mmc_data)

struct sh_mobile_sdhi_scc {
	unsigned long clk;	/* clock for SDR104 */
	u32 tap;		/* sampling clock position for SDR104 */
};

struct sh_mobile_sdhi_of_data {
	unsigned long tmio_flags;
	unsigned long capabilities;
	unsigned long capabilities2;
	enum dma_slave_buswidth dma_buswidth;
	dma_addr_t dma_rx_offset;
	unsigned bus_shift;
	unsigned int max_blk_count;
	unsigned short max_segs;
	bool sdbuf_64bit;
	int scc_offset;
	struct sh_mobile_sdhi_scc *taps;
	int taps_num;
};

static const struct sh_mobile_sdhi_of_data of_default_cfg = {
	.tmio_flags = TMIO_MMC_HAS_IDLE_WAIT,
};

static const struct sh_mobile_sdhi_of_data of_rcar_gen1_compatible = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_WRPROTECT_DISABLE |
			  TMIO_MMC_CLK_ACTUAL,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ,
};

/* Definitions for sampling clocks */
static struct sh_mobile_sdhi_scc rcar_gen2_scc_taps[] = {
	{
		.clk = 156000000,
		.tap = 0x00000703,
	},
	{
		.clk = 0,
		.tap = 0x00000300,
	},
};

static const struct sh_mobile_sdhi_of_data of_rcar_gen2_compatible = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_WRPROTECT_DISABLE |
			  TMIO_MMC_CLK_ACTUAL | TMIO_MMC_MIN_RCAR2,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ,
	.dma_buswidth	= DMA_SLAVE_BUSWIDTH_4_BYTES,
	.dma_rx_offset	= 0x2000,
	.scc_offset = 0x0300,
	.taps = rcar_gen2_scc_taps,
	.taps_num = ARRAY_SIZE(rcar_gen2_scc_taps),
};

/* Definitions for sampling clocks */
static struct sh_mobile_sdhi_scc rcar_gen3_scc_taps[] = {
	{
		.clk = 0,
		.tap = 0x00000300,
	},
};

static const struct sh_mobile_sdhi_of_data of_rcar_gen3_compatible = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_WRPROTECT_DISABLE |
			  TMIO_MMC_CLK_ACTUAL | TMIO_MMC_MIN_RCAR2 |
			  TMIO_MMC_CLK_NO_SLEEP,
	.capabilities	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ,
	.bus_shift	= 2,
	/* Gen3 SDHI DMAC can handle 0xffffffff blk count, but seg = 1 */
	.max_blk_count	= 0xffffffff,
	.max_segs = 1,
	.sdbuf_64bit = true,
	.scc_offset = 0x1000,
	.taps = rcar_gen3_scc_taps,
	.taps_num = ARRAY_SIZE(rcar_gen3_scc_taps),
};

static const struct of_device_id sh_mobile_sdhi_of_match[] = {
	{ .compatible = "renesas,sdhi-shmobile" },
	{ .compatible = "renesas,sdhi-sh73a0", .data = &of_default_cfg, },
	{ .compatible = "renesas,sdhi-r8a73a4", .data = &of_default_cfg, },
	{ .compatible = "renesas,sdhi-r8a7740", .data = &of_default_cfg, },
	{ .compatible = "renesas,sdhi-r8a7778", .data = &of_rcar_gen1_compatible, },
	{ .compatible = "renesas,sdhi-r8a7779", .data = &of_rcar_gen1_compatible, },
	{ .compatible = "renesas,sdhi-r8a7790", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7791", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7792", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7793", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7794", .data = &of_rcar_gen2_compatible, },
	{ .compatible = "renesas,sdhi-r8a7795", .data = &of_rcar_gen3_compatible, },
	{ .compatible = "renesas,mmc-r8a7795", .data = &of_rcar_gen3_compatible, },
	{},
};
MODULE_DEVICE_TABLE(of, sh_mobile_sdhi_of_match);

struct sh_mobile_sdhi_vlt {
	u32 base;		/* base address for IO voltage */
	u32 offset;		/* offset value for IO voltage */
	u32 mask;		/* bit mask position for IO voltage */
	u32 size;		/* bit mask size for IO voltage */
};

struct sh_mobile_sdhi {
	struct clk *clk;
	struct tmio_mmc_data mmc_data;
	struct tmio_mmc_dma dma_priv;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default, *pins_uhs;
	struct sh_mobile_sdhi_vlt vlt;
};

static void sh_mobile_sdhi_sdbuf_width(struct tmio_mmc_host *host, int width)
{
	u32 val;

	/*
	 * see also
	 *	sh_mobile_sdhi_of_data :: dma_buswidth
	 */
	switch (sd_ctrl_read16(host, CTL_VERSION)) {
	case 0x490C:
		val = (width == 32) ? 0x0001 : 0x0000;
		break;
	case 0xCB0D:
		val = (width == 32) ? 0x0000 : 0x0001;
		break;
	case 0xCC10: /* Gen3, SD only */
	case 0xCD10: /* Gen3, SD + MMC */
		if (width == 64)
			val = 0x0000;
		else if (width == 32)
			val = 0x0101;
		else
			val = 0x0001;
		break;
	default:
		/* nothing to do */
		return;
	}

	sd_ctrl_write16(host, HOST_MODE, val);
}

static int sh_mobile_sdhi_clk_enable(struct tmio_mmc_host *host)
{
	struct mmc_host *mmc = host->mmc;
	struct sh_mobile_sdhi *priv = host_to_priv(host);
	int ret = clk_prepare_enable(priv->clk);
	if (ret < 0)
		return ret;

	/*
	 * The clock driver may not know what maximum frequency
	 * actually works, so it should be set with the max-frequency
	 * property which will already have been read to f_max.  If it
	 * was missing, assume the current frequency is the maximum.
	 */
	if (!mmc->f_max)
		mmc->f_max = clk_get_rate(priv->clk);

	/*
	 * Minimum frequency is the minimum input clock frequency
	 * divided by our maximum divider.
	 */
	mmc->f_min = max(clk_round_rate(priv->clk, 1) / 512, 1L);

	/* enable 16bit data access on SDBUF as default */
	sh_mobile_sdhi_sdbuf_width(host, 16);

	return 0;
}

static unsigned int sh_mobile_sdhi_clk_update(struct tmio_mmc_host *host,
					      unsigned int new_clock)
{
	struct sh_mobile_sdhi *priv = host_to_priv(host);
	unsigned int freq, diff, best_freq = 0, diff_min = ~0;
	int i, ret;

	/* tested only on RCar Gen2+ currently; may work for others */
	if (!(host->pdata->flags & TMIO_MMC_MIN_RCAR2))
		return clk_get_rate(priv->clk);

	/*
	 * We want the bus clock to be as close as possible to, but no
	 * greater than, new_clock.  As we can divide by 1 << i for
	 * any i in [0, 9] we want the input clock to be as close as
	 * possible, but no greater than, new_clock << i.
	 */
	for (i = min(9, ilog2(UINT_MAX / new_clock)); i >= 0; i--) {
		freq = clk_round_rate(priv->clk, new_clock << i);
		if (freq > (new_clock << i)) {
			/* Too fast; look for a slightly slower option */
			freq = clk_round_rate(priv->clk,
					      (new_clock << i) / 4 * 3);
			if (freq > (new_clock << i))
				continue;
		}

		diff = new_clock - (freq >> i);
		if (diff <= diff_min) {
			best_freq = freq;
			diff_min = diff;
		}
	}

	ret = clk_set_rate(priv->clk, best_freq);

	return ret == 0 ? best_freq : clk_get_rate(priv->clk);
}

static void sh_mobile_sdhi_clk_disable(struct tmio_mmc_host *host)
{
	struct sh_mobile_sdhi *priv = host_to_priv(host);

	clk_disable_unprepare(priv->clk);
}

static void sh_mobile_sdhi_set_clk_div(struct platform_device *pdev,
				       int state)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct tmio_mmc_host *host = mmc_priv(mmc);

	if (state) {
		sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~0x0100 &
				sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));
		sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, 0x00ff);
	}
}

#define SH_MOBILE_SDHI_SIGNAL_180V	0
#define SH_MOBILE_SDHI_SIGNAL_330V	1

static void sh_mobile_sdhi_set_ioctrl(struct tmio_mmc_host *host, int state)
{
	struct platform_device *pdev = host->pdev;
	void __iomem *pmmr, *ioctrl;
	unsigned int ctrl, mask;
	struct sh_mobile_sdhi *priv =
		container_of(host->pdata, struct sh_mobile_sdhi, mmc_data);
	struct sh_mobile_sdhi_vlt *vlt = &priv->vlt;

	if (!vlt)
		return;

	pmmr = ioremap(vlt->base, 0x04);
	ioctrl = ioremap(vlt->base + vlt->offset, 0x04);

	ctrl = ioread32(ioctrl);
	/* Set 1.8V/3.3V */
	mask = vlt->size << vlt->mask;

	if (state == SH_MOBILE_SDHI_SIGNAL_330V)
		ctrl |= mask;
	else if (state == SH_MOBILE_SDHI_SIGNAL_180V)
		ctrl &= ~mask;
	else {
		dev_err(&pdev->dev, "update_ioctrl: unknown state\n");
		goto err;
	}

	iowrite32(~ctrl, pmmr);
	iowrite32(ctrl, ioctrl);
err:
	iounmap(pmmr);
	iounmap(ioctrl);
}

static int sh_mobile_sdhi_start_signal_voltage_switch(struct mmc_host *mmc,
						      struct mmc_ios *ios)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct sh_mobile_sdhi *priv = host_to_priv(host);
	struct pinctrl_state *pin_state;
	int ret;

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		/* Enable 3.3V Signal */
		if (!IS_ERR(mmc->supply.vqmmc)) {
			/* ioctrl */
			ret = sh_mobile_sdhi_set_ioctrl(host,
						SH_MOBILE_SDHI_SIGNAL_330V);
			if (ret) {
				dev_err(&host->pdev->dev,
					"3.3V pin function control failed\n");
				return -EIO;
			}

			ret = regulator_set_voltage(mmc->supply.vqmmc,
						    3300000, 3300000);
			if (ret) {
				dev_warn(&host->pdev->dev,
					 "3.3V signalling voltage failed\n");
				return -EIO;
			}
		} else {
			return -EIO;
		}
		usleep_range(5000, 10000);
		pin_state = priv->pins_default;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		/* Enable 1.8V Signal */
		if (!IS_ERR(mmc->supply.vqmmc)) {
			ret = regulator_set_voltage(mmc->supply.vqmmc,
						    1800000, 1800000);
			if (ret) {
				dev_warn(&host->pdev->dev,
					 "1.8V signalling voltage failed\n");
				return -EIO;
			}
			/* ioctrl */
			ret = sh_mobile_sdhi_set_ioctrl(host,
						SH_MOBILE_SDHI_SIGNAL_180V);
			if (ret) {
				dev_err(&host->pdev->dev,
					"1.8V pin function control failed\n");
				return -EIO;
			}
		} else {
			return -EIO;
		}
		/* Wait for 5ms */
		usleep_range(5000, 10000);
		pin_state = priv->pins_uhs;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * If anything is missing, assume signal voltage is fixed at
	 * 3.3V and succeed/fail accordingly.
	 */
	if (IS_ERR(priv->pinctrl) || IS_ERR(pin_state))
		return ios->signal_voltage ==
			MMC_SIGNAL_VOLTAGE_330 ? 0 : -EINVAL;

	ret = mmc_regulator_set_vqmmc(host->mmc, ios);
	if (ret)
		return ret;

	return pinctrl_select_state(priv->pinctrl, pin_state);
}

static int sh_mobile_sdhi_card_busy(struct tmio_mmc_host *host)
{
	u32 dat0;

	/* check to see DAT[3:0] */
	dat0 = sd_ctrl_read16_and_16_as_32(host, CTL_STATUS);
	return !(dat0 & TMIO_STAT_DAT0);
}

/* SCC registers */
#define SH_MOBILE_SDHI_SCC_DTCNTL	0x000
#define SH_MOBILE_SDHI_SCC_TAPSET	0x002
#define SH_MOBILE_SDHI_SCC_DT2FF	0x004
#define SH_MOBILE_SDHI_SCC_CKSEL	0x006
#define SH_MOBILE_SDHI_SCC_RVSCNTL	0x008
#define SH_MOBILE_SDHI_SCC_RVSREQ	0x00A

/* Definitions for values the SH_MOBILE_SDHI_SCC_DTCNTL register */
#define SH_MOBILE_SDHI_SCC_DTCNTL_TAPEN		(1 << 0)
/* Definitions for values the SH_MOBILE_SDHI_SCC_CKSEL register */
#define SH_MOBILE_SDHI_SCC_CKSEL_DTSEL		(1 << 0)
/* Definitions for values the SH_MOBILE_SDHI_SCC_RVSCNTL register */
#define SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN	(1 << 0)
/* Definitions for values the SH_MOBILE_SDHI_SCC_RVSREQ register */
#define SH_MOBILE_SDHI_SCC_RVSREQ_RVSERR	(1 << 2)

static inline u32 sd_scc_read32(struct tmio_mmc_host *host, int addr)
{
	struct platform_device *pdev = host->pdev;
	const struct of_device_id *of_id =
		of_match_device(sh_mobile_sdhi_of_match, &pdev->dev);
	const struct sh_mobile_sdhi_of_data *of_data = of_id->data;

	return readl(host->ctl + of_data->scc_offset +
		     (addr << host->bus_shift));
}

static inline void sd_scc_write32(struct tmio_mmc_host *host, int addr,
				  u32 val)
{
	struct platform_device *pdev = host->pdev;
	const struct of_device_id *of_id =
		of_match_device(sh_mobile_sdhi_of_match, &pdev->dev);
	const struct sh_mobile_sdhi_of_data *of_data = of_id->data;

	writel(val, host->ctl + of_data->scc_offset +
	       (addr << host->bus_shift));
}

static bool sh_mobile_sdhi_inquiry_tuning(struct tmio_mmc_host *host)
{
	/* SDHI should be tuning only SDR104 and HS200 */
	if (host->mmc->ios.timing == MMC_TIMING_UHS_SDR104 ||
	    host->mmc->ios.timing == MMC_TIMING_MMC_HS200)
		return true;

	return false;
}

static void sh_mobile_sdhi_init_tuning(struct tmio_mmc_host *host,
							unsigned long *num)
{
	/* set sampling clock selection range */
	if (host->scc_tapnum)
		sd_scc_write32(host, SH_MOBILE_SDHI_SCC_DTCNTL,
				host->scc_tapnum << 16);

	/* Initialize SCC */
	sd_ctrl_write32_as_16_and_16(host, CTL_STATUS, 0x00000000);

	sd_scc_write32(host, SH_MOBILE_SDHI_SCC_DTCNTL,
		SH_MOBILE_SDHI_SCC_DTCNTL_TAPEN |
		sd_scc_read32(host, SH_MOBILE_SDHI_SCC_DTCNTL));

	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~0x0100 &
		sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

	sd_scc_write32(host, SH_MOBILE_SDHI_SCC_CKSEL,
		SH_MOBILE_SDHI_SCC_CKSEL_DTSEL |
		sd_scc_read32(host, SH_MOBILE_SDHI_SCC_CKSEL));

	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, 0x0100 |
		sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

	sd_scc_write32(host, SH_MOBILE_SDHI_SCC_RVSCNTL,
		~SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN &
		sd_scc_read32(host, SH_MOBILE_SDHI_SCC_RVSCNTL));

	sd_scc_write32(host, SH_MOBILE_SDHI_SCC_DT2FF, host->scc_tappos);

	/* Read TAPNUM */
	*num = (sd_scc_read32(host, SH_MOBILE_SDHI_SCC_DTCNTL) >> 16) & 0xff;
}

static int sh_mobile_sdhi_prepare_tuning(struct tmio_mmc_host *host,
							unsigned long tap)
{
	/* Set sampling clock position */
	sd_scc_write32(host, SH_MOBILE_SDHI_SCC_TAPSET, tap);

	return 0;
}

#define SH_MOBILE_SDHI_MAX_TAP	3
static int sh_mobile_sdhi_select_tuning(struct tmio_mmc_host *host,
							unsigned long *tap)
{
	unsigned long tap_num;	/* total number of taps */
	unsigned long tap_cnt;	/* counter of tuning success */
	unsigned long tap_set;	/* tap position */
	unsigned long tap_start;	/* start position of tuning success */
	unsigned long tap_end;	/* end position of tuning success */
	unsigned long ntap;	/* temporary counter of tuning success */
	unsigned long i;

	/* Clear SCC_RVSREQ */
	sd_scc_write32(host, SH_MOBILE_SDHI_SCC_RVSREQ, 0);

	/* Select SCC */
	tap_num = (sd_scc_read32(host,
				 SH_MOBILE_SDHI_SCC_DTCNTL) >> 16) & 0xff;

	tap_cnt = 0;
	ntap = 0;
	tap_start = 0;
	tap_end = 0;
	for (i = 0; i < tap_num * 2; i++) {
		if (tap[i] == 0)
			ntap++;
		else {
			if (ntap > tap_cnt) {
				tap_start = i - ntap;
				tap_end = i - 1;
				tap_cnt = ntap;
			}
			ntap = 0;
		}
	}

	if (ntap > tap_cnt) {
		tap_start = i - ntap;
		tap_end = i - 1;
		tap_cnt = ntap;
	}

	if (tap_cnt >= SH_MOBILE_SDHI_MAX_TAP)
		tap_set = (tap_start + tap_end) / 2 % tap_num;
	else
		return -EIO;

	/* Set SCC */
	sd_scc_write32(host, SH_MOBILE_SDHI_SCC_TAPSET, tap_set);

	/* Enable auto re-tuning */
	sd_scc_write32(host, SH_MOBILE_SDHI_SCC_RVSCNTL,
		SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN |
		sd_scc_read32(host, SH_MOBILE_SDHI_SCC_RVSCNTL));

	return 0;
}

static bool sh_mobile_sdhi_retuning(struct tmio_mmc_host *host)
{
	/* Check SCC error */
	if (sd_scc_read32(host, SH_MOBILE_SDHI_SCC_RVSCNTL) &
	    SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN &&
	    sd_scc_read32(host, SH_MOBILE_SDHI_SCC_RVSREQ) &
	    SH_MOBILE_SDHI_SCC_RVSREQ_RVSERR) {
		/* Clear SCC error */
		sd_scc_write32(host, SH_MOBILE_SDHI_SCC_RVSREQ, 0);
		return true;
	}
	return false;
}

static void sh_mobile_sdhi_hw_reset(struct tmio_mmc_host *host)
{
	struct tmio_mmc_data *pdata = host->pdata;

	if (pdata->flags & TMIO_MMC_HAS_UHS_SCC) {
		/* Reset SCC */
		sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~0x0100 &
			sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

		sd_scc_write32(host, SH_MOBILE_SDHI_SCC_CKSEL,
			~SH_MOBILE_SDHI_SCC_CKSEL_DTSEL &
			sd_scc_read32(host, SH_MOBILE_SDHI_SCC_CKSEL));

		sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, 0x0100 |
			sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

		sd_scc_write32(host, SH_MOBILE_SDHI_SCC_RVSCNTL,
			~SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN &
			sd_scc_read32(host, SH_MOBILE_SDHI_SCC_RVSCNTL));

		sd_scc_write32(host, SH_MOBILE_SDHI_SCC_RVSCNTL,
			~SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN &
			sd_scc_read32(host, SH_MOBILE_SDHI_SCC_RVSCNTL));
	}
}

static int sh_mobile_sdhi_wait_idle(struct tmio_mmc_host *host)
{
	int timeout = 1000;

	while (--timeout && !(sd_ctrl_read16_and_16_as_32(host, CTL_STATUS)
			      & TMIO_STAT_SCLKDIVEN))
		udelay(1);

	if (!timeout) {
		dev_warn(&host->pdev->dev, "timeout waiting for SD bus idle\n");
		return -EBUSY;
	}

	return 0;
}

static int sh_mobile_sdhi_write16_hook(struct tmio_mmc_host *host, int addr)
{
	switch (addr)
	{
	case CTL_SD_CMD:
	case CTL_STOP_INTERNAL_ACTION:
	case CTL_XFER_BLK_COUNT:
	case CTL_SD_CARD_CLK_CTL:
	case CTL_SD_XFER_LEN:
	case CTL_SD_MEM_CARD_OPT:
	case CTL_TRANSACTION_CTL:
	case CTL_DMA_ENABLE:
	case HOST_MODE:
		return sh_mobile_sdhi_wait_idle(host);
	}

	return 0;
}

static int sh_mobile_sdhi_multi_io_quirk(struct mmc_card *card,
					 unsigned int direction, int blk_size)
{
	/*
	 * In Renesas controllers, when performing a
	 * multiple block read of one or two blocks,
	 * depending on the timing with which the
	 * response register is read, the response
	 * value may not be read properly.
	 * Use single block read for this HW bug
	 */
	if ((direction == MMC_DATA_READ) &&
	    blk_size == 2)
		return 1;

	return blk_size;
}

static void sh_mobile_sdhi_enable_dma(struct tmio_mmc_host *host, bool enable)
{
	int dma_width = host->dma->sdbuf_64bit ? 64 : 32;

	sd_ctrl_write16(host, CTL_DMA_ENABLE, enable ? 2 : 0);

	/* enable 32bit access if DMA mode if possibile */
	sh_mobile_sdhi_sdbuf_width(host, enable ? dma_width : 16);
}

static int sh_mobile_sdhi_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
		of_match_device(sh_mobile_sdhi_of_match, &pdev->dev);
	struct sh_mobile_sdhi *priv;
	struct tmio_mmc_data *mmc_data;
	struct tmio_mmc_data *mmd = pdev->dev.platform_data;
	struct tmio_mmc_host *host;
	struct resource *res;
	int irq, ret, i = 0;
	struct tmio_mmc_dma *dma_priv;
	const struct device_node *np = pdev->dev.of_node;
	int clk_rate;
	struct sh_mobile_sdhi_vlt *vlt;
	u32 pfcs[2], mask[2];
	u32 num, tapnum = 0, tappos;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct sh_mobile_sdhi), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mmc_data = &priv->mmc_data;
	dma_priv = &priv->dma_priv;
	vlt = &priv->vlt;

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		dev_err(&pdev->dev, "cannot get clock: %d\n", ret);
		goto eprobe;
	}

	if (np && !of_property_read_u32(np, "renesas,clk-rate", &clk_rate)) {
		if (clk_rate) {
			clk_prepare_enable(priv->clk);
			ret = clk_set_rate(priv->clk, clk_rate);
			if (ret < 0)
				dev_err(&pdev->dev,
					"cannot set clock rate: %d\n", ret);

			clk_disable_unprepare(priv->clk);
		}
	}

	if (np && !of_property_read_u32_array(np, "renesas,pfcs", pfcs, 2)) {
		if (pfcs[0]) {
			vlt->base = pfcs[0];
			vlt->offset = pfcs[1];
		}
	}

	if (np && !of_property_read_u32_array(np, "renesas,id", mask, 2)) {
		vlt->mask = mask[0];
		vlt->size = mask[1];
	}

	if (np && !of_property_read_u32(np, "renesas,mmc-scc-tapnum", &num))
		tapnum = num;

	priv->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!IS_ERR(priv->pinctrl)) {
		priv->pins_default = pinctrl_lookup_state(priv->pinctrl,
						PINCTRL_STATE_DEFAULT);
		priv->pins_uhs = pinctrl_lookup_state(priv->pinctrl,
						"state_uhs");
	}

	host = tmio_mmc_host_alloc(pdev);
	if (!host) {
		ret = -ENOMEM;
		goto eprobe;
	}

	if (of_id && of_id->data) {
		const struct sh_mobile_sdhi_of_data *of_data = of_id->data;
		const struct sh_mobile_sdhi_scc *taps = of_data->taps;

		mmc_data->flags |= of_data->tmio_flags;
		mmc_data->capabilities |= of_data->capabilities;
		mmc_data->capabilities2 |= of_data->capabilities2;
		mmc_data->dma_rx_offset = of_data->dma_rx_offset;
		mmc_data->max_blk_count	= of_data->max_blk_count;
		mmc_data->max_segs = of_data->max_segs;
		dma_priv->dma_buswidth = of_data->dma_buswidth;
		dma_priv->sdbuf_64bit = of_data->sdbuf_64bit;
		host->bus_shift = of_data->bus_shift;
		if (np && !of_property_read_u32(np, "renesas,mmc-scc-tappos",
						&tappos)) {
			host->scc_tappos = tappos;
		} else {
			for (i = 0, taps = of_data->taps;
			     i < of_data->taps_num; i++, taps++) {
				if (taps->clk == 0 || taps->clk == clk_rate) {
					host->scc_tappos = taps->tap;
					break;
				}
			}
			if (taps->clk != 0 && taps->clk != clk_rate)
				dev_warn(&host->pdev->dev, "Unknown clock rate for SDR104 and HS200\n");
		}
	}

	if (of_find_property(np, "sd-uhs-sdr50", NULL))
		mmc_data->capabilities |= MMC_CAP_UHS_SDR50;
	if (of_find_property(np, "sd-uhs-sdr104", NULL))
		mmc_data->capabilities |= MMC_CAP_UHS_SDR104;

	if (mmc_data->capabilities & MMC_CAP_UHS_SDR104) {
		mmc_data->capabilities |= MMC_CAP_HW_RESET;
		mmc_data->flags |= TMIO_MMC_HAS_UHS_SCC;
	}

	host->dma		= dma_priv;
	host->write16_hook	= sh_mobile_sdhi_write16_hook;
	host->clk_enable	= sh_mobile_sdhi_clk_enable;
	host->clk_update	= sh_mobile_sdhi_clk_update;
	host->clk_disable	= sh_mobile_sdhi_clk_disable;
	host->card_busy		= sh_mobile_sdhi_card_busy;
	host->multi_io_quirk	= sh_mobile_sdhi_multi_io_quirk;
	host->set_clk_div	= sh_mobile_sdhi_set_clk_div;
	host->start_signal_voltage_switch =
			sh_mobile_sdhi_start_signal_voltage_switch;
	host->inquiry_tuning = sh_mobile_sdhi_inquiry_tuning;
	host->init_tuning = sh_mobile_sdhi_init_tuning;
	host->prepare_tuning = sh_mobile_sdhi_prepare_tuning;
	host->select_tuning = sh_mobile_sdhi_select_tuning;
	host->retuning = sh_mobile_sdhi_retuning;
	host->hw_reset = sh_mobile_sdhi_hw_reset;
	host->scc_tapnum = tapnum;

	/* Orginally registers were 16 bit apart, could be 32 or 64 nowadays */
	if (resource_size(res) > 0x400)
		host->bus_shift = 2;
	else if (!host->bus_shift &&
	    resource_size(res) > 0x100) /* old way to determine the shift */
		host->bus_shift = 1;

	if (mmd)
		*mmc_data = *mmd;

	dma_priv->filter = shdma_chan_filter;
	dma_priv->enable = sh_mobile_sdhi_enable_dma;

	mmc_data->alignment_shift = 1; /* 2-byte alignment */
	mmc_data->capabilities |= MMC_CAP_MMC_HIGHSPEED;

	/*
	 * All SDHI blocks support 2-byte and larger block sizes in 4-bit
	 * bus width mode.
	 */
	mmc_data->flags |= TMIO_MMC_BLKSZ_2BYTES;

	/*
	 * All SDHI blocks support SDIO IRQ signalling.
	 */
	mmc_data->flags |= TMIO_MMC_SDIO_IRQ;

	/*
	 * All SDHI have CMD12 controll bit
	 */
	mmc_data->flags |= TMIO_MMC_HAVE_CMD12_CTRL;

	/*
	 * All SDHI need SDIO_INFO1 reserved bit
	 */
	mmc_data->flags |= TMIO_MMC_SDIO_STATUS_QUIRK;

	ret = tmio_mmc_host_probe(host, mmc_data);
	if (ret < 0)
		goto efree;

	while (1) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			break;
		i++;
		ret = devm_request_irq(&pdev->dev, irq, tmio_mmc_irq, 0,
				  dev_name(&pdev->dev), host);
		if (ret)
			goto eirq;
	}

	/* There must be at least one IRQ source */
	if (!i) {
		ret = irq;
		goto eirq;
	}

	dev_info(&pdev->dev, "%s base at 0x%08lx max clock rate %u MHz\n",
		 mmc_hostname(host->mmc), (unsigned long)
		 (platform_get_resource(pdev, IORESOURCE_MEM, 0)->start),
		 host->mmc->f_max / 1000000);

	return ret;

eirq:
	tmio_mmc_host_remove(host);
efree:
	tmio_mmc_host_free(host);
eprobe:
	return ret;
}

static int sh_mobile_sdhi_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct tmio_mmc_host *host = mmc_priv(mmc);

	tmio_mmc_host_remove(host);

	return 0;
}

static const struct dev_pm_ops tmio_mmc_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
			pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(tmio_mmc_host_runtime_suspend,
			tmio_mmc_host_runtime_resume,
			NULL)
};

static struct platform_driver sh_mobile_sdhi_driver = {
	.driver		= {
		.name	= "sh_mobile_sdhi",
		.pm	= &tmio_mmc_dev_pm_ops,
		.of_match_table = sh_mobile_sdhi_of_match,
	},
	.probe		= sh_mobile_sdhi_probe,
	.remove		= sh_mobile_sdhi_remove,
};

module_platform_driver(sh_mobile_sdhi_driver);

MODULE_DESCRIPTION("SuperH Mobile SDHI driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sh_mobile_sdhi");
