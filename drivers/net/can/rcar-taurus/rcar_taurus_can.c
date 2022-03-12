/*************************************************************************/ /*
 Renesas R-Car Taurus CAN device driver

 Copyright (C) 2021-2022 Renesas Electronics Corporation

 License        Dual MIT/GPLv2

 The contents of this file are subject to the MIT license as set out below.

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 Alternatively, the contents of this file may be used under the terms of
 the GNU General Public License Version 2 ("GPL") in which case the provisions
 of GPL are applicable instead of those above.

 If you wish to allow use of your version of this file only under the terms of
 GPL, and not to allow others to use your version of this file under the terms
 of the MIT license, indicate your decision by deleting the provisions above
 and replace them with the notice and other provisions required by GPL as set
 out in the file called "GPL-COPYING" included in this distribution. If you do
 not delete the provisions above, a recipient may use your version of this file
 under the terms of either the MIT license or GPL.

 This License is also included in this distribution in the file called
 "MIT-COPYING".

 EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


 GPLv2:
 If you wish to use this file under the terms of GPL, following terms are
 effective.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; version 2 of the License.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/ /*************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/can/led.h>
#include <linux/of.h>
#include <linux/circ_buf.h>
#include <linux/rpmsg.h>
#include <linux/kthread.h>
#include "r_taurus_can_protocol.h"
#include "rcar_taurus_can.h"
#include "rcar_taurus_can_conn.h"

#define RCAR_TAURUS_CAN_DRV_NAME	"rcar-taurus-can"
#define RCAR_CAN_NAPI_WEIGHT		4
#define RCAR_CAN_FIFO_DEPTH		4 /* must be power of 2 */

static const struct can_bittiming_const rcar_taurus_can_bittiming_const = {
	.name = RCAR_TAURUS_CAN_DRV_NAME,
	.tseg1_min = 4,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 1024,
	.brp_inc = 1,
};

static int rcar_taurus_can_cb(struct rpmsg_device *rpdev, void *data, int len,
			void *priv, u32 src)
{
	struct rcar_taurus_can_drv *rctcan = dev_get_drvdata(&rpdev->dev);
	struct rcar_taurus_can_channel *channel;
	uint32_t ch_id;

	struct taurus_can_res_msg *res = (struct taurus_can_res_msg*)data;
	uint32_t res_id = res->hdr.Id;

	dev_dbg(&rpdev->dev, "%s():%d\n", __FUNCTION__, __LINE__);

	if ((res->hdr.Result == R_TAURUS_CMD_NOP) && (res_id == 0)) {
		/* This is an asynchronous signal sent from the
		 * peripheral, and not an answer of a previously sent
		 * command. Just process the signal and return.*/

		dev_dbg(&rpdev->dev, "Signal received! Aux = %llx\n", res->hdr.Aux);

		switch (res->hdr.Aux) {
		case CAN_PROTOCOL_EVENT_PKT_AVAIL_CH0:
			/* set_bit(0, (long unsigned int*) &priv->rx_ready); */
			ch_id = 0;
			break;
		case CAN_PROTOCOL_EVENT_PKT_AVAIL_CH1:
			/* set_bit(1, (long unsigned int*) &priv->rx_ready); */
			ch_id = 1;
			break;
		default:
			/* event not recognized */
			return 0;
		}

		channel = rctcan->channels[ch_id];
		WRITE_ONCE(channel->rx_data_avail, 1);

		wake_up_interruptible(&channel->rx_wait_queue);

	} else {
		struct taurus_event_list *event;
		struct list_head *i;

		ch_id = res->hdr.Channel;
		if (ch_id >= NUM_RCAR_TAURUS_CAN_CHANNELS)
			return 0;

		channel = rctcan->channels[ch_id];

		/* Go through the list of pending events and check if this
		 * message matches any */
		read_lock(&channel->event_list_lock);
		list_for_each_prev(i, &channel->taurus_event_list_head) {
			event = list_entry(i, struct taurus_event_list, list);
			if (event->id == res_id) {

				memcpy(event->result, data, len);

				if(event->ack_received) {
					complete(&event->completed);
				} else {
					event->ack_received = 1;
					complete(&event->ack);
				}
				break;
			}
		}
		read_unlock(&channel->event_list_lock);
	}

	return 0;
}

