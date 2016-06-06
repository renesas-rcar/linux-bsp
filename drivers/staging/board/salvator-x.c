/*
 * Staging board support for Salvator-X.
 *
 * Copyright (C) 2015 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/cma.h>
#include <linux/dma-contiguous.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include "board.h"
#include "../../../mm/cma.h"

struct cma *find_largest_nondefault_cma(void)
{
	unsigned long largest_size;
	int k, largest_idx;

	largest_size = 0;
	largest_idx = -ENOENT;

	for (k = 0; k < cma_area_count; k++) {
		if (&cma_areas[k] == dma_contiguous_default_area)
			continue;

		if (cma_get_size(&cma_areas[k]) > largest_size) {
			largest_size = cma_get_size(&cma_areas[k]);
			largest_idx = k;
		}
	}

	if (largest_idx != -ENOENT)
		return &cma_areas[largest_idx];

	return NULL;
}

struct cma *rcar_gen3_dma_contiguous;
EXPORT_SYMBOL(rcar_gen3_dma_contiguous);

static void __init salvator_x_board_staging_init(void)
{
	phys_addr_t cma_base;
	unsigned long cma_size;

	rcar_gen3_dma_contiguous = find_largest_nondefault_cma();

	if (rcar_gen3_dma_contiguous) {
		cma_base = cma_get_base(rcar_gen3_dma_contiguous);
		cma_size = cma_get_size(rcar_gen3_dma_contiguous) / SZ_1M;

		pr_info("%s: Located CMA at %pa, size %ld MiB\n",
			__func__, &cma_base, cma_size);
	}
}

board_staging("renesas,salvator-x", salvator_x_board_staging_init);
