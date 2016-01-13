/*
 * drivers/gpu/drm/i2c/5p49v5923a.c
 *     This file is programmable clock generator driver.
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * This file is based on the drivers/clk/clk-cs2000-cp.c
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/module.h>

#define REF_CLK		1
#define CLK_MAX		5

#define INPUT_CLK	25000000

#define C5P49_FB_INT_DIV_REG1	0x17
#define C5P49_FB_INT_DIV_REG0	0x18

/* offset address*/
#define C5P49_DIV_FRAC_29_22	0x02
#define C5P49_DIV_FRAC_21_14	0x03
#define C5P49_DIV_FRAC_13_6	0x04
#define C5P49_DIV_FRAC_5_0	0x05
#define C5P49_DIV_INTEGER_11_4	0x0d
#define C5P49_DIV_INTEGER_3_0	0x0e

#define C5P49_CLK_OE_SHUTDOWN	0x68

#define hw_to_priv(_hw)		container_of(_hw, struct clk_5p49_priv, hw)
#define priv_to_client(priv)	(priv->client)
#define priv_to_dev(priv)	(&(priv_to_client(priv)->dev))

struct clk_5p49_priv {
	struct		clk_hw hw;
	struct		i2c_client *client;
	struct		clk *clk_out;
	unsigned long	index;
	unsigned long	clk_rate;
};

static const struct of_device_id clk_5p49_of_match[] = {
	{ .compatible = "idt,5p49v5923a", },
	{},
};
MODULE_DEVICE_TABLE(of, clk_5p49_of_match);

static const struct i2c_device_id clk_5p49_id[] = {
	{ "5p49v5923a",},
	{}
};
MODULE_DEVICE_TABLE(i2c, clk_5p49_id);

#define clk_5p49_read(priv, addr) \
	i2c_smbus_read_byte_data(priv_to_client(priv), \
	(addr + (0x10 * priv->index)))
#define clk_5p49_write(priv, addr, val) \
	i2c_smbus_write_byte_data(priv_to_client(priv), \
	(addr + (0x10 * priv->index)), val)

static int clk_5p49_set_rate(struct clk_hw *hw,
			     unsigned long rate, unsigned long parent_rate)
{
	return 0;
}

static void clk_5p49_power(struct clk_hw *hw, bool power)
{
	struct clk_5p49_priv *priv = hw_to_priv(hw);
	u8 reg;

	if (power) {
		reg = i2c_smbus_read_byte_data(priv->client,
					C5P49_CLK_OE_SHUTDOWN);
		reg |= (0x80 >> (priv->index - 1));
		i2c_smbus_write_byte_data(priv->client,
					C5P49_CLK_OE_SHUTDOWN, reg);
	} else {
		reg = i2c_smbus_read_byte_data(priv->client,
					C5P49_CLK_OE_SHUTDOWN);
		reg &= ~(0x80 >> (priv->index - 1));
		i2c_smbus_write_byte_data(priv->client,
					C5P49_CLK_OE_SHUTDOWN, reg);
	}
}

static int clk_5p49_enable(struct clk_hw *hw)
{
	clk_5p49_power(hw, true);

	return 0;
}

static void clk_5p49_disable(struct clk_hw *hw)
{
	clk_5p49_power(hw, false);
}

static unsigned long clk_5p49_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk_5p49_priv *priv = hw_to_priv(hw);

	return priv->clk_rate;
}

static int clk_5p49_div_calculation(struct clk_hw *hw, unsigned long rate)
{
	struct clk_5p49_priv *priv = hw_to_priv(hw);
	int integ_div, frac_div, div, vco_div, vco_clk;
	u32 shift_1kHz = 1000;
	u8 frac_0, frac_1, frac_2, frac_3;

	vco_div = ((i2c_smbus_read_byte_data(priv->client,
			C5P49_FB_INT_DIV_REG0) & 0xF0) >> 4)
			+ (i2c_smbus_read_byte_data(priv->client,
			C5P49_FB_INT_DIV_REG1) << 4);

	clk_5p49_power(hw, false);

	vco_clk = INPUT_CLK * vco_div / shift_1kHz;
	dev_dbg(&priv->client->dev, "vco clock:%d kHz\n", vco_clk);

	vco_clk = (vco_clk / 2);
	rate = rate / shift_1kHz;

	integ_div = (vco_clk / rate);
	div = ((vco_clk * shift_1kHz) / rate);
	frac_div = div - (integ_div * shift_1kHz);

	if (frac_div > 0x3fffffff)
		return -EINVAL;

	clk_5p49_write(priv, C5P49_DIV_INTEGER_11_4,
			((0x0ff0 & (u16)integ_div) >> 4));
	clk_5p49_write(priv, C5P49_DIV_INTEGER_3_0,
			((0x000f & (u16)integ_div) << 4));

	/* spread = 0.01% */
	frac_div = frac_div - ((div / (100 * 100 / 1)) / 2);
	frac_div = ((0x1000000 / shift_1kHz) * frac_div);
	dev_dbg(&priv->client->dev,
		"integer:0x%x, fraction:0x%x\n",
		integ_div, frac_div);

	frac_0 = (frac_div & 0x3fc00000) >> 22;
	frac_1 = (frac_div & 0x003fc000) >> 14;
	frac_2 = (frac_div & 0x00003fc0) >> 6;
	frac_3 = (frac_div & 0x0000003f) << 2;

	clk_5p49_write(priv, C5P49_DIV_FRAC_29_22, frac_0);
	clk_5p49_write(priv, C5P49_DIV_FRAC_21_14, frac_1);
	clk_5p49_write(priv, C5P49_DIV_FRAC_13_6,  frac_2);
	clk_5p49_write(priv, C5P49_DIV_FRAC_5_0,   frac_3);

	clk_5p49_power(hw, true);

	return 0;
}

