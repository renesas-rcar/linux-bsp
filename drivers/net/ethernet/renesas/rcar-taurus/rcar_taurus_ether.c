/*************************************************************************/ /*
 Renesas R-Car Taurus Ethernet device driver

 Copyright (C) 2022 Renesas Electronics Corporation

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
#include <linux/of.h>
#include <linux/circ_buf.h>
#include <linux/rpmsg.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include "r_taurus_ether_protocol.h"
#include "rcar_taurus_ether.h"
#include "rcar_taurus_ether_conn.h"

static int rct_eth_cb(struct rpmsg_device *rpdev, void *data, int len, void *priv, u32 src)
{
	struct rcar_taurus_ether_drv *rct_eth = dev_get_drvdata(&rpdev->dev);
	struct rcar_taurus_ether_channel *chan;
	struct taurus_ether_res_msg *res = (struct taurus_ether_res_msg *)data;
	u32 res_id = res->hdr.Id;
	u32 ch_id, pkt_addr, pkt_len;
	void __iomem *pkt_data;
	struct sk_buff *skb;

	dev_dbg(&rpdev->dev, "%s():%d\n", __func__, __LINE__);

	ch_id = res->hdr.Channel;
	if (ch_id >= NUM_RCAR_TAURUS_ETH_CHANNELS)
		return 0;

	chan = rct_eth->channels[ch_id];

	/* Prevent missing setting from CR side */
	if (!len)
		len = sizeof(struct taurus_ether_res_msg);

	if (res->hdr.Result == R_TAURUS_CMD_NOP && !res_id) {
		dev_dbg(&rpdev->dev, "Signal received! Aux = %llx\n", res->hdr.Aux);

		pkt_addr = lower_32_bits(res->hdr.Aux);
		pkt_len = upper_32_bits(res->hdr.Aux);

		pkt_data = ioremap(pkt_addr - ETH_MAC_HEADER_LEN, pkt_len);
		pkt_len = pkt_len + ETH_MAC_HEADER_LEN;

		skb = netdev_alloc_skb(chan->ndev, PKT_BUF_SZ + RCT_ETH_ALIGN - 1);
		if (!skb)
			return 0;

		skb_reserve(skb, NET_IP_ALIGN);
		skb_checksum_none_assert(skb);

		memcpy_fromio(skb->data, pkt_data, pkt_len);

		skb_put(skb, pkt_len);
		skb->protocol = eth_type_trans(skb, chan->ndev);
		netif_rx_ni(skb);

		chan->ndev->stats.rx_packets++;
		chan->ndev->stats.rx_bytes += pkt_len;

		iounmap(pkt_data);
	} else {
		struct taurus_event_list *event;
		struct list_head *i;

		/* Go through the list of pending events and check if this
		 * message matches any
		 */
		read_lock(&chan->event_list_lock);
		list_for_each_prev(i, &chan->taurus_event_list_head) {
			event = list_entry(i, struct taurus_event_list, list);
			if (event->id == res_id) {
				memcpy(event->result, data, len);

				if (event->ack_received) {
					complete(&event->completed);
				} else {
					event->ack_received = 1;
					complete(&event->ack);
				}
				break;
			}
		}
		read_unlock(&chan->event_list_lock);
	}

	return 0;
}

static int rct_eth_tx_thread(void *chan_data)
{
	struct rcar_taurus_ether_channel *chan = (struct rcar_taurus_ether_channel *)chan_data;
	struct rcar_taurus_ether_drv *rct_eth = chan->parent;
	struct taurus_ether_res_msg res_msg;
	struct rcar_taurus_tx_skb *tx_skb, *tx_skb2;
	struct sk_buff *skb;
	void __iomem *data;
	u16 data_len, frame_type;
	u32 buff_idx;
	int i, err;

	while (!kthread_should_stop()) {
		wait_event_interruptible(chan->tx_wait_queue, READ_ONCE(chan->tx_data_avail));
		WRITE_ONCE(chan->tx_data_avail, 0);
		mutex_lock(&chan->lock);

		list_for_each_entry_safe(tx_skb, tx_skb2, &chan->tx_skb_list, list) {
			skb = tx_skb->skb;

			frame_type = (*(skb->data + ETH_FRAME_TYPE_POS) << 8) +
				     *(skb->data + ETH_FRAME_TYPE_POS + 1);
			data_len = (u16)(skb->len - ETH_MAC_HEADER_LEN);

			err = rct_eth_conn_provide_tx_buffer(rct_eth, chan->ch_id, &res_msg,
							     data_len - ETH_CRC_CHKSUM_LEN);
			if (err)
				goto freed_skb;

			buff_idx = res_msg.params.tx_buffer.BufIdx;

			data = ioremap(res_msg.params.tx_buffer.BufAddr, data_len);
			memcpy_toio(data, skb->data + ETH_MAC_HEADER_LEN, data_len);
			iounmap(data);

			err = rct_eth_conn_start_xmit(rct_eth, chan->ch_id, &res_msg, buff_idx,
						      frame_type, data_len, skb->data);
			if (err)
				goto freed_skb;

			for (i = 0; i <= RCT_RETRY_TIMES; i++) {
				rct_eth_conn_tx_confirm(rct_eth, chan->ch_id, &res_msg);
				if (res_msg.params.tx_confirmation.TxConfirmed)
					break;
				mdelay(10);
			}

			chan->ndev->stats.tx_packets++;
			chan->ndev->stats.tx_bytes += skb->len;

freed_skb:
			dev_kfree_skb(skb);
			list_del(&tx_skb->list);
			kfree(tx_skb);
		}
		mutex_unlock(&chan->lock);
	}

	return 0;
}

