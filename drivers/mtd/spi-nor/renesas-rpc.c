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

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>

#include "renesas-rpc.h"

static bool use_dma = true;
module_param(use_dma, bool, 0);
MODULE_PARM_DESC(use_dma, "DMA support. 0 = Disable, 1 = Enable");

/* Debug */
#ifdef DEBUG
void rpc_regs_dump(struct rpc_info *rpc)
{
	static u32 regs[] = {
		RPC_CMNCR, RPC_SSLDR, RPC_DRCR, RPC_DRCMR, RPC_DREAR,
		RPC_DROPR, RPC_DRENR, RPC_SMCR, RPC_SMCMR, RPC_SMADR,
		RPC_SMOPR, RPC_SMENR, RPC_SMRDR0, RPC_SMRDR1, RPC_SMWDR0,
		RPC_SMWDR1, RPC_CMNSR, RPC_DRDMCR, RPC_DRDRENR, RPC_SMDMCR,
		RPC_SMDRENR, RPC_PHYCNT, RPC_PHYOFFSET1, RPC_PHYOFFSET2,
		RPC_PHYINT
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
			rpc_readl(rpc, regs[i]));
}
EXPORT_SYMBOL(rpc_regs_dump);
#endif

/* Poll operation end */
int rpc_wait(struct rpc_info *rpc, int timeout)
{
	unsigned long end = jiffies + msecs_to_jiffies(timeout);

	while (!(rpc_readl(rpc, RPC_CMNSR) & RPC_CMNSR_TEND)) {
		if (time_after(jiffies, end)) {
			dev_err(&rpc->pdev->dev, "timed out\n");
			return -ETIMEDOUT;
		}

		cpu_relax();
	}

	return 0;
}
EXPORT_SYMBOL(rpc_wait);

/* DMA support */
static void rpc_dma_complete_func(void *completion)
{
	complete(completion);
}

int rpc_dma_read(struct rpc_info *rpc, void *buf, loff_t from, ssize_t *plen)
{
	struct dma_device *dma_dev;
	enum dma_ctrl_flags flags;
	dma_addr_t dma_dst_addr;
	struct dma_async_tx_descriptor *tx = NULL;
	dma_cookie_t cookie;
	int ret = 0;
	ssize_t len;

	len = *plen;

	if (!rpc->dma_chan || len < RPC_DMA_SIZE_MIN)
		return -ENODEV;

	dma_dev = rpc->dma_chan->device;

	/* Align size to RBURST */
	len -= len % RPC_DMA_BURST;

	dma_dst_addr = dma_map_single(dma_dev->dev, buf, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(dma_dev->dev, dma_dst_addr)) {
		dev_err(&rpc->pdev->dev, "DMA map single failed\n");
		return -ENXIO;
	}

	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	tx = dma_dev->device_prep_dma_memcpy(rpc->dma_chan, dma_dst_addr,
					     rpc->read_area_dma + from,
					     len, flags);
	if (!tx) {
		dev_err(&rpc->pdev->dev, "DMA prepare memcpy failed\n");
		ret = -EIO;
		goto out_dma;
	}

	init_completion(&rpc->comp);
	tx->callback = rpc_dma_complete_func;
	tx->callback_param = &rpc->comp;

	cookie = tx->tx_submit(tx);
	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(&rpc->pdev->dev, "DMA tx submit failed\n");
		goto out_dma;
	}

	dma_async_issue_pending(rpc->dma_chan);
	wait_for_completion(&rpc->comp);

	/* Update length with actual transfer size */
	*plen = len;

out_dma:
	dma_unmap_single(dma_dev->dev, dma_dst_addr, len, DMA_FROM_DEVICE);
	return ret;
}
EXPORT_SYMBOL(rpc_dma_read);

/* Read */
void rpc_do_read_flash(struct rpc_info *rpc, loff_t from,
		       size_t len, u_char *buf, bool addr32)
{
	while (len > 0) {
		ssize_t readlen;
		loff_t _from;
		int ret;

		/* Setup ext mode address */
		rpc_clrsetl(rpc, RPC_DREAR,
			    RPC_DREAR_EAV(0xFF) | RPC_DREAR_EAC(7),
			    addr32 ?
			    RPC_DREAR_EAV(from >> 25) | RPC_DREAR_EAC(1) :
			    0);

		rpc_clrsetl(rpc, RPC_DRENR,
			    RPC_DRENR_ADE(0xF),
			    addr32 ?
			    /* bits 31-0 */
			    RPC_DRENR_ADE(0xF) :
			    /* bits 23-0 */
			    RPC_DRENR_ADE(0x7));

		/* Use address bits [25...0] */
		_from = from & RPC_READ_ADDR_MASK;

		readlen = RPC_READ_ADDR_MASK - _from + 1;
		readlen = readlen > len ? len : readlen;

		ret = rpc_dma_read(rpc, buf, _from, &readlen);
		if (ret)
			memcpy_fromio(buf, rpc->read_area + _from, readlen);

		buf += readlen;
		from += readlen;
		len -= readlen;
	}
}
EXPORT_SYMBOL(rpc_do_read_flash);

