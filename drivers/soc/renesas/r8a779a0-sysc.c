// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car V3U System Controller
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/sys_soc.h>

#include <dt-bindings/power/r8a779a0-sysc.h>

#include "rcar-sysc.h"

static struct rcar_sysc_area r8a779a0_areas[] __initdata = {
	{ "always-on",      0, 0, R8A779A0_PD_ALWAYS_ON, -1, PD_ALWAYS_ON },
	{ "a3e0",   0x1500, 0, R8A779A0_PD_CPU_DCLS0,  R8A779A0_PD_ALWAYS_ON,
	PD_SCU},
	{ "a3e1",  0x1540, 0, R8A779A0_PD_CPU_DCLS1, R8A779A0_PD_ALWAYS_ON,
	PD_SCU},
	{ "3dg-a",  0x1600, 0, R8A779A0_PD_3DG_A,      R8A779A0_PD_ALWAYS_ON },
	{ "a3vip0",        0x1E00, 0, R8A779A0_PD_A3VIP0,      R8A779A0_PD_ALWAYS_ON },
	{ "a3vip1",       0x1E40, 0, R8A779A0_PD_A3VIP1,     R8A779A0_PD_ALWAYS_ON },
	{ "a3vip3",      0x1EC0, 0, R8A779A0_PD_A3VIP3,    R8A779A0_PD_ALWAYS_ON },
	{ "a3vip2",      0x1E80, 0, R8A779A0_PD_A3VIP2,    R8A779A0_PD_ALWAYS_ON },
	{ "a3isp01",      0x1F00, 0, R8A779A0_PD_A3ISP01,    R8A779A0_PD_ALWAYS_ON },
	{ "a3isp23",      0x1F40, 0, R8A779A0_PD_A3ISP23,    R8A779A0_PD_ALWAYS_ON },
	{ "a3ir",      0x1AC0, 0, R8A779A0_PD_A3IR,   R8A779A0_PD_ALWAYS_ON },
	{ "a2e0d0",      0x1400, 0, R8A779A0_PD_A2E0D0,    R8A779A0_PD_CPU_DCLS0,
	PD_SCU},
	{ "a2e0d1",      0x1440, 0, R8A779A0_PD_A2E0D1,    R8A779A0_PD_CPU_DCLS0,
	PD_SCU},
	{ "a2e1d0",      0x1480, 0, R8A779A0_PD_A2E1D0,    R8A779A0_PD_CPU_DCLS1,
	PD_SCU},
	{ "a2e1d1",      0x14C0, 0, R8A779A0_PD_A2E1D1,   R8A779A0_PD_CPU_DCLS1,
	PD_SCU},
	{ "3dg-b",      0x1640, 0, R8A779A0_PD_3DG_B,   R8A779A0_PD_3DG_A },
	{ "a2cn0",      0x1A80, 0, R8A779A0_PD_A2CN0,    R8A779A0_PD_A3IR },
	{ "a2imp01",      0x1880, 0, R8A779A0_PD_A2IMP01,    R8A779A0_PD_A3IR },
	{ "a2dp0",      0x18C0, 0, R8A779A0_PD_A2DP0,    R8A779A0_PD_A3IR },
	{ "a2cv0",      0x1900, 0, R8A779A0_PD_A2CV0,   R8A779A0_PD_A3IR },
	{ "a2cv1",      0x1940, 0, R8A779A0_PD_A2CV1,   R8A779A0_PD_A3IR },
	{ "a2cv4",      0x1980, 0, R8A779A0_PD_A2CV4,   R8A779A0_PD_A3IR },
	{ "a2cv6",      0x19C0, 0, R8A779A0_PD_A2CV6,   R8A779A0_PD_A3IR },
	{ "a2cn2",      0x1A00, 0, R8A779A0_PD_A2CN2,    R8A779A0_PD_A3IR },
	{ "a2imp23",      0x1B80, 0, R8A779A0_PD_A2IMP23,    R8A779A0_PD_A3IR },
	{ "a2dp1",      0x1BC0, 0, R8A779A0_PD_A2DP1,    R8A779A0_PD_A3IR },
	{ "a2cv2",      0x1C00, 0, R8A779A0_PD_A2CV2,   R8A779A0_PD_A3IR },
	{ "a2cv3",      0x1C40, 0, R8A779A0_PD_A2CV3,   R8A779A0_PD_A3IR },
	{ "a2cv5",      0x1C80, 0, R8A779A0_PD_A2CV5,   R8A779A0_PD_A3IR },
	{ "a2cv7",      0x1CC0, 0, R8A779A0_PD_A2CV7,   R8A779A0_PD_A3IR },
	{ "a2cn1",      0x1D00, 0, R8A779A0_PD_A2CN1,    R8A779A0_PD_A3IR },
	{ "a1e0d0c0",      0x1000, 0, R8A779A0_PD_CA76_CPU0,   R8A779A0_PD_A2E0D0,
	PD_CPU_NOCR},
	{ "a1e0d0c1",      0x1040, 0, R8A779A0_PD_CA76_CPU1,   R8A779A0_PD_A2E0D0,
	PD_CPU_NOCR},
	{ "a1e0d1c0",      0x1080, 0, R8A779A0_PD_CA76_CPU2,    R8A779A0_PD_A2E0D1,
	PD_CPU_NOCR},
	{ "a1e0d1c1",      0x10C0, 0, R8A779A0_PD_CA76_CPU3,    R8A779A0_PD_A2E0D1,
	PD_CPU_NOCR},
	{ "a1e1d0c0",      0x1100, 0, R8A779A0_PD_CA76_CPU4,    R8A779A0_PD_A2E1D0,
	PD_CPU_NOCR},
	{ "a1e1d0c1",      0x1140, 0, R8A779A0_PD_CA76_CPU5,    R8A779A0_PD_A2E1D0,
	PD_CPU_NOCR},
	{ "a1e1d1c0",      0x1180, 0, R8A779A0_PD_CA76_CPU6,    R8A779A0_PD_A2E1D1,
	PD_CPU_NOCR},
	{ "a1e1d1c1",      0x11C0, 0, R8A779A0_PD_CA76_CPU7,    R8A779A0_PD_A2E1D1,
	PD_CPU_NOCR},
	{ "a1cnn0",      0x1A40, 0, R8A779A0_PD_A1CNN0,    R8A779A0_PD_A2CN0 },
	{ "a1cnn2",      0x1800, 0, R8A779A0_PD_A1CNN2,    R8A779A0_PD_A2CN2 },
	{ "a1dsp0",      0x1840, 0, R8A779A0_PD_A1DSP0,    R8A779A0_PD_A2CN2 },
	{ "a1cnn1",      0x1B00, 0, R8A779A0_PD_A1CNN1,    R8A779A0_PD_A2CN1 },
	{ "a1dsp1",      0x1B40, 0, R8A779A0_PD_A1DSP1,    R8A779A0_PD_A2CN1 },
};

const struct rcar_sysc_info r8a779a0_sysc_info __initconst = {
	.areas = r8a779a0_areas,
	.num_areas = ARRAY_SIZE(r8a779a0_areas),
};
