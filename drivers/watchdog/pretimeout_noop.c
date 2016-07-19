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
 * pretimeout_noop - No operation on watchdog pretimeout event
 * @wdd - watchdog_device
 *
 * This function prints a message about pretimeout to kernel log.
 */
static void pretimeout_noop(struct watchdog_device *wdd)
{
	pr_info("watchdog%d: pretimeout event\n", wdd->id);
}

static struct watchdog_governor watchdog_gov_noop = {
	.name		= "noop",
	.pretimeout	= pretimeout_noop,
	.owner		= THIS_MODULE,
};

static int __init watchdog_gov_noop_register(void)
{
	return watchdog_register_governor(&watchdog_gov_noop);
}
module_init(watchdog_gov_noop_register);

static void __exit watchdog_gov_noop_unregister(void)
{
	watchdog_unregister_governor(&watchdog_gov_noop);
}
module_exit(watchdog_gov_noop_unregister);

MODULE_AUTHOR("Vladimir Zapolskiy <vladimir_zapolskiy@mentor.com>");
MODULE_DESCRIPTION("Noop watchdog pretimeout governor");
MODULE_LICENSE("GPL");
