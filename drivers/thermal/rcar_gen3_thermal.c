/*
 *  R-Car Gen3 THS thermal sensor driver
 *  Based on rcar_thermal.c and work from Hien Dang and Khiem Nguyen.
 *
 * Copyright (C) 2016 Renesas Electronics Corporation.
 * Copyright (C) 2016 Sang Engineering
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
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>

/* Register offsets */
#define REG_GEN3_IRQSTR		0x04
#define REG_GEN3_IRQMSK		0x08
#define REG_GEN3_IRQCTL		0x0C
#define REG_GEN3_IRQEN		0x10
#define REG_GEN3_IRQTEMP1	0x14
#define REG_GEN3_IRQTEMP2	0x18
#define REG_GEN3_IRQTEMP3	0x1C
#define REG_GEN3_CTSR		0x20
#define REG_GEN3_THCTR		0x20
#define REG_GEN3_TEMP		0x28
#define REG_GEN3_THCODE1	0x50
#define REG_GEN3_THCODE2	0x54
#define REG_GEN3_THCODE3	0x58

/* CTSR bits */
#define CTSR_PONM	BIT(8)
#define CTSR_AOUT	BIT(7)
#define CTSR_THBGR	BIT(5)
#define CTSR_VMEN	BIT(4)
#define CTSR_VMST	BIT(1)
#define CTSR_THSST	BIT(0)

/* THCTR bits */
#define THCTR_PONM	BIT(6)
#define THCTR_THSST	BIT(0)

#define CTEMP_MASK	0xFFF

#define MCELSIUS(temp)	((temp) * 1000)
#define GEN3_FUSE_MASK	0xFFF

#define TSC_MAX_NUM	3

/* Structure for thermal temperature calculation */
struct equation_coefs {
	long a1;
	long b1;
	long a2;
	long b2;
};

struct rcar_gen3_thermal_tsc {
	void __iomem *base;
	struct thermal_zone_device *zone;
	struct equation_coefs coef;
	spinlock_t lock;
	u32 ctemp;
};

struct rcar_gen3_thermal_priv {
	struct rcar_gen3_thermal_tsc *tscs[TSC_MAX_NUM];
};

struct rcar_gen3_thermal_data {
	void (*thermal_init)(struct rcar_gen3_thermal_tsc *tsc);
};

/* Temperature calculation  */
#define RCAR3_THERMAL_GRAN 500
#define CODETSD(x) ((x) * 1000)
#define TJ_1 96000L
#define TJ_3 (-41000L)

static inline u32 rcar_gen3_thermal_read(struct rcar_gen3_thermal_tsc *tsc, u32 reg)
{
	return ioread32(tsc->base + reg);
}

static inline void rcar_gen3_thermal_write(struct rcar_gen3_thermal_tsc *tsc,
				     u32 reg, u32 data)
{
	iowrite32(data, tsc->base + reg);
}

static int _round_temp(int temp)
{
	int result, round_offs;

	round_offs = temp >= 0 ? RCAR3_THERMAL_GRAN / 2 : -RCAR3_THERMAL_GRAN / 2;
	result = (temp + round_offs) / RCAR3_THERMAL_GRAN;
	return result * RCAR3_THERMAL_GRAN;
}

static void _linear_coefficient_calculation(struct rcar_gen3_thermal_tsc *tsc,
					    int *ptat, int *thcode)
{
	int tj_2;
	long a1, b1;
	long a2, b2;
	long a1_num, a1_den;
	long a2_num, a2_den;

	tj_2 = (CODETSD((ptat[1] - ptat[2]) * 137)
		/ (ptat[0] - ptat[2])) - CODETSD(41);

	/* calculate coefficients for linear equation */
	a1_num = CODETSD(thcode[1] - thcode[2]);
	a1_den = tj_2 - TJ_3;
	a1 = (10000 * a1_num) / a1_den;
	b1 = (10000 * thcode[2]) - ((a1 * TJ_3) / 1000);

	a2_num = CODETSD(thcode[1] - thcode[0]);
	a2_den = tj_2 - TJ_1;
	a2 = (10000 * a2_num) / a2_den;
	b2 = (10000 * thcode[0]) - ((a2 * TJ_1) / 1000);

	tsc->coef.a1 = DIV_ROUND_CLOSEST(a1, 10);
	tsc->coef.b1 = DIV_ROUND_CLOSEST(b1, 10);
	tsc->coef.a2 = DIV_ROUND_CLOSEST(a2, 10);
	tsc->coef.b2 = DIV_ROUND_CLOSEST(b2, 10);
}

static int _linear_temp_converter(struct equation_coefs *coef,
					int temp_code)
{
	int temp, temp1, temp2;

	temp1 = MCELSIUS((CODETSD(temp_code) - coef->b1)) / coef->a1;
	temp2 = MCELSIUS((CODETSD(temp_code) - coef->b2)) / coef->a2;
	temp = (temp1 + temp2) / 2;

	return _round_temp(temp);
}

static int rcar_gen3_thermal_update_temp(struct rcar_gen3_thermal_tsc *tsc)
{
	u32 ctemp;
	unsigned long flags;

	spin_lock_irqsave(&tsc->lock, flags);
	ctemp = rcar_gen3_thermal_read(tsc, REG_GEN3_TEMP) & CTEMP_MASK;
	tsc->ctemp = ctemp;
	spin_unlock_irqrestore(&tsc->lock, flags);

	return 0;
}

