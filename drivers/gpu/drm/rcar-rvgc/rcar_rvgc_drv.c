/*************************************************************************/ /*
 rcar_rvgc_drv.c  --  R-Car RVGC DRM driver

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

//#define DEBUG

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/dma-mapping.h>

#include <drm/drm_device.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_drv.h>

#include "rcar_rvgc_drv.h"
#include "rcar_rvgc_kms.h"

#include <linux/rpmsg.h>
#include <linux/of_reserved_mem.h>
#include "r_taurus_rvgc_protocol.h"

#pragma GCC optimize ("-Og")

//#define RCAR_RVGC_DRM_NAME     "rcar-rvgc"
#define RCAR_RVGC_DRM_NAME     "rcar-du"
static bool update_primary_plane = 1;
module_param(update_primary_plane, bool, 0);

/* -----------------------------------------------------------------------------
 * RPMSG operations
 */

static int rcar_rvgc_cb(struct rpmsg_device* rpdev, void* data, int len,
			void* priv, u32 src) {
	struct rcar_rvgc_device* rcrvgc = dev_get_drvdata(&rpdev->dev);
	struct taurus_event_list* event;
	struct list_head* i;
	struct taurus_rvgc_res_msg* res = (struct taurus_rvgc_res_msg*)data;
	uint32_t res_id = res->hdr.Id;

	dev_dbg(&rpdev->dev, "%s():%d\n", __FUNCTION__, __LINE__);

	if ((res->hdr.Result == R_TAURUS_CMD_NOP) && (res_id == 0)) {
		/* This is an asynchronous signal sent from the
		 * peripheral, and not an answer of a previously sent
		 * command. Just process the signal and return.*/

		dev_dbg(&rpdev->dev, "Signal received! Aux = %llx\n", res->hdr.Aux);

		switch (res->hdr.Aux) {
		  case RVGC_PROTOCOL_EVENT_VBLANK_DISPLAY0:
			  set_bit(0, (long unsigned int*)&rcrvgc->vblank_pending);
			  break;
		  case RVGC_PROTOCOL_EVENT_VBLANK_DISPLAY1:
			  set_bit(1, (long unsigned int*)&rcrvgc->vblank_pending);
			  break;
		  case RVGC_PROTOCOL_EVENT_VBLANK_DISPLAY2:
			  set_bit(2, (long unsigned int*)&rcrvgc->vblank_pending);
			  break;
		  case RVGC_PROTOCOL_EVENT_VBLANK_DISPLAY3:
			  set_bit(3, (long unsigned int*)&rcrvgc->vblank_pending);
			  break;
		  default:
			  /* event not recognized */
			  return 0;
		}

		wake_up_interruptible(&rcrvgc->vblank_pending_wait_queue);

		return 0;
	}

	/* Go through the list of pending events and check if this
	 * message matches any */
	read_lock(&rcrvgc->event_list_lock);
	list_for_each_prev(i, &rcrvgc->taurus_event_list_head) {
		event = list_entry(i, struct taurus_event_list, list);
		if (event->id == res_id) {

			memcpy(event->result, data, len);

			if (event->ack_received) {
				complete(&event->completed);
			} else {
				event->ack_received = 1;
				complete(&event->ack);
			}
			//break;
		}
	}
	read_unlock(&rcrvgc->event_list_lock);

	return 0;
}


/* -----------------------------------------------------------------------------
 * DRM operations
 */

static int rcar_rvgc_dumb_create(struct drm_file* file, struct drm_device* dev,
				 struct drm_mode_create_dumb* args) {
	unsigned int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	unsigned int align;

	/* The R8A7779 DU requires a 16 pixels pitch alignment */
	align = 16 * args->bpp / 8;
	args->pitch = roundup(min_pitch, align);

	return drm_gem_cma_dumb_create_internal(file, dev, args);
}

DEFINE_DRM_GEM_CMA_FOPS(rcar_rvgc_fops);

static struct drm_driver rcar_rvgc_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,
	.dumb_create		= rcar_rvgc_dumb_create,
	.major			= 1,
	.minor			= 0,
	.name			= RCAR_RVGC_DRM_NAME,
	.desc			= "Renesas Virtual Graphics Card",
	.date			= "20190408",
	.fops			= &rcar_rvgc_fops,
};


/* -----------------------------------------------------------------------------
 * Platform driver
 */

