#ifndef _RCAR_MFIS_H_
#define _RCAR_MFIS_H_

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/notifier.h>

struct rcar_mfis_ch {
	unsigned int id;
	int initialized;
	struct atomic_notifier_head notifier_head;
	void *notifier_data;
};

#define NUM_MFIS_CHANNELS       8

struct rcar_mfis_dev {
	struct platform_device *pdev;
	void __iomem *mmio_base;
	struct rcar_mfis_ch channels[NUM_MFIS_CHANNELS];
};

static inline u32 rcar_mfis_reg_read(struct rcar_mfis_dev *mfis_dev, u32 reg)
{
	return ioread32(mfis_dev->mmio_base + reg);
}

static inline void rcar_mfis_reg_write(struct rcar_mfis_dev *mfis_dev, u32 reg, u32 data)
{
	iowrite32(data, mfis_dev->mmio_base + reg);
}

#endif
