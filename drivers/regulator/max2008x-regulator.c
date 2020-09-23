/*
 * gpio-regulator.c
 *
 * Copyright 2019 Andrey Gusakov <andrey.gusakov@cogentembedded.com>
 *
 * based on gpio-regulator.c
 *
 * Copyright 2011 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This is useful for systems with mixed controllable and
 * non-controllable regulators, as well as for allowing testing on
 * systems with no controllable regulators.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/iio/iio.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>

/* Register definitions */
#define REG_MASK	0x00
#define REG_CONFIG	0x01
#define REG_ID		0x02
#define REG_STAT1	0x03
/* 16 bit register: 0x04 + 0x05 */
#define REG_STAT2	0x04
/* ADC1..ADC4 */
#define REG_ADC(n)	(0x06 + ((n) & 0x03))

#define REG_CONFIG_ADC_MUX_CUR	0x00
#define REG_CONFIG_ADC_MUX_VOUT	0x40
#define REG_CONFIG_ADC_MUX_MISC	0x80
#define REG_CONFIG_ADC_MUX_MASK	0xc0

struct max2008x_regulator_data {
	int			id;
	const char		*name;
	struct regulator_init_data *init_data;
	struct device_node 	*of_node;
};

struct max2008x_data {
	struct regmap *regmap;
	struct max2008x_regulator_data  *regulators;
	struct iio_dev *iio_dev;

	unsigned char id;
	unsigned char rev;
	unsigned char num_regulators;

	unsigned char adc_mux;
};

static int max2008x_set_adc_mux(struct max2008x_data *max, int mux)
{
	int ret;
	unsigned int reg;

	mux = mux & REG_CONFIG_ADC_MUX_MASK;
	if (mux > REG_CONFIG_ADC_MUX_MISC)
		return -EINVAL;

	/* do we need this? or we can relay on regmap? */
	if (mux == max->adc_mux)
		return 0;

	ret = regmap_write_bits(max->regmap, REG_CONFIG,
		REG_CONFIG_ADC_MUX_MASK, mux);
	if (ret)
		return ret;

	max->adc_mux = mux;

	/* Read ADC1 register to clear ACC bit in STAT1 register */
	ret = regmap_read(max->regmap, REG_ADC(0), &reg);
	if (ret)
		return ret;

	return 0;
}

static int max2008x_read_adc(struct max2008x_data *max, int channel, int *value)
{
	int ret;
	int timeout = 100;
	unsigned int reg;

	/* shift 3:2 bits to 7:6 */
	ret = max2008x_set_adc_mux(max, channel << 4);
	if (ret)
		return ret;

	do {
		ret = regmap_read(max->regmap, REG_STAT1, &reg);
		if (ret)
			return ret;
	} while ((!(reg & BIT(4))) && (--timeout));

	if (timeout <= 0)
		return -ETIMEDOUT;

	ret = regmap_read(max->regmap, REG_ADC(channel & 0x03), &reg);
	if (ret)
		return ret;
	*value = reg;

	return 0;
}

static int max2008x_get_voltage(struct regulator_dev *dev)
{
	int ret;
	int value = 0;
	struct max2008x_data *max = rdev_get_drvdata(dev);

	/* voltage channels starts from 4 */
	ret = max2008x_read_adc(max, dev->desc->id + 4, &value);
	if (ret)
		return ret;

	/* in 70mV units */
	return (value * 70000);
}

static struct regulator_ops max2008x_ops = {
	.enable		= regulator_enable_regmap,
	.disable	= regulator_disable_regmap,
	.is_enabled	= regulator_is_enabled_regmap,
	.get_voltage	= max2008x_get_voltage,
};

static struct of_regulator_match max2008x_matches[] = {
	[0] = { .name = "SW0"},
	[1] = { .name = "SW1"},
	[2] = { .name = "SW2"},
	[3] = { .name = "SW3"},
};

static int of_get_max2008x_pdata(struct device *dev,
				 struct max2008x_data *pdata)
{
	int matched, i;
	struct device_node *np;
	struct max2008x_regulator_data *regulator;

	np = of_get_child_by_name(dev->of_node, "regulators");
	if (!np) {
		dev_err(dev, "missing 'regulators' subnode in DT\n");
		return -EINVAL;
	}

	matched = of_regulator_match(dev, np, max2008x_matches,
				     pdata->num_regulators);
	of_node_put(np);
	if (matched <= 0)
		return matched;

	pdata->regulators = devm_kzalloc(dev,
					 sizeof(struct max2008x_regulator_data) *
					 pdata->num_regulators, GFP_KERNEL);
	if (!pdata->regulators)
		return -ENOMEM;

	regulator = pdata->regulators;

	for (i = 0; i < pdata->num_regulators; i++) {
		regulator->id = i;
		regulator->name = max2008x_matches[i].name;
		regulator->init_data = max2008x_matches[i].init_data;
		regulator->of_node = max2008x_matches[i].of_node;
		regulator++;
	}

