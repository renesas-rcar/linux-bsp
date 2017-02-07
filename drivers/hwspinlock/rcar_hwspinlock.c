/*
 * rcar_hwspinlock.c
 *
 * Copyright (C) 2016-2017 Renesas Electronics Corporation
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

#include "hwspinlock_internal.h"

#define RCAR_HWSPINLOCK_NUM	(8)

static int rcar_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *addr = lock->priv;

	return !ioread32(addr);
}

static void rcar_hwspinlock_unlock(struct hwspinlock *lock)
{
	void __iomem *addr = lock->priv;

	iowrite32(0, addr);
}

static const struct hwspinlock_ops rcar_hwspinlock_ops = {
	.trylock	= rcar_hwspinlock_trylock,
	.unlock		= rcar_hwspinlock_unlock,
};

static const struct of_device_id rcar_hwspinlock_of_match[] = {
	{ .compatible = "renesas,mfis-lock" },
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_hwspinlock_of_match);

static int rcar_hwspinlock_probe(struct platform_device *pdev)
{
	int				idx;
	int				ret = 0;
	u32 __iomem			*addr;
	struct hwspinlock_device	*bank;
	struct hwspinlock		*lock;
	struct resource			*res = NULL;

	/* map MFIS register */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	addr = (u32 __iomem *)devm_ioremap_nocache(&pdev->dev,
					res->start, resource_size(res));
	if (IS_ERR(addr)) {
		dev_err(&pdev->dev, "Failed to remap MFIS Lock register.\n");
		ret = PTR_ERR(addr);
		goto out;
	}

	/* create hwspinlock control info */
	bank = devm_kzalloc(&pdev->dev,
			sizeof(*bank) + sizeof(*lock) * RCAR_HWSPINLOCK_NUM,
			GFP_KERNEL);
	if (!bank) {
		dev_err(&pdev->dev, "Failed to allocate memory.\n");
		ret = PTR_ERR(bank);
		goto out;
	}

	for (idx = 0; idx < RCAR_HWSPINLOCK_NUM; idx++) {
		lock = &bank->lock[idx];
		lock->priv = &addr[idx];
	}
	platform_set_drvdata(pdev, bank);

	pm_runtime_enable(&pdev->dev);

	/* register hwspinlock */
	ret = hwspin_lock_register(bank, &pdev->dev, &rcar_hwspinlock_ops,
				   0, RCAR_HWSPINLOCK_NUM);
	if (ret)
		pm_runtime_disable(&pdev->dev);

out:
	return ret;
}

static int rcar_hwspinlock_remove(struct platform_device *pdev)
{
	int		ret;

	ret = hwspin_lock_unregister(platform_get_drvdata(pdev));
	if (ret) {
		dev_err(&pdev->dev, "%s failed: %d\n", __func__, ret);
		return ret;
	}

	pm_runtime_disable(&pdev->dev);

	return 0;
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

MODULE_LICENSE("GPL v2");
