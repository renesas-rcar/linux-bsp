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
#define REG_GEN3_FTHCODEH	0x48
#define REG_GEN3_FTHCODET	0x4C
#define REG_GEN3_FTHCODEL	0x50
#define REG_GEN3_FPTATH		0x54
#define REG_GEN3_FPTATT		0x58
#define REG_GEN3_FPTATL		0x5C

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

#define MCELSIUS(temp)			((temp) * 1000)
#define TEMP_IRQ_SHIFT(tsc_id)	(0x1 << tsc_id)
#define TEMPD_IRQ_SHIFT(tsc_id)	(0x1 << (tsc_id + 3))
#define GEN3_FUSE_MASK	0xFFF

#define RCAR_H3_WS10		0x4F00
#define RCAR_H3_WS11		0x4F01

/* Product register */
#define GEN3_PRR	0xFFF00044
#define GEN3_PRR_MASK	0x4FFF

/* Quadratic and linear equation config
 * Default is using quadratic equation.
 * To switch to linear formula calculation,
 * please comment out APPLY_QUADRATIC_EQUATION macro.
*/
#define APPLY_QUADRATIC_EQUATION

#ifdef APPLY_QUADRATIC_EQUATION
/* This struct is for quadratic equation.
 * y = ax^2 + bx + c
*/
struct equation_coefs {
	long a;
	long b;
	long c;
};
#else
/* This struct is for linear equation.
 * y = a1*x + b1
 * y = a2*x + b2
*/
struct equation_coefs {
	long a1;
	long b1;
	long a2;
	long b2;
};
#endif /* APPLY_QUADRATIC_EQUATION  */

struct fuse_factors {
	int fthcode_h;
	int fthcode_t;
	int fthcode_l;
	int fptat_h;
	int fptat_t;
	int fptat_l;
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
	u32 ctemp;
};

#define rcar_priv_to_dev(priv)		((priv)->dev)
#define rcar_has_irq_support(priv)	((priv)->irq)

/* Temperature calculation  */
#define CODETSD(x)		((x) * 1000)
#define TJ_H 96000L
#define TJ_L (-41000L)
#define PW2(x) ((x)*(x))

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

static void thermal_read_fuse_factor(struct rcar_thermal_priv *priv)
{
	u32  lsi_id;
	void __iomem *product_register = ioremap_nocache(GEN3_PRR, 4);

	/* For H3 WS1.0 and H3 WS1.1,
	 * these registers have not been programmed yet.
	 * We will use fixed value as temporary solution.
	 */
	lsi_id = ioread32(product_register) & GEN3_PRR_MASK;
	if ((lsi_id != RCAR_H3_WS10) && (lsi_id != RCAR_H3_WS11)) {
		priv->factor.fthcode_h = rcar_thermal_read(priv,
						REG_GEN3_FTHCODEH)
				& GEN3_FUSE_MASK;
		priv->factor.fthcode_t = rcar_thermal_read(priv,
						REG_GEN3_FTHCODET)
				& GEN3_FUSE_MASK;
		priv->factor.fthcode_l = rcar_thermal_read(priv,
						REG_GEN3_FTHCODEL)
				& GEN3_FUSE_MASK;
		priv->factor.fptat_h = rcar_thermal_read(priv, REG_GEN3_FPTATH)
				& GEN3_FUSE_MASK;
		priv->factor.fptat_t = rcar_thermal_read(priv, REG_GEN3_FPTATT)
				& GEN3_FUSE_MASK;
		priv->factor.fptat_l = rcar_thermal_read(priv, REG_GEN3_FPTATL)
				& GEN3_FUSE_MASK;
	} else {
		priv->factor.fthcode_h = 3355;
		priv->factor.fthcode_t = 2850;
		priv->factor.fthcode_l = 2110;
		priv->factor.fptat_h = 2320;
		priv->factor.fptat_t = 1510;
		priv->factor.fptat_l = 320;
	}
	iounmap(product_register);
}

