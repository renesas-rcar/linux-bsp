/*
 *  R-Car Gen3 THS/CIVM thermal sensor driver
 *  Based on drivers/thermal/rcar_thermal.c
 *
 * Copyright (C) 2015-2016 Renesas Electronics Corporation.
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
#include <linux/sys_soc.h>
#include <linux/thermal.h>

/* Register offset */
#define REG_GEN3_CTSR		0x20
#define REG_GEN3_THCTR		0x20
#define REG_GEN3_IRQSTR		0x04
#define REG_GEN3_IRQMSK		0x08
#define REG_GEN3_IRQCTL		0x0C
#define REG_GEN3_IRQEN		0x10
#define REG_GEN3_IRQTEMP1	0x14
#define REG_GEN3_IRQTEMP2	0x18
#define REG_GEN3_IRQTEMP3	0x1C
#define REG_GEN3_TEMP		0x28
#define REG_GEN3_THCODE1	0x50
#define REG_GEN3_THCODE2	0x54
#define REG_GEN3_THCODE3	0x58

#define PTAT_BASE		0xE6198000
#define REG_GEN3_PTAT1		0x5C
#define REG_GEN3_PTAT2		0x60
#define REG_GEN3_PTAT3		0x64
#define PTAT_SIZE		REG_GEN3_PTAT3

/* CTSR bit */
#define PONM1            (0x1 << 8)	/* For H3 ES1.x */
#define AOUT            (0x1 << 7)
#define THBGR           (0x1 << 5)
#define VMEN            (0x1 << 4)
#define VMST            (0x1 << 1)
#define THSST           (0x1 << 0)

/* THCTR bit */
#define PONM2            (0x1 << 6)	/* For H3 ES2.0 and M3 ES1.0 */

#define CTEMP_MASK	0xFFF

#define IRQ_TEMP1_BIT	(0x1 << 0)
#define IRQ_TEMP2_BIT	(0x1 << 1)
#define IRQ_TEMP3_BIT	(0x1 << 2)
#define IRQ_TEMPD1_BIT	(0x1 << 3)
#define IRQ_TEMPD2_BIT	(0x1 << 4)
#define IRQ_TEMPD3_BIT	(0x1 << 5)

#define MCELSIUS(temp)			((temp) * 1000)
#define TEMP_IRQ_SHIFT(tsc_id)	(0x1 << tsc_id)
#define TEMPD_IRQ_SHIFT(tsc_id)	(0x1 << (tsc_id + 3))
#define GEN3_FUSE_MASK	0xFFF

/* Attribute structs describing Salvator-X revisions */
/* H3 ES1.0 and ES1.1 */
static const struct soc_device_attribute r8a7795es1[]  = {
	{ .soc_id = "r8a7795", .revision = "ES1.*" },
	{}
};

static const struct soc_device_attribute r8a7795[]  = {
	{ .soc_id = "r8a7795", .revision = "ES2.0" },
	{}
};

/* M3 ES1.0 */
static const struct soc_device_attribute r8a7796es10[]  = {
	{ .soc_id = "r8a7796", .revision = "ES1.0" },
	{}
};

/* Equation coefficients for thermal calculation formula.*/
struct equation_coefs {
	long a1;
	long b1;
	long a2;
	long b2;
};


struct fuse_factors {
	int thcode_1;
	int thcode_2;
	int thcode_3;
	int ptat_1;
	int ptat_2;
	int ptat_3;
};

struct rcar_thermal_priv {
	void __iomem *base;
	struct device *dev;
	struct thermal_zone_device *zone;
	struct delayed_work work;
	struct fuse_factors factor;
	struct equation_coefs coef;
	spinlock_t lock;
	int id;
	int irq;
	const struct rcar_thermal_data *data;
};

struct rcar_thermal_data {
	int (*thermal_init)(struct rcar_thermal_priv *priv);
};

#define rcar_priv_to_dev(priv)		((priv)->dev)
#define rcar_has_irq_support(priv)	((priv)->irq)

/* Temperature calculation  */
#define CODETSD(x)		((x) * 1000)
#define TJ_1 96000L
#define TJ_3 (-41000L)

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

static int round_temp(int temp)
{
	int tmp1, tmp2;
	int result = 0;

	tmp1 = abs(temp) % 1000;
	tmp2 = abs(temp) / 1000;

	if (tmp1 < 250)
		result = CODETSD(tmp2);
	else if (tmp1 < 750 && tmp1 >= 250)
		result = CODETSD(tmp2) + 500;
	else
		result = CODETSD(tmp2) + 1000;

	return ((temp < 0) ? (result * (-1)) : result);
}

