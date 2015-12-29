/*
 * Renesas R-Car CPUFreq Support
 *
 *  Copyright (C) 2015 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>
#include <linux/module.h>

int __init rcar_cpufreq_init(void)
{
	platform_device_register_simple("cpufreq-dt", -1, NULL, 0);
	return 0;
}

module_init(rcar_cpufreq_init);

MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_DESCRIPTION("R-Car CPUFreq driver");
MODULE_LICENSE("GPL v2");
