// SPDX-License-Identifier: GPL-2.0
/*
 * R-Mobile TAUD PWM driver
 *
 * Copyright (C) 2021 Renesas Solutions Corp.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define CLKC_CPUS			(0x100u)
#define CLKC_CPUS_CLKSCSID_SHIFT	0
#define CLKC_CPUS_CLKSCSID_MASK		BIT(0)

#define CLKD_PLLC			(0x120u)
#define CLKD_PLLC_PLLCLKDCSID_SHIFT	0
#define CLKD_PLLC_PLLCLKDCSID_MASK	GENMASK(3, 0)

#define CLKD_PLLS			(0x128u)
#define CLKD_PLLS_PLLCLKDSYNC_SHIFT	1
#define CLKD_PLLS_PLLCLKDSYNC_MASK	BIT(1)

#define CLKD_HSOSCS			(0x8100u)
#define CLKD_HSOSCS_HSOSCSTAB_SHIFT	1
#define CLKD_HSOSCS_HSOSCSTAB_MASK	BIT(1)

#define TAUD_TPS			(0x240u)
#define TAUD_TPS_PRS3_SHIFT		12
#define TAUD_TPS_PRS3_MASK		GENMASK(15, 12)
#define TAUD_TPS_PRS2_SHIFT		8
#define TAUD_TPS_PRS2_MASK		GENMASK(11, 8)
#define TAUD_TPS_PRS1_SHIFT		4
#define TAUD_TPS_PRS1_MASK		GENMASK(7, 4)
#define TAUD_TPS_PRS0_SHIFT		0
#define TAUD_TPS_PRS0_MASK		GENMASK(3, 0)

#define TAUD_BRS			(0x244u)
#define TAUD_BRS_SHIFT			0
#define TAUD_BRS_MASK			GENMASK(7, 0)

#define TAUD_CDR(m)			((m) * 0x4u)
#define TAUD_CDR_SHIFT			0
#define TAUD_CDR_MASK			GENMASK(15, 0)

#define TAUD_CMOR(m)			(0x200u + (m) * 0x4u)
#define TAUD_CMOR_CKS_SHIFT		14
#define TAUD_CMOR_CKS_MASK		GENMASK(15, 14)
#define TAUD_CMOR_CCS_SHIFT		12
#define TAUD_CMOR_CCS_MASK		GENMASK(13, 12)
#define TAUD_CMOR_MAS_SHIFT		11
#define TAUD_CMOR_MAS_MASK		BIT(11)
#define TAUD_CMOR_MAS_SLAVE		(0x0u)
#define TAUD_CMOR_MAS_MASTER		(0x1u)
#define TAUD_CMOR_STS_SHIFT		8
#define TAUD_CMOR_STS_MASK		GENMASK(10, 8)
#define TAUD_CMOR_COS_SHIFT		6
#define TAUD_CMOR_COS_MASK		GENMASK(7, 6)
#define TAUD_CMOR_MD_SHIFT		0
#define TAUD_CMOR_MD_MASK		GENMASK(4, 0)

#define TAUD_CMUR(m)			(0x0C0u + (m) * 4)
#define TAUD_CMUR_TIS_SHIFT		0
#define TAUD_CMUR_TIS_MASK		GENMASK(1, 0)

#define TAUD_TS				(0x1c4u)
#define TAUD_TT				(0x1c8u)

#define TAUD_RDE			(0x260u)
#define TAUD_RDM			(0x264u)
#define TAUD_RDS			(0x268u)
#define TAUD_RDC			(0x26cu)

#define TAUD_TOE			(0x5cu)
#define TAUD_TOM			(0x248u)
#define TAUD_TOC			(0x24cu)
#define TAUD_TOL			(0x40u)

#define TAUD_TDE			(0x250u)
#define TAUD_TDM			(0x254u)
#define TAUD_TDL			(0x054u)

#define TAUD_TRE			(0x258u)
#define TAUD_TRC			(0x25cu)
#define TAUD_TRO			(0x04cu)
#define TAUD_TME			(0x050u)

#define MODEMR1				(0x4u)
#define MODEMR1_MD40_39_SHIFT		7
#define MODEMR1_MD40_39_MASK		GENMASK(8, 7)

#define TAUD_CHANNEL_MAX		8
#define TAUD_CHANNEL_MASTER(a)		(((a) % 8) * 2)
#define TAUD_CHANNEL_SLAVE(a)		(((a) % 8) * 2 + 1)

struct tau_pwm_chip {
	struct pwm_chip chip;
	void __iomem *taud_base;
	void __iomem *clkc_base;
	void __iomem *modemr_base;
};

struct tau_pwm_params {
	int clk_rate;
	int clk_sel;
	int clk_prescaler;
	int clk_division;
	u16 period;
	u16 duty;
};

struct tau_pwm_device {
	struct tau_pwm_chip *tau_chip;
	int channel;
	struct tau_pwm_params params;
	bool timer_on;
};

#define tau_pwm_read(a, b, c, d)	({ ioread##a((b)->c##_base + (d)); })
#define tau_pwm_write(a, b, c, d, e)	({ iowrite##a((e), (b)->c##_base + (d)); })

static inline struct tau_pwm_chip *to_tau_pwm(struct pwm_chip *chip)
{
	return container_of(chip, struct tau_pwm_chip, chip);
}

enum {
	CLK_HSB_SOURCE_PLLO,
	CLK_HSB_SOURCE_LS_INTOSC,
	CLK_HSB_SOURCE_HS_INTOSC,
};

#define	CLK_HSB_SOURCE_MAX	(CLK_HSB_SOURCE_HS_INTOSC + 1)
#define	CLK_HSB_SELECT_MAX	(4)

static u32 clk_hsb_table[CLK_HSB_SOURCE_MAX][CLK_HSB_SELECT_MAX] = {
	/* CLK_PLLO */
	/*	2'b00		2'b01		2'b10		2'b11	*/
	{	80000000,	80000000,	80000000,	80000000 },
	/* CLK_IOSC : LS IntOSC */
	/*	2'b00		2'b01		2'b10		2'b11	*/
	{	24000,		30000,		24000,		40000 },
	/* CLK_IOSC : HS IntOSC */
	/*	2'b00		2'b01		2'b10		2'b11	*/
	{	20000000,	25000000,	33333333,	33333333},
};