static void rcar_rvgc_remove(struct rpmsg_device* rpdev) {
	struct rcar_rvgc_device* rcrvgc = dev_get_drvdata(&rpdev->dev);
	struct drm_device* ddev = rcrvgc->ddev;

	if (!rcrvgc->vsync_thread)
		dev_warn(rcrvgc->dev, "vsync_thread is not running\n");
	else
		kthread_stop(rcrvgc->vsync_thread);

	if (rcrvgc->ddev) {
		drm_dev_unregister(ddev);
		drm_kms_helper_poll_fini(ddev);
		drm_mode_config_cleanup(ddev);
		drm_dev_put(ddev);
	}
	return;
}

static int rcar_rvgc_probe(struct rpmsg_device* rpdev) {
	struct rcar_rvgc_device* rcrvgc;
	struct drm_device* ddev;
	struct device_node* rvgc_node;
	int ret = 0;

	printk(KERN_ERR "%s():%d\n", __FUNCTION__, __LINE__);

	/* Allocate and initialize the R-Car device structure. */
	rcrvgc = devm_kzalloc(&rpdev->dev, sizeof(*rcrvgc), GFP_KERNEL);
	if (rcrvgc == NULL)
		return -ENOMEM;

	dev_set_drvdata(&rpdev->dev, rcrvgc);
	rcrvgc->update_primary_plane = update_primary_plane;

	/* Save a link to struct device and struct rpmsg_device */
	rcrvgc->dev = &rpdev->dev;
	rcrvgc->rpdev = rpdev;
	/* TODO: store update_primary_plane parameter in driver struct */

	/* Initialize vblank_pending state */
	rcrvgc->vblank_pending = 0;

	/* Initialize taurus event list and its lock */
	INIT_LIST_HEAD(&rcrvgc->taurus_event_list_head);
	rwlock_init(&rcrvgc->event_list_lock);

	init_waitqueue_head(&rcrvgc->vblank_pending_wait_queue);

	/* Init device memory.
	 *
	 * The underlying device for this driver is of type struct
	 * rpmsg_device and by default it is not configured to be DMA
	 * capable.
	 *
	 * What we are doing here is basically assigning a reserved
	 * memory region (specified in the device tree) from which the
	 * device can allocate DMA'able memory, e.g. for the display
	 * framebuffers.
	 */
	rvgc_node = of_find_node_by_path("/rvgc/rvgc-memory");
	if (!rvgc_node) {
		dev_err(&rpdev->dev, "Cannot find devicetree node \"/rvgc/rvgc-memory\"\n");
		ret = -ENOMEM;
		goto error;
	}
	ret = of_reserved_mem_device_init_by_idx(&rpdev->dev, rvgc_node, 0);
	if (ret) {
		dev_err(&rpdev->dev, "of_reserved_mem_device_init_by_idx() returned %d\n", ret);
		goto error;
	}

	/* DRM/KMS objects */
	ddev = drm_dev_alloc(&rcar_rvgc_driver, &rpdev->dev);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	/* Save a link to struct drm_device (and vice versa) */
	rcrvgc->ddev = ddev;
	ddev->dev_private = rcrvgc;

	ret = rcar_rvgc_modeset_init(rcrvgc);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&rpdev->dev,
				"failed to initialize pipe (%d)\n", ret);
		goto error;
	}

	ddev->irq_enabled = 1;
	ddev->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	/*
	 * Register the DRM device with the core and the connectors with
	 * sysfs.
	 */
	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto error;

	drm_fbdev_generic_setup(ddev, 32);

	DRM_INFO("Device %s probed\n", dev_name(&rpdev->dev));

	return 0;

 error:
	rcar_rvgc_remove(rpdev);

	return ret;
}


static struct rpmsg_device_id taurus_driver_rvgc_id_table[] = {
	{ .name	= "taurus-rvgc" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, taurus_driver_rvgc_id_table);

static struct rpmsg_driver taurus_rvgc_client = {
	.drv.name	= KBUILD_MODNAME,
	.id_table	= taurus_driver_rvgc_id_table,
	.probe		= rcar_rvgc_probe,
	.callback	= rcar_rvgc_cb,
	.remove		= rcar_rvgc_remove,
};
module_rpmsg_driver(taurus_rvgc_client);

MODULE_AUTHOR("Vito Colagiacomo <vito.colagiacomo@renesas.com>");
MODULE_DESCRIPTION("Renesas Virtual Graphics Card DRM Driver");
MODULE_LICENSE("Dual MIT/GPL");
