/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _RCAR_MFIS_H_
#define _RCAR_MFIS_H_

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/notifier.h>

struct rcar_mfis_ch {
	unsigned int id;
	int initialized;
	void __iomem *base;
	struct atomic_notifier_head notifier_head;
	void *notifier_data;
};

#define NUM_MFIS_CHANNELS       64

struct rcar_mfis_priv {
	struct platform_device *pdev;
	struct rcar_mfis_ch channels[NUM_MFIS_CHANNELS];
};

static inline u32 rcar_mfis_reg_read(struct rcar_mfis_ch *chan, u32 reg)
{
	return ioread32(chan->base + reg);
}

static inline void rcar_mfis_reg_write(struct rcar_mfis_ch *chan, u32 reg, u32 data)
{
	iowrite32(data, chan->base + reg);
}

#endif
