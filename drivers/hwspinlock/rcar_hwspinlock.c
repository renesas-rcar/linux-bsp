// SPDX-License-Identifier: GPL-2.0
/*
 * rcar_hwspinlock.c
 *
 * Copyright (C) 2017 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/hwspinlock.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/sys_soc.h>
#include <linux/of_platform.h>

#include "hwspinlock_internal.h"

#define MFISLCKR_NUM_8		8	/* r8a7795 ES1.*, r8a7796 ES1.* */
#define MFISLCKR_NUM_64		64

#define UNLOCK_WRITE_VAL	0xacc00001

struct rcar_hwspinlock_of_data {
	u32 mfislckr0_offset;
	u32 mfislckr8_offset;
};

struct rcar_hwspinlock_priv {
	void __iomem *spinlock_reg;
	void __iomem *write_prot_reg;
};

static int rcar_hwspinlock_trylock(struct hwspinlock *lock)
{
	struct rcar_hwspinlock_priv *priv = lock->priv;

	if (priv->write_prot_reg)
		iowrite32(UNLOCK_WRITE_VAL, priv->write_prot_reg);

	return !ioread32(priv->spinlock_reg);
}

static void rcar_hwspinlock_unlock(struct hwspinlock *lock)
{
	struct rcar_hwspinlock_priv *priv = lock->priv;

	if (priv->write_prot_reg)
		iowrite32(UNLOCK_WRITE_VAL, priv->write_prot_reg);

	iowrite32(0, priv->spinlock_reg);
}

static const struct hwspinlock_ops rcar_hwspinlock_ops = {
	.trylock	= rcar_hwspinlock_trylock,
	.unlock		= rcar_hwspinlock_unlock,
};

static const struct soc_device_attribute mfislock_quirks_match[] = {
	{ .soc_id = "r8a7795", .revision = "ES1.*" },
	{ .soc_id = "r8a7796", .revision = "ES1.*" },
	{ /* sentinel */ }
};

static const struct rcar_hwspinlock_of_data rcar_hwspinlock_data = {
	.mfislckr0_offset	= 0xc0,
	.mfislckr8_offset	= 0x724,
};

static const struct rcar_hwspinlock_of_data rcar_hwspinlock_gen5_data = {
	.mfislckr0_offset	= 0xc0,
	.mfislckr8_offset	= 0x704,
};

static const struct of_device_id rcar_hwspinlock_of_match[] = {
	{
		.compatible = "renesas,mfis-lock",
		.data = &rcar_hwspinlock_data,
	}, {
		.compatible = "renesas,mfis-lock-gen5",
		.data = &rcar_hwspinlock_gen5_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_hwspinlock_of_match);

static int rcar_hwspinlock_probe(struct platform_device *pdev)
{
	int					ch;
	int					num_locks = MFISLCKR_NUM_64;
	int					ret = 0;
	u32 __iomem				*addr;
	void __iomem				*write_prot_reg = NULL;
	struct resource				*res;
	struct hwspinlock_device		*bank;
	struct rcar_hwspinlock_priv		*priv;
	const struct rcar_hwspinlock_of_data	*data;

	data = of_device_get_match_data(&pdev->dev);
	if (!data)
		return -EINVAL;

	/* allocate hwspinlock control info */
	bank = devm_kzalloc(&pdev->dev, sizeof(*bank)
			    + sizeof(struct hwspinlock) * MFISLCKR_NUM_64,
			    GFP_KERNEL);
	if (!bank) {
		dev_err(&pdev->dev, "Failed to allocate memory.\n");
		ret = -ENOMEM;
		goto out;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	/* map MFIS lock register */
	addr = (u32 __iomem *)devm_ioremap(&pdev->dev,
					   res->start,
					   resource_size(res));
	if (!addr) {
		dev_err(&pdev->dev, "Failed to remap register.\n");
		ret = PTR_ERR(addr);
		goto out;
	}

	/* map write-protection register if present */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "unlock_reg");
	if (res) {
		write_prot_reg = devm_ioremap(&pdev->dev, res->start, 4);
		if (!write_prot_reg) {
			ret = -ENOMEM;
			goto out;
		}
	}

	priv = devm_kcalloc(&pdev->dev, MFISLCKR_NUM_64,
			    sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto out;
	}

	/* create lock for MFISLCKR0-7 */
	for (ch = 0; ch < 8; ch++) {
		priv[ch].spinlock_reg = (void __iomem *)((void __force *)addr
				    + data->mfislckr0_offset
				    + sizeof(u32) * ch);
		priv[ch].write_prot_reg = write_prot_reg;
		bank->lock[ch].priv = &priv[ch];
	}

	/* create lock for MFISLCKR8-63 */
	for (ch = 8; ch < 64; ch++) {
		priv[ch].spinlock_reg = (void __iomem *)((void __force *)addr
				    + data->mfislckr8_offset
				    + sizeof(u32) * (ch - 8));
		priv[ch].write_prot_reg = write_prot_reg;
		bank->lock[ch].priv = &priv[ch];
	}

	platform_set_drvdata(pdev, bank);

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret)
		goto out;

	pm_runtime_enable(&pdev->dev);

	/* register hwspinlock */
	if (soc_device_match(mfislock_quirks_match))
		num_locks = MFISLCKR_NUM_8;

	ret = hwspin_lock_register(bank, &pdev->dev, &rcar_hwspinlock_ops,
				   0, num_locks);
	if (ret)
		pm_runtime_disable(&pdev->dev);

out:
	return ret;
}

static void rcar_hwspinlock_remove(struct platform_device *pdev)
{
	if (hwspin_lock_unregister(platform_get_drvdata(pdev)))
		dev_err(&pdev->dev, "%s failed\n", __func__);

	pm_runtime_disable(&pdev->dev);
}

static struct platform_driver rcar_hwspinlock_driver = {
	.probe		= rcar_hwspinlock_probe,
	.remove		= rcar_hwspinlock_remove,
	.driver		= {
		.name	= "rcar_hwspinlock",
		.of_match_table = rcar_hwspinlock_of_match,
	},
};

static int __init rcar_hwspinlock_init(void)
{
	return platform_driver_register(&rcar_hwspinlock_driver);
}
core_initcall(rcar_hwspinlock_init);

static void __exit rcar_hwspinlock_exit(void)
{
	platform_driver_unregister(&rcar_hwspinlock_driver);
}
module_exit(rcar_hwspinlock_exit);

MODULE_LICENSE("GPL");
