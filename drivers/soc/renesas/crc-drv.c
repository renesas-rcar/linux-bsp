// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car Gen4/Gen5 CRC Driver
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

/* Register offset */
/* CRC[m] Input register */
#define DCRA_CIN 0x0000

/* CRC[m] Data register */
#define DCRA_COUT 0x0004
#define COUT_DEF 0xFFFFFFFF //default; value for CRC calculation
//initial value of each polynimial: CRC calulation method; polynomial
#define COUT_32_ETHERNET 0xFFFFFFFF //default; CRC-32-IEEE 802.3; 04C11DB7
#define COUT_16_CCITT_FALSE_CRC16 0xFFFF //CCITT_FALSE_CRC16; 1021
#define COUT_8_SAE_J1850 0xFF //SAE_J1850; 1D
#define COUT_8_0x2F 0xFF // 0x2F polynomial
#define COUT_32_0xF4ACFB13 0xFFFFFFFF //0xF4ACFB13 polynomial
#define COUT_32_0x1EDC6F41 0xFFFFFFFF //0x1EDC6F41 polynomial CRC-32 (Castagnoli)
#define COUT_21_0x102899 0x1FFFFF //0x102899 polynomial CRC-21
#define COUT_17_0x1685B 0x1FFFF //0x1685B polynomial CRC-17
#define COUT_15_0x4599 0x7FFF //0x4599 polynomial CRC-15

/* CRC[m] Control register */
#define DCRA_CTL 0x0020
#define ISZ_32 0 //default 32bit width DCRA_CIN_31:0
#define ISZ_16 BIT(4) //16bit width DCRA_CIN_15:0
#define ISZ_8 BIT(5) //8bit width DCRA_CIN_7:0
#define POL_32_ETHERNET 0 //default CRC-32-IEEE 802.3
#define POL_16_CCITT_FALSE_CRC16 BIT(0) //CCITT_FALSE_CRC16
#define POL_8_SAE_J1850 BIT(1) //SAE_J1850
#define POL_8_0x2F (3 << 0) // 0x2F polynomial
#define POL_32_0xF4ACFB13 BIT(2) //0xF4ACFB13 polynomial
#define POL_32_0x1EDC6F41 (5 << 0) //0x1EDC6F41 polynomial CRC-32 (Castagnoli)
#define POL_21_0x102899 (6 << 0) //0x102899 polynomial CRC-21
#define POL_17_0x1685B (7 << 0) //0x1685B polynomial CRC-17
#define POL_15_0x4599 BIT(3) //0x4599 polynomial CRC-15

/* CRC[m] Control register 2 */
#define DCRA_CTL2 0x0040
#define xorvalmode BIT(7) //EXOR ON of output data
#define bitswapmode BIT(6) //bit swap of output data
#define byteswapmode_00 0 //default no byte swap of output data
#define byteswapmode_01 BIT(4)
#define byteswapmode_10 BIT(5)
#define byteswapmode_11 (3 << 3)
#define xorvalinmode BIT(3) //EXOR ON of input data
#define bitswapinmode BIT(2) //bit swap of input data
#define byteswapinmode_00 0 //default no byte swap of input data
#define byteswapinmode_01 BIT(0)
#define byteswapinmode_10 BIT(1)
#define byteswapinmode_11 (3 << 0)

static u32 crc_read(void __iomem *base, unsigned int offset)
{
	return ioread32(base + offset);
}

static void crc_write(void __iomem *base, unsigned int offset, u32 data)
{
	iowrite32(data, base + offset);
}

