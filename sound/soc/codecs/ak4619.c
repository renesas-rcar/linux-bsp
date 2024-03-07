// SPDX-License-Identifier: GPL-2.0
/*
 * ak4619.c -- Asahi Kasei ALSA SoC Audio driver
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 * Khanh Le <khanh.le.xr@renesas.com>
 *
 * Based on ak4613.c by Kuninori Morimoto
 * Based on da7213.c by Adam Thomson
 * Based on ak4641.c by Harald Welte
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

/*
 * Registes
 */

#define PWR_MGMT	0x00	/* Power Management */
#define AU_IFF1		0x01	/* Audio I/F Format 1 */
#define AU_IFF2		0x02	/* Audio I/F Format 1 */
#define SYS_CLK		0x03	/* System Clock Setting */
#define MIC_AMP1	0x04	/* MIC AMP Gain 1 */
#define MIC_AMP2	0x05	/* MIC AMP Gain 2 */
#define LADC1		0x06	/* ADC1 Lch Digital Volume */
#define RADC1		0x07	/* ADC1 Rch Digital Volume */
#define LADC2		0x08	/* ADC2 Lch Digital Volume */
#define RADC2		0x09	/* ADC2 Rch Digital Volume */
#define ADC_DF		0x0a	/* ADC Digital Filter Setting */
#define ADC_AI		0x0b	/* ADC Analog Input Setting */
#define ADC_MHPF	0x0D	/* ADC Mute & HPF Control */
#define LDAC1		0x0E	/* DAC1 Lch Digital Volume */
#define RDAC1		0x0F	/* DAC1 Rch Digital Volume */
#define LDAC2		0x10	/* DAC2 Lch Digital Volume */
#define RDAC2		0x11	/* DAC2 Rch Digital Volume */
#define DAC_IS		0x12	/* DAC Input Select Setting */
#define DAC_DEMP	0x13	/* DAC De-Emphasis Setting */
#define DAC_MF		0x14	/* DAC Mute & Filter Setting */

/*
 * Bit fields
 */

/* Power Management */
#define PMAD2	BIT(5)
#define PMAD1	BIT(4)
#define PMDA2	BIT(2)
#define PMDA1	BIT(1)
#define RSTN	BIT(0)

/* Audio_I/F Format 1 */
#define DCF_STEREO_I2S	(0x0 << 4)
#define DCF_STEREO_MSB	(0x5 << 4)
#define DCF_PCM_SF	(0x6 << 4)
#define DCF_PCM_LF	(0x7 << 4)
#define DSL_32		(0x3 << 2)
#define DCF_MASK 	(0x7 << 4)
#define DSL_MASK	(0x3 << 2)
#define BCKP		BIT(1)

/* Audio_I/F Format 2 */
#define DIDL_24		(0x0 << 2)
#define DIDL_20		(0x1 << 2)
#define DIDL_16		(0x2 << 2)
#define DIDL_32		(0x3 << 2)
#define DODL_24		(0x0 << 0)
#define DODL_20		(0x1 << 0)
#define DODL_16		(0x2 << 0)
#define DIDL_MASK 	(0x3 << 2)
#define DODL_MASK	(0x3 << 0)
#define SLOT            BIT(4)

/* System Clock Setting */
#define FS_MASK		0x7

/* MIC AMP Gain */
#define MGNL_SHIFT	4
#define MGNR_SHIFT	0
#define MGN_MAX		0xB

/* ADC Digital Volume */
#define VOLAD_SHIFT	0
#define VOLAD_MAX	0xFF

/* ADC Digital Filter Setting */
#define AD1SL_SHIFT	0
#define AD2SL_SHIFT	4

/* Analog Input Select */
#define AD1LSEL_SHIFT	6
#define AD1RSEL_SHIFT	4
#define AD2LSEL_SHIFT	2
#define AD2RSEL_SHIFT	0

/* ADC Mute & HPF Control */
#define ATSPAD_SHIFT	7
#define AD1MUTE_SHIFT	5
#define AD2MUTE_SHIFT	6
#define AD1MUTE_MAX	1
#define AD2MUTE_MAX	1
#define AD1MUTE_EN	BIT(5)
#define AD2MUTE_EN	BIT(6)
#define AD1HPFN_SHIFT	1
#define AD1HPFN_MAX	1
#define AD2HPFN_SHIFT	2
#define AD2HPFN_MAX	1

