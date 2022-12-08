#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/iio/dummy_adc.h>

#define EMC_BZ_MODNAME			"emc-bz"

#define INTERVAL_MS			4					// ブザー制御(入力確認)スレッドの起動周期(ms)

#define OUT_INTERVAL_MS			100					// ブザー制御周期(ms)

#define IG_VOL_THRESHOLD_MS		8					// IG電圧条件OKの閾値(ms)
#define IG_VOL_THRESHOLD		(IG_VOL_THRESHOLD_MS / INTERVAL_MS)	// IG電圧条件OKの閾値(回数)

#define OUT_H_THRESHOLD_MS		32					// ブザー制御端子H出力条件OKの閾値(ms)
#define OUT_H_THRESHOLD			(OUT_H_THRESHOLD_MS / INTERVAL_MS)	// ブザー制御端子H出力条件OKの閾値(回数)

#define ON_PIN_VOL_THRESHOLD		3					// ON異常:端子電圧条件の閾値(0.<ON_PIN_VOL_THRESHOLD>)
#define ON_ERR_THRESHOLD_CNT		21					// ON異常:判定の閾値(回数)

#define OFF_PIN_VOL_THRESHOLD		7					// OFF異常:端子電圧条件の閾値(0.<OFF_PIN_VOL_THRESHOLD>)
#define OFF_ERR_THRESHOLD_CNT		625					// OFF異常:判定の閾値(回数)

										// dummy_adc_getdata() 用ID定義
#define ID_AD_BZ			9					// AD_BZ_A/D値
#define ID_AD_PB			1					// AD_+B_A/D値


struct emc_bz_priv {
	int			ig_vol_old;	// IG電圧状態 (前回の値)
	int			ig_vol_cnt;	// IG電圧条件OK回数
	int			pin_vol;	// 端子電圧 (〔AD_BZ_A/D値(×10)〕÷〔AD_+B_A/D値(x10)〕)
	int			on_err;		// ON異常回数
	int			off_err;	// OFF異常回数
	int			out_cnt;	// ブザー制御端子H出力回数
	struct gpio_desc	*out_desc;
	struct task_struct	*out_task;
	wait_queue_head_t	out_wait;
	int			out_now;
	struct task_struct	*in_task;
	int			in_old;		// 〔じか線ブザー吹鳴制御〕(前回値)
	int			in_now;		// 〔じか線ブザー吹鳴制御〕(今回値)
};

static int get_bz_ctl_err(void)
{
	int val = 0;

	// 《じか線ブザー制御異常》を取得
	// FIXME

	pr_debug("%s: val = %d\n", __func__, val);
	return val;
}

static void set_bz_ctl_err(int val)
{
	// 《じか線ブザー制御異常》= val
	// FIXME
	pr_debug("%s: val = %d\n", __func__, val);
}

static int get_in_now(void)
{
	int val = 0;

	// 〔じか線ブザー吹鳴制御〕の値を取得する。
	// 〔じか線ブザー吹鳴制御〕＝〔じか線LDA_ACC_SW状態〕なので
	// 〔じか線LDA_ACC_SW状態〕を取得すれば良い。
	// FIXME

	pr_debug("%s: val = %d\n", __func__, val);
	return val;
}

static void out_kthread_pulse(struct emc_bz_priv *priv)
{
	// ブザー制御端子をHにする
	gpiod_set_value(priv->out_desc, 1);

	// 周期分Hを継続させる
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(OUT_INTERVAL_MS));

	// ブザー制御端子をLにする
	gpiod_set_value(priv->out_desc, 0);

	// 周期分Lを継続させる
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(OUT_INTERVAL_MS));
}

static void out_kthread_main(struct emc_bz_priv *priv)
{
	int ret;

	// ブザー制御(パルス出力)の開始を待つ
	ret = wait_event_interruptible(priv->out_wait, priv->out_now != 0);
	if (ret < 0) {
		return;
	}

	// ブザー制御(パルス出力)
	out_kthread_pulse(priv);

	// 〔じか線ブザー吹鳴制御〕が 0(非吹鳴) ？
	if (priv->in_now == 0) {
		// ブザー制御(パルス出力)を停止
		priv->out_now = 0;
	}
}

