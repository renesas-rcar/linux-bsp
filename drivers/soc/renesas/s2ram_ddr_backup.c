/*
 * S2RAM supports for DDR Power-Supply Backup/Restore Function
 *
 * Copyright (C) 2016 Renesas Electronics Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 */
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/soc/renesas/s2ram_ddr_backup.h>
#include <linux/syscore_ops.h>
#include <linux/io.h>

/* Special code for RWDT write access*/
#define RWTCNT_CODE 0x5a5a0000
#define RWTCSRA_CODE 0xa5a5a500

/* INTC-EX */
static struct hw_register intc_ex_ip_regs[] = {
	{"CONFIG_00",	0x0180, 32, 0},
	{"CONFIG_01",	0x0184, 32, 0},
	{"CONFIG_02",	0x0188, 32, 0},
	{"CONFIG_03",	0x018C, 32, 0},
	{"CONFIG_04",	0x0190, 32, 0},
	{"CONFIG_05",	0x0194, 32, 0},
};

static struct rcar_ip intc_ex_ip = {
	.ip_name	= "INTC-SYS",
	.base_addr	= 0xE61C0000,
	.size		= 0x198,
	.reg_count	= ARRAY_SIZE(intc_ex_ip_regs),
	.ip_reg		= intc_ex_ip_regs,
};

/* SYSC */
static struct hw_register sysc_ip_regs[] = {
	{"SYSCIER",     0x00C, 32, 0},
	{"SYSCIMR",     0x010, 32, 0},
};

static struct rcar_ip sysc_ip = {
	.ip_name   = "SYSC",
	.base_addr = 0xE6180000,
	.size      = 0x14,
	.reg_count = ARRAY_SIZE(sysc_ip_regs),
	.ip_reg    = sysc_ip_regs,
};

static struct rcar_ip *common_ips[] = {
	&intc_ex_ip,
	&sysc_ip,
	NULL,
};

/*
 * Handle backup/restore of IP register
 *     ip: IP to be processed
 *     handling: Flag of processing
 *         DO_IOREMAP: ioremap
 *         DO_BACKUP: backup
 *         DO_RESTORE: restore
 */
