#include "rcar_rvgc_taurus.h"

#include <linux/rpmsg.h>
#include "rcar_rvgc_drv.h"
#include "r_taurus_rvgc_protocol.h"

#include <linux/atomic.h>

#define RVGC_TAURUS_CHANNEL	0xff

static atomic_t rpmsg_id_counter = ATOMIC_INIT(0);

static int rvgc_taurus_get_uniq_id(void)
{
	return atomic_inc_return(&rpmsg_id_counter);
}

static int rvgc_taurus_send_command(struct rcar_rvgc_device *rcrvgc,
				struct taurus_rvgc_cmd_msg *cmd_msg,
				struct taurus_rvgc_res_msg *res_msg)
{
	struct taurus_event_list *event;
	struct rpmsg_device *rpdev = rcrvgc->rpdev;
	struct device *dev = rcrvgc->dev;
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

	write_lock(&rcrvgc->event_list_lock);
	list_add(&event->list, &rcrvgc->taurus_event_list_head);
	write_unlock(&rcrvgc->event_list_lock);

	/* send a message to our remote processor */
	ret = rpmsg_send(rpdev->ept, cmd_msg, sizeof(struct taurus_rvgc_cmd_msg));
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

	memcpy(res_msg, event->result, sizeof(struct taurus_rvgc_res_msg));

cleanup_3:
	write_lock(&rcrvgc->event_list_lock);
	list_del(&event->list);
	write_unlock(&rcrvgc->event_list_lock);
	devm_kfree(&rpdev->dev, event->result);
cleanup_2:
	devm_kfree(&rpdev->dev, event);
cleanup_1:
	return ret;
}

int rvgc_taurus_display_init(struct rcar_rvgc_device *rcrvgc,
			uint32_t display,
			uint32_t layer,
			struct taurus_rvgc_res_msg *res_msg)
{
	struct taurus_rvgc_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rvgc_taurus_get_uniq_id();
	cmd_msg.hdr.Channel = RVGC_TAURUS_CHANNEL;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = RVGC_PROTOCOL_IOC_DISPLAY_INIT;
	cmd_msg.type = RVGC_PROTOCOL_IOC_DISPLAY_INIT;
	cmd_msg.params.ioc_display_flush.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.ioc_display_flush.display = display;

	ret = rvgc_taurus_send_command(rcrvgc, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
		(res_msg->params.ioc_display_init.res != 0)) {
		return -EIO;
	}

	cmd_msg.hdr.Id = rvgc_taurus_get_uniq_id();
	cmd_msg.hdr.Channel = RVGC_TAURUS_CHANNEL;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = RVGC_PROTOCOL_IOC_LAYER_RESERVE;
	cmd_msg.type = RVGC_PROTOCOL_IOC_LAYER_RESERVE;
	cmd_msg.params.ioc_layer_reserve.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.ioc_layer_reserve.display = display;
	cmd_msg.params.ioc_layer_reserve.layer = layer;

	ret = rvgc_taurus_send_command(rcrvgc, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
		(res_msg->params.ioc_layer_reserve.res != 0)) {
		return -EIO;
	}

	return 0;
}

int rvgc_taurus_display_get_info(struct rcar_rvgc_device *rcrvgc,
				uint32_t display,
				struct taurus_rvgc_res_msg *res_msg)
{
	struct taurus_rvgc_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rvgc_taurus_get_uniq_id();
	cmd_msg.hdr.Channel = RVGC_TAURUS_CHANNEL;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = RVGC_PROTOCOL_IOC_DISPLAY_GET_INFO;
	cmd_msg.type = RVGC_PROTOCOL_IOC_DISPLAY_GET_INFO;
	cmd_msg.params.ioc_display_get_info.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.ioc_display_get_info.display = display;

	ret = rvgc_taurus_send_command(rcrvgc, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
		(res_msg->params.ioc_display_get_info.res != 0)) {
		return -EIO;
	}

	return 0;
}

int rvgc_taurus_display_flush(struct rcar_rvgc_device *rcrvgc,
			uint32_t display,
			uint32_t blocking,
			struct taurus_rvgc_res_msg *res_msg)
{
	struct taurus_rvgc_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rvgc_taurus_get_uniq_id();
	cmd_msg.hdr.Channel = RVGC_TAURUS_CHANNEL;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = RVGC_PROTOCOL_IOC_DISPLAY_FLUSH;
	cmd_msg.type = RVGC_PROTOCOL_IOC_DISPLAY_FLUSH;
	cmd_msg.params.ioc_display_flush.cookie = cmd_msg.hdr.Id;
	cmd_msg.params.ioc_display_flush.display = display;
	cmd_msg.params.ioc_display_flush.blocking = blocking;

	ret = rvgc_taurus_send_command(rcrvgc, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
		(res_msg->params.ioc_display_flush.res != 0)) {
		return -EIO;
	}

	return 0;
}

int rvgc_taurus_layer_set_size(struct rcar_rvgc_device *rcrvgc,
			uint32_t display,
			uint32_t layer,
			uint32_t width,
			uint32_t height,
			struct taurus_rvgc_res_msg *res_msg)
{
	struct taurus_rvgc_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rvgc_taurus_get_uniq_id();
	cmd_msg.hdr.Channel = RVGC_TAURUS_CHANNEL;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = RVGC_PROTOCOL_IOC_LAYER_SET_SIZE;
	cmd_msg.type = RVGC_PROTOCOL_IOC_LAYER_SET_SIZE;
	cmd_msg.params.ioc_layer_set_size.cookie =  cmd_msg.hdr.Id;
	cmd_msg.params.ioc_layer_set_size.display = display;
	cmd_msg.params.ioc_layer_set_size.layer = layer;
	cmd_msg.params.ioc_layer_set_size.size_w = width;
	cmd_msg.params.ioc_layer_set_size.size_h = height;

	ret = rvgc_taurus_send_command(rcrvgc, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
		(res_msg->params.ioc_layer_set_size.res != 0)) {
		return -EIO;
	}

	return 0;
}

int rvgc_taurus_layer_set_addr(struct rcar_rvgc_device *rcrvgc,
			uint32_t display,
			uint32_t layer,
			uint32_t paddr,
			struct taurus_rvgc_res_msg *res_msg)
{
	struct taurus_rvgc_cmd_msg cmd_msg;
	int ret;

	if (!res_msg)
		return -EINVAL;

	cmd_msg.hdr.Id = rvgc_taurus_get_uniq_id();
	cmd_msg.hdr.Channel = RVGC_TAURUS_CHANNEL;
	cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
	cmd_msg.hdr.Par1 = RVGC_PROTOCOL_IOC_LAYER_SET_ADDR;
	cmd_msg.type = RVGC_PROTOCOL_IOC_LAYER_SET_ADDR;
	cmd_msg.params.ioc_layer_set_addr.cookie =  cmd_msg.hdr.Id;
	cmd_msg.params.ioc_layer_set_addr.display = display;
	cmd_msg.params.ioc_layer_set_addr.layer = layer;
	cmd_msg.params.ioc_layer_set_addr.paddr = paddr;

	ret = rvgc_taurus_send_command(rcrvgc, &cmd_msg, res_msg);
	if (ret)
		return -EPIPE;

	if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
		(res_msg->params.ioc_layer_set_addr.res != 0)) {
		return -EIO;
	}

	return 0;
}
