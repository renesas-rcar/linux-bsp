/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SPI_ADCONVERTER_H__
#define __SPI_ADCONVERTER_H__

#if IS_ENABLED(CONFIG_SPI_ADC)
int adc_spi_getdata(int channel);

#else
static inline int adc_spi_getdata(int channel)
{
	return -ENOSYS;
}

#endif /* CONFIG_SPI_ADC */

#endif /* __SPI_ADCONVERTER_H__ */
