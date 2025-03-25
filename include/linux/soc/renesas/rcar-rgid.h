/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_RGID_H
#define _LINUX_RGID_H

#define ADDR_RGID(a)			(((a) & 0xFULL) << 36ULL)
#define ADDR_RGID_MASK			(0x000000F000000000ULL)
#define ADDR_PA_MASK			(0x0000000FFFFFFFFFULL)
#define ADDR_ASSIGN_RGID(a, b)		(((a) & ADDR_PA_MASK) | (ADDR_RGID(b) & ADDR_RGID_MASK))

#define REMOVE_RGID(a)			((a) &= ~(15UL << 36))

#endif /* _LINUX_RGID_H */
