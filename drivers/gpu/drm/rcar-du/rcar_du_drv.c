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
#include <linux/soc/renesas/s2ram_ddr_backup.h>
#include <linux/wait.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/rcar_du_drm.h>

#include <media/vsp1.h>

#include "rcar_du_crtc.h"
#include "rcar_du_encoder.h"
#include "rcar_du_hdmienc.h"
#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_lvdsenc.h"
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
	.vsp_num = 4,
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
};

static const struct rcar_du_device_info rcar_du_r8a7795_info = {
	.gen = 3,
	.features = RCAR_DU_FEATURE_CRTC_IRQ_CLOCK
		  | RCAR_DU_FEATURE_EXT_CTRL_REGS
		  | RCAR_DU_FEATURE_VSP1_SOURCE
		  | RCAR_DU_FEATURE_GEN3_REGS
		  | RCAR_DU_FEATURE_DIDSR2_REG,
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
	.vsp_num = 5,
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
		 * HDMI outputs.
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
	.vsp_num = 5,
};

static const struct of_device_id rcar_du_of_table[] = {
	{ .compatible = "renesas,du-r8a7779", .data = &rcar_du_r8a7779_info },
	{ .compatible = "renesas,du-r8a7790", .data = &rcar_du_r8a7790_info },
	{ .compatible = "renesas,du-r8a7791", .data = &rcar_du_r8a7791_info },
	{ .compatible = "renesas,du-r8a7793", .data = &rcar_du_r8a7791_info },
	{ .compatible = "renesas,du-r8a7794", .data = &rcar_du_r8a7794_info },
	{ .compatible = "renesas,du-r8a7795", .data = &rcar_du_r8a7795_info },
	{ .compatible = "renesas,du-r8a7796", .data = &rcar_du_r8a7796_info },
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_du_of_table);

#ifdef CONFIG_RCAR_DDR_BACKUP
static struct hw_register du0_ip_regs[] = {
	{"DSYSR0",    0x00000, 32, 0},
	{"DSMR0",     0x00004, 32, 0},
	{"DIER0",     0x00010, 32, 0},
	{"DPPPR0",    0x00018, 32, 0},
	{"DEFR0",     0x00020, 32, 0},
	{"DEF5R0",    0x000E0, 32, 0},
	{"DDLTR0",    0x000E4, 32, 0},
	{"DEF6R0",    0x000E8, 32, 0},
	{"DEF7R0",    0x000EC, 32, 0},
	{"DEF8R0",    0x20020, 32, 0},
	{"DOFLR0",    0x20024, 32, 0},
	{"DIDSR0",    0x20028, 32, 0},
	{"DEF10R0",   0x20038, 32, 0},
	{"DDTHCR0",   0x2003C, 32, 0},
	{"HDSR0",     0x00040, 32, 0},
	{"HDER0",     0x00044, 32, 0},
	{"VDSR0",     0x00048, 32, 0},
	{"VDER0",     0x0004C, 32, 0},
	{"HCR0",      0x00050, 32, 0},
	{"HSWR0",     0x00054, 32, 0},
	{"VCR0",      0x00058, 32, 0},
	{"VSPR0",     0x0005C, 32, 0},
	{"EQWR0",     0x00060, 32, 0},
	{"SPWR0",     0x00064, 32, 0},
	{"CLAMPSR0",  0x00070, 32, 0},
	{"CLAMPWR0",  0x00074, 32, 0},
	{"DESR0",     0x00078, 32, 0},
	{"DEWR0",     0x0007C, 32, 0},
	{"DOOR0",     0x00090, 32, 0},
	{"BPOR0",     0x00098, 32, 0},
	{"RINTOFSR0", 0x0009C, 32, 0},
	{"P1MR0",     0x00100, 32, 0},
	{"P1DSXR0",   0x00110, 32, 0},
	{"P1DSYR0",   0x00114, 32, 0},
	{"P1DPXR0",   0x00118, 32, 0},
	{"P1DPYR0",   0x0011C, 32, 0},
	{"P3MR0",     0x00300, 32, 0},
	{"P3DSXR0",   0x00310, 32, 0},
	{"P3DSYR0",   0x00314, 32, 0},
	{"P3DPXR0",   0x00318, 32, 0},
	{"P3DPYR0",   0x0031C, 32, 0},
	{"ESCR0",     0x10000, 32, 0},
	{"OTAR0",     0x10004, 32, 0},
	{"DORCR0",    0x11000, 32, 0},
	{"DPTSR0",    0x11004, 32, 0},
	{"DS0PR0",    0x11000, 32, 0},
	{"YNCR0",     0x14080, 32, 0},
	{"YNOR0",     0x14084, 32, 0},
	{"CRNOR0",    0x14088, 32, 0},
	{"CBNOR0",    0x1408C, 32, 0},
	{"RCRCR0",    0x14090, 32, 0},
	{"GCRCR0",    0x14094, 32, 0},
	{"GCBCR0",    0x14098, 32, 0},
	{"BCBCR0",    0x1409C, 32, 0},
};

