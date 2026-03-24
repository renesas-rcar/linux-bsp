// SPDX-License-Identifier: GPL-2.0+
/*
 * SCMI Generic vendor support.
 *
 * Copyright (C) 2026 Renesas Electronics Corporation
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/scmi_protocol.h>

static const struct scmi_vendor_ext_ops *vendor_ops;

static int scmi_vendor_probe(struct scmi_device *sdev)
{
	const struct scmi_handle *handle = sdev->handle;
	struct scmi_protocol_handle *ph;

	if (!handle)
		return -ENODEV;

	vendor_ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_VENDOR, &ph);
	if (IS_ERR(vendor_ops))
		return PTR_ERR(vendor_ops);

	return 0;
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_VENDOR, "vendor" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_vendor_driver = {
	.name = "scmi-vendor",
	.probe = scmi_vendor_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_vendor_driver);

MODULE_AUTHOR("Vinh Nguyen <vinh.nguyen.xz@renesas.com>");
MODULE_DESCRIPTION("ARM SCMI vendor driver");
MODULE_LICENSE("GPL");