static int thermal_read_fuse_factor(struct rcar_thermal_priv *priv)
{
	void __iomem *ptat_base;

	ptat_base = ioremap_nocache(PTAT_BASE, PTAT_SIZE);
	if (!ptat_base) {
		dev_err(rcar_priv_to_dev(priv), "Cannot map FUSE register\n");
		return -ENOMEM;
	}

	/* For H3 ES1.x, H3 ES2.0 and M3 ES1.0
	 * these registers have not been programmed yet.
	 * We will use fixed value as temporary solution.
	 */
	if (soc_device_match(r8a7795es1)
		|| soc_device_match(r8a7795)
		|| soc_device_match(r8a7796es10)) {
		priv->factor.ptat_1 = 2351;
		priv->factor.ptat_2 = 1509;
		priv->factor.ptat_3 = 435;
		switch (priv->id) {
		case 0:
			priv->factor.thcode_1 = 3248;
			priv->factor.thcode_2 = 2800;
			priv->factor.thcode_3 = 2221;
			break;
		case 1:
			priv->factor.thcode_1 = 3245;
			priv->factor.thcode_2 = 2795;
			priv->factor.thcode_3 = 2216;
			break;
		case 2:
			priv->factor.thcode_1 = 3250;
			priv->factor.thcode_2 = 2805;
			priv->factor.thcode_3 = 2237;
			break;
		}
	} else {
		priv->factor.thcode_1 = rcar_thermal_read(priv,
						REG_GEN3_THCODE1)
				& GEN3_FUSE_MASK;
		priv->factor.thcode_2 = rcar_thermal_read(priv,
						REG_GEN3_THCODE2)
				& GEN3_FUSE_MASK;
		priv->factor.thcode_3 = rcar_thermal_read(priv,
						REG_GEN3_THCODE3)
				& GEN3_FUSE_MASK;
		priv->factor.ptat_1 = ioread32(ptat_base + REG_GEN3_PTAT1)
				& GEN3_FUSE_MASK;
		priv->factor.ptat_2 = ioread32(ptat_base + REG_GEN3_PTAT2)
				& GEN3_FUSE_MASK;
		priv->factor.ptat_3 = ioread32(ptat_base + REG_GEN3_PTAT3)
				& GEN3_FUSE_MASK;
	}

	iounmap(ptat_base);

	return 0;
}

static void thermal_coefficient_calculation(struct rcar_thermal_priv *priv)
{
	int tj_2 = 0;
	long a1, b1;
	long a2, b2;
	long a1_num, a1_den;
	long a2_num, a2_den;

	tj_2 = (CODETSD((priv->factor.ptat_2 - priv->factor.ptat_3) * 137)
		/ (priv->factor.ptat_1 - priv->factor.ptat_3)) - CODETSD(41);

	/*
	 * The following code is to calculate coefficients.
	 */
	/* Coefficient a1 and b1 */
	a1_num = CODETSD(priv->factor.thcode_2 - priv->factor.thcode_3);
	a1_den = tj_2 - TJ_3;
	a1 = (10000 * a1_num) / a1_den;
	b1 = (10000 * priv->factor.thcode_3) - ((a1 * TJ_3) / 1000);

	/* Coefficient a2 and b2 */
	a2_num = CODETSD(priv->factor.thcode_2 - priv->factor.thcode_1);
	a2_den = tj_2 - TJ_1;
	a2 = (10000 * a2_num) / a2_den;
	b2 = (10000 * priv->factor.thcode_1) - ((a2 * TJ_1) / 1000);

	priv->coef.a1 = DIV_ROUND_CLOSEST(a1, 10);
	priv->coef.b1 = DIV_ROUND_CLOSEST(b1, 10);
	priv->coef.a2 = DIV_ROUND_CLOSEST(a2, 10);
	priv->coef.b2 = DIV_ROUND_CLOSEST(b2, 10);
}

int thermal_temp_converter(struct equation_coefs coef,
					int temp_code)
{
	int temp, temp1, temp2;

	temp1 = MCELSIUS((CODETSD(temp_code) - coef.b1)) / coef.a1;
	temp2 = MCELSIUS((CODETSD(temp_code) - coef.b2)) / coef.a2;
	temp = (temp1 + temp2) / 2;

	return round_temp(temp);
}

