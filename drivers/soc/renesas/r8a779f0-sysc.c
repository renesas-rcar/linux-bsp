// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car S4 System Controller
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 */

#include <linux/kernel.h>

#include <dt-bindings/power/r8a779f0-sysc.h>

#include "rcar-sysc.h"

static const struct rcar_sysc_area r8a779f0_areas[] __initconst = {
	{ "always-on",     0, 0, R8A779F0_PD_ALWAYS_ON, -1, PD_ALWAYS_ON },
};


const struct rcar_sysc_info r8a779f0_sysc_info __initconst = {
	.areas = r8a779f0_areas,
	.num_areas = ARRAY_SIZE(r8a779f0_areas),
};
