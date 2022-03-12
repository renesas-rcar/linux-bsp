/*************************************************************************/ /*
 rcar-vivid-drv.c --  R-Car Para-VIN driver

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
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include <linux/rpmsg.h>
#include <linux/of_reserved_mem.h>

#include "rcar-vivid.h"
#include "rcar-vivid-taurus.h"
#include "r_taurus_camera_protocol.h"


/* -----------------------------------------------------------------------------
 * RPMSG operations
 */

static int rcar_vivid_cb(struct rpmsg_device *rpdev, void *data, int len,
            void *priv, u32 src)
{

    struct rcar_vivid_device *rvivid = dev_get_drvdata(&rpdev->dev);
    struct taurus_event_list *event;
    struct list_head *i;
    struct taurus_camera_res_msg *res = (struct taurus_camera_res_msg*)data;
    uint32_t res_id = res->hdr.Id;
    int slot;
    int channel;
    int empty_buf_cnt;
    dev_dbg(&rpdev->dev,"Result %x id %x channel %x Per %x Aux %llx\n ",
        res->hdr.Result, res_id, res->hdr.Channel, res->hdr.Per, res->hdr.Aux);

    if ((res->hdr.Result == R_TAURUS_CMD_NOP) && (res_id == 0)) {
        /* This is an asynchronous signal sent from the
         * peripheral, and not an answer of a previously sent
         * command. Just process the signal and return.*/

        dev_dbg(&rpdev->dev, "Signal received! Aux = %llx\n", res->hdr.Aux);
        channel = TAURUS_CAMERA_EVT_CHANNEL(res->hdr.Aux);
        slot = TAURUS_CAMERA_EVT_FRAME_READY_FRAME_ID(res->hdr.Aux);
        empty_buf_cnt = TAURUS_CAMERA_EVT_FRAME_READY_EMPTY_BUF(res->hdr.Aux);
        /* Nothing to do if capture status is 'STOPPED' */
        if (rvivid->vivid[channel]->state == STOPPED) {
            rvivid_dbg(rvivid, "IRQ while state stopped\n");
            goto done;
        }

        /* Nothing to do if capture status is 'STOPPING' */
        if (rvivid->vivid[channel]->state == STOPPING) {
            rvivid_dbg(rvivid, "IRQ while state stopping\n");
            goto done;
        }

        /* Capture frame */
        if (rvivid->vivid[channel]->queue_buf[slot]) {
            rvivid->vivid[channel]->queue_buf[slot]->field = rvivid->vivid[channel]->format.field;
            rvivid->vivid[channel]->queue_buf[slot]->sequence = rvivid->vivid[channel]->sequence;
            rvivid->vivid[channel]->queue_buf[slot]->vb2_buf.timestamp = ktime_get_ns();
            vb2_buffer_done(&rvivid->vivid[channel]->queue_buf[slot]->vb2_buf,
                    VB2_BUF_STATE_DONE);
            rvivid->vivid[channel]->queue_buf[slot] = NULL;
        } else {
            /* Scratch buffer was used, dropping frame. */
            rvivid_dbg(rvivid, "Dropping frame %u\n", rvivid->vivid[channel]->sequence);
        }

        rvivid->vivid[channel]->sequence++;

        /* Prepare for next frame */
        vivid_fill_hw_slot(rvivid->vivid[channel], slot);
        set_bit(slot, (long unsigned int*) &rvivid->vivid[channel]->buffer_pending);
        if(0 == empty_buf_cnt)
            wake_up_interruptible(&rvivid->vivid[channel]->buffer_pending_wait_queue);
done:
        return 0;

    }

    /* Go through the list of pending events and check if this
     * message matches any */
    read_lock(&rvivid->event_list_lock);
    list_for_each_prev(i, &rvivid->taurus_event_list_head) {
        event = list_entry(i, struct taurus_event_list, list);
        if (event->id == res_id) {

            memcpy(event->result, res, sizeof(struct taurus_camera_res_msg));

            if(event->ack_received) {
                complete(&event->completed);
            } else {
                event->ack_received = 1;
                complete(&event->ack);
            }
            //break;
        }
    }
    read_unlock(&rvivid->event_list_lock);

    return 0;
}


/* -----------------------------------------------------------------------------
 * Platform driver
 */

