/*
 * Renesas Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2015 Glider bvba
 *
 * Based on clk-mstp.c, clk-rcar-gen2.c, and clk-rcar-gen3.c
 *
 * Copyright (C) 2013 Ideas On Board SPRL
 * Copyright (C) 2015 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>

#include <dt-bindings/clock/renesas-cpg-mssr.h>

#include "clk-cpg-mssr.h"
#include "clk-div6.h"


/*
 * Module Standby and Software Reset register offets.
 *
 * If the registers exist, these are valid for SH-Mobile, R-Mobile,
 * R-Car Gen 2, and R-Car Gen 3.
 * These are NOT valid for R-Car Gen1 and RZ/A1!
 */

/*
 * Module Stop Status Register offsets
 */

static const u16 mstpsr[] = {
	0x030, 0x038, 0x040, 0x048, 0x04C, 0x03C, 0x1C0, 0x1C4,
	0x9A0, 0x9A4, 0x9A8, 0x9AC,
};

#define	MSTPSR(i)	mstpsr[i]


/*
 * System Module Stop Control Register offsets
 */

static const u16 smstpcr[] = {
	0x130, 0x134, 0x138, 0x13C, 0x140, 0x144, 0x148, 0x14C,
	0x990, 0x994, 0x998, 0x99C,
};

#define	SMSTPCR(i)	smstpcr[i]


/*
 * Software Reset Register offsets
 */

static const u16 srcr[] = {
	0x0A0, 0x0A8, 0x0B0, 0x0B8, 0x0BC, 0x0C4, 0x1C8, 0x1CC,
	0x920, 0x924, 0x928, 0x92C,
};

#define	SRCR(i)		srcr[i]


/* Realtime Module Stop Control Register offsets */
#define RMSTPCR(i)	(smstpcr[i] - 0x20)

/* Modem Module Stop Control Register offsets (r8a73a4) */
#define MMSTPCR(i)	(smstpcr[i] + 0x20)

/* Software Reset Clearing Register offsets */
#define	SRSTCLR(i)	(0x940 + (i) * 4)


#define MSTP_MAX_REGS		ARRAY_SIZE(smstpcr)
#define MSTP_MAX_CLOCKS		(MSTP_MAX_REGS * 32)


/**
 * Clock Pulse Generator / Module Standby and Software Reset Private Data
 *
 * @base: CPG/MSSR register block base address
 * @mstp_lock: protects writes to SMSTPCR
 */
struct cpg_mssr_priv {
	void __iomem *base;
	spinlock_t mstp_lock;
	struct device *dev;

	/* Core and Module Clocks */
	struct clk **clks;

	/* Core Clocks */
	unsigned int num_core_clks;
	unsigned int last_dt_core_clk;

	/* Module Clocks */
	unsigned int num_mod_clks;

	/* Core Clocks suitable for PM, in addition to the module clocks */
	const unsigned int *core_pm_clks;
	unsigned int num_core_pm_clks;
};


/**
 * struct mstp_clock - MSTP gating clock
 * @hw: handle between common and hardware-specific interfaces
 * @index: MSTP clock number
 * @priv: CPG/MSSR private data
 */
struct mstp_clock {
	struct clk_hw hw;
	u32 index;
	struct cpg_mssr_priv *priv;
};

#define to_mstp_clock(_hw) container_of(_hw, struct mstp_clock, hw)

static int cpg_mstp_clock_endisable(struct clk_hw *hw, bool enable)
{
	struct mstp_clock *clock = to_mstp_clock(hw);
	struct cpg_mssr_priv *priv = clock->priv;
	unsigned int reg = clock->index / 32;
	unsigned int bit = clock->index % 32;
	struct device *dev = priv->dev;
	u32 bitmask = BIT(bit);
	unsigned long flags;
	unsigned int i;
	u32 value;

	dev_dbg(dev, "MSTP %u%02u/%pC %s\n", reg, bit, hw->clk,
		enable ? "ON" : "OFF");
	spin_lock_irqsave(&priv->mstp_lock, flags);

	value = clk_readl(priv->base + SMSTPCR(reg));
	if (enable)
		value &= ~bitmask;
	else
		value |= bitmask;
	clk_writel(value, priv->base + SMSTPCR(reg));

	spin_unlock_irqrestore(&priv->mstp_lock, flags);

	if (!enable)
		return 0;

	for (i = 1000; i > 0; --i) {
		if (!(clk_readl(priv->base + MSTPSR(reg)) &
		      bitmask))
			break;
		cpu_relax();
	}

	if (!i) {
		dev_err(dev, "Failed to enable SMSTP %p[%d]\n",
			priv->base + SMSTPCR(reg), bit);
		return -ETIMEDOUT;
	}

	return 0;
}

