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

#include <dt-bindings/clock/renesas-cpg-mssr.h>
#include <linux/bitops.h>
#include <linux/clk/shmobile.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
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
#define BITS_0_1	((u32)(BIT(0)|BIT(1)))
#define BITS_17_20	((u32)(BIT(17)|BIT(18)|BIT(19)|BIT(20)))

#define SYSCSR_RETRIES		1000
#define SYSCSR_DELAY_US		10

#define SYSCISR_RETRIES		1000
#define SYSCISR_DELAY_US	10

/* Read/write functions */
#define write_reg32	iowrite32	/* (value, address) */
#define read_reg32	ioread32	/* (address) */

#define RCAR_PWD_DEBUG_ENABLE	0

#if RCAR_PWD_DEBUG_ENABLE
/* Hardware debug macro */
#define DBG_DOMAIN_REGS(pd, t, log) ({ \
	if ((pd)) { \
		pr_debug("%s(), pd: %s: <%s %s> PWRSR=%x, SYSCISR=%x\n", \
		__func__, (pd)->genpd.name, (t), (log), \
		read_reg32((pd)->base + PWRSR), \
		read_reg32((sysc_base) + SYSCISR));	\
	}	\
})

#else
#define DBG_DOMAIN_REGS(pd, t, log)
#endif

/* R-Car Gen2 specific data */
static const struct rcar_sysc_domain_data rcar_gen2_pd_sysc[] = {
	DEF_DM_DATA("sh4a",  BIT(16), BIT(4), BIT(0), BIT(0), BIT(0), BIT(0)),
	DEF_DM_DATA("gsx",   BIT(20), BIT(4), BIT(0), BIT(0), BIT(0), BIT(0)),
	DEF_DM_DATA("imp",   BIT(24), BIT(4), BIT(0), BIT(0), BIT(0), BIT(0)),
};

static const struct rcar_sysc_domains_info rcar_gen2_domains_info = {
	.domains_list = rcar_gen2_pd_sysc,
	.len	= ARRAY_SIZE(rcar_gen2_pd_sysc),
};

/* R-Car H3 specific data */
static const struct rcar_sysc_domain_data rcar_r8a7795_pd_sysc[] = {
	DEF_DM_DATA("a3sg",  BITS_17_20 | BIT(22), (BITS_0_4 << 5),
				BITS_0_4, BITS_0_4, BITS_0_4, BITS_0_4),
	DEF_DM_DATA("a3ir",  BIT(24), BIT(4), BIT(0), BIT(0), BIT(0), BIT(0)),
	DEF_DM_DATA("a3vp",  BIT(9),  BIT(4), BIT(0), BIT(0), BIT(0), BIT(0)),
	DEF_DM_DATA("a3vc",  BIT(14), BIT(4), BIT(0), BIT(0), BIT(0), BIT(0)),
	DEF_DM_DATA("a2vc0", BIT(25), BIT(2), BIT(0), BIT(0), BIT(0), BIT(0)),
	DEF_DM_DATA("a2vc1", BIT(26), BIT(3), BIT(1), BIT(1), BIT(1), BIT(1)),
};

static const struct rcar_sysc_domains_info rcar_r8a7795_domains_info = {
	.domains_list = rcar_r8a7795_pd_sysc,
	.len	= ARRAY_SIZE(rcar_r8a7795_pd_sysc),
};

/* R-Car M3 specific data */
static const struct rcar_sysc_domain_data rcar_r8a7796_pd_sysc[] = {
	DEF_DM_DATA("a3sg",  (BIT(17) | BIT(18)), (BITS_0_1 << 5),
				BITS_0_1, BITS_0_1, BITS_0_1, BITS_0_1),
	DEF_DM_DATA("a3ir",  BIT(24), BIT(4), BIT(0), BIT(0), BIT(0), BIT(0)),
	DEF_DM_DATA("a3vc",  BIT(14), BIT(4), BIT(0), BIT(0), BIT(0), BIT(0)),
	DEF_DM_DATA("a2vc0", BIT(25), BIT(2), BIT(0), BIT(0), BIT(0), BIT(0)),
	DEF_DM_DATA("a2vc1", BIT(26), BIT(3), BIT(1), BIT(1), BIT(1), BIT(1)),
};

static const struct rcar_sysc_domains_info rcar_r8a7796_domains_info = {
	.domains_list = rcar_r8a7796_pd_sysc,
	.len	= ARRAY_SIZE(rcar_r8a7796_pd_sysc),
};

