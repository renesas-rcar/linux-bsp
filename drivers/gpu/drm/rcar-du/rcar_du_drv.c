/*
 * rcar_du_drv.c  --  R-Car Display Unit DRM driver
 *
 * Copyright (C) 2013-2017 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/rcar_du_drm.h>

#include <media/vsp1.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_kms.h"
#include "rcar_du_regs.h"
#include "rcar_du_vsp.h"

/* -----------------------------------------------------------------------------
 * Device Information
 */

static const struct rcar_du_device_info rcar_du_r8a7779_info = {
	.gen = 2,
	.features = 0,
	.num_crtcs = 2,
	.routes = {
		/*
		 * R8A7779 has two RGB outputs and one (currently unsupported)
		 * TCON output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(0),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_DPAD1] = {
			.possible_crtcs = BIT(1) | BIT(0),
			.port = 1,
		},
	},
	.num_lvds = 0,
};

static const struct rcar_du_device_info rcar_du_r8a7790_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS,
	.quirks = RCAR_DU_QUIRK_ALIGN_128B | RCAR_DU_QUIRK_LVDS_LANES,
	.num_crtcs = 3,
	.routes = {
		/*
		 * R8A7790 has one RGB output, two LVDS outputs and one
		 * (currently unsupported) TCON output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(2) | BIT(1) | BIT(0),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.port = 1,
		},
		[RCAR_DU_OUTPUT_LVDS1] = {
			.possible_crtcs = BIT(2) | BIT(1),
			.port = 2,
		},
	},
	.num_lvds = 2,
};

/* M2-W (r8a7791) and M2-N (r8a7793) are identical */
static const struct rcar_du_device_info rcar_du_r8a7791_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS,
	.num_crtcs = 2,
	.routes = {
		/*
		 * R8A779[13] has one RGB output, one LVDS output and one
		 * (currently unsupported) TCON output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(1) | BIT(0),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.port = 1,
		},
	},
	.num_lvds = 1,
};

static const struct rcar_du_device_info rcar_du_r8a7792_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS,
	.num_crtcs = 2,
	.routes = {
		/* R8A7792 has two RGB outputs. */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(0),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_DPAD1] = {
			.possible_crtcs = BIT(1),
			.port = 1,
		},
	},
	.num_lvds = 0,
};