static void rcar_taurus_can_set_bittiming(struct net_device *ndev)
{
	struct rcar_taurus_can_channel *channel = netdev_priv(ndev);
	struct can_bittiming *bt = &channel->can.bittiming;
	(void)bt;
	/* u32 bcr; */

	/* bcr = RCAR_CAN_BCR_TSEG1(bt->phase_seg1 + bt->prop_seg - 1) | */
	/*       RCAR_CAN_BCR_BPR(bt->brp - 1) | RCAR_CAN_BCR_SJW(bt->sjw - 1) | */
	/*       RCAR_CAN_BCR_TSEG2(bt->phase_seg2 - 1); */
	/* /\* Don't overwrite CLKR with 32-bit BCR access; CLKR has 8-bit access. */
	/*  * All the registers are big-endian but they get byte-swapped on 32-bit */
	/*  * read/write (but not on 8-bit, contrary to the manuals)... */
	/*  *\/ */
	/* writel((bcr << 8) | priv->clock_select, &priv->regs->bcr); */
}

static void rcar_taurus_can_start(struct net_device *ndev)
{
	struct rcar_taurus_can_channel *channel = netdev_priv(ndev);

	rcar_taurus_can_set_bittiming(ndev);
	channel->can.state = CAN_STATE_ERROR_ACTIVE;
}

static int rcar_taurus_can_open(struct net_device *ndev)
{
	struct rcar_taurus_can_channel *channel = netdev_priv(ndev);
	struct rcar_taurus_can_drv *rctcan = channel->parent;
	struct taurus_can_res_msg res_msg;
	int err;

	err = rcar_taurus_can_conn_open(rctcan,
					channel->ch_id,
					&res_msg);
	if (err) {
		netdev_err(ndev, "rcar_taurus_can_conn_open() failed, ch_id = %d, error = %d\n",
			channel->ch_id, err);
		goto out;
	}

	err = open_candev(ndev);
	if (err) {
		netdev_err(ndev, "open_candev() failed, error %d\n", err);
		goto out_close_taurus;
	}
	napi_enable(&channel->napi);
	can_led_event(ndev, CAN_LED_EVENT_OPEN);

	rcar_taurus_can_start(ndev);

	netif_start_queue(ndev);
	return 0;

out_close_taurus:
	rcar_taurus_can_conn_close(rctcan, channel->ch_id, &res_msg);
out:
	return err;
}

static void rcar_taurus_can_stop(struct net_device *ndev)
{
	struct rcar_taurus_can_channel *channel = netdev_priv(ndev);

	channel->can.state = CAN_STATE_STOPPED;
}

static int rcar_taurus_can_close(struct net_device *ndev)
{
	struct rcar_taurus_can_channel *channel = netdev_priv(ndev);
	struct rcar_taurus_can_drv *rctcan = channel->parent;
	struct taurus_can_res_msg res_msg;
	int err;

	netif_stop_queue(ndev);
	rcar_taurus_can_stop(ndev);
	napi_disable(&channel->napi);
	close_candev(ndev);
	can_led_event(ndev, CAN_LED_EVENT_STOP);

	err = rcar_taurus_can_conn_close(rctcan,
					channel->ch_id,
					&res_msg);
	if (err) {
		netdev_err(ndev, "rcar_taurus_can_conn_close() failed, ch_id = %d, error = %d\n",
			channel->ch_id, err);
	}

	return 0;
}

static int rcar_taurus_tx_thread_ep(void *data)
{
	struct rcar_taurus_can_channel *channel = (struct rcar_taurus_can_channel*)data;
	struct net_device *ndev = channel->ndev;
	struct net_device_stats *stats = &ndev->stats;
	unsigned long head;
	unsigned long tail;
	int err;

	while (!kthread_should_stop()) {

		wait_event_interruptible(channel->tx_wait_queue, READ_ONCE(channel->tx_data_avail));
		WRITE_ONCE(channel->tx_data_avail, 0);

		mutex_lock(&channel->tx_buf_consumer_lock);

		/* Read index before reading contents at that index. */
		head = smp_load_acquire(&channel->tx_head);
		tail = channel->tx_tail;

		while (CIRC_CNT(head, tail, channel->tx_buf_size)) {

			/* extract one item from the buffer */
			struct rcar_taurus_can_queued_tx_msg *queued_tx_buf = &channel->tx_buf[tail];

			if (queued_tx_buf->send) {
				struct taurus_can_res_msg res_msg;

				err = queued_tx_buf->send(channel, queued_tx_buf, &res_msg);
				if (err)
					continue;

				can_get_echo_skb(ndev, tail % RCAR_CAN_FIFO_DEPTH);

				stats->tx_packets++;
				stats->tx_bytes += res_msg.params.write.res;

				can_led_event(ndev, CAN_LED_EVENT_TX);

			}

			/* Finish reading descriptor before incrementing tail. */
			smp_store_release(&channel->tx_tail,
					(tail + 1) & (channel->tx_buf_size - 1));

			tail = channel->tx_tail;

			netif_wake_queue(channel->ndev);
		}

		mutex_unlock(&channel->tx_buf_consumer_lock);
	}

	return 0;
}