// ブザー制御(パルス出力)スレッド
static int out_kthread(void *arg)
{
	struct emc_bz_priv *priv = (struct emc_bz_priv *)arg;

	while (!kthread_should_stop()) {
		out_kthread_main(priv);
	}

	return 0;
}

static int get_ig_vol_h(void)
{
	int val = 0;

	// 〔+B高電圧状態〕を取得
	// FIXME

	pr_debug("%s: val = %d\n", __func__, val);
	return val;
}

static int get_ig_vol_l(void)
{
	int val = 0;

	// 〔+B低電圧状態〕を取得
	// FIXME

	pr_debug("%s: val = %d\n", __func__, val);
	return val;
}

// IG電圧状態の取得 (0:正常 !0:異常)
static int get_ig_vol(void)
{
	int h = get_ig_vol_h();
	int l = get_ig_vol_l();

	return (h | l);
}

static void update_ig_vol_condition(struct emc_bz_priv *priv)
{
	int ig_vol_now;

	// IG電圧状態を取得
	ig_vol_now = get_ig_vol();

	// 正常が続いている？
	if (priv->ig_vol_old == 0 && ig_vol_now == 0) {
		// IG電圧条件OK回数を更新
		if (priv->ig_vol_cnt < INT_MAX) {
			priv->ig_vol_cnt++;
		}
	} else {
		// IG電圧条件OK回数をクリア
		priv->ig_vol_cnt = 0;
	}

	priv->ig_vol_old = ig_vol_now;
}

static int get_ad_bz(void)
{
	int val = 0;

	// 〔AD_BZ_A/D値〕を取得
	val = dummy_adc_getdata(ID_AD_BZ);

	pr_debug("%s: val = %d\n", __func__, val);
	return val;
}

static int get_ad_pb(void)
{
	int val = 0;

	// 〔AD_+B_A/D値〕を取得
	val = dummy_adc_getdata(ID_AD_PB);

	pr_debug("%s: val = %d\n", __func__, val);
	return val;
}

static void update_pin_vol_condition(struct emc_bz_priv *priv)
{
	int ad_bz;
	int ad_pb;

	// 端子電圧の更新
	// ＜メモ＞「* 10」は小数を無くすためにある
	ad_bz = get_ad_bz() * 10;
	ad_pb = get_ad_pb() * 10;
	if (ad_pb != 0) {
		priv->pin_vol = ad_bz / ad_pb;
	} else {
		priv->pin_vol = 0;
	}
}

static void update_out_cnt(struct emc_bz_priv *priv)
{
	// ブザー制御端子H出力中？
	if (priv->out_now) {
		// ブザー制御端子H出力回数の更新
		if (priv->out_cnt < INT_MAX) {
			priv->out_cnt++;
		}
	} else {
		// ブザー制御端子H出力回数のクリア
		priv->out_cnt = 0;
	}
}

static void check_on_error(struct emc_bz_priv *priv)
{
	// 〔じか線ブザー吹鳴制御〕が 0(非吹鳴) ？
	if (priv->in_now == 0) {
		return;
	}

	// 時間条件 (ブザー制御端子H出力にしてから閾値以上経過) を満たしていない？
	if (priv->out_cnt < OUT_H_THRESHOLD) {
		return;
	}

	// IG電圧条件 (+B電圧状態が正常になってから閾値以上経過) を満たしていない？
	if (priv->ig_vol_cnt < IG_VOL_THRESHOLD) {
		return;
	}

	// 端子電圧条件 (〔AD_BZ_A/D値〕÷〔AD_+B_A/D値〕が閾値以上) を満たしている？
	if (priv->pin_vol >= ON_PIN_VOL_THRESHOLD) {
		// ON異常回数の更新
		if (priv->on_err < INT_MAX) {
			priv->on_err++;
		}
		// 回数条件を満たしている？
		if (priv->on_err == ON_ERR_THRESHOLD_CNT) {
			// 《じか線ブザー制御異常》= 1
			set_bz_ctl_err(1);
		}
	}
}

