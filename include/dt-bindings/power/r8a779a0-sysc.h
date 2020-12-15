/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Renesas Electronics Corp.
 */
#ifndef __DT_BINDINGS_POWER_R8A779A0_SYSC_H__
#define __DT_BINDINGS_POWER_R8A779A0_SYSC_H__

/*
 * These power domain indices match the numbers of the interrupt bits
 * representing the power areas in the various Interrupt Registers
 * (e.g. SYSCISCR0/1, Interrupt Status/Clear Register0/1)
 */

#define R8A779A0_PD_CA76_CPU0					0
#define R8A779A0_PD_CA76_CPU1					1
#define R8A779A0_PD_CA76_CPU2					2
#define R8A779A0_PD_CA76_CPU3					3
#define R8A779A0_PD_CA76_CPU4					4
#define R8A779A0_PD_CA76_CPU5					5
#define R8A779A0_PD_CA76_CPU6					6
#define R8A779A0_PD_CA76_CPU7					7
#define R8A779A0_PD_A2E0D0						16
#define R8A779A0_PD_A2E0D1						17
#define R8A779A0_PD_A2E1D0						18
#define R8A779A0_PD_A2E1D1						19
#define R8A779A0_PD_CPU_DCLS0					20
#define R8A779A0_PD_CPU_DCLS1					21
#define R8A779A0_PD_3DG_A						24
#define R8A779A0_PD_3DG_B						25
#define R8A779A0_PD_A1CNN2						32
#define R8A779A0_PD_A1DSP0						33
#define R8A779A0_PD_A2IMP01						34
#define R8A779A0_PD_A2DP0						35
#define R8A779A0_PD_A2CV0						36
#define R8A779A0_PD_A2CV1						37
#define R8A779A0_PD_A2CV4						38
#define R8A779A0_PD_A2CV6						39
#define R8A779A0_PD_A2CN2						40
#define R8A779A0_PD_A1CNN0						41
#define R8A779A0_PD_A2CN0						42
#define R8A779A0_PD_A3IR						43
#define R8A779A0_PD_A1CNN1						44
#define R8A779A0_PD_A1DSP1						45
#define R8A779A0_PD_A2IMP23						46
#define R8A779A0_PD_A2DP1						47
#define R8A779A0_PD_A2CV2						48
#define R8A779A0_PD_A2CV3						49
#define R8A779A0_PD_A2CV5						50
#define R8A779A0_PD_A2CV7						51
#define R8A779A0_PD_A2CN1						52
#define R8A779A0_PD_A3VIP0						56
#define R8A779A0_PD_A3VIP1						57
#define R8A779A0_PD_A3VIP2						58
#define R8A779A0_PD_A3VIP3						59
#define R8A779A0_PD_A3ISP01						60
#define R8A779A0_PD_A3ISP23						61

/* Always-on power area */
#define R8A779A0_PD_ALWAYS_ON					64

#endif /* __DT_BINDINGS_POWER_R8A779A0_SYSC_H__ */
