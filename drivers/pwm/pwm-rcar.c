/*
 * R-Car PWM Timer driver
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/soc/renesas/s2ram_ddr_backup.h>

#define RCAR_PWM_MAX_DIVISION	24
#define RCAR_PWM_MIN_CYCLE	2
#define RCAR_PWM_MAX_CYCLE	1023

#define RCAR_PWMCR		0x00
#define  RCAR_PWMCR_CC0_MASK	0x000f0000
#define  RCAR_PWMCR_CC0_SHIFT	16
#define  RCAR_PWMCR_CCMD	BIT(15)
#define  RCAR_PWMCR_SYNC	BIT(11)
#define  RCAR_PWMCR_SS0		BIT(4)
#define  RCAR_PWMCR_EN0		BIT(0)

#define RCAR_PWMCNT		0x04
#define  RCAR_PWMCNT_CYC0_MASK	0x03ff0000
#define  RCAR_PWMCNT_CYC0_SHIFT	16
#define  RCAR_PWMCNT_PH0_MASK	0x000003ff
#define  RCAR_PWMCNT_PH0_SHIFT	0

#ifdef CONFIG_RCAR_DDR_BACKUP
/* PWM0 */
static struct hw_register pwm0_ip_regs[] = {
	{"PWMCNT",   0x0004, 32, 0},
	{"PWMCR",    0x0000, 32, 0},
};

static struct rcar_ip pwm0_ip = {
	.ip_name   = "PWM0",
	.reg_count = ARRAY_SIZE(pwm0_ip_regs),
	.ip_reg    = pwm0_ip_regs,
};

/* PWM1 */
static struct hw_register pwm1_ip_regs[] = {
	{"PWMCNT",   0x0004, 32, 0},
	{"PWMCR",    0x0000, 32, 0},
};

static struct rcar_ip pwm1_ip = {
	.ip_name   = "PWM1",
	.reg_count = ARRAY_SIZE(pwm1_ip_regs),
	.ip_reg    = pwm1_ip_regs,
};

/* PWM2 */
static struct hw_register pwm2_ip_regs[] = {
	{"PWMCNT",   0x0004, 32, 0},
	{"PWMCR",    0x0000, 32, 0},
};

static struct rcar_ip pwm2_ip = {
	.ip_name   = "PWM2",
	.reg_count = ARRAY_SIZE(pwm2_ip_regs),
	.ip_reg    = pwm2_ip_regs,
};

/* PWM3 */
static struct hw_register pwm3_ip_regs[] = {
	{"PWMCNT",   0x0004, 32, 0},
	{"PWMCR",    0x0000, 32, 0},
};

static struct rcar_ip pwm3_ip = {
	.ip_name   = "PWM3",
	.reg_count = ARRAY_SIZE(pwm3_ip_regs),
	.ip_reg    = pwm3_ip_regs,
};

/* PWM4 */
static struct hw_register pwm4_ip_regs[] = {
	{"PWMCNT",   0x0004, 32, 0},
	{"PWMCR",    0x0000, 32, 0},
};

static struct rcar_ip pwm4_ip = {
	.ip_name   = "PWM4",
	.reg_count = ARRAY_SIZE(pwm4_ip_regs),
	.ip_reg    = pwm4_ip_regs,
};

/* PWM5 */
static struct hw_register pwm5_ip_regs[] = {
	{"PWMCNT",   0x0004, 32, 0},
	{"PWMCR",    0x0000, 32, 0},
};

static struct rcar_ip pwm5_ip = {
	.ip_name   = "PWM5",
	.reg_count = ARRAY_SIZE(pwm5_ip_regs),
	.ip_reg    = pwm5_ip_regs,
};

/* PWM6 */
static struct hw_register pwm6_ip_regs[] = {
	{"PWMCNT",   0x0004, 32, 0},
	{"PWMCR",    0x0000, 32, 0},
};

static struct rcar_ip pwm6_ip = {
	.ip_name   = "PWM6",
	.reg_count = ARRAY_SIZE(pwm6_ip_regs),
	.ip_reg    = pwm6_ip_regs,
};

struct pwm_ip_info {
	const char *name;
	struct rcar_ip *ip;
};

static struct pwm_ip_info ip_info_tbl[] = {
	{"e6e30000.pwm", &pwm0_ip},
	{"e6e31000.pwm", &pwm1_ip},
	{"e6e32000.pwm", &pwm2_ip},
	{"e6e33000.pwm", &pwm3_ip},
	{"e6e34000.pwm", &pwm4_ip},
	{"e6e35000.pwm", &pwm5_ip},
	{"e6e36000.pwm", &pwm6_ip},
	{NULL, NULL},
};

