#ifndef R_TAURUS_CAMERA_PROTOCOL_H
#define R_TAURUS_CAMERA_PROTOCOL_H

#include <taurus/r_taurus_bridge.h>
#include <taurus/r_taurus_protocol_ids.h>


#ifndef __packed
# define __packed       __attribute__((__packed__))
#endif

/*********** Camera event identifiers ************/
/* Field in R_TAURUS_ResultMsg_t.Aux (uint64_t)
 * Service ID[31:24] - ID of taurus service (TAURUS_PROTOCOL_CAMERA_ID)
 * Event ID  [23:20] - Event ID
 * Channel   [19:16] - Camera channel index
 */
#define TAURUS_CAMERA_EVT_ID(aux)               (((uint64_t)aux >> 20) & 0xf)
#define TAURUS_CAMERA_EVT_CHANNEL(aux)          (((uint64_t)aux >> 16) & 0xf)

/*
 * TAURUS_CAMERA_EVT_FRAME_READY
 * Frame ID   [63:32] - ID of current ready frame buffer previously allocated by guest.
 *                      (i.e. struct vb2_v4l2_buffer.vb2_buf.index)
 * Empty Buf  [15: 8] - Number of available empty buffers to receive next frame.
 * Vacant Cell[ 7: 0] - Number of vacant buffer cell that can feed.
 */
#define TAURUS_CAMERA_EVT_FRAME_READY                    (0)
#define TAURUS_CAMERA_EVT_FRAME_READY_VAL(chn, emp_buf, vacant_cell, frame_id)        \
                            ((uint64_t)frame_id << 32) |                              \
                            ((uint64_t)(TAURUS_PROTOCOL_CAMERA_ID & 0xff) << 24) |    \
                            ((uint64_t)(TAURUS_CAMERA_EVT_FRAME_READY & 0xf) << 20) | \
                            ((uint64_t)(chn & 0xf) << 16) |                           \
                            ((uint64_t)(emp_buf & 0xff) << 8) |                       \
                            (uint64_t)(vacant_cell & 0xff)

#define TAURUS_CAMERA_EVT_FRAME_READY_EMPTY_BUF(aux)     (((uint64_t)aux >> 8) & 0xff)
#define TAURUS_CAMERA_EVT_FRAME_READY_VACANT_CELL(aux)   (((uint64_t)aux >> 0) & 0xff)
#define TAURUS_CAMERA_EVT_FRAME_READY_FRAME_ID(aux)      (((uint64_t)aux >> 32) & 0xffffffff)

/*
 * TAURUS_CAMERA_EVT_FEED_ME
 * Buf Num[7:0] - Number of vacant buffer cell that server can hold.
 * Note that this event will be signalled when buffer cell is empty.
 */
#define TAURUS_CAMERA_EVT_FEED_ME                        (1)
#define TAURUS_CAMERA_EVT_FEED_ME_VAL(chn, buf_num)                                \
                            ((uint64_t)(TAURUS_PROTOCOL_CAMERA_ID & 0xff) << 24) | \
                            ((uint64_t)(TAURUS_CAMERA_EVT_FEED_ME & 0xf) << 20)  | \
                            ((uint64_t)(chn & 0xf) << 16) |                        \
                            (uint64_t)(buf_num & 0xff)
#define TAURUS_CAMERA_EVT_FEED_ME_BUF_NUM(aux)           (((uint64_t)aux >> 0) & 0xff)


#define TAURUS_CAMERA_RES_OK                             (0)
#define TAURUS_CAMERA_RES_ERR_PARA                       (1)
#define TAURUS_CAMERA_RES_ERR_NOINIT                     (2)
#define TAURUS_CAMERA_RES_ERR_CIO                        (3)
#define TAURUS_CAMERA_RES_ERR_THREAD                     (4)
#define TAURUS_CAMERA_RES_ERR_REINIT                     (5)


struct taurus_camera_channel_info {
    uint32_t    vacant_buf_cell_cnt;
    uint32_t    width;
    uint32_t    height;
} __packed;
typedef struct taurus_camera_channel_info taurus_camera_channel_info_t;

struct taurus_camera_buffer{
    uint32_t    index;   /* identification of buffer by guest */
    uint32_t    address; /* 128-byte aligned */
} __packed;
typedef struct taurus_camera_buffer taurus_camera_buffer_t;

/********************* IOCTLs **************************/

#define CAMERA_PROTOCOL_IOC_GET_INFO    ((TAURUS_PROTOCOL_CAMERA_ID << 24) | 0xF00000)