static int rct_eth_mii_read(struct mii_bus *bus, int addr, int regnum)
{
	struct rcar_taurus_ether_channel *chan = bus->priv;
	struct rcar_taurus_ether_drv *rct_eth = chan->parent;
	struct taurus_ether_res_msg res_msg;
	int err;

	err = rct_eth_conn_mii_read(rct_eth, chan->ch_id, &res_msg, addr, regnum);
	if (err)
		return err;

	return res_msg.params.read_mii.RegVal;
}

static int rct_eth_mii_write(struct mii_bus *bus, int addr, int regnum, u16 val)
{
	struct rcar_taurus_ether_channel *chan = bus->priv;
	struct rcar_taurus_ether_drv *rct_eth = chan->parent;
	struct taurus_ether_res_msg res_msg;
	int err;

	err = rct_eth_conn_mii_write(rct_eth, chan->ch_id, &res_msg, addr, regnum, val);
	if (err)
		return err;

	return 0;
}

static int rct_eth_mii_register(struct net_device *ndev)
{
	struct rcar_taurus_ether_channel *chan = netdev_priv(ndev);
	struct mii_bus *mii_bus;
	struct device_node *np;
	char path[20];
	int err;

	mii_bus = mdiobus_alloc();
	if (!mii_bus)
		return -ENOMEM;

	mii_bus->name = "rcar-taurus-mii";
	sprintf(mii_bus->id, "rct_eth%d", chan->ch_id);
	mii_bus->priv = chan;
	mii_bus->read = rct_eth_mii_read;
	mii_bus->write = rct_eth_mii_write;
	mii_bus->parent = &ndev->dev;

	sprintf(path, "/%s", mii_bus->id);
	np = of_find_node_by_path(path);
	of_node_get(np);
	err = of_mdiobus_register(mii_bus, np);
	if (err < 0) {
		mdiobus_free(mii_bus);
		goto out;
	}

	chan->mii = mii_bus;

out:
	of_node_put(np);

	return err;
}

static void rct_eth_mii_unregister(struct net_device *ndev)
{
	struct rcar_taurus_ether_channel *chan = netdev_priv(ndev);

	mdiobus_unregister(chan->mii);
	mdiobus_free(chan->mii);
	chan->mii = NULL;
}

static void rct_eth_adjust_link(struct net_device *ndev)
{
	phy_print_status(ndev->phydev);
}

static int rct_eth_phy_init(struct net_device *ndev)
{
	struct rcar_taurus_ether_channel *chan = netdev_priv(ndev);
	struct device_node *np, *phy;
	struct phy_device *phydev;
	char path[20];
	phy_interface_t iface;
	int err = 0;

	sprintf(path, "/%s", chan->mii->id);
	np = of_find_node_by_path(path);
	if (!np) {
		netdev_warn(ndev, "Please add %s device node!", path);
		return -ENOENT;
	}

	of_node_get(np);

	of_get_phy_mode(np, &iface);

	if (iface != PHY_INTERFACE_MODE_RGMII) {
		netdev_warn(ndev, "Set PHY interface to RGMII.\n");
		iface = PHY_INTERFACE_MODE_RGMII;
	}

	phy = of_parse_phandle(np, "phy-handle", 0);
	if (!phy) {
		netdev_warn(ndev, "Please add phy-handle into %s device node!", chan->mii->id);
		err = -ENOENT;
		goto out;
	}

	phydev = of_phy_connect(ndev, phy, rct_eth_adjust_link, 0, iface);
	if (!phydev) {
		err = -ENOENT;
		goto out;
	}

	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_Pause_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_Asym_Pause_BIT);

	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);

	/* CR side fixed to 100Mbps */
	phy_set_max_speed(phydev, SPEED_100);

	phy_attached_info(phydev);

