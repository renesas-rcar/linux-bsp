// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar-isp.c  --  R-Car Image Signal Processor Driver
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 *
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <media/rcar-isp.h>
#include <media/v4l2-device.h>

struct rcar_isp_info {
	int ch_start;
	int ch_end;
};

struct rcar_isp_device {
	struct list_head list;
	struct device *dev;
	void __iomem *base;
	const struct rcar_isp_info *info;
	u32 id;
};

static LIST_HEAD(isp_devices);
static DEFINE_MUTEX(isp_lock);

#define ISPVCR				0x0000

#define ISPFIFOCTRL			0x0004
#define FIFOCTRL_FIFO_PUSH_CSI		BIT(2)
#define FIFOCTRL_FIFO_PUSH_DVP1		BIT(1)
#define FIFOCTRL_FIFO_PUSH_DVP0		BIT(0)

#define ISPINPUTSEL0			0x0008
#define ISPINPUTSEL0_SEL_CSI0		BIT(31)

#define ISPSTART			0x0014
#define ISPSTART_START_ISP		0xffff
#define ISPSTART_STOP_ISP		0x0

#define	ISP_PADDING_CTRL		0x00c0

#define ISPWP_CTRL			0x0100
#define ISPWP_UNLOCK_CODE_U		(0xc97e << 16)
#define ISPWP_UNLOCK_CODE_L		0xfb69

#define	ISPPROCMODE_DT(n)		(0x1100 + (0x4 * n))	/* n = 0-63 */

#define	ISPWUP_EOF_MATCH_ADDRESS(n)	(0x2100 + (0x4 * n))	/* n = 0-31 */

#define	ISPWUP_EOF_MATCH_ID(n)		(0x2200 + (0x4 * n))	/* n = 0-31 */

#define ISPCS_FILTER_ID_CH(n)		(0x3000 + (0x0100 * n))
#define ISPCS_LC_MODULO_CH(n)		(0x3004 + (0x100 * n))

#define ISPCS_DT_CODE03_CH(n)		(0x3008 + (0x100 * n))
#define DT_CODE03_EN0			BIT(7)
#define DT_CODE03_EN1			BIT(15)
#define DT_CODE03_EN2			BIT(23)
#define DT_CODE03_EN3			BIT(31)
#define DT_CODE03_ALL_EN		(DT_CODE03_EN0 | DT_CODE03_EN1 \
					| DT_CODE03_EN2 | DT_CODE03_EN3)

#define ISPCS_DT_CODE47_CH(n)		(0x300C + (0x100 * n))
#define ISPCS_H_CLIP_DT_CODE_CH(m, n)	(0x3020 + (0x4 * m) + (0x100 * n))
#define ISPCS_V_CLIP_DT_CODE_CH(m, n)	(0x3030 + (0x4 * m) + (0x100 * n))
#define ISPCS_OUTPUT_MODE_CH03(n)	(0x0020 + (0x4 * n))
#define ISPCS_OUTPUT_MODE_CH47(n)	(0x0120 + (0x4 * (n - 4)))
#define ISPCS_DI_FILTER_CTRL_CH(n)	(0x3040 + (0x100 * m))
#define ISPCS_DI_FILTER_LUT_CH(p, n)	(0x3080 + (0x4 * p) + (0x100 * n))

#define MIPI_DT_YUV420_8		0x18
#define MIPI_DT_YUV420_10		0x19
#define MIPI_DT_YUV422_8		0x1e
#define MIPI_DT_YUV422_10		0x1f
#define MIPI_DT_RGB565			0x22
#define MIPI_DT_RGB888			0x24
#define MIPI_DT_RAW8			0x2a
#define MIPI_DT_RAW10			0x2b
#define MIPI_DT_RAW12			0x2c
#define MIPI_DT_RAW14			0x2d
#define MIPI_DT_RAW16			0x2e
#define MIPI_DT_RAW20			0x2f

#define SRCR6				0xE6152C18
#define SRSTCLR6			0xE6152C98
#define SR_REG_OFFSET			12

static void isp_write(struct rcar_isp_device *isp, u32 value, u32 offset)
{
	iowrite32(value, isp->base + offset);
}

static u32 isp_read(struct rcar_isp_device *isp, u32 offset)
{
	return ioread32(isp->base + offset);
}

/* -----------------------------------------------------------------------------
 * Public API
 */

/**
 * rcar_isp_get - Find and acquire a reference to an ISP instance
 * @np: Device node of the ISP instance
 *
 * Search the list of registered ISP instances for the instance corresponding to
 * the given device node.
 *
 * Return a pointer to the ISP instance, or an ERR_PTR if the instance can't be
 * found.
 */
struct rcar_isp_device *rcar_isp_get(const struct device_node *np)
{
	struct rcar_isp_device *isp;

	mutex_lock(&isp_lock);