static int cpg_mstp_clock_enable(struct clk_hw *hw)
{
	return cpg_mstp_clock_endisable(hw, true);
}

static void cpg_mstp_clock_disable(struct clk_hw *hw)
{
	cpg_mstp_clock_endisable(hw, false);
}

static int cpg_mstp_clock_is_enabled(struct clk_hw *hw)
{
	struct mstp_clock *clock = to_mstp_clock(hw);
	struct cpg_mssr_priv *priv = clock->priv;
	u32 value;

	value = clk_readl(priv->base + MSTPSR(clock->index / 32));

	return !(value & BIT(clock->index % 32));
}

static const struct clk_ops cpg_mstp_clock_ops = {
	.enable = cpg_mstp_clock_enable,
	.disable = cpg_mstp_clock_disable,
	.is_enabled = cpg_mstp_clock_is_enabled,
};

static
struct clk *cpg_mssr_clk_src_twocell_get(struct of_phandle_args *clkspec,
					 void *data)
{
	unsigned int clkidx = clkspec->args[1];
	struct cpg_mssr_priv *priv = data;
	struct device *dev = priv->dev;
	unsigned int idx;
	const char *type;
	struct clk *clk;

	switch (clkspec->args[0]) {
	case CPG_CORE:
		type = "core";
		if (clkidx > priv->last_dt_core_clk) {
			dev_err(dev, "Invalid %s clock index %u\n", type,
			       clkidx);
			return ERR_PTR(-EINVAL);
		}
		clk = priv->clks[clkidx];
		break;

	case CPG_MOD:
		type = "module";
		/* Translate from sparse base-100 to packed index space */
		idx = clkidx - (clkidx / 100) * (100 - 32);
		if (clkidx % 100 > 31 || idx >= priv->num_mod_clks) {
			dev_err(dev, "Invalid %s clock index %u\n", type,
				clkidx);
			return ERR_PTR(-EINVAL);
		}
		clk = priv->clks[priv->num_core_clks + idx];
		break;

	default:
		dev_err(dev, "Invalid CPG clock type %u\n", clkspec->args[0]);
		return ERR_PTR(-EINVAL);
	}

	if (IS_ERR(clk))
		dev_err(dev, "Cannot get %s clock %u: %ld", type, clkidx,
		       PTR_ERR(clk));
	else
		dev_dbg(dev, "clock (%u, %u) is %pC\n", clkspec->args[0],
			clkspec->args[1], clk);
	return clk;
}


// FIXME Handle packing in a macro in cpg_mssr_info.mod_clks[]???
static unsigned int __init mod_pack(unsigned int idx,
				    struct cpg_mssr_priv *priv)
{
	WARN_ON(idx % 100 > 31);
	idx -= (idx / 100) * (100 - 32);
	WARN_ON(idx >= priv->num_mod_clks);
	return idx;
}

static unsigned int __init id_to_idx(unsigned int id,
				     struct cpg_mssr_priv *priv)
{
	unsigned int idx;

	if (id < priv->num_core_clks) {
		/* Core Clock */
		dev_dbg(priv->dev, "%u is a core clock\n", id);
		return id;
	}

	/* Module Clock */
	idx = priv->num_core_clks +
	      mod_pack(id - priv->num_core_clks, priv);
	dev_dbg(priv->dev, "%u is module clock %u at index %u\n", id,
		id - priv->num_core_clks, idx);
	return idx;
}

static void __init cpg_mssr_register_core_clk(struct device_node *np,
					      const struct cpg_core_clk *core,
					      const struct cpg_mssr_info *info,
					      struct cpg_mssr_priv *priv)
{
	struct clk *clk = NULL, *parent;
	struct device *dev = priv->dev;
	unsigned int idx = core->id;
	const char *parent_name;

	dev_dbg(dev, "Registering core clock %s id %u type %u\n", core->name,
		idx, core->type);
	WARN_ON(idx >= priv->num_core_clks);
	WARN_ON(PTR_ERR(priv->clks[idx]) != -ENOENT);