int thermal_celsius_to_temp(struct equation_coefs coef,
					int ctemp)
{
	int temp_code, temp1, temp2;

	temp1 = (ctemp * coef.a1 / 1000 + coef.b1) / 1000;
	temp2 = (ctemp * coef.a2 / 1000 + coef.b2) / 1000;
	temp_code = (temp1 + temp2) / 2;

	return temp_code;
}

/*
 *		Zone device functions
 */
static int rcar_gen3_thermal_update_temp(struct rcar_thermal_priv *priv)
{
	u32 ctemp;
	unsigned long flags;
	int temp_cel, temp_code;

	spin_lock_irqsave(&priv->lock, flags);

	ctemp = rcar_thermal_read(priv, REG_GEN3_TEMP) & CTEMP_MASK;
	if (rcar_has_irq_support(priv)) {
		temp_cel = thermal_temp_converter(priv->coef, ctemp);

		/* set the interrupts to exceed the temperature */
		temp_code = thermal_celsius_to_temp(priv->coef,
						    temp_cel + MCELSIUS(1));
		rcar_thermal_write(priv, REG_GEN3_IRQTEMP1, temp_code);

		/* set the interrupts to fall below the temperature */
		temp_code = thermal_celsius_to_temp(priv->coef,
						    temp_cel - MCELSIUS(1));
		rcar_thermal_write(priv, REG_GEN3_IRQTEMP2, temp_code);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int rcar_gen3_thermal_get_temp(void *devdata, int *temp)
{
	struct rcar_thermal_priv *priv = devdata;
	int ctemp;
	unsigned long flags;
	u32 ctemp_code;

	spin_lock_irqsave(&priv->lock, flags);
	ctemp_code = rcar_thermal_read(priv, REG_GEN3_TEMP) & CTEMP_MASK;
	ctemp = thermal_temp_converter(priv->coef, ctemp_code);
	spin_unlock_irqrestore(&priv->lock, flags);

	if ((ctemp < MCELSIUS(-40)) || (ctemp > MCELSIUS(125))) {
		struct device *dev = rcar_priv_to_dev(priv);

		dev_dbg(dev, "Temperature is not measured correctly!\n");

		return -EIO;
	}

	*temp = ctemp;

	return 0;
}

static int rcar_gen3_r8a7796_thermal_init(struct rcar_thermal_priv *priv)
{
	unsigned long flags;
	unsigned long reg_val;

	spin_lock_irqsave(&priv->lock, flags);
	reg_val = rcar_thermal_read(priv, REG_GEN3_THCTR);
	reg_val &= ~PONM2;
	rcar_thermal_write(priv, REG_GEN3_THCTR, reg_val);
	udelay(1000);
	rcar_thermal_write(priv, REG_GEN3_IRQCTL, 0x3F);
	rcar_thermal_write(priv, REG_GEN3_IRQEN,
			   IRQ_TEMP1_BIT | IRQ_TEMPD2_BIT);
	reg_val = rcar_thermal_read(priv, REG_GEN3_THCTR);
	reg_val |= THSST;
	rcar_thermal_write(priv, REG_GEN3_THCTR, reg_val);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int rcar_gen3_r8a7795_thermal_init(struct rcar_thermal_priv *priv)
{
	unsigned long flags;

	if (soc_device_match(r8a7795es1)) {
		spin_lock_irqsave(&priv->lock, flags);

		rcar_thermal_write(priv, REG_GEN3_CTSR,  THBGR);
		rcar_thermal_write(priv, REG_GEN3_CTSR,  0x0);

		udelay(1000);

		rcar_thermal_write(priv, REG_GEN3_CTSR, PONM1);
		rcar_thermal_write(priv, REG_GEN3_IRQCTL, 0x3F);
		rcar_thermal_write(priv, REG_GEN3_IRQEN,
				   IRQ_TEMP1_BIT | IRQ_TEMPD2_BIT);
		rcar_thermal_write(priv, REG_GEN3_CTSR,
				PONM1 | AOUT | THBGR | VMEN);
		udelay(100);

		rcar_thermal_write(priv, REG_GEN3_CTSR,
				PONM1 | AOUT | THBGR | VMEN | VMST | THSST);

		spin_unlock_irqrestore(&priv->lock, flags);
	} else
		/* H3 ES2.0 has the same init flow with M3 ES1.0 */
		rcar_gen3_r8a7796_thermal_init(priv);

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
		enable ? (IRQ_TEMP1_BIT | IRQ_TEMPD2_BIT) : 0);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void rcar_gen3_thermal_work(struct work_struct *work)
{
	struct rcar_thermal_priv *priv;

	priv = container_of(work, struct rcar_thermal_priv, work.work);

	rcar_gen3_thermal_update_temp(priv);
	thermal_zone_device_update(priv->zone, THERMAL_EVENT_UNSPECIFIED);

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

	if (status == 0)
		return IRQ_NONE;

	if (status & (IRQ_TEMP1_BIT | IRQ_TEMPD2_BIT)) {
		rcar_thermal_irq_disable(priv);
		schedule_delayed_work(&priv->work, 0);
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
	thermal_zone_of_sensor_unregister(dev, priv->zone);

	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return 0;
}

static const struct rcar_thermal_data r8a7795_data = {
	.thermal_init = rcar_gen3_r8a7795_thermal_init,
};

static const struct rcar_thermal_data r8a7796_data = {
	.thermal_init = rcar_gen3_r8a7796_thermal_init,
};

static const struct of_device_id rcar_thermal_dt_ids[] = {
	{ .compatible = "renesas,thermal-r8a7795", .data = &r8a7795_data},
	{ .compatible = "renesas,thermal-r8a7796", .data = &r8a7796_data},
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
	int i, irq_cnt;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->dev = dev;

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	priv->data = of_device_get_match_data(dev);
	if (!priv->data)
		goto error_unregister;

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

	priv->id = of_alias_get_id(dev->of_node, "tsc");

	priv->zone = devm_thermal_zone_of_sensor_register(dev, 0, priv,
				&rcar_gen3_tz_of_ops);

	if (IS_ERR(priv->zone)) {
		dev_err(dev, "Can't register thermal zone\n");
		ret = PTR_ERR(priv->zone);
		priv->zone = NULL;
		goto error_unregister;
	}

	priv->data->thermal_init(priv);
	ret = thermal_read_fuse_factor(priv);
	if (ret)
		goto error_unregister;
	thermal_coefficient_calculation(priv);
	ret = rcar_gen3_thermal_update_temp(priv);

	if (ret < 0)
		goto error_unregister;

	rcar_thermal_irq_enable(priv);

	/* Interrupt */
	if (rcar_has_irq_support(priv)) {
		irq_cnt = platform_irq_count(pdev);
		for (i = 0; i < irq_cnt; i++) {
			irq = platform_get_resource(pdev, IORESOURCE_IRQ, i);
			ret = devm_request_irq(dev, irq->start,
					       rcar_gen3_thermal_irq,
					       IRQF_SHARED,
					       dev_name(dev), priv);
			if (ret) {
				dev_err(dev, "IRQ request failed\n ");
				goto error_unregister;
			}
		}
	}

	dev_info(dev, "Thermal sensor probed\n");

	return 0;

error_unregister:
	rcar_gen3_thermal_remove(pdev);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int rcar_gen3_thermal_suspend(struct device *dev)
{
	struct rcar_thermal_priv *priv = dev_get_drvdata(dev);

	pr_debug("%s\n", __func__);
	rcar_thermal_irq_disable(priv);

	return 0;
}

static int rcar_gen3_thermal_resume(struct device *dev)
{
	struct rcar_thermal_priv *priv = dev_get_drvdata(dev);

	pr_debug("%s\n", __func__);
	priv->data->thermal_init(priv);
	rcar_thermal_irq_enable(priv);
	rcar_gen3_thermal_update_temp(priv);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rcar_gen3_thermal_pm_ops,
			rcar_gen3_thermal_suspend,
			rcar_gen3_thermal_resume);

#define DEV_PM_OPS (&rcar_gen3_thermal_pm_ops)
#else
#define DEV_PM_OPS NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver rcar_gen3_thermal_driver = {
	.driver	= {
		.name	= "rcar_gen3_thermal",
		.pm	= DEV_PM_OPS,
		.of_match_table = rcar_thermal_dt_ids,
	},
	.probe		= rcar_gen3_thermal_probe,
	.remove		= rcar_gen3_thermal_remove,
};
module_platform_driver(rcar_gen3_thermal_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("R-Car Gen3 THS/CIVM driver");
MODULE_AUTHOR("Renesas Electronics Corporation");
