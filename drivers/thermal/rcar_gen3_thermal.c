/*
 *  R-Car Gen3 THS/CIVM thermal sensor driver
 *  Based on drivers/thermal/rcar_thermal.c
 *
 * Copyright (C) 2015 Renesas Electronics Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>

/* Register offset */
#define REG_GEN3_CTSR		0x20
#define REG_GEN3_IRQSTR		0x04
#define REG_GEN3_IRQMSK		0x08
#define REG_GEN3_IRQCTL		0x0C
#define REG_GEN3_IRQEN		0x10
#define REG_GEN3_IRQTEMP1	0x14
#define REG_GEN3_IRQTEMP2	0x18
#define REG_GEN3_IRQTEMP3	0x1C
#define REG_GEN3_TEMP		0x28

/* CTSR bit */
#define PONSEQSTOP      (0x1 << 27)
#define PONM            (0x1 << 8)
#define AOUT            (0x1 << 7)
#define THBGR           (0x1 << 5)
#define VMEN            (0x1 << 4)
#define VMST            (0x1 << 1)
#define THSST           (0x1 << 0)

#define CTEMP_MASK	0xFFF

#define POWERON		0

#define TEMP_IRQ_SHIFT(tsc_id)	(0x1 << tsc_id)
#define TEMPD_IRQ_SHIFT(tsc_id)	(0x1 << (tsc_id + 3))

struct rcar_thermal_priv {
	void __iomem *base;
	struct device *dev;
	struct thermal_zone_device *zone;
	struct delayed_work work;
	spinlock_t lock;
	int id;
	int irq;
	u32 ctemp;
};

#define MCELSIUS(temp)                  ((temp) * 1000)
#define rcar_priv_to_dev(priv)		((priv)->dev)
#define rcar_has_irq_support(priv)	((priv)->irq)

/* Temperature conversion  */
#define TEMP_CONVERT(ctemp)	(((1000 * MCELSIUS(ctemp)) - 2536700) / 7468)

#define rcar_thermal_read(p, r) _rcar_thermal_read(p, r)
static u32 _rcar_thermal_read(struct rcar_thermal_priv *priv, u32 reg)
{
	return ioread32(priv->base + reg);
}