	switch (core->type) {
	/* Generic */
	case CLK_TYPE_IN:
		/* External Clock Input */
		clk = of_clk_get_by_name(np, core->name);
		break;

	case CLK_TYPE_FF:
		/* Fixed Factor Clock */
		WARN_ON(core->parent >= priv->num_core_clks);
		parent = priv->clks[core->parent];
		if (IS_ERR(parent)) {
			clk = parent;
			goto fail;
		}

		parent_name = __clk_get_name(parent);
		clk = clk_register_fixed_factor(NULL, core->name, parent_name,
						0, core->mult, core->div);
		break;

	case CLK_TYPE_DIV6P1:
		/* DIV6 Clock with 1 parent clock */
		WARN_ON(core->parent >= priv->num_core_clks);
		parent = priv->clks[core->parent];
		if (IS_ERR(parent)) {
			clk = parent;
			goto fail;
		}

		parent_name = __clk_get_name(parent);
		clk = cpg_div6_register(core->name, 1, &parent_name,
					priv->base + core->offset);
		break;

	default:
		if (info->cpg_clk_register)
			clk = info->cpg_clk_register(dev, core, info,
						     priv->clks, priv->base);
		else
			dev_err(dev, "Unsupported core clock type %u\n",
				core->type);
		break;
	}

	if (IS_ERR_OR_NULL(clk))
		goto fail;

	dev_dbg(dev, "Registered core clock %pC\n", clk);
	priv->clks[idx] = clk;
	return;

fail:
	dev_err(dev, "Failed to register core clock %u: %ld\n", idx,
		PTR_ERR(clk));
}

static void __init cpg_mssr_register_mod_clk(const struct mssr_mod_clk *mod,
					     const struct cpg_mssr_info *info,
					     struct cpg_mssr_priv *priv)
{
	unsigned int mod_idx, idx, parent_idx;
	struct mstp_clock *clock = NULL;
	struct device *dev = priv->dev;
	struct clk_init_data init;
	struct clk *parent, *clk;
	const char *parent_name;
	unsigned int i;

	dev_dbg(dev, "Registering module clock %s id %u parent %u\n",
		mod->name, mod->id, mod->parent);

	// FIXME Incorporate offset/packing in cpg_mssr_info.mod_clks[]?
	mod_idx = mod_pack(mod->id, priv);
	WARN_ON(mod_idx >= priv->num_mod_clks);
	idx = priv->num_core_clks + mod_idx;
	parent_idx = id_to_idx(mod->parent, priv);
	WARN_ON(parent_idx >= priv->num_core_clks + priv->num_mod_clks);
	WARN_ON(PTR_ERR(priv->clks[idx]) != -ENOENT);

	parent = priv->clks[parent_idx];
	if (IS_ERR(parent)) {
		clk = parent;
		goto fail;
	}

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock) {
		clk = ERR_PTR(-ENOMEM);
		goto fail;
	}

	init.name = mod->name;
	init.ops = &cpg_mstp_clock_ops;
	init.flags = CLK_IS_BASIC | CLK_SET_RATE_PARENT;
	for (i = 0; i < info->num_crit_mod_clks; i++)
		if (mod->id == info->crit_mod_clks[i]) {
#ifdef CLK_ENABLE_HAND_OFF
			dev_dbg(dev, "MSTP %s setting CLK_ENABLE_HAND_OFF\n",
				mod->name);
			init.flags |= CLK_ENABLE_HAND_OFF;
			break;
#else
			dev_dbg(dev, "Ignoring MSTP %s to prevent disabling\n",
				mod->name);
			return;
#endif
		}

	parent_name = __clk_get_name(parent);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->index = mod_idx;
	clock->priv = priv;
	clock->hw.init = &init;

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk))
		goto fail;

	dev_dbg(dev, "Created module clock %pC\n", clk);
	priv->clks[idx] = clk;
	return;

fail:
	dev_err(dev, "Failed to create module clock %u: %ld\n", mod->id,
		PTR_ERR(clk));
	kfree(clock);
}


#ifdef CONFIG_PM_GENERIC_DOMAINS_OF
struct cpg_mssr_clk_domain {
	struct generic_pm_domain genpd;
	struct device_node *np;
	unsigned int num_core_pm_clks;
	unsigned int core_pm_clks[0];
};

static bool cpg_mssr_is_pm_clk(const struct of_phandle_args *clkspec,
			       struct cpg_mssr_clk_domain *pd)
{
	unsigned int i;

	if (clkspec->np != pd->np || clkspec->args_count != 2) {
		return false;
	}

	switch (clkspec->args[0]) {
	case CPG_CORE:
		for (i = 0; i < pd->num_core_pm_clks; i++)
			if (clkspec->args[1] == pd->core_pm_clks[i]) {
				return true;
			}
		return false;

	case CPG_MOD:
		return true;

	default:
		return false;
	}
}

static int cpg_mssr_attach_dev(struct generic_pm_domain *genpd,
			       struct device *dev)
{
	struct cpg_mssr_clk_domain *pd =
		container_of(genpd, struct cpg_mssr_clk_domain, genpd);
	struct device_node *np = dev->of_node;
	struct of_phandle_args clkspec;
	struct clk *clk;
	int i = 0;
	int error;

