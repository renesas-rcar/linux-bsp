// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCM3168A codec driver
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 *
 * Author: Damien Horsley <Damien.Horsley@imgtec.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "pcm3168a.h"

#define PCM3168A_FORMATS (/*SNDRV_PCM_FMTBIT_S16_LE | */\
			 SNDRV_PCM_FMTBIT_S24_3LE | \
			 SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_S32_LE)

#define PCM3168A_FMT_I2S		0x0
#define PCM3168A_FMT_LEFT_J		0x1
#define PCM3168A_FMT_RIGHT_J		0x2
#define PCM3168A_FMT_RIGHT_J_16		0x3
#define PCM3168A_FMT_DSP_A		0x4
#define PCM3168A_FMT_DSP_B		0x5
#define PCM3168A_FMT_I2S_TDM		0x6
#define PCM3168A_FMT_LEFT_J_TDM		0x7
/* High speed */
#define PCM3168A_FMT_I2S_TDMHS		0x8
#define PCM3168A_FMT_LEFT_J_TDMHS	0x9

#define PCM3168A_NUM_SUPPLIES 6
static const char *const pcm3168a_supply_names[PCM3168A_NUM_SUPPLIES] = {
	"VDD1",
	"VDD2",
	"VCCAD1",
	"VCCAD2",
	"VCCDA1",
	"VCCDA2"
};

#define PCM3168A_DAI_DAC		0
#define PCM3168A_DAI_ADC		1

#define TDM_MODE_NONE	0
#define TDM_MODE_NORM	1
#define TDM_MODE_HS	2

struct pcm3168a_priv {
	struct regulator_bulk_data supplies[PCM3168A_NUM_SUPPLIES];
	struct regmap *regmap;
	struct clk *scki;
	bool master_mode;
	unsigned int tdm;
	unsigned int slots;
	unsigned int slot_width;
	unsigned long sysclk;
	unsigned int adc_fmt;
	unsigned int dac_fmt;
};

static const char *const pcm3168a_roll_off[] = { "Sharp", "Slow" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_d1_roll_off, PCM3168A_DAC_OP_FLT,
		PCM3168A_DAC_FLT_SHIFT, pcm3168a_roll_off);
static SOC_ENUM_SINGLE_DECL(pcm3168a_d2_roll_off, PCM3168A_DAC_OP_FLT,
		PCM3168A_DAC_FLT_SHIFT + 1, pcm3168a_roll_off);
static SOC_ENUM_SINGLE_DECL(pcm3168a_d3_roll_off, PCM3168A_DAC_OP_FLT,
		PCM3168A_DAC_FLT_SHIFT + 2, pcm3168a_roll_off);
static SOC_ENUM_SINGLE_DECL(pcm3168a_d4_roll_off, PCM3168A_DAC_OP_FLT,
		PCM3168A_DAC_FLT_SHIFT + 3, pcm3168a_roll_off);

static const char *const pcm3168a_volume_type[] = {
		"Individual", "Master + Individual" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_dac_volume_type, PCM3168A_DAC_ATT_DEMP_ZF,
		PCM3168A_DAC_ATMDDA_SHIFT, pcm3168a_volume_type);

static const char *const pcm3168a_att_speed_mult[] = { "2048", "4096" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_dac_att_mult, PCM3168A_DAC_ATT_DEMP_ZF,
		PCM3168A_DAC_ATSPDA_SHIFT, pcm3168a_att_speed_mult);

static const char *const pcm3168a_demp[] = {
		"Disabled", "48khz", "44.1khz", "32khz" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_dac_demp, PCM3168A_DAC_ATT_DEMP_ZF,
		PCM3168A_DAC_DEMP_SHIFT, pcm3168a_demp);

static const char *const pcm3168a_zf_func[] = {
		"DAC 1/2/3/4 AND", "DAC 1/2/3/4 OR", "DAC 1/2/3 AND",
		"DAC 1/2/3 OR", "DAC 4 AND", "DAC 4 OR" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_dac_zf_func, PCM3168A_DAC_ATT_DEMP_ZF,
		PCM3168A_DAC_AZRO_SHIFT, pcm3168a_zf_func);

static const char *const pcm3168a_pol[] = { "Active High", "Active Low" };

static SOC_ENUM_SINGLE_DECL(pcm3168a_dac_zf_pol, PCM3168A_DAC_ATT_DEMP_ZF,
		PCM3168A_DAC_ATSPDA_SHIFT, pcm3168a_pol);

