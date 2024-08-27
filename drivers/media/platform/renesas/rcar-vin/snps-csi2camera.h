/* SPDX-License-Identifier: GPL-2.0
 * Dummy Driver for Synopsys Generic CSI-2 Camera model on VDK
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 */

#ifndef __SNPS_CSI2CAMERA__
#define __SNPS_CSI2CAMERA__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/media-bus-format.h>

#ifdef DEBUG
#define CSI2CAMERA_DBG(fmt, args...) \
		printk("%s: " fmt "\n", __func__, ##args)
#else
#define CSI2CAMERA_DBG(fmt, args...) do { } while (0)
#endif

#define CONTROL_REG								0x0
#define CONTROL_EN								BIT(0)
#define CONTROL_LINE_SYNC						BIT(1)
#define CONTROL_RAW_FMT							GENMASK(3, 2)
#define CONTROL_DT								GENMASK(9, 4)
#define CONTROL_VC								GENMASK(11, 10)
#define CONTROL_FPS								GENMASK(19, 12)
#define CONTROL_MAX_FRAME_NUM					GENMASK(31, 20)

#define SIZE_REG								0x4
#define SIZE_WIDTH								GENMASK(15, 0)
#define SIZE_HEIGHT								GENMASK(31, 16)

#define STATUS_REG								0x8
#define STATUS_FRAME_COUNT						GENMASK(15, 0)
#define STATUS_FRAME_COUNT_OV					BIT(16)
#define STATUS_ERR								BIT(31)

#define FRAMES_PER_SECOND						0xC

#define MAX_FRAMES								0x10

#define DOL_CONFIG								0x14
#define DOL_CONFIG_DOL							GENMASK(2, 1)
#define DOL_CONFIG_ENABLED						BIT(0)

#define OUTPUT_VIRTUAL_CHANNEL(n)				(0x18 + n)	/* n = 0,1,2 */

#define IMAGE_CONFIG_INPUT						0x24
#define IMAGE_CONFIG_INPUT_FORMAT				GENMASK(7, 0)

#define IMAGE_CONFIG_OUTPUT						0x28
#define IMAGE_CONFIG_OUTPUT_RAW_FORMAT			GENMASK(15, 8)
#define IMAGE_CONFIG_OUTPUT_DATA_TYPE			GENMASK(7, 0)

#define LUMINANCE_CONFIG_INPUT					0x2C
#define LUMINANCE_CONFIG_INPUT_FORMAT			GENMASK(7, 0)

#define LUMINANCE_CONFIG_OUTPUT					0x30
#define LUMINANCE_CONFIG_OUTPUT_RAW_FORMAT		GENMASK(15, 8)
#define LUMINANCE_CONFIG_OUTPUT_DATA_TYPE		GENMASK(7, 0)

#define PDAF_CONFIG_INPUT						0x34

#define PDAF_CONFIG_OUTPUT						0x38
#define PDAF_CONFIG_OUTPUT_BYTES_PER_TRANSFER	GENMASK(23, 8)
#define PDAF_CONFIG_OUTPUT_DATA_TYPE			GENMASK(7, 0)

struct csi2cam {
	struct device *dev;
	void __iomem *base;
};

int csi2cam_start(struct csi2cam *priv, unsigned int width, unsigned int height, unsigned int bus_fmt);
int csi2cam_stop(struct csi2cam *priv);
int advancedcsi2cam_start(struct csi2cam *priv);
int advancedcsi2cam_stop(struct csi2cam *priv);

#endif
