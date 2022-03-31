#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-rect.h>
#include <media/videobuf2-dma-contig.h>

#include "rcar-vivid-taurus.h"
#include "rcar-vivid.h"
#include "r_taurus_camera_protocol.h"

#define VIVID_MAX_WIDTH				4096
#define VIVID_MAX_HEIGHT			4096

/* Address alignment mask for HW buffers */
#define HW_BUFFER_MASK 0x7f

#define VIVID_DEFAULT_FORMAT		V4L2_PIX_FMT_ABGR32
#define VIVID_DEFAULT_WIDTH		800
#define VIVID_DEFAULT_HEIGHT		600
#define VIVID_DEFAULT_FIELD		V4L2_FIELD_NONE
#define VIVID_DEFAULT_COLORSPACE	V4L2_COLORSPACE_SRGB

struct vivid_buffer {
    struct vb2_v4l2_buffer vb;
    struct list_head list;
};

#define to_buf_list(vb2_buffer) (&container_of(vb2_buffer, \
                           struct vivid_buffer, \
                           vb)->list)


static const struct vivid_pix_format vivid_default_format[]= {
    {
        .width = VIVID_DEFAULT_WIDTH,
        .height = VIVID_DEFAULT_HEIGHT,
        .pixelformat = VIVID_DEFAULT_FORMAT,
        .field = VIVID_DEFAULT_FIELD,
        .colorspace = VIVID_DEFAULT_COLORSPACE,
    },
};

static int get_bpp_from_format(u32 pixelformat)
{
    switch (pixelformat) {
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_NV16:
        return 1;
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_ARGB555:
        return 2;
        case V4L2_PIX_FMT_ABGR32:
        case V4L2_PIX_FMT_XBGR32:
        return 4;
        default:
        return -EINVAL;
    }
}
#if 0
static struct vivid_pix_format to_vivid_format(struct v4l2_pix_format pix)
{
     struct vivid_pix_format rpix;
     rpix.colorspace = pix.colorspace;
     rpix.field = pix.colorspace;
     rpix.width = pix.width;
     rpix.height = pix.height;
     rpix.pixelformat = pix.pixelformat;
     return rpix;
}
#endif

static u32 vivid_format_bytesperline(struct v4l2_pix_format *pix)
{
    return pix->width * get_bpp_from_format(pix->pixelformat);
}

static u32 vivid_format_sizeimage(struct v4l2_pix_format *pix)
{
    if (pix->pixelformat == V4L2_PIX_FMT_NV16)
        return pix->bytesperline * pix->height * 2;

    if (pix->pixelformat == V4L2_PIX_FMT_NV12)
        return pix->bytesperline * pix->height * 3 / 2;

    return pix->bytesperline * pix->height;
}

static void __vivid_format_aling_update(struct vivid_v4l2_device *vivid,
                       struct v4l2_pix_format *pix)
{
    u32 walign;

    /* HW limit width to a multiple of 32 (2^5) for NV16/12 else 2 (2^1) */
    if (pix->pixelformat == V4L2_PIX_FMT_NV12 ||
        pix->pixelformat == V4L2_PIX_FMT_NV16)
        walign = 5;
    else if (pix->pixelformat == V4L2_PIX_FMT_YUYV ||
         pix->pixelformat == V4L2_PIX_FMT_UYVY)
        walign = 1;
    else
        walign = 0;

    /* Limit to VIN capabilities */
    v4l_bound_align_image(&pix->width, 5, VIVID_MAX_WIDTH, walign,
                  &pix->height, 2, VIVID_MAX_HEIGHT, 0, 0);

    pix->bytesperline = vivid_format_bytesperline(pix);
    pix->sizeimage = vivid_format_sizeimage(pix);
}