static u64 tau_pwm_get_pclk(struct tau_pwm_device *dev)
{
	struct tau_pwm_chip *tau_chip = dev->tau_chip;
	u64 pclk, div, src, sel;

	if (!(tau_pwm_read(32, tau_chip, clkc, CLKC_CPUS)
	     & CLKC_CPUS_CLKSCSID_MASK)) {
		src = CLK_HSB_SOURCE_PLLO;
		if (!(tau_pwm_read(32, tau_chip, clkc, CLKD_PLLC)
		     & CLKD_PLLS_PLLCLKDSYNC_MASK)) {
			div = 0;
		} else {
			div = tau_pwm_read(32, tau_chip, clkc, CLKD_PLLS);
			div = (div & CLKD_PLLC_PLLCLKDCSID_MASK)
			       >> CLKD_PLLC_PLLCLKDCSID_SHIFT;
		}
	} else if (tau_pwm_read(32, tau_chip, clkc, CLKD_HSOSCS)
		  & CLKD_HSOSCS_HSOSCSTAB_MASK) {
		src = CLK_HSB_SOURCE_HS_INTOSC;
		div = 1;
	} else {
		src = CLK_HSB_SOURCE_LS_INTOSC;
		div = 1;
	}

	sel = tau_pwm_read(32, tau_chip, modemr, MODEMR1);
	sel = (sel & MODEMR1_MD40_39_MASK) >> MODEMR1_MD40_39_SHIFT;

	if (!div)
		pclk = 0;
	else
		pclk =  DIV_ROUND_CLOSEST_ULL(clk_hsb_table[src][sel], div);

	return pclk;
}

