// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car Gen4/Gen5 WCRC Driver
 *
 * Copyright (C) 2024 Renesas Electronics Inc.
 *
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <../drivers/soc/renesas/crc-wrapper.h>

#define DEVNAME "crc-wrapper"
#define CLASS_NAME "wcrc"

/* Register offset */
#define AES_ACC_N   (0)
#define AES_ACC_P   (1)
#define CRC_M        (2)
#define KCRC_M       (3)

/* Address assignment of FIFO */
/* Data */
#define PORT_DATA(mod) (			\
	((AES_ACC_N) == (mod))	? (0x000) :	\
	((AES_ACC_P) == (mod))	? (0x400) :	\
	((CRC_M)     == (mod))	? (0x800) :	\
	((KCRC_M)    == (mod))	? (0xC00) :	\
	(0x800)					\
)

/* Command */
#define PORT_CMD(mod) (				\
	((AES_ACC_N) == (mod))	? (0x100) :	\
	((AES_ACC_P) == (mod))	? (0x500) :	\
	((CRC_M)     == (mod))	? (0x900) :	\
	((KCRC_M)    == (mod))	? (0xD00) :	\
	(0x900)					\
)

/* Expected data:
 * NOTE:
 *      AES_ACC_N: 0x200 (Reserved)
 *      AES_ACC_P: 0x600 (Reserved)
 */
#define PORT_EXPT_DATA(mod) (			\
	((CRC_M)      == (mod))	? (0xA00) :	\
	((KCRC_M)     == (mod))	? (0xE00) :	\
	(0xA00)					\
)

/* Result */
#define PORT_RES(mod) (				\
	((AES_ACC_N) == (mod))	? (0x300) :	\
	((AES_ACC_P) == (mod))	? (0x700) :	\
	((CRC_M)     == (mod))	? (0xB00) :	\
	((KCRC_M)    == (mod))	? (0xF00) :	\
	(0xB00)					\
)

/* WCRC register (XXXX: CRC_M or KCRC_M) */

/* WCRC_XXXX_EN transfer enable register */
#define WCRC_CRC_EN 0x0800
#define WCRC_KCRC_EN 0x0C00
#define WCRC_XXXX_EN(mod) (			\
	((CRC_M) == (mod))  ? (WCRC_CRC_EN)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_EN) : \
	(WCRC_CRC_EN)				\
)
#define OUT_EN BIT(16)
#define RES_EN BIT(8)
#define TRANS_EN BIT(1)
#define IN_EN BIT(0)

/* WCRC_XXXX_STOP transfer stop register */
#define WCRC_CRC_STOP 0x0820
#define WCRC_KCRC_STOP 0x0C20
#define WCRC_XXXX_STOP(mod) (				\
	((CRC_M) == (mod))  ? (WCRC_CRC_STOP)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_STOP) :	\
	(WCRC_CRC_STOP)				\
)
#define STOP BIT(0)

/* WCRC_XXXX_CMDEN transfer command enable register */
#define WCRC_CRC_CMDEN 0x0830
#define WCRC_KCRC_CMDEN 0x0C30
#define WCRC_XXXX_CMDEN(mod) (				\
	((CRC_M) == (mod))  ? (WCRC_CRC_CMDEN)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_CMDEN) :	\
	(WCRC_CRC_CMDEN)				\
)
#define CMD_EN BIT(0)

/* WCRC_XXXX_COMP compare setting register */
#define WCRC_CRC_COMP 0x0840
#define WCRC_KCRC_COMP 0x0C40
#define WCRC_XXXX_COMP(mod) (				\
	((CRC_M) == (mod))  ? (WCRC_CRC_COMP)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_COMP) :	\
	(WCRC_CRC_COMP)				\
)
#define COMP_FREQ_16 (0 << 16)
#define COMP_FREQ_32 (1 << 16)
#define COMP_FREQ_64 (3 << 16)
#define EXP_REQSEL BIT(1)
#define COMP_EN BIT(0)

/* WCRC_XXXX_COMP_RES compare result register regrister */
#define WCRC_CRC_COMP_RES 0x0850
#define WCRC_KCRC_COMP_RES 0x0C50
#define WCRC_XXXX_COMP_RES(mod) (			\
	((CRC_M) == (mod))  ? (WCRC_CRC_COMP_RES)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_COMP_RES) :	\
	(WCRC_CRC_COMP_RES)				\
)

/* WCRC_XXXX_CONV conversion setting register */
#define WCRC_CRC_CONV 0x0870
#define WCRC_KCRC_CONV 0x0C70
#define WCRC_XXXX_CONV(mod) (				\
	((CRC_M) == (mod))  ? (WCRC_CRC_CONV)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_CONV) :	\
	(WCRC_CRC_CONV)				\
)

/* WCRC_XXXX_WAIT wait register */
#define WCRC_CRC_WAIT 0x0880
#define WCRC_KCRC_WAIT 0x0C80
#define WCRC_XXXX_WAIT(mod) (				\
	((CRC_M) == (mod))  ? (WCRC_CRC_WAIT)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_WAIT) :	\
	(WCRC_CRC_WAIT)				\
)
#define WAIT BIT(0)

/* WCRC_XXXX_INIT_CRC initial CRC code register */
#define WCRC_CRC_INIT_CRC 0x0910
#define WCRC_KCRC_INIT_CRC 0x0D10
#define WCRC_XXXX_INIT_CRC(mod) (			\
	((CRC_M) == (mod))  ? (WCRC_CRC_INIT_CRC)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_INIT_CRC) :	\
	(WCRC_CRC_INIT_CRC)				\
)
#define INIT_CODE 0xFFFFFFFF

/* WCRC_XXXX_STS status register */
#define WCRC_CRC_STS 0x0A00
#define WCRC_KCRC_STS 0x0E00
#define WCRC_XXXX_STS(mod) (				\
	((CRC_M) == (mod))  ? (WCRC_CRC_STS)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_STS) :	\
	(WCRC_CRC_STS)				\
)
#define STOP_DONE BIT(31)
#define CMD_DONE BIT(24)
#define RES_DONE BIT(20)
#define COMP_ERR BIT(13)
#define COMP_DONE BIT(12)
#define TRANS_DONE BIT(0)

/* WCRC_XXXX_INTEN interrupt enable register */
#define WCRC_CRC_INTEN 0x0A40
#define WCRC_KCRC_INTEN 0x0E40
#define WCRC_XXXX_INTEN(mod) (				\
	((CRC_M) == (mod))  ? (WCRC_CRC_INTEN)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_INTEN) :	\
	(WCRC_CRC_INTEN)				\
)
#define STOP_DONE_IE BIT(31)
#define CMD_DONE_IE BIT(24)
#define RES_DONE_IE BIT(20)
#define COMP_ERR_IE BIT(13)
#define COMP_DONE_IE BIT(12)
#define TRANS_DONE_IE BIT(0)

