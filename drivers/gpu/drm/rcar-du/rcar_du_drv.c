// SPDX-License-Identifier: GPL-2.0+
/*
 * rcar_du_drv.c  --  R-Car Display Unit DRM driver
 *
 * Copyright (C) 2013-2018 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
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

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_kms.h"
#include "rcar_du_of.h"
#include "rcar_du_regs.h"
#include "rcar_du_vsp.h"

/* -----------------------------------------------------------------------------
 * Device Information
 */

static const struct rcar_du_device_info rzg1_du_r8a7743_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A774[34] has one RGB output and one LVDS output
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

static const struct rcar_du_device_info rzg1_du_r8a7745_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A7745 has two RGB outputs
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
};

static const struct rcar_du_device_info rzg1_du_r8a77470_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A77470 has two RGB outputs, one LVDS output, and
		 * one (currently unsupported) analog video output
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(0),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_DPAD1] = {
			.possible_crtcs = BIT(1),
			.port = 1,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0) | BIT(1),
			.port = 2,
		},
	},
};

static const struct rcar_du_device_info rcar_du_r8a774a1_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(2) | BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A774A1 has one RGB output, one LVDS output and one HDMI
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
	.dpll_mask =  BIT(1),
};

static const struct rcar_du_device_info rcar_du_r8a774b1_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(3) | BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A774B1 has one RGB output, one LVDS output and one HDMI
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
	.dpll_mask =  BIT(1),
};

static const struct rcar_du_device_info rcar_du_r8a774c0_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_VSP1_SOURCE,
	.channels_mask = BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A774C0 has one RGB output and two LVDS outputs
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
	.lvds_clk_mask =  BIT(1) | BIT(0),
};

static const struct rcar_du_device_info rcar_du_r8a774e1_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(3) | BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A774E1 has one RGB output, one LVDS output and one HDMI
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
	.dpll_mask =  BIT(1),
};

static const struct rcar_du_device_info rcar_du_r8a7779_info = {
	.gen = 1,
	.features = RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(1) | BIT(0),
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
};

static const struct rcar_du_device_info rcar_du_r8a7790_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.quirks = RCAR_DU_QUIRK_ALIGN_128B,
	.channels_mask = BIT(2) | BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A7742 and R8A7790 each have one RGB output and two LVDS
		 * outputs. Additionally R8A7790 supports one TCON output
		 * (currently unsupported by the driver).
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
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(1) | BIT(0),
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
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(1) | BIT(0),
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
};

static const struct rcar_du_device_info rcar_du_r8a7794_info = {
	.gen = 2,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(1) | BIT(0),
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
};

static const struct rcar_du_device_info rcar_du_r8a7795_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC
		  | RCAR_DU_FEATURE_R8A7795_REGS,
	.channels_mask = BIT(3) | BIT(2) | BIT(1) | BIT(0),
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
	.dpll_mask =  BIT(2) | BIT(1),
};

static const struct rcar_du_device_info rcar_du_r8a7796_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC
		  | RCAR_DU_FEATURE_R8A7796_REGS,
	.channels_mask = BIT(2) | BIT(1) | BIT(0),
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
	.dpll_mask =  BIT(1),
};

static const struct rcar_du_device_info rcar_du_r8a77965_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC
		  | RCAR_DU_FEATURE_R8A77965_REGS,
	.channels_mask = BIT(3) | BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A77965 has one RGB output, one LVDS output and one HDMI
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
	.dpll_mask =  BIT(1),
};

static const struct rcar_du_device_info rcar_du_r8a77970_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_INTERLACED
		  | RCAR_DU_FEATURE_TVM_SYNC,
	.channels_mask = BIT(0),
	.routes = {
		/*
		 * R8A77970 and R8A77980 have one RGB output and one LVDS
		 * output.
		 */
		[RCAR_DU_OUTPUT_DPAD0] = {
			.possible_crtcs = BIT(0),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_LVDS0] = {
			.possible_crtcs = BIT(0),
			.port = 1,
		},
	},
	.num_lvds = 1,
};

static const struct rcar_du_device_info rcar_du_r8a7799x_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_VSP1_SOURCE,
	.channels_mask = BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A77990 and R8A77995 have one RGB output and two LVDS
		 * outputs.
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
	.lvds_clk_mask =  BIT(1) | BIT(0),
};

static const struct rcar_du_device_info rcar_du_r8a779a0_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_R8A779A0_REGS,
	.channels_mask = BIT(1) | BIT(0),
	.routes = {
		/*
		 * R8A779A0 has two MIPI DSI output.
		 */
		[RCAR_DU_OUTPUT_MIPI_DSI0] = {
			.possible_crtcs = BIT(0),
			.port = 0,
		},
		[RCAR_DU_OUTPUT_MIPI_DSI1] = {
			.possible_crtcs = BIT(1),
			.port = 1,
		},
	},
	.num_mipi_dsi = 2,
	.mipi_dsi_clk_mask = BIT(1) | BIT(0),
};

