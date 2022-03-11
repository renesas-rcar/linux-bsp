#ifndef R_TAURUS_ETHER_PROTOCOL_H
#define R_TAURUS_ETHER_PROTOCOL_H

#include "r_taurus_bridge.h"
#include "r_taurus_protocol_ids.h"

#ifndef __packed
#define __packed       __attribute__((__packed__))
#endif

#define ETH_MACADDR_SIZE	6

#define ETHER_PROTOCOL_EVENT_PKT_AVAIL_CH0               ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0x000000)

#define ETHER_PROTOCOL_OPEN                              ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xE00000)

struct taurus_ether_init_in {
    uint64_t cookie;
    uint32_t pad1;
    uint8_t  pad2[8];
    uint8_t  pad3;
    uint8_t  pad4;
    uint8_t  pad5;
    uint8_t  pad6;
    uint8_t  pad7;
    /* struct STag_Eth_QueueConfigType Eth_QueueConfig; */
    uint64_t pad8;
    uint64_t pad9;
    /* struct STag_Eth_ComConfigType       stComConfig; */
    uint8_t  pad10;
    uint8_t pad11;
    /* struct STag_Eth_RxConfigType        stRxConfig; */
    uint32_t pad12;
    uint32_t pad13;
    /* struct STag_Eth_TxCBSType           stTxConfig; */
    uint8_t  pad14;
    uint8_t pad15;
    #if 0
    /* struct STag_Eth_CtrlPriorityType    stEthPriority; */
    uint16_t pad16;
    uint8_t  pad17;
    #endif
    /* Eth_ClockDelayType      stClkDelayConfig; */
    uint16_t pad18;
} __packed;
struct taurus_ether_init_out {
    uint64_t cookie;
    uint64_t res;
} __packed;
#if 1
#define ETHER_PROTOCOL_IOC_SET_MODE                      ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00000)

struct taurus_ether_set_controller_mode_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    /* Eth_ModeType CtrlMode; */
    uint8_t CtrlMode; /* 0:ETH_MODE_DOWN 1:ETH_MODE_ACTIVE */
} __packed;
struct taurus_ether_set_controller_mode_out {
    uint64_t cookie;
    uint64_t res;
} __packed;


#define ETHER_PROTOCOL_IOC_GET_MODE                      ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00001)

struct taurus_ether_get_controller_mode_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
} __packed;

struct taurus_ether_get_controller_mode_out {
    uint64_t cookie;
    uint64_t CtrlMode; /* 0:ETH_MODE_DOWN 1:ETH_MODE_ACTIVE */
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_SET_PHYS_ADDR                 ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00003)

struct taurus_ether_set_phys_addr_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    /* uint8* PhysAddrPtr */
    uint8_t PhysAddr[ETH_MACADDR_SIZE]; /* MAC address */
} __packed;
struct taurus_ether_set_phys_addr_out {
    uint64_t cookie;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_GET_PHYS_ADDR                 ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00004)

struct taurus_ether_get_phys_addr_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    /* uint8* PhysAddrPtr */
    /* Not needed PhysAddr[ETH_MACADDR_SIZE] for input parameter */
} __packed;
struct taurus_ether_get_phys_addr_out {
    uint64_t cookie;
    uint8_t PhysAddr[ETH_MACADDR_SIZE]; /* MAC address */
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_UPDATE_PHYS_ADDR_FILTER       ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00005)

struct taurus_ether_update_phys_addr_filter_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    uint8_t PhysAddr[ETH_MACADDR_SIZE]; /* MAC address */
    /* Eth_FilterActionType Action */
    uint8_t Action; /* 0:  ETH_ADD_TO_FILTER, 1:ETH_REMOVE_FROM_FILTER */
} __packed;
struct taurus_ether_update_phys_addr_filter_out{
    uint64_t cookie;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_WRITE_MII                     ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00006)

struct taurus_ether_write_mii_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    uint8_t TrcvIdx;
    uint8_t RegIdx;
    uint16_t RegVal;
} __packed;

struct taurus_ether_write_mii_out{
    uint64_t cookie;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_READ_MII                      ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00007)

struct taurus_ether_read_mii_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    uint8_t TrcvIdx;
    uint8_t RegIdx;
    /* uint16_t RegVal; */
} __packed;

struct taurus_ether_read_mii_out{
    uint64_t cookie;
    uint16_t RegVal;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_GET_DROP_COUNT                ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00008)

struct taurus_ether_get_drop_count_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    uint8_t CountValues;
    /* uint8_t DropCount; */
    /* uint16_t RegVal; */
} __packed;

struct taurus_ether_get_drop_count_out{
    uint64_t cookie;
    uint32_t DropCount;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_STATUS                            ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00009)

struct taurus_ether_get_ether_stats_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    /* uint32_t* etherStats; */
} __packed;

