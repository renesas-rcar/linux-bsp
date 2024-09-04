/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Renesas USER CRC-WRAPPER drivers header
 *
 * Copyright (C) 2024 by Renesas Electronics Corporation
 *
 * Redistribution of this file is permitted under
 * the terms of the GNU Public License (GPL)
 */

#ifndef _USER_RENESAS_CRC_WRAPPER_H_
#define _USER_RENESAS_CRC_WRAPPER_H_

#define MM_IOC_MAGIC 'o'

/* WCRC modes */
#define INDEPENDENT_CRC_MODE	_IOWR(MM_IOC_MAGIC, 0, struct wcrc_info)
#define E2E_CRC_MODE	_IOWR(MM_IOC_MAGIC, 1, struct wcrc_info)
#define DATA_THROUGH_MODE	_IOWR(MM_IOC_MAGIC, 2, struct wcrc_info)
#define E2E_CRC_DATA_THROUGH_MODE	_IOWR(MM_IOC_MAGIC, 3, struct wcrc_info)
#define REG_ACC_BY_CMD_FUNC	_IOWR(MM_IOC_MAGIC, 4, struct wcrc_info)
#define COMP_CRC_RESULT	_IOWR(MM_IOC_MAGIC, 5, struct wcrc_info)

/* Polynomial modes */
#define POLY_32_ETHERNET	_IOWR(MM_IOC_MAGIC, 7, struct wcrc_info)
#define POLY_16_CCITT_FALSE_CRC16	_IOWR(MM_IOC_MAGIC, 8, struct wcrc_info)
#define POLY_8_SAE_J1850	_IOWR(MM_IOC_MAGIC, 9, struct wcrc_info)
#define POLY_8_0X2F	_IOWR(MM_IOC_MAGIC, 10, struct wcrc_info)
#define POLY_32_0XF4ACFB13	_IOWR(MM_IOC_MAGIC, 11, struct wcrc_info)
#define POLY_32_0X1EDC6F41	_IOWR(MM_IOC_MAGIC, 12, struct wcrc_info)
#define POLY_21_0X102899	_IOWR(MM_IOC_MAGIC, 13, struct wcrc_info)
#define POLY_17_0X1685B	_IOWR(MM_IOC_MAGIC, 14, struct wcrc_info)
#define POLY_15_0X4599	_IOWR(MM_IOC_MAGIC, 15, struct wcrc_info)

struct wcrc_info {
	//Input data and output crc data
	u32 data_input;
	u32 crc_data_out;
	u32 *pcrc_data;
	u32 kcrc_data_out;
	unsigned int data_input_len;
	u32 *pdata_input;
	u32 *pdata_output;

	// Size of data input
	int d_in_sz;

	// Calculation continuous check
	bool conti_cal;
	bool during_conti_cal;
	bool skip_data_in;

	//Selecting WCRC/CRC/KCRC/FIFO channels
	unsigned int wcrc_unit;

	//CRC addiotional features
	unsigned int poly_mode;
	unsigned int in_exor_on;
	unsigned int out_exor_on;
	unsigned int in_bit_swap;
	unsigned int out_bit_swap;
	unsigned int in_byte_swap;
	unsigned int out_byte_swap;

	//KCRC addiotional features
	unsigned int kcrc_cmd0;
	unsigned int kcrc_cmd1;
	unsigned int kcrc_cmd2;

	//Choosing CRC = 0, or KCRC = 1 mode
	bool crc_opt;

	//For E2E CRC mode
	//WCRCm_CRCm_EN
	unsigned int conv_size;
	//WCRCm_CRCm_INIT_CRC
	unsigned int init_crc_code;

};

/* Currently, only support 3 registers */
enum reg_id {
	CRC_CIN = 0,
	CRC_COUT,
	CRC_CTL,
	CRC_CTL2,
	KCRC_DIN,
	KCRC_DOUT,
	KCRC_CTL,
	KCRC_POLY,
	KCRC_XOR,
};

struct reg_acc_by_cmd {
	u32 reg_id;
	u32 write_val;
};

#endif /* _USER_RENESAS_CRC_WRAPPER_H_ */