#define rcar_thermal_write(p, r, d) _rcar_thermal_write(p, r, d)
static void _rcar_thermal_write(struct rcar_thermal_priv *priv,
				u32 reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

static int round_temp(int i)
{
	int tmp1, tmp2;
	int result = 0;

	tmp1 = i % 10;
	tmp2 = i / 10;
	if (tmp1 < 5)
		result = tmp2;
	else
		result = tmp2 + 1;
	return result;
}

/*
 *		Zone device functions
 */
static int rcar_gen3_thermal_update_temp(struct rcar_thermal_priv *priv)
{
	u32 ctemp;
	int i;
	unsigned long flags;
	u32 reg = REG_GEN3_IRQTEMP1 + (priv->id * 4);

	spin_lock_irqsave(&priv->lock, flags);

	for (i = 0; i < 256; i++) {
		ctemp = rcar_thermal_read(priv, REG_GEN3_TEMP) & CTEMP_MASK;
		if (rcar_has_irq_support(priv)) {
			rcar_thermal_write(priv, reg, ctemp);
			if (rcar_thermal_read(priv, REG_GEN3_IRQSTR) != 0)
				break;
		} else
			break;

		udelay(300);
	}

	priv->ctemp = round_temp(TEMP_CONVERT(ctemp));
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int rcar_gen3_thermal_get_temp(void *devdata, int *temp)
{
	struct rcar_thermal_priv *priv = devdata;
	u32 ctemp;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	/*
	 * temp = (THCODE - 2536.7) / 7.468
	 */
	ctemp = rcar_thermal_read(priv, REG_GEN3_TEMP) & CTEMP_MASK;
	*temp = round_temp(TEMP_CONVERT(ctemp));
	priv->ctemp = *temp;
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int rcar_gen3_thermal_init(struct rcar_thermal_priv *priv)
{
	unsigned long status;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	status = rcar_thermal_read(priv, REG_GEN3_CTSR);

	if ((status & PONSEQSTOP) == POWERON)
		rcar_thermal_write(priv, REG_GEN3_CTSR,  0x0);

	rcar_thermal_write(priv, REG_GEN3_CTSR,
			PONM | AOUT | THBGR | VMEN);
	udelay(100);
	rcar_thermal_write(priv, REG_GEN3_CTSR,
			PONM | AOUT | THBGR | VMEN | VMST | THSST);
	udelay(1000);

	rcar_thermal_write(priv, REG_GEN3_IRQCTL, 0x3F);
	rcar_thermal_write(priv, REG_GEN3_IRQEN, TEMP_IRQ_SHIFT(priv->id) |
						TEMPD_IRQ_SHIFT(priv->id));

	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

/*
 *		Interrupt
 */
#define rcar_thermal_irq_enable(p)	_rcar_thermal_irq_ctrl(p, 1)
#define rcar_thermal_irq_disable(p)	_rcar_thermal_irq_ctrl(p, 0)
static void _rcar_thermal_irq_ctrl(struct rcar_thermal_priv *priv, int enable)
{
	unsigned long flags;

	if (!rcar_has_irq_support(priv))
		return;

	spin_lock_irqsave(&priv->lock, flags);
	rcar_thermal_write(priv, REG_GEN3_IRQMSK,
		enable ? (TEMP_IRQ_SHIFT(priv->id) |
			TEMPD_IRQ_SHIFT(priv->id)) : 0);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void rcar_gen3_thermal_work(struct work_struct *work)
{
	struct rcar_thermal_priv *priv;

	priv = container_of(work, struct rcar_thermal_priv, work.work);

	rcar_gen3_thermal_update_temp(priv);
	thermal_zone_device_update(priv->zone);

	rcar_thermal_irq_enable(priv);
}

static irqreturn_t rcar_gen3_thermal_irq(int irq, void *data)
{
	struct rcar_thermal_priv *priv = data;
	unsigned long flags;
	int status;

	spin_lock_irqsave(&priv->lock, flags);
	status = rcar_thermal_read(priv, REG_GEN3_IRQSTR);
	rcar_thermal_write(priv, REG_GEN3_IRQSTR, 0);
	spin_unlock_irqrestore(&priv->lock, flags);

	if ((status & TEMP_IRQ_SHIFT(priv->id)) ||
		(status & TEMPD_IRQ_SHIFT(priv->id))) {
		rcar_thermal_irq_disable(priv);
		schedule_delayed_work(&priv->work,
				      msecs_to_jiffies(300));
	}

	return IRQ_HANDLED;
}

static struct thermal_zone_of_device_ops rcar_gen3_tz_of_ops = {
	.get_temp	= rcar_gen3_thermal_get_temp,
};

/*
 *		Platform functions
 */
static int rcar_gen3_thermal_remove(struct platform_device *pdev)
{
	struct rcar_thermal_priv *priv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	rcar_thermal_irq_disable(priv);
	thermal_zone_device_unregister(priv->zone);

	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return 0;
}

static const struct of_device_id rcar_thermal_dt_ids[] = {
	{ .compatible = "renesas,rcar-gen3-thermal"},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_thermal_dt_ids);

static int rcar_gen3_thermal_probe(struct platform_device *pdev)
{
	struct rcar_thermal_priv *priv;
	struct device *dev = &pdev->dev;
	struct resource *res, *irq;
	int ret = -ENODEV;
	int idle;
	struct device_node *tz_nd, *tmp_nd;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->dev = dev;

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	priv->irq = 0;
	if (irq) {
		priv->irq = 1;
		for_each_node_with_property(tz_nd, "polling-delay") {
			tmp_nd = of_parse_phandle(tz_nd,
					"thermal-sensors", 0);
			if (tmp_nd && !strcmp(tmp_nd->full_name,
					dev->of_node->full_name)) {
				of_property_read_u32(tz_nd, "polling-delay",
					&idle);
				(idle > 0) ? (priv->irq = 0) :
						(priv->irq = 1);
				break;
			}
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		goto error_unregister;

	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto error_unregister;
	}

	spin_lock_init(&priv->lock);
	INIT_DELAYED_WORK(&priv->work, rcar_gen3_thermal_work);

	of_property_read_u32(dev->of_node, "id", &priv->id);

	priv->zone = thermal_zone_of_sensor_register(dev, 0, priv,
				&rcar_gen3_tz_of_ops);


	rcar_gen3_thermal_init(priv);
	ret = rcar_gen3_thermal_update_temp(priv);
	if (ret < 0)
		goto error_unregister;

	if (IS_ERR(priv->zone)) {
		dev_err(dev, "Can't register thermal zone\n");
		ret = PTR_ERR(priv->zone);
		goto error_unregister;
	}

	rcar_thermal_irq_enable(priv);

	/* Interrupt */
	if (irq) {
		ret = devm_request_irq(dev, irq->start,
					rcar_gen3_thermal_irq, 0,
				       dev_name(dev), priv);
		if (ret) {
			dev_err(dev, "IRQ request failed\n ");
			goto error_unregister;
		}
	}

	dev_info(dev, "Thermal sensor probed\n");

	return 0;

error_unregister:
	rcar_gen3_thermal_remove(pdev);

	return ret;
}

static struct platform_driver rcar_gen3_thermal_driver = {
	.driver	= {
		.name	= "rcar_gen3_thermal",
		.of_match_table = rcar_thermal_dt_ids,
	},
	.probe		= rcar_gen3_thermal_probe,
	.remove		= rcar_gen3_thermal_remove,
};
module_platform_driver(rcar_gen3_thermal_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("R-Car Gen3 THS/CIVM driver");
MODULE_AUTHOR("Renesas Electronics Corporation");
