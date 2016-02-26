/*
 * Renesas R-Car H1 System Controller
 *
 * Copyright (C) 2016 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/bug.h>
#include <linux/kernel.h>

#include <dt-bindings/power/r8a7779-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a7779_areas[] __initconst = {
	{ "arm1",	 0x40, 1, R8A7779_PD_ARM1,	-1, PD_CPU_CR, },
	{ "arm2",	 0x40, 2, R8A7779_PD_ARM2,	-1, PD_CPU_CR, },
	{ "arm3",	 0x40, 3, R8A7779_PD_ARM3,	-1, PD_CPU_CR, },
	{ "sh",		 0x80, 0, R8A7779_PD_SH,	-1, },
	{ "sgx",	 0xc0, 0, R8A7779_PD_SGX,	-1, },
	{ "vdp",	0x100, 0, R8A7779_PD_VDP,	-1, },
	{ "imp",	0x140, 0, R8A7779_PD_IMP,	-1, },
};

const struct rcar_sysc_info r8a7779_sysc_info __initconst = {
	.areas = r8a7779_areas,
	.num_areas = ARRAY_SIZE(r8a7779_areas),
};
