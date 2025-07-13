// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_vcon_drv.c  --  R-Car Video Interface Converter DRM driver
 *
 * Copyright (C) 2023-2024 Renesas Electronics Corporation
 *
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/rcar_vcon_drm.h>

#include <media/vsp1.h>

#include "rcar_vcon_drv.h"
#include "rcar_vcon_kms.h"
#include "rcar_vcon_crtc.h"
#include "rcar_vcon_vsp.h"

/* Hardcoded for enable module clock */
#define MDLC_BASE               0xC5000000
#define MODULE_CLK_MASK(n)       GENMASK((n) + 1, (n))
#define MODULE_CLK_SHIFT(n)      (n)

#define MDLC_PKCPROT0           (MDLC_BASE + 0x0cf0)
#define MDLC_PKCPROT1           (MDLC_BASE + 0x0cf4)

#define _MDLC_MPDG(k)           (MDLC_BASE + 0x0200 + (k) * 4)
#define _MDLC_MPDGS(k)          (MDLC_BASE + 0x0300 + (k) * 4)
#define MDLC_MPIER0             (MDLC_BASE + 0x0110)
#define MDLC_MPIMR0             (MDLC_BASE + 0x0120)

#define MDLC_MSRES(i)           (MDLC_BASE + 0x0900 + (i) * 4)
#define MDLC_MSRESS(i)		(MDLC_BASE + 0x0960 + (i) * 4)

static void rcar_vcon_module_clk_init(void)
{
	void __iomem *pll10_cr0 = ioremap(0xc6481204, 4);
	void __iomem *pll10_cr1 = ioremap(0xc6481208, 4);
	void __iomem *pll10_cr2 = ioremap(0xc648120c, 4);
	void __iomem *pll10_scr = ioremap(0xc6481318, 4);
	void __iomem *clk_dpckcr = ioremap(0xc6481010, 4);
	void __iomem *clk_vconckcr = ioremap(0xc6481014, 4);
	void __iomem *unlock = ioremap(0xc6481370, 4);
	u32 val;

	writel(0xA5A5A501, unlock);

	while (1) {
		val = readl(pll10_cr2);
		if ((val & BIT(31)) == BIT(31))
			break;
	}

	val = readl(pll10_scr);
	val |= BIT(0);
	writel(val, pll10_scr);

	while (1) {
		val = readl(pll10_scr);
		if ((val & BIT(16)) == BIT(16))
			break;
	}

	val = readl(pll10_cr2);
	val |= BIT(29);
	writel(val, pll10_cr2);

	while (1) {
		val = readl(pll10_cr2);
		if ((val & BIT(31)) == 0)
			break;
	}

	writel(0x08300000, pll10_cr0);
	writel(0x04000000, pll10_cr1);

	val = readl(pll10_cr2);
	val |= BIT(28);
	writel(val, pll10_cr2);

	while (1) {
		val = readl(pll10_cr2);
		if ((val & BIT(31)) == BIT(31))
			break;
	}

	val = readl(pll10_scr);
	val &= ~BIT(0);
	writel(val, pll10_scr);

	while (1) {
		val = readl(pll10_scr);
		if ((val & BIT(16)) == 0)
			break;
	}

	val = 0x100;
	writel(val, clk_dpckcr);

	val &= ~BIT(8);
	writel(val, clk_dpckcr);

	writel(0x1, clk_vconckcr);

	iounmap(pll10_scr);
	iounmap(clk_dpckcr);
	iounmap(clk_vconckcr);
	iounmap(unlock);
}

static void rcar_vcon_module_power_gating_set(u8 pdid, u8 mode)
{
	void __iomem *unlock = ioremap(MDLC_PKCPROT0, 4);
	void __iomem *mpdg = ioremap(_MDLC_MPDG(pdid), 4);
	void __iomem *mpdgs = ioremap(_MDLC_MPDGS(pdid), 4);
	void __iomem *mpier0 = ioremap(MDLC_MPIER0, 4);
	void __iomem *mpimr0 = ioremap(MDLC_MPIMR0, 4);

	writel(0xA5A5A501, unlock);

	if ((readl(mpdgs) & 0x3) == mode)
		goto unmap;

	while (readl(mpdgs) != readl(mpdg))
		usleep_range(1000, 1001);

	writel(0, mpier0);
	writel(0x1, mpimr0);

	writel(0x1, mpdg);

	while (readl(mpdgs) != readl(mpdg))
		usleep_range(1000, 1001);

	writel(mode, mpdg);

	while (readl(mpdgs) != readl(mpdg))
		usleep_range(1000, 1001);

unmap:
	iounmap(unlock);
	iounmap(mpdg);
	iounmap(mpdgs);
	iounmap(mpier0);
	iounmap(mpimr0);
}

