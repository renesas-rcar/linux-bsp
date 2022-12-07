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
#include <linux/spi/spi-adconverter.h>

#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>

#define NUMBERS_CHANEL 18

static struct task_struct *read_thread;
static struct mutex buf_lock;

struct adc_data {
	dev_t			devt;
	spinlock_t		spi_lock;
	struct spi_device	*spi;
	u8			*tx_buffer;
	u8			*rx_buffer;
	u32			speed_hz;
};

static unsigned bufsiz = 4096;
static int data[NUMBERS_CHANEL] = {0};

/*-------------------------------------------------------------------------*/
//READ function

/*-------------------------------------------------------------------------*/
//THREAD function

static int adc_spi_reading_thread(void *pv)
{
	int i;

	while(!kthread_should_stop())
	{
		mutex_lock(&buf_lock);
		for(i = 0; i < NUMBERS_CHANEL; i++)
		{
			data[i] += i;
		}
		mutex_unlock(&buf_lock);
		msleep(1000);
	}
	return 0;
}

/*-------------------------------------------------------------------------*/
// GET data
int adc_spi_getdata(int channel)
{
	int output = 0;

	if (channel < 0 || channel > NUMBERS_CHANEL)
		return -1;
	mutex_lock(&buf_lock);
	output = data[channel];
	mutex_unlock(&buf_lock);
	return output;
}

/*-------------------------------------------------------------------------*/

static int adc_spi_probe(struct spi_device *spi)
{
	struct adc_data	*adc;

	/* Allocate driver data */
	adc = kzalloc(sizeof(*adc), GFP_KERNEL);
	if (!adc)
		return -ENOMEM;

	/* Initialize the driver data */
	adc->spi = spi;
	spin_lock_init(&adc->spi_lock);
	mutex_init(&buf_lock);

	adc->speed_hz = spi->max_speed_hz;

	spi_set_drvdata(spi, adc);

	/* Thread trigger */
	read_thread = kthread_create(adc_spi_reading_thread,NULL,"ADC Reading Thread");
	if(read_thread) {
		wake_up_process(read_thread);
	} else {
		pr_err("Cannot create kthread\n");
		goto failed;
	}

	return 0;
failed:
	kfree(adc);
	return -1;
}

static int adc_spi_remove(struct spi_device *spi)
{
	struct adc_data	*adc = spi_get_drvdata(spi);

	kfree(adc);

	return 0;
}

static const struct of_device_id adc_id[] = {
	{ .compatible = "ad,converter" },
	{},
};
MODULE_DEVICE_TABLE(of, adc_id);

static struct spi_driver adc_spi_driver = {
	.driver = {
		.name =		"adc converter",
		.of_match_table = of_match_ptr(adc_id),
	},
	.probe =	adc_spi_probe,
	.remove =	adc_spi_remove,
};

module_spi_driver(adc_spi_driver);

MODULE_DESCRIPTION("Analog Digital Converter driver");
MODULE_LICENSE("GPL");