static struct hw_register du1_ip_regs[] = {
	{"DSYSR1",    0x40000, 32, 0},
	{"DSMR1",     0x30004, 32, 0},
	{"DIER1",     0x30010, 32, 0},
	{"DEFR1",     0x30020, 32, 0},
	{"HDSR1",     0x30040, 32, 0},
	{"HDER1",     0x30044, 32, 0},
	{"VDSR1",     0x30048, 32, 0},
	{"VDER1",     0x3004C, 32, 0},
	{"HCR1",      0x30050, 32, 0},
	{"HSWR1",     0x30054, 32, 0},
	{"VCR1",      0x30058, 32, 0},
	{"VSPR1",     0x3005C, 32, 0},
	{"EQWR1",     0x30060, 32, 0},
	{"SPWR1",     0x30064, 32, 0},
	{"CLAMPSR1",  0x30070, 32, 0},
	{"CLAMPWR1",  0x30074, 32, 0},
	{"DESR1",     0x30078, 32, 0},
	{"DEWR1",     0x3007C, 32, 0},
	{"DOOR1",     0x30090, 32, 0},
	{"BPOR1",     0x30098, 32, 0},
	{"RINTOFSR1", 0x3009C, 32, 0},
	{"ESCR1",     0x41000, 32, 0},
	{"OTAR1",     0x41004, 32, 0},
	{"DS1PR0",    0x41024, 32, 0},
};

static struct hw_register du2_ip_regs[] = {
	{"DSYSR2",    0x40000, 32, 0},
	{"DSMR0",     0x40004, 32, 0},
	{"DIER0",     0x40010, 32, 0},
	{"DPPPR0",    0x40018, 32, 0},
	{"DEFR2",     0x40020, 32, 0},
	{"DEF5R2",    0x400E0, 32, 0},
	{"DEF6R0",    0x400E8, 32, 0},
	{"DEF7R0",    0x400EC, 32, 0},
	{"DEF8R0",    0x60020, 32, 0},
	{"DOFLR0",    0x60024, 32, 0},
	{"DIDSR2",    0x60028, 32, 0},
	{"DEF10R2",   0x60038, 32, 0},
	{"DDTHCR0",   0x6003C, 32, 0},
	{"HDSR0",     0x40040, 32, 0},
	{"HDER0",     0x40044, 32, 0},
	{"VDSR0",     0x40048, 32, 0},
	{"VDER0",     0x4004C, 32, 0},
	{"HCR0",      0x40050, 32, 0},
	{"HSWR0",     0x40054, 32, 0},
	{"VCR0",      0x40058, 32, 0},
	{"VSPR0",     0x4005C, 32, 0},
	{"EQWR0",     0x40060, 32, 0},
	{"SPWR0",     0x40064, 32, 0},
	{"CLAMPSR0",  0x40070, 32, 0},
	{"CLAMPWR0",  0x40074, 32, 0},
	{"DESR0",     0x40078, 32, 0},
	{"DEWR0",     0x4007C, 32, 0},
	{"DOOR0",     0x40090, 32, 0},
	{"BPOR0",     0x40098, 32, 0},
	{"RINTOFSR0", 0x4009C, 32, 0},
	{"P1MR2",     0x40100, 32, 0},
	{"P1DSXR2",   0x40110, 32, 0},
	{"P1DSYR2",   0x40114, 32, 0},
	{"P1DPXR2",   0x40118, 32, 0},
	{"P1DPYR2",   0x4011C, 32, 0},
	{"P3MR2",     0x40300, 32, 0},
	{"P3DSXR2",   0x40310, 32, 0},
	{"P3DSYR2",   0x40314, 32, 0},
	{"P3DPXR2",   0x40318, 32, 0},
	{"P3DPYR2",   0x4031C, 32, 0},
	{"ESCR2",     0x50000, 32, 0},
	{"OTAR0",     0x50004, 32, 0},
	{"DORCR2",    0x51000, 32, 0},
	{"DS0PR2",    0x51020, 32, 0},
	{"YNCR2",     0x54080, 32, 0},
	{"YNOR2",     0x54084, 32, 0},
	{"CRNOR2",    0x54088, 32, 0},
	{"CBNOR2",    0x5408C, 32, 0},
	{"RCRCR2",    0x54090, 32, 0},
	{"GCRCR2",    0x54094, 32, 0},
	{"GCBCR2",    0x54098, 32, 0},
	{"BCBCR2",    0x5409C, 32, 0},
};

