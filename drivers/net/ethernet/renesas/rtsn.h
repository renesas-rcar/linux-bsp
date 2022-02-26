/* SPDX-License-Identifier: GPL-2.0 */
/* Renesas Ethernet-TSN gPTP device driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#ifndef __RTSN_H__
#define __RTSN_H__

#include <linux/phy.h>

#include "rtsn_ptp.h"

#define AXIRO	0
#define MHDRO	0x1000
#define	RMRO	0x2000

enum rtsn_reg {
	AXIWC		= AXIRO + 0x0000,
	AXIRC		= AXIRO + 0x0004,
	TDPC0		= AXIRO + 0x0010,
	TFT		= AXIRO + 0x0090,
	TATLS0		= AXIRO + 0x00A0,
	TATLS1		= AXIRO + 0x00A4,
	TATLR		= AXIRO + 0x00A8,
	RATLS0		= AXIRO + 0x00B0,
	RATLS1		= AXIRO + 0x00B4,
	RATLR		= AXIRO + 0x00B8,
	TSA0		= AXIRO + 0x00C0,
	TSS0		= AXIRO + 0x00C4,
	TRCR0		= AXIRO + 0x0140,
	RIDAUAS0	= AXIRO + 0x0180,
	RR		= AXIRO + 0x0200,
	TATS		= AXIRO + 0x0210,
	TATSR0		= AXIRO + 0x0214,
	TATSR1		= AXIRO + 0x0218,
	TATSR2		= AXIRO + 0x021C,
	RATS		= AXIRO + 0x0220,
	RATSR0		= AXIRO + 0x0224,
	RATSR1		= AXIRO + 0x0228,
	RATSR2		= AXIRO + 0x022C,
	RIDASM0		= AXIRO + 0x0240,
	RIDASAM0	= AXIRO + 0x0244,
	RIDACAM0	= AXIRO + 0x0248,
	EIS0		= AXIRO + 0x0300,
	EIE0		= AXIRO + 0x0304,
	EID0		= AXIRO + 0x0308,
	EIS1		= AXIRO + 0x0310,
	EIE1		= AXIRO + 0x0314,
	EID1		= AXIRO + 0x0318,
	TCEIS0		= AXIRO + 0x0340,
	TCEIE0		= AXIRO + 0x0344,
	TCEID0		= AXIRO + 0x0348,
	RFSEIS0		= AXIRO + 0x04C0,
	RFSEIE0		= AXIRO + 0x04C4,
	RFSEID0		= AXIRO + 0x04C8,
	RFEIS0		= AXIRO + 0x0540,
	RFEIE0		= AXIRO + 0x0544,
	RFEID0		= AXIRO + 0x0548,
	RCEIS0		= AXIRO + 0x05C0,
	RCEIE0		= AXIRO + 0x05C4,
	RCEID0		= AXIRO + 0x05C8,
	RIDAOIS		= AXIRO + 0x0640,
	RIDAOIE		= AXIRO + 0x0644,
	RIDAOID		= AXIRO + 0x0648,
	TSFEIS		= AXIRO + 0x06C0,
	TSFEIE		= AXIRO + 0x06C4,
	TSFEID		= AXIRO + 0x06C8,
	TSCEIS		= AXIRO + 0x06D0,
	TSCEIE		= AXIRO + 0x06D4,
	TSCEID		= AXIRO + 0x06D8,
	DIS		= AXIRO + 0x0B00,
	DIE		= AXIRO + 0x0B04,
	DID		= AXIRO + 0x0B08,
	TDIS0		= AXIRO + 0x0B10,
	TDIE0		= AXIRO + 0x0B14,
	TDID0		= AXIRO + 0x0B18,
	RDIS0		= AXIRO + 0x0B90,
	RDIE0		= AXIRO + 0x0B94,
	RDID0		= AXIRO + 0x0B98,
	TSDIS		= AXIRO + 0x0C10,
	TSDIE		= AXIRO + 0x0C14,
	TSDID		= AXIRO + 0x0C18,

	OCR		= MHDRO + 0x0000,
	OSR		= MHDRO + 0x0004,
	SWR		= MHDRO + 0x0008,
	SIS		= MHDRO + 0x000C,
	GIS		= MHDRO + 0x0010,
	GIE		= MHDRO + 0x0014,
	GID		= MHDRO + 0x0018,
	TIS1		= MHDRO + 0x0020,
	TIE1		= MHDRO + 0x0024,
	TID1		= MHDRO + 0x0028,
	TIS2		= MHDRO + 0x0030,
	TIE2		= MHDRO + 0x0034,
	TID2		= MHDRO + 0x0038,
	RIS		= MHDRO + 0x0040,
	RIE		= MHDRO + 0x0044,
	RID		= MHDRO + 0x0048,
	TGC1		= MHDRO + 0x0050,
	TGC2		= MHDRO + 0x0054,
	TSF0		= MHDRO + 0x0060,
	TCF0		= MHDRO + 0x0070,
	TCR1		= MHDRO + 0x0080,
	TCR2		= MHDRO + 0x0084,
	TCR3		= MHDRO + 0x0088,
	TCR4		= MHDRO + 0x008C,
	TMS0		= MHDRO + 0x0090,
	TSR1		= MHDRO + 0x00B0,
	TSR2		= MHDRO + 0x00B4,
	TSR3		= MHDRO + 0x00B8,
	TSR4		= MHDRO + 0x00BC,
	TSR5		= MHDRO + 0x00C0,
	RGC		= MHDRO + 0x00D0,
	RDFCR		= MHDRO + 0x00D4,
	RCFCR		= MHDRO + 0x00D8,
	REFCNCR		= MHDRO + 0x00DC,
	RSR1		= MHDRO + 0x00E0,
	RSR2		= MHDRO + 0x00E4,
	RSR3		= MHDRO + 0x00E8,
	TCIS		= MHDRO + 0x01E0,
	TCIE		= MHDRO + 0x01E4,
	TCID		= MHDRO + 0x01E8,
	TPTPC		= MHDRO + 0x01F0,
	TTML		= MHDRO + 0x01F4,
	TTJ		= MHDRO + 0x01F8,
	TCC		= MHDRO + 0x0200,
	TCS		= MHDRO + 0x0204,
	TGS		= MHDRO + 0x020C,
	TACST0		= MHDRO + 0x0210,
	TACST1		= MHDRO + 0x0214,
	TACST2		= MHDRO + 0x0218,
	TALIT0		= MHDRO + 0x0220,
	TALIT1		= MHDRO + 0x0224,
	TALIT2		= MHDRO + 0x0228,
	TAEN0		= MHDRO + 0x0230,
	TAEN1		= MHDRO + 0x0234,
	TASFE		= MHDRO + 0x0240,
	TACLL0		= MHDRO + 0x0250,
	TACLL1		= MHDRO + 0x0254,
	TACLL2		= MHDRO + 0x0258,
	CACC		= MHDRO + 0x0260,
	CCS		= MHDRO + 0x0264,
	CAIV0		= MHDRO + 0x0270,
	CAUL0		= MHDRO + 0x0290,
	TOCST0		= MHDRO + 0x0300,
	TOCST1		= MHDRO + 0x0304,
	TOCST2		= MHDRO + 0x0308,
	TOLIT0		= MHDRO + 0x0310,
	TOLIT1		= MHDRO + 0x0314,
	TOLIT2		= MHDRO + 0x0318,
	TOEN0		= MHDRO + 0x0320,
	TOEN1		= MHDRO + 0x0324,
	TOSFE		= MHDRO + 0x0330,
	TCLR0		= MHDRO + 0x0340,
	TCLR1		= MHDRO + 0x0344,
	TCLR2		= MHDRO + 0x0348,
	TSMS		= MHDRO + 0x0350,
	COCC		= MHDRO + 0x0360,
	COIV0		= MHDRO + 0x03B0,
	COUL0		= MHDRO + 0x03D0,
	QSTMACU0	= MHDRO + 0x0400,
	QSTMACD0	= MHDRO + 0x0404,
	QSTMAMU0	= MHDRO + 0x0408,
	QSTMAMD0	= MHDRO + 0x040C,
	QSFTVL0		= MHDRO + 0x0410,
	QSFTVLM0	= MHDRO + 0x0414,
	QSFTMSD0	= MHDRO + 0x0418,
	QSFTGMI0	= MHDRO + 0x041C,
	QSFTLS		= MHDRO + 0x0600,
	QSFTLIS		= MHDRO + 0x0604,
	QSFTLIE		= MHDRO + 0x0608,
	QSFTLID		= MHDRO + 0x060C,
	QSMSMC		= MHDRO + 0x0610,
	QSGTMC		= MHDRO + 0x0614,
	QSEIS		= MHDRO + 0x0618,
	QSEIE		= MHDRO + 0x061C,
	QSEID		= MHDRO + 0x0620,
	QGACST0		= MHDRO + 0x0630,
	QGACST1		= MHDRO + 0x0634,
	QGACST2		= MHDRO + 0x0638,
	QGALIT1		= MHDRO + 0x0640,
	QGALIT2		= MHDRO + 0x0644,
	QGAEN0		= MHDRO + 0x0648,
	QGAEN1		= MHDRO + 0x074C,
	QGIGS		= MHDRO + 0x0650,
	QGGC		= MHDRO + 0x0654,
	QGATL0		= MHDRO + 0x0664,
	QGATL1		= MHDRO + 0x0668,
	QGATL2		= MHDRO + 0x066C,
	QGOCST0		= MHDRO + 0x0670,
	QGOCST1		= MHDRO + 0x0674,
	QGOCST2		= MHDRO + 0x0678,
	QGOLIT0		= MHDRO + 0x067C,
	QGOLIT1		= MHDRO + 0x0680,
	QGOLIT2		= MHDRO + 0x0684,
	QGOEN0		= MHDRO + 0x0688,
	QGOEN1		= MHDRO + 0x068C,
	QGTRO		= MHDRO + 0x0690,
	QGTR1		= MHDRO + 0x0694,
	QGTR2		= MHDRO + 0x0698,
	QGFSMS		= MHDRO + 0x069C,
	QTMIS		= MHDRO + 0x06E0,
	QTMIE		= MHDRO + 0x06E4,
	QTMID		= MHDRO + 0x06E8,
	QMEC		= MHDRO + 0x0700,
	QMMC		= MHDRO + 0x0704,
	QRFDC		= MHDRO + 0x0708,
	QYFDC		= MHDRO + 0x070C,
	QVTCMC0		= MHDRO + 0x0710,
	QMCBSC0		= MHDRO + 0x0750,
	QMCIRC0		= MHDRO + 0x0790,
	QMEBSC0		= MHDRO + 0x07D0,
	QMEIRC0		= MHDRO + 0x0710,
	QMCFC		= MHDRO + 0x0850,
	QMEIS		= MHDRO + 0x0860,
	QMEIE		= MHDRO + 0x0864,
	QMEID		= MHDRO + 0x086C,
	QSMFC0		= MHDRO + 0x0870,
	QMSPPC0		= MHDRO + 0x08B0,
	QMSRPC0		= MHDRO + 0x08F0,
	QGPPC0		= MHDRO + 0x0930,
	QGRPC0		= MHDRO + 0x0950,
	QMDPC0		= MHDRO + 0x0970,
	QMGPC0		= MHDRO + 0x09B0,
	QMYPC0		= MHDRO + 0x09F0,
	QMRPC0		= MHDRO + 0x0A30,
	MQSTMACU	= MHDRO + 0x0A70,
	MQSTMACD	= MHDRO + 0x0A74,
	MQSTMAMU	= MHDRO + 0x0A78,
	MQSTMAMD	= MHDRO + 0x0A7C,
	MQSFTVL		= MHDRO + 0x0A80,
	MQSFTVLM	= MHDRO + 0x0A84,
	MQSFTMSD	= MHDRO + 0x0A88,
	MQSFTGMI	= MHDRO + 0x0A8C,

	MMC		= RMRO + 0x0000,
	MPSM		= RMRO + 0x0010,
	MPIC		= RMRO + 0x0014,
	MTFFC		= RMRO + 0x0020,
	MTPFC		= RMRO + 0x0024,
	MTATC0		= RMRO + 0x0040,
	MRGC		= RMRO + 0x0080,
	MRMAC0		= RMRO + 0x0084,
	MRMAC1		= RMRO + 0x0088,
	MRAFC		= RMRO + 0x008C,
	MRSCE		= RMRO + 0x0090,
	MRSCP		= RMRO + 0x0094,
	MRSCC		= RMRO + 0x0098,
	MRFSCE		= RMRO + 0x009C,
	MRFSCP		= RMRO + 0x00A0,
	MTRC		= RMRO + 0x00A4,
	MPFC		= RMRO + 0x0100,
	MLVC		= RMRO + 0x0340,
	MEEEC		= RMRO + 0x0350,
	MLBC		= RMRO + 0x0360,
	MGMR		= RMRO + 0x0400,
	MMPFTCT		= RMRO + 0x0410,
	MAPFTCT		= RMRO + 0x0414,
	MPFRCT		= RMRO + 0x0418,
	MFCICT		= RMRO + 0x041C,
	MEEECT		= RMRO + 0x0420,
	MEIS		= RMRO + 0x0500,
	MEIE		= RMRO + 0x0504,
	MEID		= RMRO + 0x0508,
	MMIS0		= RMRO + 0x0510,
	MMIE0		= RMRO + 0x0514,
	MMID0		= RMRO + 0x0518,
	MMIS1		= RMRO + 0x0520,
	MMIE1		= RMRO + 0x0524,
	MMID1		= RMRO + 0x0528,
	MMIS2		= RMRO + 0x0530,
	MMIE2		= RMRO + 0x0534,
	MMID2		= RMRO + 0x0538,
	MXMS		= RMRO + 0x0600,
};

/* AXIBMI */
#define RR_RATRR		BIT(0)
#define RR_TATRR		BIT(1)
#define RR_RST			(RR_RATRR | RR_TATRR)
#define RR_RST_COMPLETE		(0x03)