static struct rcar_ip *rcar_pwm_get_ip(const char *name)
{
	struct pwm_ip_info *ip_info = ip_info_tbl;
	struct rcar_ip *ip = NULL;

	while (ip_info->name) {
		if (!strcmp(ip_info->name, name)) {
			ip = ip_info->ip;
			break;
		}
		ip_info++;
	}

	return ip;
}
#endif /* CONFIG_RCAR_DDR_BACKUP */

struct rcar_pwm_chip {
	struct pwm_chip chip;
	void __iomem *base;
	struct clk *clk;
};

static inline struct rcar_pwm_chip *to_rcar_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct rcar_pwm_chip, chip);
}

static void rcar_pwm_write(struct rcar_pwm_chip *rp, u32 data,
			   unsigned int offset)
{
	writel(data, rp->base + offset);
}

static u32 rcar_pwm_read(struct rcar_pwm_chip *rp, unsigned int offset)
{
	return readl(rp->base + offset);
}

static void rcar_pwm_update(struct rcar_pwm_chip *rp, u32 mask, u32 data,
			    unsigned int offset)
{
	u32 value;

	value = rcar_pwm_read(rp, offset);
	value &= ~mask;
	value |= data & mask;
	rcar_pwm_write(rp, value, offset);
}

static int rcar_pwm_get_clock_division(struct rcar_pwm_chip *rp, int period_ns)
{
	unsigned long clk_rate = clk_get_rate(rp->clk);
	unsigned long long min, max; /* min, max cycle / nanoseconds */
	unsigned int div;

	if (clk_rate == 0)
		return -EINVAL;

	min = (unsigned long long)NSEC_PER_SEC * RCAR_PWM_MIN_CYCLE;
	do_div(min, clk_rate);
	if (period_ns < min)
		return -ERANGE;

	for (div = 0; div <= RCAR_PWM_MAX_DIVISION; div++) {
		max = (unsigned long long)NSEC_PER_SEC * RCAR_PWM_MAX_CYCLE *
			(1 << div);
		do_div(max, clk_rate);
		if (period_ns <= max)
			break;
	}

	return (div <= RCAR_PWM_MAX_DIVISION) ? div : -ERANGE;
}

static void rcar_pwm_set_clock_control(struct rcar_pwm_chip *rp,
				       unsigned int div)
{
	u32 value;

	value = rcar_pwm_read(rp, RCAR_PWMCR);
	value &= ~(RCAR_PWMCR_CCMD | RCAR_PWMCR_CC0_MASK);

	if (div & 1)
		value |= RCAR_PWMCR_CCMD;

	div >>= 1;

	value |= div << RCAR_PWMCR_CC0_SHIFT;
	rcar_pwm_write(rp, value, RCAR_PWMCR);
}

static int rcar_pwm_set_counter(struct rcar_pwm_chip *rp, int div, int duty_ns,
				int period_ns)
{
	unsigned long long one_cycle, tmp;	/* 0.01 nanoseconds */
	unsigned long clk_rate = clk_get_rate(rp->clk);
	u32 cyc, ph;

	one_cycle = (unsigned long long)NSEC_PER_SEC * 100ULL * (1 << div);
	do_div(one_cycle, clk_rate);

	tmp = period_ns * 100ULL;
	do_div(tmp, one_cycle);
	cyc = (tmp << RCAR_PWMCNT_CYC0_SHIFT) & RCAR_PWMCNT_CYC0_MASK;

	tmp = duty_ns * 100ULL;
	do_div(tmp, one_cycle);
	ph = tmp & RCAR_PWMCNT_PH0_MASK;

	/* Avoid prohibited setting */
	if (cyc == 0 || ph == 0)
		return -EINVAL;

	rcar_pwm_write(rp, cyc | ph, RCAR_PWMCNT);

	return 0;
}

static int rcar_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	return pm_runtime_get_sync(chip->dev);
}

static void rcar_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	pm_runtime_put(chip->dev);
}

static int rcar_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			   int duty_ns, int period_ns)
{
	struct rcar_pwm_chip *rp = to_rcar_pwm_chip(chip);
	int div, ret;

	div = rcar_pwm_get_clock_division(rp, period_ns);
	if (div < 0)
		return div;

	/* Let the core driver set pwm->period if disabled and duty_ns == 0 */
	if (!pwm_is_enabled(pwm) && !duty_ns && !pwm->state.duty_cycle)
		return 0;

	rcar_pwm_update(rp, RCAR_PWMCR_SYNC, RCAR_PWMCR_SYNC, RCAR_PWMCR);

	ret = rcar_pwm_set_counter(rp, div, duty_ns, period_ns);
	if (!ret)
		rcar_pwm_set_clock_control(rp, div);

	/* The SYNC should be set to 0 even if rcar_pwm_set_counter failed */
	rcar_pwm_update(rp, RCAR_PWMCR_SYNC, 0, RCAR_PWMCR);

	return ret;
}