static void vivid_format_align(struct vivid_v4l2_device *vivid, struct v4l2_pix_format *pix)
{
    int width;
    switch (pix->field) {
    case V4L2_FIELD_TOP:
    case V4L2_FIELD_BOTTOM:
    case V4L2_FIELD_NONE:
    case V4L2_FIELD_INTERLACED_TB:
    case V4L2_FIELD_INTERLACED_BT:
    case V4L2_FIELD_INTERLACED:
        break;
    case V4L2_FIELD_SEQ_TB:
    case V4L2_FIELD_SEQ_BT:
        /*
         * Due to extra hardware alignment restrictions on
         * buffer addresses for multi plane formats they
         * are not (yet) supported. This would be much simpler
         * once support for the UDS scaler is added.
         *
         * Support for multi plane formats could be supported
         * by having a different partitioning strategy when
         * capturing the second field (start capturing one
         * quarter in to the buffer instead of one half).
         */

        if (pix->pixelformat == V4L2_PIX_FMT_NV16)
            pix->pixelformat = VIVID_DEFAULT_FORMAT;

        /*
         * For sequential formats it's needed to write to
         * the same buffer two times to capture both the top
         * and bottom field. The second time it is written
         * an offset is needed as to not overwrite the
         * previous captured field. Due to hardware limitations
         * the offsets must be a multiple of 128. Try to
         * increase the width of the image until a size is
         * found which can satisfy this constraint.
         */

        width = pix->width;
        while (width < VIVID_MAX_WIDTH) {
            pix->width = width++;

            __vivid_format_aling_update(vivid, pix);

            if (((pix->sizeimage / 2) & HW_BUFFER_MASK) == 0)
                break;
        }
        break;
    case V4L2_FIELD_ALTERNATE:
        /*
         * Driver does not (yet) support outputting ALTERNATE to a
         * userspace. It does support outputting INTERLACED so use
         * the VIN hardware to combine the two fields.
         */
        pix->field = V4L2_FIELD_INTERLACED;
        pix->height *= 2;
        break;
    default:
        pix->field = VIVID_DEFAULT_FIELD;
        break;
    }

    __vivid_format_aling_update(vivid, pix);
}

static void vivid_format_update(struct vivid_v4l2_device *vivid, struct v4l2_pix_format *pix)
{
    pix->colorspace = VIVID_DEFAULT_COLORSPACE;
    pix->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(pix->colorspace);
    pix->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix->colorspace);
    pix->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true, pix->colorspace,
                              pix->ycbcr_enc);
    vivid_format_align(vivid, pix);

}

static int vivid_thread_fn(void *data)
{
    struct vivid_v4l2_device *vivid = (struct vivid_v4l2_device*)data;
    struct taurus_camera_res_msg res_msg;
    struct taurus_camera_buffer buffer[HW_BUFFER_NUM];
    int i;
    int index;
    while (!kthread_should_stop()) {
        wait_event_interruptible(vivid->buffer_pending_wait_queue, vivid->buffer_pending);
        for(i = 0; i < HW_BUFFER_NUM; i++) {
            index = ffs(vivid->buffer_pending) - 1;
            if(index < 0) {
                break;
            } else {
                clear_bit(index, (long unsigned int*) &vivid->buffer_pending);
                buffer[i].index = index;
                buffer[i].address = vivid->phys_addr[index];
            }
        }
        if(vivid->state == RUNNING)
            vivid_taurus_feed_buffers(vivid, buffer, i, &res_msg);
        memset(&res_msg,0,sizeof(res_msg));
    }
    dev_dbg(vivid->dev, "vivid thread exiting\n");
    return 0;
}


static int vivid_open(struct file *file)
{
    struct vivid_v4l2_device *vivid = video_drvdata(file);
    int ret;
    vivid_dbg(vivid,"%s\n", __func__);
    file->private_data = vivid;
    ret = v4l2_fh_open(file);
    return ret;
}