static long clk_5p49_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	struct clk_5p49_priv *priv = hw_to_priv(hw);
	int ret;

	priv->clk_rate = 0;

	ret = clk_5p49_div_calculation(hw, rate);
	if (ret < 0)
		return ret;

	priv->clk_rate = rate;

	return 0;
}

static u8 clk_5p49_get_parent(struct clk_hw *hw)
{
	return 0;
}

static const struct clk_ops clk_5p49_ops = {
	.get_parent	= clk_5p49_get_parent,
	.set_rate	= clk_5p49_set_rate,
	.prepare	= clk_5p49_enable,
	.unprepare	= clk_5p49_disable,
	.recalc_rate	= clk_5p49_recalc_rate,
	.round_rate	= clk_5p49_round_rate,
};

static int clk_5p49_clk_register(struct clk_5p49_priv *priv,
				 struct device_node *np)
{
	struct clk_init_data init;
	struct clk *clk;
	const char *name;
	static const char *parent_names[REF_CLK];
	int ret = 0;

	parent_names[0]	= __clk_get_name(of_clk_get(np, 0));
	name = np->name;

	init.name		= name;
	init.ops		= &clk_5p49_ops;
	init.flags		= CLK_IS_BASIC | CLK_SET_RATE_PARENT;
	init.parent_names	= parent_names;
	init.num_parents	= ARRAY_SIZE(parent_names);

	priv->hw.init = &init;

	clk = clk_register(NULL, &priv->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = of_clk_add_provider(np, of_clk_src_simple_get, clk);
	if (ret < 0) {
		clk_unregister(clk);
		return ret;
	}

	priv->clk_out = clk;

	return 0;
}

static int clk_5p49_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct clk_5p49_priv *priv = NULL;
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node, *ch_np;
	int ret, i, ch = 1;	/* ch = 0 reserved.*/
	u32 probe_cnt = 0;

	for (i = ch; i < CLK_MAX; i++) {
		char name[20];

		sprintf(name, "5p49v5923a_clk%u", i);
		ch_np = of_get_child_by_name(np, name);
		if (!ch_np)
			continue;

		priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
		if (!priv)
			return -ENOMEM;

		priv->client = client;
		priv->index = i + 1;
		i2c_set_clientdata(client, priv);

		ret = clk_5p49_clk_register(priv, ch_np);
		if (ret < 0)
			return ret;
		probe_cnt++;
	}

	if (probe_cnt == 0) {
		dev_err(dev, "Device tree error.\n");
		return -EINVAL;
	}

	dev_info(dev, "Rev.0x%x, probed\n",
		i2c_smbus_read_byte_data(priv->client, 0x01));

	return 0;
}

static int clk_5p49_remove(struct i2c_client *client)
{
	struct clk_5p49_priv *priv = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;

	of_clk_del_provider(np);

	clk_unregister(priv->clk_out);

	return 0;
}

static struct i2c_driver clk_5p49_driver = {
	.driver = {
		.name = "5p49v5923a",
		.of_match_table = clk_5p49_of_match,
	},
	.probe		= clk_5p49_probe,
	.remove		= clk_5p49_remove,
	.id_table	= clk_5p49_id,
};

module_i2c_driver(clk_5p49_driver);

MODULE_DESCRIPTION("5p49v5923a programmable clock generator driver");
MODULE_AUTHOR("Koji Matsuoka <koji.matsuoka.xm@renesas.com>");
MODULE_LICENSE("GPL");
