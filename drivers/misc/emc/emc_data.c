/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * emmc_data.h - definitions for the Linux eMMC interface
 * Copyright (C) 2022-2023 Xinyu Feng <xinyu.feng.kx@renesas.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <uapi/misc/emc_data.h>

/* Define global variable *****/
struct emmc_exception_data emc_global_data[ EMMC_EXCEPTION_DATA_SIZE ];
struct mutex lock;

/******************************/
/*Para:   void ****************/
/*Return: 0:OK, 1:NG **********/
/******************************/
int emc_init_exp_info(void)
{
	mutex_lock(&lock);
	memset(emc_global_data, 0xFF, sizeof(emc_global_data));
	mutex_unlock(&lock);
	return RESUTL_OK;
}

/******************************/
/*Para:   emc_index(input) ****/
/*Para:   emc_data(output) ****/
/*Return: 0:OK, 1:NG **********/
/******************************/
int emc_get_exp_info(int emc_index, unsigned short *emc_data)
{
	if(emc_index < 0 || emc_index >= EMMC_EXCEPTION_DATA_SIZE || NULL == emc_data)
	{
		return RESUTL_NG;
	}
	*emc_data = 0xFF;
	mutex_lock(&lock);
	*emc_data = emc_global_data[ emc_index ].e_data;
	mutex_unlock(&lock);
	return RESUTL_OK;
}

/******************************/
/*Para:   emc_index(input) ****/
/*Para:   emc_data(input) *****/
/*Return: 0:OK, 1:NG **********/
/******************************/
int emc_set_exp_info(int emc_index, unsigned short emc_data)
{
	if(emc_index < 0 || emc_index >= EMMC_EXCEPTION_DATA_SIZE)
	{
		return RESUTL_NG;
	}
	mutex_lock(&lock);
	emc_global_data[ emc_index ].e_data = emc_data;
	mutex_unlock(&lock);
	return RESUTL_OK;
}