struct taurus_camera_ioc_get_info_in {
    uint64_t    cookie;
} __packed;

struct taurus_camera_ioc_get_info_out {
    uint64_t    cookie;
    uint64_t    res;
    uint32_t    channel_num;
} __packed;

#define CAMERA_PROTOCOL_IOC_CHANNEL_INIT    ((TAURUS_PROTOCOL_CAMERA_ID << 24) | 0xF00001)

struct taurus_camera_ioc_channel_init_in {
    uint64_t                cookie;
    uint32_t                channel;
    taurus_camera_buffer_t  buffer[3];
} __packed;

struct taurus_camera_ioc_channel_init_out {
    uint64_t    cookie;
    uint64_t    res;
    taurus_camera_channel_info_t   channel_info;
} __packed;

#define CAMERA_PROTOCOL_IOC_CHANNEL_START     ((TAURUS_PROTOCOL_CAMERA_ID << 24) | 0xF00002)

struct taurus_camera_ioc_channel_start_in {
    uint64_t    cookie;
    uint32_t    channel;
} __packed;

struct taurus_camera_ioc_channel_start_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;

#define CAMERA_PROTOCOL_IOC_CHANNEL_STOP     ((TAURUS_PROTOCOL_CAMERA_ID << 24) | 0xF00003)

struct taurus_camera_ioc_channel_stop_in {
    uint64_t    cookie;
    uint32_t    channel;
} __packed;

struct taurus_camera_ioc_channel_stop_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;

#define CAMERA_PROTOCOL_IOC_CHANNEL_FEED_BUFFER    ((TAURUS_PROTOCOL_CAMERA_ID << 24) | 0xF00004)

struct taurus_camera_ioc_channel_feed_buffer_in {
    uint64_t                cookie;
    uint32_t                channel;
    uint32_t                buf_cnt;
    taurus_camera_buffer_t  buffer[0];
} __packed;

struct taurus_camera_ioc_channel_feed_buffer_out {
    uint64_t    cookie;
    uint64_t    res;
    uint32_t    accepted_buf_cnt;
    uint32_t    vacant_buf_cell_cnt;
    uint32_t    empty_buf_cnt;
} __packed;

#define CAMERA_PROTOCOL_IOC_CHANNEL_RELEASE     ((TAURUS_PROTOCOL_CAMERA_ID << 24) | 0xF00005)

struct taurus_camera_ioc_channel_release_in {
    uint64_t    cookie;
    uint32_t    channel;
} __packed;

struct taurus_camera_ioc_channel_release_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;

#define CAMERA_PROTOCOL_IOC_GET_CHANNEL_INFO    ((TAURUS_PROTOCOL_CAMERA_ID << 24) | 0xF00006)

struct taurus_camera_ioc_get_channel_info_in {
    uint64_t                cookie;
    uint32_t                channel;
} __packed;

struct taurus_camera_ioc_get_channel_info_out {
    uint64_t    cookie;
    uint64_t    res;
    uint32_t    width;
    uint32_t    height;
} __packed;


struct taurus_camera_cmd_msg {
    R_TAURUS_CmdMsg_t   hdr;
    uint32_t            type;
    union {
        struct taurus_camera_ioc_get_info_in ioc_get_info;
        struct taurus_camera_ioc_channel_init_in ioc_channel_init;
        struct taurus_camera_ioc_channel_start_in ioc_channel_start;
        struct taurus_camera_ioc_channel_stop_in ioc_channel_stop;
        struct taurus_camera_ioc_channel_feed_buffer_in ioc_channel_feed_buffer;
        struct taurus_camera_ioc_channel_release_in ioc_channel_release;
        struct taurus_camera_ioc_get_channel_info_in ioc_get_channel_info;
    } params;
};

struct taurus_camera_res_msg {
    R_TAURUS_ResultMsg_t    hdr;
    uint32_t                type;
    union {
        struct taurus_camera_ioc_get_info_out ioc_get_info;
        struct taurus_camera_ioc_channel_init_out ioc_channel_init;
        struct taurus_camera_ioc_channel_start_out ioc_channel_start;
        struct taurus_camera_ioc_channel_stop_out ioc_channel_stop;
        struct taurus_camera_ioc_channel_feed_buffer_out ioc_channel_feed_buffer;
        struct taurus_camera_ioc_channel_release_out ioc_channel_release;
        struct taurus_camera_ioc_get_channel_info_out ioc_get_channel_info;
    } params;
};

#endif /* R_TAURUS_CAMERA_PROTOCOL_H */
