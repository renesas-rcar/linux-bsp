/*
 * CS2000  --  CIRRUS LOGIC Fractional-N Clock Synthesizer & Clock Multiplier
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
#include <linux/of_device.h>
#include <linux/module.h>

#define CH_MAX 4

#define DEVICE_ID	0x1
#define DEVICE_CTRL	0x2
#define DEVICE_CFG1	0x3
#define DEVICE_CFG2	0x4
#define GLOBAL_CFG	0x5
#define Ratio_Add(x, nth)	(6 + (x * 4) + (nth))
#define Ratio_Val(x, nth)	((x >> (24 - (8 * nth))) & 0xFF)
#define Val_Ratio(x, nth)	((x & 0xFF) << (24 - (8 * nth)))
#define FUNC_CFG1	0x16
#define FUNC_CFG2	0x17

/* DEVICE_CTRL */
#define PLL_UNLOCK	(1 << 7)

/* DEVICE_CFG1 */
#define RSEL(x)		(((x) & 0x3) << 3)
#define RSEL_MASK	RSEL(0x3)
#define ENDEV1		(0x1)

/* GLOBAL_CFG */
#define ENDEV2		(0x1)

#define CH_SIZE_ERR(ch)		((ch < 0) || (ch >= CH_MAX))
#define hw_to_priv(_hw)		container_of(_hw, struct cs2000_priv, hw)
#define priv_to_client(priv)	(priv->client)
#define priv_to_dev(priv)	(&(priv_to_client(priv)->dev))

#define CLK_IN	0
#define REF_CLK	1
#define CLK_MAX 2

struct cs2000_priv {
	struct clk_hw hw;
	struct i2c_client *client;
	struct clk *clk_in;
	struct clk *ref_clk;
	struct clk *clk_out;
};

static const struct of_device_id cs2000_of_match[] = {
	{ .compatible = "cirrus,cs2000-cp", },
	{},
};
MODULE_DEVICE_TABLE(of, cs2000_of_match);

static const struct i2c_device_id cs2000_id[] = {
	{ "cs2000-cp", },
	{}
};
MODULE_DEVICE_TABLE(i2c, cs2000_id);

#define cs2000_read(priv, addr) \
	i2c_smbus_read_byte_data(priv_to_client(priv), addr)
#define cs2000_write(priv, addr, val) \
	i2c_smbus_write_byte_data(priv_to_client(priv), addr, val)

static int cs2000_bset(struct cs2000_priv *priv, u8 addr, u8 mask, u8 val)
{
	s32 data;

	data = cs2000_read(priv, addr);
	if (data < 0)
		return data;

	data &= ~mask;
	data |= (val & mask);

	return cs2000_write(priv, addr, data);
}

static int cs2000_enable_dev_config(struct cs2000_priv *priv, bool enable)
{
	u32 val;
	int ret;

	val = enable ? ENDEV1 : 0;
	ret = cs2000_bset(priv, DEVICE_CFG1, ENDEV1, val);
	if (ret < 0)
		return ret;

	val = enable ? ENDEV2 : 0;
	ret = cs2000_bset(priv, GLOBAL_CFG,  ENDEV2, val);
	if (ret < 0)
		return ret;

	return 0;
}

static int cs2000_clk_in_bound_rate(struct cs2000_priv *priv,
				    u32 rate_in)
{
	u32 val;

	if (rate_in >= 32000000 &&
	    rate_in < 56000000)
		val = 0x0;
	else if (rate_in >= 16000000 &&
		 rate_in < 28000000)
		val = 0x1;
	else if (rate_in >= 8000000 &&
		 rate_in < 14000000)
		val = 0x2;
	else
		return -EINVAL;

	return cs2000_bset(priv, FUNC_CFG1, 0x3 << 3, val << 3);
}

static int cs2000_wait_pll_lock(struct cs2000_priv *priv)
{
	struct device *dev = priv_to_dev(priv);
	s32 val;
	unsigned int i;

	for (i = 0; i < 256; i++) {
		val = cs2000_read(priv, DEVICE_CTRL);
		if (val < 0)
			return val;
		if (!(val & PLL_UNLOCK))
			return 0;
		udelay(1);
	}

	dev_err(dev, "pll lock failed\n");

	return -EIO;
}

