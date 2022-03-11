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
#include <linux/rpmsg.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include "rcar_taurus_ether.h"
#include "rcar_taurus_ether_conn.h"
#include "r_taurus_ether_protocol.h"

static atomic_t rpmsg_id_counter = ATOMIC_INIT(0);

static int rct_eth_conn_get_uniq_id(void)
{
	return atomic_inc_return(&rpmsg_id_counter);
}

static int rct_eth_conn_send_cmd(struct rcar_taurus_ether_channel *chan,
				 struct taurus_ether_cmd_msg *cmd_msg,
				 struct taurus_ether_res_msg *res_msg)
{
	struct rpmsg_device *rpdev = chan->parent->rpdev;
	struct taurus_event_list *event;
	struct device *dev = &rpdev->dev;
	int ret;

	event = devm_kzalloc(dev, sizeof(*event), GFP_KERNEL);
	if (!event) {
		dev_err(dev, "%s:%d Can't allocate memory for taurus event\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto cleanup_1;
	}

	event->result = devm_kzalloc(dev, sizeof(*event->result), GFP_KERNEL);
	if (!event->result) {
		dev_err(dev, "%s:%d Can't allocate memory for taurus event->result\n",
			__func__, __LINE__);
		ret = -ENOMEM;
		goto cleanup_2;
	}

	event->id = cmd_msg->hdr.Id;
	init_completion(&event->ack);
	init_completion(&event->completed);

	write_lock(&chan->event_list_lock);
	list_add(&event->list, &chan->taurus_event_list_head);
	write_unlock(&chan->event_list_lock);

	/* send a message to our remote processor */
	ret = rpmsg_send(rpdev->ept, cmd_msg, sizeof(struct taurus_ether_cmd_msg));
	if (ret) {
		dev_err(dev, "%s:%d Taurus command send failed (%d)\n", __func__, __LINE__, ret);
		goto cleanup_3;
	}

	ret = wait_for_completion_interruptible_timeout(&event->ack, msecs_to_jiffies(30000));
	if (ret == -ERESTARTSYS) {
		/* we were interrupted */
		dev_err(dev, "%s:%d Interrupted while waiting taurus ACK (%d)\n",
			__func__, __LINE__, ret);
		goto cleanup_3;
	} else if (!ret) {
		dev_err(dev, "%s:%d Timedout while waiting taurus ACK\n", __func__, __LINE__);
		ret = -ETIMEDOUT;
		goto cleanup_3;
	}

	if (event->result->hdr.Result == R_TAURUS_RES_NACK) {
		dev_info(dev, "command not acknowledged (cmd id=%d)\n", cmd_msg->hdr.Id);
		ret = -EINVAL;
		goto cleanup_3;
	}

	ret = wait_for_completion_interruptible_timeout(&event->completed, msecs_to_jiffies(30000));
	if (ret == -ERESTARTSYS) {
		/* we were interrupted */
		dev_err(dev, "%s:%d Interrupted while waiting taurus response (%d)\n",
			__func__, __LINE__, ret);
		goto cleanup_3;
	} else if (!ret) {
		dev_err(dev, "%s:%d Timedout while waiting taurus response\n", __func__, __LINE__);
		ret = -ETIMEDOUT;
		goto cleanup_3;
	}

	ret = 0;

	memcpy(res_msg, event->result, sizeof(struct taurus_ether_res_msg));

cleanup_3:
	write_lock(&chan->event_list_lock);
	list_del(&event->list);
	write_unlock(&chan->event_list_lock);
	devm_kfree(&rpdev->dev, event->result);
cleanup_2:
	devm_kfree(&rpdev->dev, event);
cleanup_1:
	return ret;
}

int rct_eth_conn_open(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
		      struct taurus_ether_res_msg *res_msg)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_OPEN;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_OPEN;
	cmd_msg.type = ETHER_PROTOCOL_OPEN;
	cmd_msg.params.eth_init.cookie = cmd_msg.hdr.Id;

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE || res_msg->params.open.res)
		return -EIO;

	return 0;
}

int rct_eth_conn_close(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
		       struct taurus_ether_res_msg *res_msg)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_CLOSE;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_CLOSE;
	cmd_msg.type = ETHER_PROTOCOL_CLOSE;
	cmd_msg.params.close.cookie = cmd_msg.hdr.Id;

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE || res_msg->params.close.res)
		return -EIO;

	return 0;
}

int rct_eth_conn_mii_read(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			  struct taurus_ether_res_msg *res_msg, int addr, int regnum)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_IOC_READ_MII;
	cmd_msg.type = ETHER_PROTOCOL_IOC_READ_MII;
	cmd_msg.params.read_mii.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.read_mii.CtrlIdx = eth_ch;
	cmd_msg.params.read_mii.TrcvIdx = (u8)addr;
	cmd_msg.params.read_mii.RegIdx = (u8)regnum;

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE || res_msg->params.read_mii.res)
		return -EIO;

	return 0;
}

