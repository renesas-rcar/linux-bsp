/*
 * Remote processor machine-specific module for R-Car Gen3 - Cortex-R7
 *
 * Copyright (C) 2019 Renesas Electronics
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/delay.h>

#include "remoteproc_internal.h"

#include <misc/rcar-mfis/rcar_mfis_public.h>
#define MFIS_CHANNEL 0 //use this mfis channel to trigger interrupts

static char *rcar_cr7_fw_name;
module_param(rcar_cr7_fw_name, charp, S_IRUGO);
MODULE_PARM_DESC(rcar_cr7_fw_name,
		 "Name of CR7 firmware file in /lib/firmware (if not specified defaults to 'rproc-cr7-fw')");

#define RST_BASE                0xE6160000
#define RST_CR7BAR_OFFSET       0x00000070

#define SYSC_BASE               0xE6180000
#define SYSC_PWRSR7_OFFSET      0x00000240
#define SYSC_PWRONCR7_OFFSET    0x0000024C

#define APMU_CR7PSTR            0XE6153040

#define CPG_BASE                0xE6150000
#define CPG_WPCR_OFFSET         0x00000904
#define CPG_WPR_OFFSET          0x00000900

#define MSSR_BASE               0xE6150000 //same as CPG
#define MSSR_SRCR2_OFFSET       0x000000B0
#define MSSR_SRSTCLR2_OFFSET    0x00000948

#define CR7_BASE                0xF0100000
#define CR7_WBPWRCTLR_OFFSET    0x00000F80
#define CR7_WBCTLR_OFFSET       0x00000000


/**
 * struct rcar_cr7_rproc - rcar_cr7 remote processor instance state
 * @rproc: rproc handle
 * @mem: internal memory regions data
 */
struct rcar_cr7_rproc {
	struct rproc *rproc;
	struct work_struct workqueue;
};

/**
 * handle_event() - inbound virtqueue message workqueue function
 *
 * This callback is registered with the R-Car MFIS atomic notifier
 * chain and is called every time the remote processor (Cortex-R7)
 * wants to notify us of pending messages available.
 */
static void handle_event(struct work_struct *work)
{
        struct rcar_cr7_rproc *rrproc =
                container_of(work, struct rcar_cr7_rproc, workqueue);

	/* Process incoming buffers on all our vrings */
        rproc_vq_interrupt(rrproc->rproc, 0);
        rproc_vq_interrupt(rrproc->rproc, 1);
}

/**
 * cr7_interrupt_cb()
 *
 * This callback is registered with the R-Car MFIS atomic notifier
 * chain and is called every time the remote processor (Cortex-R7)
 * wants to notify us of pending messages available.
 */
static int cr7_interrupt_cb(struct notifier_block *self, unsigned long action, void *data)
{
	struct rcar_cr7_rproc *rrproc = (struct rcar_cr7_rproc *)data;
	struct device *dev = rrproc->rproc->dev.parent;

	dev_dbg(dev, "%s\n", __FUNCTION__);

	schedule_work(&rrproc->workqueue);

	return NOTIFY_DONE;
}

static struct notifier_block rcar_cr7_notifier_block = {
	.notifier_call = cr7_interrupt_cb,
};

static int rcar_cr7_rproc_start(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	//struct rcar_cr7_rproc *rrproc = (struct rcar_cr7_rproc *)rproc->priv;

	void* mmio_cpg_base;
	void* mmio_rst_base;
	void* mmio_sysc_base;
	void* mmio_apmu_base;
	u32 regval;


	dev_dbg(dev, "%s\n", __FUNCTION__);

	// CR7 Power-Up Sequence (Sec. 5A.3.3 R-Car Gen3 HW User Manual)
	//////// 1. clear write protection for CPG register
	mmio_cpg_base = ioremap_nocache(CPG_BASE, 4);
	// Clear CPG Write Protect (CPGWPCR.WPE)
	iowrite32(0x5a5affff, (mmio_cpg_base + CPG_WPR_OFFSET));
	iowrite32(0xa5a50000, (mmio_cpg_base + CPG_WPCR_OFFSET));

	//////// 2. Set boot address
	// Get Reset Controller node (RST)
	mmio_rst_base = ioremap_nocache(RST_BASE, 4);
	if (rproc->bootaddr & ~0xfffc0000)
		dev_warn(dev, "Boot address (0x%x) not aligned!\n", rproc->bootaddr);
	regval = (rproc->bootaddr & 0xfffc0000); //Set Boot Addr
	regval |= 0x10; //Enable BAR
	iowrite32(regval, (mmio_rst_base + RST_CR7BAR_OFFSET));

	//////// 3. CR7 Power-On set
	// Get System Controller node (SYSC)
	mmio_sysc_base = ioremap_nocache(SYSC_BASE, 4);
	regval = 0x1; //Start power-resume sequence
	iowrite32(regval, (mmio_sysc_base + SYSC_PWRONCR7_OFFSET));


	//////// 4. Wait until Power-On
	// Get Advanced Power Management Unit (APMU)
	// CR7 Power Status Register (CR7PSTR)
	mmio_apmu_base = ioremap_nocache(APMU_CR7PSTR, 4);
	do {
		regval = ioread32(mmio_apmu_base) & 0x3; //APMU_CR7PSTR
		regval |= ioread32(mmio_sysc_base+SYSC_PWRSR7_OFFSET) &0x10;
	} while (regval != 0x10);

	//////// 5. Clear Soft Reset bit
	// MSSR Arm Realtime Core Reset Set
	iowrite32((1<<22), (mmio_cpg_base + MSSR_SRSTCLR2_OFFSET));

	iounmap(mmio_cpg_base);
	iounmap(mmio_rst_base);
	iounmap(mmio_sysc_base);
	iounmap(mmio_apmu_base);

	dev_dbg(dev, "%s: Reset released.\n", __FUNCTION__);
	return 0;
}

