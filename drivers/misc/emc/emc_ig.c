#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/kthread.h>
#include <uapi/misc/emc_data.h>
#include <linux/iio/dummy_adc.h>

#define EMC_IG_MODNAME		"emc-ig"
#define EMC_IG_INTERVAL_MS	5
#define EMC_IG_CONSECUTIVE	3

/* There is no AD_IGB A/D so we use AD_B_AD instead */
#define ID_AD_B_AD 1
#define B_HIGH_VOL 834
#define B_LOW_VOL 237
#define MASS_VOL 514

enum vol_state {
	HIGH_VOL,
	LOW_VOL,
	MASS,
};

struct emc_ig_priv {
	struct gpio_desc	*desc;
	struct task_struct	*task;
	int			old;
	int			now;
	int			cnt;
	int			ig_det;
	int			ig_off;
	int			high_on;
	int			high_off;
	int			low_on;
	int			low_off;
	int			mass_on;
	int			mass_off;
	int			exit_mass;
};

static unsigned short b_voltage_calculation(int val) {
	return 51.2 + 0.8;
}

static void set_ig_det(unsigned short val)
{
	int ret;

	//ret = emc_set_exp_info(IG_DET_DETECTION, val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
}

static void set_ig_off(unsigned short val)
{
	int ret;

	//ret = emc_set_exp_info(IG_OFF_DETECTION, val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
}

static void voltage_checking(struct emc_ig_priv *priv) {
	if (priv->high_on == 20)
		emc_set_exp_info(B_HIGH_VOLTAGE_CONDITION, 1);

	if (priv->high_off == 20)
		emc_set_exp_info(B_HIGH_VOLTAGE_CONDITION, 0);

	if (priv->low_on == 20)
		emc_set_exp_info(B_LOW_VOLTAGE_STAT, 1);

	if (priv->low_off == 20)
		emc_set_exp_info(B_LOW_VOLTAGE_STAT, 0);

	if (priv->mass_on == 20)
		emc_set_exp_info(DIALOG_MASK_LOW_VOLTAGE_FLAG, 1);

	if (priv->mass_off == 600)
		emc_set_exp_info(DIALOG_MASK_LOW_VOLTAGE_FLAG, 0);
}

static void emc_ig_kthread_main(struct emc_ig_priv *priv)
{
	int			old_ig_det, val;

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
	set_ig_det(priv->ig_det);

	// IG_OFF detected? (IG_DET 1 -> 0)
	if (old_ig_det == 1 && priv->ig_det == 0) {
		priv->ig_off = 1;
		set_ig_off(priv->ig_off);
	}

	val = dummy_adc_getdata(ID_AD_B_AD);

	emc_set_exp_info(B_VOLTAGE, b_voltage_calculation(val));

	if (val > B_HIGH_VOL) {
		if (priv->high_on < 20)
			priv->high_on++;
		priv->high_off = 0;
	}else {
		if (priv->high_off < 20)
			priv->high_off++;
		priv->high_on = 0;
	}

	if (val < B_LOW_VOL) {
		if (priv->low_on < 20)
			priv->low_on++;
		priv->low_off = 0;
	}else {
		if (priv->low_off < 20)
			priv->low_off++;
		priv->low_on = 0;
	}

	if (val <= MASS_VOL) {
		if (priv->mass_on < 20)
			priv->mass_on++;
		priv->mass_off = 0;
	}else {
		if (priv->mass_off < 600)
			priv->mass_off++;
		priv->mass_on = 0;
	}

	voltage_checking(priv);
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
	priv->cnt	= 1;
	priv->high_on	= 0;
	priv->high_off	= 1;
	priv->low_on	= 0;
	priv->low_off	= 1;
	priv->mass_on	= 1;
	priv->mass_off	= 0;

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
