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

#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>

#define NUMBERS_CHANEL 18
#define POLY	(0x11D << 7)/* polynomial x^8 + x^4 + x^3 + x^2 + 1 */

static struct task_struct *read_thread;
static struct mutex buf_lock;

struct adc_priv {
	dev_t			devt;
	struct spi_device	*spi;
	u32			speed_hz;
};

static int adc_data[NUMBERS_CHANEL + 1] = {0};

/*-------------------------------------------------------------------------*/
static u32 dummy_adc_read_u32(struct adc_priv *priv)
{
	struct spi_transfer xfer = {
		.len = 4,
		.cs_change = 0,
		.bits_per_word = 24,
		.speed_hz = 500000,
	};
	struct spi_message msg;
	u32 read_data = 0;

	xfer.rx_buf = &read_data;
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	spi_sync(priv->spi, &msg);

	return read_data;
}

static u8 dummy_adc_calc_crc8(u16 data) {
	int i;

	for (i = 0; i < 8; i++) {
		if (data & 0x8000)
			data = data ^ POLY;
		data = data << 1;
	}
	return (u8)(data >> 8);
}

static bool crc_check(u16 data, u8 crc) {
	/* skip crc check temporary */
	return true;
}

static int dummy_adc_rawdata_process(struct adc_priv *priv, u32 rawdata) {
	u16 data;
	u8 sensor_id, sensor_data, crc;

	/* Check Start bit */
	if (!(rawdata & BIT(23)))
		return -EIO;

	/* Check CRC */
	data = (rawdata >> 8) & 0xFFFF;
	crc = rawdata & 0xFF;
	if (crc_check(data, crc))
		return -EIO;

	/* Update data */
	sensor_id = (data >> 10) & 0x1F;
	sensor_data = data & 0x3FF;

	mutex_lock(&buf_lock);
	adc_data[sensor_id] = sensor_data;
	mutex_unlock(&buf_lock);

	return 0;
}

/*-------------------------------------------------------------------------*/
static int dummy_adc_reading_thread(void *pv)
{
	struct adc_priv	*priv = pv;
	u32 val;

	while(!kthread_should_stop())
	{
		val = dummy_adc_read_u32(priv);
		dummy_adc_rawdata_process(priv, val);
		msleep(1000);
	}
	return 0;
}

/*-------------------------------------------------------------------------*/
int dummy_adc_getdata(int channel)
{
	int output = 0;

	if (channel < 0 || channel > NUMBERS_CHANEL)
		return -1;
	mutex_lock(&buf_lock);
	output = adc_data[channel];
	mutex_unlock(&buf_lock);

	return output;
}

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