static const char *const pcm3168a_con[] = { "Differential", "Single-Ended" };

static SOC_ENUM_DOUBLE_DECL(pcm3168a_adc1_con, PCM3168A_ADC_SEAD,
				0, 1, pcm3168a_con);
static SOC_ENUM_DOUBLE_DECL(pcm3168a_adc2_con, PCM3168A_ADC_SEAD,
				2, 3, pcm3168a_con);
static SOC_ENUM_DOUBLE_DECL(pcm3168a_adc3_con, PCM3168A_ADC_SEAD,
				4, 5, pcm3168a_con);

static SOC_ENUM_SINGLE_DECL(pcm3168a_adc_volume_type, PCM3168A_ADC_ATT_OVF,
		PCM3168A_ADC_ATMDAD_SHIFT, pcm3168a_volume_type);

static SOC_ENUM_SINGLE_DECL(pcm3168a_adc_att_mult, PCM3168A_ADC_ATT_OVF,
		PCM3168A_ADC_ATSPAD_SHIFT, pcm3168a_att_speed_mult);

static SOC_ENUM_SINGLE_DECL(pcm3168a_adc_ov_pol, PCM3168A_ADC_ATT_OVF,
		PCM3168A_ADC_OVFP_SHIFT, pcm3168a_pol);

/* -100db to 0db, register values 0-54 cause mute */
static const DECLARE_TLV_DB_SCALE(pcm3168a_dac_tlv, -10050, 50, 1);

/* -100db to 20db, register values 0-14 cause mute */
static const DECLARE_TLV_DB_SCALE(pcm3168a_adc_tlv, -10050, 50, 1);

