/*
 * Renesas R-Car Power Domains Control driver
 * /include/linux/soc/renesas/rcar_pm_sysc.h
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Gereral Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */
#ifndef RCAR_PM_SYSC_H
#define RCAR_PM_SYSC_H

#include <linux/pm_domain.h>

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

#define DEF_DM_DATA(_name, _isrs, _pwrsr_on, \
	_pwrsr_off, _onoff_sr, _onoff_cr, _pwrer) \
	{ \
	.name = (_name), \
	.syscisrs = (u32)(_isrs), \
	.pwrsr_on = (u32)(_pwrsr_on), \
	.pwrsr_off = (u32)(_pwrsr_off), \
	.pwr_on_off_sr = (u32)(_onoff_sr), \
	.pwr_on_off_cr = (u32)(_onoff_cr), \
	.pwrer = (u32)(_pwrer), \
	}

/* Structure of platform power domains */
struct rcar_sysc_domain {
	/* appropriate generic power domain */
	struct generic_pm_domain genpd;
	struct dev_power_governor *gov;
	/* base register of power domain (= SYSC base + offset of pd) */
	void __iomem *base;
	/* specific info of power domain */
	const struct rcar_sysc_domain_data *dm_data;
};

struct rcar_sysc_domains_info {
	const struct rcar_sysc_domain_data *domains_list;
	const unsigned int len; /* length of list power domain data */
};

#endif /* RCAR_PM_SYSC_H */
