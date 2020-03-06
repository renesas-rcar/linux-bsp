
#ifndef R_VIRTDEV_DRV_API_PRIV_H
#define R_VIRTDEV_DRV_API_PRIV_H

#include "r_taurus_bridge.h"

#ifndef __packed
# define __packed       __attribute__((__packed__))
#endif

/********************* IOCTLs **************************/

#define TAURUS_VIRTDEV_IOC_OP1          0x0

struct taurus_virtdev_ioc_op1_in {
    uint8_t     string[32];
    uint32_t    par1;
    uint64_t    par2;
    uint16_t    par3;
} __packed;

struct taurus_virtdev_ioc_op1_out {
    uint64_t    par1;
    uint32_t    par2;
    uint8_t     string[16];
    uint16_t    par3;
} __packed;


#define TAURUS_VIRTDEV_IOC_OP2          0x1

struct taurus_virtdev_ioc_op2_in {
    uint32_t    par1;
    uint8_t     string[32];
    uint16_t    par2;
    uint64_t    par3;
} __packed;

struct taurus_virtdev_ioc_op2_out {
    uint64_t    par1;
    uint32_t    par2;
    uint8_t     string[8];
    uint16_t    par3;
} __packed;

/*******************************************************/

struct taurus_virtdev_cmd_msg {
    R_TAURUS_CmdMsg_t   hdr;
    uint32_t            type;
    union {
        struct taurus_virtdev_ioc_op1_in ioc_op1;
        struct taurus_virtdev_ioc_op2_in ioc_op2;
    } params;
};                                                    

struct taurus_virtdev_res_msg {
    R_TAURUS_ResultMsg_t        hdr;
    uint32_t                    type;
    union {
        struct taurus_virtdev_ioc_op1_out ioc_op1;
        struct taurus_virtdev_ioc_op2_out ioc_op2;
    } params;
};


#endif /* R_VIRTDEV_DRV_API_PRIV_H */