static const struct snd_kcontrol_new pcm3168a_snd_controls[] = {
	SOC_SINGLE("DAC Power-Save Switch", PCM3168A_DAC_PWR_MST_FMT,
			PCM3168A_DAC_PSMDA_SHIFT, 1, 1),
	SOC_ENUM("DAC1 Digital Filter roll-off", pcm3168a_d1_roll_off),
	SOC_ENUM("DAC2 Digital Filter roll-off", pcm3168a_d2_roll_off),
	SOC_ENUM("DAC3 Digital Filter roll-off", pcm3168a_d3_roll_off),
	SOC_ENUM("DAC4 Digital Filter roll-off", pcm3168a_d4_roll_off),
	SOC_DOUBLE("DAC1 Invert Switch", PCM3168A_DAC_INV, 0, 1, 1, 0),
	SOC_DOUBLE("DAC2 Invert Switch", PCM3168A_DAC_INV, 2, 3, 1, 0),
	SOC_DOUBLE("DAC3 Invert Switch", PCM3168A_DAC_INV, 4, 5, 1, 0),
	SOC_DOUBLE("DAC4 Invert Switch", PCM3168A_DAC_INV, 6, 7, 1, 0),
	SOC_DOUBLE_STS("DAC1 Zero Flag", PCM3168A_DAC_ZERO, 0, 1, 1, 0),
	SOC_DOUBLE_STS("DAC2 Zero Flag", PCM3168A_DAC_ZERO, 2, 3, 1, 0),
	SOC_DOUBLE_STS("DAC3 Zero Flag", PCM3168A_DAC_ZERO, 4, 5, 1, 0),
	SOC_DOUBLE_STS("DAC4 Zero Flag", PCM3168A_DAC_ZERO, 6, 7, 1, 0),
	SOC_ENUM("DAC Volume Control Type", pcm3168a_dac_volume_type),
	SOC_ENUM("DAC Volume Rate Multiplier", pcm3168a_dac_att_mult),
	SOC_ENUM("DAC De-Emphasis", pcm3168a_dac_demp),
	SOC_ENUM("DAC Zero Flag Function", pcm3168a_dac_zf_func),
	SOC_ENUM("DAC Zero Flag Polarity", pcm3168a_dac_zf_pol),
	SOC_SINGLE_RANGE_TLV("Master Playback Volume",
			PCM3168A_DAC_VOL_MASTER, 0, 54, 255, 0,
			pcm3168a_dac_tlv),
	SOC_DOUBLE_R_RANGE_TLV("DAC1 Playback Volume",
			PCM3168A_DAC_VOL_CHAN_START,
			PCM3168A_DAC_VOL_CHAN_START + 1,
			0, 54, 255, 0, pcm3168a_dac_tlv),
	SOC_DOUBLE_R_RANGE_TLV("DAC2 Playback Volume",
			PCM3168A_DAC_VOL_CHAN_START + 2,
			PCM3168A_DAC_VOL_CHAN_START + 3,
			0, 54, 255, 0, pcm3168a_dac_tlv),
	SOC_DOUBLE_R_RANGE_TLV("DAC3 Playback Volume",
			PCM3168A_DAC_VOL_CHAN_START + 4,
			PCM3168A_DAC_VOL_CHAN_START + 5,
			0, 54, 255, 0, pcm3168a_dac_tlv),
	SOC_DOUBLE_R_RANGE_TLV("DAC4 Playback Volume",
			PCM3168A_DAC_VOL_CHAN_START + 6,
			PCM3168A_DAC_VOL_CHAN_START + 7,
			0, 54, 255, 0, pcm3168a_dac_tlv),
	SOC_SINGLE("ADC1 High-Pass Filter Switch", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_BYP_SHIFT, 1, 1),
	SOC_SINGLE("ADC2 High-Pass Filter Switch", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_BYP_SHIFT + 1, 1, 1),
	SOC_SINGLE("ADC3 High-Pass Filter Switch", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_BYP_SHIFT + 2, 1, 1),
	SOC_ENUM("ADC1 Connection Type", pcm3168a_adc1_con),
	SOC_ENUM("ADC2 Connection Type", pcm3168a_adc2_con),
	SOC_ENUM("ADC3 Connection Type", pcm3168a_adc3_con),
	SOC_DOUBLE("ADC1 Invert Switch", PCM3168A_ADC_INV, 0, 1, 1, 0),
	SOC_DOUBLE("ADC2 Invert Switch", PCM3168A_ADC_INV, 2, 3, 1, 0),
	SOC_DOUBLE("ADC3 Invert Switch", PCM3168A_ADC_INV, 4, 5, 1, 0),
	SOC_DOUBLE("ADC1 Mute Switch", PCM3168A_ADC_MUTE, 0, 1, 1, 0),
	SOC_DOUBLE("ADC2 Mute Switch", PCM3168A_ADC_MUTE, 2, 3, 1, 0),
	SOC_DOUBLE("ADC3 Mute Switch", PCM3168A_ADC_MUTE, 4, 5, 1, 0),
	SOC_DOUBLE_STS("ADC1 Overflow Flag", PCM3168A_ADC_OV, 0, 1, 1, 0),
	SOC_DOUBLE_STS("ADC2 Overflow Flag", PCM3168A_ADC_OV, 2, 3, 1, 0),
	SOC_DOUBLE_STS("ADC3 Overflow Flag", PCM3168A_ADC_OV, 4, 5, 1, 0),
	SOC_ENUM("ADC Volume Control Type", pcm3168a_adc_volume_type),
	SOC_ENUM("ADC Volume Rate Multiplier", pcm3168a_adc_att_mult),
	SOC_ENUM("ADC Overflow Flag Polarity", pcm3168a_adc_ov_pol),
	SOC_SINGLE_RANGE_TLV("Master Capture Volume",
			PCM3168A_ADC_VOL_MASTER, 0, 14, 255, 0,
			pcm3168a_adc_tlv),
	SOC_DOUBLE_R_RANGE_TLV("ADC1 Capture Volume",
			PCM3168A_ADC_VOL_CHAN_START,
			PCM3168A_ADC_VOL_CHAN_START + 1,
			0, 14, 255, 0, pcm3168a_adc_tlv),
	SOC_DOUBLE_R_RANGE_TLV("ADC2 Capture Volume",
			PCM3168A_ADC_VOL_CHAN_START + 2,
			PCM3168A_ADC_VOL_CHAN_START + 3,
			0, 14, 255, 0, pcm3168a_adc_tlv),
	SOC_DOUBLE_R_RANGE_TLV("ADC3 Capture Volume",
			PCM3168A_ADC_VOL_CHAN_START + 4,
			PCM3168A_ADC_VOL_CHAN_START + 5,
			0, 14, 255, 0, pcm3168a_adc_tlv)
};

