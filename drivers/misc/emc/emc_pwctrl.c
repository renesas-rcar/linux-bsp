#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <uapi/misc/emc_data.h>
#include <asm/delay.h>

#define I2C_MAX_IDX		(2)

static int enable = 0;
module_param(enable, int, S_IRUGO);
MODULE_PARM_DESC(enable, "Enable pwctrl in kernel");

struct emc_pwctrl_priv {
	struct device *dev;
	struct i2c_client *i2c_client[I2C_MAX_IDX];
	struct gpio_desc *enable_gpios;
	struct gpio_desc *reset_gpios;
};

int emc_pwctrl_write8(struct i2c_client *client, int flags, int slvaddr, int val)
{
	int ret;
	union i2c_smbus_data data;

	data.byte = val;
	ret = i2c_smbus_xfer(client->adapter, client->addr, client->flags | flags, I2C_SMBUS_WRITE, slvaddr ,I2C_SMBUS_BYTE_DATA, &data);
	if (ret) {
		pr_err("i2c_smbus_xfer err %d\n", ret);
		return -EIO;
	}

	return ret;
}

int emc_pwctrl_check_tps78412 (struct emc_pwctrl_priv *priv)
{
	struct i2c_client *client38 = priv->i2c_client[0];
	int reset, i;

	reset = gpiod_get_value(priv->reset_gpios);
	if (reset) {
		pr_info(" %s %d >> %pSR\n", __func__,__LINE__, __builtin_return_address(0));
		goto err;
	}

	gpiod_set_value(priv->enable_gpios, 1);

	i = 0;
	do {
		reset = gpiod_get_value(priv->reset_gpios);
		if (i%10) {
			pr_info(" %s %d >> %pSR\n", __func__,__LINE__, __builtin_return_address(0));
		}
		i++;
		udelay(10);
	} while(!reset);

	emc_set_exp_info(TPS78412_VOUT_STATUS_NVM, 0);
	emc_set_exp_info(TPS3703_RESET_CHECK_EXP_NVM, 0);
	return 0;
err:
	/* Power OFF */
	pr_info(" %s %d >> %pSR\n", __func__,__LINE__, __builtin_return_address(0));
	emc_set_exp_info(TPS78412_VOUT_STATUS_NVM, 1);
	emc_set_exp_info(TPS3703_RESET_CHECK_EXP_NVM, 1);
	emc_pwctrl_write8(client38, I2C_CLIENT_PEC, 0xFD, 0x03);
	return 0;
}

int emc_pwctrl_set_output_valtage (struct emc_pwctrl_priv *priv)
{
	struct i2c_client *client38 = priv->i2c_client[0];
	struct i2c_client *client39 = priv->i2c_client[1];

	pr_info(" %s %d >> %pSR :\n", __func__,__LINE__, __builtin_return_address(0));

	emc_pwctrl_write8(client39, 0, 0x05, 0x88);
	emc_pwctrl_write8(client39, I2C_CLIENT_PEC, 0x05, 0x88);

	emc_pwctrl_write8(client38, I2C_CLIENT_PEC, 0x27, 0x78);
	emc_pwctrl_write8(client38, I2C_CLIENT_PEC, 0x28, 0xB5);
	emc_pwctrl_write8(client38, I2C_CLIENT_PEC, 0x29, 0x38);
	emc_pwctrl_write8(client38, I2C_CLIENT_PEC, 0x2A, 0x6D);
	emc_pwctrl_write8(client38, I2C_CLIENT_PEC, 0x2B, 0xCA);
	emc_pwctrl_write8(client38, I2C_CLIENT_PEC, 0x2C, 0x51);
	emc_pwctrl_write8(client38, I2C_CLIENT_PEC, 0x2D, 0xCA);
	emc_pwctrl_write8(client39, I2C_CLIENT_PEC, 0x07, 0x53);
	emc_pwctrl_write8(client39, I2C_CLIENT_PEC, 0x09, 0x84);

	emc_pwctrl_write8(client38, I2C_CLIENT_PEC, 0xFA, 0x00);

	pr_info(" %s %d >> %pSR :\n", __func__,__LINE__, __builtin_return_address(0));

	return 0;
}

int emc_pwctrl_setup(struct emc_pwctrl_priv *priv)
{
	emc_pwctrl_check_tps78412(priv);
	emc_pwctrl_set_output_valtage(priv);
	return 0;
}

static int emc_pwctrl_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct emc_pwctrl_priv *priv;
	struct device *dev = &client->dev;
	struct i2c_client *i2c_dummy;
	struct gpio_desc *enable_gpios, *reset_gpios;
	int ret = 0;

	if (!enable) {
		pr_info(" %s %d >> %pSR : pwctrl is disable\n", __func__,__LINE__, __builtin_return_address(0));
		return 0;
	}

	pr_info(" %s %d >> %pSR : %s\n", __func__,__LINE__, __builtin_return_address(0), dev_name(dev));

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_dummy = i2c_new_dummy_device(client->adapter, 0x39);
	if (IS_ERR(i2c_dummy)) {
		return PTR_ERR(i2c_dummy);
	}

	enable_gpios =  devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(enable_gpios))
		return PTR_ERR(enable_gpios);


	reset_gpios =  devm_gpiod_get(dev, "reset", GPIOD_IN);
	if (IS_ERR(reset_gpios))
		return PTR_ERR(reset_gpios);

	priv->dev = dev;
	priv->i2c_client[0] = client;
	priv->i2c_client[1] = i2c_dummy;
	priv->enable_gpios = enable_gpios;
	priv->reset_gpios = reset_gpios;

	i2c_set_clientdata(client, priv);

	ret = emc_pwctrl_setup(priv);

	pr_info(" %s %d >> %pSR : %s ret %d\n", __func__,__LINE__, __builtin_return_address(0), dev_name(dev), ret);

	return ret;
}

static int emc_pwctrl_i2c_remove(struct i2c_client *client)
{
	struct emc_pwctrl_priv *priv = i2c_get_clientdata(client);
	struct device *dev = priv->dev;
	pr_info(" %s %d >> %pSR : %s\n", __func__,__LINE__, __builtin_return_address(0), dev_name(dev));

	return 0;
}

static const struct of_device_id emc_pwctrl_of_match[] = {
	{ .compatible = "dummy,emc-pwctrl"},
	{ }
};
MODULE_DEVICE_TABLE(of, emc_pwctrl_of_match);

static struct i2c_driver emc_pwctrl_i2c_driver = {
	.driver	= {
		.name		= "emc_pwctrl",
		.of_match_table	= of_match_ptr(emc_pwctrl_of_match),
	},
	.probe	= emc_pwctrl_i2c_probe,
	.remove	= emc_pwctrl_i2c_remove,
};
module_i2c_driver(emc_pwctrl_i2c_driver);

MODULE_LICENSE("GPLv2");
MODULE_DESCRIPTION("EMC Power control driver");