static const struct of_device_id rcar_du_of_table[] = {
	{ .compatible = "renesas,du-r8a7742", .data = &rcar_du_r8a7790_info },
	{ .compatible = "renesas,du-r8a7743", .data = &rzg1_du_r8a7743_info },
	{ .compatible = "renesas,du-r8a7744", .data = &rzg1_du_r8a7743_info },
	{ .compatible = "renesas,du-r8a7745", .data = &rzg1_du_r8a7745_info },
	{ .compatible = "renesas,du-r8a77470", .data = &rzg1_du_r8a77470_info },
	{ .compatible = "renesas,du-r8a774a1", .data = &rcar_du_r8a774a1_info },
	{ .compatible = "renesas,du-r8a774b1", .data = &rcar_du_r8a774b1_info },
	{ .compatible = "renesas,du-r8a774c0", .data = &rcar_du_r8a774c0_info },
	{ .compatible = "renesas,du-r8a774e1", .data = &rcar_du_r8a774e1_info },
	{ .compatible = "renesas,du-r8a7779", .data = &rcar_du_r8a7779_info },
	{ .compatible = "renesas,du-r8a7790", .data = &rcar_du_r8a7790_info },
	{ .compatible = "renesas,du-r8a7791", .data = &rcar_du_r8a7791_info },
	{ .compatible = "renesas,du-r8a7792", .data = &rcar_du_r8a7792_info },
	{ .compatible = "renesas,du-r8a7793", .data = &rcar_du_r8a7791_info },
	{ .compatible = "renesas,du-r8a7794", .data = &rcar_du_r8a7794_info },
	{ .compatible = "renesas,du-r8a7795", .data = &rcar_du_r8a7795_info },
	{ .compatible = "renesas,du-r8a7796", .data = &rcar_du_r8a7796_info },
	{ .compatible = "renesas,du-r8a77961", .data = &rcar_du_r8a7796_info },
	{ .compatible = "renesas,du-r8a77965", .data = &rcar_du_r8a77965_info },
	{ .compatible = "renesas,du-r8a77970", .data = &rcar_du_r8a77970_info },
	{ .compatible = "renesas,du-r8a77980", .data = &rcar_du_r8a77970_info },
	{ .compatible = "renesas,du-r8a77990", .data = &rcar_du_r8a7799x_info },
	{ .compatible = "renesas,du-r8a77995", .data = &rcar_du_r8a7799x_info },
	{ .compatible = "renesas,du-r8a779a0", .data = &rcar_du_r8a779a0_info },
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_du_of_table);

/* -----------------------------------------------------------------------------
 * DRM operations
 */

static const struct drm_ioctl_desc rcar_du_ioctls[] = {
	DRM_IOCTL_DEF_DRV(RCAR_DU_SET_VMUTE, rcar_du_set_vmute,
			  DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(RCAR_DU_SCRSHOT, rcar_du_vsp_write_back,
			  DRM_UNLOCKED),
};

DEFINE_DRM_GEM_CMA_FOPS(rcar_du_fops);

static struct drm_driver rcar_du_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &rcar_du_fops,
	.name			= "rcar-du",
	.desc			= "Renesas R-Car Display Unit",
	.date			= "20130110",
	.major			= 1,
	.minor			= 0,
	.ioctls			= rcar_du_ioctls,
	.num_ioctls		= ARRAY_SIZE(rcar_du_ioctls),
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
};

/* -----------------------------------------------------------------------------
 * Power management
 */

#ifdef CONFIG_PM_SLEEP
static int rcar_du_pm_suspend(struct device *dev)
{
	struct rcar_du_device *rcdu = dev_get_drvdata(dev);
#if IS_ENABLED(CONFIG_DRM_RCAR_DW_HDMI)
	struct drm_encoder *encoder;
#endif

#if IS_ENABLED(CONFIG_DRM_RCAR_DW_HDMI)
	list_for_each_entry(encoder,
			    &rcdu->ddev->mode_config.encoder_list,
			    head) {
		struct rcar_du_encoder *renc = to_rcar_encoder(encoder);

		if (renc->bridge && (renc->output == RCAR_DU_OUTPUT_HDMI0 ||
		    renc->output == RCAR_DU_OUTPUT_HDMI1))
			dw_hdmi_s2r_ctrl(renc->bridge, false);
	}
#endif
	return drm_mode_config_helper_suspend(rcdu->ddev);
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
			dw_hdmi_s2r_ctrl(renc->bridge, true);
	}
#endif

#if IS_ENABLED(CONFIG_DRM_I2C_ADV7511)
	drm_helper_hpd_irq_event(rcdu->ddev);
#endif
	return drm_mode_config_helper_resume(rcdu->ddev);
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

	if (rcdu->mode_config_initialized)
		drm_atomic_helper_shutdown(ddev);

	drm_dev_unregister(ddev);

	drm_kms_helper_poll_fini(ddev);

	drm_dev_put(ddev);

	return 0;
}

static int rcar_du_probe(struct platform_device *pdev)
{
	struct rcar_du_device *rcdu;
	struct drm_device *ddev;
	struct resource *mem;
	unsigned int mask;
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

	/*
	 * Set the DMA coherent mask to reflect the DU 32-bit DMA address space
	 * limitations. When sourcing frames from a VSP the DU doesn't perform
	 * any memory access so set the mask to 40 bits to accept all buffers.
	 */
	mask = rcar_du_has(rcdu, RCAR_DU_FEATURE_VSP1_SOURCE) ? 40 : 32;
	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(mask));
	if (ret)
		return ret;

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

	drm_fbdev_generic_setup(ddev, 32);

	rcdu->mode_config_initialized = true;

	return 0;

error:
	rcar_du_remove(pdev);

	return ret;
}

static void rcar_du_shutdown(struct platform_device *pdev)
{
#ifdef CONFIG_PM_SLEEP
	rcar_du_pm_suspend(&pdev->dev);
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

static int __init rcar_du_init(void)
{
	rcar_du_of_init(rcar_du_of_table);

	return platform_driver_register(&rcar_du_platform_driver);
}
module_init(rcar_du_init);

static void __exit rcar_du_exit(void)
{
	platform_driver_unregister(&rcar_du_platform_driver);
}
module_exit(rcar_du_exit);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas R-Car Display Unit DRM Driver");
MODULE_LICENSE("GPL");
