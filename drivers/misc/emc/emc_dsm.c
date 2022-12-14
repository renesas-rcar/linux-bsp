#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/iio/dummy_adc.h>
#include <uapi/misc/emc_data.h>

// 定数定義
#define EMC_DSM_MODNAME		"emc-dsm"

#define RUN_INTERVAL_MS		5					// スレッドの起動周期(ms)

#define CHK_INTERVAL_MS		5					// DSM天絡／地絡判定周期(ms)
#define CHK_INTERVAL_TIME	(CHK_INTERVAL_MS / RUN_INTERVAL_MS)	// DSM天絡／地絡判定周期(回)

#define SKY_CHK_WAIT_MS		5					// DSM天絡判定後の待ち時間(ms)
#define PWR_ON_WAIT_MS		5					// DSM電源制御終了後の待ち時間(ms)

#define T_CLS			50					// 〔T_CLS〕                      CLSモードの時間     (ms)  (0〜255)
#define T_CLS_TIME		(T_CLS / RUN_INTERVAL_MS)		//                                CLSモードの時間     (回)

#define T_DSM			200					// 〔T_DSM天絡判定フラグ〕        DSM天絡判定許可時間 (ms)  (0〜65535)
#define T_DSM_TIME		(T_DSM / RUN_INTERVAL_MS)		//                                DSM天絡判定許可時間 (回)

#define DSM_DIAG_L_VOL		20					// 〔DSMダイアグ下限AD_+B_A/D値〕 DSMダイアグ下限電圧 (LSB) (0〜65535)
#define DSM_DIAG_H_VOL		814					// 〔DSMダイアグ上限AD_+B_A/D値〕 DSMダイアグ上限電圧 (LSB) (0〜65535)

#define DSM_SKY_FAIL_VAL	168					// 〔DSM天絡異常判定条件値〕      DSM天絡異常判定条件 (LSB) (0〜65535)
#define DSM_SKY_FAIL_TIME	20					// 〔DSM天絡異常判定時間〕        DSM天絡異常判定時間 (回)  (0〜65535)

#define DSM_GND_FAIL_VAL	99					// 〔DSM地絡異常判定条件値〕      DSM地絡異常判定条件 (LSB) (0〜65535)
#define DSM_GND_FAIL_TIME	80					// 〔DSM地絡異常判定時間〕        DSM地絡異常判定時間 (回)  (0〜65535)

									// dummy_adc_getdata() 用ID定義
#define ID_AD_PB		1					// AD_+B_A/D値
#define ID_DSM_VOL		5					// DSM電源A/D値

// 定数チェック
#if (CHK_INTERVAL_MS % RUN_INTERVAL_MS)
	#error check CHK_INTERVAL_MS or RUN_INTERVAL_MS
#endif
#if (T_CLS % RUN_INTERVAL_MS)
	#error check T_CLS or RUN_INTERVAL_MS
#endif
#if (T_DSM % RUN_INTERVAL_MS)
	#error check T_DSM or RUN_INTERVAL_MS
#endif

struct emc_dsm_priv {
	struct task_struct	*task;
	int			run_cnt;				// スレッド起動時間 (回)

	struct pwm_device	*pwm;					// DSM電源制御

	int			sky_chk_flg;				// DSM天絡判定フラグ (0:無効、1:有効)
	int			sky_chk_cnt;				// DSM天絡判定回数
	int			sky_fail_cnt;				// DSM_天絡仮異常カウンタ

	int			pwr_on_flg;				// DSM電源制御フラグ (0:OFF、1:ON)
	int			pwr_on_cnt;				// DSM電源制御ON時間 (回)

	int			gnd_chk_flg;				// DSM地絡判定フラグ (0:無効、1:有効)
	int			gnd_chk_cnt;				// DSM地絡判定回数
	int			gnd_fail_cnt;				// DSM_地絡仮異常カウンタ
};

static int get_ig_vol(void)
{
	int val = 0;

	// 〔AD_+B_A/D値〕(IG電圧) の値を取得する。
	val = dummy_adc_getdata(ID_AD_PB);

	pr_debug("%s:%d:%s: val = %d\n", __FILE__, __LINE__, __func__, val);
	return val;
}

