/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/iio/dummy_adc.h>
#include <uapi/misc/emc_data.h>

#define EMC_TEMP_MODNAME			"emc-temp"

#define INTERVAL_MS			4

struct emc_temp_priv {
	struct task_struct	*in_task;
};


static void in_kthread_main(struct emc_temp_priv *priv)
{
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(INTERVAL_MS));

}

static int in_kthread(void *arg)
{
	struct emc_temp_priv *priv = (struct emc_temp_priv *)arg;

	while (!kthread_should_stop()) {
		in_kthread_main(priv);
	}

	return 0;
}

static int emc_temp_probe(struct platform_device *pdev)
{
	struct emc_temp_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;


	priv->in_task = kthread_run(in_kthread, (void *)priv, EMC_TEMP_MODNAME" in kthread");
	if (IS_ERR(priv->in_task))
		return PTR_ERR(priv->in_task);

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int emc_temp_remove(struct platform_device *pdev)
{
	struct emc_temp_priv *priv = platform_get_drvdata(pdev);

	kthread_stop(priv->in_task);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id emc_temp_of_match[] = {
	{ .compatible = "emc-temp", },
	{ }
};
MODULE_DEVICE_TABLE(of, emc_temp_of_match);
#endif

static struct platform_driver emc_temp_platform_driver = {
	.driver	= {
		.name		= EMC_TEMP_MODNAME,
		.of_match_table	= of_match_ptr(emc_temp_of_match),
	},
	.probe	= emc_temp_probe,
	.remove	= emc_temp_remove,
};
module_platform_driver(emc_temp_platform_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("emc_temp driver");