static int rcar_taurus_rx_thread_ep(void *data)
{
	struct rcar_taurus_can_channel *channel = (struct rcar_taurus_can_channel*)data;
	struct device *dev = &channel->parent->rpdev->dev;
	unsigned long head;
	unsigned long tail;
	int err;

	while (!kthread_should_stop()) {

		wait_event_interruptible(channel->rx_wait_queue, READ_ONCE(channel->rx_data_avail));

		mutex_lock(&channel->rx_buf_producer_lock);

		head = channel->rx_head;
		tail = READ_ONCE(channel->rx_tail);

		while (CIRC_SPACE(head, tail, channel->rx_buf_size)) {

			struct taurus_can_res_msg *queued_rx_msg = &channel->rx_buf[head];

			err =  rcar_taurus_can_conn_read(channel->parent,
							channel->ch_id,
							queued_rx_msg);
			if (err) {
				dev_dbg_ratelimited(dev, "taurus_can_rx_thread: read() failed, ch_id = %d, error = %d\n",
						channel->ch_id, err);
				break;
			}

			if (!queued_rx_msg->params.read.data_len)
				break;

			smp_store_release(&channel->rx_head,
					(head + 1) & (channel->rx_buf_size - 1));

			head = channel->rx_head;

			if (napi_schedule_prep(&channel->napi)) {
				__napi_schedule(&channel->napi);
			}
		}

		WRITE_ONCE(channel->rx_data_avail, 0);

		mutex_unlock(&channel->rx_buf_producer_lock);
	}
	return 0;
}

static netdev_tx_t rcar_taurus_can_start_xmit(struct sk_buff *skb,
					struct net_device *ndev)
{
	struct rcar_taurus_can_channel *channel = netdev_priv(ndev);
	struct rcar_taurus_can_drv *rctcan = channel->parent;
	struct can_frame *cf = (struct can_frame *)skb->data;
	unsigned long flags;
	unsigned long head;
	unsigned long tail;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	spin_lock_irqsave(&channel->tx_buf_producer_lock, flags);

	head = channel->tx_head;
	tail = READ_ONCE(channel->tx_tail);

	if (CIRC_SPACE(head, tail, channel->tx_buf_size)) {

		struct rcar_taurus_can_queued_tx_msg *queued_tx_msg = &channel->tx_buf[head];

		can_put_echo_skb(skb, ndev, head % RCAR_CAN_FIFO_DEPTH);

		rcar_taurus_can_conn_queue_write(rctcan,
						channel->ch_id,
						(cf->can_id & CAN_EFF_MASK),
						cf->can_dlc,
						&cf->data[0],
						queued_tx_msg);

		smp_store_release(&channel->tx_head,
				(head + 1) & (channel->tx_buf_size - 1));

		channel->tx_data_avail = 1;
		wake_up_interruptible(&channel->tx_wait_queue);

	}

	spin_unlock_irqrestore(&channel->tx_buf_producer_lock, flags);

	/* Stop the queue if we've filled all FIFO entries */
	if (channel->tx_head - channel->tx_tail >= RCAR_CAN_FIFO_DEPTH)
		netif_stop_queue(ndev);

	return NETDEV_TX_OK;
}

