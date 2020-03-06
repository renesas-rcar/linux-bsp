
#ifndef R_TAURUS_RVGC_PROTOCOL_H
#define R_TAURUS_RVGC_PROTOCOL_H

#include "r_taurus_bridge.h"
#include "r_taurus_protocol_ids.h"

#ifndef __packed
# define __packed       __attribute__((__packed__))
#endif

/*********** RVGC signal identifiers ************/

#define RVGC_PROTOCOL_EVENT_VBLANK_DISPLAY0             ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0x000000)
#define RVGC_PROTOCOL_EVENT_VBLANK_DISPLAY1             ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0x000001)
#define RVGC_PROTOCOL_EVENT_VBLANK_DISPLAY2             ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0x000002)
#define RVGC_PROTOCOL_EVENT_VBLANK_DISPLAY3             ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0x000003)


/********************* IOCTLs **************************/

#define RVGC_PROTOCOL_IOC_LAYER_SET_ADDR                ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0xF00000)

struct taurus_rvgc_ioc_layer_set_addr_in {
    uint64_t    cookie;
    uint32_t    display;
    uint32_t    layer;
    uint32_t    paddr;
} __packed;

struct taurus_rvgc_ioc_layer_set_addr_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


#define RVGC_PROTOCOL_IOC_LAYER_SET_POS                 ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0xF00001)

struct taurus_rvgc_ioc_layer_set_pos_in {
    uint64_t    cookie;
    uint32_t    display;
    uint32_t    layer;
    uint32_t    pos_x;
    uint32_t    pos_y;
} __packed;

struct taurus_rvgc_ioc_layer_set_pos_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


#define RVGC_PROTOCOL_IOC_LAYER_SET_SIZE                ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0xF00002)

struct taurus_rvgc_ioc_layer_set_size_in {
    uint64_t    cookie;
    uint32_t    display;
    uint32_t    layer;
    uint32_t    size_w;
    uint32_t    size_h;
} __packed;

struct taurus_rvgc_ioc_layer_set_size_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


#define RVGC_PROTOCOL_IOC_DISPLAY_FLUSH                 ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0xF00003)

struct taurus_rvgc_ioc_display_flush_in {
    uint64_t    cookie;
    uint32_t    display;
    uint32_t    blocking;
} __packed;

struct taurus_rvgc_ioc_display_flush_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


#define RVGC_PROTOCOL_IOC_DISPLAY_INIT                  ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0xF00004)

struct taurus_rvgc_ioc_display_init_in {
    uint64_t    cookie;
    uint32_t    display;
} __packed;

struct taurus_rvgc_ioc_display_init_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


#define RVGC_PROTOCOL_IOC_DISPLAY_GET_INFO              ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0xF00005)

struct taurus_rvgc_ioc_display_get_info_in {
    uint64_t    cookie;
    uint32_t    display;
} __packed;

struct taurus_rvgc_ioc_display_get_info_out {
    uint64_t    cookie;
    uint64_t    res;
    uint32_t    width;
    uint32_t    height;
    uint32_t    pitch;
    uint32_t    layers;
} __packed;


#define RVGC_PROTOCOL_IOC_LAYER_RESERVE                 ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0xF00006)

struct taurus_rvgc_ioc_layer_reserve_in {
    uint64_t    cookie;
    uint32_t    display;
    uint32_t    layer;
} __packed;

struct taurus_rvgc_ioc_layer_reserve_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


#define RVGC_PROTOCOL_IOC_LAYER_RELEASE                 ((TAURUS_PROTOCOL_RVGC_ID << 24) | 0xF00007)

struct taurus_rvgc_ioc_layer_release_in {
    uint64_t    cookie;
    uint32_t    display;
    uint32_t    layer;
} __packed;

struct taurus_rvgc_ioc_layer_release_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


/*******************************************************/

struct taurus_rvgc_cmd_msg {
    R_TAURUS_CmdMsg_t   hdr;
    uint32_t            type;
    union {
        struct taurus_rvgc_ioc_layer_set_addr_in ioc_layer_set_addr;
        struct taurus_rvgc_ioc_layer_set_pos_in ioc_layer_set_pos;
        struct taurus_rvgc_ioc_layer_set_size_in ioc_layer_set_size;
        struct taurus_rvgc_ioc_layer_reserve_in ioc_layer_reserve;
        struct taurus_rvgc_ioc_layer_release_in ioc_layer_release;
        struct taurus_rvgc_ioc_display_flush_in ioc_display_flush;
        struct taurus_rvgc_ioc_display_init_in ioc_display_init;
        struct taurus_rvgc_ioc_display_get_info_in ioc_display_get_info;
    } params;
};

struct taurus_rvgc_res_msg {
    R_TAURUS_ResultMsg_t        hdr;
    uint32_t                    type;
    union {
        struct taurus_rvgc_ioc_layer_set_addr_out ioc_layer_set_addr;
        struct taurus_rvgc_ioc_layer_set_pos_out ioc_layer_set_pos;
        struct taurus_rvgc_ioc_layer_set_size_out ioc_layer_set_size;
        struct taurus_rvgc_ioc_layer_reserve_out ioc_layer_reserve;
        struct taurus_rvgc_ioc_layer_release_out ioc_layer_release;
        struct taurus_rvgc_ioc_display_flush_out ioc_display_flush;
        struct taurus_rvgc_ioc_display_init_out ioc_display_init;
        struct taurus_rvgc_ioc_display_get_info_out ioc_display_get_info;
    } params;
};


#endif /* R_TAURUS_RVGC_PROTOCOL_H */