static int cs2000_clk_out_enable(struct cs2000_priv *priv, bool enable)
{
	u32 val = enable ? 0 : 0x3;

	/* enable both AUX_OUT, CLK_OUT */
	return cs2000_write(priv, DEVICE_CTRL, val);
}

static u32 cs2000_rate_to_ratio(u32 rate_in, u32 rate_out)
{
	u64 ratio;

	/*
	 * ratio = rate_out / rate_in * 2^20
	 *
	 * To avoid over flow, rate_out is u64
	 * The result should be u32
	 */
	ratio = (u64)rate_out << 20;
	do_div(ratio, rate_in);

	return (u32)ratio;
}

static unsigned long cs2000_ratio_to_rate(u32 ratio, u32 rate_in)
{
	u64 rate_out;

	/*
	 * ratio = rate_out / rate_in * 2^20
	 *
	 * To avoid over flow, rate_out is u64
	 * The result should be u32
	 */

	rate_out = (u64)ratio * rate_in;
	return (unsigned long)(rate_out >> 20);
}

static int cs2000_ratio_set(struct cs2000_priv *priv,
			    int ch, u32 rate_in, u32 rate_out)
{
	u32 val;
	unsigned int i;
	int ret;

	if (CH_SIZE_ERR(ch))
		return -EINVAL;

	val = cs2000_rate_to_ratio(rate_in, rate_out);
	for (i = 0; i < 4; i++) {
		ret = cs2000_write(priv,
				   Ratio_Add(ch, i),
				   Ratio_Val(val, i));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static u32 cs2000_ratio_get(struct cs2000_priv *priv, int ch)
{
	u32 tmp, val;
	unsigned int i;

	val = 0;
	for (i = 0; i < 4; i++) {
		tmp = cs2000_read(priv,
				  Ratio_Add(ch, i));
		if (tmp < 0)
			return 0;

		val |= Val_Ratio(tmp, i);
	}

	return val;
}

static int cs2000_ratio_select(struct cs2000_priv *priv, int ch)
{
	int ret;

	if (CH_SIZE_ERR(ch))
		return -EINVAL;

	/*
	 * FIXME
	 *
	 * this driver supports static ratio mode only
	 * at this point
	 */
	ret = cs2000_bset(priv, DEVICE_CFG1, RSEL_MASK, RSEL(ch));
	if (ret < 0)
		return ret;

	ret = cs2000_write(priv, DEVICE_CFG2, 0x0);
	if (ret < 0)
		return ret;

	return 0;
}

static unsigned long cs2000_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct cs2000_priv *priv = hw_to_priv(hw);
	int ch = 0; /* it uses ch0 only at this point */
	u32 ratio;

	ratio = cs2000_ratio_get(priv, ch);

	return cs2000_ratio_to_rate(ratio, parent_rate);
}

static long cs2000_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *parent_rate)
{
	u32 ratio;

	ratio = cs2000_rate_to_ratio(*parent_rate, rate);

	return cs2000_ratio_to_rate(ratio, *parent_rate);
}

static int __cs2000_set_rate(struct cs2000_priv *priv, int ch,
			     unsigned long rate, unsigned long parent_rate)

{
	int ret;

	ret = cs2000_clk_in_bound_rate(priv, parent_rate);
	if (ret < 0)
		return ret;

	ret = cs2000_ratio_set(priv, ch, parent_rate, rate);
	if (ret < 0)
		return ret;

	ret = cs2000_ratio_select(priv, ch);
	if (ret < 0)
		return ret;

	return 0;
}

static int cs2000_set_rate(struct clk_hw *hw,
			   unsigned long rate, unsigned long parent_rate)
{
	struct cs2000_priv *priv = hw_to_priv(hw);
	int ch = 0; /* it uses ch0 only at this point */

	return __cs2000_set_rate(priv, ch, rate, parent_rate);
}

static int __cs2000_enable(struct cs2000_priv *priv)
{
	int ret;

	ret = cs2000_enable_dev_config(priv, true);
	if (ret < 0)
		return ret;

	ret = cs2000_clk_out_enable(priv, true);
	if (ret < 0)
		return ret;

	ret = cs2000_wait_pll_lock(priv);
	if (ret < 0)
		return ret;

	return 0;
}

static int cs2000_enable(struct clk_hw *hw)
{
	struct cs2000_priv *priv = hw_to_priv(hw);

	return __cs2000_enable(priv);
}

