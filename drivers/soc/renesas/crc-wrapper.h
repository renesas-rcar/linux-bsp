/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Renesas CRC-WRAPPER drivers header
 *
 * Copyright (C) 2024 Renesas Electronics Inc.
 *
 */

#ifndef _RENESAS_CRC_WRAPPER_H_
#define _RENESAS_CRC_WRAPPER_H_

#include <../drivers/soc/renesas/usr_wcrc.h>
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

	// DMA part
	struct resource *fifo_res;
	enum dma_data_direction dma_data_dir;
	unsigned int num_desc_tx;
	unsigned int num_desc_rx;
	unsigned int num_desc_rx_in;
	wait_queue_head_t dma_in_wait;
	bool ongoing;
	bool ongoing_dma_rx;
	bool ongoing_dma_tx;
	bool ongoing_dma_rx_in;
	void *buf_crc;
	void *buf_data;
	unsigned int num_crc;
	// TX
	struct scatterlist *sg_tx;
	struct dma_chan *dma_tx;
	enum dma_slave_buswidth tx_bus_width;
	void *buf_tx;
	unsigned int len_tx;
	dma_addr_t tx_dma_addr;
	// RX
	struct scatterlist *sg_rx;
	struct dma_chan *dma_rx;
	enum dma_slave_buswidth rx_bus_width;
	void *buf_rx;
	unsigned int len_rx;
	dma_addr_t rx_dma_addr;
	// RX in
	struct scatterlist *sg_rx_in;
	struct dma_chan *dma_rx_in;
	enum dma_slave_buswidth rx_in_bus_width;
	void *buf_rx_in;
	unsigned int len_rx_in;
	dma_addr_t rx_in_dma_addr;
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
 * @start_e2e_crc:	The routine for starting the E2E CRC mode.
 * @stop:			The routine for stopping the wcrc device.
 * @set_e2e_crc:	The routine for setting E2E CRC mode.
 *
 * The wcrc_ops structure contains a list of low-level operations
 * that control a wcrc device. It also contains the module that owns
 * these operations.
 */
struct wcrc_ops {
	struct module *owner;
	int (*stop)(struct wcrc_info *inf, struct wcrc_device *p);
	int (*set_e2e_crc)(struct wcrc_info *inf, struct wcrc_device *p);
	int (*start_e2e_crc)(struct wcrc_info *inf, struct wcrc_device *p,
			     void *data_in, void *crc_out);
	int (*set_data_thr)(struct wcrc_info *inf, struct wcrc_device *p);
	int (*start_data_thr)(struct wcrc_info *inf, struct wcrc_device *p,
			      void *data_in, void *crc_out);
	int (*set_e2e_data_thr)(struct wcrc_info *inf, struct wcrc_device *p);
	int (*start_e2e_data_thr)(struct wcrc_info *inf, struct wcrc_device *p,
				  void *data_in, void *data_out, void *crc_out);
};

#endif /* _RENESAS_CRC_WRAPPER_H_ */
