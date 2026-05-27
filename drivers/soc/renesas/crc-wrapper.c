// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car Gen4/Gen5 WCRC Driver
 *
 * Copyright (C) 2024 Renesas Electronics Inc.
 *
 */

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/types.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "crc-wrapper.h"

#define DEVNAME "crc-wrapper"
#define CLASS_NAME "wcrc"

/* Register offset */
#define CRC_M        (2)
#define KCRC_M       (3)

/* Address assignment of FIFO */
/* Data */
#define PORT_DATA(mod) ({			\
	int _mod = (mod);			\
	((_mod) == (CRC_M))  ? (0x800) :	\
	((_mod) == (KCRC_M)) ? (0xC00) :	\
	(0x800);				\
})

/* Command */
#define PORT_CMD(mod) ({			\
	int _mod = (mod);			\
	((_mod) == (CRC_M))  ? (0x900) :	\
	((_mod) == (KCRC_M)) ? (0xD00) :	\
	(0x900);				\
})

#define PORT_EXPT_DATA(mod) ({			\
	int _mod = (mod);			\
	((_mod) == (CRC_M))  ? (0xA00) :	\
	((_mod) == (KCRC_M)) ? (0xE00) :	\
	(0xA00);				\
})

/* Result */
#define PORT_RES(mod) ({			\
	int _mod = (mod);			\
	((_mod) == (CRC_M))  ? (0xB00) :	\
	((_mod) == (KCRC_M)) ? (0xF00) :	\
	(0xB00);				\
})

/* WCRC register (XXXX: CRC_M or KCRC_M) */

/* WCRC_XXXX_EN transfer enable register */
#define WCRC_CRC_EN 0x0800
#define WCRC_KCRC_EN 0x0C00
#define WCRC_XXXX_EN(mod) ({			\
	int _mod = (mod);			\
	((_mod) == (CRC_M))  ? (WCRC_CRC_EN)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_EN) :	\
	(WCRC_CRC_EN);				\
})
#define OUT_EN BIT(16)
#define RES_EN BIT(8)
#define TRANS_EN BIT(1)
#define IN_EN BIT(0)

/* WCRC_XXXX_STOP transfer stop register */
#define WCRC_CRC_STOP 0x0820
#define WCRC_KCRC_STOP 0x0C20
#define WCRC_XXXX_STOP(mod) ({				\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_STOP)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_STOP) :	\
	(WCRC_CRC_STOP);				\
})
#define STOP BIT(0)

/* WCRC_XXXX_CMDEN transfer command enable register */
#define WCRC_CRC_CMDEN 0x0830
#define WCRC_KCRC_CMDEN 0x0C30
#define WCRC_XXXX_CMDEN(mod) ({				\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_CMDEN)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_CMDEN) :	\
	(WCRC_CRC_CMDEN);				\
})
#define CMD_EN BIT(0)

/* WCRC_XXXX_COMP compare setting register */
#define WCRC_CRC_COMP 0x0840
#define WCRC_KCRC_COMP 0x0C40
#define WCRC_XXXX_COMP(mod) ({				\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_COMP)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_COMP) :	\
	(WCRC_CRC_COMP);				\
})
#define COMP_FREQ_16 (0 << 16)
#define COMP_FREQ_32 BIT(16)
#define COMP_FREQ_64 (3 << 16)
#define EXP_REQSEL BIT(1)
#define COMP_EN BIT(0)

/* WCRC_XXXX_COMP_RES compare result register regrister */
#define WCRC_CRC_COMP_RES 0x0850
#define WCRC_KCRC_COMP_RES 0x0C50
#define WCRC_XXXX_COMP_RES(mod) ({			\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_COMP_RES)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_COMP_RES) :	\
	(WCRC_CRC_COMP_RES);				\
})

/* WCRC_XXXX_CONV conversion setting register */
#define WCRC_CRC_CONV 0x0870
#define WCRC_KCRC_CONV 0x0C70
#define WCRC_XXXX_CONV(mod) ({				\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_CONV)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_CONV) :	\
	(WCRC_CRC_CONV);				\
})

/* WCRC_XXXX_WAIT wait register */
#define WCRC_CRC_WAIT 0x0880
#define WCRC_KCRC_WAIT 0x0C80
#define WCRC_XXXX_WAIT(mod) ({				\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_WAIT)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_WAIT) :	\
	(WCRC_CRC_WAIT);				\
})
#define WAIT BIT(0)

/* WCRC_XXXX_INIT_CRC initial CRC code register */
#define WCRC_CRC_INIT_CRC 0x0910
#define WCRC_KCRC_INIT_CRC 0x0D10
#define WCRC_XXXX_INIT_CRC(mod) ({			\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_INIT_CRC)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_INIT_CRC) :	\
	(WCRC_CRC_INIT_CRC);				\
})
#define INIT_CODE 0xFFFFFFFF

/* WCRC_XXXX_STS status register */
#define WCRC_CRC_STS 0x0A00
#define WCRC_KCRC_STS 0x0E00
#define WCRC_XXXX_STS(mod) ({				\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_STS)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_STS) :	\
	(WCRC_CRC_STS);					\
})
#define STOP_DONE BIT(31)
#define CMD_DONE BIT(24)
#define RES_DONE BIT(20)
#define COMP_ERR BIT(13)
#define COMP_DONE BIT(12)
#define TRANS_DONE BIT(0)

