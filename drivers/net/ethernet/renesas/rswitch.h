/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2022-2026 Renesas Electronics Corporation
 */

#ifndef __RSWITCH_H__
#define __RSWITCH_H__

#include <linux/if_vlan.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <net/switchdev.h>

#include "rcar_gen4_ptp.h"
#include "rswitch_regs.h"
#include "rswitch3_fwd.h"

#define RSWITCH_MAX_NUM_QUEUES		128
#define RSWITCH_MAX_NUM_PORTS		RSWITCH_NUM_PORTS_RSW3

#define RSWITCH2_TOP_OFFSET		0x00008000
#define RSWITCH2_COMA_OFFSET		0x00009000
#define RSWITCH2_ETHA_OFFSET		0x0000a000	/* with RMAC */
#define RSWITCH_ETHA_SIZE		0x00002000	/* with RMAC */
#define RSWITCH2_GWCA0_OFFSET		0x00010000
#define RSWITCH2_GWCA1_OFFSET		0x00012000
#define RSWITCH_GPTP_OFFSET_S4		0x00018000

#define RSWITCH_NUM_AGENTS_RSW2		5
#define RSWITCH_NUM_PORTS_RSW2		3
#define AGENT_INDEX_GWCA_RSW2		RSWITCH_NUM_PORTS_RSW2

#define RSWITCH_NUM_AGENTS_RSW3		15
#define RSWITCH_NUM_PORTS_RSW3		13
#define AGENT_INDEX_GWCA_RSW3		RSWITCH_NUM_PORTS_RSW3

#define rswitch_for_all_ports(_priv, _rdev)			\
	list_for_each_entry(_rdev, &(_priv)->port_list, list)

#define rswitch_for_each_enabled_port(priv, i)		\
	for (i = 0; i < priv->info->rswitch_num_ports; i++)		\
		if (priv->rdev[i]->disabled)		\
			continue;			\
		else

#define rswitch_for_each_enabled_port_continue_reverse(priv, i)	\
	for (; i-- > 0; )					\
		if (priv->rdev[i]->disabled)			\
			continue;				\
		else

#define field_get(_mask, _reg) (((_reg) & (_mask)) >> (ffs(_mask) - 1))
#define field_prep(_mask, _val) (((_val) << (ffs(_mask) - 1)) & (_mask))

#define TX_RING_SIZE		1024
#define RX_RING_SIZE		4096
#define TS_RING_SIZE(priv)	(TX_RING_SIZE * (priv)->info->rswitch_num_ports)

#define RSWITCH_MAX_MTU		9600
#define RSWITCH_HEADROOM	(NET_SKB_PAD + NET_IP_ALIGN)
#define RSWITCH_DESC_BUF_SIZE	2048
#define RSWITCH_TAILROOM	SKB_DATA_ALIGN(sizeof(struct skb_shared_info))
#define RSWITCH_ALIGN		128
#define RSWITCH_BUF_SIZE	(RSWITCH_HEADROOM + RSWITCH_DESC_BUF_SIZE + \
				 RSWITCH_TAILROOM + RSWITCH_ALIGN)
#define RSWITCH_MAP_BUF_SIZE	(RSWITCH_BUF_SIZE - RSWITCH_HEADROOM)
#define RSWITCH_MAX_CTAG_PCP	7

#define RSWITCH_TIMEOUT_US	100000

#define RSW_AGEING_CLK_PER_US	0x140
#define RSW_AGEING_TIME		300

#define RSWITCH3_MDC_HZ			2500000		/* 2.5 MHz */
#define RSWITCH3_FALLBACK_CLK_RELAX	687500000	/* 687.5 MHz */
#define RSWITCH3_FALLBACK_CLK_SPEC	712500000	/* 712.5 MHz */

/* TODO: hardcoded ETHA/GWCA settings for now */
#define GWCA_IRQ_RESOURCE_NAME	"gwca0_rxtx%d"
#define GWCA_IRQ_NAME		"rswitch: gwca0_rxtx%d"
#define GWCA_NUM_IRQS		8
#define GWCA_INDEX		0
#define AGENT_INDEX_GWCA	3
#define GWCA_IPV_NUM		0

#define GWCA_TS_IRQ_RESOURCE_NAME	"gwca0_rxts0"
#define GWCA_TS_IRQ_NAME		"rswitch: gwca0_rxts0"
#define GWCA_TS_IRQ_BIT			BIT(0)

/* Descriptors */
enum RX_DS_CC_BIT {
	RX_DS	= 0x0fff, /* Data size */
	RX_TR	= 0x1000, /* Truncation indication */
	RX_EI	= 0x2000, /* Error indication */
	RX_PS	= 0xc000, /* Padding selection */
};

enum TX_DS_TAGL_BIT {
	TX_DS	= 0x0fff, /* Data size */
	TX_TAGL	= 0xf000, /* Frame tag LSBs */
};

