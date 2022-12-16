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

#define B_HIGH_VOL	834
#define B_LOW_VOL	237
#define MASK_VOL	514
#define EXIT_DIALOG_MASK	600

enum vol_state {
	HIGH_VOL,
	LOW_VOL,
	MASS,
};

struct emc_ig_priv {
	struct gpio_desc	*desc;
	struct task_struct	*task;
	int			gpio_ig;
	u32			high_count;
	u32			low_count;
	u32			dia_mask_count;
	int			exit_mask;
	int			b_voltage;
	int			b_ad_voltage;
	u8			sample_count;
	u16			ig_det;
	u16			ig_det_count;
};

static int voltage_update(struct emc_ig_priv *priv)
{
	struct dummy_adc_data dat;

	dummy_adc_getdata(AD_B_AD, &dat);

	if (priv->sample_count == dat.sample_count)
		return -1;
	priv->b_ad_voltage = dat.data;
	priv->b_voltage = (((priv->b_ad_voltage * 10) / 512) + 8 ) / 10;

	/* Regist +B Voltage */
	emc_set_exp_info(B_VOLTAGE, priv->b_voltage);

	return 0;
}

static void voltage_checking(struct emc_ig_priv *priv)
{
	u32 high_vol_check = priv->high_count & 0xFFFFF;
	u32 low_vol_check = priv->low_count & 0xFFFFF;
	u32 dialog_mask_vol_check = priv->dia_mask_count & 0xFFFFF;
	u16 ig_det_detect = priv->ig_det & 0x3FF;

	if (high_vol_check == 0xFFFFF)
		emc_set_exp_info(B_HIGH_VOLTAGE_CONDITION, 1);
	else if (high_vol_check == 0)
		emc_set_exp_info(B_HIGH_VOLTAGE_CONDITION, 0);

	if (low_vol_check == 0xFFFFF)
		emc_set_exp_info(B_LOW_VOLTAGE_STAT, 1);
	else if (low_vol_check == 0)
		emc_set_exp_info(B_LOW_VOLTAGE_STAT, 0);

	if (dialog_mask_vol_check == 0xFFFFF) {
		emc_set_exp_info(DIALOG_MASK_LOW_VOLTAGE_FLAG, 1);
	}

	if(priv->exit_mask >= 600)
		emc_set_exp_info(DIALOG_MASK_LOW_VOLTAGE_FLAG, 0);

	if(ig_det_detect == 0x3FF)
	{
		priv->ig_det = 1;
		emc_set_exp_info(IG_DET_DETECTION, 1);
	}
	else if (ig_det_detect == 0)
	{
		emc_set_exp_info(IG_DET_DETECTION, 0);
		if (priv->ig_det == 1)
		{
			priv->ig_det = 0;
			emc_set_exp_info(IG_OFF_DETECTION, 1);
		}
	}
}

static void emc_ig_kthread_main(struct emc_ig_priv *priv)
{
	int ret;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(EMC_IG_INTERVAL_MS));

	ret = voltage_update(priv);
	if (ret < 0)
		return;

	if (priv->b_ad_voltage > B_HIGH_VOL) {
		priv->high_count = ( priv->high_count << 1 ) | 1;
	}else {
		priv->high_count <<= 1;
	}

	if (priv->b_ad_voltage < B_LOW_VOL) {
		priv->low_count = ( priv->low_count << 1 ) | 1;
	}else {
		priv->low_count <<= 1;
	}

	if (priv->b_ad_voltage <= MASK_VOL) {
		priv->dia_mask_count = ( priv->dia_mask_count << 1 ) | 1;
		priv->exit_mask = 0;
	}else {
		priv->dia_mask_count <<= 1;
		if (priv->exit_mask <= 600)
			priv->exit_mask++;
	}

	/* get IG_DET value */
	priv->gpio_ig = gpiod_get_value(priv->desc);

	if (priv->gpio_ig)
		priv->ig_det_count = ( priv->ig_det << 1 ) | 1;
	else
		priv->ig_det_count <<= 1;

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

	/* Init data */
	priv->gpio_ig = gpiod_get_value(priv->desc);
	priv->high_count	= 0;
	priv->low_count		= 0;
	priv->dia_mask_count= 0;
	priv->sample_count		= 0;
	priv->ig_det = 0;
	priv->ig_det_count = 0;

	/* Init value */
	emc_set_exp_info(B_VOLTAGE, 0);
	emc_set_exp_info(B_HIGH_VOLTAGE_CONDITION, 0);
	emc_set_exp_info(B_LOW_VOLTAGE_STAT, 0);
	emc_set_exp_info(DIALOG_MASK_LOW_VOLTAGE_FLAG, 1);
	emc_set_exp_info(IG_DET_DETECTION, 0);
	emc_set_exp_info(IG_OFF_DETECTION, 0);

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