	return 0;
}

static struct max2008x_regulator_data *max2008x_get_regulator_data(
		int id, struct max2008x_data *data)
{
	int i;

	if (!data)
		return NULL;

	for (i = 0; i < data->num_regulators; i++) {
		if (data->regulators[i].id == id)
			return &data->regulators[i];
	}

	return NULL;
}

#define MAX2008X_REG(_name, _id, _supply)				\
	{								\
		.name			= _name,			\
		.supply_name		= _supply,			\
		.id			= _id,				\
		.type			= REGULATOR_VOLTAGE,		\
		.ops			= &max2008x_ops,		\
		.enable_reg		= REG_CONFIG,			\
		.enable_mask		= BIT(_id),			\
		.owner			= THIS_MODULE,			\
	}

static const struct regulator_desc max2008x_regulators[] = {
	MAX2008X_REG("SW0", 0, "out0"),
	MAX2008X_REG("SW1", 1, "out1"),
	MAX2008X_REG("SW2", 2, "out2"),
	MAX2008X_REG("SW3", 3, "out3"),
};

static const char *max2008x_devnames[] = {
	"max20089",
	"max20088",
	"max20087",
	"max20086"
};

static int max2008x_adc_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret;
	struct max2008x_data *max = iio_device_get_drvdata(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = max2008x_read_adc(max, chan->channel, val);
		iio_device_release_direct_mode(indio_dev);

		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_VOLTAGE) {
			/* Vout1..Vout4 and Vin */
			if (chan->channel <= 8)
				*val = 70;
			/* Vdd */
			else if (chan->channel == 9)
				*val = 25;
			/* Viset */
			else
				*val = 5;
		} else if (chan->type == IIO_CURRENT) {
			*val = 3;
			*val2 = 0;
		}

		return IIO_VAL_INT;
	}

	return 0;
}

#define MAX2008X_ADC_C_CHAN(_chan, _addr, _name) {		\
	.type = IIO_CURRENT,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE),	\
	.scan_index = _addr,					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = 8,					\
		.storagebits = 8,				\
		.endianness = IIO_CPU,				\
	},							\
	.datasheet_name = _name,				\
}

#define MAX2008X_ADC_V_CHAN(_chan, _addr, _name) {		\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE),	\
	.scan_index = _addr,					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = 8,					\
		.storagebits = 8,				\
		.endianness = IIO_CPU,				\
	},							\
	.datasheet_name = _name,				\
}

static const struct iio_chan_spec max2008x_adc_channels[] =
{
	MAX2008X_ADC_C_CHAN( 0,  0, "out0"),
	MAX2008X_ADC_C_CHAN( 1,  1, "out1"),
	MAX2008X_ADC_C_CHAN( 2,  2, "out2"),
	MAX2008X_ADC_C_CHAN( 3,  3, "out3"),
	MAX2008X_ADC_V_CHAN( 4,  4, "out0"),
	MAX2008X_ADC_V_CHAN( 5,  5, "out1"),
	MAX2008X_ADC_V_CHAN( 6,  6, "out2"),
	MAX2008X_ADC_V_CHAN( 7,  7, "out3"),
	MAX2008X_ADC_V_CHAN( 8,  8, "Vin"),
	MAX2008X_ADC_V_CHAN( 9,  9, "Vdd"),
	MAX2008X_ADC_V_CHAN(10, 10, "Viset"),
};

static const struct iio_info max2008x_adc_info = {
	.read_raw = max2008x_adc_read_raw,
};

#if defined(CONFIG_OF)
static const struct of_device_id max2008x_of_match[] = {
	{ .compatible = "maxim,max2008x" },
	{},
};
MODULE_DEVICE_TABLE(of, max2008x_of_match);
#endif


static const struct regmap_range max2008x_reg_ranges[] = {
	regmap_reg_range(REG_MASK, REG_ADC(3)),
};

static const struct regmap_range max2008x_reg_ro_ranges[] = {
	regmap_reg_range(REG_ID, REG_ADC(3)),
};

static const struct regmap_range max2008x_reg_volatile_ranges[] = {
	regmap_reg_range(REG_STAT1, REG_ADC(3)),
};

static const struct regmap_access_table max2008x_write_ranges_table = {
	.yes_ranges	= max2008x_reg_ranges,
	.n_yes_ranges	= ARRAY_SIZE(max2008x_reg_ranges),
	.no_ranges	= max2008x_reg_ro_ranges,
	.n_no_ranges	= ARRAY_SIZE(max2008x_reg_ro_ranges),
};

static const struct regmap_access_table max2008x_read_ranges_table = {
	.yes_ranges	= max2008x_reg_ranges,
	.n_yes_ranges	= ARRAY_SIZE(max2008x_reg_ranges),
};