static const struct snd_soc_dapm_widget pcm3168a_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC1", "Playback", PCM3168A_DAC_OP_FLT,
			PCM3168A_DAC_OPEDA_SHIFT, 1),
	SND_SOC_DAPM_DAC("DAC2", "Playback", PCM3168A_DAC_OP_FLT,
			PCM3168A_DAC_OPEDA_SHIFT + 1, 1),
	SND_SOC_DAPM_DAC("DAC3", "Playback", PCM3168A_DAC_OP_FLT,
			PCM3168A_DAC_OPEDA_SHIFT + 2, 1),
	SND_SOC_DAPM_DAC("DAC4", "Playback", PCM3168A_DAC_OP_FLT,
			PCM3168A_DAC_OPEDA_SHIFT + 3, 1),

	SND_SOC_DAPM_OUTPUT("AOUT1L"),
	SND_SOC_DAPM_OUTPUT("AOUT1R"),
	SND_SOC_DAPM_OUTPUT("AOUT2L"),
	SND_SOC_DAPM_OUTPUT("AOUT2R"),
	SND_SOC_DAPM_OUTPUT("AOUT3L"),
	SND_SOC_DAPM_OUTPUT("AOUT3R"),
	SND_SOC_DAPM_OUTPUT("AOUT4L"),
	SND_SOC_DAPM_OUTPUT("AOUT4R"),

	SND_SOC_DAPM_ADC("ADC1", "Capture", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_PSVAD_SHIFT, 1),
	SND_SOC_DAPM_ADC("ADC2", "Capture", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_PSVAD_SHIFT + 1, 1),
	SND_SOC_DAPM_ADC("ADC3", "Capture", PCM3168A_ADC_PWR_HPFB,
			PCM3168A_ADC_PSVAD_SHIFT + 2, 1),

	SND_SOC_DAPM_INPUT("AIN1L"),
	SND_SOC_DAPM_INPUT("AIN1R"),
	SND_SOC_DAPM_INPUT("AIN2L"),
	SND_SOC_DAPM_INPUT("AIN2R"),
	SND_SOC_DAPM_INPUT("AIN3L"),
	SND_SOC_DAPM_INPUT("AIN3R")
};

static const struct snd_soc_dapm_route pcm3168a_dapm_routes[] = {
	/* Playback */
	{ "AOUT1L", NULL, "DAC1" },
	{ "AOUT1R", NULL, "DAC1" },

	{ "AOUT2L", NULL, "DAC2" },
	{ "AOUT2R", NULL, "DAC2" },

	{ "AOUT3L", NULL, "DAC3" },
	{ "AOUT3R", NULL, "DAC3" },

	{ "AOUT4L", NULL, "DAC4" },
	{ "AOUT4R", NULL, "DAC4" },

	/* Capture */
	{ "ADC1", NULL, "AIN1L" },
	{ "ADC1", NULL, "AIN1R" },

	{ "ADC2", NULL, "AIN2L" },
	{ "ADC2", NULL, "AIN2R" },

	{ "ADC3", NULL, "AIN3L" },
	{ "ADC3", NULL, "AIN3R" }
};

static unsigned int pcm3168a_scki_ratios[] = {
	768,
	512,
	384,
	256,
	192,
	128
};

#define PCM3168A_NUM_SCKI_RATIOS_DAC	ARRAY_SIZE(pcm3168a_scki_ratios)
#define PCM3168A_NUM_SCKI_RATIOS_ADC	(ARRAY_SIZE(pcm3168a_scki_ratios) - 2)

#define PCM3168A_MAX_SYSCLK		36864000

static int pcm3168a_reset(struct pcm3168a_priv *pcm3168a)
{
	int ret;

	ret = regmap_write(pcm3168a->regmap, PCM3168A_RST_SMODE, 0);
	if (ret)
		return ret;

	/* Internal reset is de-asserted after 3846 SCKI cycles */
	msleep(DIV_ROUND_UP(3846 * 1000, pcm3168a->sysclk));

	return regmap_write(pcm3168a->regmap, PCM3168A_RST_SMODE,
			PCM3168A_MRST_MASK | PCM3168A_SRST_MASK);
}

static int pcm3168a_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(component);

	regmap_write(pcm3168a->regmap, PCM3168A_DAC_MUTE, mute ? 0xff : 0);

	return 0;
}

static int pcm3168a_set_dai_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(dai->component);
	int ret;

	/*
	 * Some sound card sets 0 Hz as reset,
	 * but it is impossible to set. Ignore it here
	 */
	if (freq == 0)
		return 0;

	if (freq > PCM3168A_MAX_SYSCLK)
		return -EINVAL;

	ret = clk_set_rate(pcm3168a->scki, freq);
	if (ret)
		return ret;

	pcm3168a->sysclk = freq;

	return 0;
}