static int tau_pwm_update_params(struct tau_pwm_device *dev, u64 period_ns, u64 duty_cycle_ns)
{
	struct tau_pwm_params *tau_params = &dev->params;

	u64 pclk;
	int prescaler, prescaler_max = 15;
	int div, div_max = 15;
	u64 period_ns_max, period_counter_max = GENMASK(15, 0);

	pclk = tau_pwm_get_pclk(dev);

	for (prescaler = 0; prescaler <= prescaler_max; prescaler++) {
		period_ns_max =  div64_u64(period_counter_max * NSEC_PER_SEC,
					   (pclk >> prescaler));
		if (period_ns_max < period_ns)
			continue;
	}

	if (prescaler > prescaler_max) {
		for (div = 0; div <= div_max; div++) {
			period_ns_max =  div64_u64(period_counter_max * NSEC_PER_SEC,
						   (pclk >> prescaler) / div);
			if (period_ns_max < period_ns)
				continue;
		}
	}

	if (period_ns_max < period_ns)
		return -EINVAL;

	tau_params->clk_rate = pclk;
	tau_params->clk_prescaler = prescaler;

	if (prescaler <= prescaler_max) {
		tau_params->clk_sel = 0;
		tau_params->clk_division = 0;
	} else {
		tau_params->clk_sel = 3;
		tau_params->clk_division = div;
	}

	tau_params->period = div64_u64(period_ns_max * period_ns, period_ns_max);

	tau_params->duty = div64_u64(tau_params->period * duty_cycle_ns, period_ns);
	tau_params->duty = min(tau_params->duty, tau_params->period);

	return 0;
}

static int tau_pwm_update_clk(struct tau_pwm_device *dev)
{
	struct tau_pwm_chip *tau_chip = dev->tau_chip;
	struct tau_pwm_params *tau_params = &dev->params;
	u16 val, shift, mask;

	if (tau_params->clk_sel == 0) {
		shift = TAUD_TPS_PRS0_SHIFT;
		mask = TAUD_TPS_PRS0_MASK;
	} else if (tau_params->clk_sel == 1) {
		shift = TAUD_TPS_PRS1_SHIFT;
		mask = TAUD_TPS_PRS1_MASK;
	} else if (tau_params->clk_sel == 2) {
		shift = TAUD_TPS_PRS2_SHIFT;
		mask = TAUD_TPS_PRS2_MASK;
	} else if (tau_params->clk_sel == 3) {
		shift = TAUD_TPS_PRS3_SHIFT;
		mask = TAUD_TPS_PRS3_MASK;
	} else {
		return -EINVAL;
	}

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TPS);
	val &= ~mask;
	val |= (tau_params->clk_prescaler << shift) & mask;
	tau_pwm_write(16, tau_chip, taud, TAUD_TPS, val);

	if (tau_params->clk_sel == 3) {
		val = tau_pwm_read(8, tau_chip, taud, TAUD_BRS);
		val &= ~TAUD_BRS_MASK;
		val |= ((tau_params->clk_division) << TAUD_BRS_SHIFT) & TAUD_BRS_MASK;
		tau_pwm_write(8, tau_chip, taud, TAUD_BRS, val);
	}

	return 0;
}