static int vivid_release(struct file *file)
{
    struct vivid_v4l2_device *vivid = video_drvdata(file);
    bool fh_singular;
    int ret;
    vivid_dbg(vivid,"%s\n", __func__);

    fh_singular = v4l2_fh_is_singular_file(file);
    /* Save the singular status before we call the clean-up helper */
    fh_singular = v4l2_fh_is_singular_file(file);

    /* the release helper will cleanup any on-going streaming */
    ret = _vb2_fop_release(file, NULL);
#if 0
    /*
     * If this was the last open file.
     * Then de-initialize hw module.
     */
    struct taurus_camera_res_msg res_msg;
    if (fh_singular)
        ret = vivid_taurus_channel_release(vivid,&res_msg);
#endif
    return ret;
}


static const struct v4l2_file_operations vivid_fops = {
    .owner		= THIS_MODULE,
    .unlocked_ioctl	= video_ioctl2,
    .open		= vivid_open,
    .release	= vivid_release,
    .poll		= vb2_fop_poll,
    .mmap		= vb2_fop_mmap,
    .read		= vb2_fop_read,
};



static int vivid_querycap(struct file *file, void *priv,
             struct v4l2_capability *cap)
{
    struct vivid_v4l2_device *vivid = video_drvdata(file);
    vivid_dbg(vivid, "%s \n", __func__);
    strlcpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
    strlcpy(cap->card, "R_Car_VIVID", sizeof(cap->card));
    snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
         dev_name(vivid->dev));
    return 0;
}

static int vivid_try_fmt_vid_cap(struct file *file, void *priv,
                struct v4l2_format *f)
{
    struct vivid_v4l2_device *vivid = video_drvdata(file);
    int ret;
    vivid_dbg(vivid, "%s \n", __func__);
    ret = (f->fmt.pix.width != vivid->format.width);
    ret |= (f->fmt.pix.height != vivid->format.height);
    ret |= (f->fmt.pix.pixelformat!= vivid->format.pixelformat);
    ret |= (f->fmt.pix.field != vivid->format.field);

	pr_info("Capturing with: %dx%d, format:%d, field:%d\n",
		f->fmt.pix.width, f->fmt.pix.height,
		f->fmt.pix.pixelformat, f->fmt.pix.field);

    if(ret) {
	/* Right now only support 1 format
	 * w:720, h:480, pf: 875713089(ABGR32), field: 1(NONE)
	 */
	pr_err("Support only: %dx%d, format:%d(ABGR32), field:%d(NONE)\n",
		vivid->format.width, vivid->format.height,
		vivid->format.pixelformat, vivid->format.field);
        return -EPIPE;
    }
    f->fmt.pix.colorspace = vivid->format.colorspace;
    return 0;
}

static int vivid_g_fmt_vid_cap(struct file *file, void *priv,
                  struct v4l2_format *f)
{
    struct vivid_v4l2_device *vivid = video_drvdata(file);
    vivid_dbg(vivid, "%s \n", __func__);

    f->fmt.pix = vivid->format;

    return 0;
}

static int vivid_s_fmt_vid_cap(struct file *file, void *priv,
                  struct v4l2_format *f)
{
    struct vivid_v4l2_device *vivid = video_drvdata(file);
    int ret;
    vivid_dbg(vivid, "%s \n", __func__);

    if (vb2_is_busy(&vivid->queue))
        return -EBUSY;

    ret = vivid_try_fmt_vid_cap(file, NULL,f);
    if (ret)
        return ret;

    vivid_format_update(vivid, &f->fmt.pix);

    vivid->format = f->fmt.pix;

    return 0;
}

static int vivid_enum_fmt_vid_cap(struct file *file, void *priv,
                 struct v4l2_fmtdesc *f)
{
    struct vivid_v4l2_device *vivid = video_drvdata(file);
    vivid_dbg(vivid, "%s \n", __func__);
    if (f->index >= 1)
        return -EINVAL;

    f->pixelformat = VIVID_DEFAULT_FORMAT;

    return 0;
}