/* DAC Digital Volume */
#define VOLDA_SHIFT	0
#define VOLDA_MAX	0xFF

/* DAC Input Select Setting */
#define DAC1SEL_SHIFT	0
#define DAC2SEL_SHIFT	2

/* DAC De-Emphasis Setting */
#define DEM1_32000	(0x3 << 0)
#define DEM1_44100	(0x0 << 0)
#define DEM1_48000	(0x2 << 0)
#define DEM1_OFF	(0x1 << 0)
#define DEM2_32000	(0x3 << 2)
#define DEM2_44100	(0x0 << 2)
#define DEM2_48000	(0x2 << 2)
#define DEM2_OFF	(0x1 << 2)
#define DEM1_MASK	(0x3 << 0)
#define DEM2_MASK	(0x3 << 2)
#define DEM1_SHIFT	0
#define DEM2_SHIFT	2

/* DAC Mute & Filter Setting */
#define DA1MUTE_SHIFT	4
#define DA1MUTE_MAX	1
#define DA2MUTE_SHIFT	5
#define DA2MUTE_MAX	1
#define DA1MUTE_EN	BIT(4)
#define DA2MUTE_EN	BIT(5)
#define ATSPDA_SHIFT	7
#define DA1SL_SHIFT	0
#define DA2SL_SHIFT	2

/* Codec private data */
struct ak4619_priv {
	struct regmap *regmap;
	struct gpio_desc *pdn_gpiod;
	struct gpio_desc *mode_gpiod;
	int deemph_en;
	unsigned int playback_rate;
	unsigned int sysclk;
};

/*
 * DAC Volume
 *
 * max : 0x00 : +12.0 dB
 *       ( 0.5 dB step )
 * min : 0xFE : -115.0 dB
 * mute: 0xFF
 */
static const DECLARE_TLV_DB_SCALE(dac_tlv, -11550, 50, 1);

/*
 * MIC Volume
 *
 * max : 0x0B : +27.0 dB
 * 		( 3 dB step )
 * min: 0x00 : -6.0 dB
 */
static const DECLARE_TLV_DB_SCALE(mic_tlv, -600, 300, 0);

/*
 * ADC Volume
 *
 * max : 0x00 : +24.0 dB
 *		( 0.5 dB step )
 * min : 0xFE : -103.0 dB
 * mute: 0xFF
 */
static const DECLARE_TLV_DB_SCALE(adc_tlv, -10350, 50, 1);

/* ADC & DAC Volume Level Transition Time select */
static const char * const ak4619_vol_trans_time_txt[] = {
	"4/fs", "16/fs"
};

static SOC_ENUM_SINGLE_DECL(ak4619_adc_vol_trans_time,
				ADC_MHPF,
				ATSPAD_SHIFT,
				ak4619_vol_trans_time_txt);

static SOC_ENUM_SINGLE_DECL(ak4619_dac_vol_trans_time,
				DAC_MF,
				ATSPDA_SHIFT,
				ak4619_vol_trans_time_txt);

/* ADC Digital Filter select */
static const char * const ak4619_adc_digi_fil_sel_txt[] = {
	"Sharp Roll-Off Filter", "Slow Roll-Off Filter",
	"Short Delay Sharp Roll-Off Filter", "Short Delay Slow Roll-Off Filter",
	"Voice Filter"
};

static SOC_ENUM_SINGLE_DECL(ak4619_adc_1_digi_fil_sel,
				ADC_DF,
				AD1SL_SHIFT,
				ak4619_adc_digi_fil_sel_txt);

static SOC_ENUM_SINGLE_DECL(ak4619_adc_2_digi_fil_sel,
				ADC_DF,
				AD2SL_SHIFT,
				ak4619_adc_digi_fil_sel_txt);

/* DAC De-Emphasis Filter select */
static const char * const ak4619_dac_digi_de_emp_sel_txt[] = {
	"44.1kHz", "OFF", "48kHz", "32kHz"
};

static SOC_ENUM_SINGLE_DECL(ak4619_dac_1_digi_de_emp_sel,
				DAC_DEMP,
				DEM1_SHIFT,
				ak4619_dac_digi_de_emp_sel_txt);