static int tau_pwm_update_channel(struct tau_pwm_device *dev)
{
	struct tau_pwm_chip *tau_chip = dev->tau_chip;
	struct tau_pwm_params *tau_params = &dev->params;
	u32 val;

	val = tau_pwm_read(16, tau_chip, taud, TAUD_CMOR(TAUD_CHANNEL_MASTER(dev->channel)));
	val &= ~TAUD_CMOR_CKS_MASK;
	val |= (tau_params->clk_sel << TAUD_CMOR_CKS_SHIFT) & TAUD_CMOR_CKS_MASK;
	val &= ~TAUD_CMOR_CCS_MASK;
	val &= ~TAUD_CMOR_MAS_MASK;
	val |= (TAUD_CMOR_MAS_MASTER << TAUD_CMOR_MAS_SHIFT) & TAUD_CMOR_MAS_MASK;
	val &= ~TAUD_CMOR_STS_MASK;
	val &= ~TAUD_CMOR_COS_MASK;
	val &= ~TAUD_CMOR_MD_MASK;
	val |= (BIT(0) << TAUD_CMOR_MD_SHIFT) & TAUD_CMOR_MD_MASK;
	tau_pwm_write(16, tau_chip, taud, TAUD_CMOR(TAUD_CHANNEL_MASTER(dev->channel)), val);

	val = tau_pwm_read(8, tau_chip, taud, TAUD_CMUR(TAUD_CHANNEL_MASTER(dev->channel)));
	val &= ~TAUD_CMUR_TIS_MASK;
	tau_pwm_write(8, tau_chip, taud, TAUD_CMUR(TAUD_CHANNEL_MASTER(dev->channel)), val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_CMOR(TAUD_CHANNEL_SLAVE(dev->channel)));
	val &= ~TAUD_CMOR_CKS_MASK;
	val |= (tau_params->clk_sel << TAUD_CMOR_CKS_SHIFT) & TAUD_CMOR_CKS_MASK;
	val &= ~TAUD_CMOR_CCS_MASK;
	val &= ~TAUD_CMOR_MAS_MASK;
	val |= (TAUD_CMOR_MAS_SLAVE << TAUD_CMOR_MAS_SHIFT) & TAUD_CMOR_MAS_MASK;
	val &= ~TAUD_CMOR_STS_MASK;
	val |= (BIT(2) << TAUD_CMOR_STS_SHIFT) & TAUD_CMOR_STS_MASK;
	val &= ~TAUD_CMOR_COS_MASK;
	val &= ~TAUD_CMOR_MD_MASK;
	val |= ((BIT(3) | BIT(0)) << TAUD_CMOR_MD_SHIFT) & TAUD_CMOR_MD_MASK;
	tau_pwm_write(16, tau_chip, taud, TAUD_CMOR(TAUD_CHANNEL_SLAVE(dev->channel)), val);

	val = tau_pwm_read(8, tau_chip, taud, TAUD_CMUR(TAUD_CHANNEL_SLAVE(dev->channel)));
	val &= ~TAUD_CMUR_TIS_MASK;
	tau_pwm_write(8, tau_chip, taud, TAUD_CMUR(TAUD_CHANNEL_SLAVE(dev->channel)), val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TOE);
	val |= (BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TOE, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TOM);
	val |= (BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TOM, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TOC);
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TOC, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TOL);
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TOL, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TDE);
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TDE, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TDM);
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TDM, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TDL);
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TDL, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TRE);
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TRE, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TRO);
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TRO, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TRC);
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TRC, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TME);
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TME, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_RDE);
	val |= (BIT(0) << TAUD_CHANNEL_MASTER(dev->channel));
	val |= (BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_RDE, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_RDS);
	val &= ~(BIT(0) << TAUD_CHANNEL_MASTER(dev->channel));
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_RDS, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_RDM);
	val &= ~(BIT(0) << TAUD_CHANNEL_MASTER(dev->channel));
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_RDM, val);

	val = tau_pwm_read(16, tau_chip, taud, TAUD_RDC);
	val &= ~(BIT(0) << TAUD_CHANNEL_MASTER(dev->channel));
	val &= ~(BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_RDC, val);

	return 0;
}

static int tau_pwm_update_counter(struct tau_pwm_device *dev)
{
	struct tau_pwm_chip *tau_chip = dev->tau_chip;
	struct tau_pwm_params *tau_params = &dev->params;

	tau_pwm_write(16, tau_chip, taud,
		      TAUD_CDR(TAUD_CHANNEL_MASTER(dev->channel)),
		      tau_params->period);
	tau_pwm_write(16, tau_chip, taud,
		      TAUD_CDR(TAUD_CHANNEL_SLAVE(dev->channel)),
		      tau_params->duty);

	return 0;
}

static int tpu_pwm_start(struct tau_pwm_device *dev)
{
	struct tau_pwm_chip *tau_chip = dev->tau_chip;
	u16 val;

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TS);
	val |= (BIT(0) << TAUD_CHANNEL_MASTER(dev->channel));
	val |= (BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TS, val);

	return 0;
}

