/*
 * simple-card-core.c
 *
 * Copyright (c) 2016 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/of.h>
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
