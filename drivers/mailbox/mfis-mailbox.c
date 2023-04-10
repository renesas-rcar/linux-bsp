// SPDX-License-Identifier: BSD-3-Clause
/*
 * Renesas SCP Software

 * Copyright (c) 2020, Renesas Electronics Corporation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/* MFIS CPU Communication Control Register from CA[i] to G4MH[k] (i=0-7, k=0-1) */
#define MFIS_OFFSET_AM_K_IICR_I(k, i) (0x1480 + 0x1008 * (i) + 0x0100 * (k))
/* MFIS CPU Communication Control Register from G4MH[k] to CA[i] (i=0-7, k=0-1) */
#define MFIS_OFFSET_AM_K_EICR_I(k, i) (0xA484 + 0x0008 * (i) + 0x1000 * (k))

/** CH[n] from CA to G4MH */
#define MFIS_TO_G4MH_CH7	MFIS_OFFSET_AM_K_IICR_I(0, 7)

/** CH[n] from G4MH to CA */
#define MFIS_FROM_G4MH_CH7	MFIS_OFFSET_AM_K_EICR_I(0, 7)

static int mfis_send_data(struct mbox_chan *link, void *data)
{
	void __iomem *reg = link->con_priv;
	/*Trigger interrupt request to firmware(SCP)*/
	iowrite32(0x1, reg);

	return 0;
}

static irqreturn_t mfis_rx_interrupt(int irq, void *data)
{
	struct mbox_chan *link = data;
	void __iomem *reg = link->con_priv;

	mbox_chan_received_data(link, 0);

	/* Clear interrupt register */
	iowrite32(0x0, reg);

	return IRQ_HANDLED;
}

static int mfis_startup(struct mbox_chan *link)
{
	struct mbox_controller *mbox = link->mbox;
	struct device *dev = mbox->dev;
	int irq;
	int ret;

	irq = of_irq_get(dev->of_node, 0);

	ret = request_irq(irq, mfis_rx_interrupt,
			  IRQF_SHARED, "mfis-mbox", link);
	if (ret) {
		dev_err(dev,
			"Unable to acquire IRQ %d\n", irq);
		return ret;
	}
	return 0;
}

static void mfis_shutdown(struct mbox_chan *link)
{
	struct mbox_controller *mbox = link->mbox;
	struct device *dev = mbox->dev;
	int irq;

	irq = of_irq_get(dev->of_node, 0);

	free_irq(irq, link);
}

static bool mfis_last_tx_done(struct mbox_chan *link)
{
	return true;
}

static const struct mbox_chan_ops mfis_chan_ops = {
	.send_data = mfis_send_data,
	.startup = mfis_startup,
	.shutdown = mfis_shutdown,
	.last_tx_done = mfis_last_tx_done
};

static int mfis_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mbox_controller *mbox;
	void __iomem *reg;
	int ret, count = 0, i;

	while
	(of_get_address(dev->of_node, count++, NULL, NULL));
	count--;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	mbox->num_chans = count;
	mbox->chans = devm_kcalloc(dev, mbox->num_chans, sizeof(*mbox->chans),
				   GFP_KERNEL);
	if (!mbox->chans)
		return -ENOMEM;

	for (i = 0; i < mbox->num_chans; i++) {
		reg = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(reg))
			return PTR_ERR(reg);

		mbox->chans[i].mbox = mbox;
		mbox->chans[i].con_priv = reg;
	}

	mbox->txdone_poll = true;
	mbox->txdone_irq = false;
	mbox->txpoll_period = 1;
	mbox->ops = &mfis_chan_ops;
	mbox->dev = dev;

	ret = mbox_controller_register(mbox);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mbox);
	dev_info(dev, "MFIS mailbox enabled with %d chan%s.\n",
		 mbox->num_chans, mbox->num_chans == 1 ? "" : "s");

	return 0;
}

static const struct of_device_id mfis_mbox_of_match[] = {
	{ .compatible = "renesas,mfis-mbox", },
	{},
};
MODULE_DEVICE_TABLE(of, mfis_mbox_of_match);

static struct platform_driver mfis_mbox_driver = {
	.driver = {
		.name = "mfis-mbox",
		.of_match_table = mfis_mbox_of_match,
	},
	.probe	= mfis_mbox_probe,
	//.remove = mfis_mbox_remove,
};
module_platform_driver(mfis_mbox_driver);
MODULE_DESCRIPTION("Renesas MFIS mailbox driver");
MODULE_LICENSE("GPL v2");
