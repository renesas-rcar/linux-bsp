/*
 * simple_card_core.h
 *
 * Copyright (c) 2016 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __SIMPLE_CARD_CORE_H
#define __SIMPLE_CARD_CORE_H

#include <sound/soc.h>
#include <sound/jack.h>

struct asoc_simple_jack {
	struct snd_soc_jack jack;
	struct snd_soc_jack_pin pin;
	struct snd_soc_jack_gpio gpio;
};

struct asoc_simple_dai {
	const char *name;
	unsigned int sysclk;
	int slots;
	int slot_width;
	unsigned int tx_slot_mask;
	unsigned int rx_slot_mask;
	struct clk *clk;
};

int asoc_simple_card_parse_daifmt(struct device *dev,
				  struct device_node *node,
				  struct device_node *codec,
				  char *prefix,
				  unsigned int *retfmt);
int asoc_simple_card_parse_tdm(struct device_node *port_np,
			       struct asoc_simple_dai *simple_dai);
int asoc_simple_card_parse_dailink_name(struct device *dev,
					struct snd_soc_dai_link *dai_link);
int asoc_simple_card_parse_card_name(struct snd_soc_card *card,
				     char *prefix);
int asoc_simple_card_parse_card_prefix(struct snd_soc_card *card,
				       struct snd_soc_dai_link *dai_link,
				       struct snd_soc_codec_conf *codec_conf,
				       char *prefix);
int asoc_simple_card_parse_card_route(struct snd_soc_card *card,
				      char *prefix);
int asoc_simple_card_parse_card_widgets(struct snd_soc_card *card,
					char *prefix);

#define asoc_simple_card_parse_clk_cpu(port_np, dai_link, simple_dai)\
	asoc_simple_card_parse_clk(port_np, dai_link->cpu_of_node, simple_dai)
#define asoc_simple_card_parse_clk_codec(port_np, dai_link, simple_dai)  \
	asoc_simple_card_parse_clk(port_np, dai_link->codec_of_node, simple_dai)
int asoc_simple_card_parse_clk(struct device_node *port_np,
			       struct device_node *endpoint_np,
			       struct asoc_simple_dai *simple_dai);

#define asoc_simple_card_parse_cpu(port_np, dai_link,				\
				   list_name, cells_name, is_single_link)	\
	asoc_simple_card_parse_endpoint(port_np, &dai_link->cpu_of_node,	\
		&dai_link->cpu_dai_name, list_name, cells_name, is_single_link)
#define asoc_simple_card_parse_codec(port_np, dai_link,				\
				     list_name, cells_name)			\
	asoc_simple_card_parse_endpoint(port_np, &dai_link->codec_of_node,	\
		&dai_link->codec_dai_name, list_name, cells_name, NULL)
#define asoc_simple_card_parse_platform(port_np, dai_link,			\
					list_name, cells_name)			\
	asoc_simple_card_parse_endpoint(port_np, &dai_link->platform_of_node,	\
		NULL, list_name, cells_name, NULL)
int asoc_simple_card_parse_endpoint(struct device_node *port_np,
				  struct device_node **endpoint_np,
				  const char **dai_name,
				  const char *list_name,
				  const char *cells_name,
				  int *is_single_links);

#define asoc_simple_card_parse_dpcm_fe(dai_link)	\
	asoc_simple_card_parse_dpcm(dai_link, NULL)
#define asoc_simple_card_parse_dpcm_be(dai_link, fixup)	\
	asoc_simple_card_parse_dpcm(dai_link, fixup)
void asoc_simple_card_parse_dpcm(struct snd_soc_dai_link *dai_link,
			int (*be_fixup)(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params));

#define asoc_simple_card_init_hp(card, sjack, prefix)		\
	asoc_simple_card_init_jack(card, sjack, 1, prefix)
#define asoc_simple_card_init_mic(card, sjack, prefix)		\
	asoc_simple_card_init_jack(card, sjack, 0, prefix)
int asoc_simple_card_init_jack(struct snd_soc_card *card,
			       struct asoc_simple_jack *sjack,
			       int is_hp, char *prefix);

void asoc_simple_card_remove_jack(struct asoc_simple_jack *sjack);

#endif /* __SIMPLE_CARD_CORE_H */
