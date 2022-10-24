// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/uio/uio_pdrv_genirq.c
 *
 * Userspace I/O platform driver with generic IRQ handling code.
 *
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2020-2021 by Renesas Electronics Corporation
 *
 * Based on uio_pdrv.c by Uwe Kleine-Koenig,
 * Copyright (C) 2008 by Digi International Inc.
 * All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/uio_driver.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/stringify.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/irq.h>

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>
#include <uapi/linux/renesas_uioctl.h>

#define DRIVER_NAME "uio_pdrv_genirq"

struct uio_pdrv_genirq_platdata {
	struct uio_info *uioinfo;
	spinlock_t lock;
	unsigned long flags;
	struct platform_device *pdev;
	struct clk *clk;
	struct reset_control *rst;
	int pwr_cnt;
	int clk_cnt;
};

static int local_pm_runtime_get_sync(struct uio_pdrv_genirq_platdata *priv);
static int local_pm_runtime_put_sync(struct uio_pdrv_genirq_platdata *priv);
static int local_clk_enable(struct uio_pdrv_genirq_platdata *priv);
static void local_clk_disable(struct uio_pdrv_genirq_platdata *priv);
static int priv_set_pwr(struct uio_info *info, int value);
static int priv_get_pwr(struct uio_info *info);
static int priv_set_clk(struct uio_info *info, int value);
static int priv_get_clk(struct uio_info *info);
static int priv_clk_get_div(struct uio_info *info);
static int priv_clk_set_div(struct uio_info *info, int value);
static int priv_set_rst(struct uio_info *info, int value);
static int priv_get_rst(struct uio_info *info);

/* Bits in uio_pdrv_genirq_platdata.flags */
enum {
	UIO_IRQ_DISABLED = 0,
};

static int local_pm_runtime_get_sync(struct uio_pdrv_genirq_platdata *priv)
{
	if (priv->pwr_cnt == 0) {
		priv->pwr_cnt++;
		priv->clk_cnt++;
		return pm_runtime_get_sync(&priv->pdev->dev);
	}

	return 0;
}

static int local_pm_runtime_put_sync(struct uio_pdrv_genirq_platdata *priv)
{
	if (priv->pwr_cnt > 0) {
		priv->pwr_cnt--;
		priv->clk_cnt--;
		if ((priv->clk != NULL) && (priv->clk_cnt < 0)) {
			clk_enable(priv->clk);
			priv->clk_cnt = 0;
		}

		return pm_runtime_put_sync(&priv->pdev->dev);
	}

	return 0;
}

static int local_clk_enable(struct uio_pdrv_genirq_platdata *priv)
{
	int ret = 0;

	if (priv->clk_cnt == 0) {
		ret = clk_enable(priv->clk);
		priv->clk_cnt++;
	}

	return ret;
}

static void local_clk_disable(struct uio_pdrv_genirq_platdata *priv)
{
	if (priv->clk_cnt > 0) {
		clk_disable(priv->clk);
		priv->clk_cnt--;
	}
}

static int uio_pdrv_genirq_open(struct uio_info *info, struct inode *inode)
{
	struct uio_pdrv_genirq_platdata *priv = info->priv;

	/* Wait until the Runtime PM code has woken up the device */
	local_pm_runtime_get_sync(priv);
	return 0;
}

static int uio_pdrv_genirq_release(struct uio_info *info, struct inode *inode)
{
	struct uio_pdrv_genirq_platdata *priv = info->priv;

	/* Tell the Runtime PM code that the device has become idle */
	local_pm_runtime_put_sync(priv);
	return 0;
}

