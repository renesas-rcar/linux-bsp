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

#include "hwspinlock_internal.h"

#define MFISLCKR0_OFFSET	0x000000C0
#define MFISLCKR8_OFFSET	0x00000724
#define MFISLCKR_NUM_8		8	/* r8a7795 ES1.*, r8a7796 ES1.* */
#define MFISLCKR_NUM_64		64

static int rcar_hwspinlock_trylock(struct hwspinlock *lock)
{
	void *addr = lock->priv;

	return !ioread32((void __iomem *)addr);
}

static void rcar_hwspinlock_unlock(struct hwspinlock *lock)
{
	void *addr = lock->priv;

	iowrite32(0, (void __iomem *)addr);
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

static const struct of_device_id rcar_hwspinlock_of_match[] = {
	{ .compatible = "renesas,mfis-lock" },
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_hwspinlock_of_match);

static int rcar_hwspinlock_probe(struct platform_device *pdev)
{
	int				ch;
	int				num_locks = MFISLCKR_NUM_64;
	int				ret = 0;
	u32 __iomem			*addr;
	struct resource			*res;
	struct hwspinlock_device	*bank;

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
	addr = (u32 __iomem *)devm_ioremap_nocache(&pdev->dev,
						   res->start,
						   resource_size(res));
	if (!addr) {
		dev_err(&pdev->dev, "Failed to remap register.\n");
		ret = PTR_ERR(addr);
		goto out;
	}

	/* create lock for MFISLCKR0-7 */
	for (ch = 0; ch < 8; ch++)
		bank->lock[ch].priv = (void __force *)addr + MFISLCKR0_OFFSET
				+ sizeof(u32) * ch;

	/* create lock for MFISLCKR8-63 */
	for (ch = 8; ch < 64; ch++)
		bank->lock[ch].priv = (void __force *)addr + MFISLCKR8_OFFSET
				+ sizeof(u32) * (ch - 8);

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
