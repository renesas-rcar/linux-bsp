// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car V3U SYSC Power management support
 *
 * Copyright (C) 2020 Renesas Electronics Corporation
 *
 */

#include <dt-bindings/power/r8a779a0-sysc.h>
#include <linux/clk/renesas.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/of_address.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/soc/renesas/rcar-sysc.h>
#include <linux/sys_soc.h>
#include <linux/syscore_ops.h>

#include "rcar-sysc.h"

/* SYSC Common */
#define SYSCSR			0x000	/* SYSC Status Register */
#define SYSCPONSR(x)	(0x800 + ((x) * 0x4)) /* Power-ON Status Register 0 */
#define SYSCPOFFSR(x)	(0x808 + ((x) * 0x4)) /* Power-OFF Status Register */
#define SYSCISCR(x)		(0x810 + ((x) * 0x4)) /* Interrupt Status/Clear Register */
#define SYSCIER(x)		(0x820 + ((x) * 0x4)) /* Interrupt Enable Register */
#define SYSCIMR(x)		(0x830 + ((x) * 0x4)) /* Interrupt Mask Register */

/* Power Domain Registers */
#define PDRSR(n)		(0x1000 + ((n) * 0x40))
#define PDRONCR(n)		(0x1004 + ((n) * 0x40))
#define PDROFFCR(n)		(0x1008 + ((n) * 0x40))
#define PDRESR(n)		(0x100C + ((n) * 0x40))

/* Power State */
#define PW_ACTIVE		1	/* Active setting */

/* PDRSR */
#define PDRSR_OFF			BIT(0)	/* Power-OFF state */
#define PDRSR_ON			BIT(4)	/* Power-ON state */
#define PDRSR_OFF_STATE		BIT(8)  /* Processing Power-OFF sequence */
#define PDRSR_ON_STATE		BIT(12) /* Processing Power-ON sequence */

#define SYSCSR_PONENB		1	/* Ready for power resume requests */
#define SYSCSR_POFFENB		0	/* Ready for power shutoff requests */

#define SYSCSR_RETRIES		1000
#define SYSCSR_DELAY_US		10

#define PDRESR_RETRIES		1000
#define PDRESR_DELAY_US		10

#define SYSCISR_RETRIES		1000
#define SYSCISR_DELAY_US	10

#define R8A779A0_NUM_PD_ALWAYS_ON	64	/* Always-on power area */

#define NUM_DOMAINS_EACH_REG	32

struct rcar_sysc_ch {
	u16 chan_offs;
	u8 chan_bit;
	u8 isr_bit;
};

static void __iomem *rcar_sysc_base;
static DEFINE_SPINLOCK(rcar_sysc_lock); /* SMP CPUs + I/O devices */

static int rcar_sysc_pwr_on_off(const struct rcar_sysc_ch *sysc_ch, bool on)
{
	unsigned int sr_bit, reg_offs;
	int k;

	if (on) {
		sr_bit = SYSCSR_PONENB;
		reg_offs = PDRONCR(sysc_ch->isr_bit);
	} else {
		sr_bit = SYSCSR_POFFENB;
		reg_offs = PDROFFCR(sysc_ch->isr_bit);
	}

	/* Wait until SYSC is ready to accept a power request */
	for (k = 0; k < SYSCSR_RETRIES; k++) {
		if (ioread32(rcar_sysc_base + SYSCSR) & BIT(sr_bit))
			break;
		udelay(SYSCSR_DELAY_US);
	}

	if (k == SYSCSR_RETRIES)
		return -EAGAIN;

	/* Submit power shutoff or power resume request */
	iowrite32(PW_ACTIVE, rcar_sysc_base + reg_offs);

	return 0;
}

static int clear_irq_flags(unsigned int reg_idx, unsigned int isr_mask)
{
	int k = 0;

	iowrite32(isr_mask, rcar_sysc_base + SYSCISCR(reg_idx));

	for (k = 0; k < SYSCISR_RETRIES; k++) {
		if ((ioread32(rcar_sysc_base + SYSCISCR(reg_idx)) & isr_mask) == 0)
			break;

		udelay(SYSCISR_DELAY_US);
	}

	if (k == SYSCISR_RETRIES) {
		pr_err("\n %s : Can not clear IRQ flags in SYSCISCR", __func__);
		return -EIO;
	}

	return 0;
}