out:
	of_node_put(np);
	return err;
}

static void rct_eth_phy_deinit(struct net_device *ndev)
{
	phy_disconnect(ndev->phydev);
	ndev->phydev = NULL;
}

static int rct_eth_open(struct net_device *ndev)
{
	struct rcar_taurus_ether_channel *chan = netdev_priv(ndev);
	struct rcar_taurus_ether_drv *rct_eth = chan->parent;
	struct taurus_ether_res_msg res_msg;
	int i, err;

	err = rct_eth_conn_set_mode(rct_eth, chan->ch_id, &res_msg, ETH_MODE_ACTIVE);
	if (err)
		return err;

	for (i = 0; i <= RCT_RETRY_TIMES; i++) {
		rct_eth_conn_get_mode(rct_eth, chan->ch_id, &res_msg);
		if (res_msg.params.get_mode.CtrlMode == ETH_MODE_ACTIVE)
			break;

		if (i == RCT_RETRY_TIMES) {
			netdev_err(ndev, "%s-%d, set active failed.\n", __func__,  chan->ch_id);
			err = -ETIMEDOUT;
			goto out;
		}
		mdelay(10);
	}

	err = rct_eth_mii_register(ndev);
	if (err)
		goto out;

	err = rct_eth_phy_init(ndev);
	if (err)
		goto out_unregister_mii;

	phy_start(ndev->phydev);

	netif_start_queue(ndev);

	return 0;

out_unregister_mii:
	rct_eth_mii_unregister(ndev);

out:
	/* rct_eth_conn_close(rct_eth, chan->ch_id, &res_msg); */
	return err;
}

static int rct_eth_close(struct net_device *ndev)
{
	struct rcar_taurus_ether_channel *chan = netdev_priv(ndev);
	struct rcar_taurus_ether_drv *rct_eth = chan->parent;
	struct taurus_ether_res_msg res_msg;

	phy_stop(ndev->phydev);
	rct_eth_phy_deinit(ndev);
	rct_eth_mii_unregister(ndev);
	netif_stop_queue(ndev);

	rct_eth_conn_set_mode(rct_eth, chan->ch_id, &res_msg, ETH_MODE_DOWN);
	/* rct_eth_conn_close(rct_eth, chan->ch_id, &res_msg); */

	return 0;
}

static netdev_tx_t rct_eth_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct rcar_taurus_ether_channel *chan = netdev_priv(ndev);
	struct rcar_taurus_tx_skb *tx_skb;
	unsigned long flags;

	spin_lock_irqsave(&chan->tx_lock, flags);

	if (skb_put_padto(skb, ETH_ZLEN))
		goto out;

	tx_skb = kmalloc(sizeof(*tx_skb), GFP_ATOMIC);
	if (!tx_skb)
		goto out;

	tx_skb->skb = skb_get(skb);
	list_add_tail(&tx_skb->list, &chan->tx_skb_list);

	skb_tx_timestamp(skb);

	chan->tx_data_avail = 1;
	wake_up_interruptible(&chan->tx_wait_queue);

out:
	spin_unlock_irqrestore(&chan->tx_lock, flags);

	return NETDEV_TX_OK;
}

static const struct net_device_ops rcar_taurus_eth_netdev_ops = {
	.ndo_open		= rct_eth_open,
	.ndo_stop		= rct_eth_close,
	.ndo_start_xmit		= rct_eth_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
};

