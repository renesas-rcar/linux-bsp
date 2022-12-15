/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __DUMMY_ADC_H__
#define __DUMMY_ADC_H__

struct dummy_adc_data {
	u16 data;
	u8 crc;
	u8 sample_count;
};

enum dummy_adc_channel {
	AD_B_AD = 1,
	AD_LDA_ACC_SW_AD,
	AD_PCS_SW_AD,
	V4M_THER_AD,
	DSM_VOL_AD,
	AIN6_AD,
	IMAGE_THER_AD,
	AD_HEAT_AD,
	AD_BZ_AD,
	AD_3R3V_AD,
	V4M_THER_AMP_AD,
	AIN6_AMP_AD,
	IMAGE_THER_AMP_AD,
	VREFP_1_AD,
	VREFP_3_4_AD,
	VREFP_1_2_AD,
	VREFP_1_4_AD,
	VREFP_0_AD,
};

#if IS_ENABLED(CONFIG_DUMMY_ADC)
int dummy_adc_getdata(int channel, struct dummy_adc_data *data);

#else
static inline int dummy_adc_getdata(int channel, struct dummy_adc_data *data)
{
	return -ENOSYS;
}

#endif /* CONFIG_DUMMY_ADC */

#endif /* __DUMMY_ADC_H__ */
