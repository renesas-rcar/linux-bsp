// SPDX-License-Identifier: GPL-2.0-only
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

#define IICR	(0x0000)
#define EICR	(0x0004)
#define IMBR	(0x0040)
#define EMBR	(0x0044)

static struct rcar_mfis_priv *rcmfis_priv;

static irqreturn_t mfis_irq_handler(int irq, void *data)
{
	u32 value = 0;
	struct rcar_mfis_msg msg;
	struct rcar_mfis_ch *rcar_mfis_ch = (struct rcar_mfis_ch *)data;

	value = rcar_mfis_reg_read(rcar_mfis_ch, IICR);
	if (value & 0x1) {
		msg.mbr = rcar_mfis_reg_read(rcar_mfis_ch, IMBR);
		msg.icr = value >> 1;

		atomic_notifier_call_chain(&rcar_mfis_ch->notifier_head, msg.icr,
					   rcar_mfis_ch->notifier_data);

		/* clear interrupt flag */
		rcar_mfis_reg_write(rcar_mfis_ch, IICR, value & (~0x1));

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static struct rcar_mfis_ch *rcar_mfis_channel_get(unsigned int channel)
{
	struct rcar_mfis_ch *rcar_mfis_ch;
	int i;

	if (!rcmfis_priv)
		return NULL;

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
	u32 icr;

	rcar_mfis_ch = rcar_mfis_channel_get(channel);
	if (!rcar_mfis_ch)
		return -EINVAL;

	/* Check whether the remote proccessor is still processing a previous interrupt */
	icr = rcar_mfis_reg_read(rcar_mfis_ch, EICR);
	if (icr & 0x1)
		return -EBUSY;

	rcar_mfis_reg_write(rcar_mfis_ch, EMBR, msg.mbr);
	rcar_mfis_reg_write(rcar_mfis_ch, EICR, (msg.icr << 1) | 1);

	return 0;
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
	struct resource *res;
	int num_mfis_channels;
	u32 value;
	int ret, i;

	num_mfis_channels = of_property_count_elems_of_size(dev->of_node, "renesas,mfis-channels",
							    sizeof(u32));
	if (num_mfis_channels < 0) {
		dev_err(dev, "can't find renesas,mfis-channels property\n");
		return num_mfis_channels;
	}

	/* Allocate device struct */
	rcmfis_priv = kzalloc(sizeof(*rcmfis_priv), GFP_KERNEL);
	if (!rcmfis_priv)
		return -ENOMEM;

	rcmfis_priv->pdev = pdev;
	spin_lock_init(&rcmfis_priv->lock);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "unlock_reg");
	if (!res) {
		dev_err(dev, "Failed to get write protection registger.\n");
		ret = -EINVAL;
		goto free_mfis_dev;
	}

	rcmfis_priv->unlock = devm_ioremap(dev, res->start, resource_size(res));
	if (!rcmfis_priv->unlock) {
		dev_err(dev, "failed to map write protection register.\n");
		ret = -ENOMEM;
		goto free_mfis_dev;
	}

	for (i = 0; i < num_mfis_channels; i++) {
		struct rcar_mfis_ch *mfis_ch;
		char *pdev_chname, *pdev_irqname;
		int irq;

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
		mfis_ch->priv = rcmfis_priv;

		ATOMIC_INIT_NOTIFIER_HEAD(&mfis_ch->notifier_head);

		pdev_chname = kasprintf(GFP_KERNEL, "ch%u", mfis_ch->id);
		if (!pdev_chname) {
			dev_err(dev, "failed to create channel %d name. Skipping.\n", mfis_ch->id);
			continue;
		}

		/* Get channel base address */
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, pdev_chname);
		if (!res) {
			dev_err(dev, "missing base address for channel %d. Skipping.\n",
				mfis_ch->id);
			continue;
		}

		mfis_ch->base = devm_ioremap_resource(dev, res);
		if (!mfis_ch->base) {
			dev_err(dev, "failed to map IO memory for channel %d\n. Skipping.\n",
				mfis_ch->id);
			continue;
		}

		/* Get IRQ resource */
		irq = platform_get_irq(pdev, mfis_ch->id);
		if (!irq) {
			dev_err(dev, "missing IRQ for channel %d. Skipping.\n", mfis_ch->id);
			continue;
		}

		pdev_irqname = devm_kasprintf(dev, GFP_KERNEL, "mfis_ch%u", mfis_ch->id);
		if (!pdev_irqname) {
			dev_err(dev, "failed to create irqname of channel %d name. Skipping.\n",
				mfis_ch->id);
			continue;
		}

		ret = devm_request_irq(dev, irq, mfis_irq_handler, IRQF_SHARED,
				       pdev_irqname, mfis_ch);
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

static void rcar_mfis_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_info(dev, "R-Car MFIS remove\n");
	kfree(rcmfis_priv);
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