enum DIE_DT {
	/* Frame data */
	DT_FSINGLE	= 0x80,
	DT_FSTART	= 0x90,
	DT_FMID		= 0xa0,
	DT_FEND		= 0xb0,

	/* Chain control */
	DT_LEMPTY	= 0xc0,
	DT_EEMPTY	= 0xd0,
	DT_LINKFIX	= 0x00,
	DT_LINK		= 0xe0,
	DT_EOS		= 0xf0,
	/* HW/SW arbitration */
	DT_FEMPTY	= 0x40,
	DT_FEMPTY_IS	= 0x10,
	DT_FEMPTY_IC	= 0x20,
	DT_FEMPTY_ND	= 0x30,
	DT_FEMPTY_START	= 0x50,
	DT_FEMPTY_MID	= 0x60,
	DT_FEMPTY_END	= 0x70,

	DT_MASK		= 0xf0,
	DIE		= 0x08,	/* Descriptor Interrupt Enable */
};

/* Both transmission and reception */
#define INFO1_FMT		BIT(2)
#define INFO1_TXC		BIT(3)

/* For transmission */
#define INFO1_TSUN(val)		((u64)(val) << 8ULL)
#define INFO1_IPV(prio)		((u64)(prio) << 28ULL)
#define INFO1_CSD0(index)	((u64)(index) << 32ULL)
#define INFO1_CSD1(index)	((u64)(index) << 40ULL)
#define INFO1_DV(port_vector)	((u64)(port_vector) << 48ULL)

/* For reception */
#define SPN			GENMASK_U64(38, 36)

/* For timestamp descriptor in dptrl (Byte 4 to 7) */
#define TS_DESC_TSUN(dptrl)	((dptrl) & GENMASK(7, 0))
#define TS_DESC_SPN(dptrl)	(((dptrl) & GENMASK(10, 8)) >> 8)
#define TS_DESC_DPN(dptrl)	(((dptrl) & GENMASK(17, 16)) >> 16)
#define TS_DESC_TN(dptrl)	((dptrl) & BIT(24))

struct rswitch_desc {
	__le16 info_ds;	/* Descriptor size */
	u8 die_dt;	/* Descriptor interrupt enable and type */
	__u8  dptrh;	/* Descriptor pointer MSB */
	__le32 dptrl;	/* Descriptor pointer LSW */
} __packed;

struct rswitch_ts_desc {
	struct rswitch_desc desc;
	__le32 ts_nsec;
	__le32 ts_sec;
} __packed;

struct rswitch_ext_desc {
	struct rswitch_desc desc;
	__le64 info1;
} __packed;

struct rswitch_ext_ts_desc {
	struct rswitch_desc desc;
	__le64 info1;
	__le32 ts_nsec;
	__le32 ts_sec;
} __packed;

struct rswitch_etha {
	unsigned int index;
	void __iomem *addr;
	void __iomem *coma_addr;
	struct rswitch_private *priv;
	bool external_phy;
	struct mii_bus *mii;
	phy_interface_t phy_interface;
	u32 psmcs;
	u8 mac_addr[MAX_ADDR_LEN];
	int link;
	int speed;

	/* This hardware could not be initialized twice so that marked
	 * this flag to avoid multiple initialization.
	 */
	bool operated;
	bool connect_to_xpcs;
};

/* The datasheet said descriptor "chain" and/or "queue". For consistency of
 * name, this driver calls "queue".
 */
struct rswitch_gwca_queue {
	union {
		struct rswitch_ext_desc *tx_ring;
		struct rswitch_ext_ts_desc *rx_ring;
		struct rswitch_ts_desc *ts_ring;
	};

	/* Common */
	dma_addr_t ring_dma;
	unsigned int ring_size;
	unsigned int cur;
	unsigned int dirty;

	/* For [rt]x_ring */
	unsigned int index;
	bool dir_tx;
	struct net_device *ndev;	/* queue to ndev for irq */

	union {
		/* For TX */
		struct {
			struct sk_buff **skbs;
			dma_addr_t *unmap_addrs;
		};
		/* For RX */
		struct {
			void **rx_bufs;
			struct sk_buff *skb_fstart;
			u16 pkt_len;
		};
	};
};

#define RSWITCH_NUM_IRQ_REGS	(RSWITCH_MAX_NUM_QUEUES / BITS_PER_TYPE(u32))
struct rswitch_gwca {
	unsigned int agent_index;
	struct rswitch_desc *linkfix_table;
	dma_addr_t linkfix_table_dma;
	u32 linkfix_table_size;
	struct rswitch_gwca_queue *queues;
	int num_queues;
	struct rswitch_gwca_queue ts_queue;
	struct rswitch_gwca_queue *l2_shared_rx_queue;
	DECLARE_BITMAP(used, RSWITCH_MAX_NUM_QUEUES);
	u32 tx_irq_bits[RSWITCH_NUM_IRQ_REGS];
	u32 rx_irq_bits[RSWITCH_NUM_IRQ_REGS];
};