static const struct v4l2_ioctl_ops vivid_ioctl_ops = {
    .vidioc_querycap		= vivid_querycap,

    .vidioc_try_fmt_vid_cap		= vivid_try_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap		= vivid_g_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap		= vivid_s_fmt_vid_cap,
    .vidioc_enum_fmt_vid_cap	= vivid_enum_fmt_vid_cap,


    .vidioc_reqbufs			= vb2_ioctl_reqbufs,
    .vidioc_create_bufs		= vb2_ioctl_create_bufs,
    .vidioc_querybuf		= vb2_ioctl_querybuf,
    .vidioc_qbuf			= vb2_ioctl_qbuf,
    .vidioc_dqbuf			= vb2_ioctl_dqbuf,
    .vidioc_expbuf			= vb2_ioctl_expbuf,
    .vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
    .vidioc_streamon		= vb2_ioctl_streamon,
    .vidioc_streamoff		= vb2_ioctl_streamoff,
};



static void vivid_set_slot_addr(struct vivid_v4l2_device *vivid, int slot, dma_addr_t addr)
{
    if(slot < ARRAY_SIZE(vivid->phys_addr))
        vivid->phys_addr[slot] = addr;
    return;
#if 0
    const struct vivid_video_format *fmt;
    dma_addr_t offset;

    fmt = vivid_format_from_pixel(vivid->format.pixelformat);

    /*
     * There is no HW support for composition do the beast we can
     * by modifying the buffer offset
     */
    offsetx = vivid->compose.left * fmt->bpp;
    offsety = vivid->compose.top * vivid->format.bytesperline;
    offset = addr + offsetx + offsety;

    /*
     * The address needs to be 128 bytes aligned. Driver should never accept
     * settings that do not satisfy this in the first place...
     */
    if (WARN_ON((offsetx | offsety | offset) & HW_BUFFER_MASK))
        return;

    vivid_write(vivid, offset, VNMB_REG(slot));
#endif
}

/*
 * Moves a buffer from the queue to the HW slot. If no buffer is
 * available use the scratch buffer. The scratch buffer is never
 * returned to userspace, its only function is to enable the capture
 * loop to keep running.
 */
void vivid_fill_hw_slot(struct vivid_v4l2_device *vivid, int slot)
{
    struct vivid_buffer *buf;
    struct vb2_v4l2_buffer *vbuf;
    dma_addr_t phys_addr;
    /* A already populated slot shall never be overwritten. */
    if (WARN_ON(vivid->queue_buf[slot] != NULL))
        return;

    vivid_dbg(vivid, "Filling HW slot: %d\n", slot);

    if (list_empty(&vivid->buf_list)) {
        vivid->queue_buf[slot] = NULL;
        phys_addr = vivid->scratch_phys;
    } else {
        /* Keep track of buffer we give to HW */
        buf = list_entry(vivid->buf_list.next, struct vivid_buffer, list);
        vbuf = &buf->vb;
        list_del_init(to_buf_list(vbuf));
        vivid->queue_buf[slot] = vbuf;

        /* Setup DMA */
        phys_addr = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 0);
    }
    vivid_set_slot_addr(vivid, slot, phys_addr);
    return;
}

static int vivid_capture_on(struct vivid_v4l2_device *vivid)
{
    int ret;
    struct taurus_camera_res_msg res_msg;
    ret = vivid_taurus_channel_start(vivid, &res_msg);
    return ret;
}
static void vivid_capture_stop(struct vivid_v4l2_device *vivid)
{
    struct taurus_camera_res_msg res_msg;
    vivid_taurus_channel_stop(vivid, &res_msg);
    vivid_taurus_channel_release(vivid, &res_msg);
    return;
}
static int vivid_capture_start(struct vivid_v4l2_device *vivid)
{
    int slot, limit;
    struct taurus_camera_res_msg res_msg;
    limit = HW_BUFFER_NUM;
    for (slot = 0; slot < limit; slot++)
        vivid_fill_hw_slot(vivid, slot);

    vivid_taurus_channel_init(vivid, &res_msg);
    vivid_dbg(vivid, "height %d\n",
        res_msg.params.ioc_channel_init.channel_info.height);
    vivid_dbg(vivid, "width %d\n",
        res_msg.params.ioc_channel_init.channel_info.width);
    vivid_dbg(vivid, "vacant_buf_cell_cnt %d\n",
        res_msg.params.ioc_channel_init.channel_info.vacant_buf_cell_cnt);
    vivid_capture_on(vivid);
    vivid->state = RUNNING;
    return 0;
}


