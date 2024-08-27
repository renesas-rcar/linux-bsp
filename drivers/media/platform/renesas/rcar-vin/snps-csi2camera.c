// SPDX-License-Identifier: GPL-2.0
/*
 * Dummy Driver for Synopsys CSI-2 Camera model on VDK
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 */

#include <linux/of.h>
#include <linux/of_device.h>

#include "snps-csi2camera.h"

static u32 csi2cam_read(struct csi2cam *priv, unsigned int reg)
{
	return ioread32(priv->base + reg);
}

static void csi2cam_write(struct csi2cam *priv, unsigned int reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

static void csi2cam_modify(struct csi2cam *priv, unsigned int reg, u32 data, u32 mask)
{
	u32 tmp, val, start_bit = 0;

	tmp = mask;
	while ((tmp & 1) == 0) {
		tmp >>= 1;
		start_bit++;
	}

	val = csi2cam_read(priv, reg);
	val &= ~mask;
	val |= (data << start_bit);
	csi2cam_write(priv, reg, val);
}

static int csi2cam_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *mem;
	struct csi2cam *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		pr_err("Unable to get memory resource\n");
		return -ENODEV;
	}

	CSI2CAMERA_DBG("CSI2Camera physical base address = 0x%08x", (uint32_t)mem->start);

	priv->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(priv->base)) {
		pr_err("Unable to map regs\n");
		return PTR_ERR(priv->base);
	}

	platform_set_drvdata(pdev, priv);
	CSI2CAMERA_DBG("Found %s", pdev->name);

	return ret;
}

static void csi2cam_remove(struct platform_device *pdev)
{
	struct csi2cam *priv = platform_get_drvdata(pdev);

	priv->dev = NULL;
	priv->base = NULL;

	return;
}

int csi2cam_start(struct csi2cam *priv, unsigned int width, unsigned int height, unsigned int bus_fmt)
{
	unsigned int timeout;
	u32 frame_count;

	csi2cam_modify(priv, SIZE_REG, width, SIZE_WIDTH);
	csi2cam_modify(priv, SIZE_REG, height, SIZE_HEIGHT);
	csi2cam_write(priv, FRAMES_PER_SECOND, 0x1e); /* 30fps */
	csi2cam_write(priv, MAX_FRAMES, 0x2);
	csi2cam_modify(priv, DOL_CONFIG, 0x1, DOL_CONFIG_DOL);
	csi2cam_modify(priv, DOL_CONFIG, 0x0, DOL_CONFIG_ENABLED);
	if (bus_fmt == MEDIA_BUS_FMT_RGB888_1X24) {
		/* RGB */
		csi2cam_modify(priv, IMAGE_CONFIG_INPUT, 0xf, IMAGE_CONFIG_INPUT_FORMAT);
		csi2cam_modify(priv, IMAGE_CONFIG_OUTPUT, 0x24, IMAGE_CONFIG_OUTPUT_DATA_TYPE);
	} else if (bus_fmt == MEDIA_BUS_FMT_Y10_1X10) {
		/* UYVY */
		csi2cam_modify(priv, IMAGE_CONFIG_INPUT, 0x5, IMAGE_CONFIG_INPUT_FORMAT);
		csi2cam_modify(priv, IMAGE_CONFIG_OUTPUT, 0x2b, IMAGE_CONFIG_OUTPUT_DATA_TYPE);
		/* ODD_RGRG_EVEN_GBGB (for RAW only) */
		csi2cam_modify(priv, IMAGE_CONFIG_OUTPUT, 0x0, IMAGE_CONFIG_OUTPUT_RAW_FORMAT);
	} else {
		return -EINVAL;
	}
	csi2cam_modify(priv, CONTROL_REG, 0x1, CONTROL_EN);	/* Start camera */

	for (timeout = 0; timeout <= 10; timeout++) {
		frame_count = csi2cam_read(priv, STATUS_REG) & STATUS_FRAME_COUNT;
		if (frame_count) {
			CSI2CAMERA_DBG("SNPS CSI-2 Camera has been started");
			return 0;
		}
		usleep_range(1000, 2000);
	}
	pr_err("Failed to start camera (frame count = %d)\n", frame_count);

	return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(csi2cam_start);

int csi2cam_stop(struct csi2cam *priv)
{
	csi2cam_modify(priv, CONTROL_REG, 0x0, CONTROL_EN);
	CSI2CAMERA_DBG("SNPS CSI-2 Camera has been stopped");

	return 0;
}
EXPORT_SYMBOL_GPL(csi2cam_stop);

static const struct of_device_id csi2cam_of_match[] = {
	{ .compatible = "snps,csi2cam" },
	{ },
};

static struct platform_driver csi2cam_pdrv = {
	.remove	= csi2cam_remove,
	.probe	= csi2cam_probe,
	.driver	= {
		.name	= "snps-csi2cam",
		.of_match_table	= csi2cam_of_match,
	},
};

module_platform_driver(csi2cam_pdrv);

MODULE_AUTHOR("Linh Phung <linh.phung.jy@renesas.com>");
MODULE_DESCRIPTION("Dummy Driver for Synopsys CSI-2 Camera model on VDK");
MODULE_LICENSE("GPL");
