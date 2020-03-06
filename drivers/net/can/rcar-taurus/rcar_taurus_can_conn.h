#ifndef __RCAR_TAURUS_CAN_CONN_H__
#define __RCAR_TAURUS_CAN_CONN_H__

#include <linux/types.h>

struct rcar_taurus_can_drv;
struct taurus_can_res_msg;
struct rcar_taurus_can_queued_tx_msg;

int rcar_taurus_can_conn_open(struct rcar_taurus_can_drv *rctcan,
			uint32_t can_ch,
			struct taurus_can_res_msg *res_msg);

int rcar_taurus_can_conn_close(struct rcar_taurus_can_drv *rctcan,
			uint32_t can_ch,
			struct taurus_can_res_msg *res_msg);

int rcar_taurus_can_conn_read(struct rcar_taurus_can_drv *rctcan,
			uint32_t can_ch,
			struct taurus_can_res_msg *res_msg);

int rcar_taurus_can_conn_write(struct rcar_taurus_can_drv *rctcan,
			uint32_t can_ch,
			uint32_t node_id,
			uint32_t data_len,
			uint8_t *data,
			struct taurus_can_res_msg *res_msg);

void rcar_taurus_can_conn_queue_write(struct rcar_taurus_can_drv *rctcan,
				uint32_t can_ch,
				uint32_t node_id,
				uint32_t data_len,
				uint8_t *data,
				struct rcar_taurus_can_queued_tx_msg *queued_tx_msg);

#endif /* __RCAR_TAURUS_CAN_CONN_H__ */