static int vivid_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
                unsigned int *nplanes, unsigned int sizes[],
                struct device *alloc_devs[])

{
    struct  vivid_v4l2_device *vivid = vb2_get_drv_priv(vq);
    vivid_dbg(vivid, "%s \n", __func__);

    /* Make sure the image size is large enough. */
    if (*nplanes)
        return sizes[0] < vivid->format.sizeimage ? -EINVAL : 0;

    *nplanes = 1;
    sizes[0] = vivid->format.sizeimage;

    return 0;
};

static int vivid_buffer_prepare(struct vb2_buffer *vb)
{
    struct  vivid_v4l2_device *vivid = vb2_get_drv_priv(vb->vb2_queue);
    unsigned long size = vivid->format.sizeimage;
    vivid_dbg(vivid, "%s \n", __func__);

    if (vb2_plane_size(vb, 0) < size) {
        vivid_err(vivid, "buffer too small (%lu < %lu)\n",
            vb2_plane_size(vb, 0), size);
        return -EINVAL;
    }

    vb2_set_plane_payload(vb, 0, size);

    return 0;
}

static void vivid_buffer_queue(struct vb2_buffer *vb)
{
    struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
    struct vivid_v4l2_device *vivid = vb2_get_drv_priv(vb->vb2_queue);
    vivid_dbg(vivid, "%s \n", __func__);

    list_add_tail(to_buf_list(vbuf), &vivid->buf_list);

}

/* Need to hold qlock before calling */
static void return_all_buffers(struct vivid_v4l2_device *vivid,
                   enum vb2_buffer_state state)
{
    struct vivid_buffer *buf, *node;
    int i;

    for (i = 0; i < HW_BUFFER_NUM; i++) {
        if (vivid->queue_buf[i]) {
            vb2_buffer_done(&vivid->queue_buf[i]->vb2_buf,
                    state);
            vivid->queue_buf[i] = NULL;
        }
    }

    list_for_each_entry_safe(buf, node, &vivid->buf_list, list) {
        vb2_buffer_done(&buf->vb.vb2_buf, state);
        list_del(&buf->list);
    }
}

static int vivid_start_streaming(struct vb2_queue *vq, unsigned int count)
{
    struct vivid_v4l2_device *vivid = vb2_get_drv_priv(vq);
    int ret;
    vivid_dbg(vivid, "%s \n", __func__);

    /* Allocate scratch buffer. */
    vivid->scratch = dma_alloc_coherent(vivid->dev, vivid->format.sizeimage,
                      &vivid->scratch_phys, GFP_KERNEL);
    if (!vivid->scratch) {
        return_all_buffers(vivid, VB2_BUF_STATE_QUEUED);
        vivid_err(vivid, "Failed to allocate scratch buffer\n");
        return -ENOMEM;
    }

    vivid->sequence = 0;
    ret = vivid_capture_start(vivid);
    if (ret) {
        return_all_buffers(vivid, VB2_BUF_STATE_QUEUED);
        goto out;
    }

    return 0;
out:
    if (ret)
        dma_free_coherent(vivid->dev, vivid->format.sizeimage, vivid->scratch,
                  vivid->scratch_phys);

    return ret;

}

static void vivid_stop_streaming(struct vb2_queue *vq)
{
    struct vivid_v4l2_device *vivid = vb2_get_drv_priv(vq);
    vivid_dbg(vivid, "%s \n", __func__);

    vivid->state = STOPPING;
    vivid_capture_stop(vivid);
    vivid->state = STOPPED;

    if (vivid->state != STOPPED) {
        /*
         * If this happens something have gone horribly wrong.
         * Set state to stopped to prevent the interrupt handler
         * to make things worse...
         */
        vivid_err(vivid, "Failed stop HW, something is seriously broken\n");
        vivid->state = STOPPED;
    }

    /* Release all active buffers */
    return_all_buffers(vivid, VB2_BUF_STATE_ERROR);
    dma_free_coherent(vivid->dev, vivid->format.sizeimage,
                  vivid->scratch, vivid->scratch_phys);
}



