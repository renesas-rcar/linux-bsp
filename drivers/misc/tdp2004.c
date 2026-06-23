// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

struct tdp2004 {
	struct gpio_desc *enable_gpio;
};

static int tdp2004_hw_init(struct i2c_client *client)
{
	/* Disable test mode for all channels */
	return i2c_smbus_write_byte_data(client, 0x84, 4);
}

static int tdp2004_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tdp2004 *tdp;

	tdp = devm_kzalloc(dev, sizeof(*tdp), GFP_KERNEL);
	if (!tdp)
		return -ENOMEM;

	tdp->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(tdp->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(tdp->enable_gpio),
				     "unable to acquire enable gpio\n");

	i2c_set_clientdata(client, tdp);

	return tdp2004_hw_init(client);
}

static void tdp2004_remove(struct i2c_client *client)
{
}

static int tdp2004_suspend(struct device *dev)
{
	struct tdp2004 *tdp = dev_get_drvdata(dev);

	gpiod_set_value_cansleep(tdp->enable_gpio, 0);

	return 0;
}

static int tdp2004_resume(struct device *dev)
{
	struct tdp2004 *tdp = dev_get_drvdata(dev);

	gpiod_set_value_cansleep(tdp->enable_gpio, 1);

	return tdp2004_hw_init(to_i2c_client(dev));
}

static DEFINE_SIMPLE_DEV_PM_OPS(tdp2004_pm_ops, tdp2004_suspend, tdp2004_resume);

static const struct of_device_id tdp2004_of_match[] = {
	{ .compatible = "ti,tdp2004" },
	{ }
};
MODULE_DEVICE_TABLE(of, tdp2004_of_match);

static struct i2c_driver tdp2004_driver = {
	.driver = {
		.name = "tdp2004",
		.of_match_table = tdp2004_of_match,
		.pm = pm_sleep_ptr(&tdp2004_pm_ops),
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
