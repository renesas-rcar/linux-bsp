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

struct rcar_sysc_domain_data;

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

/* Print registers of each power domain */
#define RCAR_SYSC_PWD_REGISTERS(pd, t, log) ({ \
	if ((pd)) {	\
		pr_debug("%s(), pd: %s: <%s %s> PWRSR=%x, PWRONSR=%x", \
		__func__, (pd)->genpd.name, \
		(t), (log), \
		read_reg32((pd)->base + PWRSR), \
		read_reg32((pd)->base + PWRONSR)); \
		\
		pr_debug(", PWROFFSR=%x, PWRER=%x\n", \
		read_reg32((pd)->base + PWROFFSR), \
		read_reg32((pd)->base + PWRER)); \
		} \
})

/* Print common registers of SYSC
*/
#define RCAR_SYSC_CMN_REGISTERS(pd, t, log)  ({ \
	if ((pd))	{ \
		pr_debug("%s(), pd: %s: <%s %s> SYSCSR=%x, SYSCISR=%x",	\
		__func__, (pd)->genpd.name,	\
		(t), (log),	\
		read_reg32((sysc_base) + SYSCSR),	\
		read_reg32((sysc_base) + SYSCISR));	\
		\
		pr_debug("SYSCIER=%x, SYSCIMR=%x\n",	\
		read_reg32((sysc_base) + SYSCIER),	\
		read_reg32((sysc_base) + SYSCIMR));	\
	} \
})

#endif /* RCAR_PM_SYSC_H */