struct taurus_ether_get_ether_stats_out{
    uint64_t cookie;
    uint32_t etherStats;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_PROVIDE_TX_BUFF               ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF0000A)

struct taurus_ether_provide_tx_buffer_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    uint16_t LenByte;
} __packed;

struct taurus_ether_provide_tx_buffer_out{
    uint64_t cookie;
    uint32_t BufIdx;
    uint32_t BufAddr; /* Address of buffer in shared buffer */
    uint16_t LenByte;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_TRANSMIT                      ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF0000B)

struct taurus_ether_transmit_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    uint32_t BufIdx;
    uint16_t FrameType;
    bool TxConfirmation;
    uint16_t LenByte;
    uint8_t PhysAddr[ETH_MACADDR_SIZE]; /* MAC address */
} __packed;

struct taurus_ether_transmit_out{
    uint64_t cookie;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_RECEIVE                       ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF0000C)

struct taurus_ether_receive_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
} __packed;

struct taurus_ether_receive_out{
    uint64_t cookie;
    uint8_t RxStatus;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_TX_CONFIRMATION               ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF0000D)

struct taurus_ether_tx_confirmation_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
} __packed;

struct taurus_ether_tx_confirmation_out{
    uint64_t cookie;
    uint8_t TxConfirmed;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_GET_CURRENT_TIME              ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF0000E)

struct taurus_ether_get_current_time_in {
    uint64_t cookie;
    uint8_t  CtrlIdx;
} __packed;

struct taurus_ether_get_current_time_out{
    uint64_t cookie;
    uint8_t  timeQual;  /* 0:ETH_VALID 1:ETH_INVALID 2:ETH_UNCERTAION */
    uint32_t nanoseconds; /* TimeStamp */
    uint32_t seconds; /* TimeStamp */
    uint16_t secondsHi; /* TimeStamp */
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_ENABLE_EGRESS_TIME_STAMP      ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF0000F)

struct taurus_ether_enable_egress_time_stamp_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    uint8_t BufIdx;
} __packed;

struct taurus_ether_enable_egress_time_stamp_out{
    uint64_t cookie;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_GET_EGRESS_TIME_STAMP         ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00010)

struct taurus_ether_get_egress_time_stamp_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    uint8_t BufIdx;
} __packed;

struct taurus_ether_get_egress_time_stamp_out{
    uint64_t cookie;
    uint8_t  timeQual;  /* 0:ETH_VALID 1:ETH_INVALID 2:ETH_UNCERTAION */
    uint32_t nanoseconds; /* TimeStamp */
    uint32_t seconds; /* TimeStamp */
    uint16_t secondsHi; /* TimeStamp */
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_GET_INGRESS_TIME_STAMP        ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00011)

struct taurus_ether_get_ingress_time_stamp_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    uint32_t DataAddr; /* Address of message buffer in the shared memory */
} __packed;

struct taurus_ether_get_ingress_time_stamp_out{
    uint64_t cookie;
    uint8_t  timeQual;  /* 0:ETH_VALID 1:ETH_INVALID 2:ETH_UNCERTAION */
    uint32_t nanoseconds; /* TimeStamp */
    uint32_t seconds; /* TimeStamp */
    uint16_t secondsHi; /* TimeStamp */
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_SET_CORRECTION_TIME           ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00012)

struct taurus_ether_set_correction_time_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    /* Eth_TimeIntDiffType */
    uint32_t diff_nanoseconds;
    uint32_t diff_seconds;
    uint16_t diff_secondsHi;
    bool     diff_sign;
    /* Eth_RateRatioType */
    uint32_t InTs_nanosecounds;
    uint32_t InTs_seconds;
    uint16_t InTs_secondsHi;
    uint32_t OrTs_nanoseconds;
    uint32_t OrTs_seconds;
    uint16_t OrTs_secondsHi;
} __packed;

struct taurus_ether_set_correction_time_out{
    uint64_t cookie;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_SET_GLOBAL_TIME               ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00013)

struct taurus_ether_set_global_time_in{
    uint64_t cookie;
    uint8_t CtrlIdx;
    /* Eth_TimeStampType */
    uint32_t nanoseconds;
    uint32_t seconds;
    uint16_t secondsHi;
} __packed;

struct taurus_ether_set_global_time_out{
    uint64_t cookie;
    uint64_t res;
} __packed;

#define ETHER_PROTOCOL_IOC_GET_VERSION_INFO              ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00014)

struct taurus_ether_get_version_info_in{
    uint64_t cookie;
} __packed;

struct taurus_ether_get_version_info_out{
    uint64_t cookie;
    /* Std_VersionInfoType */
    uint16_t vendorID;
    uint16_t moduleID;
    uint8_t instanceID;
    uint8_t sw_major_version;
    uint8_t sw_minor_version;
    uint8_t sw_patch_version;
    uint64_t res;
} __packed;

