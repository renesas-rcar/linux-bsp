// SPDX-License-Identifier: GPL-2.0
/*
 * Window watchdog driver for Renesas WWDT Watchdog timer
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/units.h>
#include <linux/watchdog.h>
#include <linux/interrupt.h>

#define WWDTE		0x00
#define WDTA0MD		0xC
#define WSIZE(x)	(x)
#define WDTA0ERM	BIT(2)
#define WDTA0WIE	BIT(3)
#define WDTA0OVF(x)	(((x) << 4) & GENMASK(6, 4))
#define WWDTE_KEY	0xAC

/* ECM register base and offsets */
#define ECM_WWDT	22
#define ECM_BASE	0x189A0000
#define CTLR(x)	(0x0000 + (4 * (x)))
#define STSR(x)	(0x0100 + (4 * (x)))
#define RSTR(x)	(0x0300 + (4 * (x)))
#define INCR(x)	(0x0200 + (4 * (x)))
#define ECMWPCNTR	0x0A00
#define ECM_MAX_SIZE	(ECMWPCNTR + 0x04)
#define ECM_SET	(0x81 << 22)

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
					__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct wwdt_priv {
	void __iomem *base;
	struct watchdog_device wdev;
	struct clk *cntclk;
	struct clk *busclk;
	unsigned long clk_rate;
	unsigned int interval_time;
	unsigned int error_mode;
	unsigned int wsize;
	unsigned int wdt_wie;
	struct reset_control *rstc1;
	struct reset_control *rstc2;
};

static void wwdt_write(struct wwdt_priv *priv, u8 val, unsigned int reg)
{
	writeb(val, priv->base + reg);
}

static inline void ecm_write(u32 value, u32 base, u32 reg)
{
	void __iomem *ecm_base;

	ecm_base = ioremap_cache(base, ECM_MAX_SIZE);

	iowrite32(value, ecm_base + reg);

	iounmap(ecm_base);
}

static void init_ecm_registers(void)
{
	ecm_write(0xACCE0001, ECM_BASE, ECMWPCNTR);
	usleep_range(1000, 2000);
	ecm_write(ECM_SET, ECM_BASE, CTLR(ECM_WWDT));
	ecm_write(ECM_SET, ECM_BASE, STSR(ECM_WWDT));
	ecm_write(ECM_SET, ECM_BASE, INCR(ECM_WWDT));
	ecm_write(ECM_SET, ECM_BASE, RSTR(ECM_WWDT));
}

static u8 wwdt_read(struct wwdt_priv *priv, int reg)
{
	return readb_relaxed(priv->base + reg);
}

static void wwdt_refresh_counter(struct watchdog_device *wdev)
{
	struct wwdt_priv *priv = watchdog_get_drvdata(wdev);

	wwdt_write(priv, 0xAC, WWDTE);
}

static void wwdt_setup(struct watchdog_device *wdev)
{
	struct wwdt_priv *priv = watchdog_get_drvdata(wdev);
	struct device_node *np = priv->wdev.parent->of_node;

	/* Setting ECM for WWDT20 */
	if (of_find_property(np, "ecm", NULL))
		init_ecm_registers();
}

static int wwdt_start(struct watchdog_device *wdev)
{
	struct wwdt_priv *priv = watchdog_get_drvdata(wdev);

	pm_runtime_get_sync(wdev->parent);

	wwdt_setup(wdev);
	wwdt_write(priv, 0xAC, WWDTE);

	return 0;
}

static int wwdt_stop(struct watchdog_device *wdev)
{
	pm_runtime_put(wdev->parent);

	return 0;
}

static int wwdt_ping(struct watchdog_device *wdev)
{
	wwdt_refresh_counter(wdev);

	return 0;
}

static const struct watchdog_info wwdt_ident = {
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "Renesas Window WWDT Watchdog",
};

static const struct watchdog_ops wwdt_ops = {
	.owner = THIS_MODULE,
	.start = wwdt_start,
	.stop = wwdt_stop,
	.ping = wwdt_ping,
};