static void rcar_vivid_remove(struct rpmsg_device *rpdev)
{
    int i = 0;
    struct rcar_vivid_device *rvivid = dev_get_drvdata(&rpdev->dev);
    struct vivid_v4l2_device *vivid;
    struct video_device *vdev;
    for (i = 0;i < rvivid->channel_num; i++) {
        vivid = rvivid->vivid[i];
        if(vivid->buffer_thread)
            kthread_stop(vivid->buffer_thread);
        vdev = &vivid->vdev;
        if(vdev != NULL) {
            video_unregister_device(vdev);
        }
    }
    return;
}

static int rcar_vivid_probe(struct rpmsg_device *rpdev)
{
    struct rcar_vivid_device *rvivid;
    struct device_node *rvivid_node;
    int ret = 0;
    struct taurus_camera_res_msg res_msg;
    int i = 0;
    struct vivid_v4l2_device *vivid[MAX_VIVID_DEVICE_NUM];

    pr_info("%s():%d\n", __FUNCTION__, __LINE__);
    /* Allocate and initialize the R-Car device structure. */
    rvivid = devm_kzalloc(&rpdev->dev, sizeof(struct rcar_vivid_device), GFP_KERNEL);

    if (rvivid == NULL)
        return -ENOMEM;

    dev_set_drvdata(&rpdev->dev, rvivid);

    /* Save a link to struct device and struct rpmsg_device */
    rvivid->dev = &rpdev->dev;
    rvivid->rpdev = rpdev;

    /* Initialize taurus event list and its lock */
    INIT_LIST_HEAD(&rvivid->taurus_event_list_head);
    rwlock_init(&rvivid->event_list_lock);

    ret = vivid_taurus_get_info(rvivid,&res_msg);
    if (ret || rvivid->channel_num > MAX_VIVID_DEVICE_NUM)
        goto error;

    rvivid_info(rvivid, "check vivid taurus cameras num %d\n", rvivid->channel_num);
    rvivid_node = of_find_node_by_path("/rcar-vivid/rvivid-memory");
    if (!rvivid_node) {
        dev_err(&rpdev->dev, "Cannot find devicetree node \"/rcar-vivid/rvivid-memory\"\n");
        ret = -ENOMEM;
        goto error;
    }

    ret = of_reserved_mem_device_init_by_idx(rvivid->dev, rvivid_node, 0);
    if (ret) {
        dev_err(&rpdev->dev, "of_reserved_mem_device_init_by_idx() returned %d\n", ret);
        goto error;
    }

/*
    ret = of_reserved_mem_device_init_by_idx(&rpdev->dev, rvivid_node, 0);
    if (ret) {
        dev_err(&rpdev->dev, "of_reserved_mem_device_init_by_idx() returned %d\n", ret);
        goto error;
    }
*/
    for (i = 0;i < rvivid->channel_num; i++) {
        vivid[i] = devm_kzalloc(rvivid->dev, sizeof(*vivid[i]), GFP_KERNEL);
        if (vivid[i] == NULL) {
            ret = -ENOMEM;
            goto error;
        }
        vivid[i]->dev = rvivid->dev;
    #if 0
        memset(vivid_name, 0 ,sizeof(vivid_name));
        sprintf(vivid_name, "rcar vivid%d", i);
        strcpy(vivid[i]->dev->name, vivid_name);
    #endif
        vivid[i]->channel = i;
        init_waitqueue_head(&vivid[i]->buffer_pending_wait_queue);
        vivid[i]->buffer_pending = 0;
        vivid[i]->rvivid = rvivid;

        ret = rcar_vivid_queue_init(vivid[i]);
        if (ret) {
            rvivid_err(rvivid, "Failed init rcar vivid%d queue\n", i);
            goto error;
        }

        ret = rcar_vivid_v4l2_register(vivid[i]);

        if (ret) {
            rvivid_err(rvivid, "Failed to register video device vivid%d\n", i);
            goto error;
        }

        rvivid->vivid[i] = vivid[i];
    }
    return 0;
error:
    rcar_vivid_remove(rpdev);
    return ret;
}


static struct rpmsg_device_id taurus_driver_vivid_id_table[] = {
    { .name	= "taurus-vivid" },
    { .name = "taurus-camera"},
    {},
};
MODULE_DEVICE_TABLE(rpmsg, taurus_driver_vivid_id_table);

static struct rpmsg_driver taurus_vivid_client = {
    .drv.name	= KBUILD_MODNAME,
    .id_table	= taurus_driver_vivid_id_table,
    .probe		= rcar_vivid_probe,
    .callback	= rcar_vivid_cb,
    .remove		= rcar_vivid_remove,
};
module_rpmsg_driver(taurus_vivid_client);

MODULE_DESCRIPTION("Renesas Virtual Camera Driver");
MODULE_LICENSE("Dual MIT/GPL");