static int rcar_sysc_power(const struct rcar_sysc_ch *sysc_ch, bool on)
{
	unsigned int isr_mask, chan_mask;
	unsigned int reg_idx, bit_idx;
	unsigned int status;
	unsigned long flags;
	int ret = 0;
	int k;

	spin_lock_irqsave(&rcar_sysc_lock, flags);

	reg_idx = sysc_ch->isr_bit / NUM_DOMAINS_EACH_REG;
	bit_idx = sysc_ch->isr_bit % NUM_DOMAINS_EACH_REG;

	chan_mask = BIT(sysc_ch->chan_bit);
	isr_mask = BIT(bit_idx);

	/*
	 * The interrupt source needs to be enabled, but masked, to prevent the
	 * CPU from receiving it.
	 */
	iowrite32(ioread32(rcar_sysc_base + SYSCIER(reg_idx)) | isr_mask,
		  rcar_sysc_base + SYSCIER(reg_idx));
	iowrite32(ioread32(rcar_sysc_base + SYSCIMR(reg_idx)) | isr_mask,
		  rcar_sysc_base + SYSCIMR(reg_idx));

	ret = clear_irq_flags(reg_idx, isr_mask);
	if (ret)
		goto out;

	/* Submit power shutoff or resume request until it was accepted */
	for (k = 0; k < PDRESR_RETRIES; k++) {
		ret = rcar_sysc_pwr_on_off(sysc_ch, on);
		if (ret)
			goto out;

		status = ioread32(rcar_sysc_base + PDRESR(sysc_ch->isr_bit));
		if (!(status & chan_mask))
			break;

		udelay(PDRESR_DELAY_US);
	}

	if (k == PDRESR_RETRIES) {
		ret = -EIO;
		goto out;
	}

	/* Wait until the power shutoff or resume request has completed * */
	for (k = 0; k < SYSCISR_RETRIES; k++) {
		if (ioread32(rcar_sysc_base + SYSCISCR(reg_idx)) & isr_mask)
			break;
		udelay(SYSCISR_DELAY_US);
	}

	if (k == SYSCISR_RETRIES) {
		ret = -EIO;
		goto out;
	}

	/* Clear interrupt flags */
	ret = clear_irq_flags(reg_idx, isr_mask);
	if (ret)
		goto out;

 out:
	spin_unlock_irqrestore(&rcar_sysc_lock, flags);

	pr_debug("sysc power %s domain %d: %08x -> %d\n", on ? "on" : "off",
		 sysc_ch->isr_bit, ioread32(rcar_sysc_base + SYSCISCR(reg_idx)), ret);
	return ret;
}

static bool rcar_sysc_power_is_off(const struct rcar_sysc_ch *sysc_ch)
{
	unsigned int st;

	st = ioread32(rcar_sysc_base + PDRSR(sysc_ch->isr_bit));

	if (st & PDRSR_OFF)
		return true;

	return false;
}

struct rcar_sysc_pd {
	struct generic_pm_domain genpd;
	struct rcar_sysc_ch ch;
	unsigned int flags;
	char name[];
};

static inline struct rcar_sysc_pd *to_rcar_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct rcar_sysc_pd, genpd);
}

static int rcar_sysc_pd_power_off(struct generic_pm_domain *genpd)
{
	struct rcar_sysc_pd *pd = to_rcar_pd(genpd);

	pr_debug("%s: %s\n", __func__, genpd->name);
	return rcar_sysc_power(&pd->ch, false);
}

static int rcar_sysc_pd_power_on(struct generic_pm_domain *genpd)
{
	struct rcar_sysc_pd *pd = to_rcar_pd(genpd);

	pr_debug("%s: %s\n", __func__, genpd->name);
	return rcar_sysc_power(&pd->ch, true);
}

static bool has_cpg_mstp;

