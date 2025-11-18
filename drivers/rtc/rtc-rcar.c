// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Renesas R-Car RTC unit
 *
 * Copyright (C) 2024 Renesas Electronics Corporation
 *
 */
#include <linux/clk.h>
#include <linux/bcd.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/iopoll.h>

#define RCAR_RTCA_CTL0		0x00
#define RCAR_RTCA_CTL0_CE	BIT(7)
#define RCAR_RTCA_CTL0_CEST	BIT(6)
#define RCAR_RTCA_CTL0_AMPM	BIT(5)
#define RCAR_RTCA_CTL0_SLSB	BIT(4)

#define RCAR_RTCA_CTL1		0x04
#define RCAR_RTCA_CTL1_EN1HZ	BIT(5)
#define RCAR_RTCA_CTL1_ENALM	BIT(4)
#define RCAR_RTCA_CTL1_EN1S	BIT(3)
#define RCAR_RTCA_CTL1_CT_MASK	GENMASK(2, 0)
#define RCAR_RTCA_CTL1_CT1HZ	0x3
#define RCAR_RTCA_CTL1_CT2HZ	0x2
#define RCAR_RTCA_CTL1_CT4HZ	0x1
#define RCAR_RTCA_CTL1_CT0	0

#define RCAR_RTCA_CTL2		0x08
#define RCAR_RTCA_CTL2_WAIT	BIT(0)
#define RCAR_RTCA_CTL2_WST	BIT(1)
#define RCAR_RTCA_CTL2_STOPPED (RCAR_RTCA_CTL2_WAIT | RCAR_RTCA_CTL2_WST)

#define RCAR_RTCA_SEC		0x14
#define RCAR_RTCA_MIN		0x18
#define RCAR_RTCA_HOUR		0x1c
#define RCAR_RTCA_TIME		0x30
#define RCAR_RTCA_CAL		0x34
#define RCAR_RTCA_SCMP		0x3c

#define RCAR_RTCA_ALM		0x40
#define RCAR_RTCA_ALH		0x44
#define RCAR_RTCA_ALW		0x48

#define RCAR_RTCA_SECC		0x4c
#define RCAR_RTCA_MINC		0x50
#define RCAR_RTCA_HOURC		0x54
#define RCAR_RTCA_WEEKC		0x58
#define RCAR_RTCA_DAYC		0x5c
#define RCAR_RTCA_MONC		0x60
#define RCAR_RTCA_YEARC		0x64

#define RCAR_RTCA_TIMEC		0x68
#define RCAR_RTCA_TIME_S	GENMASK(6, 0)
#define RCAR_RTCA_TIME_M	GENMASK(14, 8)
#define RCAR_RTCA_TIME_H	GENMASK(21, 16)

#define RCAR_RTCA_CALC		0x6c
#define RCAR_RTCA_CAL_WD	GENMASK(2, 0)
#define RCAR_RTCA_CAL_D		GENMASK(13, 8)
#define RCAR_RTCA_CAL_M		GENMASK(20, 16)
#define RCAR_RTCA_CAL_Y		GENMASK(31, 24)

#define SLSB_CHECK_RETRIES	4

struct rcar_rtc_priv {
	void __iomem		*base;
	struct rtc_device	*rtc_dev;
	struct clk		*ref_clk;
	unsigned int ref_clk_freq;
	int alarm_irq, update_irq, periodic_irq;
	int irq_freq;
};

static void rcar_rtc_output_enable(struct device *dev, unsigned int enabled)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev);
	u8 ctl1;

	ctl1 = readb(rtc->base + RCAR_RTCA_CTL1);

	if (enabled)
		ctl1 |= RCAR_RTCA_CTL1_EN1HZ;
	else
		ctl1 &= ~RCAR_RTCA_CTL1_EN1HZ;

	writeb(ctl1, rtc->base + RCAR_RTCA_CTL1);
}

static unsigned int rcar_rtc_tm_to_wday(struct rtc_time *tm)
{
	time64_t time;
	unsigned int days;
	u32 secs;

	time = rtc_tm_to_time64(tm);
	days = div_s64_rem(time, 86400, &secs);

	/* day of the week, 1970-01-01 was a Thursday */
	return (days + 4) % 7;
}