/* WCRC_XXXX_ECMEN ECM output enable register */
#define WCRC_CRC_ECMEN 0x0A80
#define WCRC_KCRC_ECMEN 0x0E80
#define WCRC_XXXX_ECMEN(mod) (				\
	((CRC_M) == (mod))  ? (WCRC_CRC_ECMEN)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_ECMEN) :	\
	(WCRC_CRC_ECMEN)				\
)
#define COMP_ERR_OE BIT(13)

/* WCRC_XXXX_BUF_STS_RDEN Buffer state read enable register */
#define WCRC_CRC_BUF_STS_RDEN 0x0AA0
#define WCRC_KCRC_BUF_STS_RDEN 0x0EA0
#define WCRC_XXXX_BUF_STS_RDEN(mod) (				\
	((CRC_M) == (mod))  ? (WCRC_CRC_BUF_STS_RDEN)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_BUF_STS_RDEN) :	\
	(WCRC_CRC_BUF_STS_RDEN)				\
)
#define CODE_VALUE (0xA5A5 << 16)
#define BUF_STS_RDEN BIT(0)

/* WCRC_XXXX_BUF_STS Buffer state read register */
#define WCRC_CRC_BUF_STS 0x0AA4
#define WCRC_KCRC_BUF_STS 0x0EA4
#define WCRC_XXXX_BUF_STS(mod) (			\
	((CRC_M) == (mod))  ? (WCRC_CRC_BUF_STS)  :	\
	((KCRC_M) == (mod)) ? (WCRC_KCRC_BUF_STS) :	\
	(WCRC_CRC_BUF_STS)				\
)
#define RES_COMP_ENDFLAG BIT(18)
#define BUF_EMPTY BIT(8)

/* WCRC common */

/* WCRCm common status register */
#define WCRC_COMMON_STS 0x0F00
#define EDC_ERR BIT(16)

/* WCRCm common interrupt enable register */
#define WCRC_INTEN 0x0F00
#define EDC_ERR_IE BIT(16)

/* WCRCm common ECM output enable register */
#define WCRC_COMMON_ECMEN 0x0F80
#define EDC_ERR_OE BIT(16)

/* WCRCm error injection register */
#define WCRC_ERRINJ 0x0FC0
#define CODE (0xA5A5 << 16)

/* Define global variable */
DEFINE_MUTEX(lock);

static int dev_chan;
static dev_t wcrc_devt;
static struct class *wcrc_class = NULL;

static void rcar_wcrc_dma_tx_callback(void *data);
static void rcar_wcrc_dma_rx_callback(void *data);
static void rcar_wcrc_dma_rx_in_callback(void *data);

static u32 wcrc_read(void __iomem *base, unsigned int offset)
{
	return ioread32(base + offset);
}

static void wcrc_write(void __iomem *base, unsigned int offset, u32 data)
{
	iowrite32(data, base + offset);
}

/* DMA API */
static struct dma_chan *wcrc_request_dma_chan(struct device *dev,
					enum dma_transfer_direction dir,
					dma_addr_t port_addr, char *chan_name,
					enum dma_slave_buswidth bus_width)
{
	struct dma_chan *chan;
	struct dma_slave_config cfg;
	int ret;

	chan = dma_request_chan(dev, chan_name);
	if (IS_ERR(chan)) {
		dev_dbg(dev, "request_channel failed for %s (%ld)\n",
			chan_name, PTR_ERR(chan));
		pr_info("dma_request_chan: %s FAILED\n", chan_name);
		return chan;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.direction = dir;
	if (dir == DMA_MEM_TO_DEV) {
		cfg.dst_addr = port_addr;
		cfg.dst_addr_width = bus_width;
	} else {
		cfg.src_addr = port_addr;
		cfg.src_addr_width = bus_width;
	}

	ret = dmaengine_slave_config(chan, &cfg);
	if (ret) {
		dev_dbg(dev, "slave_config failed for %s (%d)\n",
			chan_name, ret);
		dma_release_channel(chan);
		pr_info("dmaengine_slave_config: FAILED\n");
		return ERR_PTR(ret);
	}

	dev_dbg(dev, "got DMA channel for %s\n", chan_name);
	return chan;
}

static struct dma_chan *wcrc_request_dma(struct wcrc_device *priv,
					enum dma_transfer_direction dir,
					dma_addr_t offs_port_addr, char *chan_name,
					enum dma_slave_buswidth bus_width)
{
	struct device *dev = priv->dev;
	struct dma_chan *chan;

	if (dir == DMA_DEV_TO_MEM) {
		if ((offs_port_addr == PORT_RES(CRC_M)) | (offs_port_addr == PORT_RES(KCRC_M)))
			priv->rx_bus_width = bus_width;
		if ((offs_port_addr == PORT_DATA(CRC_M)) | (offs_port_addr == PORT_DATA(KCRC_M)))
			priv->rx_in_bus_width = bus_width;
	}

	if (dir == DMA_MEM_TO_DEV)
		priv->tx_bus_width = bus_width;

	if ((dir == DMA_DEV_TO_MEM) || (dir == DMA_MEM_TO_DEV)) {
		chan = wcrc_request_dma_chan(dev, dir, (priv->fifo_res->start + offs_port_addr),
					chan_name, bus_width);
		//pr_info(">>>%s: dma_addr=0x%llx\n", chan_name, priv->fifo_res->start + offs_port_addr);
	} else {
		dev_dbg(dev, "%s: FAILED for dir=%d\n, chan %s", __func__, dir, chan_name);
		return ERR_PTR(-EPROBE_DEFER);
	}