static SOC_ENUM_SINGLE_DECL(ak4619_dac_2_digi_de_emp_sel,
				DAC_DEMP,
				DEM2_SHIFT,
				ak4619_dac_digi_de_emp_sel_txt);

/* DAC Digital Filter select */
static const char * const ak4619_dac_digi_fil_sel_txt[] = {
	"Sharp Roll-Off Filter", "Slow Roll-Off Filter",
	"Short Delay Sharp Roll-Off Filter", "Short Delay Slow Roll-Off Filter"
};

static SOC_ENUM_SINGLE_DECL(ak4619_dac_1_digi_fil_sel,
				DAC_MF,
				DA1SL_SHIFT,
				ak4619_dac_digi_fil_sel_txt);

static SOC_ENUM_SINGLE_DECL(ak4619_dac_2_digi_fil_sel,
				DAC_MF,
				DA2SL_SHIFT,
				ak4619_dac_digi_fil_sel_txt);

/*
 * Control functions
 */

static void ak4619_set_deemph(struct snd_soc_component *component)
{
	struct ak4619_priv *ak4619 = snd_soc_component_get_drvdata(component);
	u8 dem = 0;

	if (ak4619->deemph_en) {
		switch(ak4619->playback_rate) {
		case 32000:
			dem |= DEM1_32000 | DEM2_32000;
			break;
		case 44100:
			dem |= DEM1_44100 | DEM2_44100;
			break;
		case 48000:
			dem |= DEM1_48000 | DEM2_48000;
			break;
		default:
			dem |= DEM1_OFF | DEM2_OFF;
			break;
		}
		snd_soc_component_update_bits(component, DAC_DEMP, DEM1_MASK |
						DEM2_MASK, dem);
	}
}

static int ak4619_put_deemph(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4619_priv *ak4619 = snd_soc_component_get_drvdata(component);
	int deemph_en = ucontrol->value.integer.value[0];

	if (deemph_en > 1)
		return -EINVAL;

	ak4619->deemph_en = deemph_en;
	ak4619_set_deemph(component);
	return 0;
}

static int ak4619_get_deemph(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4619_priv *ak4619 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = ak4619->deemph_en;
	return 0;
};

/*
 * KControls
 */
static const struct snd_kcontrol_new ak4619_snd_controls[] = {

	/* Volume controls */
	SOC_DOUBLE_R_TLV("DAC 1 Volume", LDAC1, RDAC1,
			VOLDA_SHIFT, VOLDA_MAX, 1, dac_tlv),
	SOC_DOUBLE_R_TLV("DAC 2 Volume", LDAC2, RDAC2,
			VOLDA_SHIFT, VOLDA_MAX, 1, dac_tlv),
	SOC_DOUBLE_R_TLV("ADC 1 Volume", LADC1, RADC1,
			VOLAD_SHIFT, VOLAD_MAX, 1, adc_tlv),
	SOC_DOUBLE_R_TLV("ADC 2 Volume", LADC2, RADC2,
			VOLAD_SHIFT, VOLAD_MAX, 1, adc_tlv),
	SOC_DOUBLE_TLV("Mic 1 Volume", MIC_AMP1, MGNL_SHIFT,
			MGNR_SHIFT, MGN_MAX, 0, mic_tlv),
	SOC_DOUBLE_TLV("Mic 2 Volume", MIC_AMP2, MGNL_SHIFT,
			MGNR_SHIFT, MGN_MAX, 0, mic_tlv),

	/* Volume Level Transition Time controls */
	SOC_ENUM("ADC Volume Level Transition Time", ak4619_adc_vol_trans_time),
	SOC_ENUM("DAC Volume Level Transition Time", ak4619_dac_vol_trans_time),

	/* Mute controls */
	SOC_SINGLE("DAC 1 Switch", DAC_MF,
			DA1MUTE_SHIFT, DA1MUTE_MAX, 1),
	SOC_SINGLE("DAC 2 Switch", DAC_MF,
			DA2MUTE_SHIFT, DA2MUTE_MAX, 1),
	SOC_SINGLE("ADC 1 Switch", ADC_MHPF,
			AD1MUTE_SHIFT, AD1MUTE_MAX, 1),
	SOC_SINGLE("ADC 2 Switch", ADC_MHPF,
			AD2MUTE_SHIFT, AD2MUTE_MAX, 1),

	/* Filter controls */
	SOC_ENUM("ADC 1 Digital Filter", ak4619_adc_1_digi_fil_sel),
	SOC_ENUM("ADC 2 Digital Filter", ak4619_adc_2_digi_fil_sel),
	SOC_SINGLE("ADC 1 HPF", ADC_MHPF, AD1HPFN_SHIFT,
			AD1HPFN_MAX, 1),
	SOC_SINGLE("ADC 2 HPF", ADC_MHPF, AD2HPFN_SHIFT,
			AD2HPFN_MAX, 1),
	SOC_ENUM("DAC 1 De-Emphasis Filter", ak4619_dac_1_digi_de_emp_sel),
	SOC_ENUM("DAC 2 De-Emphasis Filter", ak4619_dac_2_digi_de_emp_sel),
	SOC_ENUM("DAC 1 Digital Filter", ak4619_dac_1_digi_fil_sel),
	SOC_ENUM("DAC 2 Digital Filter", ak4619_dac_2_digi_fil_sel),
	SOC_SINGLE_BOOL_EXT("Playback De-Emphasis Switch", 0,
					ak4619_get_deemph, ak4619_put_deemph),

};

