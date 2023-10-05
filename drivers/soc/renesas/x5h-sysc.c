// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car X5H System Controller
 *
 * Copyright (C) 2016-2017 Glider bvba
 * Copyright (C) 2023 Renesas Electronics Corp
 */

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/sys_soc.h>

#include <dt-bindings/power/x5h-sysc.h>

#include "rcar-gen4-sysc.h"

static struct rcar_gen4_sysc_area x5h_areas[] __initdata = {
	{ "always-on", X5H_PD_ALWAYS_ON, -1, PD_ALWAYS_ON },
};

const struct rcar_gen4_sysc_info x5h_sysc_info __initconst = {
	.areas = x5h_areas,
	.num_areas = ARRAY_SIZE(x5h_areas),
};
