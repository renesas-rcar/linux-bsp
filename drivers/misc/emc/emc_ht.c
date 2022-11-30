#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/kthread.h>
#include <linux/sched.h>

#define EMC_HT_MODNAME			"emc-ht"

#define INTERVAL_MS			4					// ヒータ制御(入力確認)スレッドの起動周期(ms)

#define ON_PIN_VOL_THRESHOLD		7					// ON異常:端子電圧条件の閾値(0.<ON_PIN_VOL_THRESHOLD>)
#define ON_ERR_THRESHOLD_CNT		625					// ON異常:判定の閾値(回数)

#define OFF_PIN_VOL_THRESHOLD		3					// OFF異常:端子電圧条件の閾値(0.<OFF_PIN_VOL_THRESHOLD>)
#define OFF_ERR_THRESHOLD_CNT		625					// OFF異常:判定の閾値(回数)


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

static void set_ht_ctl_err(int val)
{
	// 《じか線ヒータ制御異常》= val
	// FIXME
	pr_debug("%s: val = %d\n", __func__, val);
}

static int get_in_now(void)
{
	int val = 0;

	// 〔じか線ヒータ駆動制御」の値を取得する。
	// 「じか線ヒータ駆動制御」＝〔じか線PCS_SW状態〕なので
	// 〔じか線PCS_SW状態〕を取得すれば良い。
	// FIXME

	return val;
}

static int get_htr_enable(void)
{
	int val = 0;

	// 〔HTR_ENABLE〕を取得
	// FIXME

	return val;
}

static int get_ad_bz(void)
{
	int val = 0;

	// 〔AD_BZ_A/D値〕を取得
	// FIXME

	return val;
}

static int get_ad_pb(void)
{
	int val = 0;

	// 〔AD_+B_A/D値〕を取得
	// FIXME

	return val;
}

static void update_pin_vol_condition(struct emc_ht_priv *priv)
{
	int ad_bz;
	int ad_pb;

	// 端子電圧の更新
	// ＜メモ＞「* 10」は小数を無くすためにある
	ad_bz = get_ad_bz() * 10;
	ad_pb = get_ad_pb() * 10;
	// remove calculating 0 / 0
//	priv->pin_vol = ad_bz / ad_pb;
}

static void check_on_error(struct emc_ht_priv *priv)
{
	// 〔じか線ヒータ駆動制御〕が 0(非駆動) ？
	if (priv->in_now == 0) {
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
	if (priv->in_now != 0) {
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
			set_ht_ctl_err(1);
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

	// 〔じか線ヒータ駆動制御〕が 0 →  1 ？
	if (priv->in_old == 0 && priv->in_now != 0) {
		// OFF異常回数をリセット
		priv->off_err = 0;
	}

	// 〔じか線ヒータ駆動制御〕が 0、または、〔HTR_ENABLE〕が 0 ?
	if (priv->in_now == 0 || priv->htr_enable == 0) {
		// ヒータ制御がH？
		if (priv->out_now != 0) {
			// ヒータ制御をLにする
			priv->out_now = 0;
			gpiod_set_value(priv->out_desc, 0);
		}
	}

	// 〔じか線ヒータ駆動制御〕が 1、かつ、〔HTR_ENABLE〕が 1 ?
	if (priv->in_now != 0 && priv->htr_enable != 0) {
		// ヒータ制御がL？
		if (priv->out_now == 0) {
			// ヒータ制御をHにする
			priv->out_now = 1;
			gpiod_set_value(priv->out_desc, 1);
		}
	}

	// 〔じか線ヒータ駆動制御〕を保存
	priv->in_old = priv->in_now;
}

// ヒータ制御(入力確認)スレッド
static int in_kthread(void *arg)
{
	struct emc_ht_priv *priv = (struct emc_ht_priv *)arg;

	// 〔じか線ヒータ駆動制御〕を取得
	priv->in_old = get_in_now();

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