int handle_registers(struct rcar_ip *ip, unsigned int handling)
{
	int reg_count = 0;
	unsigned int j;
	void __iomem *virt_addr = NULL;
	struct hw_register *ip_reg;

	if (handling == DO_IOREMAP) { /* ioremap */
		/* Called during boot process */
		virt_addr = ioremap_nocache(ip->base_addr, ip->size);
		if (virt_addr == NULL) {
			pr_debug("s2ram: %s: Could not remap IP register\n",
				__func__);
			return -ENOMEM;
		}

		ip->virt_addr = virt_addr;

		pr_debug("s2ram ioremap: %s: Working with %s, size 0x%x, virt_addr 0x%p\n",
			__func__, ip->ip_name, ip->size, ip->virt_addr);

	} else if (handling == DO_BACKUP) { /* backup */
		if (ip->virt_addr == NULL) {
			pr_debug("s2ram backup: %s: iomem has not been mapped\n",
				__func__);
			return -ENOMEM;
		}

		ip_reg    = ip->ip_reg;
		reg_count = ip->reg_count;
		virt_addr = ip->virt_addr;

		pr_debug("s2ram backup: %s:  Working with %s, size 0x%x, virt_addr 0x%p\n",
			  __func__, ip->ip_name, ip->size, ip->virt_addr);

		for (j = 0; j < reg_count; j++) {
			pr_debug("%-20ss, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
				  ip_reg[j].reg_name, ip_reg[j].access_size,
				  ip_reg[j].reg_offset, ip_reg[j].reg_value);

			switch (ip_reg[j].access_size) {
			case 8:
				ip_reg[j].reg_value =
					readb_relaxed(virt_addr +
						      ip_reg[j].reg_offset);
				break;
			case 16:
				ip_reg[j].reg_value =
					readw_relaxed(virt_addr +
						      ip_reg[j].reg_offset);
				break;
			case 32:
				ip_reg[j].reg_value =
					readl_relaxed(virt_addr +
						      ip_reg[j].reg_offset);
				break;
			}

			pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
				  ip_reg[j].reg_name, ip_reg[j].access_size,
				  ip_reg[j].reg_offset, ip_reg[j].reg_value);
		}
	} else { /* restore */
		if (ip->virt_addr == NULL) {
			pr_debug("s2ram backup: %s: iomem has not been mapped\n",
				__func__);
			return -ENOMEM;
		}

		ip_reg    = ip->ip_reg;
		reg_count = ip->reg_count;
		virt_addr = ip->virt_addr;

		pr_debug("s2ram restore: %s: Working with %s, size 0x%x, virt_addr 0x%p\n",
			  __func__, ip->ip_name, ip->size, ip->virt_addr);

		if (strcmp(ip->ip_name, "PFC") && strcmp(ip->ip_name, "RWDT")) {
			for (j = 0; j < reg_count; j++) {
				pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
					ip_reg[j].reg_name,
					ip_reg[j].access_size,
					ip_reg[j].reg_offset,
					ip_reg[j].reg_value);

				switch (ip_reg[j].access_size) {
				case 8:
					writeb_relaxed(ip_reg[j].reg_value,
						virt_addr +
						ip_reg[j].reg_offset);
					break;
				case 16:
					writew_relaxed(ip_reg[j].reg_value,
						virt_addr +
						ip_reg[j].reg_offset);
					break;
				case 32:
					writel_relaxed(ip_reg[j].reg_value,
						virt_addr +
						ip_reg[j].reg_offset);
					break;
				}

				pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
					ip_reg[j].reg_name,
					ip_reg[j].access_size,
					ip_reg[j].reg_offset,
					readl_relaxed(virt_addr +
						ip_reg[j].reg_offset));
			}
		} else if (!strcmp(ip->ip_name, "RWDT")) {
			/* For RWDT registers, need special way to write */
			for (j = 0; j < reg_count; j++) {
				pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
					ip_reg[j].reg_name,
					ip_reg[j].access_size,
					ip_reg[j].reg_offset,
					ip_reg[j].reg_value);

				writel_relaxed(ip_reg[j].reg_value |
					(!ip_reg[j].reg_offset ? RWTCNT_CODE
						: RWTCSRA_CODE),
					virt_addr +
					ip_reg[j].reg_offset);
					switch (ip_reg[j].access_size) {
					case 8:
					pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
						ip_reg[j].reg_name,
						ip_reg[j].access_size,
						ip_reg[j].reg_offset,
						readb_relaxed(virt_addr +
							ip_reg[j].reg_offset));
					break;
					case 16:
					pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
						ip_reg[j].reg_name,
						ip_reg[j].access_size,
						ip_reg[j].reg_offset,
						readw(virt_addr +
							ip_reg[j].reg_offset));
					break;
					}
			}
		} else {
			/* For PFC registers, need to unlock before writing */
			for (j = 0; j < reg_count; j++) {
				pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
					ip_reg[j].reg_name,
					ip_reg[j].access_size,
					ip_reg[j].reg_offset,
					ip_reg[j].reg_value);

				writel_relaxed(~ip_reg[j].reg_value,
					virt_addr + ip_reg[0].reg_offset);
				writel_relaxed(ip_reg[j].reg_value,
						virt_addr +
						ip_reg[j].reg_offset);
				pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
					ip_reg[j].reg_name,
					ip_reg[j].access_size,
					ip_reg[j].reg_offset,
					readl_relaxed(virt_addr +
						ip_reg[j].reg_offset));
			}
		}
	}

	return 0;
}

/*
 * Handle backup/restore of IPs
 *     ip: Point to address of IP list to be processed
 *     handling: Flag of processing
 *         DO_IOREMAP: ioremap
 *         DO_BACKUP: backup
 *         DO_RESTORE: restore
 */
int handle_ips(struct rcar_ip **ip, unsigned int handling)
{
	struct rcar_ip *working_ip;
	unsigned int i = 0;
	unsigned int ret = 0;

	while (ip[i] != NULL) {
		working_ip = ip[i];
		ret = handle_registers(working_ip, handling);
		i++;
	}

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int ddr_bck_suspend(void)
{
	pr_debug("%s\n", __func__);

	return handle_ips(common_ips, DO_BACKUP);
}

static void ddr_bck_resume(void)
{
	pr_debug("%s\n", __func__);

	handle_ips(common_ips, DO_RESTORE);
}

static struct syscore_ops ddr_bck_syscore_ops = {
	.suspend = ddr_bck_suspend,
	.resume = ddr_bck_resume,
};

static int ddr_bck_init(void)
{
	int ret;

	/* Map register for all common IPs */
	ret = handle_ips(common_ips, DO_IOREMAP);

	register_syscore_ops(&ddr_bck_syscore_ops);

	return ret;
}
core_initcall(ddr_bck_init);

#endif /* CONFIG_PM_SLEEP */
