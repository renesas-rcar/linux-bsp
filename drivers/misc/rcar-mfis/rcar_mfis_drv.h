/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _RCAR_MFIS_H_
#define _RCAR_MFIS_H_

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>

#define UNLOCK_WRITE_PROT	0xACC00001

struct rcar_mfis_priv;

struct rcar_mfis_ch {
	unsigned int id;
	int initialized;
	void __iomem *base;
	struct atomic_notifier_head notifier_head;
	void *notifier_data;
	struct rcar_mfis_priv *priv;
};

#define NUM_MFIS_CHANNELS       64

struct rcar_mfis_priv {
	struct platform_device *pdev;
	void __iomem *unlock;
	spinlock_t lock;	/* lock for write access */
	struct rcar_mfis_ch channels[NUM_MFIS_CHANNELS];
};

static inline u32 rcar_mfis_reg_read(struct rcar_mfis_ch *chan, u32 reg)
{
	return ioread32(chan->base + reg);
}

static inline void rcar_mfis_reg_write(struct rcar_mfis_ch *chan, u32 reg, u32 data)
{
	struct rcar_mfis_priv *priv = chan->priv;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	iowrite32(UNLOCK_WRITE_PROT, priv->unlock);
	iowrite32(data, chan->base + reg);
	spin_unlock_irqrestore(&priv->lock, flags);
}

#endif
