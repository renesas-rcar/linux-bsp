/*
 * rcar_du_drv.c  --  R-Car Display Unit DRM driver
 *
 * Copyright (C) 2013-2016 Renesas Electronics Corporation
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
#include <linux/sys_soc.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include <media/vsp1.h>

#include <uapi/drm/rcar_du_drm.h>

#include "rcar_du_crtc.h"
#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_regs.h"
#include "rcar_du_vsp.h"
#include "rcar_du_hdmienc.h"
#include "rcar_du_lvdsenc.h"

/* -----------------------------------------------------------------------------
 * Device Information
 */

static const struct rcar_du_device_info rcar_du_r8a7779_info = {
	.gen = 2,
	.features = 0,
	.num_crtcs = 2,
	.routes = {
		/* R8A7779 has two RGB outputs and one (currently unsupported)
		 * TCON output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(0),
			.encoder_type = DRM_MODE_ENCODER_NONE,
			.port = 0,
		},
		[RCAR_DU_OUTPUT_DPAD1] = {
			.possible_crtcs = BIT(1) | BIT(0),
			.encoder_type = DRM_MODE_ENCODER_NONE,
			.port = 1,
		},
	},
	.num_lvds = 0,
	.dpll_ch =  0,
};

static const struct rcar_du_device_info rcar_du_r8a7790_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS,
	.quirks = RCAR_DU_QUIRK_ALIGN_128B | RCAR_DU_QUIRK_LVDS_LANES,
	.num_crtcs = 3,
	.routes = {
		/* R8A7790 has one RGB output, two LVDS outputs and one
		 * (currently unsupported) TCON output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(2) | BIT(1) | BIT(0),
			.encoder_type = DRM_MODE_ENCODER_NONE,
			.port = 0,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.encoder_type = DRM_MODE_ENCODER_LVDS,
			.port = 1,
		},
		[RCAR_DU_OUTPUT_LVDS1] = {
			.possible_crtcs = BIT(2) | BIT(1),
			.encoder_type = DRM_MODE_ENCODER_LVDS,
			.port = 2,
		},
	},
	.num_lvds = 2,
	.dpll_ch =  0,
};

/* M2-W (r8a7791) and M2-N (r8a7793) are identical */
static const struct rcar_du_device_info rcar_du_r8a7791_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS,
	.num_crtcs = 2,
	.routes = {
		/* R8A779[13] has one RGB output, one LVDS output and one
		 * (currently unsupported) TCON output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(1) | BIT(0),
			.encoder_type = DRM_MODE_ENCODER_NONE,
			.port = 0,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.encoder_type = DRM_MODE_ENCODER_LVDS,
			.port = 1,
		},
	},
	.num_lvds = 1,
	.dpll_ch =  0,
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
			.encoder_type = DRM_MODE_ENCODER_NONE,
			.port = 0,
		},
		[RCAR_DU_OUTPUT_DPAD1] = {
			.possible_crtcs = BIT(1),
			.encoder_type = DRM_MODE_ENCODER_NONE,
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
		/* R8A7794 has two RGB outputs and one (currently unsupported)
		 * TCON output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(0),
			.encoder_type = DRM_MODE_ENCODER_NONE,
			.port = 0,
		},
		[RCAR_DU_OUTPUT_DPAD1] = {
			.possible_crtcs = BIT(1),
			.encoder_type = DRM_MODE_ENCODER_NONE,
			.port = 1,
		},
	},
	.num_lvds = 0,
	.dpll_ch =  0,
};

static const struct rcar_du_device_info rcar_du_r8a7795_es1x_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_DIDSR2_REG
		  | RCAR_DU_FEATURE_GEN3_REGS,
	.num_crtcs = 4,
	.routes = {
		/* R8A7795 has one RGB output, two HDMI outputs and one
		 * LVDS output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(3),
			.encoder_type = DRM_MODE_ENCODER_NONE,
			.port = 0,
		},
		[RCAR_DU_OUTPUT_HDMI0] = {
			.possible_crtcs = BIT(1),
			.encoder_type = RCAR_DU_ENCODER_HDMI,
			.port = 1,
		},
		[RCAR_DU_OUTPUT_HDMI1] = {
			.possible_crtcs = BIT(2),
			.encoder_type = RCAR_DU_ENCODER_HDMI,
			.port = 2,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.encoder_type = DRM_MODE_ENCODER_LVDS,
			.port = 3,
		},
	},
	.num_lvds = 1,
	.dpll_ch =  BIT(1) | BIT(2),
};

static const struct rcar_du_device_info rcar_du_r8a7795_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_DIDSR2_REG
		  | RCAR_DU_FEATURE_VSPDL_SOURCE,
	.num_crtcs = 4,
	.routes = {
		/* R8A7795 has one RGB output, two HDMI outputs and one
		 * LVDS output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(3),
			.encoder_type = DRM_MODE_ENCODER_NONE,
			.port = 0,
		},
		[RCAR_DU_OUTPUT_HDMI0] = {
			.possible_crtcs = BIT(1),
			.encoder_type = RCAR_DU_ENCODER_HDMI,
			.port = 1,
		},
		[RCAR_DU_OUTPUT_HDMI1] = {
			.possible_crtcs = BIT(2),
			.encoder_type = RCAR_DU_ENCODER_HDMI,
			.port = 2,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.encoder_type = DRM_MODE_ENCODER_LVDS,
			.port = 3,
		},
	},
	.num_lvds = 1,
	.dpll_ch =  BIT(1) | BIT(2),
	.vspdl_pair_ch = DU_CH_3,
	.vspdl_ch = DU_CH_0,
};

static const struct rcar_du_device_info rcar_du_r8a7796_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_GEN3_REGS,
	.num_crtcs = 3,
	.routes = {
		/* R8A7796 has one RGB output, one LVDS output and one
		 * HDMI output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(2),
			.encoder_type = DRM_MODE_ENCODER_NONE,
			.port = 0,
		},
		[RCAR_DU_OUTPUT_HDMI0] = {
			.possible_crtcs = BIT(1),
			.encoder_type = RCAR_DU_ENCODER_HDMI,
			.port = 1,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.encoder_type = DRM_MODE_ENCODER_LVDS,
			.port = 2,
		},
	},
	.num_lvds = 1,
	.dpll_ch =  BIT(1),
};

static const struct soc_device_attribute ths_quirks_match[]  = {
	{ .soc_id = "r8a7795", .revision = "ES1.*",
	  .data = (void *)(RCAR_DU_DPLL_DUTY_RATE_WA
			 | RCAR_DU_DPLLCR_REG_WA
			 | RCAR_DU_VBK_CHECK_WA
			 | RCAR_DU_R8A7795_ES1X_INFO_JUDGE), },
	{ .soc_id = "r8a7795", .revision = "ES2.0",
	  .data = 0, },
	{ .soc_id = "r8a7796",
	  .data = 0, },
	{/*sentinel*/}
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
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_du_of_table);

/* -----------------------------------------------------------------------------
 * DRM operations
 */

static void rcar_du_lastclose(struct drm_device *dev)
{
	struct rcar_du_device *rcdu = dev->dev_private;

	if (dev->irq_enabled)
		drm_fbdev_cma_restore_mode(rcdu->fbdev);
}

static int rcar_du_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct rcar_du_device *rcdu = dev->dev_private;

	rcar_du_crtc_enable_vblank(&rcdu->crtcs[pipe], true);

	return 0;
}

