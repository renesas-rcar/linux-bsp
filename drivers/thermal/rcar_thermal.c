/*
 *  R-Car THS/TSC thermal sensor driver
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
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
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>

/* GEN2 */
#define IDLE_INTERVAL	5000

#define COMMON_STR	0x00
#define COMMON_ENR	0x04
#define COMMON_INTMSK	0x0c

#define REG_POSNEG	0x20
#define REG_FILONOFF	0x28
#define REG_THSCR	0x2c
#define REG_THSSR	0x30
#define REG_INTCTRL	0x34

/* GEN3 */
#define REG_GEN3_CTSR		0x20
#define REG_GEN3_IRQSTR		0x04
#define REG_GEN3_IRQMSK		0x08
#define REG_GEN3_IRQCTL		0x0C
#define REG_GEN3_IRQEN		0x10
#define REG_GEN3_IRQTEMP1	0x14
#define REG_GEN3_IRQTEMP2	0x18
#define REG_GEN3_IRQTEMP3	0x1C
#define REG_GEN3_TEMP		0x28

/* THSCR */
#define CPCTL		(1 << 12)

/* THSSR */
#define CTEMP		0x3f
#define GEN3_CTEMP_MASK	0xFFF

#define POWERON		0

/* CTSR  */
#define PONSEQSTOP	(0x1 << 27)
#define PONM		(0x1 << 8)
#define AOUT		(0x1 << 7)
#define THBGR		(0x1 << 5)
#define VMEN		(0x1 << 4)
#define VMST		(0x1 << 1)
#define THSST		(0x1 << 0)

#define TEMP_IRQ_SHIFT(tsc_id)	(0x1 << tsc_id)
#define TEMPD_IRQ_SHIFT(tsc_id)	(0x1 << (tsc_id + 3))

#define GEN2_COMPAT	2
#define GEN3_COMPAT	3

struct rcar_thermal_common {
	void __iomem *base;
	struct device *dev;
	struct list_head head;
	spinlock_t lock;
	u32 irq;
	const struct rcar_thermal_data *data;
};

struct rcar_thermal_priv {
	void __iomem *base;
	struct rcar_thermal_common *common;
	struct thermal_zone_device *zone;
	struct delayed_work work;
	struct mutex lock;
	spinlock_t plock;
	struct list_head list;
	int id;
	u32 ctemp;
};

struct rcar_thermal_data {
	void (*work)(struct work_struct *work);
	int (*thermal_init)(struct rcar_thermal_priv *priv);
	irqreturn_t (*irq_handler)(int irq, void *data);
	int (*update_temp)(struct rcar_thermal_priv *priv);
	int compat;
};

#define rcar_thermal_for_each_priv(pos, common)	\
	list_for_each_entry(pos, &common->head, list)

#define MCELSIUS(temp)			((temp) * 1000)
#define rcar_zone_to_priv(zone)		((zone)->devdata)
#define rcar_priv_to_dev(priv)		((priv)->common->dev)
#define rcar_has_irq_support(priv)	((priv)->common->base)
#define rcar_gen3_has_irq_support(priv)	((priv)->common->irq)
#define rcar_id_to_shift(priv)		((priv)->id * 8)

/* Temperature conversion  */
#define TEMP_CONVERT(ctemp)	(((1000 * MCELSIUS(ctemp)) - 2536700) / 7468)

#ifdef DEBUG
# define rcar_force_update_temp(priv)	1
#else
# define rcar_force_update_temp(priv)	0
#endif

/*
 *		basic functions
 */
