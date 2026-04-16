// SPDX-License-Identifier: GPL-2.0-only
/*
 * Remote processor messaging - performance measurement driver
 *
 * Copyright (C) 2026 Renesas Electronics Corp.
 */

#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/sched.h>

#define MSG		"Hello world!"
#define BYE_MSG		"Goodbye!"
#define LATENCY_RANGE	80000

static int count = 100000;
module_param(count, int, 0644);

struct rpmsg_perf_priv {
	struct completion rx_completion;
	struct task_struct *xfer_thread;
};

static int rpmsg_perf_cb(struct rpmsg_device *rpdev, void *data, int len,
			 void *priv, u32 src)
{
	struct rpmsg_perf_priv *pdata = dev_get_drvdata(&rpdev->dev);

	complete(&pdata->rx_completion);

	return 0;
}

static int rpmsg_perf_xfer(void *data)
{
	struct rpmsg_device *rpdev = data;
	struct rpmsg_perf_priv *priv = dev_get_drvdata(&rpdev->dev);
	struct timespec64 ts_start_test;
	struct timespec64 ts_end_test;
	struct timespec64 ts_current;
	struct timespec64 ts_end;
	u64 latency;
	u64 latency_worst_case	= 0;
	u64 latency_best_case	= U64_MAX;
	u64 latency_valid_total	= 0;
	s64 latency_average	= 0;
	int valid_count		= 0;
	int spike_count		= 0;
	int ret, i;

	dev_info(&rpdev->dev, "=================================\n");
	dev_info(&rpdev->dev, " RPMsg Performance Test\n");
	dev_info(&rpdev->dev, " Number of messages : %d\n", count);
	dev_info(&rpdev->dev, "=================================\n");

	ktime_get_ts64(&ts_start_test);

	for (i = 0; i < count; i++) {
		reinit_completion(&priv->rx_completion);

		ktime_get_ts64(&ts_current);

		if (i < count - 1)
			ret = rpmsg_send(rpdev->ept, MSG, strlen(MSG));
		else
			ret = rpmsg_send(rpdev->ept, BYE_MSG, strlen(BYE_MSG));

		if (ret) {
			dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
			return ret;
		}

		ret = wait_for_completion_timeout(&priv->rx_completion,
						  msecs_to_jiffies(1000));
		if (!ret) {
			dev_err(&rpdev->dev, "Timeout i=%d\n", i);
			continue;
		}

		ktime_get_ts64(&ts_end);

		latency = (ts_end.tv_sec - ts_current.tv_sec) * 1000000 +
			(ts_end.tv_nsec - ts_current.tv_nsec) / 1000;

		if (latency < latency_best_case)
			latency_best_case = latency;

		if (latency > latency_worst_case)
			latency_worst_case = latency;

		if (latency > LATENCY_RANGE) {
			dev_warn(&rpdev->dev, "[SPIKE] i=%d latency=%llu us\n", i, latency);
			spike_count++;
			continue;
		}

		latency_valid_total += latency;
		valid_count++;
	}

	ktime_get_ts64(&ts_end_test);

	if (valid_count > 0)
		latency_average = latency_valid_total / valid_count;

	dev_info(&rpdev->dev, "========== Results ==========");
	dev_info(&rpdev->dev, "Total samples  : %d",		count);
	dev_info(&rpdev->dev, "Valid  samples : %d",		valid_count);
	dev_info(&rpdev->dev, "Spike  samples : %d",		spike_count);
	dev_info(&rpdev->dev, "Best-case  RTT : %llu us",	latency_best_case);
	dev_info(&rpdev->dev, "Average    RTT : %lld us",	latency_average);
	dev_info(&rpdev->dev, "Worst-case RTT : %llu us",	latency_worst_case);
	dev_info(&rpdev->dev, "Total time	  : %lld s",
		 ts_end_test.tv_sec - ts_start_test.tv_sec);
	dev_info(&rpdev->dev, "=============================");

	return 0;
}

static int rpmsg_perf_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_perf_priv *priv;
	int ret;

	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n", rpdev->src, rpdev->dst);

	priv = devm_kzalloc(&rpdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	init_completion(&priv->rx_completion);
	dev_set_drvdata(&rpdev->dev, priv);

	/* Warm-up */
	ret = rpmsg_send(rpdev->ept, MSG, strlen(MSG));
	if (ret) {
		dev_err(&rpdev->dev, "warm-up send failed: %d\n", ret);
		return ret;
	}

	wait_for_completion_timeout(&priv->rx_completion, msecs_to_jiffies(1000));
	reinit_completion(&priv->rx_completion);

	priv->xfer_thread = kthread_create(rpmsg_perf_xfer, rpdev, "rpmsg_perf");
	if (IS_ERR(priv->xfer_thread))
		return PTR_ERR(priv->xfer_thread);

	set_user_nice(priv->xfer_thread, -20);
	wake_up_process(priv->xfer_thread);

	return 0;
}

static void rpmsg_perf_remove(struct rpmsg_device *rpdev)
{
	struct rpmsg_perf_priv *priv = dev_get_drvdata(&rpdev->dev);

	if (priv->xfer_thread)
		kthread_stop(priv->xfer_thread);

	dev_info(&rpdev->dev, "rpmsg perf driver is removed\n");
}

static const struct rpmsg_device_id rpmsg_perf_id_table[] = {
	{ .name	= "rpmsg-perf-sample" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_perf_id_table);

static struct rpmsg_driver rpmsg_perf_driver = {
	.drv.name	= KBUILD_MODNAME,
	.id_table	= rpmsg_perf_id_table,
	.probe		= rpmsg_perf_probe,
	.callback	= rpmsg_perf_cb,
	.remove		= rpmsg_perf_remove,
};
module_rpmsg_driver(rpmsg_perf_driver);

MODULE_AUTHOR("Phong Hoang <phong.hoang.wz@renesas.com>");
MODULE_DESCRIPTION("Remote processor messaging performance measurement driver");
MODULE_LICENSE("GPL");
