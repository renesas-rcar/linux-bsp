/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * rcar_rvgc_drv.h  --  R-Car Display Unit DRM driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#ifndef __RCAR_RVGC_DRV_H__
#define __RCAR_RVGC_DRV_H__

#include <linux/kernel.h>
#include "rcar_rvgc_pipe.h"

struct taurus_rvgc_res_msg;

struct taurus_event_list {
	uint32_t id;
	struct taurus_rvgc_res_msg* result;
	struct list_head list;
	struct completion ack;
	bool ack_received;
	struct completion completed;
};

struct rcar_rvgc_device {
	struct device* dev;

	struct drm_device* ddev;

	struct rpmsg_device* rpdev;

	unsigned int nr_rvgc_pipes;
	struct rcar_rvgc_pipe* rvgc_pipes;

	/* needed for taurus configuration */
	uint8_t vblank_pending;
	wait_queue_head_t vblank_pending_wait_queue;

	/* needed for drm communication */
	wait_queue_head_t vblank_enable_wait_queue;
	atomic_t global_vblank_enable;

	struct task_struct* vsync_thread;

	struct list_head taurus_event_list_head;
	rwlock_t event_list_lock;

	bool update_primary_plane;
};

#endif /* __RCAR_RVGC_DRV_H__ */