/*======= Sysc/Power Domain Driver =======*/
static const struct rcar_sysc_domains_info *domains_info;
static void __iomem *sysc_base; /* sysc base address */
/* Lock to prevent accessing power domains simultaneously*/
static DEFINE_SPINLOCK(sysc_lock);
static unsigned long sysc_lock_flags;

static inline
struct rcar_sysc_domain *to_rcar_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct rcar_sysc_domain, genpd);
}

/* This function checks status of power domains.
 * Return: 'true' : when domain is OFF or BEING OFF
 * Return: 'false' : in other states
 */
bool is_dm_off(struct rcar_sysc_domain *pd)
{
	const struct rcar_sysc_domain_data *dm_data = pd->dm_data;

	if (dm_data == NULL)
		return false;

	/* Check domain is OFF or BEING OFF */
	if ((dm_data->pwrsr_off & read_reg32(pd->base + PWRSR)) ||
	    (dm_data->pwr_on_off_sr & read_reg32(pd->base + PWROFFSR)))
		return true;

	return false;
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
		/* Start W/A for A3VP, A3VC, and A3IR domains */
		if ((flag == 0) && (!strcmp("a3vp", dm_data->name)
				|| !strcmp("a3ir", dm_data->name)
				|| !strcmp("a3vc", dm_data->name)))
			udelay(1);

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

int rcar_set_power_on_off(struct generic_pm_domain *genpd, int flag)
{
	/* Get appropriate platform power domain */
	struct rcar_sysc_domain *pd = to_rcar_pd(genpd);
	int ret = 0, i = 0;
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
		DBG_DOMAIN_REGS(pd, "before set", on_off);
		/* lock power domain to avoid simultaneously turn on/off */
		spin_lock_irqsave(&sysc_lock, sysc_lock_flags);

		for (i = 0; i < SYSCSR_RETRIES; i++) {
			/* Check SYSC resume/shutoff is ready to start */
			if (read_reg32(sysc_base + SYSCSR) &
						syscsr_pon_off_enb) {

				ret = set_dm_on_off(pd, flag);
				if (ret != -EAGAIN)
					break;
			}
			udelay(SYSCSR_DELAY_US);
		}

		spin_unlock_irqrestore(&sysc_lock, sysc_lock_flags);

		if (ret == 1) { /* already on/off */
			pr_debug("%s: %s: has already %s\n",
						__func__, genpd->name, on_off);
			return 0;
		}

		if (i == SYSCSR_RETRIES) { /* timeout error */
			pr_warn("%s: %s: turn %s, return timeout %d\n",
				__func__, genpd->name, on_off, ret);
			return -ETIMEDOUT;
		}

		/* Wait until the power shutoff/resume request has completed */
		for (i = 0; i < SYSCISR_RETRIES; i++) {
			if (pd->dm_data->syscisrs == (pd->dm_data->syscisrs &
					read_reg32(sysc_base + SYSCISR)))
				break;
			udelay(SYSCISR_DELAY_US);
		}

		if (i == SYSCISR_RETRIES) {
			ret = -ETIMEDOUT;
			pr_warn("%s(): pd: %s, %d (us)\n",
				__func__, genpd->name, i*SYSCISR_DELAY_US);
		}

		DBG_DOMAIN_REGS(pd, "after set", on_off);
		/* Clear interrupt */
		write_reg32(pd->dm_data->syscisrs, sysc_base + SYSCISCR);
		DBG_DOMAIN_REGS(pd, "after clear interrupt", on_off);
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

bool rcar_check_pm_clks(struct of_phandle_args *clkspec)
{
	if (!clkspec || !clkspec->np || !clkspec->np->name)
		return false;

	/* R-Car Gen3: get clocks only from cpg to support for pm */
	if (!strcmp("clock-controller", clkspec->np->name))
		return true;

	/* R-Car Gen2: get clocks only from mstp driver and zb_clk */
	if (of_device_is_compatible(clkspec->np,
			 "renesas,cpg-mstp-clocks") ||
		(!strcmp(clkspec->np->name, "zb_clk")))
		return true;

	return false;
}

static int rcar_clk_attach_dev(struct generic_pm_domain *genpd,
				  struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct of_phandle_args clkspec;
	struct clk *clk;
	int i = 0;
	int error;

	error = pm_clk_create(dev);
	if (error) {
		pr_err("%s: pm_clk_create failed %d\n", __func__, error);
		return error;
	}

	while (!of_parse_phandle_with_args(np, "clocks", "#clock-cells", i,
					   &clkspec)) {

		i++;
		if (!rcar_check_pm_clks(&clkspec))
			continue;

		clk = of_clk_get_from_provider(&clkspec);
		of_node_put(clkspec.np);

		error = IS_ERR(clk);
		if (error)
			goto fail_destroy;

		error = pm_clk_add_clk(dev, clk);
		if (error) {
			pr_err("%s: pm_clk_add_clk %pC failed %d\n",
				__func__, clk, error);
			goto fail_destroy;
		}
	}

	return 0;

fail_destroy:
	pm_clk_destroy(dev);
	return error;
}

static void rcar_clk_detach_dev(struct generic_pm_domain *genpd,
				struct device *dev)
{
	if (!list_empty(&dev->power.subsys_data->clock_list))
		pm_clk_destroy(dev);
}

static int rcar_setup_pm_domain(struct device_node *np,
					struct rcar_sysc_domain *pd)
{
	int i;

	if (!strcmp(np->name, "always_on")) {
		pd->dm_data = NULL;
	} else {
		for (i = 0; i < domains_info->len; i++) {
			if (!strcmp(domains_info->domains_list[i].name,
				np->name)) {

				pd->dm_data = domains_info->domains_list + i;
				break;
			}
		}

		/* Enable interupts for power domains */
		write_reg32(pd->dm_data->syscisrs |
			read_reg32(sysc_base + SYSCIER), sysc_base + SYSCIER);

		/* Mask interrupt factor of power domain */
		write_reg32(pd->dm_data->syscisrs |
			read_reg32(sysc_base + SYSCIMR), sysc_base + SYSCIMR);
	}

	pd->genpd.name = np->name;

	pd->genpd.flags = GENPD_FLAG_PM_CLK;
	pm_genpd_init(&pd->genpd, &simple_qos_governor, is_dm_off(pd));
	pd->genpd.attach_dev = rcar_clk_attach_dev;
	pd->genpd.detach_dev = rcar_clk_detach_dev;

	pd->genpd.power_off = rcar_power_off;
	pd->genpd.power_on  = rcar_power_on;

	of_genpd_add_provider_simple(np, &pd->genpd);

	return 0;
}

static int rcar_add_pm_domains(void __iomem *sysc_base,
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

static int rcar_pm_sysc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *pmd, *np = dev->of_node;
	int ret = 0;
	const struct of_device_id *match;

	if (!np) {
		dev_err(dev, "device tree node not found\n");
		return -ENODEV;
	}

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "missing sysc data match\n");
		return -EINVAL;
	}

	domains_info = (struct rcar_sysc_domains_info *)match->data;

	sysc_base = of_iomap(np, 0);

	if (!sysc_base) {
		pr_warn("%s cannot map sysc address\n", np->full_name);
		return -EINVAL;
	}

	pmd = of_get_child_by_name(np, "pm-domains");
	if (!pmd) {
		pr_warn("%s lacks pm-domains node\n", np->full_name);
		return -EINVAL;
	}

	ret = rcar_add_pm_domains(sysc_base, pmd, NULL);
	of_node_put(pmd);

	of_node_put(np);

	return ret;
}

static const struct of_device_id rcar_pm_sysc_dt_match[] = {
	{
		.compatible = "renesas,rcar-gen2-sysc",
		.data = (void *)&rcar_gen2_domains_info,
	},
	{
		.compatible = "renesas,rcar-r8a7795-sysc",
		.data = (void *)&rcar_r8a7795_domains_info,
	},
	{
		.compatible = "renesas,rcar-r8a7796-sysc",
		.data = (void *)&rcar_r8a7796_domains_info,
	},
	{ /* sentinel */ },
};

static struct platform_driver rcar_pm_sysc_driver = {
	.probe = rcar_pm_sysc_probe,
	.driver = {
		.name   = "rcar-pm-sysc",
		.of_match_table = rcar_pm_sysc_dt_match,
	},
};

static int __init rcar_pm_sysc_drv_register(void)
{
	return platform_driver_register(&rcar_pm_sysc_driver);
}

postcore_initcall(rcar_pm_sysc_drv_register);

MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_DESCRIPTION("Renesas R-Car Power Domain Control driver");
MODULE_LICENSE("GPL v2");
