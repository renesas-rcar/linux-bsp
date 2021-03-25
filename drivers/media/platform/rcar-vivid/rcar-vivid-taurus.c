#include <linux/atomic.h>
#include <linux/rpmsg.h>

#include "rcar-vivid-taurus.h"
#include "r_taurus_camera_protocol.h"



#define RVIVID_TAURUS_CHANNEL		0x0
#define RVIVID_TAURUS_VIN_CHANNEL	0x0

static atomic_t rpmsg_id_counter = ATOMIC_INIT(0);

static int vivid_taurus_get_uniq_id(void)
{
    return atomic_inc_return(&rpmsg_id_counter);
}


static int vivid_taurus_send_command(struct rcar_vivid_device *rvivid,
                struct taurus_camera_cmd_msg *cmd_msg,
                struct taurus_camera_res_msg *res_msg, int cmd_extra_size)
{
    struct taurus_event_list *event;
    struct rpmsg_device *rpdev = rvivid->rpdev;
    struct device *dev = rvivid->dev;
    int ret = 0;
    if(!rvivid)
        dev_err(dev, "no rvivid device to send \n");
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

    write_lock(&rvivid->event_list_lock);
    list_add(&event->list, &rvivid->taurus_event_list_head);
    write_unlock(&rvivid->event_list_lock);

    /* send a message to our remote processor */
    ret = rpmsg_send(rpdev->ept, cmd_msg, sizeof(struct taurus_camera_cmd_msg) + cmd_extra_size);
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

    memcpy(res_msg, event->result, sizeof(struct taurus_camera_res_msg));

cleanup_3:
    write_lock(&rvivid->event_list_lock);
    list_del(&event->list);
    write_unlock(&rvivid->event_list_lock);
    devm_kfree(&rpdev->dev, event->result);
cleanup_2:
    devm_kfree(&rpdev->dev, event);
cleanup_1:
    return ret;
}


int vivid_taurus_channel_init(struct vivid_v4l2_device *vivid,
            struct taurus_camera_res_msg *res_msg)
{
    struct taurus_camera_cmd_msg cmd_msg;
    struct rcar_vivid_device *rvivid = vivid->rvivid;

    int ret;
    if (!res_msg)
        return -EINVAL;

    cmd_msg.hdr.Id = vivid_taurus_get_uniq_id();
    cmd_msg.hdr.Channel = RVIVID_TAURUS_CHANNEL;
    cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
    cmd_msg.hdr.Par1 = CAMERA_PROTOCOL_IOC_CHANNEL_INIT;
    cmd_msg.type = CAMERA_PROTOCOL_IOC_CHANNEL_INIT;
    cmd_msg.params.ioc_channel_init.channel= vivid->channel;
    cmd_msg.params.ioc_channel_init.cookie = cmd_msg.hdr.Id;
    cmd_msg.params.ioc_channel_init.buffer[0].address = vivid->phys_addr[0];
    cmd_msg.params.ioc_channel_init.buffer[0].index = 0;
    cmd_msg.params.ioc_channel_init.buffer[1].address = vivid->phys_addr[1];
    cmd_msg.params.ioc_channel_init.buffer[1].index = 1;
    cmd_msg.params.ioc_channel_init.buffer[2].address = vivid->phys_addr[2];
    cmd_msg.params.ioc_channel_init.buffer[2].index = 2;
    ret = vivid_taurus_send_command(rvivid, &cmd_msg, res_msg, 0);
    if (ret)
        return -EPIPE;
    if (TAURUS_CAMERA_RES_ERR_REINIT == res_msg->params.ioc_channel_init.res) {
        rvivid_warn(rvivid, "%s channel reinit!\n", __func__);
        return TAURUS_CAMERA_RES_ERR_REINIT;
    }
    if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
        (res_msg->params.ioc_channel_init.res!= 0)) {
        return -EIO;
    }

    return 0;

}

int vivid_taurus_channel_start(struct vivid_v4l2_device *vivid,
            struct taurus_camera_res_msg *res_msg)
{
    struct taurus_camera_cmd_msg cmd_msg;
    int ret;
    struct rcar_vivid_device *rvivid = vivid->rvivid;
    if (!res_msg)
        return -EINVAL;

