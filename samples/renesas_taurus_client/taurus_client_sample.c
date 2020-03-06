#define DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/rpmsg.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include "r_virtdev_drv_api_priv.h"

struct taurus_event_list {
	uint32_t id;
	struct taurus_virtdev_res_msg result;
	struct list_head list;
	struct completion ack;
	bool ack_received;
	struct completion completed;
};

struct taurus_sample_instance_data {
	struct list_head taurus_event_list_head;
	rwlock_t event_list_lock;
	struct task_struct *taurus_sample_kthread;
};


static int taurus_sample_kthreadfn(void *data)
{
	int ret = 0;
	uint32_t cnt = 0x100;
	struct rpmsg_device *rpdev = (struct rpmsg_device*)data;
	struct taurus_sample_instance_data *idata = dev_get_drvdata(&rpdev->dev);
	struct taurus_virtdev_cmd_msg cmd_msg;
	struct taurus_virtdev_res_msg *res_msg;
	struct taurus_event_list *event;
	char string[] = "Virtdev IOCTL OP1";
	
	dev_dbg(&rpdev->dev, "%s():%d\n", __FUNCTION__, __LINE__);

	while (!kthread_should_stop()) {
		cmd_msg.hdr.Id = cnt;
		cmd_msg.hdr.Channel = 0xff;
		cmd_msg.hdr.Cmd = R_TAURUS_CMD_IOCTL;
		cmd_msg.hdr.Par1 = TAURUS_VIRTDEV_IOC_OP1;
		cmd_msg.type = TAURUS_VIRTDEV_IOC_OP1;
		memcpy(cmd_msg.params.ioc_op1.string, string, sizeof(string));
		cmd_msg.params.ioc_op1.par1 = 0x1234ABCD;
		cmd_msg.params.ioc_op1.par2 = 0xFFEE1234ABCDEEFF;
		cmd_msg.params.ioc_op1.par3 = 0xCAFE;

		event = devm_kzalloc(&rpdev->dev, sizeof(*event), GFP_KERNEL);
		if (!event)
			return -ENOMEM;

		event->id = cmd_msg.hdr.Id;
		init_completion(&event->ack);
		init_completion(&event->completed);

		write_lock(&idata->event_list_lock);
		list_add(&event->list, &idata->taurus_event_list_head);
		write_unlock(&idata->event_list_lock);

		/* send a message to our remote processor */
		ret = rpmsg_send(rpdev->ept, &cmd_msg, sizeof(struct taurus_virtdev_cmd_msg));
		if (ret) {
			dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", ret);
			return ret;
		}

		dev_info(&rpdev->dev,
			"sent command:\n" \
 			"     cmd_msg.hdr.Id               = 0x%x\n" \
			"     cmd_msg.hdr.Channel          = 0x%x\n" \
			"     cmd_msg.hdr.Cmd              = 0x%x\n" \
			"     cmd_msg.hdr.Par1             = 0x%llx\n" \
			"     cmd_msg.hdr.type             = 0x%x\n" \
			"     cmd_msg.params.ioc_op1.sting = %s\n" \
			"     cmd_msg.params.ioc_op1.par1  = 0x%.8x\n" \
			"     cmd_msg.params.ioc_op1.par2  = 0x%.16llx\n" \
			"     cmd_msg.params.ioc_op1.par3  = 0x%.4x\n",
			cmd_msg.hdr.Id,
			cmd_msg.hdr.Channel,
			cmd_msg.hdr.Cmd,
			cmd_msg.hdr.Par1,
			cmd_msg.type,
			cmd_msg.params.ioc_op1.string,
			cmd_msg.params.ioc_op1.par1,
			cmd_msg.params.ioc_op1.par2,
			cmd_msg.params.ioc_op1.par3);
			

		ret = wait_for_completion_interruptible(&event->ack);
		if (ret == -ERESTARTSYS) {
			/* we were interrupted */
			write_lock(&idata->event_list_lock);
			list_del(&event->list);
			write_unlock(&idata->event_list_lock);
			devm_kfree(&rpdev->dev, event);
			return 0;
		};

		res_msg = &event->result;
		if (res_msg->hdr.Result == R_TAURUS_RES_NACK) {
			dev_info(&rpdev->dev, "command not acknowledged (cmd id=%d)\n", cmd_msg.hdr.Id);
			write_lock(&idata->event_list_lock);
			list_del(&event->list);
			write_unlock(&idata->event_list_lock);
			devm_kfree(&rpdev->dev, event);
			continue;
		}

		dev_info(&rpdev->dev,
			"received ack:\n" \
 			"     event->result.hdr.Id      = 0x%x\n" \
			"     event->result.hdr.Channel = 0x%x\n" \
			"     event->result.hdr.Result  = 0x%x\n" \
			"     event->result.hdr.Aux     = 0x%llx\n",
			event->result.hdr.Id,
			event->result.hdr.Channel,
			event->result.hdr.Result,
			event->result.hdr.Aux);

		ret = wait_for_completion_interruptible(&event->completed);
		if (ret == -ERESTARTSYS) {
			/* we were interrupted */
			return 0;
		};

		dev_info(&rpdev->dev,
			"received result:\n" \
 			"     event->result.hdr.Id                = 0x%x\n" \
			"     event->result.hdr.Channel           = 0x%x\n" \
			"     event->result.hdr.Result            = 0x%x\n" \
			"     event->result.hdr.Aux               = 0x%llx\n" \
			"     event->result.type                  = 0x%x\n" \
			"     event->result.params.ioc_op1.par1   = 0x%.16llx\n" \
			"     event->result.params.ioc_op1.par2   = 0x%.8x\n" \
			"     event->result.params.ioc_op1.string = %s\n" \
			"     event->result.params.ioc_op1.par3   = 0x%.4x\n",
			event->result.hdr.Id,
			event->result.hdr.Channel,
			event->result.hdr.Result,
			event->result.hdr.Aux,
			event->result.type,
			event->result.params.ioc_op1.par1,
			event->result.params.ioc_op1.par2,
			event->result.params.ioc_op1.string,
			event->result.params.ioc_op1.par3);
			
		write_lock(&idata->event_list_lock);
		list_del(&event->list);
		write_unlock(&idata->event_list_lock);

		devm_kfree(&rpdev->dev, event);

		cnt++;
	}
	return ret;
}