static int rcar_cr7_rproc_stop(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	//struct rcar_cr7_rproc *rrproc = (struct rcar_cr7_rproc *)rproc->priv;

	dev_dbg(dev, "%s\n", __FUNCTION__);

	/* Implement me */

	return 0;
}

/* kick a virtqueue */
static void rcar_cr7_rproc_kick(struct rproc *rproc, int vqid)
{
	int ret;
	struct device *dev = rproc->dev.parent;
	struct rcar_mfis_msg msg;
	// struct rcar_cr7_rproc *rrproc = (struct rcar_cr7_rproc *)rproc->priv;
	unsigned int n_tries = 3;

	dev_dbg(dev, "%s\n", __FUNCTION__);

	msg.icr = vqid;
	msg.mbr = 0;

	do {
	    ret = rcar_mfis_trigger_interrupt(MFIS_CHANNEL, msg);
	    if (ret)
		udelay(500);

	} while (ret && n_tries--);

	if (ret) {
		dev_dbg(dev, "%s failed\n", __FUNCTION__);
	}
}

static const struct rproc_ops rcar_cr7_rproc_ops = {
	.start = rcar_cr7_rproc_start,
	.stop = rcar_cr7_rproc_stop,
	.kick = rcar_cr7_rproc_kick,
};

static int rcar_cr7_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_cr7_rproc *rrproc;
	struct rproc *rproc;
	int ret;

	if (dev->of_node) {
		ret = of_reserved_mem_device_init(dev);
		if (ret) {
			dev_err(dev, "device does not have specific CMA pool: %d\n", ret);
			return ret;
		}
	}

	rproc = rproc_alloc(dev, "cr7", &rcar_cr7_rproc_ops, rcar_cr7_fw_name, sizeof(*rrproc));
	if (!rproc) {
		ret = -ENOMEM;
		goto free_mem;
	}

	rrproc = rproc->priv;
	rrproc->rproc = rproc;
	rproc->has_iommu = false;

	INIT_WORK(&rrproc->workqueue, handle_event);

	platform_set_drvdata(pdev, rrproc);

	ret = rcar_mfis_register_notifier(MFIS_CHANNEL, &rcar_cr7_notifier_block, rrproc);
	if (ret) {
		dev_err(dev, "cannot register notifier on mfis channel %d\n", MFIS_CHANNEL);
		goto free_rproc;
	}

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed: %d\n", ret);
		goto unregister_notifier;
	}

	return 0;

unregister_notifier:
	rcar_mfis_unregister_notifier(MFIS_CHANNEL, &rcar_cr7_notifier_block);
	flush_work(&rrproc->workqueue);
free_rproc:
	rproc_free(rproc);
free_mem:
	if (dev->of_node)
		of_reserved_mem_device_release(dev);
	return ret;
}

static int rcar_cr7_rproc_remove(struct platform_device *pdev)
{
	struct rcar_cr7_rproc *rrproc = platform_get_drvdata(pdev);
	struct rproc *rproc = rrproc->rproc;
	struct device *dev = &pdev->dev;

	rcar_mfis_unregister_notifier(MFIS_CHANNEL, &rcar_cr7_notifier_block);
	flush_work(&rrproc->workqueue);
	rproc_del(rproc);
	rproc_free(rproc);
	if (dev->of_node)
		of_reserved_mem_device_release(dev);

	return 0;
}

static const struct of_device_id rcar_cr7_rproc_of_match[] = {
	{ .compatible = "renesas,rcar-cr7", },
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_cr7_rproc_of_match);

static struct platform_driver rcar_cr7_rproc_driver = {
	.probe = rcar_cr7_rproc_probe,
	.remove = rcar_cr7_rproc_remove,
	.driver = {
		.name = "rcar-cr7-rproc",
		.of_match_table = of_match_ptr(rcar_cr7_rproc_of_match),
	},
};

module_platform_driver(rcar_cr7_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("RCAR_CR7 Remote Processor control driver");