	return chan;
}

static void rcar_wcrc_cleanup_dma(struct wcrc_device *priv)
{
	if (priv->dma_data_dir == DMA_NONE)
		return;
	else if (priv->dma_data_dir == DMA_FROM_DEVICE)
		dmaengine_terminate_async(priv->dma_rx);
	else if (priv->dma_data_dir == DMA_TO_DEVICE)
		dmaengine_terminate_async(priv->dma_tx);
}

static bool rcar_wcrc_dma_tx(struct wcrc_device *priv,
			void *data, unsigned int len)
{
	struct device *dev = priv->dev;
	struct dma_async_tx_descriptor *txdesc;
	struct dma_chan *chan;
	dma_cookie_t cookie;
	int num_desc;
	enum dma_data_direction data_dir;
	enum dma_transfer_direction trans_dir;
	void *buf;
	int i;

	/* DMA_MEM_TO_DEV */
	priv->buf_tx = data;
	//pr_info("priv->buf_tx points to block mem %zu bytes\n", ksize(priv->buf_tx));
	buf = priv->buf_tx;
	num_desc = len / priv->tx_bus_width;
	priv->num_desc_tx = num_desc;
	priv->dma_data_dir = DMA_TO_DEVICE;
	data_dir = priv->dma_data_dir;
	trans_dir = DMA_MEM_TO_DEV;
	chan = priv->dma_tx;

	priv->sg_tx = kmalloc_array(num_desc, sizeof(struct scatterlist), GFP_KERNEL);
	if (!priv->sg_tx)
		return false;

	sg_init_table(priv->sg_tx, num_desc);

	for (i = 0; i < num_desc; i++) {
		sg_dma_len(&priv->sg_tx[i]) = len/num_desc;
		sg_dma_address(&priv->sg_tx[i]) = dma_map_single(chan->device->dev,
								buf + i*(priv->tx_bus_width),
								len/num_desc, data_dir);
	}

	txdesc = dmaengine_prep_slave_sg(chan, priv->sg_tx, num_desc,
					trans_dir, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	txdesc->callback = rcar_wcrc_dma_tx_callback;
	txdesc->callback_param = priv;

	cookie = dmaengine_submit(txdesc);
	if (dma_submit_error(cookie)) {
		dev_dbg(dev, "%s: submit TX dma failed, using PIO\n", __func__);
		rcar_wcrc_cleanup_dma(priv);
		return false;
	}

	priv->ongoing_dma_tx = 1;

	dma_async_issue_pending(chan);

	return true;
}

static bool rcar_wcrc_dma_rx(struct wcrc_device *priv,
			void *data, unsigned int len)
{
	struct device *dev = priv->dev;
	struct dma_async_tx_descriptor *rxdesc;
	struct dma_chan *chan;
	dma_cookie_t cookie;
	int num_desc;
	enum dma_data_direction data_dir;
	enum dma_transfer_direction trans_dir;
	void *buf;
	int i;

	/* DMA_DEV_TO_MEM */
	//data = kzalloc(len, GFP_KERNEL);
	priv->buf_rx = data;
	//pr_info("priv->buf_rx points to block mem %zu bytes\n", ksize(priv->buf_rx));
	buf = priv->buf_rx;
	priv->len_rx = len;
	num_desc = len / priv->rx_bus_width;
	priv->num_desc_rx = num_desc;
	data_dir = DMA_FROM_DEVICE;
	trans_dir = DMA_DEV_TO_MEM;
	chan = priv->dma_rx;

	priv->sg_rx = kmalloc_array(num_desc, sizeof(struct scatterlist), GFP_KERNEL);
	if (!priv->sg_rx)
		return false;

	sg_init_table(priv->sg_rx, num_desc);

	for (i = 0; i < num_desc; i++) {
		sg_dma_len(&priv->sg_rx[i]) = len/num_desc;
		sg_dma_address(&priv->sg_rx[i]) = dma_map_single(chan->device->dev,
								buf + i*(priv->rx_bus_width),
								len/num_desc, data_dir);
	}

	rxdesc = dmaengine_prep_slave_sg(chan, priv->sg_rx, num_desc,
					trans_dir, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);


	rxdesc->callback = rcar_wcrc_dma_rx_callback;
	rxdesc->callback_param = priv;

	cookie = dmaengine_submit(rxdesc);
	if (dma_submit_error(cookie)) {
		pr_info("dmaengine_submit_error\n");
		dev_dbg(dev, "%s: submit RX dma failed, using PIO\n", __func__);
		rcar_wcrc_cleanup_dma(priv);
		return false;
	}

	priv->ongoing_dma_rx = 1;

	dma_async_issue_pending(chan);

	return true;

}

static bool rcar_wcrc_dma_rx_in(struct wcrc_device *priv,
				void *data, unsigned int len)
{
	struct device *dev = priv->dev;
	struct dma_async_tx_descriptor *rxdesc;
	struct dma_chan *chan;
	dma_cookie_t cookie;
	int num_desc;
	enum dma_data_direction data_dir;
	enum dma_transfer_direction trans_dir;
	void *buf;
	int i;

	/* DMA_DEV_TO_MEM */
	//data = kzalloc(len, GFP_KERNEL);
	priv->buf_rx_in = data;
	//pr_info("priv->buf_rx_in points to block mem %zu bytes\n", ksize(priv->buf_rx_in));
	buf = priv->buf_rx_in;
	priv->len_rx_in = len;
	//pr_info("priv->len_rx_in = %d\n", priv->len_rx_in);
	num_desc = len / priv->rx_in_bus_width;
	//pr_info("num_desc = %d\n", num_desc);
	priv->num_desc_rx_in = num_desc;
	data_dir = DMA_FROM_DEVICE;
	trans_dir = DMA_DEV_TO_MEM;
	chan = priv->dma_rx_in;

	priv->sg_rx_in = kmalloc_array(num_desc, sizeof(struct scatterlist), GFP_KERNEL);
	if (!priv->sg_rx_in)
		return false;

	sg_init_table(priv->sg_rx_in, num_desc);

	for (i = 0; i < num_desc; i++) {
		sg_dma_len(&priv->sg_rx_in[i]) = len/num_desc;
		sg_dma_address(&priv->sg_rx_in[i]) = dma_map_single(chan->device->dev,
								buf + i*(priv->rx_in_bus_width),
								len/num_desc, data_dir);
	}

	rxdesc = dmaengine_prep_slave_sg(chan, priv->sg_rx_in, num_desc,
					trans_dir, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);


	rxdesc->callback = rcar_wcrc_dma_rx_in_callback;
	rxdesc->callback_param = priv;

	cookie = dmaengine_submit(rxdesc);
	if (dma_submit_error(cookie)) {
		pr_info("dmaengine_submit_error\n");
		dev_dbg(dev, "%s: submit RX dma failed, using PIO\n", __func__);
		rcar_wcrc_cleanup_dma(priv);
		return false;
	}

	priv->ongoing_dma_rx_in = 1;

	dma_async_issue_pending(chan);

	return true;

}

static void rcar_wcrc_dma_tx_callback(void *data)
{
	struct wcrc_device *priv = data;
	struct dma_chan *chan = priv->dma_tx;

	priv->ongoing_dma_tx = 0;
	wake_up_interruptible(&priv->dma_in_wait);

	dma_unmap_sg(chan->device->dev, priv->sg_tx, priv->num_desc_tx, DMA_TO_DEVICE);

	// Use when device accesses the buffer through dma_map_single()
	//dma_sync_sg_for_device(chan->device->dev, priv->sg_tx, priv->num_desc_tx, DMA_TO_DEVICE);
	//pr_info("======>>>>>>%s: %d\n", __func__, __LINE__);
}

static void rcar_wcrc_dma_rx_callback(void *data)
{
	struct wcrc_device *priv = data;
	struct dma_chan *chan = priv->dma_rx;

	priv->ongoing_dma_rx = 0;
	wake_up_interruptible(&priv->dma_in_wait);

	dma_sync_sg_for_cpu(chan->device->dev, priv->sg_rx,
			priv->num_desc_rx, DMA_FROM_DEVICE);

	dma_unmap_sg(chan->device->dev, priv->sg_rx, priv->num_desc_rx, DMA_FROM_DEVICE);
	//pr_info("<<<<<<======%s: %d\n", __func__, __LINE__);
}

static void rcar_wcrc_dma_rx_in_callback(void *data)
{
	struct wcrc_device *priv = data;
	struct dma_chan *chan = priv->dma_rx_in;

	priv->ongoing_dma_rx_in = 0;
	wake_up_interruptible(&priv->dma_in_wait);

	dma_sync_sg_for_cpu(chan->device->dev, priv->sg_rx_in,
			priv->num_desc_rx_in, DMA_FROM_DEVICE);

	dma_unmap_sg(priv->dma_rx_in->device->dev, priv->sg_rx_in, priv->num_desc_rx_in, DMA_FROM_DEVICE);
	//pr_info("<<<<<<======%s: %d\n", __func__, __LINE__);
}

static irqreturn_t rcar_wcrc_irq(int irq_num, void *ptr)
{
	struct wcrc_device *priv = ptr;
	uint32_t reg_val;

	reg_val = wcrc_read(priv->base, WCRC_XXXX_STS(priv->module));
	//Clear trans_done in WCRC_XXXX_STS.
	if ((TRANS_DONE & reg_val)) {
		wcrc_write(priv->base, WCRC_XXXX_STS(priv->module), TRANS_DONE);
		//pr_info("<<<<<<======%s: TRANS_DONE %d\n", __func__, __LINE__);
		goto return_irq;
	}

	//Clear res_done in WCRC_XXXX_STS.
	if ((RES_DONE & reg_val)) {
		wcrc_write(priv->base, WCRC_XXXX_STS(priv->module), RES_DONE);
		//pr_info("<<<<<<======%s: RES_DONE %d\n", __func__, __LINE__);
		goto return_irq;
	}

	//Clear cmd_done in WCRC_XXXX_STS.
	if ((CMD_DONE & reg_val)) {
		wcrc_write(priv->base, WCRC_XXXX_STS(priv->module), CMD_DONE);
		//pr_info("<<<<<<======%s: CMD_DONE %d\n", __func__, __LINE__);
		goto return_irq;
	}

	//Clear stop_done in WCRC_XXXX_STS.
	if ((STOP_DONE & reg_val)) {
		wcrc_write(priv->base, WCRC_XXXX_STS(priv->module), STOP_DONE);
		//pr_info("<<<<<<======%s: STOP_DONE %d\n", __func__, __LINE__);
	}

return_irq:
	return IRQ_HANDLED;
}

static void rcar_wcrc_release_dma(struct wcrc_device *priv)
{
	if (!IS_ERR(priv->dma_tx)) {
		dma_release_channel(priv->dma_tx);
		priv->dma_tx = ERR_PTR(-EPROBE_DEFER);
	}

	if (!IS_ERR(priv->dma_rx)) {
		dma_release_channel(priv->dma_rx);
		priv->dma_rx = ERR_PTR(-EPROBE_DEFER);
		kfree(priv->buf_rx);
	}

	if (!IS_ERR(priv->dma_rx_in)) {
		dma_release_channel(priv->dma_rx_in);
		priv->dma_rx_in = ERR_PTR(-EPROBE_DEFER);
		kfree(priv->buf_rx_in);
	}
}

static int wcrc_independent_crc(struct wcrc_device *p, struct wcrc_info *info)
{
	int ret;

	mutex_lock(&lock);

	//pr_info("Addr 0x%llx\n", (long long unsigned int)p);
	if (info->crc_opt == 0)
		ret = crc_calculate(p->crc_dev, info);
	else if (info->crc_opt == 1)
		ret = kcrc_calculate(p->kcrc_dev, info);
	else
		ret = -1;

	mutex_unlock(&lock);

	if (ret)
		pr_err("Calculation Aborted!, ERR: %d", ret);

	return 0;
}

static int wcrc_setting_e2e_crc(struct wcrc_info *info, struct wcrc_device *priv)
{
	int ret = 0;
	int module = 0;
	unsigned int reg_val = 0;
	char *dma_name[2];

	mutex_lock(&lock);

	if (info->crc_opt == 0) {
		module = CRC_M;
		dma_name[0] = "crc_tx";
		dma_name[1] = "crc_rx";
	} else if (info->crc_opt == 1) {
		module = KCRC_M;
		dma_name[0] = "kcrc_tx";
		dma_name[1] = "kcrc_rx";
	} else {
		ret = -EINVAL;
		goto end_mode;
	}
	priv->module = module;

	//Enable interrupt for the complete of stop operation, command function and input data transfer.
	reg_val = STOP_DONE_IE | RES_DONE_IE;
	wcrc_write(priv->base, WCRC_XXXX_INTEN(module), reg_val);
	//pr_info("WCRC_XXXX_INTEN = 0x%x\n", wcrc_read(priv->base, WCRC_XXXX_INTEN(module)));

	//1. Set CRC conversion size to once in WCRC_XXXX_CONV register.
	reg_val = info->conv_size;
	wcrc_write(priv->base, WCRC_XXXX_CONV(module), reg_val);
	//pr_info("===>>>WCRC_XXXX_CONV = 0x%x\n", wcrc_read(priv->base, WCRC_XXXX_CONV(module)));

	//2. Set initial CRC code value in WCRC_XXXX_INIT_CRC register.
	reg_val = 0xFFFFFFFF;
	wcrc_write(priv->base, WCRC_XXXX_INIT_CRC(module), reg_val);
	//pr_info("WCRC_XXXX_INIT_CRC=0x%x\n", wcrc_read(priv->base, WCRC_XXXX_INIT_CRC(module)));

	//3. (For CRC) Set DCRAmCTL, DCRAmCTL2, DCRAmCOUT registers.
	//	(For KCRC) Set KCRCmCTL, KCRCmPOLY, KCRCmXOR, KCRCmDOUT registers.
	if (CRC_M == module)
		crc_setting(priv->crc_dev, info);
	else if (KCRC_M == module)
		kcrc_setting(priv->kcrc_dev, info);
	else {
		ret = -EINVAL;
		goto end_mode;
	}

	//4. Set in_en=1, trans_en=1, res_en=1 in WCRC_XXXX_EN register.
	reg_val = wcrc_read(priv->base, WCRC_XXXX_EN(module));
	reg_val = IN_EN | TRANS_EN | RES_EN;
	wcrc_write(priv->base, WCRC_XXXX_EN(module), reg_val);
	//pr_info("WCRC_XXXX_EN= 0x%x\n", wcrc_read(priv->base, WCRC_XXXX_EN(module)));

	//5. Set cmd_en=1 in WCRC_XXXX_CMDEN register
	reg_val = wcrc_read(priv->base, WCRC_XXXX_CMDEN(module));
	reg_val = CMD_EN;
	wcrc_write(priv->base, WCRC_XXXX_CMDEN(module), reg_val);
	//pr_info("WCRC_XXXX_CMDEN= 0x%x\n", wcrc_read(priv->base, WCRC_XXXX_CMDEN(module)));

	priv->dma_tx = wcrc_request_dma(priv, DMA_MEM_TO_DEV, PORT_DATA(module),
					dma_name[0], DMA_SLAVE_BUSWIDTH_64_BYTES);

	priv->dma_rx = wcrc_request_dma(priv, DMA_DEV_TO_MEM, PORT_RES(module),
					dma_name[1], DMA_SLAVE_BUSWIDTH_16_BYTES);

	priv->num_crc = info->data_input_len/info->conv_size;
	priv->buf_crc = kzalloc(priv->num_crc * sizeof(u32), GFP_KERNEL);
	//pr_info("priv->buf_crc points to block mem %zu bytes\n", ksize(priv->buf_crc));

end_mode:
	mutex_unlock(&lock);

	return ret;
}

static int wcrc_start_e2e_crc(struct wcrc_info *info, struct wcrc_device *priv,
						void *p_u_data, void *p_drv_crc)
{
	int ret;
	int i, loop;

	//6. Transfer input data to data port of FIFO by DMAC.
	ret = (int)rcar_wcrc_dma_tx(priv, p_u_data, info->data_input_len);
	if (!ret) {
		ret = -EFAULT;
		goto end_func;
	}

	//7. Read out result data from result port of FIFO by DMAC.
	ret = rcar_wcrc_dma_rx(priv, p_drv_crc, priv->num_crc*4);
	if (!ret) {
		pr_err("E2E_CRC_MODE: run FAILED\n");
		ret = -EFAULT;
		goto end_func;
	}

end_func:
	return !ret;
}

static int wcrc_stop(struct wcrc_info *info, struct wcrc_device *priv)
{
	int ret;
	int module;
	unsigned int reg_val;

	ret = 0;
	if (info->crc_opt == 0)
		module = CRC_M;
	else if (info->crc_opt == 1)
		module = KCRC_M;
	else {
		ret = -EINVAL;
		goto end_stop;
	}

	//8. Set stop=1 in WCRC_XXXX_STOP by command function.
	reg_val = wcrc_read(priv->base, WCRC_XXXX_STOP(module));
	reg_val |= STOP;
	wcrc_write(priv->base, WCRC_XXXX_STOP(module), reg_val);

	//9. Clear stop_done in WCRC_XXXX_STS.
	//The func rcar_wcrc_irq() will handle.

end_stop:
	rcar_wcrc_release_dma(priv);

	return ret;
}

static int wcrc_setting_data_thr(struct wcrc_info *info, struct wcrc_device *priv)
{
	int ret = 0;
	int module = 0;
	unsigned int reg_val = 0;
	char *dma_name[2];
	enum dma_slave_buswidth bus_width;

	mutex_lock(&lock);

	//Select which module to run: CRC or KCRC
	if (info->crc_opt == 0) {
		module = CRC_M;
		dma_name[0] = "crc_tx";
		dma_name[1] = "crc_rx_in";
	} else if (info->crc_opt == 1) {
		module = KCRC_M;
		dma_name[0] = "kcrc_tx";
		dma_name[1] = "kcrc_rx_in";
	} else {
		//pr_info("Not support\n");
		ret = -EINVAL;
		goto end_mode;
	}
	priv->module = module;

	//In HW_UM V4H, sec 135.1.3 / (4) Data through mode:
	//Note: 2. The DMA transfer size (TS[3:0]) of read should be same as one of write.
	// ==> DMA Rx read size = DMA Tx transfer size = bus_width
	bus_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	//Enable interrupt for the complete of stop operation by WCRC_CRC_STOP register.
	reg_val = STOP_DONE_IE;
	wcrc_write(priv->base, WCRC_XXXX_INTEN(module), reg_val);

	//1. Set in_en=1, out_en=1 in WCRC_XXXX_EN register.
	reg_val = wcrc_read(priv->base, WCRC_XXXX_EN(module));
	reg_val = IN_EN | OUT_EN;
	wcrc_write(priv->base, WCRC_XXXX_EN(module), reg_val);

	priv->dma_tx = wcrc_request_dma(priv, DMA_MEM_TO_DEV, PORT_DATA(module),
					dma_name[0], bus_width);

	priv->dma_rx_in = wcrc_request_dma(priv, DMA_DEV_TO_MEM, PORT_DATA(module),
					dma_name[1], bus_width);

	priv->buf_data = kzalloc(info->data_input_len, GFP_KERNEL);
	//pr_info("priv->buf_data points to block mem %zu bytes\n", ksize(priv->buf_data));

end_mode:
	mutex_unlock(&lock);

	return ret;
}

static int wcrc_start_data_thr(struct wcrc_info *info, struct wcrc_device *priv,
						void *p_u_data, void *p_drv_data)
{
	int ret;

	//2. Transfer input data to data port of FIFO by DMAC.
	ret = rcar_wcrc_dma_tx(priv, p_u_data, info->data_input_len);
	if (!ret) {
		//pr_err("DATA_THROUGH_MODE: run FAILED\n");
		ret = -EFAULT;
		goto end_func;
	}

	//3. Read out input data from data port of FIFO by DMAC.
	ret = rcar_wcrc_dma_rx_in(priv, p_drv_data, info->data_input_len);
	if (!ret) {
		//pr_err("DATA_THROUGH_MODE: run FAILED\n");
		ret = -EFAULT;
		goto end_func;
	}

end_func:
	return !ret;
}

static int wcrc_setting_e2e_data_thr(struct wcrc_info *info, struct wcrc_device *priv)
{
	int ret = 0;
	int module = 0;
	unsigned int reg_val = 0;
	char *dma_name[3];
	enum dma_slave_buswidth bus_width;

	mutex_lock(&lock);

	if (info->crc_opt == 0) {
		module = CRC_M;
		dma_name[0] = "crc_tx";
		dma_name[1] = "crc_rx_in";
		dma_name[2] = "crc_rx";
	} else if (info->crc_opt == 1) {
		module = KCRC_M;
		dma_name[0] = "kcrc_tx";
		dma_name[1] = "kcrc_rx_in";
		dma_name[2] = "kcrc_rx";
	} else {
		ret = -EINVAL;
		goto end_mode;
	}
	priv->module = module;

	bus_width = DMA_SLAVE_BUSWIDTH_64_BYTES;

	//Enable interrupt for the complete of stop operation, command function and input data transfer.
	reg_val = STOP_DONE_IE | RES_DONE_IE;
	wcrc_write(priv->base, WCRC_XXXX_INTEN(module), reg_val);
	//pr_info("WCRC_XXXX_INTEN = 0x%x\n", wcrc_read(priv->base, WCRC_XXXX_INTEN(module)));

	//2. Set CRC conversion size to once in WCRC_XXXX_CONV register.
	reg_val = info->conv_size;
	wcrc_write(priv->base, WCRC_XXXX_CONV(module), reg_val);
	//pr_info("WCRC_XXXX_CONV = 0x%x\n", wcrc_read(priv->base, WCRC_XXXX_CONV(module)));

	//3. Set initial CRC code value in WCRC_XXXX_INIT_CRC register.
	reg_val = 0xFFFFFFFF;
	wcrc_write(priv->base, WCRC_XXXX_INIT_CRC(module), reg_val);
	//pr_info("WCRC_XXXX_INIT_CRC=0x%x\n", wcrc_read(priv->base, WCRC_XXXX_INIT_CRC(module)));

	//4. (For CRC) Set DCRAmCTL, DCRAmCTL2, DCRAmCOUT registers.
	//	(For KCRC) Set KCRCmCTL, KCRCmPOLY, KCRCmXOR, KCRCmDOUT registers.
	if (CRC_M == module)
		crc_setting(priv->crc_dev, info);
	else if (KCRC_M == module)
		kcrc_setting(priv->kcrc_dev, info);
	else {
		ret = -EINVAL;
		goto end_mode;
	}

	//5. Set in_en=1, trans_en=1, res_en=1, out_en=1 in WCRC_XXXX_EN register.
	reg_val = wcrc_read(priv->base, WCRC_XXXX_EN(module));
	reg_val = IN_EN | TRANS_EN | RES_EN | OUT_EN;
	wcrc_write(priv->base, WCRC_XXXX_EN(module), reg_val);
	//pr_info("WCRC_XXXX_EN= 0x%x\n", wcrc_read(priv->base, WCRC_XXXX_EN(module)));

	//6. Set cmd_en=1 in WCRC_XXXX_CMDEN register
	reg_val = wcrc_read(priv->base, WCRC_XXXX_CMDEN(module));
	reg_val = CMD_EN;
	wcrc_write(priv->base, WCRC_XXXX_CMDEN(module), reg_val);
	//pr_info("WCRC_XXXX_CMDEN= 0x%x\n", wcrc_read(priv->base, WCRC_XXXX_CMDEN(module)));

	priv->dma_tx = wcrc_request_dma(priv,
					DMA_MEM_TO_DEV, PORT_DATA(module),
					dma_name[0], bus_width);

	priv->dma_rx_in = wcrc_request_dma(priv,
					DMA_DEV_TO_MEM, PORT_DATA(module),
					dma_name[1], bus_width);

	priv->dma_rx = wcrc_request_dma(priv,
					DMA_DEV_TO_MEM, PORT_RES(module),
					dma_name[2], DMA_SLAVE_BUSWIDTH_16_BYTES);

	priv->num_crc = info->data_input_len/info->conv_size;
	priv->buf_crc = kzalloc(priv->num_crc * sizeof(u32), GFP_KERNEL);
	//pr_info("priv->buf_crc points to block mem %zu bytes\n", ksize(priv->buf_crc));

	priv->buf_data = kzalloc(info->data_input_len, GFP_KERNEL);
	//pr_info("priv->buf_data points to block mem %zu bytes\n", ksize(priv->buf_data));

end_mode:
	mutex_unlock(&lock);

	return ret;
}

static int wcrc_start_e2e_data_thr(struct wcrc_info *info, struct wcrc_device *priv,
				void *p_u_data, void *p_drv_data, void *p_drv_crc)
{
	int ret;

	//7. Transfer input data to data port of FIFO by DMAC.
	ret = rcar_wcrc_dma_tx(priv, p_u_data, info->data_input_len);
	if (!ret) {
		//pr_err("E2E_CRC_DATA_THROUGH_MODE: run FAILED\n");
		ret = -EFAULT;
		goto end_func;
	}

	//8. Read out input data from data port of FIFO by DMAC.
	ret = rcar_wcrc_dma_rx_in(priv, p_drv_data, info->data_input_len);
	if (!ret) {
		//pr_err("E2E_CRC_DATA_THROUGH_MODE: run FAILED\n");
		ret = -EFAULT;
		goto end_func;
	}

	//9. Read out result data from result port of FIFO by DMAC.
	ret = rcar_wcrc_dma_rx(priv, p_drv_crc, priv->num_crc*4);
	if (!ret) {
		//pr_err("E2E_CRC_DATA_THROUGH_MODE: run FAILED\n");
		ret = -EFAULT;
		goto end_func;
	}


end_func:
	return !ret;
}

static int wcrc_open(struct inode *inode, struct file *filep)
{
	struct wcrc_device *priv;

	priv = container_of(inode->i_cdev, struct wcrc_device, cdev);
	filep->private_data = priv;

	return 0;
}

static int wcrc_release(struct inode *inode, struct file *filep)
{
	struct wcrc_device *priv;

	priv = filep->private_data;
	kfree(priv);
	filep->private_data = NULL;

	return 0;
}

static int extract_data(struct wcrc_info *u_features, struct wcrc_device *priv,
						unsigned long arg, void **u_data)
{
	int ret = 0;
	unsigned int u_len = 0;

	//Read setting from user
	ret = copy_from_user(u_features, (struct wcrc_info *)(arg), sizeof(struct wcrc_info));
	if (ret) {
		ret = -EFAULT;
		goto exit_func;
	}

	u_len = u_features->data_input_len;
	if ((u_len < 4) || (u_len > 1048576*4)) {
		ret = -EINVAL;
		goto exit_func;
	}

	*u_data = kzalloc(u_len, GFP_KERNEL);
	if (*u_data == NULL) {
		ret = -ENOMEM;
		goto exit_func;
	}

	ret = copy_from_user(*u_data, u_features->pdata_input, u_len);
	if (ret) {
		ret = -EFAULT;
		goto exit_func;
	}

exit_func:
	return ret;

}

static long dev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct wcrc_info u_features;
	struct wcrc_device *priv;
	int ret;
	void *u_data;

	priv = filep->private_data;
	ret = 0;
	u_data = NULL;

	if (cmd == E2E_CRC_MODE ||
		cmd == DATA_THROUGH_MODE ||
		cmd == E2E_CRC_DATA_THROUGH_MODE) {
		//Read setting from user
		ret = extract_data(&u_features, priv, arg, &u_data);
		if (ret) {
			//pr_err("Read User Setting FAILED\n");
			ret = -EFAULT;
			goto exit_func;
		}
	}

	switch (cmd) {
	case INDEPENDENT_CRC_MODE:
		ret = copy_from_user(&u_features, (struct wcrc_info *)arg, sizeof(u_features));
		if (ret) {
			//pr_err("INDEPENDENT_CRC_MODE: Error taking data from user\n");
			ret = -EFAULT;
			goto exit_func;
		}

		wcrc_independent_crc(priv, &u_features);

		ret = copy_to_user((struct wcrc_info *)arg, &u_features, sizeof(u_features));
		if (ret) {
			//pr_err("INDEPENDENT_CRC_MODE: Error sending data to user\n");
			ret = -EFAULT;
			goto exit_func;
		}

		break;

	case E2E_CRC_MODE:
		//Setting mode
		ret = priv->ops->set_e2e_crc(&u_features, priv);
		if (ret) {
			pr_err("E2E_CRC_MODE: setting FAILED\n");
			ret = -EFAULT;
			goto exit_func;
		}

		//Run mode
		ret = priv->ops->start_e2e_crc(&u_features, priv,
					u_data, priv->buf_crc);
		if (ret) {
			pr_err("E2E_CRC_MODE: run FAILED\n");
			ret = -EFAULT;
			goto exit_func;
		}

		//Wait mode to finish
		ret = wait_event_interruptible(priv->dma_in_wait, !(priv->ongoing_dma_rx));
		if (ret < 0) {
			pr_info("%s: wait_event_interruptible FAILED\n", __func__);
			ret = -ERESTARTSYS;
			goto exit_func;
		}

		//Return data to user
		ret = copy_to_user(u_features.pcrc_data, priv->buf_crc, priv->num_crc*4);
		if (ret) {
			pr_err("E2E_CRC_MODE: Error sending data to user\n");
			ret = -EFAULT;
			goto exit_func;
		}


		break;

	case DATA_THROUGH_MODE:
		//Setting mode
		ret = priv->ops->set_data_thr(&u_features, priv);
		if (ret) {
			//pr_err("DATA_THROUGH_MODE: setting FAILED\n");
			ret = -EFAULT;
			goto exit_func;
		}

		//Run mode
		ret = priv->ops->start_data_thr(&u_features, priv,
					u_data, priv->buf_data);
		if (ret) {
			//pr_err("DATA_THROUGH_MODE: setting FAILED\n");
			ret = -EFAULT;
			goto exit_func;
		}

		//Wait mode to finish
		ret = wait_event_interruptible(priv->dma_in_wait, !(priv->ongoing_dma_tx | priv->ongoing_dma_rx_in));
		if (ret < 0) {
			//pr_info("%s: wait_event_interruptible FAILED\n", __func__);
			ret = -ERESTARTSYS;
			goto exit_func;
		}

		//Return data to user
		ret = copy_to_user(u_features.pdata_output, priv->buf_data, u_features.data_input_len);
		if (ret) {
			//pr_err("DATA_THROUGH_MODE: Error sending data to user\n");
			ret = -EFAULT;
			goto exit_func;
		}

		break;

	case E2E_CRC_DATA_THROUGH_MODE:
		//Setting mode
		ret = priv->ops->set_e2e_data_thr(&u_features, priv);
		if (ret) {
			//pr_err("E2E_CRC_DATA_THROUGH_MODE: setting FAILED\n");
			ret = -EFAULT;
			goto exit_func;
		}

		//Run mode
		ret = priv->ops->start_e2e_data_thr(&u_features, priv,
				u_data, priv->buf_data, priv->buf_crc);
		if (ret) {
			//pr_err("E2E_CRC_DATA_THROUGH_MODE: setting FAILED\n");
			ret = -EFAULT;
			goto exit_func;
		}

		//Wait mode to finish
		ret = wait_event_interruptible(priv->dma_in_wait, !(priv->ongoing_dma_rx | priv->ongoing_dma_rx_in));
		if (ret < 0) {
			//pr_info("%s: wait_event_interruptible FAILED\n", __func__);
			ret = -ERESTARTSYS;
			goto exit_func;
		}

		//Return data to user
		ret  = copy_to_user(u_features.pdata_output, priv->buf_data, u_features.data_input_len);
		ret |= copy_to_user(u_features.pcrc_data, priv->buf_crc, priv->num_crc*4);
		if (ret) {
			//pr_info("E2E_CRC_DATA_THROUGH_MODE: Error sending data to user\n");
			ret = -EFAULT;
			goto exit_func;
		}

		break;

	default:
		ret = -EINVAL;
		goto exit_func;
	}

	if (cmd == E2E_CRC_MODE ||
		cmd == DATA_THROUGH_MODE ||
		cmd == E2E_CRC_DATA_THROUGH_MODE) {
		//Flush data which is copy from from user.
		kfree(u_data);

		//Stop WCRC
		ret = priv->ops->stop(&u_features, priv);
		if (ret)
			ret = -EFAULT;
	}

exit_func:
	return ret;
}

static const struct of_device_id wcrc_of_ids[] = {
	{
		.compatible = "renesas,crc-wrapper",
	}, {
		.compatible = "renesas,wcrc-r8a78000",
	}, {
		.compatible = "renesas,wcrc-r8a779g0",
	}, {
		.compatible = "renesas,rcar-gen5-wcrc",
	}, {
		/* Terminator */
	},
};

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = wcrc_open,
	.release = wcrc_release,
	.unlocked_ioctl = dev_ioctl,
};

