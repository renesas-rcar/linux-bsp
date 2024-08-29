// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car Gen4/Gen5 KCRC Driver
 *
 * Copyright (C) 2024 Renesas Electronics Inc.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <../drivers/soc/renesas/crc-wrapper.h>

/* Register offsets */
/* KCRC[m] data input register */
#define KCRC_DIN 0x0000

/* KCRC[m] data output register */
#define KCRC_DOUT 0x0080
#define DOUT_DEF 0x0 //initialize value

/* KCRC[m] control register */
#define KCRC_CTL 0x0090
#define PSIZE_32 (31 << 16) //default 32-bit
#define PSIZE_16 (15 << 16) //16-bit
#define PSIZE_8 (7 << 16) //8-bit
#define CMD0 BIT(8) //0: Mode N (Normal), 1: Mode R (output reflect)
#define CMD1 BIT(5) //0: Mode N (Normal), 1: Mode R (input reflect)
#define CMD2 BIT(4) //0: Mode M (MSB shift), 1: Mode R (LSB shift)
#define DW_32 0 //default 32-bit fix mode
#define DW_16 BIT(0) //16-bit fix mode
#define DW_8 (3 << 0) //8-bit fix mode

/* KCRC[m] Polynomial register */
#define KCRC_POLY 0x00A0
#define POL_32_ETHERNET 0x04C11DB7 //default 32-bit Ethernet CRC
#define POL_16_CCITT 0x1021 //16-bit CCITT CRC
#define POL_8_SAE_J1850 0x1D //8-bit SAE J1850 CRC
#define POL_8_0x2F 0x2F //8-bit 0x2F CRC
#define POL_32_CRC32C 0x1EDC6F41 //32-bit CRC32C (Castagnoli)

/* KCRC[m] XOR mask register */
#define KCRC_XOR 0x00B0
#define DEF_XOR 0xFFFFFFFF //default value

static u32 kcrc_read(void __iomem *base, unsigned int offset)
{
	return ioread32(base + offset);
}

static void kcrc_write(void __iomem *base, unsigned int offset, u32 data)
{
	iowrite32(data, base + offset);
}

void kcrc_setting(struct kcrc_device *p, struct wcrc_info *info)
{
	unsigned int poly_set;
	unsigned int p_size;
	unsigned int kcrc_cmd;
	unsigned int input_dw;
	u32 reg;

	/* Checking the Polynomial mode */
	switch (info->poly_mode) {
	case POLY_32_ETHERNET:
		p_size = PSIZE_32;
		poly_set = POL_32_ETHERNET;
		break;
	case POLY_16_CCITT_FALSE_CRC16:
		p_size = PSIZE_16;
		poly_set = POL_16_CCITT;
		break;
	case POLY_8_SAE_J1850:
		p_size = PSIZE_8;
		poly_set = POL_8_SAE_J1850;
		break;
	case POLY_8_0x2F:
		p_size = PSIZE_8;
		poly_set = POL_8_0x2F;
		break;
	case POLY_32_0x1EDC6F41:
		p_size = PSIZE_32;
		poly_set = POL_32_CRC32C;
		break;
	default:
		pr_err("ERROR: Polynomial mode NOT found\n");
		return;
	}

	/* Checking KCRC Calculate Mode 0/1/2 */
	kcrc_cmd = (info->kcrc_cmd0 ? CMD0 : 0) |
			(info->kcrc_cmd1 ? CMD1 : 0) |
			(info->kcrc_cmd2 ? CMD2 : 0);

	/* Checking KCRC_DIN and KCRC_DOUT data size setting */
	if (info->d_in_sz == 8)
		input_dw = DW_8;
	else if (info->d_in_sz == 16)
		input_dw = DW_16;
	else  //default 32-bit
		input_dw = DW_32;

	/* Set KCRC_CTL registers. */
	reg = p_size | kcrc_cmd | input_dw;
	kcrc_write(p->base, KCRC_CTL, reg);

	/* Set KCRC_POLY registers. */
	kcrc_write(p->base, KCRC_POLY, poly_set);

	/* Set KCRC_XOR register. */
	kcrc_write(p->base, KCRC_XOR, DEF_XOR);

	/* Set initial value to KCRC_DOUT register. */
	kcrc_write(p->base, KCRC_DOUT, DOUT_DEF);
}
EXPORT_SYMBOL(kcrc_setting);

int kcrc_calculate(struct kcrc_device *p, struct wcrc_info *info)
{
	/* Skiping data input to*/
	if (info->skip_data_in)
		goto geting_output;

	/* Calculating data larger than 4bytes*/
	if (info->conti_cal)
		goto bypass_setting;

	/* Setting KCRC registers */
	kcrc_setting(p, info);

bypass_setting:
	/* Set input value to KCRC_DIN register. */
	kcrc_write(p->base, KCRC_DIN, info->data_input);

geting_output:
	/* Read out the operated data from KCRC_DOUT register. */
	if (!info->during_conti_cal)
		info->kcrc_data_out = kcrc_read(p->base, KCRC_DOUT);

	return 0;
}
EXPORT_SYMBOL(kcrc_calculate);

static const struct of_device_id kcrc_of_ids[] = {
	{
		.compatible = "renesas,kcrc-drv",
	},
	{
		.compatible = "renesas,kcrc-r8a78000",
	},
	{
		.compatible = "renesas,rcar-gen5-kcrc",
	},
	{
		/* Terminator */
	}
};

static int kcrc_probe(struct platform_device *pdev)
{
	struct kcrc_device *priv;
	struct device *dev;
	struct resource *res;
	int ret;

	dev = &pdev->dev;
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	platform_set_drvdata(pdev, priv);
	if (!priv)
		return -ENOMEM;

	/* Map I/O memory */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pr_info("Instance: kcrc_res=0x%llx", res->start);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	///* Look up and obtains to a clock node */
	//priv->clk = devm_clk_get(dev, "fck");
	//if (IS_ERR(priv->clk)) {
	//	dev_err(dev, "Failed to get kcrc clock: %ld\n",
	//	PTR_ERR(priv->clk));
	//	return PTR_ERR(priv->clk);
	//}

	///* Enable peripheral clock for register access */
	//ret = clk_prepare_enable(priv->clk);
	//if (ret) {
	//	dev_err(dev, "failed to enable peripheral clock, error %d\n", ret);
	//	return ret;
	//}

	return 0;
}

int rcar_kcrc_init(struct platform_device *pdev)
{
	struct kcrc_device *rkcrc = platform_get_drvdata(pdev);

	if (!rkcrc)
		return -EPROBE_DEFER;

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_kcrc_init);

static int kcrc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver kcrc_driver = {
	.driver = {
		.name = "kcrc-driver",
		.of_match_table = of_match_ptr(kcrc_of_ids),
		.owner = THIS_MODULE,
	},
	.probe = kcrc_probe,
	.remove = kcrc_remove,
};

static int __init kcrc_drv_init(void)
{
	struct device_node *np;
	int ret;

	np = of_find_matching_node(NULL, kcrc_of_ids);
	if (!np)
		return 0;

	of_node_put(np);

	ret = platform_driver_register(&kcrc_driver);
	if (ret < 0)
		return ret;

	return 0;
}

static void __exit kcrc_drv_exit(void)
{
	platform_driver_unregister(&kcrc_driver);
}
module_init(kcrc_drv_init);
module_exit(kcrc_drv_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("huybui2 <huy.bui.wm@renesas.com>");
MODULE_DESCRIPTION("R-Car Cyclic Redundancy Check Wrapper");