static void cs2000_disable(struct clk_hw *hw)
{
	struct cs2000_priv *priv = hw_to_priv(hw);

	cs2000_enable_dev_config(priv, false);

	cs2000_clk_out_enable(priv, false);
}

static u8 cs2000_get_parent(struct clk_hw *hw)
{
	/* always return REF_CLK */
	return REF_CLK;
}

static const struct clk_ops cs2000_ops = {
	.get_parent	= cs2000_get_parent,
	.recalc_rate	= cs2000_recalc_rate,
	.round_rate	= cs2000_round_rate,
	.set_rate	= cs2000_set_rate,
	.enable		= cs2000_enable,
	.disable	= cs2000_disable,
};

static int cs2000_clk_get(struct cs2000_priv *priv)
{
	struct i2c_client *client = priv_to_client(priv);
	struct device *dev = &client->dev;
	struct clk *clk_in, *ref_clk;

	clk_in = devm_clk_get(dev, "clk_in");
	/* not yet provided */
	if (IS_ERR(clk_in))
		return -EPROBE_DEFER;

	ref_clk = devm_clk_get(dev, "ref_clk");
	/* not yet provided */
	if (IS_ERR(ref_clk))
		return -EPROBE_DEFER;

	priv->clk_in	= clk_in;
	priv->ref_clk	= ref_clk;

	return 0;
}

static int cs2000_clk_register(struct cs2000_priv *priv)
{
	struct device *dev = priv_to_dev(priv);
	struct device_node *np = dev->of_node;
	struct clk_init_data init;
	const char *name = np->name;
	struct clk *clk;
	static const char *parent_names[CLK_MAX];
	int ret;

	of_property_read_string(np, "clock-output-names", &name);

	parent_names[CLK_IN]	= __clk_get_name(priv->clk_in);
	parent_names[REF_CLK]	= __clk_get_name(priv->ref_clk);

	init.name		= name;
	init.ops		= &cs2000_ops;
	init.flags		= CLK_IS_BASIC | CLK_SET_RATE_GATE;
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

static int cs2000_clk_init(struct cs2000_priv *priv)
{
	struct device *dev = priv_to_dev(priv);
	struct device_node *np = dev->of_node;
	u32 rate;
	int ch = 0; /* it uses ch0 only at this point */
	int ret;

	if (of_property_read_u32(np, "clock-frequency", &rate))
		return 0;

	ret = __cs2000_set_rate(priv, ch, rate, clk_get_rate(priv->ref_clk));
	if (ret < 0)
		return ret;

	ret = __cs2000_enable(priv);
	if (ret < 0)
		return ret;

	return 0;
}

static int cs2000_version_print(struct cs2000_priv *priv)
{
	struct i2c_client *client = priv_to_client(priv);
	struct device *dev = &client->dev;
	s32 val = cs2000_read(priv, DEVICE_ID);
	const char *revision;

	if (val < 0)
		return val;

	/* CS2000 should be 0x0 */
	if (0 != (val >> 3))
		return -EIO;

	switch (val & 0x7) {
	case 0x4:
		revision = "B2 / B3";
		break;
	case 0x6:
		revision = "C1";
		break;
	default:
		return -EIO;
	}

	dev_info(dev, "revision - %s\n", revision);

	return 0;
}

static int cs2000_remove(struct i2c_client *client)
{
	struct cs2000_priv *priv = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;

	of_clk_del_provider(np);

	clk_unregister(priv->clk_out);

	return 0;
}

static int cs2000_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct cs2000_priv *priv;
	struct device *dev = &client->dev;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	i2c_set_clientdata(client, priv);

	ret = cs2000_clk_get(priv);
	if (ret < 0)
		return ret;

	ret = cs2000_clk_register(priv);
	if (ret < 0)
		return ret;

	ret = cs2000_clk_init(priv);
	if (ret < 0)
		return ret;

	ret = cs2000_version_print(priv);
	if (ret < 0)
		goto probe_err;

	return 0;

probe_err:
	cs2000_remove(client);

	return ret;
}

static struct i2c_driver cs2000_driver = {
	.driver = {
		.name = "cs2000-cp",
		.of_match_table = cs2000_of_match,
	},
	.probe		= cs2000_probe,
	.remove		= cs2000_remove,
	.id_table	= cs2000_id,
};

module_i2c_driver(cs2000_driver);

MODULE_DESCRIPTION("CS2000-CP driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_LICENSE("GPL v2");