#define NUM_QUEUES_PER_NDEV	2
#define TS_TAGS_PER_PORT	256

struct rswitch_device {
	struct rswitch_private *priv;
	struct net_device *ndev;
	struct napi_struct napi;
	void __iomem *addr;
	struct rswitch_gwca_queue *tx_queue;
	struct rswitch_gwca_queue *rx_queue;
	struct rswitch_gwca_queue *rx_old_queue;
	struct sk_buff *ts_skb[TS_TAGS_PER_PORT];
	DECLARE_BITMAP(ts_skb_used, TS_TAGS_PER_PORT);
	bool disabled;

	struct list_head list;

	int port;
	int etha_mii_idx;
	struct rswitch_etha *etha;
	struct rswitch_etha *etha_mii;
	struct device_node *np_port;
	struct phy *serdes;

	struct net_device *brdev;	/* master bridge device */
	unsigned int learning_requested : 1;
	unsigned int learning_offloaded : 1;
	unsigned int forwarding_requested : 1;
	unsigned int forwarding_offloaded : 1;
};

struct rswitch_mfwd_mac_table_entry {
	int queue_index;
	unsigned char addr[MAX_ADDR_LEN];
};

struct rswitch_mfwd {
	struct rswitch_mac_table_entry *mac_table_entries;
	int num_mac_table_entries;
};

struct rswitch_private {
	struct platform_device *pdev;
	void __iomem *addr_mfwd;
	void __iomem *addr_mfab;
	void __iomem *addr_coma;
	void __iomem *addr_etha;
	void __iomem *addr_gwca;

	struct rcar_gen4_ptp_private *ptp_priv;

	struct rswitch_device *rdev[RSWITCH_MAX_NUM_PORTS];
	DECLARE_BITMAP(opened_ports, RSWITCH_MAX_NUM_PORTS);

	struct rswitch_gwca gwca;
	struct rswitch_etha etha[RSWITCH_MAX_NUM_PORTS];
	struct rswitch_etha etha_mii[RSWITCH_MAX_NUM_PORTS];
	struct rswitch_mfwd mfwd;

	/* Notifiers */
	struct notifier_block rswitch_netdevice_nb;
	struct notifier_block rswitch_switchdev_nb;
	struct notifier_block rswitch_switchdev_blocking_nb;

	struct list_head port_list;

	spinlock_t lock;	/* lock interrupt registers' control */
	spinlock_t agent_lock;	/* rswitch agent clock control */

	struct clk *clk;

	bool etha_no_runtime_change;
	bool gwca_halt;
	const struct rswitch_hw_info *info;

	struct net_device *offload_brdev;

	enum hwtstamp_tx_types tstamp_tx_ctrl;
	enum hwtstamp_rx_filters tstamp_rx_ctrl;

	struct rswitch_switchdev_event_work *switchdev_work;
};

struct rswitch_hw_info {
	int (*rswitch_map_addr)(struct platform_device *pdev, struct rswitch_private *priv);
	int (*bpool_config)(struct rswitch_private *priv);
	int (*rswitch_fwd_init)(struct rswitch_private *priv);
	void (*rswitch_etha_enable_mii)(struct rswitch_etha *etha);
	void (*rswitch_rmac_setting)(struct rswitch_etha *etha, const u8 *mac);
	u8 rswitch_num_ports;
	u8 rswitch_num_agents;
	u8 agent_index_gwca;
	u8 rswitch_version;
};

/* RSwitch hardware version */
enum VERS {
	RSW20 = 20,
	RSW30 = 30,
	RSW31 = 31,
	RSWFPGA = 99,
};

struct rswitch_switchdev_event_work {
	struct work_struct work;
	netdevice_tracker dev_tracker;
	struct switchdev_notifier_fdb_info fdb_info;
	struct net_device *ndev;
	struct rswitch_private *priv;
	unsigned long event;
};

bool is_rdev(const struct net_device *ndev);
void rswitch_modify(void __iomem *addr, u32 reg, u32 clear, u32 set);
int rswitch_reg_wait(void __iomem *addr, u32 offs, u32 mask, u32 expected);
int rswitch_gwca_change_mode(struct rswitch_private *priv, enum rswitch_port_mode mode);
int rswitch_etha_change_mode(struct rswitch_etha *etha, enum rswitch_port_mode mode);

static bool parallel_mode;
module_param(parallel_mode, bool, 0644);
MODULE_PARM_DESC(parallel_mode, "Operate simultaneously with Realtime core");

#endif	/* #ifndef __RSWITCH_H__ */
