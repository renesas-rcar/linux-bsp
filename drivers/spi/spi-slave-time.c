/*
 * SPI slave handler reporting boot up time
 *
 * This SPI slave handler sends the time of reception of the last SPI message
 * as two 32-bit unsigned integers in binary format and in network byte order,
 * representing the number of seconds and fractional seconds (in microseconds)
 * since boot up.
 *
 * Copyright (C) 2016 Glider bvba
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spi/spi.h>


struct spi_slave_time_priv {
	struct spi_device *spi;
	struct task_struct *thread;
};

static int spi_slave_time_send(struct spi_device *spi)
{
	__be32 msg[2];
	u32 rem_ns;
	u64 ts;

	ts = local_clock();
	rem_ns = do_div(ts, 1000000000) / 1000;

	msg[0] = cpu_to_be32(ts);
	msg[1] = cpu_to_be32(rem_ns);

	return spi_write(spi, &msg, sizeof(msg));
}

static int spi_slave_time_thread(void *data)
{
	struct spi_slave_time_priv *priv = data;
	int error;

	while (!kthread_should_stop()) {
		error = spi_slave_time_send(priv->spi);
		if (error)
			pr_err("%s: SPI transfer failed %d\n", __func__, error);
	}

	return 0;
}

static int spi_slave_time_probe(struct spi_device *spi)
{
	struct spi_slave_time_priv *priv;
	int ret;

	/*
	 * bits_per_word cannot be configured in platform data
	 */
	spi->bits_per_word = 8;

	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi = spi;
	priv->thread = kthread_run(spi_slave_time_thread, priv,
				   "spi-slave-time/%s", dev_name(&spi->dev));
	if (IS_ERR(priv->thread))
		return PTR_ERR(priv->thread);

	spi_set_drvdata(spi, priv);
	return 0;
}

static int spi_slave_time_remove(struct spi_device *spi)
{
	struct spi_slave_time_priv *priv = spi_get_drvdata(spi);

	/* FIXME Doesn't work, as spi_write() is blocked on a completion */
	kthread_stop(priv->thread);
	return 0;
}

static struct spi_driver spi_slave_time_driver = {
	.driver = {
		.name	= "spi-slave-time",
	},
	.probe		= spi_slave_time_probe,
	.remove		= spi_slave_time_remove,
};
module_spi_driver(spi_slave_time_driver);

MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_DESCRIPTION("SPI slave reporting boot up time");
MODULE_LICENSE("GPL v2");