static const struct rcar_du_device_info rcar_du_r8a7794_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS,
	.num_crtcs = 2,
	.routes = {
		/*
		 * R8A7794 has two RGB outputs and one (currently unsupported)
		 * TCON output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(0),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_DPAD1] = {
			.possible_crtcs = BIT(1),
			.port = 1,
		},
	},
	.num_lvds = 0,
};

static const struct rcar_du_device_info rcar_du_r8a7795_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_R8A7795_REGS,
	.num_crtcs = 4,
	.routes = {
		/*
		 * R8A7795 has one RGB output, two HDMI outputs and one
		 * LVDS output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(3),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_HDMI0] = {
			.possible_crtcs = BIT(1),
			.port = 1,
		},
		[RCAR_DU_OUTPUT_HDMI1] = {
			.possible_crtcs = BIT(2),
			.port = 2,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.port = 3,
		},
	},
	.num_lvds = 1,
	.dpll_ch =  BIT(1) | BIT(2),
};

static const struct rcar_du_device_info rcar_du_r8a7796_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_R8A7796_REGS,
	.num_crtcs = 3,
	.routes = {
		/*
		 * R8A7796 has one RGB output, one LVDS output and one HDMI
		 * output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(2),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_HDMI0] = {
			.possible_crtcs = BIT(1),
			.port = 1,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.port = 2,
		},
	},
	.num_lvds = 1,
	.dpll_ch =  BIT(1),
};

static const struct rcar_du_device_info rcar_du_r8a77965_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_R8A77965_REGS,
	.num_crtcs = 3,
	.routes = {
		/* R8A77965 has one RGB output, one LVDS output and one
		 * HDMI output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(2),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_HDMI0] = {
			.possible_crtcs = BIT(1),
			.port = 1,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.port = 2,
		},
	},
	.num_lvds = 1,
	.dpll_ch =  BIT(1),
	.skip_ch = BIT(2),
};

static const struct rcar_du_device_info rcar_du_r8a77995_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_R8A77995_REGS
		  | RCAR_DU_FEATURE_LVDS_PLL,
	.num_crtcs = 2,
	.routes = {
		/* R8A77995 has two LVDS output and one RGB output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(0) | BIT(1),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.port = 1,
		},
		[RCAR_DU_OUTPUT_LVDS1] = {
			.possible_crtcs = BIT(1),
			.port = 2,
		},
	},
	.num_lvds = 2,
};

static const struct rcar_du_device_info rcar_du_r8a77990_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_R8A77990_REGS
		  | RCAR_DU_FEATURE_LVDS_PLL,
	.num_crtcs = 2,
	.routes = {
		/* R8A77990 has two LVDS output and one RGB output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(0) | BIT(1),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.port = 1,
		},
		[RCAR_DU_OUTPUT_LVDS1] = {
			.possible_crtcs = BIT(1),
			.port = 2,
		},
	},
	.num_lvds = 2,
};

static const struct of_device_id rcar_du_of_table[] = {
	{ .compatible = "renesas,du-r8a7779", .data = &rcar_du_r8a7779_info },
	{ .compatible = "renesas,du-r8a7790", .data = &rcar_du_r8a7790_info },
	{ .compatible = "renesas,du-r8a7791", .data = &rcar_du_r8a7791_info },
	{ .compatible = "renesas,du-r8a7792", .data = &rcar_du_r8a7792_info },
	{ .compatible = "renesas,du-r8a7793", .data = &rcar_du_r8a7791_info },
	{ .compatible = "renesas,du-r8a7794", .data = &rcar_du_r8a7794_info },
	{ .compatible = "renesas,du-r8a7795", .data = &rcar_du_r8a7795_info },
	{ .compatible = "renesas,du-r8a7796", .data = &rcar_du_r8a7796_info },
	{ .compatible = "renesas,du-r8a77965", .data = &rcar_du_r8a77965_info },
	{ .compatible = "renesas,du-r8a77995", .data = &rcar_du_r8a77995_info },
	{ .compatible = "renesas,du-r8a77990", .data = &rcar_du_r8a77990_info },
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_du_of_table);

/* -----------------------------------------------------------------------------
 * DRM operations
 */

static void rcar_du_lastclose(struct drm_device *dev)
{
	struct rcar_du_device *rcdu = dev->dev_private;

	drm_fbdev_cma_restore_mode(rcdu->fbdev);
}

static const struct drm_ioctl_desc rcar_du_ioctls[] = {
	DRM_IOCTL_DEF_DRV(RCAR_DU_SET_VMUTE, rcar_du_set_vmute,
			  DRM_UNLOCKED | DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(RCAR_DU_SCRSHOT, rcar_du_vsp_write_back,
			  DRM_UNLOCKED | DRM_CONTROL_ALLOW),
};

DEFINE_DRM_GEM_CMA_FOPS(rcar_du_fops);

static struct drm_driver rcar_du_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME
				| DRIVER_ATOMIC,
	.lastclose		= rcar_du_lastclose,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = rcar_du_gem_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,
	.dumb_create		= rcar_du_dumb_create,
	.fops			= &rcar_du_fops,
	.name			= "rcar-du",
	.desc			= "Renesas R-Car Display Unit",
	.date			= "20130110",
	.major			= 1,
	.minor			= 0,
	.ioctls			= rcar_du_ioctls,
	.num_ioctls		= ARRAY_SIZE(rcar_du_ioctls),
};

/* -----------------------------------------------------------------------------
 * Power management
 */

#ifdef CONFIG_PM_SLEEP
static int rcar_du_pm_shutdown(struct device *dev)
{
	struct rcar_du_device *rcdu = dev_get_drvdata(dev);
	struct drm_atomic_state *state;
#if IS_ENABLED(CONFIG_DRM_RCAR_DW_HDMI)
	struct drm_encoder *encoder;
#endif

	drm_kms_helper_poll_disable(rcdu->ddev);
	drm_fbdev_cma_set_suspend_unlocked(rcdu->fbdev, true);

	state = drm_atomic_helper_suspend(rcdu->ddev);
	if (IS_ERR(state)) {
		drm_fbdev_cma_set_suspend_unlocked(rcdu->fbdev, false);
		drm_kms_helper_poll_enable(rcdu->ddev);
		return PTR_ERR(state);
	}

#if IS_ENABLED(CONFIG_DRM_RCAR_DW_HDMI)
	list_for_each_entry(encoder,
			    &rcdu->ddev->mode_config.encoder_list,
			    head) {
		struct rcar_du_encoder *renc = to_rcar_encoder(encoder);

		if (renc->bridge && (renc->output == RCAR_DU_OUTPUT_HDMI0 ||
		    renc->output == RCAR_DU_OUTPUT_HDMI1))
			dw_hdmi_s2r_ctrl(encoder->bridge, false);
	}
#endif
	rcdu->suspend_state = state;

	return 0;
}

static int rcar_du_pm_suspend(struct device *dev)
{
	struct rcar_du_device *rcdu = dev_get_drvdata(dev);
	int i, ret;

	ret = rcar_du_pm_shutdown(dev);
	if (ret)
		return ret;

	for (i = 0; i < rcdu->num_crtcs; ++i)
		clk_set_rate(rcdu->crtcs[i].extclock, 0);

	return 0;
}

static int rcar_du_pm_resume(struct device *dev)
{
	struct rcar_du_device *rcdu = dev_get_drvdata(dev);
#if IS_ENABLED(CONFIG_DRM_RCAR_DW_HDMI)
	struct drm_encoder *encoder;

	list_for_each_entry(encoder,
			    &rcdu->ddev->mode_config.encoder_list,
			    head) {
		struct rcar_du_encoder *renc = to_rcar_encoder(encoder);

		if (renc->bridge && (renc->output == RCAR_DU_OUTPUT_HDMI0 ||
		    renc->output == RCAR_DU_OUTPUT_HDMI1))
			dw_hdmi_s2r_ctrl(encoder->bridge, true);
	}
#endif
	drm_atomic_helper_resume(rcdu->ddev, rcdu->suspend_state);
	drm_fbdev_cma_set_suspend_unlocked(rcdu->fbdev, false);
	drm_kms_helper_poll_enable(rcdu->ddev);

	return 0;
}
#endif

static const struct dev_pm_ops rcar_du_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rcar_du_pm_suspend, rcar_du_pm_resume)
};