#define rcar_thermal_common_read(c, r) \
	_rcar_thermal_common_read(c, COMMON_ ##r)
static u32 _rcar_thermal_common_read(struct rcar_thermal_common *common,
				     u32 reg)
{
	return ioread32(common->base + reg);
}

#define rcar_thermal_common_write(c, r, d) \
	_rcar_thermal_common_write(c, COMMON_ ##r, d)
static void _rcar_thermal_common_write(struct rcar_thermal_common *common,
				       u32 reg, u32 data)
{
	iowrite32(data, common->base + reg);
}

#define rcar_thermal_common_bset(c, r, m, d) \
	_rcar_thermal_common_bset(c, COMMON_ ##r, m, d)
static void _rcar_thermal_common_bset(struct rcar_thermal_common *common,
				      u32 reg, u32 mask, u32 data)
{
	u32 val;

	val = ioread32(common->base + reg);
	val &= ~mask;
	val |= (data & mask);
	iowrite32(val, common->base + reg);
}

#define rcar_thermal_read(p, r) _rcar_thermal_read(p, REG_ ##r)
static u32 _rcar_thermal_read(struct rcar_thermal_priv *priv, u32 reg)
{
	return ioread32(priv->base + reg);
}

#define rcar_thermal_write(p, r, d) _rcar_thermal_write(p, REG_ ##r, d)
static void _rcar_thermal_write(struct rcar_thermal_priv *priv,
				u32 reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

#define rcar_thermal_bset(p, r, m, d) _rcar_thermal_bset(p, REG_ ##r, m, d)
static void _rcar_thermal_bset(struct rcar_thermal_priv *priv, u32 reg,
			       u32 mask, u32 data)
{
	u32 val;

	val = ioread32(priv->base + reg);
	val &= ~mask;
	val |= (data & mask);
	iowrite32(val, priv->base + reg);
}

/*
 *		zone device functions
 */
static int rcar_thermal_update_temp(struct rcar_thermal_priv *priv)
{
	struct device *dev = rcar_priv_to_dev(priv);
	int i;
	u32 ctemp, old, new;
	int ret = -EINVAL;

	mutex_lock(&priv->lock);

	/*
	 * TSC decides a value of CPTAP automatically,
	 * and this is the conditions which validate interrupt.
	 */
	rcar_thermal_bset(priv, THSCR, CPCTL, CPCTL);

	ctemp = 0;
	old = ~0;
	for (i = 0; i < 128; i++) {
		/*
		 * we need to wait 300us after changing comparator offset
		 * to get stable temperature.
		 * see "Usage Notes" on datasheet
		 */
		udelay(300);

		new = rcar_thermal_read(priv, THSSR) & CTEMP;
		if (new == old) {
			ctemp = new;
			break;
		}
		old = new;
	}

	if (!ctemp) {
		dev_err(dev, "thermal sensor was broken\n");
		goto err_out_unlock;
	}

	/*
	 * enable IRQ
	 */
	if (rcar_has_irq_support(priv)) {
		rcar_thermal_write(priv, FILONOFF, 0);

		/* enable Rising/Falling edge interrupt */
		rcar_thermal_write(priv, POSNEG,  0x1);
		rcar_thermal_write(priv, INTCTRL, (((ctemp - 0) << 8) |
						   ((ctemp - 1) << 0)));
	}

	dev_dbg(dev, "thermal%d  %d -> %d\n", priv->id, priv->ctemp, ctemp);

	priv->ctemp = ctemp;
	ret = 0;
err_out_unlock:
	mutex_unlock(&priv->lock);
	return ret;
}

static int _round_temp(int i)
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

static int rcar_gen3_thermal_update_temp(struct rcar_thermal_priv *priv)
{
	struct device *dev = rcar_priv_to_dev(priv);
	u32 ctemp;
	int i;

	mutex_lock(&priv->lock);

	/*
	 * enable IRQ
	 */
	for (i = 0; i < 8; i++) {
		udelay(300);
		ctemp = rcar_thermal_read(priv, GEN3_TEMP) & GEN3_CTEMP_MASK;
		if (rcar_gen3_has_irq_support(priv))
			_rcar_thermal_write(priv,
					REG_GEN3_IRQTEMP1 + (priv->id * 4),
					ctemp);
	}

	dev_dbg(dev, "thermal%d  %d -> %d\n", priv->id, priv->ctemp, ctemp);
	dev_dbg(dev, "thermal%d  %d -> %d\n", priv->id, priv->ctemp,
		_round_temp(TEMP_CONVERT(ctemp)));

	priv->ctemp = _round_temp(TEMP_CONVERT(ctemp));
	mutex_unlock(&priv->lock);

	return 0;
}

static int rcar_thermal_get_temp(struct thermal_zone_device *zone, int *temp)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);

	if (!rcar_has_irq_support(priv) || rcar_force_update_temp(priv))
		rcar_thermal_update_temp(priv);

	mutex_lock(&priv->lock);
	*temp =  MCELSIUS((priv->ctemp * 5) - 65);
	mutex_unlock(&priv->lock);

	return 0;
}

static int rcar_gen3_thermal_get_temp(void *devdata, int *temp)
{
	struct rcar_thermal_priv *priv = devdata;
	u32 ctemp;

	mutex_lock(&priv->lock);
	/*
	 * temp = (THCODE - 2536.7) / 7.468
	 */
	ctemp = rcar_thermal_read(priv, GEN3_TEMP) & GEN3_CTEMP_MASK;
	*temp = _round_temp(TEMP_CONVERT(ctemp));
	priv->ctemp = *temp;
	mutex_unlock(&priv->lock);

	return 0;
}

static int rcar_thermal_init(struct rcar_thermal_priv *priv)
{
	/* Do nothing for Gen2  */
	return 0;
}

static int rcar_gen3_thermal_init(struct rcar_thermal_priv *priv)
{
	int status;

	mutex_lock(&priv->lock);
	status = rcar_thermal_read(priv, GEN3_CTSR);

	/* Check Thermal status, whether it is Power On or Normal mode */
	if ((status & PONSEQSTOP) == POWERON)
		rcar_thermal_write(priv, GEN3_CTSR,  0x0);

	rcar_thermal_write(priv, GEN3_CTSR,
			PONM | AOUT | THBGR | VMEN);
	udelay(100);
	rcar_thermal_write(priv, GEN3_CTSR,
			PONM | AOUT | THBGR | VMEN | VMST | THSST);
	udelay(1000);

	rcar_thermal_write(priv, GEN3_IRQCTL, 0x3F);
	rcar_thermal_write(priv, GEN3_IRQEN, TEMP_IRQ_SHIFT(priv->id) |
						TEMPD_IRQ_SHIFT(priv->id));

	mutex_unlock(&priv->lock);
	return 0;
}

static int rcar_thermal_get_trip_type(struct thermal_zone_device *zone,
				      int trip, enum thermal_trip_type *type)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);
	struct device *dev = rcar_priv_to_dev(priv);

	/* see rcar_thermal_get_temp() */
	switch (trip) {
	case 0: /* +90 <= temp */
		*type = THERMAL_TRIP_CRITICAL;
		break;
	default:
		dev_err(dev, "rcar driver trip error\n");
		return -EINVAL;
	}

	return 0;
}