static int rcar_wcrc_init_crc(struct wcrc_device *rwcrc)
{
	const struct device_node *np = rwcrc->dev->of_node;
	int cells;
	struct platform_device *pdev;
	char *propname;
	struct device_node *dn;
	int ret;

	propname = "sub-crc";
	cells = of_property_count_u32_elems(np, propname);
	if (cells == -EINVAL)
		return 0;

	if (cells > 1) {
		dev_err(rwcrc->dev,
			"Invalid number of entries in '%s'\n", propname);
		return -EINVAL;
	}

	dn = of_parse_phandle(np, propname, 0);
	if (!dn) {
		dev_err(rwcrc->dev,
			"Failed to parse '%s' property\n", propname);
		return -EINVAL;
	}

	if (!of_device_is_available(dn)) {
		/* It's NOT OK to have a phandle to a non-enabled property. */
		dev_err(rwcrc->dev,
			"phandle to a non-enabled property '%s'\n", propname);
		return -EINVAL;
	}

	pdev = of_find_device_by_node(dn);
	if (!pdev) {
		dev_err(rwcrc->dev, "No device found for %s\n", propname);
		of_node_put(dn);
		return -EINVAL;
	}

	/*
	 * -ENODEV is used to report that the CRC/KCRC config option is
	 * disabled: return 0 and let the WCRC continue probing.
	 */
	ret = rcar_crc_init(pdev);
	if (ret)
		return ret == -ENODEV ? 0 : ret;
	rwcrc->crc_dev = platform_get_drvdata(pdev);

	return 0;
}