    cmd_msg.hdr.Id = vivid_taurus_get_uniq_id();
    cmd_msg.hdr.Channel = RVIVID_TAURUS_CHANNEL;
    cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
    cmd_msg.hdr.Par1 = CAMERA_PROTOCOL_IOC_CHANNEL_START;
    cmd_msg.type = CAMERA_PROTOCOL_IOC_CHANNEL_START;
    cmd_msg.params.ioc_channel_start.cookie= cmd_msg.hdr.Id;
    cmd_msg.params.ioc_channel_start.channel= vivid->channel;
    ret = vivid_taurus_send_command(rvivid, &cmd_msg, res_msg, 0);
    if (ret)
            return -EPIPE;

    if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
        (res_msg->params.ioc_channel_start.res != 0)) {
        return -EIO;
    }
    return 0;

}

int vivid_taurus_channel_stop(struct vivid_v4l2_device *vivid,
            struct taurus_camera_res_msg *res_msg)
{
    struct taurus_camera_cmd_msg cmd_msg;
    int ret;
    struct rcar_vivid_device *rvivid = vivid->rvivid;
    if (!res_msg)
        return -EINVAL;

    cmd_msg.hdr.Id = vivid_taurus_get_uniq_id();
    cmd_msg.hdr.Channel = RVIVID_TAURUS_CHANNEL;
    cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
    cmd_msg.hdr.Par1 = CAMERA_PROTOCOL_IOC_CHANNEL_STOP;
    cmd_msg.type = CAMERA_PROTOCOL_IOC_CHANNEL_STOP;
    cmd_msg.params.ioc_channel_stop.cookie= cmd_msg.hdr.Id;
    cmd_msg.params.ioc_channel_stop.channel= vivid->channel;
    ret = vivid_taurus_send_command(rvivid, &cmd_msg, res_msg, 0);
    if (ret)
            return -EPIPE;

    if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
        (res_msg->params.ioc_channel_stop.res != 0)) {
        return -EIO;
    }
    return 0;
}

int vivid_taurus_feed_buffer(struct vivid_v4l2_device *vivid,
            uint32_t address,int slot,
            struct taurus_camera_res_msg *res_msg)
{
    struct taurus_camera_cmd_msg cmd_msg;
    int ret;
    struct rcar_vivid_device *rvivid = vivid->rvivid;
    if (!res_msg)
        return -EINVAL;

    cmd_msg.hdr.Id = vivid_taurus_get_uniq_id();
    cmd_msg.hdr.Channel = RVIVID_TAURUS_CHANNEL;
    cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
    cmd_msg.hdr.Par1 = CAMERA_PROTOCOL_IOC_CHANNEL_FEED_BUFFER;
    cmd_msg.type = CAMERA_PROTOCOL_IOC_CHANNEL_FEED_BUFFER;
    cmd_msg.params.ioc_channel_feed_buffer.buf_cnt = HW_BUFFER_NUM;
    cmd_msg.params.ioc_channel_feed_buffer.channel = vivid->channel;
    cmd_msg.params.ioc_channel_feed_buffer.cookie = cmd_msg.hdr.Id;
    cmd_msg.params.ioc_channel_feed_buffer.buf_cnt= 1;
    cmd_msg.params.ioc_channel_feed_buffer.buffer[0].address = address;
    cmd_msg.params.ioc_channel_feed_buffer.buffer[0].index= slot;
    ret = vivid_taurus_send_command(rvivid, &cmd_msg, res_msg, 0);
    if (ret)
            return -EPIPE;

    if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
        (res_msg->params.ioc_channel_feed_buffer.res != 0)) {
        return -EIO;
    }
    return 0;
}

int vivid_taurus_feed_buffers(struct vivid_v4l2_device *vivid,
            struct taurus_camera_buffer *buffer, int buf_cnt,
            struct taurus_camera_res_msg *res_msg)
{
    struct taurus_camera_cmd_msg cmd_msg;
    int ret;
    int i = 0;
    struct rcar_vivid_device *rvivid = vivid->rvivid;
    if (!res_msg)
        return -EINVAL;
    cmd_msg.hdr.Id = vivid_taurus_get_uniq_id();
    cmd_msg.hdr.Channel = RVIVID_TAURUS_CHANNEL;
    cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
    cmd_msg.hdr.Par1 = CAMERA_PROTOCOL_IOC_CHANNEL_FEED_BUFFER;
    cmd_msg.type = CAMERA_PROTOCOL_IOC_CHANNEL_FEED_BUFFER;
    cmd_msg.params.ioc_channel_feed_buffer.buf_cnt = HW_BUFFER_NUM;
    cmd_msg.params.ioc_channel_feed_buffer.channel = vivid->channel;
    cmd_msg.params.ioc_channel_feed_buffer.cookie = cmd_msg.hdr.Id;
    cmd_msg.params.ioc_channel_feed_buffer.buf_cnt= buf_cnt;
    for(i = 0; i < buf_cnt; i++) {
        cmd_msg.params.ioc_channel_feed_buffer.buffer[i].address = buffer[i].address;
        cmd_msg.params.ioc_channel_feed_buffer.buffer[i].index= buffer[i].index;
    }
    ret = vivid_taurus_send_command(rvivid, &cmd_msg, res_msg, buf_cnt * sizeof(struct taurus_camera_buffer));
    if (ret)
            return -EPIPE;
    if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
        (res_msg->params.ioc_channel_feed_buffer.res != 0))
        return -EIO;

    return 0;

}