/* Own clock setup */
static int rpc_own_clk_set_rate(struct rpc_info *rpc, u32 max_clk_rate)
{
	unsigned long rate = clk_get_rate(rpc->clk);
	u32 ratio;

	ratio = DIV_ROUND_UP(rate, max_clk_rate * 2) >> 1;
	if (ratio > RPC_DIVREG_RATIO_MAX)
		ratio = RPC_DIVREG_RATIO_MAX;

	rpc_clrsetl(rpc, RPC_DIVREG,
		    RPC_DIVREG_RATIO(0x3),
		    RPC_DIVREG_RATIO(ratio));
	return 0;
}

static int rpc_probe(struct platform_device *pdev)
{
	struct rpc_info *rpc;
	struct resource *res;
	const char *name = NULL;
	u32 rate = 0;
	int ret;

	rpc = devm_kzalloc(&pdev->dev, sizeof(*rpc), GFP_KERNEL);
	if (!rpc) {
		dev_err(&pdev->dev, "allocation failed\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rpc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rpc->base)) {
		dev_err(&pdev->dev, "cannot get base resource\n");
		return PTR_ERR(rpc->base);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	rpc->read_area = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rpc->read_area)) {
		dev_err(&pdev->dev, "cannot get read resource\n");
		return PTR_ERR(rpc->read_area);
	}

	if (resource_size(res) & RPC_READ_ADDR_MASK) {
		dev_err(&pdev->dev, "invalid read resource\n");
		return -EINVAL;
	}

	rpc->read_area_dma = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	rpc->write_area = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rpc->write_area)) {
		dev_warn(&pdev->dev, "cannot get write resource\n");
		rpc->write_area = NULL;
	}

	rpc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(rpc->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return PTR_ERR(rpc->clk);
	}

	rpc->flash = of_get_next_available_child(pdev->dev.of_node, NULL);
	if (!rpc->flash) {
		dev_err(&pdev->dev, "no flash device to configure\n");
		return -ENOTSUPP;
	}

	rpc->mtdtype = of_find_property(pdev->dev.of_node, "dual", NULL) ?
		       RPC_DUAL : RPC_SINGLE;

	of_property_read_u32(rpc->flash, "spi-max-frequency", &rate);

	if (of_device_is_compatible(rpc->flash, "jedec,spi-nor")) {
		name = "renesas-rpc-qspi";
		if (!rate)
			rate = 50000000;
	} else if (of_device_is_compatible(rpc->flash, "cfi-flash")) {
		rpc->mtdtype = RPC_DUAL;
		name = "renesas-rpc-hyperflash";
		if (!rate)
			rate = 80000000;
	}

	if (!name) {
		dev_err(&pdev->dev, "no supported flash device detected\n");
		ret = -ENODEV;
		goto error;
	}

	if (rate) {
		ret = of_device_is_compatible(pdev->dev.of_node,
					      "renesas,rpc-r8a77970") ?
		      rpc_own_clk_set_rate(rpc, rate) :
		      clk_set_rate(rpc->clk, rate);
		if (ret) {
			dev_err(&pdev->dev, "clock rate setup failed\n");
			goto error;
		}
	}

	if (use_dma) {
		dma_cap_mask_t mask;

		dma_cap_zero(mask);
		dma_cap_set(DMA_MEMCPY, mask);
		rpc->dma_chan = dma_request_channel(mask, NULL, NULL);
		if (!rpc->dma_chan)
			dev_warn(&pdev->dev, "DMA channel request failed\n");
		else
			dev_info(&pdev->dev, "using DMA read (%s)\n",
				 dma_chan_name(rpc->dma_chan));
	}

	platform_set_drvdata(pdev, rpc);
	rpc->pdev = platform_device_register_data(&pdev->dev, name, -1, NULL, 0);
	if (IS_ERR(rpc->pdev)) {
		dev_err(&pdev->dev, "%s device registration failed\n", name);
		ret = PTR_ERR(rpc->pdev);
		goto error;
	}

	return 0;

error:
	if (rpc->dma_chan)
		dma_release_channel(rpc->dma_chan);

	of_node_put(rpc->flash);
	return ret;
}

static int rpc_remove(struct platform_device *pdev)
{
	struct rpc_info *rpc = platform_get_drvdata(pdev);

	if (rpc->pdev)
		platform_device_unregister(rpc->pdev);
	if (rpc->dma_chan)
		dma_release_channel(rpc->dma_chan);
	if (rpc->flash)
		of_node_put(rpc->flash);

	return 0;
}

static const struct of_device_id rpc_of_match[] = {
	{ .compatible = "renesas,rpc-r8a7795" },
	{ .compatible = "renesas,rpc-r8a7796" },
	{ .compatible = "renesas,rpc-r8a77965" },
	{
		.compatible = "renesas,rpc-r8a77970",
		.data = (void *)RPC_OWN_CLOCK_DIVIDER,
	},
	{ .compatible = "renesas,rpc-r8a77980" },
	{ },
};

/* platform driver interface */
static struct platform_driver rpc_platform_driver = {
	.probe		= rpc_probe,
	.remove		= rpc_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "renesas-rpc",
		.of_match_table = of_match_ptr(rpc_of_match),
	},
};

module_platform_driver(rpc_platform_driver);

MODULE_ALIAS("renesas-rpc");
MODULE_AUTHOR("Cogent Embedded Inc. <sources@cogentembedded.com>");
MODULE_DESCRIPTION("Renesas RPC Driver");
MODULE_LICENSE("GPL");