#define AXIWC_WREON_SHIFT	(12)
#define AXIWC_WRPON_SHIFT	(8)
#define AXIWC_WREON_DEFAULT	(0x04 << AXIWC_WREON_SHIFT)
#define AXIWC_WRPON_DEFAULT	(0x01 << AXIWC_WRPON_SHIFT)

#define AXIRC_RREON_SHIFT	(12)
#define AXIRC_RRPON_SHIFT	(8)
#define AXIRC_RREON_DEFAULT	(0x01 << AXIRC_RREON_SHIFT)
#define AXIRC_RRPON_DEFAULT	(0x08 << AXIRC_RRPON_SHIFT)

#define TATLS0_TEDE		BIT(1)
#define TATLS0_TATEN_SHIFT	(24)
#define TATLS0_TATEN(n)		((n) << TATLS0_TATEN_SHIFT)
#define TATLR_TATL		BIT(31)

#define RATLS0_REDE		BIT(3)
#define RATLS0_RATEN_SHIFT	(24)
#define RATLS0_RATEN(n)		((n) << RATLS0_RATEN_SHIFT)
#define RATLR_RATL		BIT(31)

#define DIE_DID_TDICX(n)	BIT((n))
#define DIE_DID_RDICX(n)	BIT((n) + 8)
#define TDIE_TDID_TDX(n)	BIT(n)
#define RDIE_RDID_RDX(n)	BIT(n)
#define TDIS_TDS(n)		BIT(n)
#define RDIS_RDS(n)		BIT(n)