int vivid_taurus_channel_release(struct vivid_v4l2_device *vivid,
            struct taurus_camera_res_msg *res_msg)
{
    struct taurus_camera_cmd_msg cmd_msg;
    int ret;
    struct rcar_vivid_device *rvivid = vivid->rvivid;
    if (!res_msg)
        return -EINVAL;

    cmd_msg.hdr.Id = vivid_taurus_get_uniq_id();
    cmd_msg.hdr.Channel = RVIVID_TAURUS_CHANNEL;
    cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
    cmd_msg.hdr.Par1 = CAMERA_PROTOCOL_IOC_CHANNEL_RELEASE;
    cmd_msg.type = CAMERA_PROTOCOL_IOC_CHANNEL_RELEASE;
    cmd_msg.params.ioc_channel_release.channel= vivid->channel;
    cmd_msg.params.ioc_channel_release.cookie= cmd_msg.hdr.Id;
    ret = vivid_taurus_send_command(rvivid, &cmd_msg, res_msg, 0);
    if (ret)
            return -EPIPE;

    if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
        (res_msg->params.ioc_channel_release.res != 0)) {
        return -EIO;
    }
    return 0;
}

int vivid_taurus_get_info(struct rcar_vivid_device *rvivid,
            struct taurus_camera_res_msg *res_msg)
{
    struct taurus_camera_cmd_msg cmd_msg;
    int ret;
    if (!res_msg)
        return -EINVAL;

    cmd_msg.hdr.Id = vivid_taurus_get_uniq_id();
    cmd_msg.hdr.Channel = RVIVID_TAURUS_CHANNEL;
    cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
    cmd_msg.hdr.Par1 = CAMERA_PROTOCOL_IOC_GET_INFO;
    cmd_msg.type = CAMERA_PROTOCOL_IOC_GET_INFO;
    cmd_msg.params.ioc_get_info.cookie = cmd_msg.hdr.Id;
    ret = vivid_taurus_send_command(rvivid, &cmd_msg, res_msg, 0);
    if (ret)
            return -EPIPE;

    if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
        (res_msg->params.ioc_get_info.res != 0)) {
        return -EIO;
    }
    rvivid->channel_num = res_msg->params.ioc_get_info.channel_num;
    return 0;
}

int vivid_taurus_get_channel_info(struct vivid_v4l2_device *vivid,
            struct taurus_camera_res_msg *res_msg)
{
    struct taurus_camera_cmd_msg cmd_msg;
    int ret;
    struct rcar_vivid_device *rvivid = vivid->rvivid;
    if (!res_msg)
        return -EINVAL;

    cmd_msg.hdr.Id = vivid_taurus_get_uniq_id();
    cmd_msg.hdr.Channel = RVIVID_TAURUS_CHANNEL;
    cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
    cmd_msg.hdr.Par1 = CAMERA_PROTOCOL_IOC_GET_CHANNEL_INFO;
    cmd_msg.type = CAMERA_PROTOCOL_IOC_GET_CHANNEL_INFO;
    cmd_msg.params.ioc_get_channel_info.channel= vivid->channel;
    cmd_msg.params.ioc_get_channel_info.cookie = cmd_msg.hdr.Id;
    ret = vivid_taurus_send_command(rvivid, &cmd_msg, res_msg, 0);
    if (ret)
            return -EPIPE;

    if ((res_msg->hdr.Result != R_TAURUS_RES_COMPLETE) ||
        (res_msg->params.ioc_get_info.res != 0)) {
        return -EIO;
    }
    vivid->format.width = res_msg->params.ioc_get_channel_info.width;
    vivid->format.height = res_msg->params.ioc_get_channel_info.height;
    return 0;
}