/* -----------------------------------------------------------------------------
 * Platform driver
 */

static int rcar_du_remove(struct platform_device *pdev)
{
	struct rcar_du_device *rcdu = platform_get_drvdata(pdev);
	struct drm_device *ddev = rcdu->ddev;

	drm_dev_unregister(ddev);

	if (rcdu->fbdev)
		drm_fbdev_cma_fini(rcdu->fbdev);

	drm_kms_helper_poll_fini(ddev);
	drm_mode_config_cleanup(ddev);

	drm_dev_unref(ddev);

	return 0;
}

static int rcar_du_probe(struct platform_device *pdev)
{
	struct rcar_du_device *rcdu;
	struct drm_device *ddev;
	struct resource *mem;
	int ret;

	/* Allocate and initialize the R-Car device structure. */
	rcdu = devm_kzalloc(&pdev->dev, sizeof(*rcdu), GFP_KERNEL);
	if (rcdu == NULL)
		return -ENOMEM;

	rcdu->dev = &pdev->dev;
	rcdu->info = of_device_get_match_data(rcdu->dev);

	platform_set_drvdata(pdev, rcdu);

	/* I/O resources */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rcdu->mmio = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(rcdu->mmio))
		return PTR_ERR(rcdu->mmio);

	/* DRM/KMS objects */
	ddev = drm_dev_alloc(&rcar_du_driver, &pdev->dev);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	rcdu->ddev = ddev;
	ddev->dev_private = rcdu;

	ret = rcar_du_modeset_init(rcdu);
	if (ret < 0) {
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

	return 0;

error:
	rcar_du_remove(pdev);

	return ret;
}

static void rcar_du_shutdown(struct platform_device *pdev)
{
#ifdef CONFIG_PM_SLEEP
	rcar_du_pm_shutdown(&pdev->dev);
#endif
}
static struct platform_driver rcar_du_platform_driver = {
	.probe		= rcar_du_probe,
	.remove		= rcar_du_remove,
	.driver		= {
		.name	= "rcar-du",
		.pm	= &rcar_du_pm_ops,
		.of_match_table = rcar_du_of_table,
	},
	.shutdown       = rcar_du_shutdown,
};

module_platform_driver(rcar_du_platform_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas R-Car Display Unit DRM Driver");
MODULE_LICENSE("GPL");
