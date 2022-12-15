// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Simple ADC driver using spi interface
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/acpi.h>

#include <linux/spi/spi.h>
#include <linux/iio/dummy_adc.h>
#include <uapi/misc/emc_data.h>

#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>

#define NUM_CHAN 18
#define POLY	0x1D	/* polynomial x^8 + x^4 + x^3 + x^2 + 1 */

#define VREFR_1_AD_LOW		758
#define VREFP_3_4_AD_HIGH	776
#define VREFP_3_4_AD_LOW	503
#define VREFP_1_2_AD_HIGH	648
#define VREFP_1_2_AD_LOW	375
#define VREFP_1_4_AD_HIGH	520
#define VREFP_1_4_AD_LOW	247
#define VREFP_0_AD_HIGH		264

static struct task_struct *read_thread;
static struct mutex buf_lock;

struct adc_priv {
	dev_t			devt;
	struct spi_device	*spi;
	u32			speed_hz;
	u8			crc_table[256];
	u8			ad_error;
	u8			spi_error;
	u8			self_check;
};

static int adc_data[NUM_CHAN + 1] = {0};

/*-------------------------------------------------------------------------*/
static u32 dummy_adc_read_u32(struct adc_priv *priv)
{
	struct spi_transfer xfer = {
		.len = 4,
		.cs_change = 0,
		.bits_per_word = 24,
		.speed_hz = priv->speed_hz,
	};
	struct spi_message msg;
	u32 read_data = 0;

	xfer.rx_buf = &read_data;
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	spi_sync(priv->spi, &msg);

	return read_data;
}

static void dummy_adc_crc_table_init(struct adc_priv *priv) {
	u8 crc, bit;
	int i;

	for (i = 0; i < 256; i++) {
		crc = i;

		for (bit = 0; bit < 8; bit++) {
			crc = (crc & 0x80) ? ((crc << 1) ^ 0x1D) : (crc << 1);
		}

		priv->crc_table[i] = crc;
	}
}

static u8 dummy_adc_calc_crc8(struct adc_priv *priv, u16 val) {
	u8 crc = 0xFF;

	crc = priv->crc_table[crc ^ ((val >> 8) & 0xFF)];
	crc = priv->crc_table[crc ^ (val & 0xFF)];

	return ~(crc & 0xFF);
}

static bool crc_check_error(struct adc_priv *priv, u16 data, u8 crc) {
	if (crc != dummy_adc_calc_crc8(priv, data))
		return true;
	return false;
}

static int dummy_adc_rawdata_process(struct adc_priv *priv, u32 rawdata) {
	u16 data;
	u8 sensor_id, crc;
	u16 sensor_data;

	/* Check Start bit */
	if (rawdata == 0) {
		priv->ad_error++;
		emc_set_exp_info(EX_AD_SPI_COM_EXP_NVM, 1);
		return -EIO;
	}

	/* Check CRC */
	data = (rawdata >> 8) & 0xFFFF;
	crc = rawdata & 0xFF;
	if (crc_check_error(priv, data, crc)) {
		priv->ad_error++;
		emc_set_exp_info(EX_AD_SPI_COM_EXP_NVM, 1);
		return -EIO;
	}

	/* Update data */
	sensor_id = (data >> 10) & 0x1F;
	sensor_data = data & 0x3FF;
	adc_data[sensor_id] = sensor_data;

	return 0;
}

static void error_checking(struct adc_priv *priv)
{

#if 0
	/* Checking SPI trasnfer status error */
	if (priv->spi_error >= 5)
		emc_set_exp_info(EX_AD_TRAN_DUMMY_EXP_NVM, 1);
#endif

	/* Checking ADC data status error */
	if (priv->ad_error >= 10)
		emc_set_exp_info(EX_AD_TRAN_EXP_NVM, 1);
}

