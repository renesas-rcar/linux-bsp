#ifndef __RCAR_VIVID_TAURUS_H__
#define __RCAR_VIVID_TAURUS_H__

#include <linux/types.h>
#include <taurus/r_taurus_bridge.h>
#include <taurus/r_taurus_protocol_ids.h>
#include "rcar-vivid.h"

struct taurus_camera_res_msg;
struct taurus_camera_buffer;

int vivid_taurus_channel_init(struct vivid_v4l2_device *vivid,
            struct taurus_camera_res_msg *res_msg);

int vivid_taurus_channel_start(struct vivid_v4l2_device *vivid,
            struct taurus_camera_res_msg *res_msg);


int vivid_taurus_channel_stop(struct vivid_v4l2_device *vivid,
            struct taurus_camera_res_msg *res_msg);

int vivid_taurus_feed_buffer(struct vivid_v4l2_device *vivid,
            uint32_t address,int slot,
            struct taurus_camera_res_msg *res_msg);

int vivid_taurus_feed_buffers(struct vivid_v4l2_device *vivid,
            struct taurus_camera_buffer *buffer, int buf_cnt,
            struct taurus_camera_res_msg *res_msg);

int vivid_taurus_channel_release(struct vivid_v4l2_device *vivid,
            struct taurus_camera_res_msg *res_msg);

int vivid_taurus_get_channel_info(struct vivid_v4l2_device *vivid,
            struct taurus_camera_res_msg *res_msg);

int vivid_taurus_get_info(struct rcar_vivid_device *vivid,
            struct taurus_camera_res_msg *res_msg);

#endif