static int __init rcar_sysc_pd_setup(struct rcar_sysc_pd *pd)
{
	struct generic_pm_domain *genpd = &pd->genpd;
	const char *name = pd->genpd.name;
	int error;

	if (pd->flags & PD_CPU) {
		/*
		 * This domain contains a CPU core and therefore it should
		 * only be turned off if the CPU is not in use.
		 */
		pr_debug("PM domain %s contains %s\n", name, "CPU");
		genpd->flags |= GENPD_FLAG_ALWAYS_ON;
	} else if (pd->flags & PD_SCU) {
		/*
		 * This domain contains an SCU and cache-controller, and
		 * therefore it should only be turned off if the CPU cores are
		 * not in use.
		 */
		pr_debug("PM domain %s contains %s\n", name, "SCU");
		genpd->flags |= GENPD_FLAG_ALWAYS_ON;
	} else if (pd->flags & PD_NO_CR) {
		/*
		 * This domain cannot be turned off.
		 */
		genpd->flags |= GENPD_FLAG_ALWAYS_ON;
	}

	if (!(pd->flags & (PD_CPU | PD_SCU))) {
		/* Enable Clock Domain for I/O devices */
		genpd->flags |= GENPD_FLAG_PM_CLK | GENPD_FLAG_ACTIVE_WAKEUP;
		if (has_cpg_mstp) {
			genpd->attach_dev = cpg_mstp_attach_dev;
			genpd->detach_dev = cpg_mstp_detach_dev;
		} else {
			genpd->attach_dev = cpg_mssr_attach_dev;
			genpd->detach_dev = cpg_mssr_detach_dev;
		}
	}

	genpd->power_off = rcar_sysc_pd_power_off;
	genpd->power_on = rcar_sysc_pd_power_on;

	if (pd->flags & (PD_CPU | PD_NO_CR)) {
		/* Skip CPUs (handled by SMP code) and areas without control */
		pr_debug("%s: Not touching %s\n", __func__, genpd->name);
		goto finalize;
	}

	if (!rcar_sysc_power_is_off(&pd->ch)) {
		pr_debug("%s: %s is already powered\n", __func__, genpd->name);
		goto finalize;
	}

	rcar_sysc_power(&pd->ch, true);

finalize:
	error = pm_genpd_init(genpd, &simple_qos_governor, false);
	if (error)
		pr_err("Failed to init PM domain %s: %d\n", name, error);

	return error;
}

static const struct of_device_id rcar_sysc_matches[] __initconst = {
#ifdef CONFIG_SYSC_R8A779A0
	{ .compatible = "renesas,r8a779a0-sysc", .data = &r8a779a0_sysc_info },
#endif
	{ /* sentinel */ }
};

struct rcar_pm_domains {
	struct genpd_onecell_data onecell_data;
	struct generic_pm_domain *domains[R8A779A0_NUM_PD_ALWAYS_ON + 1];
};

static struct genpd_onecell_data *rcar_sysc_onecell_data;

static int __init rcar_sysc_pd_init(void)
{
	const struct rcar_sysc_info *info;
	const struct of_device_id *match;
	struct rcar_pm_domains *domains;
	struct device_node *np;
	void __iomem *base;
	unsigned int i;
	int error;

	np = of_find_matching_node_and_match(NULL, rcar_sysc_matches, &match);
	if (!np)
		return -ENODEV;

	info = match->data;

	if (info->init) {
		error = info->init();
		if (error)
			goto out_put;
	}

	has_cpg_mstp = of_find_compatible_node(NULL, NULL,
					       "renesas,cpg-mstp-clocks");

	base = of_iomap(np, 0);
	if (!base) {
		pr_warn("%pOF: Cannot map regs\n", np);
		error = -ENOMEM;
		goto out_put;
	}

	rcar_sysc_base = base;

	domains = kzalloc(sizeof(*domains), GFP_KERNEL);
	if (!domains) {
		error = -ENOMEM;
		goto out_put;
	}

	domains->onecell_data.domains = domains->domains;
	domains->onecell_data.num_domains = ARRAY_SIZE(domains->domains);
	rcar_sysc_onecell_data = &domains->onecell_data;

	for (i = 0; i < info->num_areas; i++) {
		const struct rcar_sysc_area *area = &info->areas[i];
		struct rcar_sysc_pd *pd;

		if (!area->name) {
			/* Skip NULLified area */
			continue;
		}

		pd = kzalloc(sizeof(*pd) + strlen(area->name) + 1, GFP_KERNEL);
		if (!pd) {
			error = -ENOMEM;
			goto out_put;
		}

		strcpy(pd->name, area->name);
		pd->genpd.name = pd->name;
		pd->ch.chan_offs = area->chan_offs;
		pd->ch.chan_bit = area->chan_bit;
		pd->ch.isr_bit = area->isr_bit;
		pd->flags = area->flags;

		error = rcar_sysc_pd_setup(pd);
		if (error)
			goto out_put;

		domains->domains[area->isr_bit] = &pd->genpd;

		if (area->parent < 0)
			continue;

		error = pm_genpd_add_subdomain(domains->domains[area->parent],
					       &pd->genpd);
		if (error) {
			pr_warn("Failed to add PM subdomain %s to parent %u\n",
				area->name, area->parent);
			goto out_put;
		}
	}

	error = of_genpd_add_provider_onecell(np, &domains->onecell_data);

out_put:
	of_node_put(np);
	return error;
}
early_initcall(rcar_sysc_pd_init);
