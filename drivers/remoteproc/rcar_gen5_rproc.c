// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/limits.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/delay.h>

#include "remoteproc_internal.h"
#include <misc/rcar-mfis/rcar_mfis_public.h>

struct rcar_rproc {
	struct rproc *rproc;
	struct work_struct workqueue;
	u32 mfis_chan;
};

static int rcar_gen5_rproc_mem_alloc(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	struct device *dev = &rproc->dev;
	void *va;

	dev_dbg(dev, "map memory: %pa+%zx\n", &mem->dma, mem->len);
	va = ioremap_wc(mem->dma, mem->len);
	if (!va) {
		dev_err(dev, "Unable to map memory region: %pa+%zx\n",
			&mem->dma, mem->len);
		return -ENOMEM;
	}

	/* Update memory entry va */
	mem->va = va;

	return 0;
}

static int rcar_gen5_rproc_mem_release(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	dev_dbg(&rproc->dev, "unmap memory: %pa\n", &mem->dma);
	iounmap(mem->va);

	return 0;
}

static void handle_event(struct work_struct *work)
{
	struct rcar_rproc *priv = container_of(work, struct rcar_rproc, workqueue);

	rproc_vq_interrupt(priv->rproc, 0);
	rproc_vq_interrupt(priv->rproc, 1);
}

static int rcar_gen5_rproc_interrupt_cb(struct notifier_block *self, unsigned long action,
					void *data)
{
	struct rcar_rproc *priv = (struct rcar_rproc *)data;

	schedule_work(&priv->workqueue);

	return NOTIFY_DONE;
}

static struct notifier_block rcar_gen5_rproc_notifier_block = {
	.notifier_call = rcar_gen5_rproc_interrupt_cb,
};

static int rcar_gen5_rproc_prepare(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct of_phandle_iterator it;
	struct rproc_mem_entry *mem;
	struct reserved_mem *rmem;
	u32 da;

	/* Register associated reserved memory regions */
	of_phandle_iterator_init(&it, np, "memory-region", NULL, 0);
	while (of_phandle_iterator_next(&it) == 0) {
		rmem = of_reserved_mem_lookup(it.node);
		if (!rmem) {
			of_node_put(it.node);
			dev_err(&rproc->dev,
				"unable to acquire memory-region\n");
			return -EINVAL;
		}

		if (rmem->base > U32_MAX) {
			of_node_put(it.node);
			return -EINVAL;
		}

		/* No need to translate pa to da, R-Car use same map */
		da = rmem->base;
		mem = rproc_mem_entry_init(dev, NULL,
					   rmem->base,
					   rmem->size, da,
					   rcar_gen5_rproc_mem_alloc,
					   rcar_gen5_rproc_mem_release,
					   it.node->name);

		if (!mem) {
			of_node_put(it.node);
			return -ENOMEM;
		}

		rproc_add_carveout(rproc, mem);
	}

	return 0;
}

static int rcar_gen5_rproc_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int ret;

	ret = rproc_elf_load_rsc_table(rproc, fw);
	if (ret)
		dev_info(&rproc->dev, "No resource table in elf\n");

	return 0;
}

static int rcar_gen5_rproc_start(struct rproc *rproc)
{
	if (!rproc->bootaddr)
		return -EINVAL;

	return 0;
}

static int rcar_gen5_rproc_stop(struct rproc *rproc)
{
	return 0;
}

static void rcar_gen5_rproc_kick(struct rproc *rproc, int vqid)
{
	struct device *dev = rproc->dev.parent;
	struct rcar_rproc *priv = rproc->priv;
	struct rcar_mfis_msg msg;
	unsigned int n_tries = 3;
	int ret;

	msg.icr = vqid;
	msg.mbr = 0;

	do {
		ret = rcar_mfis_trigger_interrupt(priv->mfis_chan, msg);
		if (ret)
			usleep_range(500, 510);

	} while (ret && n_tries--);

	if (ret)
		dev_info(dev, "%s failed\n", __func__);
}

static int rcar_gen5_rproc_elf_load_segments(struct rproc *rproc, const struct firmware *fw)
{
	return 0;
}

static struct rproc_ops rcar_rproc_ops = {
	.prepare		= rcar_gen5_rproc_prepare,
	.start			= rcar_gen5_rproc_start,
	.stop			= rcar_gen5_rproc_stop,
	.kick			= rcar_gen5_rproc_kick,
	.load			= rcar_gen5_rproc_elf_load_segments,
	.parse_fw		= rcar_gen5_rproc_parse_fw,
	.find_loaded_rsc_table	= rproc_elf_find_loaded_rsc_table,
	.sanity_check		= rproc_elf_sanity_check,
	.get_boot_addr		= rproc_elf_get_boot_addr,

};

static int rcar_gen5_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rcar_rproc *priv;
	struct rproc *rproc;
	int ret;

	rproc = devm_rproc_alloc(dev, np->name, &rcar_rproc_ops, NULL, sizeof(*priv));
	if (!rproc)
		return -ENOMEM;

	priv = rproc->priv;

	dev_set_drvdata(dev, rproc);

	priv->rproc = rproc;

	INIT_WORK(&priv->workqueue, handle_event);

	ret = of_property_read_u32(np, "renesas,mfis-channel", &priv->mfis_chan);
	if (ret) {
		/* Default is channel 0 */
		priv->mfis_chan = 0;
	}

	ret = rcar_mfis_register_notifier(priv->mfis_chan, &rcar_gen5_rproc_notifier_block, priv);
	if (ret) {
		dev_err(dev, "cannot register notifier on mfis channel %d\n", 0);
		return ret;
	}

	/* Manually start the rproc */
	rproc->auto_boot = false;

	ret = devm_rproc_add(dev, rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed\n");
		goto unregister_notifier;
	}

	return 0;

unregister_notifier:
	rcar_mfis_unregister_notifier(priv->mfis_chan, &rcar_gen5_rproc_notifier_block);
	flush_work(&priv->workqueue);

	return ret;
}

static void rcar_gen5_rproc_remove(struct platform_device *pdev)
{
	struct rcar_rproc *priv =  platform_get_drvdata(pdev);

	rcar_mfis_unregister_notifier(priv->mfis_chan, &rcar_gen5_rproc_notifier_block);
	flush_work(&priv->workqueue);
	rproc_del(priv->rproc);
}

static const struct of_device_id rcar_gen5_rproc_of_match[] = {
	{ .compatible = "renesas,r8a78000-rproc" },
	{ .compatible = "renesas,rcar-gen5-rproc" },
	{},
};

MODULE_DEVICE_TABLE(of, rcar_gen5_rproc_of_match);

static struct platform_driver rcar_gen5_rproc_driver = {
	.probe = rcar_gen5_rproc_probe,
	.remove = rcar_gen5_rproc_remove,
	.driver = {
		.name = "rcar-gen5-rproc",
		.of_match_table = rcar_gen5_rproc_of_match,
	},
};

module_platform_driver(rcar_gen5_rproc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas R-Car Gen5 remote processor control driver");
MODULE_AUTHOR("Phong Hoang <phong.hoang.wz@renesas.com>");