static const struct regmap_access_table max2008x_volatile_ranges_table = {
	.yes_ranges	= max2008x_reg_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(max2008x_reg_volatile_ranges),
};

static const struct regmap_config max2008x_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= REG_ADC(3),
	.wr_table	= &max2008x_write_ranges_table,
	.rd_table	= &max2008x_read_ranges_table,
	.volatile_table	= &max2008x_volatile_ranges_table,
};

static int max2008x_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct max2008x_data *max;
	struct gpio_desc *enable_gpio;
	unsigned int reg;
	int i, ret;

	max = devm_kzalloc(&client->dev, sizeof(struct max2008x_data),
			       GFP_KERNEL);
	if (max == NULL)
		return -ENOMEM;

	enable_gpio = devm_gpiod_get_optional(&client->dev, "enable", GPIOD_OUT_LOW);

	/* regmap */
	max->regmap = devm_regmap_init_i2c(client, &max2008x_regmap_config);
	if (IS_ERR(max->regmap)) {
		ret = PTR_ERR(max->regmap);
		dev_err(&client->dev,
			"regmap allocation failed with err %d\n", ret);
		return ret;
	}
	i2c_set_clientdata(client, max);

	ret = regmap_read(max->regmap, REG_ID, &reg);
	if (ret)
		return ret;

	max->id = (reg >> 4) & 0x03;
	max->rev = reg & 0x0f;
	/* MAX20086 and MAX20087 have 4 outputs */
	max->num_regulators = ((max->id == 0x2) || (max->id == 0x3)) ? 4 : 2;

	/* Disable all */
	ret = regmap_write_bits(max->regmap, REG_CONFIG, BIT(max->num_regulators) - 1, 0x00);
	if (ret)
		return ret;

	/* to update mux on first access */
	max->adc_mux = 0xff;

	/* set autoconvertion mode for ADC */
	ret = regmap_write_bits(max->regmap, REG_CONFIG, BIT(5), BIT(5));
	if (ret)
		return ret;

	dev_info(&client->dev, "%s rev %d found (%d channels)\n",
		max2008x_devnames[max->id], max->rev, max->num_regulators);

	if (np) {
		ret = of_get_max2008x_pdata(&client->dev, max);
		if (ret) {
			dev_err(&client->dev,
				"dt parse error %d\n", ret);
			return ret;
		}
	}

	/* Finally register devices */
	for (i = 0; i < max->num_regulators; i++) {
		const struct regulator_desc *desc = &max2008x_regulators[i];
		struct regulator_config config = { };
		struct max2008x_regulator_data *rdata;
		struct regulator_dev *rdev;

		config.dev = dev;
		config.driver_data = max;
		config.regmap = max->regmap;

		rdata = max2008x_get_regulator_data(desc->id, max);
		if (rdata) {
			config.init_data = rdata->init_data;
			config.of_node = rdata->of_node;
		}

		rdev = devm_regulator_register(dev, desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "failed to register %s\n", desc->name);
			return PTR_ERR(rdev);
		}
	}

	/* IIO device */
	max->iio_dev = devm_iio_device_alloc(dev, 0);
	if (!max->iio_dev)
		return -ENOMEM;

	max->iio_dev->info = &max2008x_adc_info;
	max->iio_dev->dev.parent = dev;
	max->iio_dev->dev.of_node = dev->of_node;
	max->iio_dev->name = "max2008x-adc";
	max->iio_dev->modes = INDIO_DIRECT_MODE;

	max->iio_dev->channels = max2008x_adc_channels;
	/* only ASIL can measure voltages */
	if ((max->id == 0x00) || (max->id == 0x02))
		max->iio_dev->num_channels = ARRAY_SIZE(max2008x_adc_channels);
	else
		max->iio_dev->num_channels = 4;

	iio_device_set_drvdata(max->iio_dev, max);

	ret = iio_device_register(max->iio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to register IIO device: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, max);

	if (!IS_ERR(enable_gpio))
		gpiod_set_value_cansleep(enable_gpio, 1);

	return 0;
}

static const struct i2c_device_id max2008x_id[] = {
	{ "maxim,max2008x" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max2008x_id);

static struct i2c_driver max2008x_driver = {
	.driver		= {
		.name		= "max2008x",
		.of_match_table = of_match_ptr(max2008x_of_match),
	},
	.probe		= max2008x_probe,
	.id_table	= max2008x_id,
};

static int __init max2008x_init(void)
{
	return i2c_add_driver(&max2008x_driver);
}
subsys_initcall(max2008x_init);

static void __exit max2008x_exit(void)
{
	i2c_del_driver(&max2008x_driver);
}
module_exit(max2008x_exit);

MODULE_AUTHOR("Andrey Gusakov <andrey.gusakov@cogentembedded.com>");
MODULE_DESCRIPTION("max2008x Dual/Quad Camera Power Protector");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:max2008x");
