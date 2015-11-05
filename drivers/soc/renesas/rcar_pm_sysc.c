/*
 * Renesas R-Car Power Domains Control driver
 * /drivers/soc/renesas/rcar_pm_sysc.c
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 * Based on: arch/arm/mach-shmobile/pm-rcar.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Gereral Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include <linux/bitops.h>
#include <linux/clk/shmobile.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_clock.h>
#include <linux/slab.h>
#include <linux/soc/renesas/rcar_pm_sysc.h>
#include <linux/spinlock.h>
#include <linux/string.h>

/* Define common registers for SYSC */
#define SYSCSR		0x0000  /* SYSC Status Register */

/* Bits */
#define POFFENB		((u32)BIT(0)) /* bit 0 */
#define PONENB		((u32)BIT(1)) /* bit 1 */

#define SYSCISR		0x0004	/* Interrupt Status Register */
#define SYSCISCR	0x0008	/* Interrupt Status Clear Register */
#define SYSCIER		0x000C	/* Interrupt Enable Register */
#define SYSCIMR		0x0010	/* Interrupt Mask Register */

/*
 * Define offset for registers in each power domains (A3IR, A3VP,
 * A3VC, A2VC1/0). Base addresses are defined in device node
 */
#define PWRSR		0x0000	/* Power Status Register */
#define PWROFFCR	0x0004	/* Power Shutoff Control Register */
#define PWROFFSR	0x0008	/* Power Shutoff Status Register */
#define PWRONCR		0x000C	/* Power Resume Control Register */
#define PWRONSR		0x0010	/* Power Resume Status Register */
#define PWRER		0x0014	/* Power Shutoff/Resume Error Register */
#define PWRPSEU		0x0038	/* Power pseudo shutoff register */

/* Bits for control 3DG power domains */
#define BITS_0_4	((u32)(BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(4)))

#define SYSCSR_RETRIES		1000
#define SYSCSR_DELAY_US		10

#define SYSCISR_RETRIES		1000
#define SYSCISR_DELAY_US	10

/* Read/write functions */
#define write_reg32	iowrite32	/* (value, address) */
#define read_reg32	ioread32	/* (address) */

#define RCAR_PWD_DEBUG_ENABLE	0

#if RCAR_PWD_DEBUG_ENABLE
#define PRINT_DOMAIN_REGS(pd, t, log) RCAR_SYSC_PWD_REGISTERS(pd, t, log)
#define PRINT_COMMON_REGS(pd, t, log) RCAR_SYSC_CMN_REGISTERS(pd, t, log)
#else
#define PRINT_DOMAIN_REGS(pd, t, log)
#define PRINT_COMMON_REGS(pd, t, log)
#endif

struct rcar_sysc_domain_data {
	const char *name;
	u32 syscisrs;		/* Bit on interrupt regs */
	u32 pwrsr_on;		/* PWRSR.ON bits */
	union {
	  u32 pwrsr_off;	/* PWRSR.OFF bits */
	  u32 pwr_on_off_sr;	/* PWRONSR and PWROFFSR */
	  u32 pwr_on_off_cr;	/* PWRONCR and PWROFFCR */
	  u32 pwrer;		/* PWRER */
	};
};

static const struct rcar_sysc_domain_data power_domains_sysc[] = {
	{
		.name		= "a3sg",
		.syscisrs	= 0x005E0000, /* bits 17,18,19,20,22 */
		.pwrsr_on	= (u32)(BITS_0_4 << 5),
		.pwrsr_off	= (u32)BITS_0_4,
		.pwr_on_off_sr	= (u32)BITS_0_4,
		.pwr_on_off_cr	= (u32)BITS_0_4,
		.pwrer		= (u32)BITS_0_4,
	},
	{
		.name		= "a3ir",
		.syscisrs	= 0x01000000, /* bit 24 */
		.pwrsr_on	= (u32)BIT(4),
		.pwrsr_off	= (u32)BIT(0),
		.pwr_on_off_sr	= (u32)BIT(0),
		.pwr_on_off_cr	= (u32)BIT(0),
		.pwrer		= (u32)BIT(0),
	},
	{
		.name		= "a3vp",
		.syscisrs	= 0x00000200, /* bit 9 */
		.pwrsr_on	= (u32)BIT(4),
		.pwrsr_off	= (u32)BIT(0),
		.pwr_on_off_sr	= (u32)BIT(0),
		.pwr_on_off_cr	= (u32)BIT(0),
		.pwrer		= (u32)BIT(0),
	},
	{
		.name		= "a3vc",
		.syscisrs	= 0x00004000, /* bit 14 */
		.pwrsr_on	= (u32)BIT(4),
		.pwrsr_off	= (u32)BIT(0),
		.pwr_on_off_sr	= (u32)BIT(0),
		.pwr_on_off_cr	= (u32)BIT(0),
		.pwrer		= (u32)BIT(0),
	},
	{
		.name		= "a2vc0",
		.syscisrs	= 0x02000000, /* bit 25 */
		.pwrsr_on	= (u32)BIT(2),
		.pwrsr_off	= (u32)BIT(0),
		.pwr_on_off_sr	= (u32)BIT(0),
		.pwr_on_off_cr	= (u32)BIT(0),
		.pwrer		= (u32)BIT(0),
	},
	{
		.name		= "a2vc1",
		.syscisrs	= 0x04000000, /* bit 26 */
		.pwrsr_on	= (u32)BIT(3),
		.pwrsr_off	= (u32)BIT(1),
		.pwr_on_off_sr	= (u32)BIT(1),
		.pwr_on_off_cr	= (u32)BIT(1),
		.pwrer		= (u32)BIT(1),
	},
};

