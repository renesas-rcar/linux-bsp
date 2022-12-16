#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/iio/dummy_adc.h>
#include <uapi/misc/emc_data.h>

#define EMC_HT_MODNAME			"emc-ht"

#define INTERVAL_MS			4					// ヒータ制御(入力確認)スレッドの起動周期(ms)

#define ON_PIN_VOL_THRESHOLD		7					// ON異常:端子電圧条件の閾値(0.<ON_PIN_VOL_THRESHOLD>)
#define ON_ERR_THRESHOLD_CNT		625					// ON異常:判定の閾値(回数)

#define OFF_PIN_VOL_THRESHOLD		3					// OFF異常:端子電圧条件の閾値(0.<OFF_PIN_VOL_THRESHOLD>)
#define OFF_ERR_THRESHOLD_CNT		625					// OFF異常:判定の閾値(回数)

#define SW_THRESHOLD_UPPER		769
#define SW_THRESHOLD_LOWER		401

#define GET_IN_STATE_TIME		4
#define GET_IN_STATE_MASK		GENMASK(3, 0)
#define GET_IN_STATE_HIGH		GENMASK(3, 0)
#define GET_IN_STATE_LOW		0

struct emc_ht_priv {
	int			pin_vol;	// 端子電圧 (〔AD_BZ_A/D値(×10)〕÷〔AD_+B_A/D値(x10)〕)
	int			on_err;		// ON異常回数
	int			off_err;	// OFF異常回数
	struct gpio_desc	*out_desc;
	int			out_now;	// ヒータ制御出力値
	struct task_struct	*in_task;
	int			in_old;		// 〔じか線ヒータ駆動制御〕(前回値)
	int			in_now;		// 〔じか線ヒータ駆動制御〕(今回値)
	int			htr_enable;	// 〔HTR_ENABLE〕
};