/*-------------------------------------------------------------------------*/
static int dummy_adc_reading_thread(void *pv)
{
	struct adc_priv	*priv = pv;
	u32 raw_data[NUM_CHAN+1] = {0};
	int i;

	while(!kthread_should_stop())
	{
		for (i = 1; i <= NUM_CHAN; i++) {
			raw_data[i] = dummy_adc_read_u32(priv);
			udelay(100);
		}
		msleep(5);

		mutex_lock(&buf_lock);
		for (i = 1; i <= NUM_CHAN; i++)
			dummy_adc_rawdata_process(priv, raw_data[i]);

		if (priv->self_check++ >= 10) {
			if (adc_data[VREFP_1_AD] < VREFR_1_AD_LOW) {
				emc_set_exp_info(EX_AD_SPI_COM_EXP_NVM, 1);
				priv->ad_error++;
			}

			if (adc_data[VREFP_3_4_AD] < VREFP_3_4_AD_LOW ||
				adc_data[VREFP_3_4_AD] > VREFP_3_4_AD_HIGH) {
				emc_set_exp_info(EX_AD_SPI_COM_EXP_NVM, 1);
				priv->ad_error++;
			}

			if (adc_data[VREFP_1_2_AD] < VREFP_1_2_AD_LOW ||
				adc_data[VREFP_1_2_AD] > VREFP_1_2_AD_HIGH) {
				emc_set_exp_info(EX_AD_SPI_COM_EXP_NVM, 1);
				priv->ad_error++;
			}

			if (adc_data[VREFP_1_4_AD] < VREFP_1_4_AD_LOW ||
				adc_data[VREFP_1_4_AD] > VREFP_1_4_AD_HIGH) {
				emc_set_exp_info(EX_AD_SPI_COM_EXP_NVM, 1);
				priv->ad_error++;
			}

			if (adc_data[VREFP_0_AD] > VREFP_0_AD_HIGH) {
				emc_set_exp_info(EX_AD_SPI_COM_EXP_NVM, 1);
				priv->ad_error++;
			}

			priv->self_check = 0;
		}
		mutex_unlock(&buf_lock);

		error_checking(priv);
	}
	return 0;
}

/*-------------------------------------------------------------------------*/
int dummy_adc_getdata(int channel, struct dummy_adc_data *dat)
{
	int ret;

	if (channel < 0 || channel > NUM_CHAN)
		return -1;
	mutex_lock(&buf_lock);
	dat->data = adc_data[channel];
	mutex_unlock(&buf_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dummy_adc_getdata);

/*-------------------------------------------------------------------------*/
static int dummy_adc_probe(struct spi_device *spi)
{
	struct adc_priv	*priv;

	/* Allocate driver data */
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Initialize the driver data */
	priv->spi = spi;
	mutex_init(&buf_lock);

	priv->speed_hz = spi->max_speed_hz;

	dummy_adc_crc_table_init(priv);

	spi_set_drvdata(spi, priv);

	/* Thread trigger */
	read_thread = kthread_create(dummy_adc_reading_thread,(void *)priv,"ADC Reading Thread");
	if(read_thread) {
		wake_up_process(read_thread);
	} else {
		pr_err("Cannot create kthread\n");
		goto failed;
	}

	return 0;
failed:
	kfree(priv);
	return -1;
}

static int dummy_adc_remove(struct spi_device *spi)
{
	struct adc_priv	*priv = spi_get_drvdata(spi);

	kfree(priv);

	return 0;
}

static const struct of_device_id adc_id[] = {
	{ .compatible = "dummy,adc" },
	{},
};
MODULE_DEVICE_TABLE(of, adc_id);

static struct spi_driver adc_spi_driver = {
	.driver = {
		.name =		"adc converter",
		.of_match_table = of_match_ptr(adc_id),
	},
	.probe =	dummy_adc_probe,
	.remove =	dummy_adc_remove,
};

module_spi_driver(adc_spi_driver);

MODULE_DESCRIPTION("Analog Digital Converter driver");
MODULE_LICENSE("GPL");
