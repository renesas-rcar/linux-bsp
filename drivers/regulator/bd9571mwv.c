/*
 * Power Management IC for BD9571MWV-M.
 *
 * Copyright (C) 2015 Renesas Electronics Corporation.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct bd9571mwv {
	struct regulator_dev *rdev;
	struct regmap *regmap;
};

static const struct regmap_config bd9571mwv_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static struct regulator_ops bd9571mwv_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.list_voltage = regulator_list_voltage_linear,
};

/* Default limits measured in millivolts and milliamps */
#define BD9571MWV_MIN_MV		600
#define BD9571MWV_MAX_MV		1100
#define BD9571MWV_STEP_MV		10
#define BD9571MWV_SLEWRATE		10000

/* Define Register */
#define BD9571_DVFS_SETVID		0x54
#define BD9571_DVFS_SETVID_MASK		0x7F

static const struct regulator_desc bd9571mwv_reg = {
	.name = "BD9571MWV",
	.id = 0,
	.ops = &bd9571mwv_ops,
	.type = REGULATOR_VOLTAGE,
	.n_voltages = BD9571MWV_MAX_MV / BD9571MWV_STEP_MV + 1,
	.min_uV = BD9571MWV_MIN_MV * 1000,
	.uV_step = BD9571MWV_STEP_MV * 1000,
	.ramp_delay = BD9571MWV_SLEWRATE,
	.vsel_reg = BD9571_DVFS_SETVID,
	.vsel_mask = BD9571_DVFS_SETVID_MASK,
	.linear_min_sel = BD9571MWV_MIN_MV / BD9571MWV_STEP_MV,
	.owner = THIS_MODULE,
};

/*
 * I2C driver interface functions
 */
static int bd9571mwv_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct bd9571mwv *chip;
	struct device *dev = &i2c->dev;
	struct regulator_dev *rdev = NULL;
	struct regulator_config config = { };
	int error;

	chip = devm_kzalloc(&i2c->dev, sizeof(struct bd9571mwv), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->regmap = devm_regmap_init_i2c(i2c, &bd9571mwv_regmap_config);
	if (IS_ERR(chip->regmap)) {
		error = PTR_ERR(chip->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	config.dev = &i2c->dev;
	config.init_data = of_get_regulator_init_data(dev,
				dev->of_node, &bd9571mwv_reg);
	config.driver_data = chip;
	config.regmap = chip->regmap;
	config.of_node = dev->of_node;

	rdev = devm_regulator_register(&i2c->dev, &bd9571mwv_reg, &config);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "Failed to register BD9571MWV\n");
		return PTR_ERR(rdev);
	}

	chip->rdev = rdev;

	i2c_set_clientdata(i2c, chip);

	dev_info(dev, "bd9571mwv probed\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id bd9571mwv_dt_ids[] = {
	{ .compatible = "rohm,bd9571mwv" },
	{},
};
MODULE_DEVICE_TABLE(of, bd9571mwv_dt_ids);
#endif

static const struct i2c_device_id bd9571mwv_i2c_id[] = {
	{"bd9571mwv", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, bd9571mwv_i2c_id);

static struct i2c_driver bd9571mwv_regulator_driver = {
	.driver = {
		.name = "bd9571mwv",
		.of_match_table	= of_match_ptr(bd9571mwv_dt_ids),
	},
	.probe = bd9571mwv_i2c_probe,
	.id_table = bd9571mwv_i2c_id,
};

module_i2c_driver(bd9571mwv_regulator_driver);

MODULE_AUTHOR("Keita Kobayashi <keita.kobayashi.ym@renesas.com");
MODULE_DESCRIPTION("Power Management IC for BD9571MWV-M");
MODULE_LICENSE("GPL v2");