static int rcar_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev);
	u8 val, secs;
	u32 time, cal;

	/*
	 * The RTC was not started or is stopped and thus does not carry the
	 * proper time/date.
	 */
	val = readb(rtc->base + RCAR_RTCA_CTL2);
	if (val & RCAR_RTCA_CTL2_STOPPED)
		return -EINVAL;

	tm->tm_sec = readb(rtc->base + RCAR_RTCA_SECC);
	time = readl(rtc->base + RCAR_RTCA_TIMEC);
	cal = readl(rtc->base + RCAR_RTCA_CALC);
	secs = readb(rtc->base + RCAR_RTCA_SECC);

	if (tm->tm_sec != secs) {
		time = readl(rtc->base + RCAR_RTCA_TIMEC);
		cal = readl(rtc->base + RCAR_RTCA_CALC);
	}

	tm->tm_sec  = bcd2bin(FIELD_GET(RCAR_RTCA_TIME_S, time));
	tm->tm_min  = bcd2bin(FIELD_GET(RCAR_RTCA_TIME_M, time));
	tm->tm_hour = bcd2bin(FIELD_GET(RCAR_RTCA_TIME_H, time));
	tm->tm_mday = bcd2bin(FIELD_GET(RCAR_RTCA_CAL_D, cal));

	/*
	 * This device returns months from 1 to 12.
	 * But rtc_time.tm_mon expects a value in the range 0 to 11.
	 */
	tm->tm_mon  = bcd2bin(FIELD_GET(RCAR_RTCA_CAL_M, cal)) - 1;

	/*
	 * This device's Epoch is 2000.
	 * But rtc_time.tm_year expects years from Epoch 1900.
	 */
	tm->tm_year = bcd2bin(FIELD_GET(RCAR_RTCA_CAL_Y, cal)) + 100;
	tm->tm_wday = bcd2bin(FIELD_GET(RCAR_RTCA_CAL_WD, cal));

	return 0;
}

static int rcar_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev);
	int ret;
	u8 val;
	u32 time, cal;

	time = FIELD_PREP(RCAR_RTCA_TIME_S, bin2bcd(tm->tm_sec))
	     | FIELD_PREP(RCAR_RTCA_TIME_M, bin2bcd(tm->tm_min))
	     | FIELD_PREP(RCAR_RTCA_TIME_H, bin2bcd(tm->tm_hour));

	/*
	 * This device expects months from 1 to 12.
	 * But rtc_time.tm_mon contains months from 0 to 11.
	 *
	 * This device expects year since Epoch 2000.
	 * But rtc_time.tm_year contains year since Epoch 1900.
	 */
	cal = FIELD_PREP(RCAR_RTCA_CAL_D, bin2bcd(tm->tm_mday))
	    | FIELD_PREP(RCAR_RTCA_CAL_M, bin2bcd(tm->tm_mon + 1))
	    | FIELD_PREP(RCAR_RTCA_CAL_Y, bin2bcd(tm->tm_year - 100))
	    | FIELD_PREP(RCAR_RTCA_CAL_WD, bin2bcd(rcar_rtc_tm_to_wday(tm)));

	val = readb(rtc->base + RCAR_RTCA_CTL2);
	if (!(val & RCAR_RTCA_CTL2_STOPPED)) {
		/* Hold the counter if it was counting up */
		writeb(RCAR_RTCA_CTL2_WAIT, rtc->base + RCAR_RTCA_CTL2);

		/* Wait for the counter to stop: two 32k clock cycles */
		usleep_range(61, 100);
		ret = readb_poll_timeout(rtc->base + RCAR_RTCA_CTL2, val,
					 val & RCAR_RTCA_CTL2_WST, 0, 100);
		if (ret)
			return ret;
	}

	writel(time, rtc->base + RCAR_RTCA_TIME);
	writel(cal, rtc->base + RCAR_RTCA_CAL);

	val = readb(rtc->base + RCAR_RTCA_CTL2);
	val &= ~RCAR_RTCA_CTL2_WAIT;
	writeb(val, rtc->base + RCAR_RTCA_CTL2);

	return 0;
}

static irqreturn_t rcar_rtc_update_irq_handler(int irq, void *id)
{
	struct rcar_rtc_priv *rtc = id;

	rtc_update_irq(rtc->rtc_dev, 1, RTC_IRQF | RTC_UF);

	return IRQ_HANDLED;
}

static irqreturn_t rcar_rtc_periodic_irq_handler(int irq, void *id)
{
	struct rcar_rtc_priv *rtc = id;

	rtc_update_irq(rtc->rtc_dev, 1, RTC_IRQF | RTC_PF);

	return IRQ_HANDLED;
}