static int tpu_pwm_stop(struct tau_pwm_device *dev)
{
	struct tau_pwm_chip *tau_chip = dev->tau_chip;
	u16 val;

	val = tau_pwm_read(16, tau_chip, taud, TAUD_TT);
	val |= (BIT(0) << TAUD_CHANNEL_MASTER(dev->channel));
	val |= (BIT(0) << TAUD_CHANNEL_SLAVE(dev->channel));
	tau_pwm_write(16, tau_chip, taud, TAUD_TT, val);

	return 0;
}

/* -----------------------------------------------------------------------------
 * PWM API
 */
static int tpu_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tau_pwm_chip *tau_chip = to_tau_pwm(chip);
	struct tau_pwm_device *tau_dev;

	if (pwm->hwpwm >= TAUD_CHANNEL_MAX)
		return -EINVAL;

	tau_dev = devm_kzalloc(chip->dev, sizeof(*tau_dev), GFP_KERNEL);
	if (!tau_dev)
		return -ENOMEM;

	tau_dev->tau_chip = tau_chip;
	tau_dev->channel = pwm->hwpwm;

	memset(&tau_dev->params, 0, sizeof(tau_dev->params));

	tau_dev->timer_on = false;

	pwm_set_chip_data(pwm, tau_dev);

	return 0;
}

static void tpu_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tau_pwm_device *tau_dev = pwm_get_chip_data(pwm);

	tpu_pwm_stop(tau_dev);
}

static int tau_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct tau_pwm_device *tau_dev  = pwm_get_chip_data(pwm);
	int ret;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EOPNOTSUPP;

	if (!state->enabled) {
		tpu_pwm_stop(tau_dev);
		return 0;
	}

	ret = tau_pwm_update_params(tau_dev, state->period, state->duty_cycle);
	if (ret)
		return ret;

	ret = tau_pwm_update_clk(tau_dev);
	if (ret)
		return ret;

	ret = tau_pwm_update_channel(tau_dev);
	if (ret)
		return ret;

	ret = tau_pwm_update_counter(tau_dev);
	if (ret)
		return ret;

	ret = tpu_pwm_start(tau_dev);
	if (ret)
		return ret;

	return 0;
}

static const struct pwm_ops tau_pwm_ops = {
	.request = tpu_pwm_request,
	.free = tpu_pwm_free,
	.apply	= tau_pwm_apply,
	.owner = THIS_MODULE,
};

static int tau_probe(struct platform_device *pdev)
{
	struct tau_pwm_chip *tau;
	int ret;

	tau = devm_kzalloc(&pdev->dev, sizeof(*tau), GFP_KERNEL);
	if (!tau)
		return -ENOMEM;

	tau->taud_base = devm_platform_ioremap_resource_byname(pdev, "taud");
	if (IS_ERR(tau->taud_base))
		return PTR_ERR(tau->taud_base);

	tau->clkc_base = devm_platform_ioremap_resource_byname(pdev, "clkc");
	if (IS_ERR(tau->clkc_base))
		return PTR_ERR(tau->clkc_base);

	tau->modemr_base = devm_platform_ioremap_resource_byname(pdev, "modemr");
	if (IS_ERR(tau->clkc_base))
		return PTR_ERR(tau->clkc_base);

	tau->chip.dev = &pdev->dev;
	tau->chip.ops = &tau_pwm_ops;
	tau->chip.npwm = TAUD_CHANNEL_MAX;

	/* Initialize and register the device. */
	platform_set_drvdata(pdev, tau);

	ret = pwmchip_add(&tau->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register PWM chip\n");
		return ret;
	}

	return 0;
}

static int tau_remove(struct platform_device *pdev)
{
	struct tau_pwm_chip *tau = platform_get_drvdata(pdev);

	return pwmchip_remove(&tau->chip);
}

static const struct of_device_id tau_of_table[] = {
	{ .compatible = "renesas,tau-pwm", },
	{ },
};

MODULE_DEVICE_TABLE(of, tau_of_table);

static struct platform_driver tau_driver = {
	.probe		= tau_probe,
	.remove		= tau_remove,
	.driver		= {
		.name	= "renesas-tau-pwm",
		.of_match_table = of_match_ptr(tau_of_table),
	}
};

module_platform_driver(tau_driver);

MODULE_DESCRIPTION("Renesas TAU PWM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:renesas-tau-pwm");