static void __iomem *sysc_base; /* sysc base address */
/* Lock to protect power domains, avoid simultaneously access */
static DEFINE_SPINLOCK(sysc_lock);
static unsigned long sysc_lock_flags;

static inline
struct rcar_sysc_domain *to_rcar_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct rcar_sysc_domain, genpd);
}

int set_dm_on_off(struct rcar_sysc_domain *pd, int flag)
{
	const struct rcar_sysc_domain_data *dm_data = pd->dm_data;
	void __iomem *pwr_onoff_sr;
	void __iomem *pwr_onoff_cr;
	u32 pwrsr;

	if (flag == 1) { /* set on */
		pwr_onoff_sr = pd->base + PWROFFSR;
		pwr_onoff_cr = pd->base + PWRONCR;
		pwrsr = dm_data->pwrsr_off;
	} else { /* set off */
		pwr_onoff_sr = pd->base + PWRONSR;
		pwr_onoff_cr = pd->base + PWROFFCR;
		pwrsr = dm_data->pwrsr_on;
	}
	/* Check power domain is resuming (DWNSTATE/UPSTATE bits) */
	if (dm_data->pwr_on_off_sr & read_reg32(pwr_onoff_sr))
		return -EAGAIN;

	/* Check the power domain is ON/OFF before set OFF/ON */
	if ((pwrsr & read_reg32(pd->base + PWRSR)) == pwrsr) {
		/* set 1 to PWRUP/PWRDWN bit(s) of PWRONCR/PWROFFCR regs */
		write_reg32(dm_data->pwr_on_off_cr, pwr_onoff_cr);
		/* Check err register, either shutoff/resume was not accepted */
		if (read_reg32(pd->base + PWRER) & dm_data->pwrer)
			return -EAGAIN;
	} else {
		return 1; /* Power domain is ready shuted-off */
	}
		return 0;
}

/*
 * Input:
 *     genpd: generic power domain to turn on/off power
 *     flag:    1 -> turn on power
 *              0 -> turn off power
 */
int rcar_set_power_on_off(struct generic_pm_domain *genpd, int flag)
{
	/* Get appropriate platform power domain */
	struct rcar_sysc_domain *pd = to_rcar_pd(genpd);
	int ret = -ETIMEDOUT, i = 0;
	u32	syscsr_pon_off_enb;
	char *on_off = "on/off";

	if (flag == 1) { /* turn on power */
		syscsr_pon_off_enb = PONENB;
		on_off = "on";
	} else {
		syscsr_pon_off_enb = POFFENB;
		on_off = "off";
	}

	if (pd->dm_data) {
		/* lock power domain to avoid simultaneously turn on/off */
		PRINT_DOMAIN_REGS(pd, "before set", on_off);
		spin_lock_irqsave(&sysc_lock, sysc_lock_flags);

		for (i = 0; i < SYSCSR_RETRIES; i++) {
			/* Check SYSC resume/shutoff is ready to start */
			if (read_reg32(sysc_base + SYSCSR) &
						syscsr_pon_off_enb) {

				ret = set_dm_on_off(pd, flag);
				if (!ret || 1 == ret)
					break;
			}
			udelay(SYSCISR_DELAY_US);
		}

		spin_unlock_irqrestore(&sysc_lock, sysc_lock_flags);

		if (ret == 1) { /* already on/off */
			pr_debug("%s: %s: has already %s\n",
						__func__, genpd->name, on_off);
			return 0;
		} else if (i == SYSCSR_RETRIES) { /* error */
			pr_debug("%s: %s: turn %s, return timeout %d\n",
					__func__, genpd->name, on_off, ret);
			return ret;
		}

		/* Wait until the power shutoff/resume request has completed */
		for (i = 0; i < SYSCISR_RETRIES; i++) {
			if (pd->dm_data->syscisrs == (pd->dm_data->syscisrs &
					read_reg32(sysc_base + SYSCISR)))
				break;
			udelay(SYSCISR_DELAY_US);
		}

		if (i == SYSCISR_RETRIES) {
			ret = -EIO;
			pr_debug("WARNING: %s, pd: %s, %d (us)\n",
				__func__, genpd->name, i*SYSCISR_DELAY_US);
		}

		PRINT_DOMAIN_REGS(pd, "after set", on_off);
		PRINT_COMMON_REGS(pd, "before clear interrupt", on_off);
		/* Clear interrupt */
		write_reg32(pd->dm_data->syscisrs, sysc_base + SYSCISCR);
		PRINT_COMMON_REGS(pd, "after clear interrupt", on_off);
	}
	return ret;
}