static int rcar_thermal_get_trip_temp(struct thermal_zone_device *zone,
				      int trip, int *temp)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);
	struct device *dev = rcar_priv_to_dev(priv);

	/* see rcar_thermal_get_temp() */
	switch (trip) {
	case 0: /* +90 <= temp */
		*temp = MCELSIUS(90);
		break;
	default:
		dev_err(dev, "rcar driver trip error\n");
		return -EINVAL;
	}

	return 0;
}
static int rcar_thermal_notify(struct thermal_zone_device *zone,
			       int trip, enum thermal_trip_type type)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);
	struct device *dev = rcar_priv_to_dev(priv);

	switch (type) {
	case THERMAL_TRIP_CRITICAL:
		/* FIXME */
		dev_warn(dev, "Thermal reached to critical temperature\n");
		break;
	default:
		break;
	}

	return 0;
}

static struct thermal_zone_device_ops rcar_thermal_zone_ops = {
	.get_temp	= rcar_thermal_get_temp,
	.get_trip_type	= rcar_thermal_get_trip_type,
	.get_trip_temp	= rcar_thermal_get_trip_temp,
	.notify		= rcar_thermal_notify,
};

static struct thermal_zone_of_device_ops rcar_tzone_of_ops = {
	.get_temp	= rcar_gen3_thermal_get_temp,
};

/*
 *		interrupt
 */
#define rcar_thermal_irq_enable(p)	_rcar_thermal_irq_ctrl(p, 1)
#define rcar_thermal_irq_disable(p)	_rcar_thermal_irq_ctrl(p, 0)
static void _rcar_thermal_irq_ctrl(struct rcar_thermal_priv *priv, int enable)
{
	struct rcar_thermal_common *common = priv->common;
	unsigned long flags;
	u32 mask = 0x3 << rcar_id_to_shift(priv); /* enable Rising/Falling */

	if (common->data->compat == GEN3_COMPAT) {
		spin_lock_irqsave(&priv->plock, flags);
		rcar_thermal_write(priv, GEN3_IRQMSK,
			enable ? (TEMP_IRQ_SHIFT(priv->id) |
				TEMPD_IRQ_SHIFT(priv->id)) : 0);
		spin_unlock_irqrestore(&priv->plock, flags);
	} else {
		spin_lock_irqsave(&common->lock, flags);
		rcar_thermal_common_bset(common, INTMSK, mask,
					enable ? 0 : mask);
		spin_unlock_irqrestore(&common->lock, flags);
	}

}