static irqreturn_t rcar_rtc_alarm_irq_handler(int irq, void *id)
{
	struct rcar_rtc_priv *rtc = id;

	rtc_update_irq(rtc->rtc_dev, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static void rcar_rtc_update_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev);
	u8 ctl1;

	ctl1 = readb(rtc->base + RCAR_RTCA_CTL1);

	if (enabled)
		ctl1 |= RCAR_RTCA_CTL1_EN1S;
	else
		ctl1 &= ~RCAR_RTCA_CTL1_EN1S;

	writeb(ctl1, rtc->base + RCAR_RTCA_CTL1);
}

static int rcar_rtc_periodic_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev);
	u8 ctl1;

	ctl1 = readb(rtc->base + RCAR_RTCA_CTL1);
	ctl1 &= ~RCAR_RTCA_CTL1_CT_MASK;

	if (enabled) {
		/* Disable it before update new irq_freq */
		writeb(ctl1, rtc->base + RCAR_RTCA_CTL1);

		switch (rtc->irq_freq) {
		case 1:
			ctl1 |= RCAR_RTCA_CTL1_CT1HZ;
			break;
		case 2:
			ctl1 |= RCAR_RTCA_CTL1_CT2HZ;
			break;
		case 4:
			ctl1 |= RCAR_RTCA_CTL1_CT4HZ;
			break;
		default:
			dev_err(dev, "unsupported interrupt frequency: %d\n",
				rtc->irq_freq);
			return -EINVAL;
		}
	}

	writeb(ctl1, rtc->base + RCAR_RTCA_CTL1);

	return 0;
}

static int rcar_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev);
	u8 ctl1;

	ctl1 = readb(rtc->base + RCAR_RTCA_CTL1);
	if (enabled)
		ctl1 |= RCAR_RTCA_CTL1_ENALM;
	else
		ctl1 &= ~RCAR_RTCA_CTL1_ENALM;

	writeb(ctl1, rtc->base + RCAR_RTCA_CTL1);

	return 0;
}

static int rcar_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	unsigned int min, hour, wday, delta_days;
	time64_t alarm;
	u32 ctl1;
	int ret;

	ret = rcar_rtc_read_time(dev, tm);
	if (ret)
		return ret;

	min = readb(rtc->base + RCAR_RTCA_ALM);
	hour = readb(rtc->base + RCAR_RTCA_ALH);
	wday = readb(rtc->base + RCAR_RTCA_ALW);

	tm->tm_sec = 0;
	tm->tm_min = bcd2bin(min);
	tm->tm_hour = bcd2bin(hour);
	delta_days = ((fls(wday) - 1) - tm->tm_wday + 7) % 7;
	tm->tm_wday = fls(wday) - 1;

	if (delta_days) {
		alarm = rtc_tm_to_time64(tm) + (delta_days * 86400);
		rtc_time64_to_tm(alarm, tm);
	}

	ctl1 = readb(rtc->base + RCAR_RTCA_CTL1);
	alrm->enabled = !!(ctl1 & RCAR_RTCA_CTL1_ENALM);

	return 0;
}

static int rcar_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time, tm_now;
	unsigned long alarm, farest;
	unsigned int days_ahead, wday;
	int ret;

	ret = rcar_rtc_read_time(dev, &tm_now);
	if (ret)
		return ret;

	rcar_rtc_alarm_irq_enable(dev, 0);

	/* We cannot set alarms more than one week ahead */
	farest = rtc_tm_to_time64(&tm_now) + 7 * 86400;
	alarm = rtc_tm_to_time64(tm);
	if (time_after(alarm, farest))
		return -ERANGE;

	/* Convert alarm day into week day */
	days_ahead = tm->tm_mday - tm_now.tm_mday;

	wday = (tm_now.tm_wday + days_ahead) % 7;

	writeb(bin2bcd(tm->tm_min), rtc->base + RCAR_RTCA_ALM);
	writeb(bin2bcd(tm->tm_hour), rtc->base + RCAR_RTCA_ALH);
	writeb(BIT(wday), rtc->base + RCAR_RTCA_ALW);

	rcar_rtc_alarm_irq_enable(dev, alrm->enabled);

	return 0;
}

/*
 * Provide additional RTC information in /proc/driver/rtc
 */