/* WCRC_XXXX_INTEN interrupt enable register */
#define WCRC_CRC_INTEN 0x0A40
#define WCRC_KCRC_INTEN 0x0E40
#define WCRC_XXXX_INTEN(mod) ({				\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_INTEN)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_INTEN) :	\
	(WCRC_CRC_INTEN);				\
})
#define STOP_DONE_IE BIT(31)
#define CMD_DONE_IE BIT(24)
#define RES_DONE_IE BIT(20)
#define COMP_ERR_IE BIT(13)
#define COMP_DONE_IE BIT(12)
#define TRANS_DONE_IE BIT(0)

/* WCRC_XXXX_ECMEN ECM output enable register */
#define WCRC_CRC_ECMEN 0x0A80
#define WCRC_KCRC_ECMEN 0x0E80
#define WCRC_XXXX_ECMEN(mod) ({				\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_ECMEN)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_ECMEN) :	\
	(WCRC_CRC_ECMEN);				\
})
#define COMP_ERR_OE BIT(13)

/* WCRC_XXXX_BUF_STS_RDEN Buffer state read enable register */
#define WCRC_CRC_BUF_STS_RDEN 0x0AA0
#define WCRC_KCRC_BUF_STS_RDEN 0x0EA0
#define WCRC_XXXX_BUF_STS_RDEN(mod) ({				\
	int _mod = (mod);					\
	((_mod) == (CRC_M))  ? (WCRC_CRC_BUF_STS_RDEN)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_BUF_STS_RDEN) :	\
	(WCRC_CRC_BUF_STS_RDEN);				\
})
#define CODE_VALUE (0xA5A5 << 16)
#define BUF_STS_RDEN BIT(0)

/* WCRC_XXXX_BUF_STS Buffer state read register */
#define WCRC_CRC_BUF_STS 0x0AA4
#define WCRC_KCRC_BUF_STS 0x0EA4
#define WCRC_XXXX_BUF_STS(mod) ({			\
	int _mod = (mod);				\
	((_mod) == (CRC_M))  ? (WCRC_CRC_BUF_STS)  :	\
	((_mod) == (KCRC_M)) ? (WCRC_KCRC_BUF_STS) :	\
	(WCRC_CRC_BUF_STS);				\
})
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
static struct class *wcrc_class;

static u32 wcrc_read(void __iomem *base, unsigned int offset)
{
	return ioread32(base + offset);
}

static void wcrc_write(void __iomem *base, unsigned int offset, u32 data)
{
	iowrite32(data, base + offset);
}

static irqreturn_t rcar_wcrc_irq(int irq_num, void *ptr)
{
	struct wcrc_device *priv = ptr;
	u32 reg_val;

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

static int wcrc_stop(struct wcrc_info *info, struct wcrc_device *priv)
{
	int ret;
	int module;
	unsigned int reg_val;

	ret = 0;
	if (info->crc_opt == 0) {
		module = CRC_M;
	} else if (info->crc_opt == 1) {
		module = KCRC_M;
	} else {
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
	return ret;
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
	filep->private_data = NULL;

	return 0;
}

static long dev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct wcrc_info u_feat;
	struct wcrc_device *priv;
	int ret;
	void *u_data;

	priv = filep->private_data;
	ret = 0;
	u_data = NULL;

	switch (cmd) {
	case INDEPENDENT_CRC_MODE:
		ret = copy_from_user(&u_feat, (struct wcrc_info *)arg, sizeof(u_feat));
		if (ret) {
			//pr_err("INDEPENDENT_CRC_MODE: Error taking data from user\n");
			ret = -EFAULT;
			goto exit_func;
		}

		wcrc_independent_crc(priv, &u_feat);

		ret = copy_to_user((struct wcrc_info *)arg, &u_feat, sizeof(u_feat));
		if (ret) {
			//pr_err("INDEPENDENT_CRC_MODE: Error sending data to user\n");
			ret = -EFAULT;
			goto exit_func;
		}

		break;

	default:
		ret = -EINVAL;
		goto exit_func;
	}

exit_func:
	return ret;
}

static const struct of_device_id wcrc_of_ids[] = {
	{
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

	/* Look up and obtains to a clock node */
	priv->clk = devm_clk_get(dev, "fck");
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	/* Enable peripheral clock for register access */
	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev,
			"Failed to enable peripheral clock, error %d\n", ret);
		return ret;
	}

	priv->ops = &rwcrc_ops;

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

static void wcrc_remove(struct platform_device *pdev)
{
	struct wcrc_device *priv = platform_get_drvdata(pdev);

	pr_info("%s: priv->devt=%d\n", __func__, priv->devt);
	//cdev_del(&priv->cdev);
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

	wcrc_class = class_create(CLASS_NAME);
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
	for (i = 0; i < 11; i++) {
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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huy Bui <huy.bui.wm@renesas.com>");
MODULE_DESCRIPTION("R-Car Cyclic Redundancy Check Wrapper");