static void rcar_thermal_work(struct work_struct *work)
{
	struct rcar_thermal_priv *priv;
	int cctemp, nctemp;

	priv = container_of(work, struct rcar_thermal_priv, work.work);

	rcar_thermal_get_temp(priv->zone, &cctemp);
	rcar_thermal_update_temp(priv);
	rcar_thermal_irq_enable(priv);

	rcar_thermal_get_temp(priv->zone, &nctemp);
	if (nctemp != cctemp)
		thermal_zone_device_update(priv->zone);
}

static void rcar_gen3_thermal_work(struct work_struct *work)
{
	struct rcar_thermal_priv *priv;

	priv = container_of(work, struct rcar_thermal_priv, work.work);

	rcar_gen3_thermal_update_temp(priv);
	thermal_zone_device_update(priv->zone);

	rcar_thermal_irq_enable(priv);
}

static u32 rcar_thermal_had_changed(struct rcar_thermal_priv *priv, u32 status)
{
	struct device *dev = rcar_priv_to_dev(priv);

	status = (status >> rcar_id_to_shift(priv)) & 0x3;

	if (status) {
		dev_dbg(dev, "thermal%d %s%s\n",
			priv->id,
			(status & 0x2) ? "Rising " : "",
			(status & 0x1) ? "Falling" : "");
	}

	return status;
}