	list_for_each_entry(isp, &isp_devices, list) {
		if (isp->dev->of_node != np)
			continue;

		get_device(isp->dev);
		goto done;
	}

	isp = ERR_PTR(-EPROBE_DEFER);

done:
	mutex_unlock(&isp_lock);
	return isp;
}
EXPORT_SYMBOL_GPL(rcar_isp_get);

/**
 * rcar_isp_put - Release a reference to an ISP instance
 * @isp: The ISP instance
 *
 * Release the ISP instance acquired by a call to rcar_isp_get().
 */
void rcar_isp_put(struct rcar_isp_device *isp)
{
	if (isp)
		put_device(isp->dev);
}
EXPORT_SYMBOL_GPL(rcar_isp_put);

struct device *rcar_isp_get_device(struct rcar_isp_device *isp)
{
	return isp->dev;
}
EXPORT_SYMBOL_GPL(rcar_isp_get_device);

/**
 * rcar_isp_enable - Enable an ISP
 * @isp: The ISP instance
 *
 * Before any memory access through an ISP is performed by a module, the ISP
 * must be enabled by a call to this function. The enable calls are reference
 * counted, each successful call must be followed by one rcar_isp_disable()
 * call when no more memory transfer can occur through the ISP.
 *
 * Return 0 on success or a negative error code if an error occurs. The enable
 * reference count isn't increased when this function returns an error.
 */
int rcar_isp_enable(struct rcar_isp_device *isp)
{
	int ret;
	void __iomem *srstclr6_reg;

	if (!isp)
		return 0;

	ret = pm_runtime_get_sync(isp->dev);
	if (ret < 0)
		return ret;

	srstclr6_reg = ioremap(SRSTCLR6, 0x04);
	writel((0x01 << (isp->id + SR_REG_OFFSET)), srstclr6_reg);
	iounmap(srstclr6_reg);

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_isp_enable);

/**
 * rcar_isp_disable - Disable an ISP
 * @isp: The ISP instance
 *
 * This function is the counterpart of rcar_isp_enable(). As enable calls are
 * reference counted a disable call may not disable the ISP synchronously.
 */
void rcar_isp_disable(struct rcar_isp_device *isp)
{
	if (isp) {
		void __iomem *srcr6_reg;

		srcr6_reg = ioremap(SRCR6, 0x04);
		writel((0x01 << (isp->id + SR_REG_OFFSET)), srcr6_reg);
		iounmap(srcr6_reg);
		pm_runtime_put(isp->dev);
	}
}
EXPORT_SYMBOL_GPL(rcar_isp_disable);

static inline int rcar_mbus_to_data_type(struct rcar_isp_device *isp,
					 u32 mbus_code)
{
	switch (mbus_code) {
	case MEDIA_BUS_FMT_Y10_1X10:
		return MIPI_DT_RAW10;
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_YUYV10_2X10:
		return MIPI_DT_YUV422_8;
	case MEDIA_BUS_FMT_RGB888_1X24:
		return MIPI_DT_RGB888;
	}

	dev_err(isp->dev, "mbus type is not found\n");

	return -1;
}

static inline int rcar_data_type_to_proc_mode(struct rcar_isp_device *isp,
					      u32 data_type)
{
	switch (data_type) {
	case MIPI_DT_RAW8:
		return 0x00;
	case MIPI_DT_RAW10:
		return 0x01;
	case MIPI_DT_RAW12:
		return 0x02;
	case MIPI_DT_RAW14:
		return 0x03;
	case MIPI_DT_RAW16:
		return 0x04;
	case MIPI_DT_RAW20:
		return 0x05;
	case MIPI_DT_YUV420_8:
		return 0x0a;
	case MIPI_DT_YUV420_10:
		return 0x0b;
	case MIPI_DT_YUV422_8:
		return 0x0c;
	case MIPI_DT_YUV422_10:
		return 0x0d;
	case MIPI_DT_RGB565:
		return 0x14;
	case MIPI_DT_RGB888:
		return 0x15;
	}

	dev_err(isp->dev, "data type is not found\n");

	return -1;
}

static void rcar_isp_pre_init(struct rcar_isp_device *isp,
			      u32 ch, u32 vc, u32 data_type)
{
	int i;
	u32 dt_code_val = 0;

	isp_write(isp, (0x01 << vc), ISPCS_FILTER_ID_CH(ch));
	isp_write(isp, 0x00000000, ISPCS_LC_MODULO_CH(ch));

	dt_code_val = (data_type << 24) | (data_type << 16) |
		      (data_type << 8) | data_type;
	isp_write(isp, DT_CODE03_ALL_EN | dt_code_val, ISPCS_DT_CODE03_CH(ch));

	/* Filer slot4,5,6,7 are not used */
	isp_write(isp, 0x00000000, ISPCS_DT_CODE47_CH(ch));

	/* Set default value */
	for (i = 0; i < 4; i++) {
		isp_write(isp, 0x0fff0000, ISPCS_H_CLIP_DT_CODE_CH(i, ch));
		isp_write(isp, 0x0fff0000, ISPCS_V_CLIP_DT_CODE_CH(i, ch));
	}
	/* Don't set ISPCS_OUTPUT_MODE_CHn for selecting channel selector */

	return;
}