/*
 * DAPM
 */

/* Analog input mode */
static const char * const ak4619_analog_in_sel_txt[] = {
	"Differential", "Single-Ended1", "Single-Ended2", "Pseudo Differential"
};

static SOC_ENUM_SINGLE_DECL(ak4619_ad_1_left_in_sel,
				ADC_AI,
				AD1LSEL_SHIFT,
				ak4619_analog_in_sel_txt);

static const struct snd_kcontrol_new ak4619_ad_1_left_in_sel_mux =
	SOC_DAPM_ENUM("Analog Input 1 Left MUX", ak4619_ad_1_left_in_sel);

static SOC_ENUM_SINGLE_DECL(ak4619_ad_1_right_in_sel,
				ADC_AI,
				AD1RSEL_SHIFT,
				ak4619_analog_in_sel_txt);

static const struct snd_kcontrol_new ak4619_ad_1_right_in_sel_mux =
	SOC_DAPM_ENUM("Analog Input 1 Right MUX", ak4619_ad_1_right_in_sel);

static SOC_ENUM_SINGLE_DECL(ak4619_ad_2_left_in_sel,
				ADC_AI,
				AD2LSEL_SHIFT,
				ak4619_analog_in_sel_txt);

static const struct snd_kcontrol_new ak4619_ad_2_left_in_sel_mux =
	SOC_DAPM_ENUM("Analog Input 2 Left MUX", ak4619_ad_2_left_in_sel);

static SOC_ENUM_SINGLE_DECL(ak4619_ad_2_right_in_sel,
				ADC_AI,
				AD2RSEL_SHIFT,
				ak4619_analog_in_sel_txt);

static const struct snd_kcontrol_new ak4619_ad_2_right_in_sel_mux =
	SOC_DAPM_ENUM("Analog Input 2 Right MUX", ak4619_ad_2_right_in_sel);

/* DAC source mux */
static const char * const ak4619_dac_in_sel_txt[] = {
	"SDIN1", "SDIN2", "SDOUT1", "SDOUT2"
};

static SOC_ENUM_SINGLE_DECL(ak4619_dac_1_in_sel,
				DAC_IS,
				DAC1SEL_SHIFT,
				ak4619_dac_in_sel_txt);

static const struct snd_kcontrol_new ak4619_dac_1_in_sel_mux =
	SOC_DAPM_ENUM("DAC 1 Source MUX", ak4619_dac_1_in_sel);

static SOC_ENUM_SINGLE_DECL(ak4619_dac_2_in_sel,
				DAC_IS,
				DAC2SEL_SHIFT,
				ak4619_dac_in_sel_txt);

static const struct snd_kcontrol_new ak4619_dac_2_in_sel_mux =
	SOC_DAPM_ENUM("DAC 2 Source MUX", ak4619_dac_2_in_sel);

