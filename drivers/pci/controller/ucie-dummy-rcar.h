/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Dummy UCIe driver for Renesas R-Car SoCs
 *  Copyright (C) 2025 Renesas Electronics Corp.
 *
 * Based on R-Car PCIe driver
 */

#ifndef _UCIE_DUMMY_RCAR_H
#define _UCIE_DUMMY_RCAR_H

#include <linux/pci.h>

/* UCIe Register Offsets (from ucie.adoc spec) */
#define UCIE_UPPER_SRC_ADDR	0x50
#define UCIE_LOWER_SRC_ADDR	0x54
#define UCIE_UPPER_DST_ADDR	0x58
#define UCIE_LOWER_DST_ADDR	0x5C
#define UCIE_MAPPING_SIZE	0x60
#define UCIE_MAPPING_EN		0x64
#define UCIE_CLR_INT		0x70
#define UCIE_INT_STS		0x74
#define UCIE_RQ_SYNC_TO		0x78
#define UCIE_RQ_SYNC_FROM	0x7C
#define UCIE_MAPPING_CLR	0x80

/* UCIE_MAPPING_EN register bits */
#define MAPPING_FILE_STS	BIT(2)
#define MAPPING_FILE_EN		BIT(1)
#define MAPPING_EN		BIT(0)

/* UCIE_MAPPING_CLR register bits */
#define MAPPING_FILE_CLR	BIT(0)

/* UCIE_RQ_SYNC_TO/FROM register bits */
#define CLR_STS			BIT(2)
#define SYNC_STS		BIT(1)
#define STR_SYNC		BIT(0)

/* UCIe Interrupt bits (0-15) */
#define INT0_DMA_EVENT		BIT(0)
#define INT1_DMA_EVENT		BIT(1)
#define INT2_DMA_EVENT		BIT(2)
#define INT3_DMA_EVENT		BIT(3)
#define INT4_DMA_EVENT		BIT(4)
#define INT5_DMA_EVENT		BIT(5)
#define INT6_DMA_EVENT		BIT(6)
#define INT7_DMA_EVENT		BIT(7)
#define INT8_LPERR		BIT(8)
#define INT9_MISC		BIT(9)
#define INT10_STRM_CORERR	BIT(10)
#define INT11_STRM_FATALERR	BIT(11)
#define INT12_STRM_NONFATALERR	BIT(12)
#define INT13_SUB1		BIT(13)
#define INT14_SUB		BIT(14)
#define INT15_VNDMSG		BIT(15)

/* Hardcoded IDs for dummy driver (bypass enumeration) */
#define UCIE_DUMMY_VENDOR_ID	0x1912  /* Renesas */
#define UCIE_DUMMY_DEVICE_ID	0xabcd  /* Dummy UCIe device */

/* Maximum functions supported */
#define UCIE_MAX_FUNCTIONS	1

/* BAR size - 16MB for UCIe shared memory region */
#define UCIE_BAR0_SIZE		0x1000000	/* 16MB (0xdd000000-0xddffffff) */

/* Mapping operation status */
#define UCIE_MAPPING_SUCCESS	0
#define UCIE_MAPPING_BUSY	1
#define UCIE_MAPPING_ERROR	2

/* Structure representing the UCIe controller (common base) */
struct ucie_dummy {
	struct device		*dev;
	void __iomem		*base;
	resource_size_t		bar0_phys;
	resource_size_t		bar0_size;
};

/* Function prototypes for common layer */
void ucie_write_reg(struct ucie_dummy *ucie, u32 val, unsigned int reg);
u32 ucie_read_reg(struct ucie_dummy *ucie, unsigned int reg);
void ucie_rmw32(struct ucie_dummy *ucie, int where, u32 mask, u32 data);

#endif /* _UCIE_DUMMY_RCAR_H */