void crc_setting(struct crc_device *p, struct wcrc_info *info)
{
	unsigned int crc_size;
	unsigned int poly_set;
	unsigned int initial_set;
	unsigned int crc_cmd;
	u32 reg;

	/* Checking the Polynomial mode */
	switch (info->poly_mode) {
	case POLY_32_ETHERNET:
		poly_set = POL_32_ETHERNET;
		initial_set = COUT_32_ETHERNET;
		break;
	case POLY_16_CCITT_FALSE_CRC16:
		poly_set = POL_16_CCITT_FALSE_CRC16;
		initial_set = COUT_16_CCITT_FALSE_CRC16;
		break;
	case POLY_8_SAE_J1850:
		poly_set = POL_8_SAE_J1850;
		initial_set = COUT_8_SAE_J1850;
		break;
	case POLY_8_0x2F:
		poly_set = POL_8_0x2F;
		initial_set = COUT_8_0x2F;
		break;
	case POLY_32_0xF4ACFB13:
		poly_set = POL_32_0xF4ACFB13;
		initial_set = COUT_32_0xF4ACFB13;
		break;
	case POLY_32_0x1EDC6F41:
		poly_set = POL_32_0x1EDC6F41;
		initial_set = COUT_32_0x1EDC6F41;
		break;
	case POLY_21_0x102899:
		poly_set = POL_21_0x102899;
		initial_set = COUT_21_0x102899;
		break;
	case POLY_17_0x1685B:
		poly_set = POL_17_0x1685B;
		initial_set = COUT_17_0x1685B;
		break;
	case POLY_15_0x4599:
		poly_set = POL_15_0x4599;
		initial_set = COUT_15_0x4599;
		break;
	default:
		pr_err("ERROR: Polynomial mode NOT found\n");
		return;
	}

	/* Checking DCRA_CIN data size setting */
	if (info->d_in_sz == 8)
		crc_size = ISZ_8;
	else if (info->d_in_sz == 16)
		crc_size = ISZ_16;
	else // default 32-bit
		crc_size = ISZ_32;

	/* Set DCRA_CTL registers. */
	reg = crc_size | poly_set;
	crc_write(p->base, DCRA_CTL, reg);

	/* Checking DCRA_CTL2 setting */
	crc_cmd = (info->out_exor_on ? xorvalmode : 0) |
			(info->out_bit_swap ? bitswapmode : 0) |
			(info->in_exor_on ? xorvalinmode : 0) |
			(info->in_bit_swap ? bitswapinmode : 0);

	switch (info->out_byte_swap) {
	case 01:
		crc_cmd |= byteswapmode_01;
		break;
	case 10:
		crc_cmd |= byteswapmode_10;
		break;
	case 11:
		crc_cmd |= byteswapmode_11;
		break;
	default: // no swap
		crc_cmd |= byteswapmode_00;
		break;
	}

	switch (info->in_byte_swap) {
	case 01:
		crc_cmd |= byteswapinmode_01;
		break;
	case 10:
		crc_cmd |= byteswapinmode_10;
		break;
	case 11:
		crc_cmd |= byteswapinmode_11;
		break;
	default: // no swap
		crc_cmd |= byteswapinmode_00;
		break;
	}

	/* Set DCRA_CTL2 registers. */
	crc_write(p->base, DCRA_CTL2, crc_cmd);

	/* Set initial value to DCRA_COUT register. */
	crc_write(p->base, DCRA_COUT, COUT_DEF);

	/* Set polynomial initial value to DCRA_COUT register. */
	crc_write(p->base, DCRA_COUT, initial_set);
}
EXPORT_SYMBOL(crc_setting);

int crc_calculate(struct crc_device *p, struct wcrc_info *info)
{
	/* Skiping data input to DCRA_CIN */
	if (info->skip_data_in)
		goto geting_output;

	/* Calculating data larger than 4bytes */
	if (info->conti_cal)
		goto bypass_setting;

	/* Setting CRC registers */
	crc_setting(p, info);

bypass_setting:
	/* Set input value to DCRA_CIN register. */
	crc_write(p->base, DCRA_CIN, info->data_input);

geting_output:
	/* Read out the operated data from DCRA_COUT register. */
	if (!info->during_conti_cal)
		info->crc_data_out = crc_read(p->base, DCRA_COUT);

	return 0;
}
EXPORT_SYMBOL(crc_calculate);

static const struct of_device_id crc_of_ids[] = {
	{
		.compatible = "renesas,crc-drv",
	},
	{
		.compatible = "renesas,crc-r8a78000",
	},
	{
		.compatible = "renesas,rcar-gen5-crc",
	},
	{
		/* Terminator */
	}
};

static int crc_probe(struct platform_device *pdev)
{
	struct crc_device *priv;
	struct device *dev;
	struct resource *res;

	dev = &pdev->dev;
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Map I/O memory */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pr_info("Instance: crc_res=0x%llx\n", res->start);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	///* Look up and obtains to a clock node */
	//priv->clk = devm_clk_get(dev, "fck");
	//if (IS_ERR(priv->clk)) {
	//	dev_err(dev, "Failed to get crc clock: %ld\n",
	//	PTR_ERR(priv->clk));
	//	return PTR_ERR(priv->clk);
	//}

	///* Enable peripheral clock for register access */
	//ret = clk_prepare_enable(priv->clk);
	//if (ret) {
	//	dev_err(dev, "failed to enable peripheral clock, error %d\n", ret);
	//	return ret;
	//}

	platform_set_drvdata(pdev, priv);

	return 0;
}

int rcar_crc_init(struct platform_device *pdev)
{
	struct crc_device *rcrc = platform_get_drvdata(pdev);

	if (!rcrc)
		return -EPROBE_DEFER;

	return 0;
}
EXPORT_SYMBOL_GPL(rcar_crc_init);

static int crc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver crc_driver = {
	.driver = {
		.name = "crc-driver",
		.of_match_table = of_match_ptr(crc_of_ids),
		.owner = THIS_MODULE,
	},
	.probe = crc_probe,
	.remove = crc_remove,
};

int __init crc_drv_init(void)
{
	struct device_node *np;
	int ret;

	np = of_find_matching_node(NULL, crc_of_ids);
	if (!np)
		return 0;

	of_node_put(np);

	ret = platform_driver_register(&crc_driver);
	if (ret < 0)
		return ret;

	return 0;
}

void __exit crc_drv_exit(void)
{
	platform_driver_unregister(&crc_driver);
}
//module_init(crc_drv_init);
//module_exit(crc_drv_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("huybui2 <huy.bui.wm@renesas.com>");
MODULE_DESCRIPTION("R-Car Cyclic Redundancy Check Wrapper");