static const struct snd_soc_dapm_widget ak4619_dapm_widgets[] = {

	/* DACs */
	SND_SOC_DAPM_DAC("DAC1", NULL, PWR_MGMT, 1, 0),
	SND_SOC_DAPM_DAC("DAC2", NULL, PWR_MGMT, 2, 0),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC1", NULL, PWR_MGMT, 4, 0),
	SND_SOC_DAPM_ADC("ADC2", NULL, PWR_MGMT, 5, 0),

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("AOUT1L"),
	SND_SOC_DAPM_OUTPUT("AOUT2L"),

	SND_SOC_DAPM_OUTPUT("AOUT1R"),
	SND_SOC_DAPM_OUTPUT("AOUT2R"),

	/* Inputs */
	SND_SOC_DAPM_INPUT("AIN1L"),
	SND_SOC_DAPM_INPUT("AIN2L"),
	SND_SOC_DAPM_INPUT("AIN4L"),
	SND_SOC_DAPM_INPUT("AIN5L"),

	SND_SOC_DAPM_INPUT("AIN1R"),
	SND_SOC_DAPM_INPUT("AIN2R"),
	SND_SOC_DAPM_INPUT("AIN4R"),
	SND_SOC_DAPM_INPUT("AIN5R"),

	SND_SOC_DAPM_INPUT("MIC1L"),
	SND_SOC_DAPM_INPUT("MIC1R"),
	SND_SOC_DAPM_INPUT("MIC2L"),
	SND_SOC_DAPM_INPUT("MIC2R"),

	/* DAI */
	SND_SOC_DAPM_AIF_IN("SDIN1", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("SDIN2", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SDOUT1", "Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("SDOUT2", "Capture", 0, SND_SOC_NOPM, 0, 0),

	/* MUXs for Mic PGA source selection */
	SND_SOC_DAPM_MUX("Analog Input 1 Left MUX", SND_SOC_NOPM, 0, 0,
		&ak4619_ad_1_left_in_sel_mux),
	SND_SOC_DAPM_MUX("Analog Input 1 Right MUX", SND_SOC_NOPM, 0, 0,
		&ak4619_ad_1_right_in_sel_mux),
	SND_SOC_DAPM_MUX("Analog Input 2 Left MUX", SND_SOC_NOPM, 0, 0,
		&ak4619_ad_2_left_in_sel_mux),
	SND_SOC_DAPM_MUX("Analog Input 2 Right MUX", SND_SOC_NOPM, 0, 0,
		&ak4619_ad_2_right_in_sel_mux),

	/* MUXs for DAC source selection */
	SND_SOC_DAPM_MUX("DAC 1 Source MUX", SND_SOC_NOPM, 0, 0,
		&ak4619_dac_1_in_sel_mux),
	SND_SOC_DAPM_MUX("DAC 2 Source MUX", SND_SOC_NOPM, 0, 0,
		&ak4619_dac_2_in_sel_mux),

};

static const struct snd_soc_dapm_route ak4619_intercon[] = {
	/* Dest       Connecting Widget    Source */

	/* Output path */
	{"AOUT1L", NULL, "DAC1"},
	{"AOUT2L", NULL, "DAC2"},

	{"AOUT1R", NULL, "DAC1"},
	{"AOUT2R", NULL, "DAC2"},

	{"DAC1", NULL, "DAC 1 Source MUX"},
	{"DAC2", NULL, "DAC 2 Source MUX"},

	{"DAC 1 Source MUX", "SDIN1", "SDIN1"},
	{"DAC 1 Source MUX", "SDIN2", "SDIN2"},
	{"DAC 1 Source MUX", "SDOUT1", "SDOUT1"},
	{"DAC 1 Source MUX", "SDOUT2", "SDOUT2"},

	{"DAC 2 Source MUX", "SDIN1", "SDIN1"},
	{"DAC 2 Source MUX", "SDIN2", "SDIN2"},
	{"DAC 2 Source MUX", "SDOUT1", "SDOUT1"},
	{"DAC 2 Source MUX", "SDOUT2", "SDOUT2"},

	/* Input path */
	{"SDOUT1", NULL, "ADC1"},
	{"SDOUT2", NULL, "ADC2"},

	{"ADC1", NULL, "Analog Input 1 Left MUX"},
	{"ADC1", NULL, "Analog Input 1 Right MUX"},

	{"ADC2", NULL, "Analog Input 2 Left MUX"},
	{"ADC2", NULL, "Analog Input 2 Right MUX"},

	{"Analog Input 1 Left MUX", "Differential", "MIC1L"},
	{"Analog Input 1 Left MUX", "Single-Ended1", "MIC1L"},
	{"Analog Input 1 Left MUX", "Single-Ended2", "MIC1L"},
	{"Analog Input 1 Left MUX", "Pseudo Differential", "MIC1L"},

	{"Analog Input 1 Right MUX", "Differential", "MIC1R"},
	{"Analog Input 1 Right MUX", "Single-Ended1", "MIC1R"},
	{"Analog Input 1 Right MUX", "Single-Ended2", "MIC1R"},
	{"Analog Input 1 Right MUX", "Pseudo Differential", "MIC1R"},

	{"Analog Input 2 Left MUX", "Differential", "MIC2L"},
	{"Analog Input 2 Left MUX", "Single-Ended1", "MIC2L"},
	{"Analog Input 2 Left MUX", "Single-Ended2", "MIC2L"},
	{"Analog Input 2 Left MUX", "Pseudo Differential", "MIC2L"},

	{"Analog Input 2 Right MUX", "Differential", "MIC2R"},
	{"Analog Input 2 Right MUX", "Single-Ended1", "MIC2R"},
	{"Analog Input 2 Right MUX", "Single-Ended2", "MIC2R"},
	{"Analog Input 2 Right MUX", "Pseudo Differential", "MIC2R"},

	{"MIC1L", NULL, "AIN1L"},
	{"MIC1L", NULL, "AIN2L"},

	{"MIC1R", NULL, "AIN1R"},
	{"MIC1R", NULL, "AIN2R"},

	{"MIC2L", NULL, "AIN4L"},
	{"MIC2L", NULL, "AIN5L"},

	{"MIC2R", NULL, "AIN4R"},
	{"MIC2R", NULL, "AIN5R"},

};

static const struct reg_default ak4619_reg_defaults[] = {
	{ PWR_MGMT, 0x00 },
	{ AU_IFF1, 0x0C },
	{ AU_IFF2, 0x0C },
	{ SYS_CLK, 0x00 },
	{ MIC_AMP1, 0x22 },
	{ MIC_AMP2, 0x22 },
	{ LADC1, 0x30 },
	{ RADC1, 0x30 },
	{ LADC2, 0x30 },
	{ RADC2, 0x30 },
	{ ADC_DF, 0x00 },
	{ ADC_AI, 0x00 },
	{ ADC_MHPF, 0x00 },
	{ LDAC1, 0x18 },
	{ RDAC1, 0x18 },
	{ LDAC2, 0x18 },
	{ RDAC2, 0x18 },
	{ DAC_IS, 0x04 },
	{ DAC_DEMP, 0x05 },
	{ DAC_MF, 0x0A },
};

static int ak4619_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	u8 pwr_ctrl = 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
		pwr_ctrl |= RSTN;
		fallthrough;
	case SND_SOC_BIAS_PREPARE:
		pwr_ctrl |= PMAD1 | PMAD2 | PMDA1 | PMDA2;
		fallthrough;
	case SND_SOC_BIAS_STANDBY:
	case SND_SOC_BIAS_OFF:
	default:
		break;
	}

	snd_soc_component_write(component, PWR_MGMT, pwr_ctrl);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_ak4619 = {
	.set_bias_level	= ak4619_set_bias_level,
	.controls		= ak4619_snd_controls,
	.num_controls		= ARRAY_SIZE(ak4619_snd_controls),
	.dapm_widgets		= ak4619_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ak4619_dapm_widgets),
	.dapm_routes		= ak4619_intercon,
	.num_dapm_routes	= ARRAY_SIZE(ak4619_intercon),
	.idle_bias_on		= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config ak4619_regmap_cfg = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= 0x14,
	.reg_defaults		= ak4619_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(ak4619_reg_defaults),
	.cache_type		= REGCACHE_RBTREE,
};

