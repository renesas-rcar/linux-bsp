/*
 * Renesas SoC Identification
 *
 * Copyright (C) 2014-2016 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sys_soc.h>


#define CCCR	0xe600101c	/* Common Chip Code Register */
#define PRR	0xff000044	/* Product Register */
#define PRR3	0xfff00044	/* Product Register on R-Car Gen3 */

static const struct of_device_id renesas_socs[] __initconst = {
#ifdef CONFIG_ARCH_R8A73A4
	{ .compatible = "renesas,r8a73a4",	.data = (void *)PRR, },
#endif
#ifdef CONFIG_ARCH_R8A7740
	{ .compatible = "renesas,r8a7740",	.data = (void *)CCCR, },
#endif
#ifdef CONFIG_ARCH_R8A7743
	{ .compatible = "renesas,r8a7743",	.data = (void *)PRR, },
#endif
#ifdef CONFIG_ARCH_R8A7745
	{ .compatible = "renesas,r8a7745",	.data = (void *)PRR, },
#endif
#ifdef CONFIG_ARCH_R8A7779
	{ .compatible = "renesas,r8a7779",	.data = (void *)PRR, },
#endif
#ifdef CONFIG_ARCH_R8A7790
	{ .compatible = "renesas,r8a7790",	.data = (void *)PRR, },
#endif
#ifdef CONFIG_ARCH_R8A7791
	{ .compatible = "renesas,r8a7791",	.data = (void *)PRR, },
#endif
#ifdef CONFIG_ARCH_R8A7792
	{ .compatible = "renesas,r8a7792",	.data = (void *)PRR, },
#endif
#ifdef CONFIG_ARCH_R8A7793
	{ .compatible = "renesas,r8a7793",	.data = (void *)PRR, },
#endif
#ifdef CONFIG_ARCH_R8A7794
	{ .compatible = "renesas,r8a7794",	.data = (void *)PRR, },
#endif
#ifdef CONFIG_ARCH_R8A7795
	{ .compatible = "renesas,r8a7795",	.data = (void *)PRR3, },
#endif
#ifdef CONFIG_ARCH_R8A7796
	{ .compatible = "renesas,r8a7796",	.data = (void *)PRR3, },
#endif
#ifdef CONFIG_ARCH_SH73A0
	{ .compatible = "renesas,sh73a0",	.data = (void *)CCCR, },
#endif
	{ /* sentinel */ }
};

static int __init renesas_soc_init(void)
{
	struct soc_device_attribute *soc_dev_attr;
	const struct of_device_id *match;
	void __iomem *chipid = NULL;
	struct soc_device *soc_dev;
	struct device_node *np;
	unsigned int product;

	np = of_find_matching_node_and_match(NULL, renesas_socs, &match);
	if (!np)
		return -ENODEV;

	of_node_put(np);

	/* Try PRR first, then CCCR, then hardcoded fallback */
	np = of_find_compatible_node(NULL, NULL, "renesas,prr");
	if (!np)
		np = of_find_compatible_node(NULL, NULL, "renesas,cccr");
	if (np) {
		chipid = of_iomap(np, 0);
		of_node_put(np);
	} else if (match->data) {
		chipid = ioremap((uintptr_t)match->data, 4);
	}
	if (!chipid)
		return -ENODEV;

	product = readl(chipid);
	iounmap(chipid);

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	np = of_find_node_by_path("/");
	of_property_read_string(np, "model", &soc_dev_attr->machine);
	of_node_put(np);

	soc_dev_attr->family = "Renesas";
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "ES%u.%u",
					   ((product >> 4) & 0xf) + 1,
					   product & 0xf);
	soc_dev_attr->soc_id = kstrdup_const(strchr(match->compatible, ',') + 1,
					     GFP_KERNEL);
	pr_info("Detected %s %s (0x%02x) %s\n", soc_dev_attr->family,
		soc_dev_attr->soc_id, (product >> 8) & 0xff,
		soc_dev_attr->revision);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->revision);
		kfree_const(soc_dev_attr->soc_id);
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	return 0;
}
core_initcall(renesas_soc_init);
