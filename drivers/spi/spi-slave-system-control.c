/*
 * SPI slave handler controlling system state
 *
 * This SPI slave handler allows remote control of system reboot, power off,
 * halt, and suspend.
 *
 * Copyright (C) 2016 Glider bvba
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/completion.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/spi/spi.h>

/*
 * The numbers are chosen to display something human-readable on two 7-segment
 * displays connected to two 74HC595 shift registers
 */
#define CMD_REBOOT	0x507c	/* rb */
#define CMD_POWEROFF	0x3f71	/* OF */
#define CMD_HALT	0x7638	/* HL */
#define CMD_SUSPEND	0x1b1b	/* ZZ */

struct spi_slave_system_control_priv {
	struct spi_device *spi;
	struct completion finished;
	struct spi_transfer xfer;
	struct spi_message msg;
	__le16 cmd;
};

static
int spi_slave_system_control_submit(struct spi_slave_system_control_priv *priv);

static void spi_slave_system_control_complete(void *arg)
{
	struct spi_slave_system_control_priv *priv = arg;
	u16 cmd;
	int ret;

	if (priv->msg.status)
		goto terminate;

	cmd = le16_to_cpu(priv->cmd);
	switch (cmd) {
	case CMD_REBOOT:
		pr_info("Rebooting system...\n");
		kernel_restart(NULL);

	case CMD_POWEROFF:
		pr_info("Powering off system...\n");
		kernel_power_off();
		break;

	case CMD_HALT:
		pr_info("Halting system...\n");
		kernel_halt();
		break;

	case CMD_SUSPEND:
		pr_info("Suspending system...\n");
		pm_suspend(PM_SUSPEND_MEM);
		break;

	default:
		pr_warn("%s: Unknown command 0x%x\n", __func__, cmd);
		break;
	}

	ret = spi_slave_system_control_submit(priv);
	if (ret)
		goto terminate;

	return;

terminate:
	pr_info("%s: Terminating\n", __func__);
	complete(&priv->finished);
}

static
int spi_slave_system_control_submit(struct spi_slave_system_control_priv *priv)
{
	int ret;

	spi_message_init_with_transfers(&priv->msg, &priv->xfer, 1);

	priv->msg.complete = spi_slave_system_control_complete;
	priv->msg.context = priv;

	ret = spi_async(priv->spi, &priv->msg);
	if (ret)
		pr_err("%s: spi_async() failed %d\n", __func__, ret);

	return ret;
}

static int spi_slave_system_control_probe(struct spi_device *spi)
{
	struct spi_slave_system_control_priv *priv;
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
	init_completion(&priv->finished);
	priv->xfer.rx_buf = &priv->cmd;
	priv->xfer.len = sizeof(priv->cmd);

	ret = spi_slave_system_control_submit(priv);
	if (ret)
		return ret;

	spi_set_drvdata(spi, priv);
	return 0;
}

static int spi_slave_system_control_remove(struct spi_device *spi)
{
	struct spi_slave_system_control_priv *priv = spi_get_drvdata(spi);

	spi_slave_abort(spi);
	wait_for_completion(&priv->finished);
	return 0;
}

static struct spi_driver spi_slave_system_control_driver = {
	.driver = {
		.name	= "spi-slave-system-control",
	},
	.probe		= spi_slave_system_control_probe,
	.remove		= spi_slave_system_control_remove,
};
module_spi_driver(spi_slave_system_control_driver);

MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_DESCRIPTION("SPI slave handler controlling system state");
MODULE_LICENSE("GPL v2");