static irqreturn_t rcar_thermal_irq(int irq, void *data)
{
	struct rcar_thermal_common *common = data;
	struct rcar_thermal_priv *priv;
	unsigned long flags;
	u32 status, mask;

	spin_lock_irqsave(&common->lock, flags);

	mask	= rcar_thermal_common_read(common, INTMSK);
	status	= rcar_thermal_common_read(common, STR);
	rcar_thermal_common_write(common, STR, 0x000F0F0F & mask);

	spin_unlock_irqrestore(&common->lock, flags);

	status = status & ~mask;

	/*
	 * check the status
	 */
	rcar_thermal_for_each_priv(priv, common) {
		if (rcar_thermal_had_changed(priv, status)) {
			rcar_thermal_irq_disable(priv);
			schedule_delayed_work(&priv->work,
					      msecs_to_jiffies(300));
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t rcar_gen3_thermal_irq(int irq, void *data)
{
	struct rcar_thermal_common *common = data;
	struct rcar_thermal_priv *priv;
	unsigned long flags;
	int status;

	rcar_thermal_for_each_priv(priv, common) {
		spin_lock_irqsave(&priv->plock, flags);
		status = rcar_thermal_read(priv, GEN3_IRQSTR);
		rcar_thermal_write(priv, GEN3_IRQSTR, 0);
		spin_unlock_irqrestore(&priv->plock, flags);

		if ((status & TEMP_IRQ_SHIFT(priv->id)) ||
			(status & TEMPD_IRQ_SHIFT(priv->id))) {
			rcar_thermal_irq_disable(priv);
			schedule_delayed_work(&priv->work,
					      msecs_to_jiffies(300));
		}
	}

	return IRQ_HANDLED;
}

static const struct rcar_thermal_data gen2_thermal_data = {
	.work		= rcar_thermal_work,
	.thermal_init	= rcar_thermal_init,
	.irq_handler	= rcar_thermal_irq,
	.update_temp	= rcar_thermal_update_temp,
	.compat		= GEN2_COMPAT,
};

static const struct rcar_thermal_data gen3_thermal_data = {
	.work		= rcar_gen3_thermal_work,
	.thermal_init	= rcar_gen3_thermal_init,
	.irq_handler	= rcar_gen3_thermal_irq,
	.update_temp	= rcar_gen3_thermal_update_temp,
	.compat		= GEN3_COMPAT,
};

static const struct of_device_id rcar_thermal_dt_ids[] = {
	{ .compatible = "renesas,thermal-r8a7790", .data = &gen2_thermal_data},
	{ .compatible = "renesas,thermal-r8a7791", .data = &gen2_thermal_data},
	{ .compatible = "renesas,thermal-r8a7793", .data = &gen2_thermal_data},
	{ .compatible = "renesas,thermal-r8a7794", .data = &gen2_thermal_data},
	{ .compatible = "renesas,thermal-r8a7795", .data = &gen3_thermal_data},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_thermal_dt_ids);

/*
 *		platform functions
 */
static int rcar_thermal_probe(struct platform_device *pdev)
{
	struct rcar_thermal_common *common;
	struct rcar_thermal_priv *priv;
	struct device *dev = &pdev->dev;
	struct resource *res, *irq;
	int mres = 0;
	int i;
	int ret = -ENODEV;
	int idle = IDLE_INTERVAL;
	u32 enr_bits = 0;
	struct device_node *tz_nd, *tmp_nd;
	const struct of_device_id *of_id;

	common = devm_kzalloc(dev, sizeof(*common), GFP_KERNEL);
	if (!common)
		return -ENOMEM;

	INIT_LIST_HEAD(&common->head);
	spin_lock_init(&common->lock);
	common->dev = dev;

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	of_id = of_match_device(rcar_thermal_dt_ids, dev);
	if (of_id)
		common->data =  of_id->data;

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq) {
		common->irq = 1;
		if (common->data->compat == GEN3_COMPAT) {
			for_each_node_with_property(tz_nd, "polling-delay") {
				tmp_nd = of_parse_phandle(tz_nd,
							"thermal-sensors", 0);
				if (tmp_nd && !strcmp(tmp_nd->full_name,
						dev->of_node->full_name)) {
					of_property_read_u32(tz_nd,
							"polling-delay", &idle);
					(idle > 0) ? (common->irq = 0) :
							(common->irq = 1);
					break;
				}
			}
		} else {
			/*
			 * platform has IRQ support.
			 * Then, driver uses common registers
			 * rcar_has_irq_support() will be enabled
			 */
			res = platform_get_resource(pdev, IORESOURCE_MEM,
							mres++);
			common->base = devm_ioremap_resource(dev, res);
			if (IS_ERR(common->base))
				return PTR_ERR(common->base);

			idle = 0; /* polling delay is not needed */
		}
	}

	for (i = 0;; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, mres++);
		if (!res)
			break;

		priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
		if (!priv) {
			ret = -ENOMEM;
			goto error_unregister;
		}

		priv->base = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->base)) {
			ret = PTR_ERR(priv->base);
			goto error_unregister;
		}

		priv->common = common;
		priv->id = i;
		mutex_init(&priv->lock);
		spin_lock_init(&priv->plock);
		INIT_LIST_HEAD(&priv->list);
		INIT_DELAYED_WORK(&priv->work, common->data->work);
		if (common->data->compat == GEN3_COMPAT) {
			of_property_read_u32(dev->of_node, "id", &priv->id);
			priv->zone = thermal_zone_of_sensor_register(
						dev,
						0, priv,
						&rcar_tzone_of_ops);

		} else
			priv->zone = thermal_zone_device_register(
						"rcar_thermal",
						1, 0, priv,
						&rcar_thermal_zone_ops, NULL, 0,
						idle);

		common->data->thermal_init(priv);
		common->data->update_temp(priv);
		if (IS_ERR(priv->zone)) {
			dev_err(dev, "can't register thermal zone\n");
			ret = PTR_ERR(priv->zone);
			goto error_unregister;
		}

		if (rcar_has_irq_support(priv) || irq)
			rcar_thermal_irq_enable(priv);

		list_move_tail(&priv->list, &common->head);

		/* update ENR bits */
		enr_bits |= 3 << (i * 8);
	}

	/* enable temperature comparation */
	if (irq) {
		ret = devm_request_irq(dev, irq->start,
					common->data->irq_handler, 0,
					dev_name(dev), common);

		if (common->data->compat != GEN3_COMPAT)
			rcar_thermal_common_write(common, ENR, enr_bits);

		if (ret) {
			dev_err(dev, "irq request failed\n ");
			goto error_unregister;
		}
	}

	platform_set_drvdata(pdev, common);

	dev_info(dev, "%d sensor probed\n", i);

	return 0;

error_unregister:
	rcar_thermal_for_each_priv(priv, common) {
		if (rcar_has_irq_support(priv) ||
			rcar_gen3_has_irq_support(priv))
			rcar_thermal_irq_disable(priv);
		thermal_zone_device_unregister(priv->zone);
	}

	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return ret;
}

static int rcar_thermal_remove(struct platform_device *pdev)
{
	struct rcar_thermal_common *common = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct rcar_thermal_priv *priv;

	rcar_thermal_for_each_priv(priv, common) {
		if (rcar_has_irq_support(priv) ||
			rcar_gen3_has_irq_support(priv))
			rcar_thermal_irq_disable(priv);
		thermal_zone_device_unregister(priv->zone);
	}

	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return 0;
}

static struct platform_driver rcar_thermal_driver = {
	.driver	= {
		.name	= "rcar_thermal",
		.of_match_table = rcar_thermal_dt_ids,
	},
	.probe		= rcar_thermal_probe,
	.remove		= rcar_thermal_remove,
};
module_platform_driver(rcar_thermal_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("R-Car THS/TSC thermal sensor driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