static int rcar_wcrc_init_kcrc(struct wcrc_device *rwcrc)
{
	const struct device_node *np = rwcrc->dev->of_node;
	int cells;
	struct platform_device *pdev;
	char *propname;
	struct device_node *dn;
	int ret;

	propname = "sub-kcrc";
	cells = of_property_count_u32_elems(np, propname);
	if (cells == -EINVAL)
		return 0;

	if (cells > 1) {
		dev_err(rwcrc->dev,
			"Invalid number of entries in '%s'\n", propname);
		return -EINVAL;
	}

	dn = of_parse_phandle(np, propname, 0);
	if (!dn) {
		dev_err(rwcrc->dev,
			"Failed to parse '%s' property\n", propname);
		return -EINVAL;
	}

	if (!of_device_is_available(dn)) {
		/* It's NOT OK to have a phandle to a non-enabled property. */
		dev_err(rwcrc->dev,
			"phandle to a non-enabled property '%s'\n", propname);
		return -EINVAL;
	}

	pdev = of_find_device_by_node(dn);
	if (!pdev) {
		dev_err(rwcrc->dev, "No device found for %s\n", propname);
		of_node_put(dn);
		return -EINVAL;
	}

	/*
	 * -ENODEV is used to report that the CRC/KCRC config option is
	 * disabled: return 0 and let the WCRC continue probing.
	 */
	ret = rcar_kcrc_init(pdev);
	if (ret)
		return ret == -ENODEV ? 0 : ret;
	rwcrc->kcrc_dev = platform_get_drvdata(pdev);

	return 0;
}

