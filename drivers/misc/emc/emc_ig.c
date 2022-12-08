#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/kthread.h>

#define EMC_IG_MODNAME		"emc-ig"
#define EMC_IG_INTERVAL_MS	5
#define EMC_IG_CONSECUTIVE	3


struct emc_ig_priv {
	struct gpio_desc	*desc;
	struct task_struct	*task;
	int			old;
	int			now;
	int			cnt;
	int			ig_det;
	int			ig_off;
};

static void emc_ig_kthread_main(struct emc_ig_priv *priv)
{
	int			old_ig_det;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(EMC_IG_INTERVAL_MS));

	// get IG_DET value
	priv->now = gpiod_get_value(priv->desc);

	// IG_DET value changed?
	if (priv->old != priv->now) {
		// reset IG_DET detection counts
		priv->cnt = 1;
	} else {
		// update IG_DET detection counts
		if (priv->cnt < INT_MAX) {
			priv->cnt++;
		}
	}

	// update old IG_DET value
	priv->old = priv->now;

	// IG_DET detection condition not met?
	if (priv->cnt != EMC_IG_CONSECUTIVE) {
		return;
	}

	// update variable IG_DET judgment
	old_ig_det = priv->ig_det;
	priv->ig_det = priv->now;
	pr_debug("IG_DET = %d\n", priv->ig_det);
	// FIXME : memory write

	// IG_OFF detected? (IG_DET 1 -> 0)
	if (old_ig_det == 1 && priv->ig_det == 0) {
		priv->ig_off = 1;
		pr_debug("IG_OFF = %d\n", priv->ig_off);
		// FIXME : memory write
	}
}

static int emc_ig_kthread(void *arg)
{
	struct emc_ig_priv	*priv = (struct emc_ig_priv *)arg;

	while (!kthread_should_stop()) {
		emc_ig_kthread_main(priv);
	}

	return 0;
}

static int emc_ig_probe(struct platform_device *pdev)
{
	struct emc_ig_priv	*priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	// get gpio desc
	priv->desc = devm_gpiod_get(&pdev->dev, NULL, GPIOD_IN);
	if (IS_ERR(priv->desc))
		return PTR_ERR(priv->desc);

	// get 1st data
	priv->old = gpiod_get_value(priv->desc);
	priv->cnt = 1;

	// start kthread
	priv->task = kthread_run(emc_ig_kthread, (void *)priv, EMC_IG_MODNAME" kthread");
	if (IS_ERR(priv->task))
		return PTR_ERR(priv->task);

	// set private data
	platform_set_drvdata(pdev, priv);

	return 0;
}

static int emc_ig_remove(struct platform_device *pdev)
{
	struct emc_ig_priv	*priv = platform_get_drvdata(pdev);

	// stop kthread
	kthread_stop(priv->task);

	// unset private data
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id emc_ig_of_match[] = {
	{ .compatible = "emc-ig", },
	{ }
};
MODULE_DEVICE_TABLE(of, emc_ig_of_match);
#endif

static struct platform_driver emc_ig_platform_driver = {
	.driver	= {
		.name		= EMC_IG_MODNAME,
		.of_match_table	= of_match_ptr(emc_ig_of_match),
	},
	.probe	= emc_ig_probe,
	.remove	= emc_ig_remove,
};
module_platform_driver(emc_ig_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("aaa <bbb@xxx.yyy.zzz>");
MODULE_DESCRIPTION("emc_ig driver");