	while (!of_parse_phandle_with_args(np, "clocks", "#clock-cells", i,
					   &clkspec)) {
		if (cpg_mssr_is_pm_clk(&clkspec, pd))
			goto found;

		of_node_put(clkspec.np);
		i++;
	}

	return 0;

found:
	clk = of_clk_get_from_provider(&clkspec);
	of_node_put(clkspec.np);

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	error = pm_clk_create(dev);
	if (error) {
		dev_err(dev, "pm_clk_create failed %d\n", error);
		goto fail_put;
	}

	error = pm_clk_add_clk(dev, clk);
	if (error) {
		dev_err(dev, "pm_clk_add_clk %pC failed %d\n", clk, error);
		goto fail_destroy;
	}

	return 0;

fail_destroy:
	pm_clk_destroy(dev);
fail_put:
	clk_put(clk);
	return error;
}

static void cpg_mssr_detach_dev(struct generic_pm_domain *genpd,
				struct device *dev)
{
	if (!list_empty(&dev->power.subsys_data->clock_list))
		pm_clk_destroy(dev);
}

static int __init cpg_mssr_add_clk_domain(struct device *dev,
					  const unsigned int *core_pm_clks,
					  unsigned int num_core_pm_clks)
{
	struct device_node *np = dev->of_node;
	struct generic_pm_domain *genpd;
	struct cpg_mssr_clk_domain *pd;
	size_t pm_size = num_core_pm_clks * sizeof(core_pm_clks[0]);

	pd = devm_kzalloc(dev, sizeof(*pd) + pm_size, GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->np = np;
	pd->num_core_pm_clks = num_core_pm_clks;
	memcpy(pd->core_pm_clks, core_pm_clks, pm_size);

	genpd = &pd->genpd;
	genpd->name = np->name;
	genpd->flags = GENPD_FLAG_PM_CLK;
	pm_genpd_init(genpd, &simple_qos_governor, false);
	genpd->attach_dev = cpg_mssr_attach_dev;
	genpd->detach_dev = cpg_mssr_detach_dev;

	of_genpd_add_provider_simple(np, genpd);
	return 0;
}
#else
static inline int cpg_mssr_add_clk_domain(struct device *dev,
					  const unsigned int *core_pm_clks,
					  unsigned int num_core_pm_clks) {}
#endif /* !CONFIG_PM_GENERIC_DOMAINS_OF */


static const struct of_device_id cpg_mssr_match[] = {
	{ /* sentinel */ }
};

static void cpg_mssr_del_clk_provider(void *data)
{
        of_clk_del_provider(data);
}

static int __init cpg_mssr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct cpg_mssr_info *info;
	struct cpg_mssr_priv *priv;
	unsigned int nclks, i;
	struct resource *res;
	struct clk **clks;
	int error;

	info = of_match_node(cpg_mssr_match, np)->data;
	if (info->init) {
		error = info->init(dev);
		if (error)
			return error;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	nclks = info->num_total_core_clks + info->num_hw_mod_clks;
	clks = devm_kmalloc_array(dev, nclks, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	spin_lock_init(&priv->mstp_lock);
	priv->clks = clks;
	priv->num_core_clks = info->num_total_core_clks;
	priv->last_dt_core_clk = info->last_dt_core_clk;
	priv->num_mod_clks = info->num_hw_mod_clks;

	for (i = 0; i < nclks; i++)
		clks[i] = ERR_PTR(-ENOENT);

	for (i = 0; i < info->num_core_clks; i++)
		cpg_mssr_register_core_clk(np, &info->core_clks[i], info,
					   priv);

	for (i = 0; i < info->num_mod_clks; i++)
		cpg_mssr_register_mod_clk(&info->mod_clks[i], info, priv);

	error = of_clk_add_provider(np, cpg_mssr_clk_src_twocell_get, priv);
	if (error)
		return error;

	devm_add_action(dev, cpg_mssr_del_clk_provider, np);

	error = cpg_mssr_add_clk_domain(dev, info->core_pm_clks,
					info->num_core_pm_clks);
	if (error)
		return error;

	return 0;
}

static struct platform_driver cpg_mssr_driver = {
	.driver		= {
		.name	= "clk-cpg-mssr",
		.of_match_table = cpg_mssr_match,
	},
};

static int __init cpg_mssr_init(void)
{
	return platform_driver_probe(&cpg_mssr_driver, cpg_mssr_probe);
}

subsys_initcall(cpg_mssr_init);

MODULE_DESCRIPTION("Renesas CPG/MSSR Driver");
MODULE_LICENSE("GPL v2");