/*
 * rcar_wcrc_init() - Initialize the WCRC unit
 * @pdev: The platform device associated with the WCRC instance
 *
 * Return: 0 on success, -EPROBE_DEFER if the WCRC is not available yet.
 */
int rcar_wcrc_init(struct platform_device *pdev)
{
	struct wcrc_device *priv = platform_get_drvdata(pdev);

	if (!priv)
		return -EPROBE_DEFER;

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_wcrc_init);

static const struct wcrc_ops rwcrc_ops = {
	.owner = THIS_MODULE,
	.stop = wcrc_stop,
	.set_e2e_crc = wcrc_setting_e2e_crc,
	.start_e2e_crc = wcrc_start_e2e_crc,
	.set_data_thr = wcrc_setting_data_thr,
	.start_data_thr = wcrc_start_data_thr,
	.set_e2e_data_thr = wcrc_setting_e2e_data_thr,
	.start_e2e_data_thr = wcrc_start_e2e_data_thr,
};

static int wcrc_probe(struct platform_device *pdev)
{
	struct wcrc_device *priv;
	struct device *dev;
	struct resource *res;
	int ret;
	unsigned long irqflags = 0;
	irqreturn_t (*irqhandler)(int irq_num, void *ptr) = rcar_wcrc_irq;

	dev = &pdev->dev;
	priv = devm_kzalloc(dev, sizeof(struct wcrc_device), GFP_KERNEL);
	//pr_info("Addr priv %d: 0x%llx\n", dev_chan, (long long unsigned int)priv);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	/* Map I/O memory */
	priv->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	res = priv->res;
	//pr_info("Instance %d: wcrc_res=0x%llx\n", dev_chan, res->start);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base)) {
		dev_err(dev, "Unable to map I/O for device\n");
		return PTR_ERR(priv->base);
	}

	priv->fifo_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	//pr_info("Instance %d: fifo_res=0x%llx\n", dev_chan, priv->fifo_res->start);

	///* Look up and obtains to a clock node */
	//priv->clk = devm_clk_get(dev, "fck");
	//if (IS_ERR(priv->clk)) {
	//	dev_err(dev, "Failed to get wcrc clock: %ld\n",
	//	PTR_ERR(priv->clk));
	//	return PTR_ERR(priv->clk);
	//}

	///* Enable peripheral clock for register access */
	//ret = clk_prepare_enable(priv->clk);
	//if (ret) {
	//	dev_err(dev,
	//	"failed to enable peripheral clock, error %d\n", ret);
	//	return ret;
	//}

	priv->ops = &rwcrc_ops;

	/* Init DMA */
	priv->dma_rx_in = priv->dma_rx = priv->dma_tx = ERR_PTR(-EPROBE_DEFER);
	priv->buf_rx_in = priv->buf_rx = priv->buf_tx = ERR_PTR(-EPROBE_DEFER);
	init_waitqueue_head(&priv->dma_in_wait);
	priv->ongoing_dma_rx = 0;
	priv->ongoing_dma_tx = 0;
	priv->ongoing_dma_rx_in = 0;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	priv->irq = ret;

	ret = devm_request_irq(dev, priv->irq, irqhandler, irqflags, DEVNAME, priv);
	if (ret < 0) {
		dev_err(dev, "cannot get irq %d\n", priv->irq);
		return ret;
	}

	/* Creating WCRC device */
	priv->devt = MKDEV(MAJOR(wcrc_devt), dev_chan);
	//pr_info("%s: priv->devt=%d\n", __func__, priv->devt);
	cdev_init(&priv->cdev, &fops);
	priv->cdev.owner = THIS_MODULE;
	ret = cdev_add(&priv->cdev, priv->devt, 1);
	if (ret < 0) {
		dev_err(dev, "Unable to add char device\n");
		return ret;
	}

	dev = device_create(wcrc_class, NULL, priv->devt,
				NULL, "wcrc%d", dev_chan);

	if (IS_ERR(dev)) {
		dev_err(dev, "Unable to create device\n");
		cdev_del(&priv->cdev);
		return PTR_ERR(dev);
	}

	dev_chan++;

	platform_set_drvdata(pdev, priv);

	/* Initialize the WCRC sub-modules(CRC, KCRC). */
	ret = rcar_wcrc_init_crc(priv);
	ret |= rcar_wcrc_init_kcrc(priv);
	if (ret)
		return ret;

	return 0;
}