int format_table[3][6] = {
	[TDM_MODE_NONE] = {
		[0]				= -1,
		[SND_SOC_DAIFMT_I2S]		= PCM3168A_FMT_I2S,
		[SND_SOC_DAIFMT_LEFT_J]		= PCM3168A_FMT_LEFT_J,
		[SND_SOC_DAIFMT_RIGHT_J]	= PCM3168A_FMT_RIGHT_J,
		[SND_SOC_DAIFMT_DSP_A]		= PCM3168A_FMT_DSP_A,
		[SND_SOC_DAIFMT_DSP_B]		= PCM3168A_FMT_DSP_B},
	[TDM_MODE_NORM] = {
		[0]				= -1,
		[SND_SOC_DAIFMT_I2S]		= PCM3168A_FMT_I2S_TDM,
		[SND_SOC_DAIFMT_LEFT_J]		= PCM3168A_FMT_LEFT_J_TDM,
		[SND_SOC_DAIFMT_RIGHT_J]	= -1,
		[SND_SOC_DAIFMT_DSP_A]		= -1,
		[SND_SOC_DAIFMT_DSP_B]		= -1},
	[TDM_MODE_HS] = {
		[0]				= -1,
		[SND_SOC_DAIFMT_I2S]		= PCM3168A_FMT_I2S_TDMHS,
		[SND_SOC_DAIFMT_LEFT_J]		= PCM3168A_FMT_LEFT_J_TDMHS,
		[SND_SOC_DAIFMT_RIGHT_J]	= -1,
		[SND_SOC_DAIFMT_DSP_A]		= -1,
		[SND_SOC_DAIFMT_DSP_B]		= -1},
};

