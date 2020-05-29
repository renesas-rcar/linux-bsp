/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015-2018, Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef OPTEE_RCAR_H
#define OPTEE_RCAR_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/wait.h>

#include "rcar_version.h"

#define OPTEE_MSG_RPC_CMD_DEBUG_LOG	(0x3F000000U)

struct rcar_debug_log_info {
	wait_queue_head_t waitq;
	struct list_head queue;
	spinlock_t q_lock; /* queue lock */
};

struct rcar_debug_log_node {
	struct list_head list;
	char *logmsg;
};

void handle_rpc_func_cmd_debug_log(struct optee_msg_arg *arg);
int optee_rcar_probe(struct optee *optee);
void optee_rcar_remove(void);

#endif /* OPTEE_RCAR_H */