static int wcrc_remove(struct platform_device *pdev)
{
	struct wcrc_device *priv = platform_get_drvdata(pdev);

	rcar_wcrc_release_dma(priv);
	pr_info("%s: priv->devt=%d\n", __func__, priv->devt);
	//cdev_del(&priv->cdev);

	return 0;
}

static struct platform_driver wcrc_driver = {
	.driver = {
		.name = DEVNAME,
		.of_match_table = of_match_ptr(wcrc_of_ids),
		.owner = THIS_MODULE,
	},
	.probe = wcrc_probe,
	.remove = wcrc_remove,
};

static int __init wcrc_init(void)
{
	struct device_node *np;
	int ret;

	np = of_find_matching_node(NULL, wcrc_of_ids);
	if (!np)
		return 0;

	of_node_put(np);

	ret = alloc_chrdev_region(&wcrc_devt, 0, WCRC_DEVICES, DEVNAME);
	if (ret) {
		pr_err("wcrc: Failed to register device\n");
		return ret;
	}

	wcrc_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(wcrc_class)) {
		pr_err("wcrc: Failed to create class\n");
		ret = (PTR_ERR(wcrc_class));
		goto class_err;
	}

	ret = crc_drv_init();
	if (ret) {
		pr_err("crc: Failed to register\n");
		goto drv_reg_err;
	}

	ret = kcrc_drv_init();
	if (ret) {
		pr_err("kcrc: Failed to register\n");
		goto drv_reg_err;
	}

	ret = platform_driver_register(&wcrc_driver);
	if (ret) {
		pr_err("wcrc: Failed to register\n");
		goto drv_reg_err;
	}

	return 0;

drv_reg_err:
	class_destroy(wcrc_class);

class_err:
	unregister_chrdev_region(wcrc_devt, WCRC_DEVICES);

	return ret;
}

static void __exit wcrc_exit(void)
{
	int i;

	platform_driver_unregister(&wcrc_driver);
	for(i = 0; i < 11; i++) {
		pr_info("%s: dev%d\n", __func__, i);
		device_destroy(wcrc_class, MKDEV(MAJOR(wcrc_devt), i));
	}
	if (wcrc_class) {
		pr_info("%s: wcrc_class\n", __func__);
		class_destroy(wcrc_class);
		wcrc_class = NULL;
	}
	unregister_chrdev_region(wcrc_devt, WCRC_DEVICES);
	crc_drv_exit();
	kcrc_drv_exit();
}

module_init(wcrc_init);
module_exit(wcrc_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("huybui2 <huy.bui.wm@renesas.com>");
MODULE_DESCRIPTION("R-Car Cyclic Redundancy Check Wrapper");