static struct hw_register du3_ip_regs[] = {
	{"DSYR3",     0x70000, 32, 0},
	{"DSMR3",     0x70004, 32, 0},
	{"DIER3",     0x70010, 32, 0},
	{"DEFR3",     0x70020, 32, 0},
	{"HDSR3",     0x70040, 32, 0},
	{"HDER3",     0x70044, 32, 0},
	{"VDSR3",     0x70048, 32, 0},
	{"VDER3",     0x7004C, 32, 0},
	{"HCR3",      0x70050, 32, 0},
	{"HSWR3",     0x70054, 32, 0},
	{"VCR3",      0x70058, 32, 0},
	{"VSPR3",     0x7005C, 32, 0},
	{"EQWR3",     0x70060, 32, 0},
	{"SPWR3",     0x70064, 32, 0},
	{"CLAMPSR3",  0x70070, 32, 0},
	{"CLAMPWR3",  0x70074, 32, 0},
	{"DESR3",     0x70078, 32, 0},
	{"DEWR3",     0x7007C, 32, 0},
	{"DOOR3",     0x70090, 32, 0},
	{"BPOR3",     0x70098, 32, 0},
	{"RINTOFSR3", 0x7009C, 32, 0},
	{"ESCR3",     0x81000, 32, 0},
	{"OTAR3",     0x81004, 32, 0},
	{"DS1PR3",    0x81024, 32, 0},
};

static struct rcar_ip du0_ip = {
	.ip_name = "DU0",
	.reg_count = ARRAY_SIZE(du0_ip_regs),
	.ip_reg = du0_ip_regs,
};

static struct rcar_ip du1_ip = {
	.ip_name = "DU1",
	.reg_count = ARRAY_SIZE(du1_ip_regs),
	.ip_reg = du1_ip_regs,
};

static struct rcar_ip du2_ip = {
	.ip_name = "DU2",
	.reg_count = ARRAY_SIZE(du2_ip_regs),
	.ip_reg = du2_ip_regs,
};

static struct rcar_ip du3_ip = {
	.ip_name = "DU3",
	.reg_count = ARRAY_SIZE(du3_ip_regs),
	.ip_reg = du3_ip_regs,
};

static struct rcar_ip *ip_tbl[] = {
	&du0_ip,
	&du1_ip,
	&du2_ip,
	&du3_ip,
};

static int rcar_du_save_regs(struct rcar_du_device *rcdu)
{
	int ret = 0;
	int i;

	for (i = 0; i < rcdu->num_crtcs; i++)
		if (!ip_tbl[i]->virt_addr) {
			ip_tbl[i]->virt_addr = rcdu->mmio;
			ret = handle_registers(ip_tbl[i], DO_BACKUP);
			pr_debug("%s: Backup %s registers\n",
				__func__, ip_tbl[i]->ip_name);
		}

	return ret;
}