static int get_dsm_vol(void)
{
	int val = 0;

	// 〔DSM電源AD値〕(DSM電圧) の値を取得する。
	val = dummy_adc_getdata(ID_DSM_VOL);

	pr_debug("%s:%d:%s: val = %d\n", __FILE__, __LINE__, __func__, val);
	return val;
}

static void set_htr_enable(unsigned short val)
{
	int ret;

	// 《HTR_ENABLE》= val
	ret = emc_set_exp_info(HTR_ENABLE, val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
}

static void set_dsm_power_enable(unsigned short val)
{
	int ret;

	// 《DSM_POWER_ENABLE》= val
	ret = emc_set_exp_info(DSM_POWER_ENABLE, val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
}

static void set_dsm_sky_tmp_fail(unsigned short val)
{
	int ret;

	// 《DSM_天絡仮異常》= val
	ret = emc_set_exp_info(DRM_SUPPLY_TEMPORARY_FAULT, val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
}

static void set_dsm_sky_fail(unsigned short val)
{
	int ret;

	// 《DSM_天絡異常》= val
	ret = emc_set_exp_info(DRM_SUPPLY_FAULT, val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
}

static void set_dsm_gnd_tmp_fail(unsigned short val)
{
	int ret;

	// 《DSM_地絡仮異常》= val
	ret = emc_set_exp_info(DRM_GROUND_TEMPORARY_FAULT, val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
}

static void set_dsm_gnd_fail(unsigned short val)
{
	int ret;

	// 《DSM_地絡異常》= val
	ret = emc_set_exp_info(DRM_GROUND_FAULT, val);
	// FIXME : 復帰値がエラー時はどうすれば良いか不明

	pr_debug("%s:%d:%s: ret = %d, val = %u\n", __FILE__, __LINE__, __func__, ret, val);
}

static void check_sky_fail(struct emc_dsm_priv *priv)
{
	int ig_vol;
	int dsm_vol;

	// -- 仮異常判定 --
	// DSM天絡判定フラグが0？
	if (priv->sky_chk_flg == 0) {
		goto L_release;
	}

	// 〔AD_+B_A/D値〕の取得
	ig_vol = get_ig_vol();

	// 〔DSMダイアグ下限AD_+B_A/d値〕＞〔AD_+B_A/D値〕
	if (DSM_DIAG_L_VOL > ig_vol) {
		goto L_release;
	}

	// 〔AD_+B_A/D値〕＞〔DSMダイアグ上限AD_+B_A/d値〕
	if (ig_vol > DSM_DIAG_H_VOL) {
		goto L_release;
	}

	// 〔DSM電源AD値〕の取得
	dsm_vol = get_dsm_vol();

	// 〔DSM電源AD値〕＜〔DSM天絡異常判定条件値〕
	if (dsm_vol < DSM_SKY_FAIL_VAL) {
		goto L_release;
	}

	// -- 仮異常の成立 --
	// DSM_天絡仮異常カウンタのカウントアップ
	if (priv->sky_fail_cnt < INT_MAX) {
		priv->sky_fail_cnt++;
	}

	// 《DSM_天絡仮異常》 = 1
	if (priv->sky_fail_cnt == 1) {
		set_dsm_sky_tmp_fail(ABNORMAL);
	}

	// -- 異常判定 --
	// DSM_天絡仮異常カウンタが〔DSM天絡異常判定時間〕回？
	if (priv->sky_fail_cnt == DSM_SKY_FAIL_TIME) {
		// -- 異常の成立 --
		// 《DSM_天絡異常》 = 1
		set_dsm_sky_fail(ABNORMAL);
	}

	return;

L_release:
	// -- 仮異常の解除 --
	if (priv->sky_fail_cnt != 0) {
		// 《DSM_天絡仮異常》 = 0
		set_dsm_sky_tmp_fail(NORMAL);

		// DSM_天絡仮異常カウンタ = 0
		priv->sky_fail_cnt = 0;
	}
}

static void check_sky_cnt(struct emc_dsm_priv *priv)
{
	// DSM天絡判定回数をカウントアップ
	if (priv->sky_chk_cnt < INT_MAX) {
		priv->sky_chk_cnt++;
	}

	// DSM天絡判定回数が〔T_DSM天絡判定フラグ〕回？
	if (priv->sky_chk_cnt == T_DSM_TIME) {
		// DSM天絡判定フラグ = 0
		priv->sky_chk_flg = 0;

		// DSM天絡判定後の待ち時間(ms)のウェイト
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(SKY_CHK_WAIT_MS));

		// DSM電源制御フラグ = 1
		priv->pwr_on_flg = 1;
	}
}

static void check_sky(struct emc_dsm_priv *priv)
{
	check_sky_fail(priv);
	check_sky_cnt(priv);
}

static void pwr_on_pwm(struct emc_dsm_priv *priv)
{
	struct pwm_state state;

	// DSM電源制御 = PWM (PWM duty 50%)
	pwm_get_state(priv->pwm, &state);
	state.duty_cycle = state.period / 2;	/* duty 50% */
	state.enabled = true;
	pwm_apply_state(priv->pwm, &state);
}

static void pwr_on_hi(struct emc_dsm_priv *priv)
{
	struct pwm_state state;

	// DSM電源制御 = H (PWM duty 100%)
	pwm_get_state(priv->pwm, &state);
	state.duty_cycle = state.period;	/* duty 100% */
	state.enabled = true;
	pwm_apply_state(priv->pwm, &state);
}

static void pwr_on(struct emc_dsm_priv *priv)
{
	// DSM電源制御ON時間(回)のカウントアップ
	if (priv->pwr_on_cnt < INT_MAX) {
		priv->pwr_on_cnt++;
	}

	// DSM電源制御ON時間(回)が1回？
	if (priv->pwr_on_cnt == 1) {
		// DSM電源制御 = PWM
		pwr_on_pwm(priv);
	}

	// DSM電源制御ON時間(回)＞〔T_CLS〕回？
	if (priv->pwr_on_cnt > T_CLS_TIME) {
		// DSM電源制御 = H
		pwr_on_hi(priv);

		// DSM電源制御フラグ = 0
		priv->pwr_on_flg = 0;

		// DSM電源制御終了後の待ち時間 (ms) のウェイト
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(PWR_ON_WAIT_MS));

		// 《HTR_ENABLE》 = 1
		set_htr_enable(1);

		// 《DSM_POWER_ENABLE》 = 1
		set_dsm_power_enable(1);

		// DSM地絡判定フラグ = 1
		priv->gnd_chk_flg = 1;
	}
}

static void check_gnd_fail(struct emc_dsm_priv *priv)
{
	int ig_vol;
	int dsm_vol;

	// -- 仮異常判定 --
	// DSM地絡判定フラグが0？
	if (priv->gnd_chk_flg == 0) {
		goto L_release;
	}

	// 〔AD_+B_A/D値〕の取得
	ig_vol = get_ig_vol();

	// 〔DSMダイアグ下限AD_+B_A/d値〕＞〔AD_+B_A/D値〕
	if (DSM_DIAG_L_VOL > ig_vol) {
		goto L_release;
	}

	// 〔AD_+B_A/D値〕＞〔DSMダイアグ上限AD_+B_A/d値〕
	if (ig_vol > DSM_DIAG_H_VOL) {
		goto L_release;
	}

	// 〔DSM電源AD値〕の取得
	dsm_vol = get_dsm_vol();

	// 〔DSM電源AD値〕＞〔DSM地絡異常判定条件値〕
	if (dsm_vol < DSM_GND_FAIL_VAL) {
		goto L_release;
	}

	// -- 仮異常の成立 --
	// DSM_天絡仮異常カウンタのカウントアップ
	if (priv->gnd_fail_cnt < INT_MAX) {
		priv->gnd_fail_cnt++;
	}

	// 《DSM_絡仮異常》 = 1
	if (priv->gnd_fail_cnt == 1) {
		set_dsm_gnd_tmp_fail(ABNORMAL);
	}

	// -- 異常判定 --
	// DSM_地絡仮異常カウンタが〔DSM地絡異常判定時間〕回？
	if (priv->gnd_fail_cnt == DSM_GND_FAIL_TIME) {
		// -- 異常の成立 --
		// 《DSM_地絡異常》 = 1
		set_dsm_gnd_fail(ABNORMAL);
	}

	return;

L_release:
	// -- 仮異常の解除 --
	if (priv->gnd_fail_cnt != 0) {
		// 《DSM_地絡仮異常》 = 0
		set_dsm_gnd_tmp_fail(NORMAL);

		// DSM_地絡仮異常カウンタ = 0
		priv->gnd_fail_cnt = 0;
	}
}

static void check_gnd_cnt(struct emc_dsm_priv *priv)
{
	// DSM地絡判定回数をカウントアップ
	if (priv->gnd_chk_cnt < INT_MAX) {
		priv->gnd_chk_cnt++;
	}
}

static void check_gnd(struct emc_dsm_priv *priv)
{
	check_gnd_fail(priv);
	check_gnd_cnt(priv);
}

static void kthread_main(struct emc_dsm_priv *priv)
{
	// スレッド起動時間(回)をカウントアップ
	if (priv->run_cnt < INT_MAX) {
		priv->run_cnt++;
	}

	// DSM天絡判定フラグが1？
	if (priv->sky_chk_flg != 0) {
		// スレッド起動時間(回)＞DSM天絡／地絡判定周期(回)？
		if (priv->run_cnt > CHK_INTERVAL_TIME) {
			// DSM天絡異常判定
			check_sky(priv);
			priv->run_cnt = 0;
		}
	}

	// DSM電源制御フラグが1？
	if (priv->pwr_on_flg != 0) {
		// DSM電源制御
		pwr_on(priv);
	}

	// DSM地絡判定フラグが1？
	if (priv->gnd_chk_flg != 0) {
		// スレッド起動時間(回)＞DSM天絡／地絡判定周期(回)？
		if (priv->run_cnt > CHK_INTERVAL_TIME) {
			// DSM地絡異常判定
			check_gnd(priv);
			priv->run_cnt = 0;
		}
	}

	// スレッドの起動周期間隔(ms)のウェイト
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(RUN_INTERVAL_MS));
}

// スレッド
static int kthread(void *arg)
{
	struct emc_dsm_priv *priv = (struct emc_dsm_priv *)arg;

	// DSM天絡判定フラグ = 1
	priv->sky_chk_flg = 1;

	while (!kthread_should_stop()) {
		kthread_main(priv);
	}

	return 0;
}

static int emc_dsm_probe(struct platform_device *pdev)
{
	struct emc_dsm_priv *priv;
	struct pwm_state state;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	// get pwm
	priv->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(priv->pwm))
		return PTR_ERR(priv->pwm);

	// setup pwm
	pwm_init_state(priv->pwm, &state);
	state.enabled = false;
	ret = pwm_apply_state(priv->pwm, &state);
	if (ret) {
		return ret;
	}

	// start kthread
	priv->task = kthread_run(kthread, (void *)priv, EMC_DSM_MODNAME" kthread");
	if (IS_ERR(priv->task))
		return PTR_ERR(priv->task);

	// set private data
	platform_set_drvdata(pdev, priv);

	return 0;
}

static int emc_dsm_remove(struct platform_device *pdev)
{
	struct emc_dsm_priv *priv = platform_get_drvdata(pdev);

	// stop kthread
	kthread_stop(priv->task);

	// unset private data
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id emc_dsm_of_match[] = {
	{ .compatible = "emc-dsm", },
	{ }
};
MODULE_DEVICE_TABLE(of, emc_dsm_of_match);
#endif

static struct platform_driver emc_dsm_platform_driver = {
	.driver	= {
		.name		= EMC_DSM_MODNAME,
		.of_match_table	= of_match_ptr(emc_dsm_of_match),
	},
	.probe	= emc_dsm_probe,
	.remove	= emc_dsm_remove,
};
module_platform_driver(emc_dsm_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("aaa <bbb@xxx.yyy.zzz>");
MODULE_DESCRIPTION("emc_dsm driver");