static int __pcm3168a_set_dai_fmt(struct snd_soc_dai *dai,
			       unsigned int format, bool dac)
{
	struct snd_soc_component *component = dai->component;
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(component);
	u32 reg, mask, shift;
	int fmt;
	bool master_mode;

	fmt = format_table[pcm3168a->tdm][format & SND_SOC_DAIFMT_FORMAT_MASK];

	if (fmt < 0) {
		dev_err(component->dev, "unsupported dai format of TDM mode\n");
		return -EINVAL;
	}

	switch (format & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		master_mode = false;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		master_mode = true;
		break;
	default:
		dev_err(component->dev, "unsupported master/slave mode\n");
		return -EINVAL;
	}

	if ((pcm3168a->tdm == TDM_MODE_HS) && (master_mode)) {
		dev_err(component->dev, "TDM high speed supported only in slave mode\n");
		return -EINVAL;
	}

	if ((pcm3168a->tdm == TDM_MODE_HS) && (!dac)) {
		dev_err(component->dev, "TDM high speed not supported for ADC\n");
		return -EINVAL;
	}

	switch (format & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	if (dac) {
		reg = PCM3168A_DAC_PWR_MST_FMT;
		mask = PCM3168A_DAC_FMT_MASK;
		shift = PCM3168A_DAC_FMT_SHIFT;
		pcm3168a->dac_fmt = fmt;
	} else {
		reg = PCM3168A_ADC_MST_FMT;
		mask = PCM3168A_ADC_FMTAD_MASK;
		shift = PCM3168A_ADC_FMTAD_SHIFT;
		pcm3168a->adc_fmt = fmt;
	}

	pcm3168a->master_mode = master_mode;

	regmap_update_bits(pcm3168a->regmap, reg, mask, fmt << shift);

	return 0;
}

static int pcm3168a_set_dai_fmt(struct snd_soc_dai *dai,
			       unsigned int format)
{
	int ret;

	/* dac */
	ret = __pcm3168a_set_dai_fmt(dai, format, false);
	if (ret)
		return ret;
	/* adc */
	return __pcm3168a_set_dai_fmt(dai, format, true);
}

static int pcm3168a_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(component);
	int bits;
	bool tx;
	u32 val, mask, shift, reg;
	u32 sample_rate = 0; /* auto */
	unsigned int rate, channels, fmt, ratio, max_ratio;
	int i, min_frame_size;

	rate = params_rate(params);
	channels = params_channels(params);
	bits = params->msbits;

	tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	if (tx) {
		max_ratio = PCM3168A_NUM_SCKI_RATIOS_DAC;
		fmt = pcm3168a->dac_fmt;
	} else {
		max_ratio = PCM3168A_NUM_SCKI_RATIOS_ADC;
		fmt = pcm3168a->adc_fmt;
	}

	if (pcm3168a->master_mode) {
		ratio = pcm3168a->sysclk / rate;
		for (i = 0; i < max_ratio; i++)
			if (pcm3168a_scki_ratios[i] == ratio)
				break;

		if (i == max_ratio) {
			dev_err(component->dev, "unsupported sysclk ratio: %d\n", ratio);
			return -EINVAL;
		}
		val = i + 1;
	} else {
		/* slave mode */
		val = 0;
	}

	if (pcm3168a->tdm == TDM_MODE_NONE) {
		/* one stereo frame size */
		min_frame_size = bits * 2;
		switch (min_frame_size) {
		case 32:
			if (pcm3168a->master_mode ||
				(fmt != PCM3168A_FMT_RIGHT_J)) {
				dev_err(component->dev, "32-bit frames are supported only for slave mode using right justified\n");
				return -EINVAL;
			}
			fmt = PCM3168A_FMT_RIGHT_J_16;
			break;
		case 48:
			if (pcm3168a->master_mode ||
				(fmt == PCM3168A_FMT_DSP_A) ||
				(fmt == PCM3168A_FMT_DSP_B)) {
				dev_err(component->dev, "48-bit frames not supported in master mode, or slave mode using DSP\n");
				return -EINVAL;
			}
			break;
		case 64:
			break;
		default:
			dev_err(component->dev, "unsupported frame size: %d\n", min_frame_size);
			return -EINVAL;
		}
	}
	if ((pcm3168a->tdm == TDM_MODE_NORM) ||
		(pcm3168a->tdm == TDM_MODE_HS)) {
		/* all channels over one or two line */
		min_frame_size = bits * channels;

		/* single rate */
		sample_rate = 1;

		/*
		 * 256fs for single line DIN0/DOUT0
		 * 128fs for two lines DIN01/DOU01
		 */
		if ((min_frame_size != 256) &&
			(min_frame_size != 128)) {
			dev_err(component->dev, "256/128-bit frames only supported in TDM formats\n");
			return -EINVAL;
		}
	}
 
	/* Setup ADC in master mode, couse it drives ADC */
	if ((pcm3168a->master_mode) || (tx)) {
		fmt = pcm3168a->dac_fmt;
		reg = PCM3168A_DAC_PWR_MST_FMT;
		mask = PCM3168A_DAC_MSDA_MASK | PCM3168A_DAC_FMT_MASK;
		shift = PCM3168A_DAC_MSDA_SHIFT;
		/* start DAC */
		regmap_update_bits(pcm3168a->regmap, reg, mask, (val << shift) | fmt);
	}
	/* Do we need also ADC? */
	if (!tx) {
		fmt = pcm3168a->adc_fmt;
		reg = PCM3168A_ADC_MST_FMT;
		mask = PCM3168A_ADC_MSAD_MASK | PCM3168A_ADC_FMTAD_MASK;
		shift = PCM3168A_ADC_MSAD_SHIFT;
		/* ADC slave mode only, driven by DAC or CPU DAI */
		val = 0;
		regmap_update_bits(pcm3168a->regmap, reg, mask, (val << shift) | fmt);
	}

	regmap_update_bits(pcm3168a->regmap, PCM3168A_RST_SMODE,
		PCM3168A_DAC_SRDA_MASK,
		sample_rate << PCM3168A_DAC_SRDA_SHIFT);

	return 0;
}

static int pcm3168a_set_tdm_slot(struct snd_soc_dai *dai,
			        unsigned int tx_mask,
				unsigned int rx_mask,
				int slots,
				int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct pcm3168a_priv *pcm3168a = snd_soc_component_get_drvdata(component);

	if ((slots != 8) && (slots != 4))
		return -EINVAL;

	if ((slot_width != 32) && (slot_width != 24))
		return -EINVAL;

	pcm3168a->slots = slots;
	pcm3168a->slot_width = slot_width;

	return 0;
}

static const struct snd_soc_dai_ops pcm3168a_dai_ops = {
	.set_fmt	= pcm3168a_set_dai_fmt,
	.set_sysclk	= pcm3168a_set_dai_sysclk,
	.hw_params	= pcm3168a_hw_params,
	.digital_mute	= pcm3168a_digital_mute,
	.set_tdm_slot	= pcm3168a_set_tdm_slot,
};

