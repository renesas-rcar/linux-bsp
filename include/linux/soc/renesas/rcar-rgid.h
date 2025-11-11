/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_RGID_H
#define _LINUX_RGID_H

#include <linux/ctype.h>
#include <linux/of_address.h>

#define ADDR_RGID(a)			(((a) & 0xFULL) << 36ULL)
#define ADDR_RGID_MASK			(0x000000F000000000ULL)
#define ADDR_PA_MASK			(0x0000000FFFFFFFFFULL)
#define ADDR_ASSIGN_RGID(a, b)		(((a) & ADDR_PA_MASK) | (ADDR_RGID(b) & ADDR_RGID_MASK))
#define REMOVE_RGID(a)			((a) &= ~(15UL << 36))

static inline bool check_cmem_others_node(const char *name)
{
    struct device_node *np, *cmem_np;
    const char *region_name;
    int len = strlen(name);
    int i = len - 1;
    int node;

    // Walk backwards to find where the number starts
    while (i >= 0 && isdigit(name[i])) {
        i--;
    }

    // Convert numeric suffix to integer
    if (kstrtoint(&name[i + 1], 10, &node) != 0)
        return false;

    np = of_find_node_by_path("/cmem");
    if (!np)
        return false;

    cmem_np = of_parse_phandle(np, "memory-region", node);
    if (!cmem_np)
        return false;

    region_name = cmem_np->name;

    return region_name && strstr(region_name, CONFIG_CMEM_NODE);
}

#endif /* _LINUX_RGID_H */