static irqreturn_t uio_pdrv_genirq_handler(int irq, struct uio_info *dev_info)
{
	struct uio_pdrv_genirq_platdata *priv = dev_info->priv;

	/* Just disable the interrupt in the interrupt controller, and
	 * remember the state so we can allow user space to enable it later.
	 */

	spin_lock(&priv->lock);
	if (!__test_and_set_bit(UIO_IRQ_DISABLED, &priv->flags))
		disable_irq_nosync(irq);
	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

/**
 * Changes the drivers power state
 * if value == 0, calls pm_runtime_put_sync
 * if value == 1, calls pm_runtime_get_sync
 */
static int priv_set_pwr(struct uio_info *info, int value)
{
	int ret = 0;
	struct uio_pdrv_genirq_platdata *priv = info->priv;

	if (((value == 0) && priv->pwr_cnt > 0) || ((value != 0)
		    && priv->pwr_cnt == 0)) {
		if (value == 0)
			ret = local_pm_runtime_put_sync(priv);
		else
			ret = local_pm_runtime_get_sync(priv);
	}

	dev_dbg(&priv->pdev->dev, "Set power state value=0x%x pwr_cnt=%d, clk_cnt=%d\n",
		value, priv->pwr_cnt, priv->clk_cnt);

	return ret;
}

/**
 * Gets the power status of the driver, priv->pwr_cnt is returned
 */
static int priv_get_pwr(struct uio_info *info)
{
	struct uio_pdrv_genirq_platdata *priv = info->priv;

	dev_dbg(&priv->pdev->dev, "Get power state pwr_cnt=%d, clk_cnt=%d\n",
		priv->pwr_cnt, priv->clk_cnt);

	return priv->pwr_cnt;
}

/**
 * Changes the drivers clock state
 * if value == 0, calls local_clk_disable
 * if value == 1, calls local_clk_enable
 */
static int priv_set_clk(struct uio_info *info, int value)
{
	int ret = 0;
	struct uio_pdrv_genirq_platdata *priv = info->priv;

	if (value == 0)
		local_clk_disable(priv);
	else
		ret = local_clk_enable(priv);

	dev_dbg(&priv->pdev->dev, "Set clock state - value = 0x%x clk_cnt=%d\n",
		value, priv->clk_cnt);

	return ret;
}

/**
 * Gets the clock status of the driver
 * Returns priv->clk_cnt
 */
static int priv_get_clk(struct uio_info *info)
{
	struct uio_pdrv_genirq_platdata *priv = info->priv;

	dev_dbg(&priv->pdev->dev, "Get clock state - clk_cnt=%d\n",
		priv->clk_cnt);

	return priv->clk_cnt;
}

static int priv_clk_get_div(struct uio_info *info)
{
	struct uio_pdrv_genirq_platdata *priv = info->priv;
	unsigned long rate, div;
	struct clk *parent;

	rate = clk_get_rate(priv->clk);
	if (!rate)
		return 0;

	parent = clk_get_parent(priv->clk);
	div = clk_get_rate(parent) / rate;

	dev_dbg(&priv->pdev->dev, "Get clock div = %lu\n", div);

	return div;
}

static int priv_clk_set_div(struct uio_info *info, int div)
{
	struct uio_pdrv_genirq_platdata *priv = info->priv;
	struct clk *parent;
	struct clk_hw *hw;
	unsigned long value;

	if (div <= 0)
		return -EINVAL;

	hw = __clk_get_hw(priv->clk);

	if (!hw) {
		dev_err(&priv->pdev->dev, "No define clock for device\n");
		return -EINVAL;
	}

	value = clk_hw_get_flags(hw);
	if (value & CLK_SET_RATE_PARENT)
		return -EOPNOTSUPP;

	parent = clk_get_parent(priv->clk);
	value = clk_get_rate(parent) / div;

	dev_dbg(&priv->pdev->dev, "Set clock div = %i\n", div);

	return clk_set_rate(priv->clk, value);
}

static int priv_set_rst(struct uio_info *info, int value)
{
	struct uio_pdrv_genirq_platdata *priv = info->priv;
	int status, ret;

	status = reset_control_status(priv->rst);

	switch (value) {
	case 0:
		if (status > 0)
			ret = reset_control_deassert(priv->rst);
		break;
	case 1:
		ret = reset_control_assert(priv->rst);
		break;
	default:
		if (status == 0)
			ret = reset_control_reset(priv->rst);
		break;
	}

	dev_dbg(&priv->pdev->dev, "Set reset state - value = 0x%x\n", value);

	return ret;
}

static int priv_get_rst(struct uio_info *info)
{
	struct uio_pdrv_genirq_platdata *priv = info->priv;
	int status;

	if (!priv->rst)
		return -EOPNOTSUPP;

	status = reset_control_status(priv->rst);
	dev_dbg(&priv->pdev->dev, "Get reset state 0x%x\n", status);

	return status;
}

static int uio_pdrv_genirq_ioctl(struct uio_info *info, unsigned int cmd,
				 unsigned long arg)
{
	int value, ret = 0;

	switch (cmd) {
	case UIO_PDRV_SET_PWR:
		if (copy_from_user(&value, (int __user *)arg, sizeof(value)))
			return -EFAULT;
		ret = priv_set_pwr(info, value);
		break;
	case UIO_PDRV_GET_PWR:
		value = priv_get_pwr(info);
		if (copy_to_user((int __user *)arg, &value, sizeof(value)))
			return -EFAULT;
		arg = value;
		break;
	case UIO_PDRV_SET_CLK:
		if (copy_from_user(&value, (int __user *)arg, sizeof(value)))
			return -EFAULT;
		ret = priv_set_clk(info, value);
		break;
	case UIO_PDRV_GET_CLK:
		value = priv_get_clk(info);
		if (copy_to_user((int __user *)arg, &value, sizeof(value)))
			return -EFAULT;
		arg = value;
		break;
	case UIO_PDRV_CLK_GET_DIV:
		value = priv_clk_get_div(info);
		if (copy_to_user((int __user *)arg, &value, sizeof(value)))
			return -EFAULT;
		arg = value;
		break;
	case UIO_PDRV_CLK_SET_DIV:
		if (copy_from_user(&value, (int __user *)arg, sizeof(value)))
			return -EFAULT;
		return priv_clk_set_div(info, value);
	case UIO_PDRV_SET_RESET:
		if (copy_from_user(&value, (int __user *)arg, sizeof(value)))
			return -EFAULT;
		ret = priv_set_rst(info, value);
		break;
	case UIO_PDRV_GET_RESET:
		value = priv_get_rst(info);
		if (copy_to_user((int __user *)arg, &value, sizeof(value)))
			return -EFAULT;
		break;
	}

	return ret;
}

static int uio_pdrv_genirq_irqcontrol(struct uio_info *dev_info, s32 irq_on)
{
	struct uio_pdrv_genirq_platdata *priv = dev_info->priv;
	unsigned long flags;

	/* Allow user space to enable and disable the interrupt
	 * in the interrupt controller, but keep track of the
	 * state to prevent per-irq depth damage.
	 *
	 * Serialize this operation to support multiple tasks and concurrency
	 * with irq handler on SMP systems.
	 */

	spin_lock_irqsave(&priv->lock, flags);
	if (irq_on) {
		if (__test_and_clear_bit(UIO_IRQ_DISABLED, &priv->flags))
			enable_irq(dev_info->irq);
	} else {
		if (!__test_and_set_bit(UIO_IRQ_DISABLED, &priv->flags))
			disable_irq_nosync(dev_info->irq);
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static void uio_pdrv_genirq_cleanup(void *data)
{
	struct device *dev = data;

	pm_runtime_disable(dev);
}

static int uio_pdrv_genirq_probe(struct platform_device *pdev)
{
	struct uio_info *uioinfo = dev_get_platdata(&pdev->dev);
	struct device_node *node = pdev->dev.of_node;
	struct uio_pdrv_genirq_platdata *priv;
	struct uio_mem *uiomem;
	int ret = -EINVAL;
	int i;

	if (node) {
		const char *name;

		/* alloc uioinfo for one device */
		uioinfo = devm_kzalloc(&pdev->dev, sizeof(*uioinfo),
				       GFP_KERNEL);
		if (!uioinfo) {
			dev_err(&pdev->dev, "unable to kmalloc\n");
			return -ENOMEM;
		}

		if (!of_property_read_string(node, "linux,uio-name", &name))
			uioinfo->name = devm_kstrdup(&pdev->dev, name, GFP_KERNEL);
		else
			uioinfo->name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
						       "%pOFn", node);

		uioinfo->version = "devicetree";
		/* Multiple IRQs are not supported */
	}

	if (!uioinfo || !uioinfo->name || !uioinfo->version) {
		dev_err(&pdev->dev, "missing platform_data\n");
		return ret;
	}

	if (uioinfo->handler || uioinfo->irqcontrol ||
	    uioinfo->irq_flags & IRQF_SHARED) {
		dev_err(&pdev->dev, "interrupt configuration error\n");
		return ret;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "unable to kmalloc\n");
		return -ENOMEM;
	}

	priv->uioinfo = uioinfo;
	spin_lock_init(&priv->lock);
	priv->flags = 0; /* interrupt is enabled to begin with */
	priv->pdev = pdev;

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		priv->clk = NULL;

	priv->clk_cnt = 0;
	priv->pwr_cnt = 0;

	priv->rst = devm_reset_control_get_optional_exclusive(&pdev->dev, NULL);
	if (IS_ERR(priv->rst)) {
		dev_err(&pdev->dev, "failed to get cpg reset\n");
		return PTR_ERR(priv->rst);
	}

	if (!uioinfo->irq) {
		ret = platform_get_irq_optional(pdev, 0);
		uioinfo->irq = ret;
		if (ret == -ENXIO)
			uioinfo->irq = UIO_IRQ_NONE;
		else if (ret == -EPROBE_DEFER)
			return ret;
		else if (ret < 0) {
			dev_err(&pdev->dev, "failed to get IRQ\n");
			return ret;
		}
	}

	if (uioinfo->irq) {
		struct irq_data *irq_data = irq_get_irq_data(uioinfo->irq);

		/*
		 * If a level interrupt, dont do lazy disable. Otherwise the
		 * irq will fire again since clearing of the actual cause, on
		 * device level, is done in userspace
		 * irqd_is_level_type() isn't used since isn't valid until
		 * irq is configured.
		 */
		if (irq_data &&
		    irqd_get_trigger_type(irq_data) & IRQ_TYPE_LEVEL_MASK) {
			dev_dbg(&pdev->dev, "disable lazy unmask\n");
			irq_set_status_flags(uioinfo->irq, IRQ_DISABLE_UNLAZY);
		}
	}

	uiomem = &uioinfo->mem[0];

	for (i = 0; i < pdev->num_resources; ++i) {
		struct resource *r = &pdev->resource[i];

		if (r->flags != IORESOURCE_MEM)
			continue;

		if (uiomem >= &uioinfo->mem[MAX_UIO_MAPS]) {
			dev_warn(&pdev->dev, "device has more than "
					__stringify(MAX_UIO_MAPS)
					" I/O memory resources.\n");
			break;
		}

		uiomem->memtype = UIO_MEM_PHYS;
		uiomem->addr = r->start & PAGE_MASK;
		uiomem->offs = r->start & ~PAGE_MASK;
		uiomem->size = (uiomem->offs + resource_size(r)
				+ PAGE_SIZE - 1) & PAGE_MASK;
		uiomem->name = r->name;
		++uiomem;
	}

	while (uiomem < &uioinfo->mem[MAX_UIO_MAPS]) {
		uiomem->size = 0;
		++uiomem;
	}

	/* This driver requires no hardware specific kernel code to handle
	 * interrupts. Instead, the interrupt handler simply disables the
	 * interrupt in the interrupt controller. User space is responsible
	 * for performing hardware specific acknowledge and re-enabling of
	 * the interrupt in the interrupt controller.
	 *
	 * Interrupt sharing is not supported.
	 */

	uioinfo->handler = uio_pdrv_genirq_handler;
	uioinfo->irqcontrol = uio_pdrv_genirq_irqcontrol;
	uioinfo->open = uio_pdrv_genirq_open;
	uioinfo->release = uio_pdrv_genirq_release;
	uioinfo->ioctl = uio_pdrv_genirq_ioctl;
	uioinfo->priv = priv;

	/* Enable Runtime PM for this device:
	 * The device starts in suspended state to allow the hardware to be
	 * turned off by default. The Runtime PM bus code should power on the
	 * hardware and enable clocks at open().
	 */
	pm_runtime_enable(&pdev->dev);

	ret = devm_add_action_or_reset(&pdev->dev, uio_pdrv_genirq_cleanup,
				       &pdev->dev);
	if (ret)
		return ret;

	ret = devm_uio_register_device(&pdev->dev, priv->uioinfo);
	if (ret)
		dev_err(&pdev->dev, "unable to register uio device\n");

	return ret;
}

static int uio_pdrv_genirq_runtime_nop(struct device *dev)
{
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * In this driver pm_runtime_get_sync() and pm_runtime_put_sync()
	 * are used at open() and release() time. This allows the
	 * Runtime PM code to turn off power to the device while the
	 * device is unused, ie before open() and after release().
	 *
	 * This Runtime PM callback does not need to save or restore
	 * any registers since user space is responsbile for hardware
	 * register reinitialization after open().
	 */
	return 0;
}

static const struct dev_pm_ops uio_pdrv_genirq_dev_pm_ops = {
	.runtime_suspend = uio_pdrv_genirq_runtime_nop,
	.runtime_resume = uio_pdrv_genirq_runtime_nop,
};

#ifdef CONFIG_OF
static struct of_device_id uio_of_genirq_match[] = {
	{ /* This is filled with module_parm */ },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, uio_of_genirq_match);
module_param_string(of_id, uio_of_genirq_match[0].compatible, 128, 0);
MODULE_PARM_DESC(of_id, "Openfirmware id of the device to be handled by uio");
#endif

static struct platform_driver uio_pdrv_genirq = {
	.probe = uio_pdrv_genirq_probe,
	.driver = {
		.name = DRIVER_NAME,
		.pm = &uio_pdrv_genirq_dev_pm_ops,
		.of_match_table = of_match_ptr(uio_of_genirq_match),
	},
};

module_platform_driver(uio_pdrv_genirq);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("Userspace I/O platform driver with generic IRQ handling");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