static void rcar_du_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct rcar_du_device *rcdu = dev->dev_private;

	rcar_du_crtc_enable_vblank(&rcdu->crtcs[pipe], false);
}

static const struct drm_ioctl_desc rcar_du_ioctls[] = {
	DRM_IOCTL_DEF_DRV(RCAR_DU_SET_VMUTE, rcar_du_set_vmute,
		DRM_UNLOCKED | DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(RCAR_DU_SCRSHOT, rcar_du_vsp_write_back,
		DRM_UNLOCKED | DRM_CONTROL_ALLOW),
};

static const struct file_operations rcar_du_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= drm_compat_ioctl,
#endif
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
	.mmap		= drm_gem_cma_mmap,
};

static struct drm_driver rcar_du_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME
				| DRIVER_ATOMIC,
	.lastclose		= rcar_du_lastclose,
	.get_vblank_counter	= drm_vblank_no_hw_counter,
	.enable_vblank		= rcar_du_enable_vblank,
	.disable_vblank		= rcar_du_disable_vblank,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,
	.dumb_create		= rcar_du_dumb_create,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
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
static int rcar_du_pm_suspend(struct device *dev)
{
	struct rcar_du_device *rcdu = dev_get_drvdata(dev);
	int i;
#if IS_ENABLED(CONFIG_DRM_RCAR_HDMI)
	struct drm_encoder *encoder;
#endif

	drm_kms_helper_poll_disable(rcdu->ddev);

#if IS_ENABLED(CONFIG_DRM_RCAR_HDMI)
	list_for_each_entry(encoder,
		 &rcdu->ddev->mode_config.encoder_list, head) {
		if (encoder->encoder_type == DRM_MODE_ENCODER_TMDS)
			rcar_du_hdmienc_suspend(encoder);
	}
#endif

#if IS_ENABLED(CONFIG_DRM_RCAR_LVDS)
	for (i = 0; i < rcdu->info->num_lvds; ++i) {
		if (rcdu->lvds[i])
			rcar_du_lvdsenc_stop_suspend(rcdu->lvds[i]);
	}
#endif
	for (i = 0; i < rcdu->num_crtcs; ++i) {
		if (rcdu->crtcs[i].initialized) {
			rcar_du_crtc_suspend(&rcdu->crtcs[i]);
			clk_set_rate(rcdu->crtcs[i].extclock, 0);
			rcdu->crtcs[i].group->used_crtcs = 0;
		}
	}

	return 0;
}