#ifdef CONFIG_PROC_FS
static int rcar_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev);
	u8 ctl1;

	ctl1 = readb(rtc->base + RCAR_RTCA_CTL1);

	seq_printf(seq,
		   "HW update IRQ enabled\t\t: %s\n"
		   "HW periodic IRQ enabled\t\t: %s\n"
		   "HW periodic IRQ frequency\t: %d\n"
		   "HW 1Hz pulse output enabled\t: %s\n",
		   (ctl1 & RCAR_RTCA_CTL1_EN1S) ? "yes" : "no",
		   (ctl1 & RCAR_RTCA_CTL1_CT_MASK) ? "yes" : "no",
		   rtc->irq_freq,
		   (ctl1 & RCAR_RTCA_CTL1_EN1HZ) ? "yes" : "no");
	return 0;
}
#else
#define rcar_rtc_proc	NULL
#endif

static const struct rtc_class_ops rcar_rtc_ops = {
	.read_time		= rcar_rtc_read_time,
	.set_time		= rcar_rtc_set_time,
	.read_alarm		= rcar_rtc_read_alarm,
	.set_alarm		= rcar_rtc_set_alarm,
	.alarm_irq_enable	= rcar_rtc_alarm_irq_enable,
	.proc			= rcar_rtc_proc,
};

/* SysFS interface */

/*
 * R-Car RTC can generate:
 * Update interrupt (1Hz),
 * Periodic interrupt (1Hz, 2Hz, 4Hz)
 * 1Hz pulse output.
 * Each function can be controlled separately.
 */

/*
 * uie - sysfs file for update interrupt control.
 */

static ssize_t
rcar_rtc_sysfs_uie_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev->parent);
	u8 ctl1;

	ctl1 = readb(rtc->base + RCAR_RTCA_CTL1);

	return sprintf(buf, "%d\n", !!(ctl1 & RCAR_RTCA_CTL1_EN1S));
}

static ssize_t
rcar_rtc_sysfs_uie_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t n)
{
	int ret;
	unsigned int val;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	switch (val) {
	case 0:
	case 1:
		rcar_rtc_update_irq_enable(dev->parent, val);
		break;
	default:
		return -EINVAL;
	}

	return n;
}

static DEVICE_ATTR(uie, 0644,
		   rcar_rtc_sysfs_uie_show,
		   rcar_rtc_sysfs_uie_store);

/*
 * pie - sysfs file for periodic interrupt control.
 */

static ssize_t
rcar_rtc_sysfs_pie_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev->parent);
	u8 ctl1;

	ctl1 = readb(rtc->base + RCAR_RTCA_CTL1);

	return sprintf(buf, "%d\n", !!(ctl1 & RCAR_RTCA_CTL1_CT_MASK));
}

static ssize_t
rcar_rtc_sysfs_pie_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t n)
{
	int ret;
	unsigned int val;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	switch (val) {
	case 0:
	case 1:
		ret = rcar_rtc_periodic_irq_enable(dev->parent, val);
		break;
	default:
		return -EINVAL;
	}

	return ret ? ret : n;
}
static DEVICE_ATTR(pie, 0644,
		   rcar_rtc_sysfs_pie_show,
		   rcar_rtc_sysfs_pie_store);

/*
 * irq_freq - sysfs file for periodic interrupt frequency control.
 */

static ssize_t
rcar_rtc_sysfs_irq_freq_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev->parent);

	return sprintf(buf, "%d\n", rtc->irq_freq);
}

static ssize_t
rcar_rtc_sysfs_irq_freq_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t n)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev->parent);
	int val, ret;
	u8 ctl1;
	unsigned int pie;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	switch (val) {
	case 1:
	case 2:
	case 4:
		rtc->irq_freq = val;
		break;
	default:
		return -EINVAL;
	}

	ctl1 = readb(rtc->base + RCAR_RTCA_CTL1);
	pie = !!(ctl1 & RCAR_RTCA_CTL1_CT_MASK);
	ret = rcar_rtc_periodic_irq_enable(dev->parent, pie);

	return ret ? ret : n;
}
static DEVICE_ATTR(irq_freq, 0644,
		   rcar_rtc_sysfs_irq_freq_show,
		   rcar_rtc_sysfs_irq_freq_store);

/*
 * output - sysfs file for 1Hz pulse output control.
 */