static const struct vb2_ops vivid_qops = {
    .queue_setup		= vivid_queue_setup,
    .buf_prepare		= vivid_buffer_prepare,
    .buf_queue		= vivid_buffer_queue,
    .start_streaming	= vivid_start_streaming,
    .stop_streaming		= vivid_stop_streaming,
    .wait_prepare		= vb2_ops_wait_prepare,
    .wait_finish		= vb2_ops_wait_finish,
};

int rcar_vivid_queue_init(struct vivid_v4l2_device *vivid)
{
    struct vb2_queue *q = &vivid->queue;
    int i, ret;

    ret = v4l2_device_register(vivid->dev, &vivid->v4l2_dev);
    if (ret)
        return ret;

    mutex_init(&vivid->lock);
    INIT_LIST_HEAD(&vivid->buf_list);
    vivid->state = STOPPED;
    init_waitqueue_head(&vivid->setup_wait);

    for (i = 0; i < HW_BUFFER_NUM; i++)
        vivid->queue_buf[i] = NULL;

    /* buffer queue */
    q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    q->io_modes = VB2_MMAP | VB2_READ | VB2_DMABUF | VB2_USERPTR;
    q->lock = &vivid->lock;
    q->drv_priv = vivid;
    q->buf_struct_size = sizeof(struct vivid_buffer);
    q->ops = &vivid_qops;
    q->mem_ops = &vb2_dma_contig_memops;
    q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    q->min_buffers_needed = 1;
    q->dev = vivid->dev;

    ret = vb2_queue_init(q);
    if (ret) {
        vivid_err(vivid, "failed to initialize VB2 queue\n");
        return ret;
    }
    return ret;
}



int rcar_vivid_v4l2_register(struct vivid_v4l2_device *vivid)
{
    struct video_device *vdev = &vivid->vdev;
    int ret;
    struct taurus_camera_res_msg res_msg;

    snprintf(vdev->name, sizeof(vdev->name), "VIVID%u output", vivid->channel);
    vdev->release = video_device_release_empty;
    vdev->v4l2_dev = &vivid->v4l2_dev;
    vdev->queue = &vivid->queue;
    vdev->lock = &vivid->lock;
    vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
        V4L2_CAP_READWRITE;

    vdev->fops = &vivid_fops;
    vdev->ioctl_ops = &vivid_ioctl_ops;
    vdev->dev_parent = vivid->dev;

    memset(&res_msg, 0, sizeof(res_msg));
    ret = vivid_taurus_get_channel_info(vivid, &res_msg);
    if (ret) {
        pr_info("get channel info failed \n");
        vivid->format.width = vivid_default_format[0].width;
        vivid->format.height = vivid_default_format[0].height;
    }
    vivid->format.pixelformat	= vivid_default_format[0].pixelformat;
    vivid->format.field = vivid_default_format[0].field;
    vivid->format.colorspace = vivid_default_format[0].colorspace;
    vivid_format_align(vivid, &vivid->format);

    ret = video_register_device(&vivid->vdev, VFL_TYPE_VIDEO, -1);
    if (ret) {
        vivid_err(vivid, "Failed to register video device\n");
        return ret;
    }
    video_set_drvdata(&vivid->vdev, vivid);
    v4l2_info(&vivid->v4l2_dev,"Device registered as %s\n",
          video_device_node_name(&vivid->vdev));
    v4l2_info(&vivid->v4l2_dev,"format W:%d H:%d \n", vivid->format.width, vivid->format.height);

    if (vivid->buffer_thread)
        dev_warn(vivid->dev, "buffer_thread is already running\n");
    else
        vivid->buffer_thread = kthread_run(vivid_thread_fn,
                        vivid,
                        "rcar vivid buffer kthread");

    return ret;

}
