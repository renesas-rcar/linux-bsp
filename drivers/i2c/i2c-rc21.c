// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Renesas RC21008a Clock Generator
 */

#include <linux/clk-provider.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

struct rc21_driver_data {
	struct i2c_client	*client;
	struct regmap		*regmap;
	u8			addr_byte;
};

static int rc21_regmap_i2c_write(void *context,
				 unsigned int reg, unsigned int val)
{
	struct i2c_client *i2c = context;
	struct rc21_driver_data *rc21 = i2c_get_clientdata(i2c);
	u8 data[3];
	int count, ret;

	if (rc21->addr_byte == 2) {
		data[0] = reg >> 8;
		data[1] = reg & 0xff;
		data[2] = val & 0xff;
		count	= 3;
	} else {
		data[0] = reg & 0xff;
		data[1] = val & 0xff;
		count	= 2;
	}

	ret = i2c_master_send(i2c, data, count);
	if (ret == count)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int rc21_init(struct device *dev)
{
	struct rc21_driver_data *rc21 = dev_get_drvdata(dev);
	int ret = 0;

	rc21->addr_byte = 1;
	ret = regmap_write(rc21->regmap, 0x26, 0x5);
	if (ret < 0)
		return ret;

	rc21->addr_byte = 2;
	ret = regmap_write(rc21->regmap, 0x254, 0x1e);
	if (ret < 0)
		return ret;

	ret = regmap_write(rc21->regmap, 0x258, 0x1e);
	if (ret < 0)
		return ret;

	ret = regmap_write(rc21->regmap, 0x0026, 0x1);
	if (ret < 0)
		return ret;

	rc21->addr_byte = 1;

	return 0;
}

static int rc21_probe(struct i2c_client *client)
{
	struct rc21_driver_data *rc21;
	int ret = 0;

	static const struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 912,
		.reg_write = rc21_regmap_i2c_write,
	};

	rc21 = devm_kzalloc(&client->dev, sizeof(*rc21), GFP_KERNEL);
	if (!rc21)
		return -ENOMEM;

	i2c_set_clientdata(client, rc21);
	rc21->client = client;

	rc21->regmap = devm_regmap_init(&client->dev, NULL,
					client, &config);
	if (IS_ERR(rc21->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(rc21->regmap),
				     "Failed to allocate register map\n");

	ret = rc21_init(&client->dev);

	return ret;
}

static int __maybe_unused rc21_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused rc21_resume(struct device *dev)
{
	int ret = 0;

	ret = rc21_init(dev);

	return ret;
}

static const struct of_device_id clk_rc21_of_match[] = {
	{ .compatible = "renesas,rc21008a", },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_rc21_of_match);

static const struct dev_pm_ops rc21_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(rc21_suspend, rc21_resume)
};

static struct i2c_driver rc21_driver = {
	.driver = {
		.name = "i2c-rc21",
		.pm	= &rc21_pm_ops,
		.of_match_table = clk_rc21_of_match,
	},
	.probe_new	= rc21_probe,
};
module_i2c_driver(rc21_driver);

MODULE_AUTHOR("Cong Dang <cong.dang.xn@renesas.com>");
MODULE_DESCRIPTION("Renesas RC21008a clock generator driver");
MODULE_LICENSE("GPL");