/* MHD */
#define OSR_OPS			(0x07)
#define SWR_SWR			BIT(0)

#define TGC1_TQTM_SFM		(0xff00)

/* RMAC */
#define MPIC_PIS_MII		(0)
#define MPIC_PIS_RMII		(0x01)
#define MPIC_PIS_GMII		(0x02)
#define MPIC_PIS_RGMII		(0x03)
#define MPIC_LSC_10M		(0)
#define MPIC_LSC_100M		(0x01)
#define MPIC_LSC_1G		(0x02)
#define MPIC_PSMCS_SHIFT	(16)
#define MPIC_PSMCS_MASK		GENMASK(21, MPIC_PSMCS_SHIFT)
#define MPIC_PSMCS_DEFAULT	(0x1a << MPIC_PSMCS_SHIFT)

#define MLVC_PLV		BIT(17)

#define MPSM_PSME		BIT(0)
#define MPSM_PSMAD		BIT(1)
#define MPSM_PDA_SHIFT		(3)
#define MPSM_PDA_MASK		GENMASK(7, 3)
#define MPSM_PDA(n)		(((n) << MPSM_PDA_SHIFT) & MPSM_PDA_MASK)
#define MPSM_PRA_SHIFT		(8)
#define MPSM_PRA_MASK		GENMASK(12, 8)
#define MPSM_PRA(n)		(((n) << MPSM_PRA_SHIFT) & MPSM_PRA_MASK)
#define MPSM_PRD_SHIFT		(16)
#define MPSM_PRD_SET(n)		((n) << MPSM_PRD_SHIFT)
#define MPSM_PRD_GET(n)		((n) >> MPSM_PRD_SHIFT)

