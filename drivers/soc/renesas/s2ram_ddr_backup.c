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

static unsigned int read_reg(unsigned int size, void __iomem *addr)
{
	unsigned int ret = 0;

	switch (size) {
	case 8:
		ret = readb_relaxed(addr);
		break;
	case 16:
		ret = readw_relaxed(addr);
		break;
	case 32:
		ret = readl_relaxed(addr);
		break;
	default:
		pr_debug("%s: Wrong access size\n", __func__);
		break;
	}

	return ret;
}

static void write_reg(unsigned int size, unsigned int value,
			void __iomem *addr)
{
	switch (size) {
	case 8:
		writeb_relaxed(value, addr);
		break;
	case 16:
		writew_relaxed(value, addr);
		break;
	case 32:
		writel_relaxed(value, addr);
		break;
	default:
		pr_debug("%s: Wrong access size\n", __func__);
		break;
	}
}

static int _do_ioremap(struct rcar_ip *ip)
{
	ip->virt_addr = ioremap_nocache(ip->base_addr, ip->size);

	if (ip->virt_addr == NULL) {
		pr_debug("s2ram ioremap: Could not remap IP register\n");
		return -ENOMEM;
	}

	return 0;
}
static int _do_backup(struct rcar_ip *ip)
{
	struct hw_register *ip_reg;
	void __iomem *virt_addr = NULL;
	int reg_count = 0;
	unsigned int j;

	if (ip->virt_addr == NULL) {
		pr_debug("s2ram backup: Registers have not been mapped\n");
		return -EINVAL;
	}

	ip_reg    = ip->ip_reg;
	reg_count = ip->reg_count;
	virt_addr = ip->virt_addr;

	pr_debug("s2ram backup:  Working with %s, size 0x%x, virt_addr 0x%p\n",
		  ip->ip_name, ip->size, ip->virt_addr);

	for (j = 0; j < reg_count; j++) {
		pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
			  ip_reg[j].reg_name, ip_reg[j].access_size,
			  ip_reg[j].reg_offset, ip_reg[j].reg_value);

		ip_reg[j].reg_value = read_reg(ip_reg[j].access_size,
				virt_addr + ip_reg[j].reg_offset);

		pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
			  ip_reg[j].reg_name, ip_reg[j].access_size,
			  ip_reg[j].reg_offset, ip_reg[j].reg_value);
	}

	return 0;
}

static int _do_restore(struct rcar_ip *ip)
{
	struct hw_register *ip_reg;
	void __iomem *virt_addr = NULL;
	int reg_count = 0;
	unsigned int j;

	if (ip->virt_addr == NULL) {
		pr_debug("s2ram restore: Registers have not been mapped\n");
		return -EINVAL;
	}

	ip_reg    = ip->ip_reg;
	reg_count = ip->reg_count;
	virt_addr = ip->virt_addr;

	pr_debug("s2ram restore: Working with %s, size 0x%x, virt_addr 0x%p\n",
		  ip->ip_name, ip->size, ip->virt_addr);

	if (strcmp(ip->ip_name, "PFC") && strcmp(ip->ip_name, "RWDT")) {
		for (j = 0; j < reg_count; j++) {
			pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
				ip_reg[j].reg_name,
				ip_reg[j].access_size,
				ip_reg[j].reg_offset,
				ip_reg[j].reg_value);

			write_reg(ip_reg[j].access_size, ip_reg[j].reg_value,
				virt_addr + ip_reg[j].reg_offset);

			pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
				ip_reg[j].reg_name,
				ip_reg[j].access_size,
				ip_reg[j].reg_offset,
				read_reg(ip_reg[j].access_size, virt_addr +
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

			/* Only two registers are backup/restored.
			 * If offset is zero, it is RWTCNT register
			 * and RWTCNT_CODE is used for writing.
			 * Otherwise, RWTCSRA_CODE is used for RWTCSRA register.
			 */
			writel_relaxed(ip_reg[j].reg_value |
				(!ip_reg[j].reg_offset ? RWTCNT_CODE
					: RWTCSRA_CODE),
				virt_addr + ip_reg[j].reg_offset);

			pr_debug("%-20s, access_size 0x%-2x, offset 0x%-4x, value 0x%x\n",
				ip_reg[j].reg_name,
				ip_reg[j].access_size,
				ip_reg[j].reg_offset,
				read_reg(ip_reg[j].access_size, virt_addr +
					ip_reg[j].reg_offset));
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

	return 0;
}

/*
 * Handle backup/restore of IP register
 *     ip: IP to be processed
 *     handling: Flag of processing
 *         DO_IOREMAP: ioremap
 *         DO_BACKUP: backup
 *         DO_RESTORE: restore
 */
int rcar_handle_registers(struct rcar_ip *ip, unsigned int handling)
{
	int ret = 0;

	switch (handling) {
	case DO_IOREMAP:
		ret = _do_ioremap(ip);
		break;
	case DO_BACKUP:
		ret = _do_backup(ip);
		break;
	case DO_RESTORE:
		ret = _do_restore(ip);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL(rcar_handle_registers);

/*
 * Handle backup/restore of IPs
 *     ip: Point to address of IP list to be processed
 *     handling: Flag of processing
 *         DO_IOREMAP: ioremap
 *         DO_BACKUP: backup
 *         DO_RESTORE: restore
 */
int rcar_handle_ips(struct rcar_ip **ip, unsigned int handling)
{
	struct rcar_ip *working_ip;
	unsigned int i = 0;
	unsigned int ret = 0;

	while (ip[i] != NULL) {
		working_ip = ip[i];
		ret = rcar_handle_registers(working_ip, handling);
		i++;
	}

	return ret;
}
EXPORT_SYMBOL(rcar_handle_ips);

#ifdef CONFIG_PM_SLEEP
static int ddr_backup_suspend(void)
{
	pr_debug("%s\n", __func__);

	return rcar_handle_ips(common_ips, DO_BACKUP);
}

static void ddr_backup_resume(void)
{
	pr_debug("%s\n", __func__);

	rcar_handle_ips(common_ips, DO_RESTORE);
}

static struct syscore_ops ddr_backup_syscore_ops = {
	.suspend = ddr_backup_suspend,
	.resume = ddr_backup_resume,
};

static int ddr_backup_init(void)
{
	int ret;

	/* Map register for all common IPs */
	ret = rcar_handle_ips(common_ips, DO_IOREMAP);

	register_syscore_ops(&ddr_backup_syscore_ops);

	return ret;
}
core_initcall(ddr_backup_init);

#endif /* CONFIG_PM_SLEEP */
