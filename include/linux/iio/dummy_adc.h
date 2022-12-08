/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __DUMMY_ADC_H__
#define __DUMMY_ADC_H__

#if IS_ENABLED(CONFIG_DUMMY_ADC)
int dummy_adc_getdata(int channel);

#else
static inline int dummy_adc_getdata(int channel)
{
	return -ENOSYS;
}

#endif /* CONFIG_DUMMY_ADC */

#endif /* __DUMMY_ADC_H__ */