/* RTSN */
#define RTSN_INTERVAL_US	1000
#define RTSN_TIMEOUT_US		1000000

#define TX_NUM_CHAINS		1
#define RX_NUM_CHAINS		1

#define TX_CHAIN_SIZE		1024
#define RX_CHAIN_SIZE		1024

#define PKT_BUF_SZ		1584
#define RTSN_ALIGN		128

enum rtsn_mode {
	OCR_OPC_DISABLE,
	OCR_OPC_CONFIG,
	OCR_OPC_OPERATION,
};

/* Descriptors */
enum RX_DS_CC_BIT {
	RX_DS	= 0x0fff, /* Data size */
	RX_TR	= 0x1000, /* Truncation indication */
	RX_EI	= 0x2000, /* Error indication */
	RX_PS	= 0xc000, /* Padding selection */
};

enum TX_FS_TAGL_BIT {
	TX_DS	= 0x0fff, /* Data size */
	TX_TAGL	= 0xf000, /* Frame tag LSBs */
};

enum DIE_DT {
	/* HW/SW arbitration */
	DT_FEMPTY_IS	= 0x10,
	DT_FEMPTY_IC	= 0x20,
	DT_FEMPTY_ND	= 0x30,
	DT_FEMPTY	= 0x40,
	DT_FEMPTY_START	= 0x50,
	DT_FEMPTY_MID	= 0x60,
	DT_FEMPTY_END	= 0x70,

