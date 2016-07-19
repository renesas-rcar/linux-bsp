/*
 * Copyright (C) 2015 Mentor Graphics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/watchdog.h>

#include "watchdog_pretimeout.h"

/**
 * pretimeout_userspace - Notify userspace on watchdog pretimeout event
 * @wdd - watchdog_device
 *
 * Send watchdog device uevent to userspace to handle pretimeout event
 */
static void pretimeout_userspace(struct watchdog_device *wdd)
{
	watchdog_dev_uevent(wdd);
}

static struct watchdog_governor watchdog_gov_userspace = {
	.name		= "userspace",
	.pretimeout	= pretimeout_userspace,
	.owner		= THIS_MODULE,
};

static int __init watchdog_gov_userspace_register(void)
{
	return watchdog_register_governor(&watchdog_gov_userspace);
}
module_init(watchdog_gov_userspace_register);

static void __exit watchdog_gov_userspace_unregister(void)
{
	watchdog_unregister_governor(&watchdog_gov_userspace);
}
module_exit(watchdog_gov_userspace_unregister);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir_zapolskiy@mentor.com>");
MODULE_DESCRIPTION("Userspace notifier watchdog pretimeout governor");
MODULE_LICENSE("GPL");