static int rcar_du_restore_regs(struct rcar_du_device *rcdu)
{
	int ret = 0;
	int i;

	for (i = 0; i < rcdu->num_crtcs; i++) {
		ret = handle_registers(ip_tbl[i], DO_RESTORE);
		pr_debug("%s: Restore %s registers\n",
			__func__, ip_tbl[i]->ip_name);
	}

	return ret;
}
#endif /* CONFIG_RCAR_DDR_BACKUP */

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
	DRM_IOCTL_DEF_DRV(DRM_RCAR_DU_SET_VMUTE, rcar_du_set_vmute,
		DRM_UNLOCKED | DRM_CONTROL_ALLOW),
	DRM_IOCTL_DEF_DRV(DRM_RCAR_DU_SCRSHOT, rcar_du_vsp_write_back,
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
	.gem_create_object	= rcar_du_create_object,
	.gem_free_object	= drm_gem_cma_free_object,
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
	int ret = 0;

#if IS_ENABLED(CONFIG_DRM_RCAR_HDMI)
	struct drm_encoder *encoder;
#endif

#ifdef CONFIG_RCAR_DDR_BACKUP
	ret = rcar_du_save_regs(rcdu);
#endif /* CONFIG_RCAR_DDR_BACKUP */

	drm_kms_helper_poll_disable(rcdu->ddev);

#if IS_ENABLED(CONFIG_DRM_RCAR_HDMI)
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
		if (rcdu->crtcs[i].started)
			rcar_du_crtc_suspend(&rcdu->crtcs[i]);
	}

	return ret;
}

static int rcar_du_pm_resume(struct device *dev)
{
	struct rcar_du_device *rcdu = dev_get_drvdata(dev);
	struct drm_encoder *encoder;
	int i;
	int ret = 0;

	encoder = NULL;

	for (i = 0; i < rcdu->num_crtcs; ++i) {
		rcar_du_crtc_resume(&rcdu->crtcs[i]);
	}

#if IS_ENABLED(CONFIG_DRM_RCAR_LVDS)
	for (i = 0; i < rcdu->num_crtcs; ++i) {
		if (rcdu->crtcs[i].lvds_ch >= 0)
			rcar_du_lvdsenc_start(
				rcdu->lvds[rcdu->crtcs[i].lvds_ch],
				&rcdu->crtcs[i]);
	}
#endif

#ifdef CONFIG_RCAR_DDR_BACKUP
	ret = rcar_du_restore_regs(rcdu);
#endif /* CONFIG_RCAR_DDR_BACKUP */

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
		if (rcdu->crtcs[i].started)
			rcar_du_crtc_remove_suspend(&rcdu->crtcs[i]);
	}
}

static int rcar_du_remove(struct platform_device *pdev)
{
	struct rcar_du_device *rcdu = platform_get_drvdata(pdev);
	struct drm_device *ddev = rcdu->ddev;
	int i;

	ddev->irq_enabled = 0;

	drm_connector_unregister_all(ddev);

	for (i = 0; i < rcdu->num_crtcs; ++i) {
		if (rcdu->crtcs[i].started)
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
	struct device_node *np = pdev->dev.of_node;
	struct rcar_du_device *rcdu;
	struct drm_device *ddev;
	struct resource *mem;
	int ret;

	if (np == NULL) {
		dev_err(&pdev->dev, "no device tree node\n");
		return -ENODEV;
	}

	/* Allocate and initialize the DRM and R-Car device structures. */
	rcdu = devm_kzalloc(&pdev->dev, sizeof(*rcdu), GFP_KERNEL);
	if (rcdu == NULL)
		return -ENOMEM;

	init_waitqueue_head(&rcdu->commit.wait);

	rcdu->dev = &pdev->dev;
	rcdu->info = of_match_device(rcar_du_of_table, rcdu->dev)->data;

	ddev = drm_dev_alloc(&rcar_du_driver, &pdev->dev);
	if (!ddev)
		return -ENOMEM;

	drm_dev_set_unique(ddev, dev_name(&pdev->dev));

	rcdu->ddev = ddev;
	ddev->dev_private = rcdu;

	/* I/O resources */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rcdu->mmio = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(rcdu->mmio)) {
		ret = PTR_ERR(rcdu->mmio);
		goto error;
	}

	/* Initialize vertical blanking interrupts handling. Start with vblank
	 * disabled for all CRTCs.
	 */
	ret = drm_vblank_init(ddev, (1 << rcdu->info->num_crtcs) - 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize vblank\n");
		goto error;
	}

	/* DRM/KMS objects */
	ret = rcar_du_modeset_init(rcdu);
	if (ret < 0) {
		platform_set_drvdata(pdev, rcdu);
		dev_err(&pdev->dev, "failed to initialize DRM/KMS (%d)\n", ret);
		goto error;
	}

	ddev->irq_enabled = 1;

	/* Register the DRM device with the core and the connectors with
	 * sysfs.
	 */
	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto error;

	ret = drm_connector_register_all(ddev);
	if (ret < 0)
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