static const struct net_device_ops rcar_taurus_can_netdev_ops = {
	.ndo_open = rcar_taurus_can_open,
	.ndo_stop = rcar_taurus_can_close,
	.ndo_start_xmit = rcar_taurus_can_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int rcar_taurus_can_rx_pkt(struct rcar_taurus_can_channel *channel)
{
	struct net_device_stats *stats = &channel->ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u8 dlc;
	int ret = 0;
	unsigned long flags;
	unsigned long head;
	unsigned long tail;

	spin_lock_irqsave(&channel->rx_buf_consumer_lock, flags);

	/* Read index before reading contents at that index. */
	head = smp_load_acquire(&channel->rx_head);
	tail = channel->rx_tail;

	if (CIRC_CNT(head, tail, channel->rx_buf_size)) {

		/* extract one item from the buffer */
		struct taurus_can_res_msg *res_msg = &channel->rx_buf[tail];

		skb = alloc_can_skb(channel->ndev, &cf);
		if (!skb) {
			ret = -ENOMEM;
			goto out_unlock;
		}

		cf->can_id = (res_msg->params.read.node_id | CAN_EFF_MASK);
		cf->can_dlc = get_can_dlc(res_msg->params.read.data_len);
		for (dlc = 0; dlc < cf->can_dlc; dlc++)
			cf->data[dlc] = res_msg->params.read.data[dlc];

		can_led_event(channel->ndev, CAN_LED_EVENT_RX);

		stats->rx_bytes += cf->can_dlc;
		stats->rx_packets++;
		netif_receive_skb(skb);

		/* Finish reading descriptor before incrementing tail. */
		smp_store_release(&channel->rx_tail,
				(tail + 1) & (channel->rx_buf_size - 1));

		ret = 1;
	}

out_unlock:
	spin_unlock_irqrestore(&channel->rx_buf_consumer_lock, flags);

	return ret;
}

static int rcar_taurus_can_rx_poll(struct napi_struct *napi, int quota)
{
	struct rcar_taurus_can_channel *channel = container_of(napi,
							struct rcar_taurus_can_channel, napi);
	int num_pkts;
	int ret = 0;

	for (num_pkts = 0; num_pkts < quota; num_pkts++) {
		ret = rcar_taurus_can_rx_pkt(channel);
		if (ret <= 0)
			break;
	}
	/* All packets processed */
	if ((num_pkts < quota) && (ret == 0)) {
		napi_complete_done(napi, num_pkts);
	}
	return num_pkts;
}

static int rcar_taurus_can_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		rcar_taurus_can_start(ndev);
		netif_wake_queue(ndev);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int rcar_taurus_can_get_berr_counter(const struct net_device *dev,
					struct can_berr_counter *bec)
{
	bec->txerr = 0;
	bec->rxerr = 0;
	return 0;
}

static int rcar_taurus_can_init_ch(struct rcar_taurus_can_drv *rctcan, int ch_id)
{
	struct net_device *ndev;
	struct rcar_taurus_can_channel *channel;
	struct rpmsg_device *rpdev = rctcan->rpdev;
	int err;

	ndev = alloc_candev(sizeof(struct rcar_taurus_can_channel), RCAR_CAN_FIFO_DEPTH);
	if (!ndev) {
		dev_err(&rpdev->dev, "alloc_candev() failed (ch %d)\n", ch_id);
		err = -ENOMEM;
		goto fail;
	}

	ndev->netdev_ops = &rcar_taurus_can_netdev_ops;
	ndev->flags |= IFF_ECHO;

	channel = netdev_priv(ndev);

	channel->ch_id = ch_id;
	channel->ndev = ndev;
	channel->parent = rctcan;

	channel->can.bittiming_const = &rcar_taurus_can_bittiming_const;
	channel->can.do_set_mode = rcar_taurus_can_do_set_mode;
	channel->can.do_get_berr_counter = rcar_taurus_can_get_berr_counter;
	channel->can.ctrlmode_supported = CAN_CTRLMODE_BERR_REPORTING;
	channel->can.clock.freq = 66560000;

	/* Initialize taurus event list and its lock */
	INIT_LIST_HEAD(&channel->taurus_event_list_head);
	rwlock_init(&channel->event_list_lock);

	/* Initialize TX buffer */
	channel->tx_buf = devm_kzalloc(&rpdev->dev,
				sizeof(struct rcar_taurus_can_queued_tx_msg) * RCAR_CAN_FIFO_DEPTH,
				GFP_KERNEL);
	if (!channel->tx_buf) {
		dev_err(&rpdev->dev, "devm_kzalloc() for channel->tx_buf failed\n");
		err = -ENOMEM;
		goto fail;
	}

	channel->tx_head = channel->tx_tail = 0;
	channel->tx_buf_size = RCAR_CAN_FIFO_DEPTH;
	spin_lock_init(&channel->tx_buf_producer_lock);
	mutex_init(&channel->tx_buf_consumer_lock);
	init_waitqueue_head(&channel->tx_wait_queue);
	channel->tx_data_avail = 0;

	channel->tx_thread = kthread_run(rcar_taurus_tx_thread_ep,
					channel,
					"taurus_can_tx%d", channel->ch_id);

	/* Initialize RX buffer */
	channel->rx_buf = devm_kzalloc(&rpdev->dev,
				sizeof(struct taurus_can_res_msg) * RCAR_CAN_FIFO_DEPTH,
				GFP_KERNEL);
	if (!channel->rx_buf) {
		dev_err(&rpdev->dev, "devm_kzalloc() for channel->rx_buf failed\n");
		err = -ENOMEM;
		goto fail;
	}

	channel->rx_head = channel->rx_tail = 0;
	channel->rx_buf_size = RCAR_CAN_FIFO_DEPTH;
	spin_lock_init(&channel->rx_buf_consumer_lock);
	mutex_init(&channel->rx_buf_producer_lock);
	init_waitqueue_head(&channel->rx_wait_queue);
	channel->rx_data_avail = 0;

	channel->rx_thread = kthread_run(rcar_taurus_rx_thread_ep,
						channel,
						"taurus_can_rx%d", channel->ch_id);

	SET_NETDEV_DEV(ndev, &rpdev->dev);

	netif_napi_add(ndev, &channel->napi, rcar_taurus_can_rx_poll,
		RCAR_CAN_NAPI_WEIGHT);

	rctcan->channels[ch_id] = channel;

	err = register_candev(ndev);
	if (err) {
		dev_err(&rpdev->dev, "register_candev() failed, error %d\n",
			err);
		rctcan->channels[ch_id] = NULL;
		goto fail_candev;
	}

	devm_can_led_init(ndev);

	return 0;
fail_candev:
	netif_napi_del(&channel->napi);
	free_candev(ndev);
fail:
	return err;
}

static int rcar_taurus_can_probe(struct rpmsg_device *rpdev)
{
	struct rcar_taurus_can_drv *rctcan;
	int i;

	dev_info(&rpdev->dev, "Probe R-Car Taurus virtual CAN driver\n");

	/* Allocate and initialize the R-Car device structure. */
	rctcan = devm_kzalloc(&rpdev->dev, sizeof(*rctcan), GFP_KERNEL);
	if (rctcan == NULL) {
		dev_err(&rpdev->dev, "devm_kzalloc() failed\n");
		return -ENOMEM;
	}

	rctcan->rpdev = rpdev;
	dev_set_drvdata(&rpdev->dev, rctcan);

	for (i=0; i<NUM_RCAR_TAURUS_CAN_CHANNELS; i++) {
		int err;
		err = rcar_taurus_can_init_ch(rctcan, i);
		if (err) {
			dev_warn(&rpdev->dev, "rcar_taurus_can_init_ch() failed (ch=%d, err=%d)\n", i, err);
		} else {
			dev_info(&rpdev->dev, "Channel %d intialized\n", i);
		}
	}

	return 0;
}

static void rcar_taurus_can_remove(struct rpmsg_device *rpdev)
{
	int i;
	struct rcar_taurus_can_drv *rctcan = dev_get_drvdata(&rpdev->dev);

	dev_info(&rpdev->dev, "Remove R-Car Taurus virtual CAN driver\n");

	for (i=0; i<NUM_RCAR_TAURUS_CAN_CHANNELS; i++) {
		struct net_device *ndev;
		struct napi_struct *napi;

		struct rcar_taurus_can_channel *channel = rctcan->channels[i];
		if (!channel)
			continue;

		if (channel->tx_thread)
			kthread_stop(channel->tx_thread);

		if (channel->rx_thread)
			kthread_stop(channel->rx_thread);

		ndev = channel->ndev;
		napi = &channel->napi;

		unregister_candev(ndev);
		netif_napi_del(napi);
		free_candev(ndev);
	}
}

static struct rpmsg_device_id rcar_taurus_can_id_table[] = {
	{ .name	= "taurus-can" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rcar_taurus_can_id_table);

static struct rpmsg_driver rcar_taurus_can_driver = {
	.drv.name	= RCAR_TAURUS_CAN_DRV_NAME,
	.id_table	= rcar_taurus_can_id_table,
	.probe		= rcar_taurus_can_probe,
	.callback	= rcar_taurus_can_cb,
	.remove		= rcar_taurus_can_remove,
};
module_rpmsg_driver(rcar_taurus_can_driver);

MODULE_AUTHOR("Vito Colagiacomo");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("Taurus virtual CAN driver for Renesas R-Car SoC");
MODULE_ALIAS("platform:" RCAR_TAURUS_CAN_DRV_NAME);