static ssize_t
rcar_rtc_sysfs_output_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev->parent);
	u8 ctl1;

	ctl1 = readb(rtc->base + RCAR_RTCA_CTL1);

	return sprintf(buf, "%d\n", !!(ctl1 & RCAR_RTCA_CTL1_EN1HZ));
}

static ssize_t
rcar_rtc_sysfs_output_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t n)
{
	int ret;
	unsigned int val;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	switch (val) {
	case 0:
	case 1:
		rcar_rtc_output_enable(dev->parent, val);
		break;
	default:
		return -EINVAL;
	}

	return n;
}
static DEVICE_ATTR(output, 0644,
		   rcar_rtc_sysfs_output_show,
		   rcar_rtc_sysfs_output_store);

/*
 * struct rcar_rtc_sysfs_attrs - list for RTC features.
 */
static struct attribute *rcar_rtc_sysfs_attrs[] = {
	&dev_attr_uie.attr,
	&dev_attr_pie.attr,
	&dev_attr_irq_freq.attr,
	&dev_attr_output.attr,
	NULL
};

static umode_t rcar_rtc_attr_is_visible(struct kobject *kobj,
					struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct rcar_rtc_priv *rtc = dev_get_drvdata(dev->parent);
	umode_t mode = attr->mode;

	if (attr == &dev_attr_uie.attr) {
		if (rtc->update_irq <= 0)
			mode = 0;
	} else if (attr == &dev_attr_pie.attr ||
		   attr == &dev_attr_irq_freq.attr) {
		if (rtc->periodic_irq <= 0)
			mode = 0;
	}

	return mode;
}

/*
 * struct rcar_rtc_sysfs_grp - attr group for RTC features.
 */
static const struct attribute_group rcar_rtc_sysfs_grp = {
	.name		= "rcar_hw_ctrl",
	.is_visible	= rcar_rtc_attr_is_visible,
	.attrs		= rcar_rtc_sysfs_attrs,
};

static int rcar_rtc_init(struct rcar_rtc_priv *rtc, struct device *dev, bool stopped)
{
	int ret;
	int i = 0;
	u8 val;
	bool slsb = false;

	if (!stopped) {
		writeb(0, rtc->base + RCAR_RTCA_CTL0);
		ret = readb_poll_timeout(rtc->base + RCAR_RTCA_CTL0, val,
					 !(val & RCAR_RTCA_CTL0_CEST), 100, 500);

		if (ret < 0) {
			dev_err(dev, "failed to stop RTC: %d\n", ret);
			return ret;
		}
	}

	do {
		writeb((RCAR_RTCA_CTL0_AMPM | RCAR_RTCA_CTL0_SLSB), rtc->base + RCAR_RTCA_CTL0);
		slsb = (readb(rtc->base + RCAR_RTCA_CTL0) & RCAR_RTCA_CTL0_SLSB) ? true : false;
		i++;
		msleep(20);
	} while ((i < SLSB_CHECK_RETRIES) && (!slsb));

	if (!slsb) {
		ret = -ETIMEDOUT;
		dev_err(dev, "failed to initialize RTC: %d\n", ret);
		return ret;
	}

	writel((rtc->ref_clk_freq - 1), rtc->base + RCAR_RTCA_SCMP);

	val = readb(rtc->base + RCAR_RTCA_CTL0);
	val |= RCAR_RTCA_CTL0_CE;
	writeb(val, rtc->base + RCAR_RTCA_CTL0);

	return 0;
}

