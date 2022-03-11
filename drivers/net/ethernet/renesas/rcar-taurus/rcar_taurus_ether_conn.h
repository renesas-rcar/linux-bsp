/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __RCAR_TAURUS_ETHER_CONN_H__
#define __RCAR_TAURUS_ETHER_CONN_H__

#include <linux/types.h>

struct rcar_taurus_ether_drv;
struct taurus_ether_res_msg;

int rct_eth_conn_open(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
		      struct taurus_ether_res_msg *res_msg);

int rct_eth_conn_close(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
		       struct taurus_ether_res_msg *res_msg);

int rct_eth_conn_mii_read(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			  struct taurus_ether_res_msg *res_msg, int addr, int regnum);

int rct_eth_conn_mii_write(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			   struct taurus_ether_res_msg *res_msg, int addr, int regnum, u16 val);

int rct_eth_conn_provide_tx_buffer(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
				   struct taurus_ether_res_msg *res_msg, u16 data_len);

int rct_eth_conn_set_mode(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			  struct taurus_ether_res_msg *res_msg, bool mode);

int rct_eth_conn_get_mode(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			  struct taurus_ether_res_msg *res_msg);

int rct_eth_conn_read(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
		      struct taurus_ether_res_msg *res_msg);

int rct_eth_conn_get_mac_addr(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			      struct taurus_ether_res_msg *res_msg);

int rct_eth_conn_set_mac_addr(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			      struct taurus_ether_res_msg *res_msg, u8 *mac_addr);

int rct_eth_conn_start_xmit(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			    struct taurus_ether_res_msg *res_msg, u32 buff_idx,
			    u16 frame_type, u16 data_len, u8 *dest_addr);

int rct_eth_conn_tx_confirm(struct rcar_taurus_ether_drv *rct_eth, u32 eth_ch,
			    struct taurus_ether_res_msg *res_msg);

#endif /* __RCAR_TAURUS_ETHER_CONN_H__ */
