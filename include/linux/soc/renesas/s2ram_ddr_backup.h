#ifndef S2RAM_DDR_BACKUP_H
#define S2RAM_DDR_BACKUP_H

#include <linux/types.h>

enum {
	DO_IOREMAP,
	DO_BACKUP,
	DO_RESTORE,
};

struct hw_register {
	char *reg_name;
	unsigned int reg_offset;
	unsigned int access_size;
	unsigned int reg_value;
};

struct rcar_ip {
	char *ip_name;
	void __iomem *virt_addr;
	phys_addr_t base_addr;
	unsigned int size;
	unsigned int reg_count;
	struct hw_register *ip_reg;
};

#ifdef CONFIG_RCAR_DDR_BACKUP
int handle_registers(struct rcar_ip *ip, unsigned int handling);
int handle_ips(struct rcar_ip **ip, unsigned int handling);
#else
static inline int handle_registers(struct rcar_ip *ip, unsigned int handling)
{
	return 0;
}

static inline int handle_ips(struct rcar_ip **ip, unsigned int handling)
{
	return 0;
}
#endif /* CONFIG_RCAR_DDR_BACKUP */

#endif /* S2RAM_DDR_BACKUP_H */