static void check_off_error(struct emc_bz_priv *priv)
{
	// 〔じか線ブザー吹鳴制御〕が 0(非吹鳴) ？
	if (priv->in_now == 0) {
		return;
	}

	// 端子電圧条件 (〔AD_BZ_A/D値〕÷〔AD_+B_A/D値〕が閾値以下) を満たしている？
	if (priv->pin_vol <= OFF_PIN_VOL_THRESHOLD) {
		// OFF異常回数の更新
		if (priv->off_err < INT_MAX) {
			priv->off_err++;
		}
		// 回数条件を満たしている？
		if (priv->off_err == OFF_ERR_THRESHOLD_CNT) {
			// 《じか線ブザー制御異常》= 1
			set_bz_ctl_err(1);
		}

	// 端子電圧条件 (〔AD_BZ_A/D値〕÷〔AD_+B_A/D値〕が閾値以下) を満たしていない？
	} else {
		// OFF異常回数をリセット
		priv->off_err = 0;
	}
}

static void check_error(struct emc_bz_priv *priv)
{
	// IG電圧条件の更新
	update_ig_vol_condition(priv);

	// 端子電圧条件の更新
	update_pin_vol_condition(priv);

	// ブザー制御端子H出力時間の更新
	update_out_cnt(priv);

	// ON異常判定
	check_on_error(priv);

	// OFF異常判定
	check_off_error(priv);
}

static void in_kthread_main(struct emc_bz_priv *priv)
{
	// 少し待つ
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(INTERVAL_MS));

	// 〔じか線ブザー吹鳴制御〕を取得
	priv->in_now = get_in_now();

	// 異常判定
	check_error(priv);

	// 〔じか線ブザー吹鳴制御〕が 1 →  0 ？
	if (priv->in_old != 0 && priv->in_now == 0) {
		// ON異常回数をリセット
		priv->on_err = 0;
	}

	// 〔じか線ブザー吹鳴制御〕が 0 →  1 ？
	if (priv->in_old == 0 && priv->in_now != 0) {
		// OFF異常回数をリセット
		priv->off_err = 0;

		// 《じか線ブザー制御異常》= 0 ？
		if (get_bz_ctl_err() == 0) {
			// ブザー制御(パルス出力)スレッドを起こす
			priv->out_now = 1;
			wake_up_interruptible(&priv->out_wait);
		}
	}

	// 〔じか線ブザー吹鳴制御〕を保存
	priv->in_old = priv->in_now;
}

// ブザー制御(入力確認)スレッド
static int in_kthread(void *arg)
{
	struct emc_bz_priv *priv = (struct emc_bz_priv *)arg;

	// IG電圧状態を取得
	priv->ig_vol_old = get_ig_vol();

	// 〔じか線ブザー吹鳴制御〕を取得
	priv->in_old = get_in_now();

	while (!kthread_should_stop()) {
		in_kthread_main(priv);
	}

	return 0;
}

static int emc_bz_probe(struct platform_device *pdev)
{
	struct emc_bz_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	init_waitqueue_head(&priv->out_wait);

	// get gpio desc
	priv->out_desc = devm_gpiod_get(&pdev->dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(priv->out_desc))
		return PTR_ERR(priv->out_desc);

	// start kthread (out)
	priv->out_task = kthread_run(out_kthread, (void *)priv, EMC_BZ_MODNAME" out kthread");
	if (IS_ERR(priv->out_task))
		return PTR_ERR(priv->out_task);

	// start kthread (in)
	priv->in_task = kthread_run(in_kthread, (void *)priv, EMC_BZ_MODNAME" in kthread");
	if (IS_ERR(priv->in_task)) {
		kthread_stop(priv->out_task);
		return PTR_ERR(priv->in_task);
	}

	// set private data
	platform_set_drvdata(pdev, priv);

	return 0;
}

static int emc_bz_remove(struct platform_device *pdev)
{
	struct emc_bz_priv *priv = platform_get_drvdata(pdev);

	// stop kthread
	kthread_stop(priv->in_task);
	kthread_stop(priv->out_task);

	// unset private data
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id emc_bz_of_match[] = {
	{ .compatible = "emc-bz", },
	{ }
};
MODULE_DEVICE_TABLE(of, emc_bz_of_match);
#endif

static struct platform_driver emc_bz_platform_driver = {
	.driver	= {
		.name		= EMC_BZ_MODNAME,
		.of_match_table	= of_match_ptr(emc_bz_of_match),
	},
	.probe	= emc_bz_probe,
	.remove	= emc_bz_remove,
};
module_platform_driver(emc_bz_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("aaa <bbb@xxx.yyy.zzz>");
MODULE_DESCRIPTION("emc_bz driver");
