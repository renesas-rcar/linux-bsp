// SPDX-License-Identifier: GPL-2.0+
/*
 * System Control and Management Interface (SCMI) Vendor Protocol
 *
 * Copyright (C) 2026 Renesas Electronics Corporation
 */

#define pr_fmt(fmt) "SCMI Notifications VENDOR - " fmt

#include <linux/module.h>
#include <linux/byteorder/generic.h>
#include <linux/scmi_protocol.h>

#include "common.h"
#include "protocols.h"
#include "notify.h"

enum scmi_vendor_protocol_cmd {
	RESET_DOMAIN_STATUS = 0x80,
};

struct scmi_vendor_info {
	u32 version;
};

static int
renesas_reset_status_get(const struct scmi_protocol_handle *ph, u32 domain)
{
	int ret;
	u32 reset_status;
	struct scmi_xfer *t;
	__le32 *attr;

	ret = ph->xops->xfer_get_init(ph, RESET_DOMAIN_STATUS,
				      sizeof(u32), sizeof(u32), &t);
	if (ret)
		return ret;

	put_unaligned_le32(domain, t->tx.buf);
	attr = t->rx.buf;

	ret = ph->xops->do_xfer(ph, t);
	if (!ret)
		reset_status = le32_to_cpu(*attr);

	ph->xops->xfer_put(ph, t);

	return ret ? ret : !reset_status;
}

static const struct scmi_vendor_ext_ops renesas_vendor_ops = {
	.reset_status_get = renesas_reset_status_get,
};

static int scmi_vendor_protocol_init(const struct scmi_protocol_handle *ph)
{
	int ret;
	u32 version;
	struct scmi_vendor_info *pinfo;

	ret = ph->xops->version_get(ph, &version);
	if (ret)
		return ret;

	dev_dbg(ph->dev, "Vendor Version %d.%d\n",
		PROTOCOL_REV_MAJOR(version), PROTOCOL_REV_MINOR(version));

	pinfo = devm_kzalloc(ph->dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	pinfo->version = version;
	ret = ph->set_priv(ph, pinfo, version);
	if (!ret)
		scmi_vendor_ops_register(ph, &renesas_vendor_ops);

	return ret;
}

static const struct scmi_protocol scmi_vendor = {
	.id = SCMI_PROTOCOL_VENDOR,
	.owner = THIS_MODULE,
	.instance_init = &scmi_vendor_protocol_init,
	.ops = &renesas_vendor_ops,
};

DEFINE_SCMI_PROTOCOL_REGISTER_UNREGISTER(vendor, scmi_vendor)
