/*
 * Renesas R-Car E2 System Controller
 *
 * Copyright (C) 2016 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/bug.h>
#include <linux/kernel.h>

#include <dt-bindings/power/r8a7794-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a7794_areas[] __initconst = {
	{ "ca7-scu",	0x100, 0, R8A7794_PD_CA7_SCU,	-1,
	  PD_SCU },
	{ "ca7-cpu0",	0x1c0, 0, R8A7794_PD_CA7_CPU0,	R8A7794_PD_CA7_SCU,
	  PD_CPU_NOCR, },
	{ "ca7-cpu1",	0x1c0, 1, R8A7794_PD_CA7_CPU1,	R8A7794_PD_CA7_SCU,
	  PD_CPU_NOCR, },
	{ "sh",		 0x80, 0, R8A7794_PD_SH,	-1, },
	{ "sgx",	 0xc0, 0, R8A7794_PD_SGX,	-1, },
};

const struct rcar_sysc_info r8a7794_sysc_info __initconst = {
	.areas = r8a7794_areas,
	.num_areas = ARRAY_SIZE(r8a7794_areas),
};
