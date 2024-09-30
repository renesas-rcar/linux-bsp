// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe 6.0 RC driver for R-Car Gen5
 *
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors: Joao Pinto <Joao.Pinto@synopsys.com>
 */
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/gpio/consumer.h>

#include "../../pci.h"

#include "pcie6-designware.h"

static const struct of_device_id pcie6_rcar_host_of_match[];

static int pcie6_rcar_host_init(struct pcie_port *pp)
{
	struct dw_pcie6 *pci = to_dw_pcie6_from_pp(pp);

	dw_pcie6_setup_rc(pp);
	dw_pcie6_wait_for_link(pci);
	dw_pcie6_msi_init(pp);

	return 0;
}

void dw_plat_set_num_vectors(struct pcie_port *pp)
{
	pp->num_vectors = MAX_MSI_IRQS;
}

static const struct dw_pcie6_host_ops pcie6_rcar_host_ops = {
	.host_init = pcie6_rcar_host_init,
	.set_num_vectors = dw_plat_set_num_vectors,
};

static int pcie6_rcar_establish_link(struct dw_pcie6 *pci)
{
	return 0;
}

static const struct dw_pcie6_ops dw_pcie6_ops = {
	.start_link = pcie6_rcar_establish_link,
};

static int rcar_add_pcie6_port(struct dw_plat_pcie6 *dw_plat_pcie6,
				 struct platform_device *pdev)
{
	struct dw_pcie6 *pci = dw_plat_pcie6->pci;
	struct pcie_port *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int ret;

	pp->irq = platform_get_irq(pdev, 1);
	if (pp->irq < 0)
		return pp->irq;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = platform_get_irq(pdev, 0);
		if (pp->msi_irq < 0)
			return pp->msi_irq;
	}

	pp->ops = &pcie6_rcar_host_ops;

	ret = dw_pcie6_host_init(pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int pcie6_rcar_host_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_plat_pcie6 *dw_plat_pcie6;
	struct dw_pcie6 *pci;
	struct resource *res;  /* Resource from DT */
	int ret;
	const struct of_device_id *match;
	const struct dw_plat_pcie6_of_data *data;
	enum dw_pcie6_device_mode mode;

	match = of_match_device(pcie6_rcar_host_of_match, dev);
	if (!match)
		return -EINVAL;

	data = (struct dw_plat_pcie6_of_data *)match->data;
	mode = (enum dw_pcie6_device_mode)data->mode;

	dw_plat_pcie6 = devm_kzalloc(dev, sizeof(*dw_plat_pcie6), GFP_KERNEL);
	if (!dw_plat_pcie6)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &dw_pcie6_ops;

	dw_plat_pcie6->pci = pci;
	dw_plat_pcie6->mode = mode;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	if (!res)
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	pci->dbi_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	platform_set_drvdata(pdev, dw_plat_pcie6);

	switch (dw_plat_pcie6->mode) {
	case DW_PCIE_RC_TYPE:
		if (!IS_ENABLED(CONFIG_PCIE6_RCAR_HOST))
			return -ENODEV;

		ret = rcar_add_pcie6_port(dw_plat_pcie6, pdev);
		if (ret < 0)
			return ret;
		break;
	default:
		dev_err(dev, "INVALID device type %d\n", dw_plat_pcie6->mode);
	}

	return 0;
}

static const struct dw_plat_pcie6_of_data pcie6_rcar_host_rc_of_data = {
	.mode = DW_PCIE_RC_TYPE,
};

static const struct of_device_id pcie6_rcar_host_of_match[] = {
	{
		.compatible = "renesas,rcar-gen5-pcie6",
		.data = &pcie6_rcar_host_rc_of_data,
	},
	{},
};

static struct platform_driver pcie6_rcar_host_driver = {
	.driver = {
		.name	= "pcie6-rcar",
		.of_match_table = pcie6_rcar_host_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = pcie6_rcar_host_probe,
};

static int __init pcie6_rcar_init(void) {
	return platform_driver_register(&pcie6_rcar_host_driver);
}

static void __exit pcie6_rcar_exit(void) {
	platform_driver_unregister(&pcie6_rcar_host_driver);
}

module_init(pcie6_rcar_init);
module_exit(pcie6_rcar_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCIe 6.0 R-Car Gen5 Host Driver");