static void rcar_vcon_module_standy_set(u8 clk_reg_no, u8 pos, u8 mode)
{
	void __iomem *unlock = ioremap(MDLC_PKCPROT1, 4);
	void __iomem *msress = ioremap(MDLC_MSRESS(clk_reg_no), 4);
	void __iomem *msres = ioremap(MDLC_MSRES(clk_reg_no), 4);
	u32 val;

	writel(0xA5A5A501, unlock);

	if ((readl(msress) & MODULE_CLK_MASK(pos)) == (mode << MODULE_CLK_SHIFT(pos)))
		goto unmap;

	while ((readl(msress) & MODULE_CLK_MASK(pos)) != (readl(msres) & MODULE_CLK_MASK(pos)))
		usleep_range(1000, 1001);

	val = readl(msres);
	val &= ~MODULE_CLK_MASK(pos);
	val |= mode << MODULE_CLK_SHIFT(pos);
	writel(val, msres);

	while ((readl(msress) & MODULE_CLK_MASK(pos)) != (readl(msres) & MODULE_CLK_MASK(pos)))
		usleep_range(1000, 1001);

unmap:
	iounmap(unlock);
	iounmap(msress);
	iounmap(msres);
}

static void rcar_vcon_module_power_run(void)
{
	rcar_vcon_module_clk_init();

	rcar_vcon_module_power_gating_set(7, 0x03);
	rcar_vcon_module_power_gating_set(4, 0x03);
	rcar_vcon_module_power_gating_set(5, 0x03);
	rcar_vcon_module_power_gating_set(6, 0x03);

	/* VSPD */
	rcar_vcon_module_standy_set(7, 0, 0x03);
	rcar_vcon_module_standy_set(7, 2, 0x03);
	rcar_vcon_module_standy_set(7, 4, 0x03);
	rcar_vcon_module_standy_set(7, 6, 0x03);
	rcar_vcon_module_standy_set(7, 8, 0x03);
	rcar_vcon_module_standy_set(7, 10, 0x03);
	rcar_vcon_module_standy_set(7, 12, 0x03);
	rcar_vcon_module_standy_set(7, 14, 0x03);
	rcar_vcon_module_standy_set(7, 16, 0x03);
	rcar_vcon_module_standy_set(7, 18, 0x03);

	/* FCPVD */
	rcar_vcon_module_standy_set(7, 28, 0x03);
	rcar_vcon_module_standy_set(7, 30, 0x03);
	rcar_vcon_module_standy_set(8, 0, 0x03);
	rcar_vcon_module_standy_set(8, 2, 0x03);
	rcar_vcon_module_standy_set(8, 4, 0x03);
	rcar_vcon_module_standy_set(8, 6, 0x03);
	rcar_vcon_module_standy_set(8, 8, 0x03);
	rcar_vcon_module_standy_set(8, 10, 0x03);
	rcar_vcon_module_standy_set(8, 12, 0x03);
	rcar_vcon_module_standy_set(8, 14, 0x03);

	/* VCON */
	rcar_vcon_module_standy_set(14, 30, 0x03);
	rcar_vcon_module_standy_set(15, 0, 0x03);
	rcar_vcon_module_standy_set(15, 2, 0x03);
	rcar_vcon_module_standy_set(15, 4, 0x03);
	rcar_vcon_module_standy_set(15, 6, 0x03);
	rcar_vcon_module_standy_set(15, 8, 0x03);
	rcar_vcon_module_standy_set(15, 10, 0x03);
	rcar_vcon_module_standy_set(15, 12, 0x03);
	rcar_vcon_module_standy_set(15, 14, 0x03);
	rcar_vcon_module_standy_set(15, 16, 0x03);

	/* DP-TX */
	rcar_vcon_module_standy_set(6, 8, 0x03);
	rcar_vcon_module_standy_set(6, 10, 0x03);
	rcar_vcon_module_standy_set(6, 12, 0x03);
}

/* -----------------------------------------------------------------------------
 * DRM operations
 */

static const struct drm_ioctl_desc rcar_vcon_ioctls[] = {
	DRM_IOCTL_DEF_DRV(RCAR_VCON_SET_VMUTE, rcar_vcon_set_vmute, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(RCAR_VCON_SCRSHOT, rcar_vcon_vsp_write_back, DRM_UNLOCKED),
};

DEFINE_DRM_GEM_DMA_FOPS(rcar_vcon_fops);

static struct drm_driver rcar_vcon_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.dumb_create		= rcar_vcon_dumb_create,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import_sg_table = rcar_vcon_gem_prime_import_sg_table,
	.gem_prime_mmap		= drm_gem_prime_mmap,
	.fops			= &rcar_vcon_fops,
	.name			= "rcar-vcon",
	.desc			= "Renesas R-Car Video Interface Converter",
	.date			= "20231119",
	.major			= 1,
	.minor			= 0,
	.ioctls			= rcar_vcon_ioctls,
	.num_ioctls		= ARRAY_SIZE(rcar_vcon_ioctls),
};

/* -----------------------------------------------------------------------------
 * Platform driver
 */