static int rcar_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rcar_pwm_chip *rp = to_rcar_pwm_chip(chip);
	u32 value;

	/* Don't enable the PWM device if CYC0 or PH0 is 0 */
	value = rcar_pwm_read(rp, RCAR_PWMCNT);
	if ((value & RCAR_PWMCNT_CYC0_MASK) == 0 ||
	    (value & RCAR_PWMCNT_PH0_MASK) == 0)
		return -EINVAL;

	rcar_pwm_update(rp, RCAR_PWMCR_EN0, RCAR_PWMCR_EN0, RCAR_PWMCR);

	return 0;
}

static void rcar_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rcar_pwm_chip *rp = to_rcar_pwm_chip(chip);

	rcar_pwm_update(rp, RCAR_PWMCR_EN0, 0, RCAR_PWMCR);
}

static const struct pwm_ops rcar_pwm_ops = {
	.request = rcar_pwm_request,
	.free = rcar_pwm_free,
	.config = rcar_pwm_config,
	.enable = rcar_pwm_enable,
	.disable = rcar_pwm_disable,
	.owner = THIS_MODULE,
};

static int rcar_pwm_probe(struct platform_device *pdev)
{
	struct rcar_pwm_chip *rcar_pwm;
	struct resource *res;
	int ret;

	rcar_pwm = devm_kzalloc(&pdev->dev, sizeof(*rcar_pwm), GFP_KERNEL);
	if (rcar_pwm == NULL)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rcar_pwm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rcar_pwm->base))
		return PTR_ERR(rcar_pwm->base);

	rcar_pwm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(rcar_pwm->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return PTR_ERR(rcar_pwm->clk);
	}

	platform_set_drvdata(pdev, rcar_pwm);

	rcar_pwm->chip.dev = &pdev->dev;
	rcar_pwm->chip.ops = &rcar_pwm_ops;
	rcar_pwm->chip.base = -1;
	rcar_pwm->chip.npwm = 1;

	ret = pwmchip_add(&rcar_pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register PWM chip: %d\n", ret);
		return ret;
	}

	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int rcar_pwm_remove(struct platform_device *pdev)
{
	struct rcar_pwm_chip *rcar_pwm = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	return pwmchip_remove(&rcar_pwm->chip);
}

static const struct of_device_id rcar_pwm_of_table[] = {
	{ .compatible = "renesas,pwm-rcar", },
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_pwm_of_table);

#ifdef CONFIG_PM_SLEEP
static int rcar_pwm_suspend(struct device *dev)
{
	int ret = 0;
#ifdef CONFIG_RCAR_DDR_BACKUP
	struct platform_device *pdev = to_platform_device(dev);
	struct rcar_ip *ip = rcar_pwm_get_ip(pdev->name);

	if (ip) {
		struct rcar_pwm_chip *pwm = platform_get_drvdata(pdev);

		if (!ip->virt_addr)
			ip->virt_addr = pwm->base;

		ret = rcar_handle_registers(ip, DO_BACKUP);
	} else {
		pr_err("%s: Failed to find PWM device\n", __func__);
		ret = -ENODEV;
	}
#endif /* CONFIG_RCAR_DDR_BACKUP */
	return ret;
}

static int rcar_pwm_resume(struct device *dev)
{
	int ret = 0;
#ifdef CONFIG_RCAR_DDR_BACKUP
	struct platform_device *pdev = to_platform_device(dev);
	struct rcar_ip *ip = rcar_pwm_get_ip(pdev->name);

	if (ip)
		ret = rcar_handle_registers(ip, DO_RESTORE);
	else {
		pr_err("%s: Failed to find PWM device\n", __func__);
		ret = -ENODEV;
	}
#endif /* CONFIG_RCAR_DDR_BACKUP */
	return ret;
}

static SIMPLE_DEV_PM_OPS(rcar_pwm_pm_ops,
			rcar_pwm_suspend, rcar_pwm_resume);
#define DEV_PM_OPS (&rcar_pwm_pm_ops)
#else
#define DEV_PM_OPS NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver rcar_pwm_driver = {
	.probe = rcar_pwm_probe,
	.remove = rcar_pwm_remove,
	.driver = {
		.name = "pwm-rcar",
		.pm	= DEV_PM_OPS,
		.of_match_table = of_match_ptr(rcar_pwm_of_table),
	}
};
module_platform_driver(rcar_pwm_driver);

MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
MODULE_DESCRIPTION("Renesas PWM Timer Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pwm-rcar");