int rcar_power_off(struct generic_pm_domain *genpd)
{
	return rcar_set_power_on_off(genpd, 0);
}

int rcar_power_on(struct generic_pm_domain *genpd)
{
	return rcar_set_power_on_off(genpd, 1);
}

static int __init rcar_setup_pm_domain(struct device_node *np,
					struct rcar_sysc_domain *pd)
{
	int i, len;

	if (!pd || !np) {
		pr_debug("%s: invalid arguments\n", __func__);
		return -EINVAL;
	}

	if (!strcmp(np->name, "always_on")) {
		pd->dm_data = NULL;
	} else {
		len = ARRAY_SIZE(power_domains_sysc);
		for (i = 0; i < len; i++) {
			if (!strcmp(power_domains_sysc[i].name, np->name)) {
				pd->dm_data = power_domains_sysc + i;
				break;
			}
		}

		/* Enable interupts for power domains */
		write_reg32(pd->dm_data->syscisrs |
			read_reg32(sysc_base + SYSCIER), sysc_base + SYSCIER);

		/* Mask interrupt factor of power domain*/
		write_reg32(pd->dm_data->syscisrs |
			read_reg32(sysc_base + SYSCIMR), sysc_base + SYSCIMR);
	}

	pd->genpd.name = np->name;

	pd->genpd.flags = GENPD_FLAG_PM_CLK;
	pm_genpd_init(&pd->genpd, &simple_qos_governor, false);
	pd->genpd.attach_dev = cpg_mstp_attach_dev;
	pd->genpd.detach_dev = cpg_mstp_detach_dev;

	pd->genpd.power_off = rcar_power_off;
	pd->genpd.power_on  = rcar_power_on;

	of_genpd_add_provider_simple(np, &pd->genpd);

	return 0;
}

static int __init rcar_add_pm_domains(void __iomem *sysc_base,
					 struct device_node *parent,
					 struct generic_pm_domain *genpd_parent)
{
	struct device_node *np;
	int ret = 0;

	for_each_child_of_node(parent, np) {
		struct rcar_sysc_domain *pd;
		u32 pd_offset = 0;

		of_property_read_u32(np, "reg", &pd_offset);

		pd = kzalloc(sizeof(*pd), GFP_KERNEL);
		if (!pd)
			return -ENOMEM;

		/* Setup for a power domain */
		/* SYSC sysc_base+ offset of current power domain */
		pd->base = sysc_base + pd_offset;

		ret = rcar_setup_pm_domain(np, pd);
		if (ret < 0)
			return ret;
#if RCAR_PWD_DEBUG_ENABLE
		rcar_power_on(&pd->genpd); /* Enable to confirm hardware */
#endif
		if (genpd_parent)
			pm_genpd_add_subdomain(genpd_parent, &pd->genpd);
		/* Recursive to setup for child-power domains */
		rcar_add_pm_domains(sysc_base, np, &pd->genpd);
	}

	return ret;
}

static int __init rcar_sysc_domains_init(void)
{
	struct device_node *np, *pmd;
	int ret = 0;

	sysc_base = NULL;

	/* Parse sysc node */
	for_each_compatible_node(np, NULL, "renesas,sysc-rcar") {
		sysc_base = of_iomap(np, 0);

		if (!sysc_base) {
			pr_warn("%s cannot map reg 0\n", np->full_name);
			continue;
		}

		pmd = of_get_child_by_name(np, "pm-domains");
		if (!pmd) {
			pr_warn("%s lacks pm-domains node\n", np->full_name);
			continue;
		}

		ret = rcar_add_pm_domains(sysc_base, pmd, NULL);
		of_node_put(pmd);
		if (ret) {
			of_node_put(np);
			break;
		}
	}

	return ret;
}

core_initcall(rcar_sysc_domains_init);

MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_DESCRIPTION("Renesas R-Car Power Domain Control driver");
MODULE_LICENSE("GPL v2");
