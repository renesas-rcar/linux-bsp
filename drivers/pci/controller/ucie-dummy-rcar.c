// SPDX-License-Identifier: GPL-2.0+
/*
 * Common UCIe driver for Renesas R-Car SoCs
 *  Copyright (C) 2025 Renesas Electronics Corp.
 *
 * Author: Based on R-Car PCIe driver architecture
 *
 * This module provides basic hardware register access functions shared
 * between Host and Endpoint modes. Following R-Car PCIe architecture,
 * business logic belongs in the host/endpoint drivers, not here.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "ucie-dummy-rcar.h"

/*
 * Write 32-bit value to UCIe register
 * Simple passthrough to hardware - no business logic
 */
void ucie_write_reg(struct ucie_dummy *ucie, u32 val, unsigned int reg)
{
	writel(val, ucie->base + reg);
}
EXPORT_SYMBOL_GPL(ucie_write_reg);

/*
 * Read 32-bit value from UCIe register
 * Simple passthrough from hardware - no business logic
 */
u32 ucie_read_reg(struct ucie_dummy *ucie, unsigned int reg)
{
	return readl(ucie->base + reg);
}
EXPORT_SYMBOL_GPL(ucie_read_reg);

/*
 * Read-Modify-Write operation on 32-bit register
 * Helper function for bit manipulation
 */
void ucie_rmw32(struct ucie_dummy *ucie, int where, u32 mask, u32 data)
{
	unsigned int shift = BITS_PER_BYTE * (where & 3);
	u32 val = ucie_read_reg(ucie, where & ~3);

	val &= ~(mask << shift);
	val |= data << shift;

	/* Direct write to avoid any hooks */
	writel(val, ucie->base + (where & ~3));
}
EXPORT_SYMBOL_GPL(ucie_rmw32);

MODULE_DESCRIPTION("Common UCIe driver for Renesas R-Car SoCs");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hau Vo");