static const struct of_device_id ak4619_of_match[] = {
	{ .compatible = "asahi-kasei,ak4619",	.data = &ak4619_regmap_cfg },
	{},
};
MODULE_DEVICE_TABLE(of, ak4619_of_match);

static const struct i2c_device_id ak4619_i2c_id[] = {
	{ "ak4619", (kernel_ulong_t)&ak4619_regmap_cfg },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak4619_i2c_id);

static int ak4619_dai_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4619_priv *ak4619 = snd_soc_component_get_drvdata(component);
	unsigned int width;
	unsigned int rate;
	unsigned int fs;
	bool is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	u8 dai_ctrl = 0;
	u8 clk_mode = 0;

	width = params_width(params);
	switch (width) {
	case 16:
		dai_ctrl |= is_play ? DIDL_16 : DODL_16;
		break;
	case 20:
		dai_ctrl |= is_play ? DIDL_20 : DODL_20;
		break;
	case 24:
		dai_ctrl |= is_play ? DIDL_24 : DODL_24;
		break;
	case 32:
		if (is_play)
			dai_ctrl |= DIDL_32;
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	rate = params_rate(params);
	if (rate)
		fs = ak4619->sysclk / rate;
	else
		return -EINVAL;

	switch (rate) {
	case 8000:
	case 11025:
	case 12000:
	case 16000:
	case 22050:
	case 24000:
	case 32000:
	case 44100:
	case 48000:
		switch (fs) {
		case 256:
			clk_mode |= (0x0 << 0);
			break;
		case 384:
			clk_mode |= (0x2 << 0);
			break;
		case 512:
			clk_mode |= (0x3 << 0);
			break;
		default:
			return -EINVAL;
		}
		break;
	case 64000:
	case 88200:
	case 96000:
		if (fs == 256)
			clk_mode |= (0x1 << 0);
		else
			return -EINVAL;
		break;
	case 176400:
	case 192000:
		if (fs == 128)
			clk_mode |= (0x4 << 0);
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, SYS_CLK, FS_MASK, clk_mode);
	snd_soc_component_update_bits(component, AU_IFF2, is_play ? DIDL_MASK : DODL_MASK, dai_ctrl);

	if (is_play) {
		ak4619->playback_rate = rate;
		ak4619_set_deemph(component);
	}

	return 0;
}

