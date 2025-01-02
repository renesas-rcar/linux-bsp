// SPDX-License-Identifier: GPL-2.0
/* R-Car MFIS driver
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/notifier.h>

#include "rcar_mfis_drv.h"
#include <misc/rcar-mfis/rcar_mfis_public.h>

#define IICR(n) (0x0000 + (n) * 0x1000)
#define EICR(n) (0x0004 + (n) * 0x1000)
#define IMBR(n) (0x0040 + (n) * 0x1000)
#define EMBR(n) (0x0044 + (n) * 0x1000)

static struct rcar_mfis_priv *rcmfis_priv;

static irqreturn_t mfis_irq_handler(int irq, void *data)
{
	u32 value = 0;
	struct rcar_mfis_msg msg;
	struct rcar_mfis_ch *rcar_mfis_ch = (struct rcar_mfis_ch *)data;
	unsigned int ch = rcar_mfis_ch->id;

	value = rcar_mfis_reg_read(rcmfis_priv, IICR(ch));
	if (value & 0x1) {
		msg.mbr = rcar_mfis_reg_read(rcmfis_priv, IMBR(ch));
		msg.icr = value >> 1;

		atomic_notifier_call_chain(&rcar_mfis_ch->notifier_head, msg.icr,
					   rcar_mfis_ch->notifier_data);

		/* clear interrupt flag */
		rcar_mfis_reg_write(rcmfis_priv, IICR(ch), value & (~0x1));

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static struct rcar_mfis_ch *rcar_mfis_channel_get(unsigned int channel)
{
	struct rcar_mfis_ch *rcar_mfis_ch;
	int i;

	for (i = 0; i < NUM_MFIS_CHANNELS; i++) {
		if (rcmfis_priv->channels[i].initialized &&
		    rcmfis_priv->channels[i].id == channel) {
			rcar_mfis_ch = &rcmfis_priv->channels[i];
			break;
		}
	}

	return rcar_mfis_ch;
}

/****** Exported functions ******/

int rcar_mfis_trigger_interrupt(int channel, struct rcar_mfis_msg msg)
{
	struct rcar_mfis_ch *rcar_mfis_ch;
	int ret;
	u32 icr;

	rcar_mfis_ch = rcar_mfis_channel_get(channel);
	if (!rcar_mfis_ch)
		return -EINVAL;

	/* Check whether the remote proccessor is still processing a previous interrupt */
	icr = rcar_mfis_reg_read(rcmfis_priv, EICR(channel));
	if (icr & 0x1)
		return -EBUSY;

	rcar_mfis_reg_write(rcmfis_priv, EMBR(channel), msg.mbr);
	rcar_mfis_reg_write(rcmfis_priv, EICR(channel), (msg.icr << 1) | 1);

	return ret;
}
EXPORT_SYMBOL(rcar_mfis_trigger_interrupt);

int rcar_mfis_register_notifier(int channel, struct notifier_block *nb, void *data)
{
	struct rcar_mfis_ch *rcar_mfis_ch;
	struct atomic_notifier_head *nh;

	rcar_mfis_ch = rcar_mfis_channel_get(channel);
	if (!rcar_mfis_ch)
		return -EINVAL;

	if (!rcmfis_priv)
		return -ENXIO;

	rcar_mfis_ch->notifier_data = data;

	nh = &rcar_mfis_ch->notifier_head;
	return atomic_notifier_chain_register(nh, nb);
}
EXPORT_SYMBOL(rcar_mfis_register_notifier);

int rcar_mfis_unregister_notifier(int channel, struct notifier_block *nb)
{
	struct rcar_mfis_ch *rcar_mfis_ch;
	struct atomic_notifier_head *nh;

	rcar_mfis_ch = rcar_mfis_channel_get(channel);
	if (!rcar_mfis_ch)
		return -EINVAL;

	if (!rcmfis_priv)
		return -ENXIO;

	nh = &rcar_mfis_ch->notifier_head;
	return atomic_notifier_chain_unregister(nh, nb);
}
EXPORT_SYMBOL(rcar_mfis_unregister_notifier);