static const struct rcar_vcon_device_info rcar_vcon_r8a78000_group2_info = {
	.routes = {
		[RCAR_VCON_OUTPUT_DPMST0] = {
			.possible_crtcs		= BIT(0),
			.possible_clones	= BIT(0),
			.port			= 0,
		},
		[RCAR_VCON_OUTPUT_DPMST1] = {
			.possible_crtcs		= BIT(1),
			.possible_clones	= BIT(1),
			.port			= 1,
		},
	},
};

static const struct rcar_vcon_device_info rcar_vcon_r8a78000_group4_info = {
	.routes = {
		[RCAR_VCON_OUTPUT_DPMST0] = {
			.possible_crtcs		= BIT(0),
			.possible_clones	= BIT(0),
			.port			= 0,
		},
		[RCAR_VCON_OUTPUT_DPMST1] = {
			.possible_crtcs		= BIT(1),
			.possible_crtcs		= BIT(1),
			.port			= 1,
		},
		[RCAR_VCON_OUTPUT_DPMST2] = {
			.possible_crtcs		= BIT(2),
			.possible_crtcs		= BIT(2),
			.port			= 2,
		},
		[RCAR_VCON_OUTPUT_DPMST3] = {
			.possible_crtcs		= BIT(3),
			.possible_crtcs		= BIT(3),
			.port			= 3,
		},
	},
};

static int rcar_vcon_parse_of(struct rcar_vcon_device *rvcon)
{
	struct device_node *np = rvcon->dev->of_node;
	int ret;

	ret = of_property_read_u32(np, "channels", &rvcon->num_crtcs);
	if (ret) {
		dev_err(rvcon->dev, "Unable to read number of channels property\n");
		return ret;
	}

	if (rvcon->num_crtcs > RCAR_VCON_MAX_CRTCS) {
		dev_err(rvcon->dev, "The 'channels' property is higher than hardware supported\n");
		return -EINVAL;
	}

	return 0;
}

static int rcar_vcon_remove(struct platform_device *pdev)
{
	struct rcar_vcon_device *rvcon = platform_get_drvdata(pdev);
	struct drm_device *ddev = &rvcon->ddev;

	drm_dev_unregister(ddev);
	drm_atomic_helper_shutdown(ddev);

	drm_kms_helper_poll_fini(ddev);

	return 0;
}

static int rcar_vcon_probe(struct platform_device *pdev)
{
	struct rcar_vcon_device *rvcon;
	int i, ret;

	rcar_vcon_module_power_run();

	/* Allocate and initialize the R-Car device structure. */
	rvcon = devm_drm_dev_alloc(&pdev->dev, &rcar_vcon_driver, struct rcar_vcon_device, ddev);
	if (IS_ERR(rvcon))
		return -PTR_ERR(rvcon);

	rvcon->dev = &pdev->dev;

	platform_set_drvdata(pdev, rvcon);

	ret = rcar_vcon_parse_of(rvcon);
	if (ret)
		return ret;

	switch (rvcon->num_crtcs) {
	case 2:
		rvcon->info = &rcar_vcon_r8a78000_group2_info;
		break;
	case 4:
		rvcon->info = &rcar_vcon_r8a78000_group4_info;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < rvcon->num_crtcs; i++) {
		rvcon->crtcs[i].addr = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(rvcon->crtcs[i].addr))
			return PTR_ERR(rvcon->crtcs[i].addr);
	}

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));
	if (ret)
		return ret;

	ret = rcar_vcon_modeset_init(rvcon);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to initialize DRM/KMS (%d)\n", ret);
		goto error;
	}

	/* Register the DRM device with the core and the connectors with sysfs. */
	ret = drm_dev_register(&rvcon->ddev, 0);
	if (ret)
		goto error;

	DRM_INFO("Device %s probed\n", dev_name(&pdev->dev));

	drm_fbdev_generic_setup(&rvcon->ddev, 32);

	return 0;

error:
	drm_kms_helper_poll_fini(&rvcon->ddev);
	return ret;
}

static void rcar_vcon_shutdown(struct platform_device *pdev)
{
	struct rcar_vcon_device *rvcon = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(&rvcon->ddev);
}

static const struct of_device_id rcar_vcon_of_table[] = {
	{ .compatible = "renesas,r8a78000-vcon"},
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_vcon_of_table);

static struct platform_driver rcar_vcon_platform_driver = {
	.probe		= rcar_vcon_probe,
	.remove		= rcar_vcon_remove,
	.shutdown       = rcar_vcon_shutdown,
	.driver		= {
			.name = "rcar-vcon",
			.of_match_table = of_match_ptr(rcar_vcon_of_table),
	},
};
module_platform_driver(rcar_vcon_platform_driver);

MODULE_AUTHOR("Phong Hoang <phong.hoang.wz@renesas.com>");
MODULE_DESCRIPTION("Renesas R-Car Video Interface Converter DRM Driver");
MODULE_LICENSE("GPL");
