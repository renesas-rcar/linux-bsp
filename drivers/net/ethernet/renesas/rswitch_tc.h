/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2026 Renesas Electronics Corporation
 */

#ifndef __RSWITCH_TC_H__
#define __RSWITCH_TC_H__

int rswitch_setup_tc(struct net_device *ndev, enum tc_setup_type type,
		     void *type_data);

#endif	/* #ifndef __RSWITCH_TC_H__ */
