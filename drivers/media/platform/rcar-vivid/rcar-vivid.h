#ifndef __RCAR_VIVID__
#define __RCAR_VIVID__

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/clk.h>
#include <linux/kref.h>
#include <linux/reset.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-core.h>

#define MAX_VIVID_DEVICE_NUM	8

#define rvivid_dbg(d, fmt, arg...)		dev_dbg(d->dev, fmt, ##arg)
#define rvivid_info(d, fmt, arg...)	    dev_info(d->dev, fmt, ##arg)
#define rvivid_warn(d, fmt, arg...)	    dev_warn(d->dev, fmt, ##arg)
#define rvivid_err(d, fmt, arg...)		dev_err(d->dev, fmt, ##arg)

#define vivid_dbg(d, fmt, arg...)		dev_dbg(d->dev, fmt, ##arg)
#define vivid_info(d, fmt, arg...)	    dev_info(d->dev, fmt, ##arg)
#define vivid_warn(d, fmt, arg...)	    dev_warn(d->dev, fmt, ##arg)
#define vivid_err(d, fmt, arg...)		dev_err(d->dev, fmt, ##arg)

/* Number of HW buffers */
#define HW_BUFFER_NUM 3

struct rcar_vivid_device;

struct vivid_pix_format {
    u32			width;
    u32			height;
    u32			pixelformat;
    u32			field;		/* enum v4l2_field */
    u32			colorspace;	/* enum v4l2_colorspace */
};

enum rvivid_dma_state {
    STOPPED = 0,
    RUNNING,
    STALLED,
    STOPPING,
};

struct taurus_camera_res_msg;

struct taurus_event_list {
    uint32_t id;
    struct taurus_camera_res_msg *result;
    struct list_head list;
    struct completion ack;
    bool ack_received;
    struct completion completed;
};

#if 0
struct rcar_vivid_device {
    struct device *dev;
    struct video_device vdev;
    struct v4l2_device v4l2_dev;
    struct rpmsg_device *rpdev;
    struct v4l2_pix_format format;

    uint8_t buffer_pending;
    wait_queue_head_t buffer_pending_wait_queue;

    struct mutex lock;
    struct vb2_queue queue;
    struct vb2_v4l2_buffer *queue_buf[HW_BUFFER_NUM];
    struct list_head buf_list;
    void *scratch;
    unsigned int sequence;
    enum rvivid_dma_state state;
    dma_addr_t scratch_phys;
    dma_addr_t phys_addr[HW_BUFFER_NUM];
    uint8_t cur_slot;

    int channel;
    struct workqueue_struct *work_queue;
    wait_queue_head_t setup_wait;

    struct task_struct *buffer_thread;

    struct list_head taurus_event_list_head;
    rwlock_t event_list_lock;
};
#endif

struct vivid_v4l2_device {

    struct rcar_vivid_device *rvivid;
    struct device *dev;
    struct video_device vdev;
    struct v4l2_device v4l2_dev;
    struct list_head vivid_list;
    struct v4l2_pix_format format;

    struct mutex lock;
    struct vb2_queue queue;
    struct vb2_v4l2_buffer *queue_buf[HW_BUFFER_NUM];
    struct list_head buf_list;
    void *scratch;
    unsigned int sequence;
    enum rvivid_dma_state state;
    dma_addr_t scratch_phys;
    dma_addr_t phys_addr[HW_BUFFER_NUM];
    uint8_t cur_slot;

    int channel;
    struct workqueue_struct *work_queue;
    wait_queue_head_t setup_wait;

    wait_queue_head_t buffer_pending_wait_queue;
    unsigned char buffer_pending;
    struct task_struct *buffer_thread;
};

struct rcar_vivid_device {
    struct device *dev;
    struct rpmsg_device *rpdev;
    struct list_head taurus_event_list_head;
    rwlock_t event_list_lock;
    int channel_num;
    struct vivid_v4l2_device *vivid[MAX_VIVID_DEVICE_NUM];
};

int rcar_vivid_v4l2_register(struct vivid_v4l2_device *rvivid);
int rcar_vivid_queue_init(struct vivid_v4l2_device *rvivid);
void vivid_fill_hw_slot(struct vivid_v4l2_device *rvivid, int slot);
#endif
