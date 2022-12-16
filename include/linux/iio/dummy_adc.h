/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __DUMMY_ADC_H__
#define __DUMMY_ADC_H__

struct dummy_adc_data {
	u16 data;
	u8 crc;
	u8 sample_count;
};

enum dummy_adc_channel {
	AD_B_AD = 1,		/* channel 1  */
	AD_LDA_ACC_SW_AD,	/* channel 2  */
	AD_PCS_SW_AD,		/* channel 3  */
	V4M_THER_AD,		/* channel 4  */
	DSM_VOL_AD,			/* channel 5  */
	AIN6_AD,			/* channel 6  */
	IMAGE_THER_AD,		/* channel 7  */
	AD_HEAT_AD,			/* channel 8  */
	AD_BZ_AD,			/* channel 9  */
	AD_3R3V_AD,			/* channel 10 */
	V4M_THER_AMP_AD,	/* channel 11 */
	AIN6_AMP_AD,		/* channel 12 */
	IMAGE_THER_AMP_AD,	/* channel 13 */
	VREFP_1_AD,			/* channel 14 */
	VREFP_3_4_AD,		/* channel 15 */
	VREFP_1_2_AD,		/* channel 16 */
	VREFP_1_4_AD,		/* channel 17 */
	VREFP_0_AD,			/* channel 18 */
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
