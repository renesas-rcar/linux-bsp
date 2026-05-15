/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2019 Synopsys, Inc. and/or its affiliates.
 *
 * Author: Vitor Soares <vitor.soares@synopsys.com>
 */

#ifndef _UAPI_I3C_DEV_H_
#define _UAPI_I3C_DEV_H_

#include <linux/types.h>
#include <linux/ioctl.h>

/* IOCTL commands */
#define I3C_DEV_IOC_MAGIC	0x07

struct i3c_ioc_priv_xfer {
	struct i3c_priv_xfer __user *xfers;	/* pointers to i3c_priv_xfer */
	__u32 nxfers;				/* number of i3c_priv_xfer */
};

#define I3C_IOC_PRIV_XFER	\
	_IOWR(I3C_DEV_IOC_MAGIC, 30, struct i3c_ioc_priv_xfer)

#define  I3C_IOC_PRIV_XFER_MAX_MSGS	42

#endif
