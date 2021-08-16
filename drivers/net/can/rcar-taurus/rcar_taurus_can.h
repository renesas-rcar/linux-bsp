#ifndef __RCAR_TAURUS_CAN_H__
#define __RCAR_TAURUS_CAN_H__

#include <linux/can/dev.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include "r_taurus_can_protocol.h"

#define NUM_RCAR_TAURUS_CAN_CHANNELS	2

struct taurus_event_list {
	uint32_t id;
	struct taurus_can_res_msg *result;
	struct list_head list;
	struct completion ack;
	bool ack_received;
	struct completion completed;
};

struct rcar_taurus_can_channel;

struct rcar_taurus_can_queued_tx_msg {
	struct taurus_can_cmd_msg cmd;
	int (*send)(struct rcar_taurus_can_channel*, struct rcar_taurus_can_queued_tx_msg *, struct taurus_can_res_msg*);
};

struct task_struct;

struct rcar_taurus_can_channel {
	struct can_priv can;	/* Must be the first member! */
	struct net_device *ndev;
	struct napi_struct napi;
	struct rcar_taurus_can_drv *parent;
	unsigned int ch_id;

	struct list_head taurus_event_list_head;
	rwlock_t event_list_lock;

	/* Queue, locks and thread for outgoing messages */
	uint32_t tx_head;
	uint32_t tx_tail;
	uint32_t tx_buf_size; /* must be power of 2 */
	struct rcar_taurus_can_queued_tx_msg *tx_buf;

	spinlock_t tx_buf_producer_lock;
	struct mutex tx_buf_consumer_lock;

	struct task_struct *tx_thread;
	wait_queue_head_t tx_wait_queue;
	int tx_data_avail;


	/* Queue, locks and thread for incoming messages */
	uint32_t rx_head;
	uint32_t rx_tail;
	uint32_t rx_buf_size; /* must be power of 2 */
	struct taurus_can_res_msg *rx_buf;

	spinlock_t rx_buf_consumer_lock;
	struct mutex rx_buf_producer_lock;

	struct task_struct *rx_thread;
	wait_queue_head_t rx_wait_queue;
	int rx_data_avail;
};

struct rpmsg_device;

struct rcar_taurus_can_drv {
	struct rpmsg_device *rpdev;
	struct rcar_taurus_can_channel *channels[NUM_RCAR_TAURUS_CAN_CHANNELS];
};

#endif
