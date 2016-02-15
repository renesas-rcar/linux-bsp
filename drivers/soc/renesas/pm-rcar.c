/*
 * R-Car SYSC Power management support
 *
 * Copyright (C) 2014  Magnus Damm
 * Copyright (C) 2015-2016 Glider bvba
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/of_address.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/soc/renesas/pm-rcar.h>

/* SYSC Common */
#define SYSCSR			0x00	/* SYSC Status Register */
#define SYSCISR			0x04	/* Interrupt Status Register */
#define SYSCISCR		0x08	/* Interrupt Status Clear Register */
#define SYSCIER			0x0c	/* Interrupt Enable Register */
#define SYSCIMR			0x10	/* Interrupt Mask Register */

/* SYSC Status Register */
#define SYSCSR_PONENB		1	/* Ready for power resume requests */
#define SYSCSR_POFFENB		0	/* Ready for power shutoff requests */

/*
 * Power Control Register Offsets inside the register block for each domain
 * Note: The "CR" registers for ARM cores exist on H1 only
 *       Use WFI to power off, CPG/APMU to resume ARM cores on R-Car Gen2
 */
#define PWRSR_OFFS		0x00	/* Power Status Register */
#define PWROFFCR_OFFS		0x04	/* Power Shutoff Control Register */
#define PWROFFSR_OFFS		0x08	/* Power Shutoff Status Register */
#define PWRONCR_OFFS		0x0c	/* Power Resume Control Register */
#define PWRONSR_OFFS		0x10	/* Power Resume Status Register */
#define PWRER_OFFS		0x14	/* Power Shutoff/Resume Error */

/*
 * SYSC Power Control Register Base Addresses (R-Car Gen2)
 */
#define SYSC_PWR_CA15_CPU	0x40	/* CA15 cores (incl. L1C) (H2/M2/V2H) */
#define SYSC_PWR_CA7_CPU	0x1c0	/* CA7 cores (incl. L1C) (H2/E2) */

/*
 * SYSC Power Control Register Base Addresses (R-Car Gen3)
 */
#define SYSC_PWR_CA57_CPU	0x80	/* CA57 cores (incl. L1C) (H3) */
#define SYSC_PWR_CA53_CPU	0x200	/* CA53 cores (incl. L1C) (H3) */


#define SYSCSR_RETRIES		100
#define SYSCSR_DELAY_US		1

#define PWRER_RETRIES		100
#define PWRER_DELAY_US		1

#define SYSCISR_RETRIES		1000
#define SYSCISR_DELAY_US	1

static void __iomem *rcar_sysc_base;
static DEFINE_SPINLOCK(rcar_sysc_lock); /* SMP CPUs + I/O devices */

static unsigned int rcar_gen;