static int rcar_du_pm_resume(struct device *dev)
{
	struct rcar_du_device *rcdu = dev_get_drvdata(dev);
	struct drm_encoder *encoder;
	int i;

	encoder = NULL;

	for (i = 0; i < rcdu->num_crtcs; ++i)
		rcar_du_crtc_resume(&rcdu->crtcs[i]);

#if IS_ENABLED(CONFIG_DRM_RCAR_HDMI)
	list_for_each_entry(encoder,
		&rcdu->ddev->mode_config.encoder_list, head) {
		if (encoder->encoder_type == DRM_MODE_ENCODER_TMDS)
			rcar_du_hdmienc_resume(encoder);
	}
#endif

#if IS_ENABLED(CONFIG_DRM_RCAR_LVDS)
	for (i = 0; i < rcdu->num_crtcs; ++i) {
		if (rcdu->crtcs[i].lvds_ch >= 0)
			rcar_du_lvdsenc_start(
				rcdu->lvds[rcdu->crtcs[i].lvds_ch],
				&rcdu->crtcs[i]);
	}
#endif

	for (i = 0; i < rcdu->num_crtcs; ++i) {
		struct drm_crtc *crtc = &rcdu->crtcs[i].crtc;

		if (rcdu->crtcs[i].initialized) {
			rcar_du_group_restart(rcdu->crtcs[i].group,
						&rcdu->crtcs[i]);
			rcar_du_async_commit(rcdu->ddev, crtc);
		}
	}

	drm_kms_helper_poll_enable(rcdu->ddev);
	drm_fbdev_cma_hotplug_event(rcdu->fbdev);

	return 0;
}
#endif

static const struct dev_pm_ops rcar_du_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rcar_du_pm_suspend, rcar_du_pm_resume)
};

/* -----------------------------------------------------------------------------
 * Platform driver
 */

static void rcar_du_remove_suspend(struct rcar_du_device *rcdu)
{
	int i;
#if IS_ENABLED(CONFIG_DRM_RCAR_HDMI)
	struct drm_encoder *encoder;

	list_for_each_entry(encoder,
		&rcdu->ddev->mode_config.encoder_list, head) {
		if (encoder->encoder_type == DRM_MODE_ENCODER_TMDS)
			rcar_du_hdmienc_disable(encoder);
	}
#endif

#if IS_ENABLED(CONFIG_DRM_RCAR_LVDS)
	for (i = 0; i < rcdu->info->num_lvds; ++i) {
		if (rcdu->lvds[i])
			rcar_du_lvdsenc_stop_suspend(rcdu->lvds[i]);
	}
#endif
	for (i = 0; i < rcdu->num_crtcs; ++i) {
		if (rcdu->crtcs[i].initialized)
			rcar_du_crtc_remove_suspend(&rcdu->crtcs[i]);
	}
}

static int rcar_du_remove(struct platform_device *pdev)
{
	struct rcar_du_device *rcdu = platform_get_drvdata(pdev);
	struct drm_device *ddev = rcdu->ddev;
	int i;

	ddev->irq_enabled = 0;

	for (i = 0; i < rcdu->num_crtcs; ++i) {
		if (rcdu->crtcs[i].initialized)
			drm_crtc_vblank_off(&rcdu->crtcs[i].crtc);
	}

	drm_dev_unregister(ddev);

	rcar_du_remove_suspend(rcdu);

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
	const struct soc_device_attribute *attr;

	/* Allocate and initialize the R-Car device structure. */
	rcdu = devm_kzalloc(&pdev->dev, sizeof(*rcdu), GFP_KERNEL);
	if (rcdu == NULL)
		return -ENOMEM;

	init_waitqueue_head(&rcdu->commit.wait);

	rcdu->dev = &pdev->dev;
	rcdu->info = of_match_device(rcar_du_of_table, rcdu->dev)->data;

	attr = soc_device_match(ths_quirks_match);
	if (attr)
		rcdu->ths_quirks = (uintptr_t)attr->data;

	pr_debug("%s: ths_quirks: 0x%x\n", __func__, rcdu->ths_quirks);

	if (rcdu->ths_quirks & RCAR_DU_R8A7795_ES1X_INFO_JUDGE)
		rcdu->info = &rcar_du_r8a7795_es1x_info;

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
		platform_set_drvdata(pdev, rcdu);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to initialize DRM/KMS (%d)\n", ret);
		goto error;
	}

	ddev->irq_enabled = 1;

	/* Register the DRM device with the core and the connectors with
	 * sysfs.
	 */
	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto error;

	platform_set_drvdata(pdev, rcdu);

	DRM_INFO("Device %s probed\n", dev_name(&pdev->dev));

	return 0;

error:
	rcar_du_remove(pdev);

	return ret;
}

static struct platform_driver rcar_du_platform_driver = {
	.probe		= rcar_du_probe,
	.remove		= rcar_du_remove,
	.driver		= {
		.name	= "rcar-du",
		.pm	= &rcar_du_pm_ops,
		.of_match_table = rcar_du_of_table,
	},
};

module_platform_driver(rcar_du_platform_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas R-Car Display Unit DRM Driver");
MODULE_LICENSE("GPL");