static int taurus_sample_cb(struct rpmsg_device *rpdev, void *data, int len,
			void *priv, u32 src)
{
	struct taurus_sample_instance_data *idata = dev_get_drvdata(&rpdev->dev);
	struct taurus_event_list *event;
	struct list_head *i;
	struct taurus_virtdev_res_msg *res = (struct taurus_virtdev_res_msg*)data;
	uint32_t res_id = res->hdr.Id;

	dev_dbg(&rpdev->dev, "%s():%d\n", __FUNCTION__, __LINE__);

	if ((res->hdr.Result == R_TAURUS_CMD_NOP) && (res_id ==0)) {
		/* This is an asynchronous signal sent from the
		 * peripheral, and not an answer of a previously sent
		 * command. Just process the signal and return.*/

		/* process res->hdr.Aux */
		dev_dbg(&rpdev->dev, "Signal received! Aux = %llx\n", res->hdr.Aux);

		return 0;
	}

	/* Go through the list of pending events and check if this
	 * message matches any */
	read_lock(&idata->event_list_lock);
	list_for_each_prev(i, &idata->taurus_event_list_head) {
		event = list_entry(i, struct taurus_event_list, list);
		if (event->id == res_id) {

			memcpy(&event->result, data, len);

			if(event->ack_received) {
				complete(&event->completed);
			} else {
				event->ack_received = 1;
				complete(&event->ack);
			}
			break;
		}
	}
	read_unlock(&idata->event_list_lock);
	
	return 0;
}

static int taurus_sample_probe(struct rpmsg_device *rpdev)
{
	struct taurus_sample_instance_data *idata;

	dev_dbg(&rpdev->dev, "%s():%d\n", __FUNCTION__, __LINE__);
	
	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n",
		rpdev->src, rpdev->dst);

	idata = devm_kzalloc(&rpdev->dev, sizeof(*idata), GFP_KERNEL);
	if (!idata)
		return -ENOMEM;

	INIT_LIST_HEAD(&idata->taurus_event_list_head);
	rwlock_init(&idata->event_list_lock);

	dev_set_drvdata(&rpdev->dev, idata);

	idata->taurus_sample_kthread = kthread_run(&taurus_sample_kthreadfn, (void*)rpdev, "taurus-virtdev");

	return 0;
}

static void taurus_sample_remove(struct rpmsg_device *rpdev)
{
	struct taurus_sample_instance_data *idata = dev_get_drvdata(&rpdev->dev);

	dev_dbg(&rpdev->dev, "%s():%d\n", __FUNCTION__, __LINE__);
	kthread_stop(idata->taurus_sample_kthread);
	dev_info(&rpdev->dev, "taurus sample client driver is removed\n");
}

static struct rpmsg_device_id taurus_driver_sample_id_table[] = {
	{ .name	= "taurus-virtdev" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, taurus_driver_sample_id_table);

static struct rpmsg_driver taurus_sample_client = {
	.drv.name	= KBUILD_MODNAME,
	.id_table	= taurus_driver_sample_id_table,
	.probe		= taurus_sample_probe,
	.callback	= taurus_sample_cb,
	.remove		= taurus_sample_remove,
};
module_rpmsg_driver(taurus_sample_client);

MODULE_DESCRIPTION("Taurus sample client driver");
MODULE_LICENSE("GPL v2");
