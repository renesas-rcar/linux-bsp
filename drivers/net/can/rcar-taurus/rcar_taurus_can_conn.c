#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/atomic.h>
#include "rcar_taurus_can.h"
#include "rcar_taurus_can_conn.h"
#include "r_taurus_can_protocol.h"

static atomic_t rpmsg_id_counter = ATOMIC_INIT(0);

static int rcar_taurus_can_conn_get_uniq_id(void)
{
	return atomic_inc_return(&rpmsg_id_counter);
}

static int rcar_taurus_can_conn_send_cmd(struct rcar_taurus_can_channel *can_ch,
					struct taurus_can_cmd_msg *cmd_msg,
					struct taurus_can_res_msg *res_msg)
{
	struct rpmsg_device *rpdev = can_ch->parent->rpdev;
	struct taurus_event_list *event;
	struct device *dev = &rpdev->dev;
	int ret = 0;

	event = devm_kzalloc(dev, sizeof(*event), GFP_KERNEL);
	if (!event) {
		dev_err(dev, "%s:%d Can't allocate memory for taurus event\n", __FUNCTION__, __LINE__);
		ret = -ENOMEM;
		goto cleanup_1;
	}

	event->result = devm_kzalloc(dev, sizeof(*event->result), GFP_KERNEL);
	if (!event->result) {
		dev_err(dev, "%s:%d Can't allocate memory for taurus event->result\n", __FUNCTION__, __LINE__);
		ret = -ENOMEM;
		goto cleanup_2;
	}

	event->id = cmd_msg->hdr.Id;
	init_completion(&event->ack);
	init_completion(&event->completed);

	write_lock(&can_ch->event_list_lock);
	list_add(&event->list, &can_ch->taurus_event_list_head);
	write_unlock(&can_ch->event_list_lock);

	/* send a message to our remote processor */
	ret = rpmsg_send(rpdev->ept, cmd_msg, sizeof(struct taurus_can_cmd_msg));
	if (ret) {
		dev_err(dev, "%s:%d Taurus command send failed (%d)\n", __FUNCTION__, __LINE__, ret);
		goto cleanup_3;
	}

	ret = wait_for_completion_interruptible(&event->ack);
	if (ret == -ERESTARTSYS) {
		/* we were interrupted */
		dev_err(dev, "%s:%d Interrupted while waiting taurus ACK (%d)\n", __FUNCTION__, __LINE__, ret);
		goto cleanup_3;
	};

	if (event->result->hdr.Result == R_TAURUS_RES_NACK) {
		dev_info(dev, "command not acknowledged (cmd id=%d)\n", cmd_msg->hdr.Id);
		ret = -EINVAL;
		goto cleanup_3;
	}

	ret = wait_for_completion_interruptible(&event->completed);
	if (ret == -ERESTARTSYS) {
		/* we were interrupted */
		dev_err(dev, "%s:%d Interrupted while waiting taurus response (%d)\n", __FUNCTION__, __LINE__, ret);
		goto cleanup_3;
	}

	memcpy(res_msg, event->result, sizeof(struct taurus_can_res_msg));

cleanup_3:
	write_lock(&can_ch->event_list_lock);
	list_del(&event->list);
	write_unlock(&can_ch->event_list_lock);
	devm_kfree(&rpdev->dev, event->result);
cleanup_2:
	devm_kfree(&rpdev->dev, event);
cleanup_1:
	return ret;
}

static int rcar_taurus_can_conn_send_queued_msg(struct rcar_taurus_can_channel *channel,
						struct rcar_taurus_can_queued_tx_msg *queued_tx_buf,
						struct taurus_can_res_msg *res_msg)
{
	return rcar_taurus_can_conn_send_cmd(channel,
					&queued_tx_buf->cmd,
					res_msg);
}

int rcar_taurus_can_conn_open(struct rcar_taurus_can_drv *rctcan,
			uint32_t can_ch,
			struct taurus_can_res_msg *res_msg)
{
	struct rcar_taurus_can_channel *channel = rctcan->channels[can_ch];
	struct taurus_can_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rcar_taurus_can_conn_get_uniq_id();
	cmd_msg.hdr.Channel = can_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_OPEN;
	cmd_msg.hdr.Par1 = CAN_PROTOCOL_OPEN;
	cmd_msg.type = CAN_PROTOCOL_OPEN;
	cmd_msg.params.open.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.open.can_ch = can_ch;