static int ak4619_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	u8 dai_fmt1 = 0;
	u8 dai_fmt2 = 0;

	/* Set clock normal/inverted */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			dai_fmt1 |= BCKP;
			break;
		case SND_SOC_DAIFMT_NB_IF:
		case SND_SOC_DAIFMT_IB_IF:
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAI_FORMAT_DSP_A:
	case SND_SOC_DAI_FORMAT_DSP_B:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			dai_fmt1 |= BCKP;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			break;
		case SND_SOC_DAIFMT_NB_IF:
		case SND_SOC_DAIFMT_IB_IF:
		default:
			return -EINVAL;
		}
		break;
	}

	/* Only Stereo modes are supported */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dai_fmt1 |= DCF_STEREO_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dai_fmt1 |= DCF_STEREO_MSB;
		break;
	case SND_SOC_DAI_FORMAT_DSP_A:
		dai_fmt1 |= DCF_PCM_SF;
		dai_fmt2 |= SLOT;
		break;
	case SND_SOC_DAI_FORMAT_DSP_B:
		dai_fmt1 |= DCF_PCM_SF;
		dai_fmt2 |= SLOT;
		break;
	default:
		return -EINVAL;
	}

	/* Only slave mode is support */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBS_CFS:
			break;
		default:
			return -EINVAL;
	}

	/* By default only 64 BICK per LRCLK is supported */
	dai_fmt1 |= DSL_32;

	snd_soc_component_update_bits(component, AU_IFF1, DCF_MASK |
					DSL_MASK | BCKP, dai_fmt1);
	snd_soc_component_update_bits(component, AU_IFF2, SLOT, dai_fmt2);

	return 0;
}

static int ak4619_dai_set_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct ak4619_priv *ak4619 = snd_soc_component_get_drvdata(component);

	ak4619->sysclk = freq;
	return 0;
}