static struct snd_soc_dai_driver pcm3168a_dai = {
	.name = "pcm3168a",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = PCM3168A_FORMATS
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = PCM3168A_FORMATS
	},
	.ops = &pcm3168a_dai_ops,
	.symmetric_rates = 1,
};

static const struct reg_default pcm3168a_reg_default[] = {
	{ PCM3168A_RST_SMODE, PCM3168A_MRST_MASK | PCM3168A_SRST_MASK },
	{ PCM3168A_DAC_PWR_MST_FMT, 0x80 },
	{ PCM3168A_DAC_OP_FLT, 0x00 },
	{ PCM3168A_DAC_INV, 0x00 },
	{ PCM3168A_DAC_MUTE, 0x00 },
	{ PCM3168A_DAC_ZERO, 0x00 },
	{ PCM3168A_DAC_ATT_DEMP_ZF, 0x00 },
	{ PCM3168A_DAC_VOL_MASTER, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 1, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 2, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 3, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 4, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 5, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 6, 0xff },
	{ PCM3168A_DAC_VOL_CHAN_START + 7, 0xff },
	{ PCM3168A_ADC_SMODE, 0x00 },
	{ PCM3168A_ADC_MST_FMT, 0x00 },
	{ PCM3168A_ADC_PWR_HPFB, 0x00 },
	{ PCM3168A_ADC_SEAD, 0x00 },
	{ PCM3168A_ADC_INV, 0x00 },
	{ PCM3168A_ADC_MUTE, 0x00 },
	{ PCM3168A_ADC_OV, 0x00 },
	{ PCM3168A_ADC_ATT_OVF, 0x00 },
	{ PCM3168A_ADC_VOL_MASTER, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START + 1, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START + 2, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START + 3, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START + 4, 0xd3 },
	{ PCM3168A_ADC_VOL_CHAN_START + 5, 0xd3 }
};

static bool pcm3168a_readable_register(struct device *dev, unsigned int reg)
{
	if (reg >= PCM3168A_RST_SMODE)
		return true;
	else
		return false;
}

static bool pcm3168a_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PCM3168A_DAC_ZERO:
	case PCM3168A_ADC_OV:
		return true;
	default:
		return false;
	}
}

static bool pcm3168a_writeable_register(struct device *dev, unsigned int reg)
{
	if (reg < PCM3168A_RST_SMODE)
		return false;

	switch (reg) {
	case PCM3168A_DAC_ZERO:
	case PCM3168A_ADC_OV:
		return false;
	default:
		return true;
	}
}

const struct regmap_config pcm3168a_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = PCM3168A_ADC_VOL_CHAN_START + 5,
	.reg_defaults = pcm3168a_reg_default,
	.num_reg_defaults = ARRAY_SIZE(pcm3168a_reg_default),
	.readable_reg = pcm3168a_readable_register,
	.volatile_reg = pcm3168a_volatile_register,
	.writeable_reg = pcm3168a_writeable_register,
	.cache_type = REGCACHE_FLAT
};
EXPORT_SYMBOL_GPL(pcm3168a_regmap);

