#ifndef R_TAURUS_CAN_PROTOCOL_H
#define R_TAURUS_CAN_PROTOCOL_H

#include "r_taurus_bridge.h"
#include "r_taurus_protocol_ids.h"

#ifndef __packed
# define __packed       __attribute__((__packed__))
#endif

/**************** CAN signal identifiers ***************/

#define CAN_PROTOCOL_EVENT_PKT_AVAIL_CH0               ((TAURUS_PROTOCOL_CAN_ID << 24) | 0x000000)
#define CAN_PROTOCOL_EVENT_PKT_AVAIL_CH1               ((TAURUS_PROTOCOL_CAN_ID << 24) | 0x000001)


/************************ Commands *********************/

#define CAN_PROTOCOL_OPEN                              ((TAURUS_PROTOCOL_CAN_ID << 24) | 0xE00000)

struct taurus_can_open_in {
    uint64_t    cookie;
    uint32_t    can_ch;
} __packed;

struct taurus_can_open_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


#define CAN_PROTOCOL_CLOSE                             ((TAURUS_PROTOCOL_CAN_ID << 24) | 0xE00001)

struct taurus_can_close_in {
    uint64_t    cookie;
    uint32_t    can_ch;
} __packed;

struct taurus_can_close_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


#define CAN_PROTOCOL_READ                              ((TAURUS_PROTOCOL_CAN_ID << 24) | 0xE00002)

struct taurus_can_read_in {
    uint64_t    cookie;
    uint32_t    can_ch;
} __packed;

struct taurus_can_read_out {
    uint64_t    cookie;
    uint64_t    res;
    uint32_t    can_ch;
    uint32_t    node_id;
    uint32_t    data_len;
    char        data[64];
} __packed;


#define CAN_PROTOCOL_WRITE                             ((TAURUS_PROTOCOL_CAN_ID << 24) | 0xE00003)

struct taurus_can_write_in {
    uint64_t    cookie;
    uint32_t    can_ch;
    uint32_t    node_id;
    uint32_t    data_len;
    char        data[64];
} __packed;

struct taurus_can_write_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


/* placeholders for future ioctl */
#define CAN_PROTOCOL_IOC_OP1                           ((TAURUS_PROTOCOL_CAN_ID << 24) | 0xF00000)

struct taurus_can_ioc_op1_in {
    uint64_t    cookie;
    uint32_t    can_ch;
    uint32_t    arg1;
    uint32_t    arg2;
} __packed;

struct taurus_can_ioc_op1_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;


#define CAN_PROTOCOL_IOC_OP2                           ((TAURUS_PROTOCOL_CAN_ID << 24) | 0xF00001)

struct taurus_can_ioc_op2_in {
    uint64_t    cookie;
    uint32_t    can_ch;
    uint32_t    arg1;
    uint32_t    arg2;
} __packed;

struct taurus_can_ioc_op2_out {
    uint64_t    cookie;
    uint64_t    res;
} __packed;



/*******************************************************/

struct taurus_can_cmd_msg {
    R_TAURUS_CmdMsg_t   hdr;
    uint32_t            type;
    union {
	struct taurus_can_open_in open;
	struct taurus_can_close_in close;
	struct taurus_can_read_in read;
	struct taurus_can_write_in write;
        struct taurus_can_ioc_op1_in ioc_op1;
        struct taurus_can_ioc_op2_in ioc_op2;
    } params;
};

struct taurus_can_res_msg {
    R_TAURUS_ResultMsg_t        hdr;
    uint32_t                    type;
    union {
	struct taurus_can_open_out open;
	struct taurus_can_close_out close;
	struct taurus_can_read_out read;
	struct taurus_can_write_out write;
        struct taurus_can_ioc_op1_out ioc_op1;
        struct taurus_can_ioc_op2_out ioc_op2;
    } params;
};


#endif /* R_TAURUS_CAN_PROTOCOL_H */