#ifdef APPLY_QUADRATIC_EQUATION
static void _quadratic_coef_calc(struct rcar_thermal_priv *priv)
{
	long tj_t = 0;
	long a, b, c;
	long num_a, num_a1, num_a2;
	long den_a, den_a1, den_a2;
	long num_b1, num_b2, num_b, den_b;
	long para_c1, para_c2, para_c3;

	tj_t = (CODETSD((priv->factor.fptat_t - priv->factor.fptat_l) * 137)
		/ (priv->factor.fptat_h - priv->factor.fptat_l)) - CODETSD(41);

	/*
	 * The following code is to calculate coefficients
	 * for quadratic equation.
	 */
	/* Coefficient a */
	num_a1 = (CODETSD(priv->factor.fthcode_t)
			- CODETSD(priv->factor.fthcode_l)) * (TJ_H - TJ_L);
	num_a2 = (CODETSD(priv->factor.fthcode_h)
		- CODETSD(priv->factor.fthcode_l)) * (tj_t - TJ_L);
	num_a = num_a1 - num_a2;
	den_a1 = (PW2(tj_t) - PW2(TJ_L)) * (TJ_H - TJ_L);
	den_a2 = (PW2(TJ_H) - PW2(TJ_L)) * (tj_t - TJ_L);
	den_a = (den_a1 - den_a2) / 1000;
	a = (100000 * num_a) / den_a;

	/* Coefficient b */
	num_b1 = (CODETSD(priv->factor.fthcode_t)
		- CODETSD(priv->factor.fthcode_l))
			* (TJ_H - TJ_L);
	num_b2 = ((PW2(tj_t) - PW2(TJ_L)) * (TJ_H - TJ_L) * a) / 1000;
	num_b = 100000 * num_b1 - num_b2;
	den_b = ((tj_t - TJ_L) * (TJ_H - TJ_L));
	b = num_b / den_b;

	/* Coefficient c */
	para_c1 = 100000 * priv->factor.fthcode_l;
	para_c2 = (PW2(TJ_L) * a) / PW2(1000);
	para_c3 = (TJ_L * b) / 1000;
	c = para_c1 - para_c2 - para_c3;

	priv->coef.a = DIV_ROUND_CLOSEST(a, 10);
	priv->coef.b = DIV_ROUND_CLOSEST(b, 10);
	priv->coef.c = DIV_ROUND_CLOSEST(c, 10);
}
#else
static void _linear_coef_calc(struct rcar_thermal_priv *priv)
{
	int tj_t = 0;
	long a1, b1;
	long a2, b2;
	long a1_num, a1_den;
	long a2_num, a2_den;

	tj_t = (CODETSD((priv->factor.fptat_t - priv->factor.fptat_l) * 137)
		/ (priv->factor.fptat_h - priv->factor.fptat_l)) - CODETSD(41);

	/*
	 * The following code is to calculate coefficients for linear equation.
	 */
	/* Coefficient a1 and b1 */
	a1_num = CODETSD(priv->factor.fthcode_t - priv->factor.fthcode_l);
	a1_den = tj_t - TJ_L;
	a1 = (10000 * a1_num) / a1_den;
	b1 = (10000 * priv->factor.fthcode_l) - ((a1 * TJ_L) / 1000);

	/* Coefficient a2 and b2 */
	a2_num = CODETSD(priv->factor.fthcode_t - priv->factor.fthcode_h);
	a2_den = tj_t - TJ_H;
	a2 = (10000 * a2_num) / a2_den;
	b2 = (10000 * priv->factor.fthcode_h) - ((a2 * TJ_H) / 1000);

	priv->coef.a1 = DIV_ROUND_CLOSEST(a1, 10);
	priv->coef.b1 = DIV_ROUND_CLOSEST(b1, 10);
	priv->coef.a2 = DIV_ROUND_CLOSEST(a2, 10);
	priv->coef.b2 = DIV_ROUND_CLOSEST(b2, 10);
}
#endif /* APPLY_QUADRATIC_EQUATION */

static void thermal_coefficient_calculation(struct rcar_thermal_priv *priv)
{
#ifdef APPLY_QUADRATIC_EQUATION
	_quadratic_coef_calc(priv);
#else
	_linear_coef_calc(priv);
#endif /* APPLY_QUADRATIC_EQUATION */
}

#ifdef APPLY_QUADRATIC_EQUATION
int _quadratic_temp_converter(struct equation_coefs coef, int temp_code)
{
	int temp, temp1, temp2;
	long delta;

	/* Multiply with 10000 to sync with coef a, coef b and coef c. */
	delta = coef.b * coef.b - 4 * coef.a * (coef.c - 10000 * temp_code);

	/* Multiply temp with 1000 to convert to Mili-Celsius */
	temp1 = (CODETSD(-coef.b) + int_sqrt(1000000 * delta))
				/ (2 * coef.a);
	temp2 = (CODETSD(-coef.b) - int_sqrt(1000000 * delta))
				/ (2 * coef.a);

	if (temp1 > -45000000)
		temp = temp1;
	else
		temp = temp2;

	return round_temp(temp);
}
#else
int _linear_temp_converter(struct equation_coefs coef,
					int temp_code)
{
	int temp, temp1, temp2;

	temp1 = MCELSIUS((CODETSD(temp_code) - coef.b1)) / coef.a1;
	temp2 = MCELSIUS((CODETSD(temp_code) - coef.b2)) / coef.a2;
	temp = (temp1 + temp2) / 2;

	return round_temp(temp);
}
#endif /* APPLY_QUADRATIC_EQUATION */

int thermal_temp_converter(struct equation_coefs coef,
					int temp_code)
{
	int ctemp = 0;
#ifdef APPLY_QUADRATIC_EQUATION
	ctemp = _quadratic_temp_converter(coef, temp_code);
#else
	ctemp = _linear_temp_converter(coef, temp_code);
#endif /* APPLY_QUADRATIC_EQUATION */

	return ctemp;
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

		udelay(150);
	}

	priv->ctemp = ctemp;
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int rcar_gen3_thermal_get_temp(void *devdata, int *temp)
{
	struct rcar_thermal_priv *priv = devdata;
	int ctemp;
	unsigned long flags;

	rcar_gen3_thermal_update_temp(priv);

	spin_lock_irqsave(&priv->lock, flags);
	ctemp = thermal_temp_converter(priv->coef, priv->ctemp);
	spin_unlock_irqrestore(&priv->lock, flags);

	if ((ctemp < MCELSIUS(-40)) || (ctemp > MCELSIUS(125))) {
		struct device *dev = rcar_priv_to_dev(priv);

		dev_err(dev, "Temperature is not measured correclty!\n");

		return -EIO;
	}

	*temp = ctemp;

	return 0;
}

static int rcar_gen3_thermal_init(struct rcar_thermal_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

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

	priv->id = of_alias_get_id(dev->of_node, "tsc");

	priv->zone = thermal_zone_of_sensor_register(dev, 0, priv,
				&rcar_gen3_tz_of_ops);


	rcar_gen3_thermal_init(priv);
	thermal_read_fuse_factor(priv);
	thermal_coefficient_calculation(priv);
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
