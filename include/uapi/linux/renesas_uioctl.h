/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Renesas IOCTL defines for user io drivers;
 * used to control power, clock and reset
 *
 * Copyright (C) 2020 by Renesas Electronics Corporation
 *
 * Redistribution of this file is permitted under
 * the terms of the GNU Public License (GPL)
 */
#ifndef _RENESAS_UIOCTL_H_
#define _RENESAS_UIOCTL_H_

#define UIO_PDRV_IOCCTL_BASE	'I'
#define UIO_PDRV_SET_PWR	_IOW(UIO_PDRV_IOCCTL_BASE, 0, int)
#define UIO_PDRV_GET_PWR	_IOR(UIO_PDRV_IOCCTL_BASE, 1, int)
#define UIO_PDRV_SET_CLK	_IOW(UIO_PDRV_IOCCTL_BASE, 2, int)
#define UIO_PDRV_GET_CLK	_IOR(UIO_PDRV_IOCCTL_BASE, 3, int)
#define UIO_PDRV_SET_RESET	_IOW(UIO_PDRV_IOCCTL_BASE, 4, int)
#define UIO_PDRV_GET_RESET	_IOR(UIO_PDRV_IOCCTL_BASE, 5, int)
#define UIO_PDRV_CLK_GET_DIV	_IOR(UIO_PDRV_IOCCTL_BASE, 6, int)
#define UIO_PDRV_CLK_SET_DIV	_IOR(UIO_PDRV_IOCCTL_BASE, 7, int)

#endif /* _RENESAS_UIOCTL_H_ */
