/*
 * Renesas R-Car Product Register (PRR) helpers
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Gereral Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef __SOC_RCAR_PRR_H
#define __SOC_RCAR_PRR_H

#include <linux/types.h>
#include <linux/io.h>

#define PRR				(0xfff00044ul) /* Product Register */

/* PRR PRODUCT for RCAR */
#define PRR_PRODUCT_RCAR_H3		(0x4f00ul)
#define PRR_PRODUCT_RCAR_M3		(0x5200ul)
#define PRR_PRODUCT_MASK		(0x7f00ul)

/* PRR PRODUCT and CUT for RCAR */
#define PRR_PRODUCT_CUT_RCAR_H3_WS10	(PRR_PRODUCT_RCAR_H3   | 0x00ul)
#define PRR_PRODUCT_CUT_RCAR_H3_WS11	(PRR_PRODUCT_RCAR_H3   | 0x01ul)
#define PRR_PRODUCT_CUT_RCAR_M3_ES10	(PRR_PRODUCT_RCAR_M3   | 0x00ul)
#define PRR_PRODUCT_CUT_MASK		(PRR_PRODUCT_MASK      | 0xfful)

#define RCAR_PRR_INIT()			rcar_prr_init()

#define RCAR_PRR_IS_PRODUCT(a) \
		rcar_prr_compare_product(PRR_PRODUCT_RCAR_##a)

#define RCAR_PRR_CHK_CUT(a, b) \
		rcar_prr_check_product_cut(PRR_PRODUCT_CUT_RCAR_##a##_##b)

static u32 rcar_prr = 0xffffffff;

static inline int rcar_prr_compare_product(u32 id)
{
	return (rcar_prr & PRR_PRODUCT_MASK) == (id & PRR_PRODUCT_MASK);
}

static inline int rcar_prr_check_product_cut(u32 id)
{
	return (rcar_prr & PRR_PRODUCT_CUT_MASK) - (id & PRR_PRODUCT_CUT_MASK);
}

static inline int rcar_prr_init(void)
{
	void __iomem *reg;

	reg = ioremap(PRR, 0x04);
	if (!reg)
		return -ENOMEM;

	rcar_prr = ioread32(reg);
	iounmap(reg);

	return 0;
}
#endif /* __SOC_RCAR_PRR_H */