int rct_eth_conn_mii_write(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			   struct taurus_ether_res_msg *res_msg, int addr, int regnum, u16 val)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_IOC_WRITE_MII;
	cmd_msg.type = ETHER_PROTOCOL_IOC_WRITE_MII;
	cmd_msg.params.write_mii.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.write_mii.CtrlIdx = eth_ch;
	cmd_msg.params.write_mii.TrcvIdx = (u8)addr;
	cmd_msg.params.write_mii.RegIdx = (u8)regnum;
	cmd_msg.params.write_mii.RegVal = val;

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE || res_msg->params.read_mii.res)
		return -EIO;

	return 0;
}

int rct_eth_conn_set_mode(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			  struct taurus_ether_res_msg *res_msg, bool mode)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_IOC_SET_MODE;
	cmd_msg.type = ETHER_PROTOCOL_IOC_SET_MODE;
	cmd_msg.params.eth_set_mode.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.eth_set_mode.CtrlIdx = eth_ch;
	cmd_msg.params.eth_set_mode.CtrlMode = mode;

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE || res_msg->params.set_mode.res)
		return -EIO;

	return 0;
}

int rct_eth_conn_get_mode(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			  struct taurus_ether_res_msg *res_msg)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_IOC_GET_MODE;
	cmd_msg.type = ETHER_PROTOCOL_IOC_GET_MODE;
	cmd_msg.params.eth_get_mode.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.eth_get_mode.CtrlIdx = eth_ch;

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE || res_msg->params.get_mode.res)
		return -EIO;

	return 0;
}

int rct_eth_conn_get_mac_addr(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			      struct taurus_ether_res_msg *res_msg)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_IOC_GET_PHYS_ADDR;
	cmd_msg.type = ETHER_PROTOCOL_IOC_GET_PHYS_ADDR;
	cmd_msg.params.get_phys.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.get_phys.CtrlIdx = eth_ch;

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE || res_msg->params.get_phys.res)
		return -EIO;

	return 0;
}

int rct_eth_conn_set_mac_addr(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			      struct taurus_ether_res_msg *res_msg, u8 *mac_addr)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_IOC_SET_PHYS_ADDR;
	cmd_msg.type = ETHER_PROTOCOL_IOC_SET_PHYS_ADDR;
	cmd_msg.params.set_phys.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.set_phys.CtrlIdx = eth_ch;
	memcpy(&cmd_msg.params.set_phys.PhysAddr, mac_addr, ETH_MACADDR_SIZE);

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE || res_msg->params.set_phys.res)
		return -EIO;

	return 0;
}

int rct_eth_conn_provide_tx_buffer(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
				   struct taurus_ether_res_msg *res_msg, u16 data_len)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_IOC_PROVIDE_TX_BUFF;
	cmd_msg.type = ETHER_PROTOCOL_IOC_PROVIDE_TX_BUFF;
	cmd_msg.params.tx_buffer.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.tx_buffer.CtrlIdx = eth_ch;
	cmd_msg.params.tx_buffer.LenByte = data_len;

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE || res_msg->params.tx_buffer.res)
		return -EIO;

	return 0;
}

int rct_eth_conn_start_xmit(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			    struct taurus_ether_res_msg *res_msg, u32 buff_idx,
			    u16 frame_type, u16 data_len, u8 *dest_addr)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_IOC_TRANSMIT;
	cmd_msg.type = ETHER_PROTOCOL_IOC_TRANSMIT;
	cmd_msg.params.transmit.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.transmit.CtrlIdx = eth_ch;
	cmd_msg.params.transmit.BufIdx = buff_idx;
	cmd_msg.params.transmit.FrameType = frame_type;
	cmd_msg.params.transmit.TxConfirmation = true;
	cmd_msg.params.transmit.LenByte = data_len;
	memcpy(&cmd_msg.params.transmit.PhysAddr, dest_addr, ETH_MACADDR_SIZE);

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE || res_msg->params.transmit.res)
		return -EIO;

	return 0;
}

int rct_eth_conn_tx_confirm(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			    struct taurus_ether_res_msg *res_msg)
{
	struct rcar_taurus_ether_channel *chan = rct_eth->channels[eth_ch];
	struct taurus_ether_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rct_eth_conn_get_uniq_id();
	cmd_msg.hdr.Channel = eth_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = ETHER_PROTOCOL_IOC_TX_CONFIRMATION;
	cmd_msg.type = ETHER_PROTOCOL_IOC_TX_CONFIRMATION;
	cmd_msg.params.tx_confirmation.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.tx_confirmation.CtrlIdx = eth_ch;

	ret = rct_eth_conn_send_cmd(chan, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if (res_msg->hdr.Result != R_TAURUS_RES_COMPLETE ||
	    res_msg->params.tx_confirmation.res)
		return -EIO;

	return 0;
}