static int rcar_sysc_pwr_on_off(const struct rcar_sysc_ch *sysc_ch, bool on)
{
	unsigned int sr_bit, reg_offs;
	int k;

	/*
	 * Only R-Car H1 can control power to CPUs
	 * Use WFI to power off, CPG/APMU to resume ARM cores on later R-Car
	 * Generations
	 */
	switch (rcar_gen) {
	case 2:
		/* FIXME Check rcar_pm_domain.cpu instead? */
		switch (sysc_ch->chan_offs) {
		case SYSC_PWR_CA15_CPU:
		case SYSC_PWR_CA7_CPU:
			pr_err("%s: Cannot control power to CPU\n", __func__);
			return -EINVAL;
		}
		break;

	case 3:
		/* FIXME Check rcar_pm_domain.cpu instead? */
		switch (sysc_ch->chan_offs) {
		case SYSC_PWR_CA57_CPU:
		case SYSC_PWR_CA53_CPU:
			pr_err("%s: Cannot control power to CPU\n", __func__);
			return -EINVAL;
		}
		break;
	}

	if (on) {
		sr_bit = SYSCSR_PONENB;
		reg_offs = PWRONCR_OFFS;
	} else {
		sr_bit = SYSCSR_POFFENB;
		reg_offs = PWROFFCR_OFFS;
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
	iowrite32(BIT(sysc_ch->chan_bit),
		  rcar_sysc_base + sysc_ch->chan_offs + reg_offs);

	return 0;
}

static int rcar_sysc_power(const struct rcar_sysc_ch *sysc_ch, bool on)
{
	unsigned int isr_mask = BIT(sysc_ch->isr_bit);
	unsigned int chan_mask = BIT(sysc_ch->chan_bit);
	unsigned int status;
	unsigned long flags;
	int ret = 0;
	int k;

	spin_lock_irqsave(&rcar_sysc_lock, flags);

	iowrite32(isr_mask, rcar_sysc_base + SYSCISCR);

	/* Submit power shutoff or resume request until it was accepted */
	for (k = 0; k < PWRER_RETRIES; k++) {
		ret = rcar_sysc_pwr_on_off(sysc_ch, on);
		if (ret)
			goto out;

		status = ioread32(rcar_sysc_base +
				  sysc_ch->chan_offs + PWRER_OFFS);
		if (!(status & chan_mask))
			break;

		udelay(PWRER_DELAY_US);
	}

	if (k == PWRER_RETRIES) {
		ret = -EIO;
		goto out;
	}

	/* Wait until the power shutoff or resume request has completed * */
	for (k = 0; k < SYSCISR_RETRIES; k++) {
		if (ioread32(rcar_sysc_base + SYSCISR) & isr_mask)
			break;
		udelay(SYSCISR_DELAY_US);
	}

	if (k == SYSCISR_RETRIES)
		ret = -EIO;

	iowrite32(isr_mask, rcar_sysc_base + SYSCISCR);

 out:
	spin_unlock_irqrestore(&rcar_sysc_lock, flags);

	pr_debug("sysc power %s domain %d: %08x -> %d\n", on ? "on" : "off",
		 sysc_ch->isr_bit, ioread32(rcar_sysc_base + SYSCISR), ret);
	return ret;
}

int rcar_sysc_power_down(const struct rcar_sysc_ch *sysc_ch)
{
	return rcar_sysc_power(sysc_ch, false);
}

int rcar_sysc_power_up(const struct rcar_sysc_ch *sysc_ch)
{
	return rcar_sysc_power(sysc_ch, true);
}

bool rcar_sysc_power_is_off(const struct rcar_sysc_ch *sysc_ch)
{
	unsigned int st;

	st = ioread32(rcar_sysc_base + sysc_ch->chan_offs + PWRSR_OFFS);
	if (st & BIT(sysc_ch->chan_bit))
		return true;

	return false;
}

void __iomem *rcar_sysc_init(phys_addr_t base)
{
	rcar_sysc_base = ioremap_nocache(base, PAGE_SIZE);
	if (!rcar_sysc_base)
		panic("unable to ioremap R-Car SYSC hardware block\n");

	return rcar_sysc_base;
}

#ifdef CONFIG_PM_GENERIC_DOMAINS
struct rcar_pm_domain {
	struct generic_pm_domain genpd;
	struct dev_power_governor *gov;
	struct rcar_sysc_ch ch;
	unsigned busy:1;		/* Set if always -EBUSY */
	unsigned cpu:1;			/* Set if domain contains CPU */
	char name[0];
};

static inline struct rcar_pm_domain *to_rcar_pd(struct generic_pm_domain *d)
{
	return container_of(d, struct rcar_pm_domain, genpd);
}

static bool rcar_pd_active_wakeup(struct device *dev)
{
	return true;
}

static int rcar_pd_power_down(struct generic_pm_domain *genpd)
{
	struct rcar_pm_domain *rcar_pd = to_rcar_pd(genpd);

	pr_debug("%s: %s\n", __func__, genpd->name);

	if (rcar_pd->busy) {
		pr_debug("%s: %s busy\n", __func__, genpd->name);
		return -EBUSY;
	}

	return rcar_sysc_power_down(&rcar_pd->ch);
}

static int rcar_pd_power_up(struct generic_pm_domain *genpd)
{
	pr_debug("%s: %s\n", __func__, genpd->name);
	return rcar_sysc_power_up(&to_rcar_pd(genpd)->ch);
}

static void rcar_init_pm_domain(struct rcar_pm_domain *rcar_pd)
{
	struct generic_pm_domain *genpd = &rcar_pd->genpd;
	struct dev_power_governor *gov = rcar_pd->gov;

	pm_genpd_init(genpd, gov ? : &simple_qos_governor, false);
	genpd->dev_ops.active_wakeup	= rcar_pd_active_wakeup;
	genpd->power_off		= rcar_pd_power_down;
	genpd->power_on			= rcar_pd_power_up;

	if (rcar_sysc_power_is_off(&rcar_pd->ch))
		rcar_sysc_power_up(&rcar_pd->ch);
}

enum pd_types {
	PD_NORMAL,
	PD_CPU,
	PD_SCU,
};

#define MAX_NUM_SPECIAL_PDS	16

static struct special_pd {
	struct device_node *pd;
	enum pd_types type;
} special_pds[MAX_NUM_SPECIAL_PDS] __initdata;

static unsigned int num_special_pds __initdata;

static void __init add_special_pd(struct device_node *np, enum pd_types type)
{
	unsigned int i;
	struct device_node *pd;

	pd = of_parse_phandle(np, "power-domains", 0);
	if (!pd)
		return;

	for (i = 0; i < num_special_pds; i++)
		if (pd == special_pds[i].pd && type == special_pds[i].type) {
			of_node_put(pd);
			return;
		}

	if (num_special_pds == ARRAY_SIZE(special_pds)) {
		pr_warn("Too many special PM domains\n");
		of_node_put(pd);
		return;
	}

	pr_debug("Special PM domain %s type %d for %s\n", pd->name, type,
		 np->full_name);

	special_pds[num_special_pds].pd = pd;
	special_pds[num_special_pds].type = type;
	num_special_pds++;
}

static void __init get_special_pds(void)
{
	struct device_node *cpu, *scu;

	/* PM domains containing CPUs */
	for_each_node_by_type(cpu, "cpu") {
		add_special_pd(cpu, PD_CPU);

		/* SCU, represented by an L2 node */
		scu = of_parse_phandle(cpu, "next-level-cache", 0);
		if (scu) {
			add_special_pd(scu, PD_SCU);
			of_node_put(scu);
		}
	}
}

static void __init put_special_pds(void)
{
	unsigned int i;

	for (i = 0; i < num_special_pds; i++)
		of_node_put(special_pds[i].pd);
}

static enum pd_types __init pd_type(const struct device_node *pd)
{
	unsigned int i;

	for (i = 0; i < num_special_pds; i++)
		if (pd == special_pds[i].pd)
			return special_pds[i].type;

	return PD_NORMAL;
}

static void __init rcar_setup_pm_domain(struct device_node *np,
					struct rcar_pm_domain *pd)
{
	const char *name = pd->genpd.name;

	switch (pd_type(np)) {
	case PD_CPU:
		/*
		 * This domain contains a CPU core and therefore it should
		 * only be turned off if the CPU is not in use.
		 */
		pr_debug("PM domain %s contains CPU\n", name);
		pd->gov = &pm_domain_always_on_gov;
		pd->busy = true;
		pd->cpu = true;
		break;

	case PD_SCU:
		/*
		 * This domain contains an SCU and cache-controller, and
		 * therefore it should only be turned off if the CPU cores are
		 * not in use.
		 */
		pr_debug("PM domain %s contains SCU\n", name);
		pd->gov = &pm_domain_always_on_gov;
		pd->busy = true;
		break;

	case PD_NORMAL:
		break;
	}

	rcar_init_pm_domain(pd);
}

static int __init rcar_add_pm_domains(struct device_node *parent,
				      struct generic_pm_domain *genpd_parent,
				      u32 *syscier)
{
	struct device_node *np;

	for_each_child_of_node(parent, np) {
		struct rcar_pm_domain *pd;
		u32 reg[2];
		int n;

		if (of_property_read_u32_array(np, "reg", reg,
					       ARRAY_SIZE(reg))) {
			of_node_put(np);
			return -EINVAL;
		}

		*syscier |= BIT(reg[0]);

		if (!IS_ENABLED(CONFIG_PM)) {
			/* Just continue parsing "reg" to update *syscier */
			rcar_add_pm_domains(np, NULL, syscier);
			continue;
		}

		n = snprintf(NULL, 0, "%s@%u", np->name, reg[0]) + 1;

		pd = kzalloc(sizeof(*pd) + n, GFP_KERNEL);
		if (!pd) {
			of_node_put(np);
			return -ENOMEM;
		}

		snprintf(pd->name, n, "%s@%u", np->name, reg[0]);
		pd->genpd.name = pd->name;
		pd->ch.chan_offs = reg[1] & ~31;
		pd->ch.chan_bit = reg[1] & 31;
		pd->ch.isr_bit = reg[0];

		rcar_setup_pm_domain(np, pd);
		if (genpd_parent)
			pm_genpd_add_subdomain(genpd_parent, &pd->genpd);
		of_genpd_add_provider_simple(np, &pd->genpd);

		rcar_add_pm_domains(np, &pd->genpd, syscier);
	}
	return 0;
}

static const struct of_device_id rcar_sysc_matches[] = {
	{ .compatible = "renesas,r8a7779-sysc", .data = (void *)1 },
	{ .compatible = "renesas,rcar-gen2-sysc", .data = (void *)2 },
	{ .compatible = "renesas,rcar-gen3-sysc", .data = (void *)3 },
	{ /* sentinel */ }
};

static int __init rcar_init_pm_domains(void)
{
	const struct of_device_id *match;
	struct device_node *np, *pmd;
	bool scanned = false;
	void __iomem *base;
	int ret = 0;

	for_each_matching_node_and_match(np, rcar_sysc_matches, &match) {
		u32 syscier = 0;

		rcar_gen = (uintptr_t)match->data;

		base = of_iomap(np, 0);
		if (!base) {
			pr_warn("%s cannot map reg 0\n", np->full_name);
			continue;
		}

		rcar_sysc_base = base;	// FIXME conflicts with rcar_sysc_init()

		pmd = of_get_child_by_name(np, "pm-domains");
		if (!pmd) {
			pr_warn("%s lacks pm-domains node\n", np->full_name);
			continue;
		}

		if (!scanned) {
			/* Find PM domains containing special blocks */
			get_special_pds();
			scanned = true;
		}

		ret = rcar_add_pm_domains(pmd, NULL, &syscier);
		of_node_put(pmd);
		if (ret) {
			of_node_put(np);
			break;
		}

		/*
		 * Enable all interrupt sources, but do not use interrupt
		 * handler
		 */
		pr_debug("%s: syscier = 0x%08x\n", np->full_name, syscier);
		iowrite32(syscier, rcar_sysc_base + SYSCIER);
		iowrite32(0, rcar_sysc_base + SYSCIMR);
	}

	put_special_pds();

	return ret;
}

core_initcall(rcar_init_pm_domains);
#endif /* PM_GENERIC_DOMAINS */
