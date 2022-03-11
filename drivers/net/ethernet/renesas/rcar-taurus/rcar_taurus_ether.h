/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __RCAR_TAURUS_ETHER_H__
#define __RCAR_TAURUS_ETHER_H__

#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include "r_taurus_ether_protocol.h"

#define NUM_RCAR_TAURUS_ETH_CHANNELS	1
#define NUM_TX_QUEUE			1
#define NUM_RX_QUEUE			1

#define RCT_TIMEOUT_MS		1000

#define ETH_MODE_ACTIVE		1
#define ETH_MODE_DOWN		0

#define ETH_MAC_HEADER_LEN	14
#define ETH_CRC_CHKSUM_LEN	4

#define PKT_BUF_SZ		1584
#define RCT_ETH_ALIGN		128

struct taurus_event_list {
	u32 id;
	struct taurus_ether_res_msg *result;
	struct list_head list;
	struct completion ack;
	bool ack_received;
	struct completion completed;
};

struct rcar_taurus_ether_channel;

struct rcar_taurus_ether_channel {
	struct net_device *ndev;
	struct rcar_taurus_ether_drv *parent;
	u32 ch_id;

	struct list_head taurus_event_list_head;
	rwlock_t event_list_lock;

	struct mii_bus *mii;

};

struct rpmsg_device;

struct rcar_taurus_ether_drv {
	struct rpmsg_device *rpdev;
	struct rcar_taurus_ether_channel *channels[NUM_RCAR_TAURUS_ETH_CHANNELS];
};

#endif