static void set_ht_ctl_err(unsigned short val)
{
	int ret;

	// 《じか線ヒータ制御異常》= val
	ret = emc_set_exp_info(HTR_CONTROL_ERROR, val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
}

static unsigned short get_in_now(void)
{
	struct dummy_adc_data dat;
	static unsigned short val = 0;
	static unsigned short temp_val = 0;
	static u8 sample;
	int ret;

	// 〔じか線ヒータ駆動制御〕の値を取得する。
	// 「じか線ヒータ駆動制御〕＝〔じか線PCS_SW状態〕なので
	// 〔じか線PCS_SW状態〕を取得すれば良い。
	ret = emc_get_exp_info(PCS_SW_STAT, &val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	/*
	 *  Just need to care only PCS_SW state
	 *  PCS_SW = 1 --> Turn on heater
	 *  PCS_SW = 0 --> Turn off heater
	 */

	dummy_adc_getdata(AD_PCS_SW_AD, &dat);
	if (dat.sample_count != sample) {

		if (dat.data <= SW_THRESHOLD_LOWER)
			temp_val = (temp_val << 1) | 1;
		else
			temp_val = temp_val << 1;

		if ((temp_val & GET_IN_STATE_MASK) == GET_IN_STATE_HIGH)
			val = 1;
		else if ((temp_val & GET_IN_STATE_MASK) == GET_IN_STATE_LOW)
			val = 0;

		sample = dat.sample_count;
	}

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
	return val;
}

static unsigned short get_htr_enable(void)
{
	int ret;
	unsigned short val = 0;

	// 〔HTR_ENABLE〕を取得
	ret = emc_get_exp_info(HTR_ENABLE, &val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
	return val;
}

static int get_ad_ht(void)
{
	struct dummy_adc_data dat;
	int ret;

	// 〔AD_BZ_A/D値〕を取得
	ret = dummy_adc_getdata(AD_HEAT_AD, &dat);
	if (ret)
		return ret;

	return dat.data;
}

static int get_ad_pb(void)
{
	struct dummy_adc_data dat;

	// 〔AD_+B_A/D値〕を取得
	dummy_adc_getdata(AD_B_AD, &dat);

	return dat.data;
}

static void update_pin_vol_condition(struct emc_ht_priv *priv)
{
	int ad_ht;
	int ad_pb;

	// 端子電圧の更新
	// ＜メモ＞「* 10」は小数を無くすためにある
	ad_ht = get_ad_ht() * 10;
	ad_pb = get_ad_pb();
	if (ad_pb != 0) {
		priv->pin_vol = ad_ht / ad_pb;
	} else {
		priv->pin_vol = 0;
	}
}

static void check_on_error(struct emc_ht_priv *priv)
{
	// 〔じか線ヒータ駆動制御〕が 0(非駆動) ？
	if (priv->out_now == 0) {
		return;
	}

	// 〔HTR_ENABLE〕が 0 ？
	if (priv->htr_enable == 0) {
		return;
	}

	// 端子電圧条件 (〔AD_BZ_A/D値〕÷〔AD_+B_A/D値〕が閾値以下) を満たしている？
	if (priv->pin_vol <= ON_PIN_VOL_THRESHOLD) {
		// ON異常回数の更新
		if (priv->on_err < INT_MAX) {
			priv->on_err++;
		}
		// 回数条件を満たしている？
		if (priv->on_err == ON_ERR_THRESHOLD_CNT) {
			// 《じか線ヒータ制御異常》= 1
			set_ht_ctl_err(1);
		}

	// 端子電圧条件 (〔AD_BZ_A/D値〕÷〔AD_+B_A/D値〕が閾値以下) を満たしていない？
	} else {
		// ON異常回数をリセット
		priv->on_err = 0;
	}
}

static void check_off_error(struct emc_ht_priv *priv)
{
	// 〔じか線ヒータ駆動制御〕が 1(駆動) ？
	if (priv->out_now != 0) {
		return;
	}

	// 〔HTR_ENABLE〕が 1 ？
	if (priv->htr_enable != 0) {
		return;
	}

	// 端子電圧条件 (〔AD_BZ_A/D値〕÷〔AD_+B_A/D値〕が閾値以上) を満たしている？
	if (priv->pin_vol >= OFF_PIN_VOL_THRESHOLD) {
		// OFF異常回数の更新
		if (priv->off_err < INT_MAX) {
			priv->off_err++;
		}
		// 回数条件を満たしている？
		if (priv->off_err == OFF_ERR_THRESHOLD_CNT) {
			// 《じか線ヒータ制御異常》= 1
			set_ht_ctl_err(ABNORMAL);
		}

	// 端子電圧条件 (〔AD_BZ_A/D値〕÷〔AD_+B_A/D値〕が閾値以上) を満たしていない？
	} else {
		// OFF異常回数をリセット
		priv->off_err = 0;
	}
}

static void check_error(struct emc_ht_priv *priv)
{
	// 端子電圧条件の更新
	update_pin_vol_condition(priv);

	// ON異常判定
	check_on_error(priv);

	// OFF異常判定
	check_off_error(priv);
}

static void in_kthread_main(struct emc_ht_priv *priv)
{
	static int htr_enable_old = 0;

	// 少し待つ
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(INTERVAL_MS));

	// 〔じか線ヒータ駆動制御〕を取得
	priv->in_now = get_in_now();

	// 〔HTR_ENABLE〕を取得
	priv->htr_enable = get_htr_enable();

	// 異常判定
	check_error(priv);

	// 〔じか線ヒータ駆動制御〕が 1 →  0 ？
	if (priv->in_old != 0 && priv->in_now == 0) {
		// ON異常回数をリセット
		priv->on_err = 0;
	}

	if (htr_enable_old != priv->htr_enable) {
		gpiod_set_value(priv->out_desc, priv->htr_enable);
		htr_enable_old = !!priv->htr_enable;
		priv->out_now = !!priv->htr_enable;
	} else if (priv->in_now != priv->out_now) {
		gpiod_set_value(priv->out_desc, priv->in_now);
		priv->out_now = priv->in_now;
	}
}

// ヒータ制御(入力確認)スレッド
static int in_kthread(void *arg)
{
	struct emc_ht_priv *priv = (struct emc_ht_priv *)arg;

	// 〔じか線ヒータ駆動制御〕を取得
	priv->in_old = get_in_now();
	priv->out_now = 0;

	// 〔HTR_ENABLE〕を取得
	priv->htr_enable = get_htr_enable();

	while (!kthread_should_stop()) {
		in_kthread_main(priv);
	}

	return 0;
}

static int emc_ht_probe(struct platform_device *pdev)
{
	struct emc_ht_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	// get gpio desc
	priv->out_desc = devm_gpiod_get(&pdev->dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(priv->out_desc))
		return PTR_ERR(priv->out_desc);

	// start kthread (in)
	priv->in_task = kthread_run(in_kthread, (void *)priv, EMC_HT_MODNAME" in kthread");
	if (IS_ERR(priv->in_task))
		return PTR_ERR(priv->in_task);

	// set private data
	platform_set_drvdata(pdev, priv);

	return 0;
}

static int emc_ht_remove(struct platform_device *pdev)
{
	struct emc_ht_priv *priv = platform_get_drvdata(pdev);

	// stop kthread
	kthread_stop(priv->in_task);

	// unset private data
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id emc_ht_of_match[] = {
	{ .compatible = "emc-ht", },
	{ }
};
MODULE_DEVICE_TABLE(of, emc_ht_of_match);
#endif

static struct platform_driver emc_ht_platform_driver = {
	.driver	= {
		.name		= EMC_HT_MODNAME,
		.of_match_table	= of_match_ptr(emc_ht_of_match),
	},
	.probe	= emc_ht_probe,
	.remove	= emc_ht_remove,
};
module_platform_driver(emc_ht_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("aaa <bbb@xxx.yyy.zzz>");
MODULE_DESCRIPTION("emc_ht driver");
