/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Renesas CRC-WRAPPER drivers header
 *
 * Copyright (C) 2024 Renesas Electronics Inc.
 *
 */

#ifndef _RENESAS_CRC_WRAPPER_H_
#define _RENESAS_CRC_WRAPPER_H_

#include <uapi/linux/usr_wcrc.h>
#include <linux/cdev.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#define WCRC_DEVICES 11

struct crc_device {
	void __iomem *base;
	struct device *dev;
	struct clk *clk;
};

struct kcrc_device {
	void __iomem *base;
	struct device *dev;
	struct clk *clk;
};

struct wcrc_device {
	// WCRC part
	void __iomem *base;
	struct resource *res;
	struct device *dev;
	struct clk *clk;
	struct cdev cdev;
	dev_t devt;
	int irq;
	int module;
	const struct wcrc_ops *ops;

	// WCRC sub-module
	struct crc_device *crc_dev;
	struct kcrc_device *kcrc_dev;

};

int kcrc_calculate(struct kcrc_device *p, struct wcrc_info *info);
void kcrc_setting(struct kcrc_device *p, struct wcrc_info *info);
int rcar_kcrc_init(struct platform_device *pdev);
int kcrc_drv_init(void);
void kcrc_drv_exit(void);

int crc_calculate(struct crc_device *p, struct wcrc_info *info);
void crc_setting(struct crc_device *p, struct wcrc_info *info);
int rcar_crc_init(struct platform_device *pdev);
int crc_drv_init(void);
void crc_drv_exit(void);

int rcar_wcrc_init(struct platform_device *pdev);

/** struct wcrc_ops - The wcrc devices operations
 *
 * @owner:			The module owner.
 *
 * The wcrc_ops structure contains a list of low-level operations
 * that control a wcrc device. It also contains the module that owns
 * these operations.
 */
struct wcrc_ops {
	struct module *owner;
	int (*stop)(struct wcrc_info *inf, struct wcrc_device *p);
};

#endif /* _RENESAS_CRC_WRAPPER_H_ */