	/* Frame data */
	DT_FSINGLE	= 0x80,
	DT_FSTART	= 0x90,
	DT_FMID		= 0xA0,
	DT_FEND		= 0xB0,

	/* Chain control */
	DT_LEMPTY	= 0xC0,
	DT_EEMPTY	= 0xD0,
	DT_LINK		= 0xE0,
	DT_EOS		= 0xF0,

	DT_MASK		= 0xF0,
	D_DIE		= 0x08,
};

struct rtsn_desc {
	__le16 info_ds;
	__u8 info;
	u8 die_dt;
	__le32 dptr;
} __packed;

struct rtsn_ts_desc {
	__le16 info_ds;
	__u8 info;
	u8 die_dt;
	__le32 dptr;
	__le32 ts_nsec;
	__le32 ts_sec;
} __packed;

struct rtsn_ext_desc {
	__le16 info_ds;
	__u8 info;
	u8 die_dt;
	__le32 dptr;
	__le64 info1;
} __packed;

struct rtsn_ext_ts_desc {
	__le16 info_ds;
	__u8 info;
	u8 die_dt;
	__le32 dptr;
	__le64 info1;
	__le32 ts_nsec;
	__le32 ts_sec;
} __packed;

enum EXT_INFO_DS_BIT {
	TXC = 0x40,
};

struct rtsn_private {
	struct net_device *ndev;
	struct platform_device *pdev;
	void __iomem *addr;
	struct rtsn_ptp_private *ptp_priv;
	struct clk *clk;
	u32 num_tx_ring;
	u32 num_rx_ring;
	u32 tx_desc_bat_size;
	dma_addr_t tx_desc_bat_dma;
	struct rtsn_desc *tx_desc_bat;
	u32 rx_desc_bat_size;
	dma_addr_t rx_desc_bat_dma;
	struct rtsn_desc *rx_desc_bat;
	dma_addr_t tx_desc_dma;
	dma_addr_t rx_desc_dma;
	struct rtsn_ext_desc *tx_ring;
	struct rtsn_ext_ts_desc *rx_ring;
	struct sk_buff **tx_skb;
	struct sk_buff **rx_skb;
	spinlock_t lock;	/* Register access lock */
	u32 cur_tx;
	u32 dirty_tx;
	u32 cur_rx;
	u32 dirty_rx;
	u8 ts_tag;
	struct napi_struct napi;

	struct mii_bus *mii;
	phy_interface_t iface;
	int link;
	int speed;
	u8 mac_addr[MAX_ADDR_LEN];
};

u32 rtsn_read(void __iomem *addr);
void rtsn_write(u32 data, void __iomem *addr);
void rtsn_modify(void __iomem *addr, u32 clear, u32 set);
int rtsn_reg_wait(void __iomem *addr, u32 mask, u32 expected);

#endif