static int rct_eth_init_ch(struct rcar_taurus_ether_drv *rct_eth, int ch_id)
{
	struct net_device *ndev;
	struct rcar_taurus_ether_channel *chan;
	struct rpmsg_device *rpdev = rct_eth->rpdev;
	struct taurus_ether_res_msg res_msg;
	u8 mac_addr[ETH_MACADDR_SIZE] = {0x74, 0x90, 0x50, 0, 0, 0};
	int err;

	ndev = alloc_etherdev_mqs(sizeof(struct rcar_taurus_ether_channel), NUM_TX_QUEUE,
				  NUM_RX_QUEUE);
	if (!ndev) {
		dev_err(&rpdev->dev, "alloc_ethdev_mqs() failed (ch %d)\n", ch_id);
		return -ENOMEM;
	}

	chan = netdev_priv(ndev);

	chan->ch_id = ch_id;
	chan->ndev = ndev;
	chan->parent = rct_eth;

	rct_eth->channels[ch_id] = chan;

	/* Initialize taurus event list and its lock */
	INIT_LIST_HEAD(&chan->taurus_event_list_head);
	rwlock_init(&chan->event_list_lock);

	INIT_LIST_HEAD(&chan->tx_skb_list);

	spin_lock_init(&chan->tx_lock);
	mutex_init(&chan->lock);

	init_waitqueue_head(&chan->tx_wait_queue);
	chan->tx_thread = kthread_run(rct_eth_tx_thread, chan, "rct_eth_tx%d", chan->ch_id);

	SET_NETDEV_DEV(ndev, &rpdev->dev);
	ether_setup(ndev);

	/* Set function */
	ndev->netdev_ops = &rcar_taurus_eth_netdev_ops;
	/* FIXME */
	/* ndev->ethtool_ops = &rcar_taurus_eth_ethtool_ops; */

	err = rct_eth_conn_open(rct_eth, chan->ch_id, &res_msg);
	if (err) {
		dev_err(&rpdev->dev, "Open channel %d failed.\n", chan->ch_id);
		return err;
	}

	/* Set MAC address */
	/* CR side uses the fixed MAC address: 74:90:50:00:00:00 */
	ether_addr_copy(ndev->dev_addr, &mac_addr[0]);
	rct_eth_conn_set_mac_addr(rct_eth, chan->ch_id, &res_msg, ndev->dev_addr);

	err = register_netdev(ndev);
	if (err) {
		dev_err(&rpdev->dev, "register_netdev() failed, error %d\n", err);
		rct_eth->channels[ch_id] = NULL;
		goto out;
	}

	/* Print device information */
	netdev_info(ndev, "MAC address %pMn", ndev->dev_addr);

	return 0;

out:
	free_netdev(ndev);
	return err;
}

static int rct_eth_probe(struct rpmsg_device *rpdev)
{
	struct rcar_taurus_ether_drv *rct_eth;
	int i;

	dev_info(&rpdev->dev, "Probe R-Car Taurus virtual Ethernet driver\n");

	/* Allocate and initialize the R-Car device structure. */
	rct_eth = devm_kzalloc(&rpdev->dev, sizeof(*rct_eth), GFP_KERNEL);
	if (!rct_eth) {
		dev_err(&rpdev->dev, "devm_kzalloc() failed\n");
		return -ENOMEM;
	}

	rct_eth->rpdev = rpdev;
	dev_set_drvdata(&rpdev->dev, rct_eth);

	for (i = 0; i < NUM_RCAR_TAURUS_ETH_CHANNELS; i++) {
		int err;

		err = rct_eth_init_ch(rct_eth, i);
		if (err) {
			dev_warn(&rpdev->dev, "R-Car Taurus Ether init failed (ch=%d, err=%d)\n",
				 i, err);
		} else {
			dev_info(&rpdev->dev, "Channel %d initialized\n", i);
		}
	}

	return 0;
}

static void rct_eth_remove(struct rpmsg_device *rpdev)
{
	struct rcar_taurus_ether_drv *rct_eth = dev_get_drvdata(&rpdev->dev);
	int i;

	dev_info(&rpdev->dev, "Remove R-Car Taurus virtual Ethernet driver\n");

	for (i = 0; i < NUM_RCAR_TAURUS_ETH_CHANNELS; i++) {
		struct net_device *ndev;
		struct rcar_taurus_ether_channel *channel = rct_eth->channels[i];

		if (!channel)
			continue;

		ndev = channel->ndev;

		unregister_netdev(ndev);
		free_netdev(ndev);
	}
}

static struct rpmsg_device_id rct_eth_id_table[] = {
	{ .name	= "taurus-ether" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rct_eth_id_table);

static struct rpmsg_driver rct_eth_driver = {
	.drv.name	= "rcar-taurus-ether",
	.id_table	= rct_eth_id_table,
	.probe		= rct_eth_probe,
	.callback	= rct_eth_cb,
	.remove		= rct_eth_remove,
};
module_rpmsg_driver(rct_eth_driver);

MODULE_AUTHOR("Phong Hoang");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("Taurus virtual Ethernet driver for Renesas R-Car SoC");
