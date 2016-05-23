/*
 * simple-card-core.c
 *
 * Copyright (c) 2016 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <sound/simple_card_core.h>

int asoc_simple_card_parse_daifmt(struct device *dev,
				  struct device_node *node,
				  struct device_node *codec,
				  char *prefix,
				  unsigned int *retfmt)
{
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	int prefix_len = prefix ? strlen(prefix) : 0;
	unsigned int daifmt;

	daifmt = snd_soc_of_parse_daifmt(node, prefix,
					 &bitclkmaster, &framemaster);
	daifmt &= ~SND_SOC_DAIFMT_MASTER_MASK;

	if (prefix_len && !bitclkmaster && !framemaster) {
		/*
		 * No dai-link level and master setting was not found from
		 * sound node level, revert back to legacy DT parsing and
		 * take the settings from codec node.
		 */
		dev_dbg(dev, "Revert to legacy daifmt parsing\n");

		daifmt = snd_soc_of_parse_daifmt(codec, NULL, NULL, NULL) |
			(daifmt & ~SND_SOC_DAIFMT_CLOCK_MASK);
	} else {
		if (codec == bitclkmaster)
			daifmt |= (codec == framemaster) ?
				SND_SOC_DAIFMT_CBM_CFM : SND_SOC_DAIFMT_CBM_CFS;
		else
			daifmt |= (codec == framemaster) ?
				SND_SOC_DAIFMT_CBS_CFM : SND_SOC_DAIFMT_CBS_CFS;
	}

	of_node_put(bitclkmaster);
	of_node_put(framemaster);

	*retfmt = daifmt;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_daifmt);

int asoc_simple_card_parse_tdm(struct device_node *port_np,
			       struct asoc_simple_dai *simple_dai)
{
	return snd_soc_of_parse_tdm_slot(port_np,
					 &simple_dai->tx_slot_mask,
					 &simple_dai->rx_slot_mask,
					 &simple_dai->slots,
					 &simple_dai->slot_width);
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_tdm);

int asoc_simple_card_parse_dailink_name(struct device *dev,
					struct snd_soc_dai_link *dai_link)
{
	char *name = NULL;
	int ret = -ENOMEM;

	if (dai_link->dynamic && dai_link->cpu_dai_name) {
		name = devm_kzalloc(dev,
				    strlen(dai_link->cpu_dai_name) + 4,
				    GFP_KERNEL);
		if (name)
			sprintf(name, "fe.%s", dai_link->cpu_dai_name);

	} else if (dai_link->no_pcm && dai_link->codec_dai_name) {
		name = devm_kzalloc(dev,
				    strlen(dai_link->codec_dai_name) + 4,
				    GFP_KERNEL);
		if (name)
			sprintf(name, "be.%s", dai_link->codec_dai_name);
	} else if (dai_link->cpu_dai_name && dai_link->codec_dai_name) {
		name = devm_kzalloc(dev,
				    strlen(dai_link->cpu_dai_name)   +
				    strlen(dai_link->codec_dai_name) + 2,
				    GFP_KERNEL);
		if (name) {
			sprintf(name, "%s-%s",
				dai_link->cpu_dai_name,
				dai_link->codec_dai_name);
		}
	}

	if (name) {
		ret = 0;

		dai_link->name =
			dai_link->stream_name = name;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_dailink_name);

int asoc_simple_card_parse_card_name(struct snd_soc_card *card,
				     char *prefix)
{
	char prop[128];
	int ret;

	snprintf(prop, sizeof(prop), "%sname", prefix);

	/* Parse the card name from DT */
	ret = snd_soc_of_parse_card_name(card, prop);
	if (ret < 0)
		return ret;

	if (!card->name)
		card->name = card->dai_link->name;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_card_name);

int asoc_simple_card_parse_card_prefix(struct snd_soc_card *card,
				       struct snd_soc_dai_link *dai_link,
				       struct snd_soc_codec_conf *codec_conf,
				       char *prefix)
{
	char prop[128];

	snprintf(prop, sizeof(prop), "%sprefix", prefix);

	snd_soc_of_parse_audio_prefix(card, codec_conf,
				      dai_link->codec_of_node,
				      prop);

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_card_prefix);

int asoc_simple_card_parse_card_route(struct snd_soc_card *card,
				      char *prefix)
{
	struct device_node *np = card->dev->of_node;
	char prop[128];
	int ret = 0;

	snprintf(prop, sizeof(prop), "%srouting", prefix);

	if (of_property_read_bool(np, prop))
		ret = snd_soc_of_parse_audio_routing(card, prop);

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_card_route);

int asoc_simple_card_parse_card_widgets(struct snd_soc_card *card,
					char *prefix)
{
	struct device_node *np = card->dev->of_node;
	char prop[128];
	int ret = 0;

	snprintf(prop, sizeof(prop), "%swidgets", prefix);

	if (of_property_read_bool(np, prop))
		ret = snd_soc_of_parse_audio_simple_widgets(card, prop);

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_card_widgets);

int asoc_simple_card_parse_clk(struct device_node *port_np,
			       struct device_node *endpoint_np,
			       struct asoc_simple_dai *simple_dai)
{
	struct clk *clk;
	u32 val;