/*  renesas API */
struct taurus_ether_module_stop_check_in{
    uint64_t cookie;
} __packed;
struct taurus_ether_module_stop_check_out{
    uint64_t cookie;
    uint64_t res;
} __packed;


/*
struct taurus_ether_dmac_reset_in{
} __packed;
struct taurus_ether_dmac_reset_out{
} __packed;
*/

#define ETHER_PROTOCOL_CLOSE                             ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xE00001)

struct taurus_ether_close_in {
    uint64_t cookie;
} __packed;

struct taurus_ether_close_out {
    uint64_t cookie;
    uint64_t res;
} __packed;


#define ETHER_PROTOCOL_IOC_MAPPINGS_GET                  ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00015)

#define ETHER_PROTOCOL_IOC_MAPPINGS_SET                  ((TAURUS_PROTOCOL_ETHER_ID << 24) | 0xF00016)

struct taurus_ether_ioc_mappings_set_in {
    uint64_t cookie;
    uint32_t pa;
    uint32_t ipa;
    uint32_t size;
} __packed;

struct taurus_ether_ioc_mappings_set_out {
    uint64_t cookie;
    uint64_t res;
} __packed;



/*******************************************************/

struct taurus_ether_cmd_msg {
    R_TAURUS_CmdMsg_t hdr;
    uint32_t type;
    union {
        struct taurus_ether_init_in eth_init; /*  Eth_Init */
        struct taurus_ether_set_controller_mode_in eth_set_mode;
        struct taurus_ether_get_controller_mode_in eth_get_mode;
        struct taurus_ether_set_phys_addr_in set_phys;
        struct taurus_ether_get_phys_addr_in get_phys;
        struct taurus_ether_update_phys_addr_filter_in update_phys;
        struct taurus_ether_write_mii_in write_mii;
        struct taurus_ether_read_mii_in read_mii;
        struct taurus_ether_get_drop_count_in get_drop_count;
        struct taurus_ether_get_ether_stats_in get_ether_stats;
        struct taurus_ether_provide_tx_buffer_in tx_buffer;
        struct taurus_ether_transmit_in transmit;
        struct taurus_ether_receive_in receive;
        struct taurus_ether_tx_confirmation_in tx_confirmation;
        struct taurus_ether_get_current_time_in get_current_time;
        struct taurus_ether_enable_egress_time_stamp_in enable_egress_time_stamp;
        struct taurus_ether_get_egress_time_stamp_in get_egress_time_stamp;
        struct taurus_ether_get_ingress_time_stamp_in get_ingress_time_stamp;
        struct taurus_ether_set_correction_time_in set_correction_time;
        struct taurus_ether_set_global_time_in set_global_time;
        struct taurus_ether_get_version_info_in get_version_info;
        /* struct taurus_ether_module_stop_check_in module_stop_check; */
        /* struct taurus_ether_dmac_reset_in dmac_reset; */
        struct taurus_ether_close_in close;
    } params;
};

struct taurus_ether_res_msg {
    R_TAURUS_ResultMsg_t hdr;
    uint32_t type;
    union {
        struct taurus_ether_init_out open; /*  Eth_Init */
        struct taurus_ether_set_controller_mode_out set_mode;
        struct taurus_ether_get_controller_mode_out get_mode;
        struct taurus_ether_set_phys_addr_out set_phys;
        struct taurus_ether_get_phys_addr_out get_phys;
        struct taurus_ether_update_phys_addr_filter_out update_phys;
        struct taurus_ether_write_mii_out write_mii;
        struct taurus_ether_read_mii_out read_mii;
        struct taurus_ether_get_drop_count_out get_drop_count;
        struct taurus_ether_get_ether_stats_out get_ether_stats;
        struct taurus_ether_provide_tx_buffer_out tx_buffer;
        struct taurus_ether_transmit_out transmit;
        struct taurus_ether_receive_out receive;
        struct taurus_ether_tx_confirmation_out tx_confirmation;
        struct taurus_ether_get_current_time_out get_current_time;
        struct taurus_ether_enable_egress_time_stamp_out enable_egress_time_stamp;
        struct taurus_ether_get_egress_time_stamp_out get_egress_time_stamp;
        struct taurus_ether_get_ingress_time_stamp_out get_ingress_time_stamp;
        struct taurus_ether_set_correction_time_out set_correction_time;
        struct taurus_ether_set_global_time_out set_global_time;
        struct taurus_ether_get_version_info_out get_version_info;
        /* struct taurus_ether_module_stop_check_out module_stop_check; */
        /* struct taurus_ether_dmac_reset_out dmac_reset; */
        struct taurus_ether_close_out close;
    } params;
};
#endif
#endif /* R_TAURUS_ETHER_PROTOCOL_H */
