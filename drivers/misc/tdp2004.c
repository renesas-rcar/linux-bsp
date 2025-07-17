// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

static const struct of_device_id tdp2004_of_match[] = {
	{ .compatible = "ti,tdp2004" },
	{ }
};
MODULE_DEVICE_TABLE(of, tdp2004_of_match);

static int tdp2004_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct gpio_desc *enable_gpio;

	enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(enable_gpio))
		return dev_err_probe(dev, PTR_ERR(enable_gpio), "unable to acquire enable gpio\n");

	/* Disable test mode for all channels */
	return i2c_smbus_write_byte_data(client, 0x84, 4);
}

static void tdp2004_remove(struct i2c_client *client)
{
}

static struct i2c_driver tdp2004_driver = {
	.driver = {
		.name = "tdp2004",
		.of_match_table = tdp2004_of_match,
	},
	.probe = tdp2004_probe,
	.remove = tdp2004_remove,
};

static int __init tdp2004_init(void)
{
	return i2c_add_driver(&tdp2004_driver);
}
module_init(tdp2004_init);

static void __exit tdp2004_exit(void)
{
	i2c_del_driver(&tdp2004_driver);
}
module_exit(tdp2004_exit);

MODULE_DESCRIPTION("Driver for TI TDP2004");
MODULE_LICENSE("GPL");