static const struct snd_soc_component_driver pcm3168a_driver = {
	.controls		= pcm3168a_snd_controls,
	.num_controls		= ARRAY_SIZE(pcm3168a_snd_controls),
	.dapm_widgets		= pcm3168a_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcm3168a_dapm_widgets),
	.dapm_routes		= pcm3168a_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(pcm3168a_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

int pcm3168a_probe(struct device *dev, struct regmap *regmap)
{
	struct pcm3168a_priv *pcm3168a;
	int ret, i;

	pcm3168a = devm_kzalloc(dev, sizeof(*pcm3168a), GFP_KERNEL);
	if (pcm3168a == NULL)
		return -ENOMEM;

	dev_set_drvdata(dev, pcm3168a);

	pcm3168a->scki = devm_clk_get(dev, "scki");
	if (IS_ERR(pcm3168a->scki)) {
		ret = PTR_ERR(pcm3168a->scki);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to acquire clock 'scki': %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(pcm3168a->scki);
	if (ret) {
		dev_err(dev, "Failed to enable mclk: %d\n", ret);
		return ret;
	}

	pcm3168a->sysclk = clk_get_rate(pcm3168a->scki);

	for (i = 0; i < ARRAY_SIZE(pcm3168a->supplies); i++)
		pcm3168a->supplies[i].supply = pcm3168a_supply_names[i];

	ret = devm_regulator_bulk_get(dev,
			ARRAY_SIZE(pcm3168a->supplies), pcm3168a->supplies);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to request supplies: %d\n", ret);
		goto err_clk;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(pcm3168a->supplies),
				    pcm3168a->supplies);
	if (ret) {
		dev_err(dev, "failed to enable supplies: %d\n", ret);
		goto err_clk;
	}

	pcm3168a->regmap = regmap;
	if (IS_ERR(pcm3168a->regmap)) {
		ret = PTR_ERR(pcm3168a->regmap);
		dev_err(dev, "failed to allocate regmap: %d\n", ret);
		goto err_regulator;
	}

	ret = pcm3168a_reset(pcm3168a);
	if (ret) {
		dev_err(dev, "Failed to reset device: %d\n", ret);
		goto err_regulator;
	}

	/* get TDM mode */
	if (dev->of_node) {
		if (of_get_property(dev->of_node, "tdm", NULL))
			pcm3168a->tdm = TDM_MODE_NORM;
		else if (of_get_property(dev->of_node, "tdmhs", NULL))
			pcm3168a->tdm = TDM_MODE_HS;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	if (pcm3168a->tdm != TDM_MODE_NONE) {
		pcm3168a_dai.playback.channels_min = 8;
		pcm3168a_dai.capture.channels_min = 8;
	}

	ret = devm_snd_soc_register_component(dev, &pcm3168a_driver, &pcm3168a_dai, 1);
	if (ret) {
		dev_err(dev, "failed to register component: %d\n", ret);
		goto err_regulator;
	}

	return 0;

err_regulator:
	regulator_bulk_disable(ARRAY_SIZE(pcm3168a->supplies),
			pcm3168a->supplies);
err_clk:
	clk_disable_unprepare(pcm3168a->scki);

	return ret;
}
EXPORT_SYMBOL_GPL(pcm3168a_probe);

static void pcm3168a_disable(struct device *dev)
{
	struct pcm3168a_priv *pcm3168a = dev_get_drvdata(dev);

	regulator_bulk_disable(ARRAY_SIZE(pcm3168a->supplies),
			       pcm3168a->supplies);
	clk_disable_unprepare(pcm3168a->scki);
}

void pcm3168a_remove(struct device *dev)
{
	pm_runtime_disable(dev);
#ifndef CONFIG_PM
	pcm3168a_disable(dev);
#endif
}
EXPORT_SYMBOL_GPL(pcm3168a_remove);

#ifdef CONFIG_PM
static int pcm3168a_rt_resume(struct device *dev)
{
	struct pcm3168a_priv *pcm3168a = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(pcm3168a->scki);
	if (ret) {
		dev_err(dev, "Failed to enable mclk: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(pcm3168a->supplies),
				    pcm3168a->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		goto err_clk;
	}

	ret = pcm3168a_reset(pcm3168a);
	if (ret) {
		dev_err(dev, "Failed to reset device: %d\n", ret);
		goto err_regulator;
	}

	regcache_cache_only(pcm3168a->regmap, false);

	regcache_mark_dirty(pcm3168a->regmap);

	ret = regcache_sync(pcm3168a->regmap);
	if (ret) {
		dev_err(dev, "Failed to sync regmap: %d\n", ret);
		goto err_regulator;
	}

	return 0;

err_regulator:
	regulator_bulk_disable(ARRAY_SIZE(pcm3168a->supplies),
			       pcm3168a->supplies);
err_clk:
	clk_disable_unprepare(pcm3168a->scki);

	return ret;
}

static int pcm3168a_rt_suspend(struct device *dev)
{
	struct pcm3168a_priv *pcm3168a = dev_get_drvdata(dev);

	regcache_cache_only(pcm3168a->regmap, true);

	pcm3168a_disable(dev);

	return 0;
}
#endif

const struct dev_pm_ops pcm3168a_pm_ops = {
	SET_RUNTIME_PM_OPS(pcm3168a_rt_suspend, pcm3168a_rt_resume, NULL)
};
EXPORT_SYMBOL_GPL(pcm3168a_pm_ops);

MODULE_DESCRIPTION("PCM3168A codec driver");
MODULE_AUTHOR("Damien Horsley <Damien.Horsley@imgtec.com>");
MODULE_LICENSE("GPL v2");