	/*
	 * Parse dai->sysclk come from "clocks = <&xxx>"
	 * (if system has common clock)
	 *  or "system-clock-frequency = <xxx>"
	 *  or device's module clock.
	 */
	clk = of_clk_get(port_np, 0);
	if (!IS_ERR(clk)) {
		simple_dai->sysclk = clk_get_rate(clk);
		simple_dai->clk = clk;
	} else if (!of_property_read_u32(port_np, "system-clock-frequency", &val)) {
		simple_dai->sysclk = val;
	} else {
		clk = of_clk_get(endpoint_np, 0);
		if (!IS_ERR(clk))
			simple_dai->sysclk = clk_get_rate(clk);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_clk);

int asoc_simple_card_parse_endpoint(struct device_node *port_np,
				    struct device_node **endpoint_np,
				    const char **dai_name,
				    const char *list_name,
				    const char *cells_name,
				    int *is_single_link)
{
	struct of_phandle_args args;
	int ret;

	if (!port_np)
		return 0;

	/*
	 * Get node via "sound-dai = <&phandle port>"
	 * it will be used as xxx_of_node on soc_bind_dai_link()
	 */
	ret = of_parse_phandle_with_args(port_np,
					 list_name, cells_name, 0, &args);
	if (ret)
		return ret;

	/* Get dai->name */
	if (dai_name) {
		ret = snd_soc_of_get_dai_name(port_np, dai_name);
		if (ret < 0)
			return ret;
	}

	*endpoint_np = args.np;

	if (is_single_link)
		*is_single_link = !args.args_count;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_endpoint);

void asoc_simple_card_parse_dpcm(struct snd_soc_dai_link *dai_link,
				 int (*be_fixup)(struct snd_soc_pcm_runtime *rtd,
						 struct snd_pcm_hw_params *params))
{
	if (be_fixup) {
		/* FE is dummy */
		dai_link->cpu_of_node		= NULL;
		dai_link->cpu_dai_name		= "snd-soc-dummy-dai";
		dai_link->cpu_name		= "snd-soc-dummy";

		/* BE settings */
		dai_link->no_pcm		= 1;
		dai_link->be_hw_params_fixup	= be_fixup;
	} else {
		/* BE is dummy */
		dai_link->codec_of_node		= NULL;
		dai_link->codec_dai_name	= "snd-soc-dummy-dai";
		dai_link->codec_name		= "snd-soc-dummy";

		/* FE settings */
		dai_link->dynamic		= 1;
		dai_link->dpcm_merged_format	= 1;
	}

	dai_link->dpcm_playback		= 1;
	dai_link->dpcm_capture		= 1;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_dpcm);

int asoc_simple_card_init_jack(struct snd_soc_card *card,
			       struct asoc_simple_jack *sjack,
			       int is_hp, char *prefix)
{
	struct device *dev = card->dev;
	enum of_gpio_flags flags;
	char prop[128];
	char *pin_name;
	char *gpio_name;
	int mask;
	int det;

	sjack->gpio.gpio = -ENOENT;

	if (is_hp) {
		snprintf(prop, sizeof(prop), "%shp-det-gpio", prefix);
		pin_name	= "Headphones";
		gpio_name	= "Headphone detection";
		mask		= SND_JACK_HEADPHONE;
	} else {
		snprintf(prop, sizeof(prop), "%smic-det-gpio", prefix);
		pin_name	= "Mic Jack";
		gpio_name	= "Mic detection";
		mask		= SND_JACK_MICROPHONE;
	}

	det = of_get_named_gpio_flags(dev->of_node, prop, 0, &flags);
	if (det == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	if (gpio_is_valid(det)) {
		sjack->pin.pin		= pin_name;
		sjack->pin.mask		= mask;

		sjack->gpio.name	= gpio_name;
		sjack->gpio.report	= mask;
		sjack->gpio.gpio	= det;
		sjack->gpio.invert	= !!(flags & OF_GPIO_ACTIVE_LOW);
		sjack->gpio.debounce_time = 150;

		snd_soc_card_jack_new(card, pin_name, mask,
				      &sjack->jack,
				      &sjack->pin, 1);

		snd_soc_jack_add_gpios(&sjack->jack, 1,
				       &sjack->gpio);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_init_jack);

void asoc_simple_card_remove_jack(struct asoc_simple_jack *sjack)
{
	if (gpio_is_valid(sjack->gpio.gpio))
		snd_soc_jack_free_gpios(&sjack->jack, 1, &sjack->gpio);
}
EXPORT_SYMBOL_GPL(asoc_simple_card_remove_jack);

int asoc_simple_card_init_dai(struct snd_soc_dai *dai,
			      struct asoc_simple_dai *simple_dai)
{
	int ret;

	if (simple_dai->sysclk) {
		ret = snd_soc_dai_set_sysclk(dai, 0, simple_dai->sysclk, 0);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dai->dev, "simple-card: set_sysclk error\n");
			return ret;
		}
	}

	if (simple_dai->slots) {
		ret = snd_soc_dai_set_tdm_slot(dai,
					       simple_dai->tx_slot_mask,
					       simple_dai->rx_slot_mask,
					       simple_dai->slots,
					       simple_dai->slot_width);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dai->dev, "simple-card: set_tdm_slot error\n");
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_init_dai);

int asoc_simple_card_canonicalize_dailink(struct snd_soc_dai_link *dai_link)
{
	if (!dai_link->cpu_dai_name || !dai_link->codec_dai_name)
		return -EINVAL;

	/* Assumes platform == cpu */
	if (!dai_link->platform_of_node)
		dai_link->platform_of_node = dai_link->cpu_of_node;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_canonicalize_dailink);