static int ak4619_dai_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;

	snd_soc_component_update_bits(component, DAC_MF,
				DA1MUTE_EN, mute ? DA1MUTE_EN : 0);
	snd_soc_component_update_bits(component, DAC_MF,
				DA2MUTE_EN, mute ? DA2MUTE_EN : 0);

	return 0;
}

enum ak4619_operating_mode {
	PLAYBACK_MODE = 0,
	CAPTURE_MODE = 1,
};

static int ak4619_dai_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4619_priv *ak4619 = snd_soc_component_get_drvdata(component);
	bool is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	/*
	 * Data path switch control
	 *
	 * Playback Mode: change data path to SDIN1/2 pin
	 *
	 * Capture Mode: change data path to SDOUT1/2 pin
	 */

	if (ak4619->mode_gpiod)
		gpiod_set_value(ak4619->mode_gpiod, is_play ? PLAYBACK_MODE : CAPTURE_MODE);

	return 0;
}

static struct snd_soc_dai_ops ak4619_dai_ops = {
	.startup	=	ak4619_dai_startup,
	.set_sysclk	=	ak4619_dai_set_sysclk,
	.set_fmt	=	ak4619_dai_set_fmt,
	.hw_params	=	ak4619_dai_hw_params,
	.mute_stream 	=	ak4619_dai_mute,
};

#define AK4619_RATES	SNDRV_PCM_RATE_8000_192000

#define AK4619_DAC_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE |\
				 SNDRV_PCM_FMTBIT_S20_LE |\
				 SNDRV_PCM_FMTBIT_S24_LE |\
				 SNDRV_PCM_FMTBIT_S32_LE)

#define AK4619_ADC_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE |\
				 SNDRV_PCM_FMTBIT_S20_LE |\
				 SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver ak4619_dai = {
	.name = "ak4619-hifi",
	.playback = {
		.stream_name	=	"Playback",
		.channels_min	=	1,
		.channels_max	=	2,
		.rates		=	AK4619_RATES,
		.formats	=	AK4619_DAC_FORMATS,
	},
	.capture = {
		.stream_name	=	"Capture",
		.channels_min	=	1,
		.channels_max	=	2,
		.rates		=	AK4619_RATES,
		.formats	=	AK4619_ADC_FORMATS,
	},
	.ops			=	&ak4619_dai_ops,
	.symmetric_rates	=	1,
};

static int ak4619_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct ak4619_priv *ak4619;
	int ret;
	ak4619 = devm_kzalloc(dev, sizeof(struct ak4619_priv), GFP_KERNEL);
	if (!ak4619)
		return -ENOMEM;

	i2c_set_clientdata(i2c, ak4619);

	ak4619->regmap = devm_regmap_init_i2c(i2c, &ak4619_regmap_cfg);
	if (IS_ERR(ak4619->regmap)) {
		ret = PTR_ERR(ak4619->regmap);
		dev_err(dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	ak4619->pdn_gpiod = devm_gpiod_get_optional(dev, "pdn",
						GPIOD_OUT_HIGH);
	if (IS_ERR(ak4619->pdn_gpiod))
		return PTR_ERR(ak4619->pdn_gpiod);

	ak4619->mode_gpiod = devm_gpiod_get_optional(dev, "mode",
						GPIOD_OUT_LOW);
	if (IS_ERR(ak4619->mode_gpiod))
		return PTR_ERR(ak4619->mode_gpiod);

	ret = devm_snd_soc_register_component(dev, &soc_component_dev_ak4619,
				      &ak4619_dai, 1);
	if (ret < 0) {
		dev_err(dev, "Failed to register ak4619 component: %d\n",
			ret);
		return ret;
	}
	return 0;
}

static int ak4619_i2c_remove(struct i2c_client *client)
{
	return 0;
}


static struct i2c_driver ak4619_i2c_driver = {
	.driver = {
		.name = "ak4619-codec",
		.of_match_table = ak4619_of_match,
	},
	.probe		= ak4619_i2c_probe,
	.remove		= ak4619_i2c_remove,
	.id_table	= ak4619_i2c_id,
};

module_i2c_driver(ak4619_i2c_driver);

MODULE_DESCRIPTION("SoC AK4619 driver");
MODULE_AUTHOR("Khanh Le <khanh.le.xr@renesas.com>");
MODULE_LICENSE("GPL v2");
