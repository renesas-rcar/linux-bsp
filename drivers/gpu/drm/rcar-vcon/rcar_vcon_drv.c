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

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/rcar_du_drm.h>

#include <media/vsp1.h>

#include "rcar_vcon_drv.h"
#include "rcar_vcon_kms.h"
#include "rcar_vcon_crtc.h"
#include "rcar_vcon_vsp.h"

/* -----------------------------------------------------------------------------
 * DRM operations
 */

static const struct drm_ioctl_desc rcar_vcon_ioctls[] = {
	DRM_IOCTL_DEF_DRV(RCAR_DU_SET_VMUTE, rcar_vcon_set_vmute, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(RCAR_DU_SCRSHOT, rcar_vcon_vsp_write_back, DRM_UNLOCKED),
};

DEFINE_DRM_GEM_CMA_FOPS(rcar_vcon_fops);

static struct drm_driver rcar_vcon_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	DRM_GEM_CMA_DRIVER_OPS_WITH_DUMB_CREATE(rcar_vcon_dumb_create),
	.fops			= &rcar_vcon_fops,
	.name			= "rcar-vcon",
	.desc			= "Renesas R-Car Video Interface Converter",
	.date			= "20231103",
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
		[RCAR_VCON_OUTPUT_DP] = {
			.possible_crtcs		= BIT(0) | BIT(1),
			.possible_clones	= BIT(0),
			.port			= 0,
		},
	},
};

static const struct rcar_vcon_device_info rcar_vcon_r8a78000_group4_info = {
	.routes = {
		[RCAR_VCON_OUTPUT_DP] = {
			.possible_crtcs		= BIT(0) | BIT(1) | BIT(2) | BIT(3),
			.possible_clones	= BIT(0),
			.port			= 0,
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
	struct drm_device *ddev = rvcon->ddev;

	if (rvcon->mode_config_initialized)
		drm_atomic_helper_shutdown(ddev);

	drm_dev_unregister(ddev);

	drm_kms_helper_poll_fini(ddev);

	drm_dev_put(ddev);

	return 0;
}

static int rcar_vcon_probe(struct platform_device *pdev)
{
	struct rcar_vcon_device *rvcon;
	struct drm_device *ddev;
	int i, ret;

	/* Allocate and initialize the R-Car device structure. */
	rvcon = devm_kzalloc(&pdev->dev, sizeof(*rvcon), GFP_KERNEL);
	if (!rvcon)
		return -ENOMEM;

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

	/* DRM/KMS objects */
	ddev = drm_dev_alloc(&rcar_vcon_driver, &pdev->dev);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	rvcon->ddev = ddev;
	ddev->dev_private = rvcon;

	ret = rcar_vcon_modeset_init(rvcon);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to initialize DRM/KMS (%d)\n", ret);
		goto error;
	}

	ddev->irq_enabled = 1;

	/*
	 * Register the DRM device with the core and the connectors with
	 * sysfs.
	 */
	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto error;

	DRM_INFO("Device %s probed\n", dev_name(&pdev->dev));

	drm_fbdev_generic_setup(ddev, 32);

	rvcon->mode_config_initialized = true;

	return 0;

error:
	rcar_vcon_remove(pdev);

	return ret;
}

static void rcar_vcon_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id rcar_vcon_of_table[] = {
	{ .compatible = "renesas,r8a78000-vcon"},
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_vcon_of_table);

static struct platform_driver rcar_vcon_platform_driver = {
	.probe		= rcar_vcon_probe,
	.remove		= rcar_vcon_remove,
	.driver		= {
			.name = "rcar-vcon",
			.of_match_table = of_match_ptr(rcar_vcon_of_table),
	},
	.shutdown       = rcar_vcon_shutdown,
};
module_platform_driver(rcar_vcon_platform_driver);

MODULE_AUTHOR("Phong Hoang <phong.hoang.wz@renesas.com>");
MODULE_DESCRIPTION("Renesas R-Car Video Interface Converter DRM Driver");
MODULE_LICENSE("GPL");