static int rcar_gen3_thermal_get_temp(void *devdata, int *temp)
{
	struct rcar_gen3_thermal_tsc *tsc = devdata;
	int ctemp;
	unsigned long flags;

	rcar_gen3_thermal_update_temp(tsc);

	spin_lock_irqsave(&tsc->lock, flags);
	ctemp = _linear_temp_converter(&tsc->coef, tsc->ctemp);
	spin_unlock_irqrestore(&tsc->lock, flags);

	if ((ctemp < MCELSIUS(-40)) || (ctemp > MCELSIUS(125)))
		return -EIO;

	*temp = ctemp;

	return 0;
}

static struct thermal_zone_of_device_ops rcar_gen3_tz_of_ops = {
	.get_temp	= rcar_gen3_thermal_get_temp,
};

static void r8a7795_thermal_init(struct rcar_gen3_thermal_tsc *tsc)
{
	unsigned long flags;

	spin_lock_irqsave(&tsc->lock, flags);

	rcar_gen3_thermal_write(tsc, REG_GEN3_CTSR,  CTSR_THBGR);
	rcar_gen3_thermal_write(tsc, REG_GEN3_CTSR,  0x0);

	udelay(1000);

	rcar_gen3_thermal_write(tsc, REG_GEN3_CTSR, CTSR_PONM);
	rcar_gen3_thermal_write(tsc, REG_GEN3_IRQCTL, 0x3F);
	rcar_gen3_thermal_write(tsc, REG_GEN3_CTSR,
			   CTSR_PONM | CTSR_AOUT | CTSR_THBGR | CTSR_VMEN);

	udelay(100);

	rcar_gen3_thermal_write(tsc, REG_GEN3_CTSR,
			  CTSR_PONM | CTSR_AOUT | CTSR_THBGR | CTSR_VMEN |
			  CTSR_VMST | CTSR_THSST);

	spin_unlock_irqrestore(&tsc->lock, flags);
}

static void r8a7796_thermal_init(struct rcar_gen3_thermal_tsc *tsc)
{
	unsigned long flags;
	u32 reg_val;

	spin_lock_irqsave(&tsc->lock, flags);

	reg_val = rcar_gen3_thermal_read(tsc, REG_GEN3_THCTR);
	reg_val &= ~THCTR_PONM;
	rcar_gen3_thermal_write(tsc, REG_GEN3_THCTR, reg_val);

	udelay(1000);

	rcar_gen3_thermal_write(tsc, REG_GEN3_IRQCTL, 0x3F);
	reg_val = rcar_gen3_thermal_read(tsc, REG_GEN3_THCTR);
	reg_val |= THCTR_THSST;
	rcar_gen3_thermal_write(tsc, REG_GEN3_THCTR, reg_val);

	spin_unlock_irqrestore(&tsc->lock, flags);
}

static const struct rcar_gen3_thermal_data r8a7795_data = {
	.thermal_init = r8a7795_thermal_init,
};

static const struct rcar_gen3_thermal_data r8a7796_data = {
	.thermal_init = r8a7796_thermal_init,
};

static const struct of_device_id rcar_gen3_thermal_dt_ids[] = {
	{ .compatible = "renesas,r8a7795-thermal", .data = &r8a7795_data},
	{ .compatible = "renesas,r8a7796-thermal", .data = &r8a7796_data},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_gen3_thermal_dt_ids);

static int rcar_gen3_thermal_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return 0;
}

static int rcar_gen3_thermal_probe(struct platform_device *pdev)
{
	struct rcar_gen3_thermal_priv *priv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct thermal_zone_device *zone;
	int ret, i;
	const struct rcar_gen3_thermal_data *match_data = of_device_get_match_data(dev);

	/* default values if FUSEs are missing */
	int ptat[3] = { 2351, 1509, 435 };
	int thcode[TSC_MAX_NUM][3] = {
		{ 3248, 2800, 2221 },
		{ 3245, 2795, 2216 },
		{ 3250, 2805, 2237 },
	};

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	for (i = 0; i < TSC_MAX_NUM; i++) {
		struct rcar_gen3_thermal_tsc *tsc;

		tsc = devm_kzalloc(dev, sizeof(*tsc), GFP_KERNEL);
		if (!tsc)
			return -ENOMEM;

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break;

		tsc->base = devm_ioremap_resource(dev, res);
		if (IS_ERR(tsc->base)) {
			ret = PTR_ERR(tsc->base);
			goto error_unregister;
		}

		priv->tscs[i] = tsc;
		spin_lock_init(&tsc->lock);

		zone = devm_thermal_zone_of_sensor_register(dev, i, tsc,
					&rcar_gen3_tz_of_ops);
		if (IS_ERR(zone)) {
			dev_err(dev, "Can't register thermal zone\n");
			ret = PTR_ERR(zone);
			goto error_unregister;
		}
		tsc->zone = zone;

		match_data->thermal_init(tsc);

		_linear_coefficient_calculation(tsc, ptat, thcode[i]);

		ret = rcar_gen3_thermal_update_temp(tsc);

		if (ret < 0)
			goto error_unregister;
	}

	return 0;

error_unregister:
	rcar_gen3_thermal_remove(pdev);

	return ret;
}

static struct platform_driver rcar_gen3_thermal_driver = {
	.driver	= {
		.name	= "rcar_gen3_thermal",
		.of_match_table = rcar_gen3_thermal_dt_ids,
	},
	.probe		= rcar_gen3_thermal_probe,
	.remove		= rcar_gen3_thermal_remove,
};
module_platform_driver(rcar_gen3_thermal_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("R-Car Gen3 THS thermal sensor driver");
MODULE_AUTHOR("Wolfram Sang <wsa+renesas@sang-engineering.com>");