static int rcar_rtc_probe(struct platform_device *pdev)
{
	struct rcar_rtc_priv *rtc;
	struct device *dev = &pdev->dev;
	int ret;
	u8 cest;
	u32 scmp;

	rtc = devm_kzalloc(&pdev->dev, sizeof(struct rcar_rtc_priv), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rtc->base))
		return PTR_ERR(rtc->base);

	rtc->update_irq = platform_get_irq_byname_optional(pdev, "update");
	if (rtc->update_irq > 0) {
		ret = devm_request_irq(&pdev->dev, rtc->update_irq,
				       rcar_rtc_update_irq_handler, 0,
				       "rcar-rtc update irq", &pdev->dev);

		if (ret) {
			dev_err(&pdev->dev,
				"Failed to request update interrupt for the device, %d\n",
				ret);
			rtc->update_irq = 0;
		}
	}

	rtc->alarm_irq = platform_get_irq_byname_optional(pdev, "alarm");
	if (rtc->alarm_irq > 0) {
		ret = devm_request_irq(&pdev->dev, rtc->alarm_irq,
				       rcar_rtc_alarm_irq_handler, 0,
				       "rcar-rtc alarm irq", &pdev->dev);

		if (ret) {
			dev_err(&pdev->dev,
				"Failed to request alarm interrupt for the device, %d\n",
				ret);
			rtc->alarm_irq = 0;
		}
	}

	rtc->periodic_irq = platform_get_irq_byname_optional(pdev, "period");
	if (rtc->periodic_irq > 0) {
		ret = devm_request_irq(&pdev->dev, rtc->periodic_irq,
				       rcar_rtc_periodic_irq_handler, 0,
				       "rcar-rtc periodic irq", &pdev->dev);

		if (ret) {
			dev_err(&pdev->dev,
				"Failed to request periodic interrupt for the device, %d\n",
				ret);
			rtc->periodic_irq = 0;
		}
	}

	rtc->ref_clk = devm_clk_get(dev, "ref_clk");
	if (IS_ERR(rtc->ref_clk)) {
		dev_err(dev,
			"Failed to retrieve the reference clock, %d\n", ret);
		return PTR_ERR(rtc->ref_clk);
	}

	rtc->rtc_dev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtc_dev))
		return PTR_ERR(rtc->rtc_dev);

	platform_set_drvdata(pdev, rtc);

	ret = clk_prepare_enable(rtc->ref_clk);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to enable the reference clock, %d\n", ret);
		goto err_disable_ref_clk;
	}

	rtc->ref_clk_freq = clk_get_rate(rtc->ref_clk);

	if (rtc->ref_clk_freq != 32768 &&
	    rtc->ref_clk_freq != 240000 &&
	    rtc->ref_clk_freq != 2083000) {
		dev_err(dev,
			"Invalid reference clock frequency %d Hz.\n",
			rtc->ref_clk_freq);
		return -EINVAL;
	}

	scmp = readl(rtc->base + RCAR_RTCA_SCMP);
	cest = readb(rtc->base + RCAR_RTCA_CTL0) & RCAR_RTCA_CTL0_CEST;

	if (cest != RCAR_RTCA_CTL0_CEST)
		ret = rcar_rtc_init(rtc, dev, 1);
	else if (scmp != (rtc->ref_clk_freq - 1))
		ret = rcar_rtc_init(rtc, dev, 0);

	if (ret)
		goto err_disable_ref_clk;

	/* Disable all interrupts and output pulse */
	writeb(0, rtc->base + RCAR_RTCA_CTL1);

	rtc->rtc_dev->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->rtc_dev->range_max = RTC_TIMESTAMP_END_2099;
	rtc->rtc_dev->ops = &rcar_rtc_ops;
	rtc->irq_freq = 1;

	/* Register sysfs attributes */
	ret = rtc_add_group(rtc->rtc_dev, &rcar_rtc_sysfs_grp);
	if (ret)
		goto err_disable_wakeup;

	if (rtc->alarm_irq > 0)
		device_init_wakeup(&pdev->dev, true);

	ret = devm_rtc_register_device(rtc->rtc_dev);
	if (ret)
		goto err_disable_wakeup;

	return 0;

err_disable_wakeup:
	device_init_wakeup(&pdev->dev, false);

err_disable_ref_clk:
	clk_disable_unprepare(rtc->ref_clk);

	return ret;
}

static int rcar_rtc_remove(struct platform_device *pdev)
{
	struct rcar_rtc_priv *rtc = dev_get_drvdata(&pdev->dev);

	/* Disable all interrupts and output pulse */
	writeb(0, rtc->base + RCAR_RTCA_CTL1);

	device_init_wakeup(&pdev->dev, 0);

	clk_disable_unprepare(rtc->ref_clk);

	return 0;
}

static const struct of_device_id rcar_rtc_of_table[] = {
	{ .compatible = "renesas,rtc-r8a78000", },
	{ .compatible = "renesas,rtc-rcar", },
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_rtc_of_table);

static struct platform_driver rcar_rtc_driver = {
	.probe = rcar_rtc_probe,
	.remove = rcar_rtc_remove,
	.driver = {
		.name = "rtc-rcar",
		.of_match_table = of_match_ptr(rcar_rtc_of_table),
	}
};
module_platform_driver(rcar_rtc_driver);

MODULE_AUTHOR("Khanh Le <khanh.le.xr@renesas.com>");
MODULE_DESCRIPTION("Renesas R-Car RTC Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtc-rcar");