static int rcar_mfis_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	u32 __iomem *mmio_base, *unlock;
	struct resource *res;
	u32 num_mfis_channels;
	u32 value;
	int ret, i;
	int irq;

	num_mfis_channels = of_property_count_elems_of_size(dev->of_node, "renesas,mfis-channels",
							    sizeof(u32));
	if (value == -EINVAL) {
		dev_err(dev, "can't find renesas,mfis-channels property\n");
		return value;
	}

	/* Allocate device struct */
	rcmfis_priv = kzalloc(sizeof(*rcmfis_priv), GFP_KERNEL);
	if (!rcmfis_priv)
		return -ENOMEM;

	rcmfis_priv->pdev = pdev;

	/* Map MFIS registers */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "io_base");
	if (!res) {
		dev_err(dev, "Failed to get MFIS IO base.\n");
		ret = -EINVAL;
		goto free_mfis_dev;
	}

	mmio_base = (u32 __iomem *)devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(mmio_base)) {
		dev_err(dev, "Failed to remap MFIS registers.\n");
		ret = PTR_ERR(mmio_base);
		goto free_mfis_dev;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "unlock_reg");
	if (!res) {
		dev_err(dev, "Failed to get write protection registger.\n");
		ret = -EINVAL;
		goto free_mfis_dev;
	}

	/* Unlock register write protection */
	unlock = ioremap(res->start, 4);
	iowrite32(0xACC00001, unlock);
	iounmap(unlock);

	rcmfis_priv->mmio_base = mmio_base;

	for (i = 0; i < num_mfis_channels; i++) {
		struct rcar_mfis_ch *mfis_ch;

		ret = of_property_read_u32_index(dev->of_node, "renesas,mfis-channels", i, &value);
		if (ret)
			continue;
		else if (value < 0 || value >= NUM_MFIS_CHANNELS)
			continue;

		mfis_ch = &rcmfis_priv->channels[value];
		if (mfis_ch->initialized) {
			dev_warn(dev, "mfis channel %d is already initialized. Skipping.\n", value);
			continue;
		}

		mfis_ch->id = value;

		ATOMIC_INIT_NOTIFIER_HEAD(&mfis_ch->notifier_head);

		/* Get IRQ resource */
		irq = platform_get_irq(pdev, mfis_ch->id);
		if (!irq) {
			dev_err(dev, "missing IRQ for channel %d. Skipping.\n", mfis_ch->id);
			continue;
		}

		ret = devm_request_irq(dev, irq, mfis_irq_handler, IRQF_SHARED,
				       dev_name(dev), mfis_ch);
		if (ret < 0) {
			dev_err(dev, "failed to request IRQ for channel %d. Skipping.\n",
				mfis_ch->id);
			continue;
		}

		mfis_ch->initialized = 1;
		dev_info(dev, "channel %d initialized\n", mfis_ch->id);
	}

	return 0;

free_mfis_dev:
	kfree(rcmfis_priv);
	return ret;
}

static int rcar_mfis_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_info(dev, "R-Car MFIS remove\n");
	kfree(rcmfis_priv);

	return 0;
}

static const struct of_device_id rcar_mfis_of_match[] = {
	{ .compatible = "renesas,mfis" },
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_mfis_of_match);

static struct platform_driver rcar_mfis_driver = {
	.probe      = rcar_mfis_probe,
	.remove     = rcar_mfis_remove,
	.driver     = {
		.name   = "rcar_mfis",
		.of_match_table = rcar_mfis_of_match,
	},
};

static int __init rcar_mfis_init(void)
{
	return platform_driver_register(&rcar_mfis_driver);
}
core_initcall(rcar_mfis_init);

static void __exit rcar_mfis_exit(void)
{
	platform_driver_unregister(&rcar_mfis_driver);
}
module_exit(rcar_mfis_exit);

MODULE_LICENSE("GPL");