static int wwdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wwdt_priv *priv;
	struct watchdog_device *wdev;
	unsigned long rate;
	unsigned int interval, window_size;
	u8 val;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->busclk = devm_clk_get(dev, "busclk");
	if (IS_ERR(priv->busclk))
		return PTR_ERR(priv->busclk);

	priv->cntclk = devm_clk_get(dev, "cntclk");
	if (IS_ERR(priv->cntclk))
		return PTR_ERR(priv->cntclk);

	priv->rstc1 = devm_reset_control_get_optional_exclusive(dev, "rstc1");
	if (IS_ERR(priv->rstc1))
		return dev_err_probe(dev, PTR_ERR(priv->rstc1),
				     "failed to get reset rstc1\n");

	priv->rstc2 = devm_reset_control_get_optional_exclusive(dev, "rstc2");
	if (IS_ERR(priv->rstc2))
		return dev_err_probe(dev, PTR_ERR(priv->rstc2),
				     "failed to get reset rstc2\n");

	ret = reset_control_reset(priv->rstc1);
	if (ret)
		return ret;

	ret = reset_control_reset(priv->rstc2);
	if (ret)
		return ret;

	ret = clk_prepare_enable(priv->busclk);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(priv->cntclk);
	if (ret < 0)
		goto fail_clk;

	rate = clk_get_rate(priv->cntclk);

	if (!rate) {
		u32 clk_rate;

		if (of_property_read_u32(pdev->dev.of_node,
					 "renesas,cntclk", &clk_rate))
			goto fail_dev;

		rate = clk_rate;
	}

	ret = device_property_read_u32(dev, "interval-time", &priv->interval_time);
	if (ret)
		dev_warn(dev, "interval-time not found, defaulting to 0\n");

	ret = device_property_read_u32(dev, "error-mode", &priv->error_mode);
	if (ret) {
		dev_warn(dev, "error-mode not found, defaulting to 1\n");
		priv->error_mode = 1;
	}

	ret = device_property_read_u32(dev, "wsize", &priv->wsize);
	if (ret) {
		dev_warn(dev, "window-size not found, defaulting to 100%%\n");
		priv->wsize = 3;
	}

	ret = device_property_read_u32(dev, "irq_75p", &priv->wdt_wie);
	if (ret) {
		dev_warn(dev, "75%% interurpt is disabled\n");
		priv->wdt_wie = 0;
	}

	/* Default state after reset release */
	val = wwdt_read(priv, WDTA0MD);
	if (!priv->error_mode)
		val &= ~WDTA0ERM;

	val |= (WDTA0OVF(priv->interval_time)) | WSIZE(priv->wsize);
	interval = 1 << (9 + (priv->interval_time));
	window_size = 250 * (3 - (priv->wsize));
	wwdt_write(priv, val, WDTA0MD);

	wdev = &priv->wdev;
	wdev->max_hw_heartbeat_ms = 1000 * interval / rate;
	wdev->min_hw_heartbeat_ms = window_size * interval / rate;
	wdev->timeout = DIV_ROUND_UP(wdev->max_hw_heartbeat_ms, 1000);

	pm_runtime_enable(dev);
	wdev->info = &wwdt_ident;
	wdev->ops = &wwdt_ops;
	wdev->parent = dev;

	platform_set_drvdata(pdev, priv);
	watchdog_set_drvdata(&priv->wdev, priv);

	watchdog_set_nowayout(&priv->wdev, nowayout);
	watchdog_set_restart_priority(&priv->wdev, 0);
	watchdog_stop_on_unregister(&priv->wdev);

	ret = watchdog_register_device(wdev);
	if (ret < 0) {
		dev_err(dev, "Fail to register device\n");
		goto fail_dev;
	}

	dev_info(dev, "probed\n");
	return 0;

fail_dev:
	clk_disable_unprepare(priv->cntclk);
fail_clk:
	clk_disable_unprepare(priv->busclk);
	pm_runtime_disable(dev);
	return ret;
}

static void wwdt_remove(struct platform_device *pdev)
{
	struct wwdt_priv *priv = platform_get_drvdata(pdev);

	watchdog_unregister_device(&priv->wdev);
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id renesas_wwdt_ids[] = {
	{ .compatible = "renesas, rcar-gen5-wwdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, renesas_wwdt_ids);

static struct platform_driver renesas_wwdt_driver = {
	.driver = {
		.name = "renesas_wwdt",
		.of_match_table = renesas_wwdt_ids,
	},
	.probe = wwdt_probe,
	.remove = wwdt_remove,
};
module_platform_driver(renesas_wwdt_driver);

MODULE_DESCRIPTION("Renesas WWDT Window Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("<Minh Le <minh.le.aj@renesas.com>");
