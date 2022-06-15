// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet-TSN gPTP device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "rtsn_ptp.h"
#define ptp_to_priv(ptp)	container_of(ptp, struct rtsn_ptp_private, info)

static const struct rtsn_ptp_reg_offset s4_offs = {
	.enable = PTPTMEC,
	.disable = PTPTMDC,
	.increment = PTPTIVC0,
	.config_t0 = PTPTOVC00,
	.config_t1 = PTPTOVC10,
	.config_t2 = PTPTOVC20,
	.monitor_t0 = PTPGPTPTM00,
	.monitor_t1 = PTPGPTPTM10,
	.monitor_t2 = PTPGPTPTM20,
};

static const struct rtsn_ptp_reg_offset v4h_offs = {
	.enable		= TME,
	.disable	= TMD,
	.increment	= GTIVC,
	.config_t0	= GTOV00,
	.config_t1	= GTOV10,
	.config_t2	= GTOV20,
	.monitor_t0	= GTOV00,
	.monitor_t1	= GTOV10,
	.monitor_t2	= GTOV20,
};

static int rtsn_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct rtsn_ptp_private *ptp_priv = ptp_to_priv(ptp);
	const struct rtsn_ptp_reg_offset *offs = ptp_priv->offs;
	s64 addend = ptp_priv->default_addend;
	s64 diff;
	bool neg_adj = scaled_ppm < 0 ? true : false;

	if (ptp_priv->parallel_mode)
		return -EOPNOTSUPP;

	if (neg_adj)
		scaled_ppm = -scaled_ppm;
	diff = div_s64(addend * scaled_ppm_to_ppb(scaled_ppm), NSEC_PER_SEC);
	addend = neg_adj ? addend - diff : addend + diff;

	iowrite32(addend, ptp_priv->addr + offs->increment);

	return 0;
}

static int rtsn_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct rtsn_ptp_private *ptp_priv = ptp_to_priv(ptp);
	const struct rtsn_ptp_reg_offset *offs = ptp_priv->offs;

	ts->tv_nsec = readl(ptp_priv->addr + offs->monitor_t0);
	ts->tv_sec = readl(ptp_priv->addr + offs->monitor_t1) |
		     ((s64)readl(ptp_priv->addr + offs->monitor_t2) << 32);

	return 0;
}

static int rtsn_ptp_settime(struct ptp_clock_info *ptp, const struct timespec64 *ts)
{
	struct rtsn_ptp_private *ptp_priv = ptp_to_priv(ptp);
	const struct rtsn_ptp_reg_offset *offs = ptp_priv->offs;

	if (ptp_priv->parallel_mode)
		return -EOPNOTSUPP;

	writel(1, ptp_priv->addr + offs->disable);
	writel(0, ptp_priv->addr + offs->config_t2);
	writel(0, ptp_priv->addr + offs->config_t1);
	writel(0, ptp_priv->addr + offs->config_t0);
	writel(1, ptp_priv->addr + offs->enable);
	writel(ts->tv_sec >> 32, ptp_priv->addr + offs->config_t2);
	writel(ts->tv_sec, ptp_priv->addr + offs->config_t1);
	writel(ts->tv_nsec, ptp_priv->addr + offs->config_t0);

	return 0;
}

static int rtsn_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct rtsn_ptp_private *ptp_priv = ptp_to_priv(ptp);
	struct timespec64 ts;
	s64 now;

	if (ptp_priv->parallel_mode)
		return -EOPNOTSUPP;

	rtsn_ptp_gettime(ptp, &ts);
	now = ktime_to_ns(timespec64_to_ktime(ts));
	ts = ns_to_timespec64(now + delta);
	rtsn_ptp_settime(ptp, &ts);

	return 0;
}

static int rtsn_ptp_enable(struct ptp_clock_info *ptp, struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static struct ptp_clock_info rtsn_ptp_info = {
	.owner = THIS_MODULE,
	.name = "rtsn-ptp",
	.max_adj = 50000000,
	.adjfine = rtsn_ptp_adjfine,
	.adjtime = rtsn_ptp_adjtime,
	.gettime64 = rtsn_ptp_gettime,
	.settime64 = rtsn_ptp_settime,
	.enable = rtsn_ptp_enable,
};

static void rtsn_ptp_set_offs(struct rtsn_ptp_private *ptp_priv, enum rtsn_ptp_reg_layout layout)
{
	switch (layout) {
	case RTSN_PTP_REG_LAYOUT_S4:
		ptp_priv->offs = &s4_offs;
		break;
	case RTSN_PTP_REG_LAYOUT_V4H:
		ptp_priv->offs = &v4h_offs;
		break;
	default:
		WARN_ON(1);
	}
}

int rtsn_ptp_init(struct rtsn_ptp_private *ptp_priv, enum rtsn_ptp_reg_layout layout, u32 clock)
{
	if (ptp_priv->initialized)
		return 0;

	rtsn_ptp_set_offs(ptp_priv, layout);

	if (ptp_priv->parallel_mode) {
		ptp_priv->default_addend = ioread32(ptp_priv->addr + ptp_priv->offs->increment);
	} else {
		ptp_priv->default_addend = clock;
		iowrite32(ptp_priv->default_addend, ptp_priv->addr + ptp_priv->offs->increment);
	}

	ptp_priv->clock = ptp_clock_register(&ptp_priv->info, NULL);
	if (IS_ERR(ptp_priv->clock))
		return PTR_ERR(ptp_priv->clock);

	if (!ptp_priv->parallel_mode)
		writel(0x01, ptp_priv->addr + ptp_priv->offs->enable);

	ptp_priv->initialized = true;

	return 0;
}

struct rtsn_ptp_private *rtsn_ptp_alloc(struct platform_device *pdev)
{
	struct rtsn_ptp_private *ptp;

	ptp = devm_kzalloc(&pdev->dev, sizeof(*ptp), GFP_KERNEL);
	if (!ptp)
		return NULL;

	ptp->info = rtsn_ptp_info;

	return ptp;
}
