/*
 * Copyright (C) 2016 Glider bvba
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef __DT_BINDINGS_POWER_R8A7793_SYSC_H__
#define __DT_BINDINGS_POWER_R8A7793_SYSC_H__

/*
 * These power domain indices match the numbers of the interrupt bits
 * representing the power areas in the various Interrupt Registers
 * (e.g. SYSCISR, Interrupt Status Register)
 *
 * R-Car M2-N is identical to R-Car M2-W w.r.t. power domains.
 */

#include "r8a7791-sysc.h"

#define R8A7793_PD_CA15_CPU0		R8A7791_PD_CA15_CPU0
#define R8A7793_PD_CA15_CPU1		R8A7791_PD_CA15_CPU1
#define R8A7793_PD_CA15_SCU		R8A7791_PD_CA15_SCU
#define R8A7793_PD_SH			R8A7791_PD_SH
#define R8A7793_PD_SGX			R8A7791_PD_SGX

#endif /* __DT_BINDINGS_POWER_R8A7793_SYSC_H__ */