int rcar_isp_init(struct rcar_isp_device *isp, u32 mbus_code)
{
	int ch, ch_s, ch_e, vc;
	u32 sel_csi = 0;
	u32 data_type;
	u32 proc_val;

	if (!isp)
		return 0;

	data_type = rcar_mbus_to_data_type(isp, mbus_code);
	proc_val = rcar_data_type_to_proc_mode(isp, data_type);

	ch_s = isp->info->ch_start;
	ch_e = isp->info->ch_end;

	for (ch = ch_s, vc = 0; ch < ch_e && vc < 4; ch++, vc++)
		rcar_isp_pre_init(isp, ch, vc, data_type);

	isp_write(isp, ISPWP_UNLOCK_CODE_U | ISPWP_UNLOCK_CODE_L, ISPWP_CTRL);
	if (isp->id % 2)
		sel_csi = ISPINPUTSEL0_SEL_CSI0;
	isp_write(isp, isp_read(isp, ISPINPUTSEL0) | sel_csi, ISPINPUTSEL0);
	isp_write(isp, isp_read(isp, ISP_PADDING_CTRL) | 0x20,
		  ISP_PADDING_CTRL);
	isp_write(isp, (proc_val << 24) | (proc_val << 16) | (proc_val << 8) |
		  proc_val, ISPPROCMODE_DT(data_type));
	isp_write(isp, ISPWP_UNLOCK_CODE_U | ISPWP_UNLOCK_CODE_L, ISPWP_CTRL);

	isp_write(isp, FIFOCTRL_FIFO_PUSH_CSI, ISPFIFOCTRL);
	isp_write(isp, ISPSTART_START_ISP, ISPSTART);

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_isp_init);

/* -----------------------------------------------------------------------------
 * Platform Driver
 */
static int rcar_isp_parse(struct rcar_isp_device *isp)
{
	struct device_node *np;
	u32 id;
	int ret;

	np = isp->dev->of_node;

	ret = of_property_read_u32(np, "renesas,id", &id);
	if (ret) {
		dev_err(isp->dev, "%pOF: No renesas,id property found\n",
			isp->dev->of_node);
		return -EINVAL;
	}
	isp->id = id;

	return 0;
}

static int rcar_isp_probe(struct platform_device *pdev)
{
	struct rcar_isp_device *isp;
	struct resource *mem;

	isp = devm_kzalloc(&pdev->dev, sizeof(*isp), GFP_KERNEL);
	if (isp == NULL)
		return -ENOMEM;

	isp->dev = &pdev->dev;
	isp->info = of_device_get_match_data(&pdev->dev);

	rcar_isp_parse(isp);

	/* Get Channel selector register */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;

	isp->base = devm_ioremap_resource(isp->dev, mem);
	if (IS_ERR(isp->base))
		return PTR_ERR(isp->base);

	pm_runtime_enable(&pdev->dev);

	mutex_lock(&isp_lock);
	list_add_tail(&isp->list, &isp_devices);
	mutex_unlock(&isp_lock);

	platform_set_drvdata(pdev, isp);

	dev_info(isp->dev, "probed.\n");

	return 0;
}

static int rcar_isp_remove(struct platform_device *pdev)
{
	struct rcar_isp_device *isp = platform_get_drvdata(pdev);

	mutex_lock(&isp_lock);
	list_del(&isp->list);
	mutex_unlock(&isp_lock);

	pm_runtime_disable(&pdev->dev);

	return 0;
}
static const struct rcar_isp_info rcar_isp_info_r8a779a0 = {
	.ch_start = 4,
	.ch_end = 8,
};

static const struct rcar_isp_info rcar_isp_info_r8a779g0 = {
	.ch_start = 4,
	.ch_end = 8,
};

static const struct of_device_id rcar_isp_of_match[] = {
	{
		.compatible = "renesas,isp-r8a779a0",
		.data = &rcar_isp_info_r8a779a0,
	},
	{
		.compatible = "renesas,isp-r8a779g0",
		.data = &rcar_isp_info_r8a779g0,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_isp_of_match);

static struct platform_driver rcar_isp_platform_driver = {
	.probe		= rcar_isp_probe,
	.remove		= rcar_isp_remove,
	.driver		= {
		.name	= "rcar-isp",
		.of_match_table = rcar_isp_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(rcar_isp_platform_driver);

MODULE_ALIAS("rcar-isp");
MODULE_DESCRIPTION("Renesas ISP Channel Selector Driver");
MODULE_LICENSE("GPL");