	ret = rcar_taurus_can_conn_send_cmd(channel, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
		(res_msg->params.open.res != 0)) {
		return -EIO;
	}

	return 0;
}

int rcar_taurus_can_conn_close(struct rcar_taurus_can_drv *rctcan,
			uint32_t can_ch,
			struct taurus_can_res_msg *res_msg)
{
	struct rcar_taurus_can_channel *channel = rctcan->channels[can_ch];
	struct taurus_can_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rcar_taurus_can_conn_get_uniq_id();
	cmd_msg.hdr.Channel = can_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_CLOSE;
	cmd_msg.hdr.Par1 = CAN_PROTOCOL_CLOSE;
	cmd_msg.type = CAN_PROTOCOL_CLOSE;
	cmd_msg.params.close.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.close.can_ch = can_ch;

	ret = rcar_taurus_can_conn_send_cmd(channel, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
		(res_msg->params.close.res != 0)) {
		return -EIO;
	}

	return 0;
}

int rcar_taurus_can_conn_read(struct rcar_taurus_can_drv *rctcan,
			uint32_t can_ch,
			struct taurus_can_res_msg *res_msg)
{
	struct rcar_taurus_can_channel *channel = rctcan->channels[can_ch];
	struct taurus_can_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rcar_taurus_can_conn_get_uniq_id();
	cmd_msg.hdr.Channel = can_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_READ;
	cmd_msg.hdr.Par1 = CAN_PROTOCOL_READ;
	cmd_msg.type = CAN_PROTOCOL_READ;
	cmd_msg.params.read.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.read.can_ch = can_ch;

	ret = rcar_taurus_can_conn_send_cmd(channel, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
		(res_msg->params.read.res != 0)) {
		return -EIO;
	}

	return 0;
}

/*
 * Blocking. Cannot be called from interrupt context (e.g. softirq)
 */
int rcar_taurus_can_conn_write(struct rcar_taurus_can_drv *rctcan,
			uint32_t can_ch,
			uint32_t node_id,
			uint32_t data_len,
			uint8_t *data,
			struct taurus_can_res_msg *res_msg)
{
	struct rcar_taurus_can_channel *channel = rctcan->channels[can_ch];
	struct taurus_can_cmd_msg cmd_msg;
	int ret;
	int size;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rcar_taurus_can_conn_get_uniq_id();
	cmd_msg.hdr.Channel = can_ch;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_WRITE;
	cmd_msg.hdr.Par1 = CAN_PROTOCOL_WRITE;
	cmd_msg.type = CAN_PROTOCOL_WRITE;
	cmd_msg.params.write.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.write.can_ch = can_ch;
	cmd_msg.params.write.node_id = node_id;

	size = data_len < sizeof(cmd_msg.params.write.data) ? data_len : sizeof(cmd_msg.params.write.data);
	memcpy(cmd_msg.params.write.data, data, size);
	cmd_msg.params.write.data_len = size;

	ret = rcar_taurus_can_conn_send_cmd(channel, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
		(res_msg->params.write.res != 0)) {
		return -EIO;
	}

	return 0;
}

/*
 * Non-Blocking. Queue the write operation in queued_tx_msg and return
 * immediately.
 *
 * The consumer of the circular buffer will have to call *
 * queued_tx_msg->send() to complete the operation.
 */
void rcar_taurus_can_conn_queue_write(struct rcar_taurus_can_drv *rctcan,
				uint32_t can_ch,
				uint32_t node_id,
				uint32_t data_len,
				uint8_t *data,
				struct rcar_taurus_can_queued_tx_msg *queued_tx_msg)
{
	struct taurus_can_cmd_msg *cmd = &queued_tx_msg->cmd;
	uint32_t  size;

	cmd->hdr.Id = rcar_taurus_can_conn_get_uniq_id();
	cmd->hdr.Channel = can_ch;
	cmd->hdr.Cmd = R_TAURUS_CMD_WRITE;
	cmd->hdr.Par1 = CAN_PROTOCOL_WRITE;
	cmd->type = CAN_PROTOCOL_WRITE;
	cmd->params.write.cookie = cmd->hdr.Id;
	cmd->params.write.can_ch = can_ch;
	cmd->params.write.node_id = node_id;

	size = data_len < sizeof(cmd->params.write.data) ? data_len : sizeof(cmd->params.write.data);
	memcpy(cmd->params.write.data, data, size);
	cmd->params.write.data_len = size;

	queued_tx_msg->send = rcar_taurus_can_conn_send_queued_msg;

	return;
}


