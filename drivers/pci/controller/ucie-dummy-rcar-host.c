// SPDX-License-Identifier: GPL-2.0+
/*
 * UCIe Host mode driver for Renesas R-Car SoCs
 *  Copyright (C) 2025 Renesas Electronics Corp.
 *
 * Based on R-Car PCIe driver architecture
 * This is a simplified Root Complex driver for virtual platform testing
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "ucie-dummy-rcar.h"

/* Structure representing the UCIe host controller */
struct ucie_dummy_host {
	struct ucie_dummy	ucie;		/* Base structure */
	struct pci_host_bridge	*bridge;	/* PCI host bridge */
	struct clk		*bus_clk;	/* Bus clock */
	int			irq;		/* IRQ number */
	u32			bar0_value;	/* BAR0 config value */
	u32			bar1_value;	/* BAR1 config value */
	u32			bar2_value;	/* BAR2 config value */
	u32			bar3_value;	/* BAR3 config value */

	/* Data mapping state (host-specific feature) */
	/* BAR0-as-shared-memory model: user space mmaps BAR0 directly */
	size_t			map_size;	/* Shared memory size */
	bool			mapping_active;	/* Mapping is active */

	/* Register polling for user space MMIO writes */
	struct delayed_work	poll_work;	/* Polling work */
	u32			last_mapping_en; /* Last MAPPING_EN value */
	u32			last_sync_to;	/* Last RQ_SYNC_TO value */
	u32			last_sync_from;	/* Last RQ_SYNC_FROM value */
};

/* Forward declarations for data mapping functions */
static int ucie_host_enable_mapping(struct ucie_dummy_host *host);
static void ucie_host_disable_mapping(struct ucie_dummy_host *host);
static int ucie_host_sync_to_dest(struct ucie_dummy_host *host);
static int ucie_host_sync_from_dest(struct ucie_dummy_host *host);

/*
 * UCIe hardware initialization
 */
static int ucie_dummy_hw_init(struct ucie_dummy *ucie)
{
	struct device *dev = ucie->dev;

	dev_info(dev, "UCIe dummy hardware initialization\n");

	/* Clear all interrupts */
	ucie_write_reg(ucie, 0xFFFF, UCIE_CLR_INT);

	/* Disable mapping initially */
	ucie_write_reg(ucie, 0, UCIE_MAPPING_EN);

	/* Clear mapping status */
	ucie_write_reg(ucie, MAPPING_FILE_CLR, UCIE_MAPPING_CLR);

	dev_info(dev, "UCIe dummy hardware initialized\n");

	return 0;
}

/*
 * UCIe Data Mapping Functions (Host-Specific)
 *
 * Architecture: BAR0 IS the Shared Memory
 * - BAR0 (0xdc002000) is the actual shared memory region (ucie_00)
 * - User space mmaps BAR0 to access shared memory directly
 * - Driver just enables/disables sharing and manages state
 * - No external memory mapping needed (zero-copy access)
 */

/*
 * Enable UCIe data mapping
 * Marks the shared memory region as active and accessible
 */
static int ucie_host_enable_mapping(struct ucie_dummy_host *host)
{
	struct ucie_dummy *ucie = &host->ucie;
	struct device *dev = ucie->dev;
	u64 src_addr, dst_addr;
	u32 size_units;
	size_t size_bytes;

	/* Read mapping configuration from registers (for logging/state) */
	src_addr = ((u64)ucie_read_reg(ucie, UCIE_UPPER_SRC_ADDR) << 32) |
		   ucie_read_reg(ucie, UCIE_LOWER_SRC_ADDR);
	dst_addr = ((u64)ucie_read_reg(ucie, UCIE_UPPER_DST_ADDR) << 32) |
		   ucie_read_reg(ucie, UCIE_LOWER_DST_ADDR);
	size_units = ucie_read_reg(ucie, UCIE_MAPPING_SIZE);
	size_bytes = (size_t)size_units * 4;  /* Size is in 4-byte units */

	dev_info(dev, "Enabling shared memory mapping:\n");
	dev_info(dev, "  BAR0 region: 0x%llx (size: 0x%zx bytes)\n",
		 (u64)ucie->bar0_phys, size_bytes);
	dev_info(dev, "  Accessible to UCIe devices\n");

	/* Validate parameters */
	if (size_bytes == 0) {
		dev_err(dev, "Invalid mapping size: 0\n");
		return -EINVAL;
	}

	/* Disable existing mapping if active */
	if (host->mapping_active) {
		dev_info(dev, "Disabling existing mapping before creating new one\n");
		ucie_host_disable_mapping(host);
	}

	/* BAR0 is the shared memory - just mark as active */
	host->map_size = size_bytes;
	host->mapping_active = true;

	dev_info(dev, "Shared memory mapping enabled\n");
	dev_info(dev, "User space can access via: /sys/bus/pci/devices/.../resource0\n");

	return 0;
}

/*
 * Disable UCIe data mapping
 * Marks the shared memory as inactive
 */
static void ucie_host_disable_mapping(struct ucie_dummy_host *host)
{
	struct device *dev = host->ucie.dev;

	if (!host->mapping_active) {
		dev_dbg(dev, "No active mapping to disable\n");
		return;
	}

	dev_info(dev, "Disabling shared memory mapping\n");

	/* Clear state */
	host->map_size = 0;
	host->mapping_active = false;

	dev_info(dev, "Shared memory mapping disabled\n");
}

/*
 * Sync data (dummy implementation)
 * In real hardware, this would trigger hardware to sync data between UCIe devices
 * For BAR0-as-shared-memory model, user space directly accesses the memory
 */
static int ucie_host_sync_to_dest(struct ucie_dummy_host *host)
{
	struct device *dev = host->ucie.dev;

	if (!host->mapping_active) {
		dev_err(dev, "Cannot sync: no active mapping\n");
		return -EINVAL;
	}

	dev_info(dev, "Sync requested (BAR0 shared memory model - no-op)\n");
	dev_info(dev, "Data is already shared via BAR0 - no sync needed\n");

	/* Set sync status bit */
	ucie_rmw32(&host->ucie, UCIE_RQ_SYNC_TO, SYNC_STS, SYNC_STS);

	return 0;
}

/*
 * Sync from destination (dummy implementation)
 */
static int ucie_host_sync_from_dest(struct ucie_dummy_host *host)
{
	struct device *dev = host->ucie.dev;

	if (!host->mapping_active) {
		dev_err(dev, "Cannot sync: no active mapping\n");
		return -EINVAL;
	}

	dev_info(dev, "Reverse sync requested (BAR0 shared memory model - no-op)\n");
	dev_info(dev, "Data is already shared via BAR0 - no sync needed\n");

	/* Set sync status bit */
	ucie_rmw32(&host->ucie, UCIE_RQ_SYNC_FROM, SYNC_STS, SYNC_STS);

	return 0;
}

/*
 * Register write hook - intercepts writes to mapping control registers
 * This is called from the common layer write function
 */
static void ucie_host_handle_reg_write(struct ucie_dummy_host *host,
				       unsigned int reg, u32 val)
{
	struct ucie_dummy *ucie = &host->ucie;
	u32 old_val;

	switch (reg) {
	case UCIE_MAPPING_EN:
		old_val = ucie_read_reg(ucie, reg);

		/* Check if mapping enable state changed */
		if ((val & MAPPING_FILE_EN) && !(old_val & MAPPING_FILE_EN)) {
			/* Enabling mapping */
			dev_info(ucie->dev, "Enabling UCIe data mapping\n");
			if (ucie_host_enable_mapping(host) == 0) {
				/* Set MAPPING_FILE_STS bit to indicate success */
				ucie_rmw32(ucie, UCIE_MAPPING_EN, MAPPING_FILE_STS,
					   MAPPING_FILE_STS);
				/* Trigger interrupt INT14_SUB */
				ucie_rmw32(ucie, UCIE_INT_STS, INT14_SUB, INT14_SUB);
				dev_info(ucie->dev, "UCIe data mapping enabled successfully\n");
			} else {
				dev_err(ucie->dev, "Failed to enable UCIe data mapping\n");
			}
		} else if (!(val & MAPPING_FILE_EN) && (old_val & MAPPING_FILE_EN)) {
			/* Disabling mapping */
			dev_info(ucie->dev, "Disabling UCIe data mapping\n");
			ucie_host_disable_mapping(host);
			/* Clear MAPPING_FILE_STS bit */
			ucie_rmw32(ucie, UCIE_MAPPING_EN, MAPPING_FILE_STS, 0);
			/* Trigger interrupt INT14_SUB */
			ucie_rmw32(ucie, UCIE_INT_STS, INT14_SUB, INT14_SUB);
			dev_info(ucie->dev, "UCIe data mapping disabled\n");
		}
		break;

	case UCIE_MAPPING_CLR:
		/* Handle mapping clear */
		if (val & MAPPING_FILE_CLR) {
			dev_info(ucie->dev, "Clearing UCIe mapping file status\n");
			ucie_host_disable_mapping(host);
			ucie_rmw32(ucie, UCIE_MAPPING_EN, MAPPING_FILE_STS, 0);
		}
		break;

	case UCIE_RQ_SYNC_TO:
		/* Handle sync to destination request */
		if (val & STR_SYNC)
			ucie_host_sync_to_dest(host);

		if (val & CLR_STS)
			/* Clear sync status */
			ucie_rmw32(ucie, UCIE_RQ_SYNC_TO, SYNC_STS, 0);

		break;

	case UCIE_RQ_SYNC_FROM:
		/* Handle sync from destination request */
		if (val & STR_SYNC)
			ucie_host_sync_from_dest(host);

		if (val & CLR_STS)
			/* Clear sync status */
			ucie_rmw32(ucie, UCIE_RQ_SYNC_FROM, SYNC_STS, 0);

		break;

	default:
		/* No special handling needed */
		break;
	}
}

/*
 * Host-specific register write wrapper
 * Intercepts writes to special registers and handles mapping logic
 * Following R-Car architecture pattern where host driver owns business logic
 */
static void ucie_host_write_reg(struct ucie_dummy_host *host,
				u32 val, unsigned int reg)
{
	/* Handle special mapping control registers before writing */
	ucie_host_handle_reg_write(host, reg, val);

	/* Write the register value */
	ucie_write_reg(&host->ucie, val, reg);
}

/*
 * Configuration space access for dummy device
 * Returns hardcoded values to simulate a PCIe device
 */
static int ucie_dummy_config_read(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 *val)
{
	struct ucie_dummy_host *host = bus->sysdata;
	struct ucie_dummy *ucie = &host->ucie;
	u32 data = 0;

	/* Only respond to device 0, function 0 */
	if (devfn != 0) {
		*val = 0xFFFFFFFF;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	/* Hardcode configuration space for dummy device */
	switch (where & ~3) {
	case PCI_VENDOR_ID:
		/* Vendor ID + Device ID */
		data = (UCIE_DUMMY_DEVICE_ID << 16) | UCIE_DUMMY_VENDOR_ID;
		break;
	case PCI_COMMAND:
		/* Command + Status */
		data = (PCI_STATUS_CAP_LIST << 16) |
		       (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
		break;
	case PCI_CLASS_REVISION:
		/* Class code: Network controller */
		data = (PCI_CLASS_NETWORK_OTHER << 16) | 0x01;
		break;
	case PCI_CACHE_LINE_SIZE:
		/* Cache line size, latency, header type, BIST */
		data = PCI_HEADER_TYPE_NORMAL;
		break;
	case PCI_BASE_ADDRESS_0:
		/* BAR0 - Memory space, 64-bit, prefetchable
		 * For dummy driver, BAR0 is fixed to actual register physical address
		 * to allow direct user space access via /sys/bus/pci/devices/.../resource0
		 */
		if (host->bar0_value == 0xffffffff) {
			/* Sizing operation - return size */
			data = ~(UCIE_BAR0_SIZE - 1) |
			       PCI_BASE_ADDRESS_MEM_TYPE_64 |
			       PCI_BASE_ADDRESS_MEM_PREFETCH;
		} else {
			/* Always return fixed physical address for user space mapping */
			data = (ucie->bar0_phys & 0xFFFFFFFF) |
			       PCI_BASE_ADDRESS_MEM_TYPE_64 |
			       PCI_BASE_ADDRESS_MEM_PREFETCH;
		}
		break;
	case PCI_BASE_ADDRESS_1:
		/* BAR1 - upper 32-bit of 64-bit BAR */
		if (host->bar1_value == 0xffffffff)
			data = 0xffffffff;
		else
			data = (ucie->bar0_phys >> 32) & 0xFFFFFFFF;
		break;
	case PCI_SUBSYSTEM_VENDOR_ID:
		/* Subsystem Vendor ID + Subsystem ID */
		data = (UCIE_DUMMY_DEVICE_ID << 16) | UCIE_DUMMY_VENDOR_ID;
		break;
	case PCI_INTERRUPT_LINE:
		/* Interrupt line, pin */
		data = (1 << 8); /* INT A */
		break;
	default:
		data = 0;
		break;
	}

	/* Extract requested size */
	if (size == 1)
		*val = (data >> ((where & 3) * 8)) & 0xFF;
	else if (size == 2)
		*val = (data >> ((where & 2) * 8)) & 0xFFFF;
	else
		*val = data;

	dev_dbg(&bus->dev, "config read: bus=%d devfn=0x%x where=0x%x size=%d val=0x%x\n",
		bus->number, devfn, where, size, *val);

	return PCIBIOS_SUCCESSFUL;
}

static int ucie_dummy_config_write(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 val)
{
	struct ucie_dummy_host *host = bus->sysdata;

	/* Only respond to device 0, function 0 */
	if (devfn != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	dev_dbg(&bus->dev, "config write: bus=%d devfn=0x%x where=0x%x size=%d val=0x%x\n",
		bus->number, devfn, where, size, val);

	/* Handle BAR writes for proper enumeration */
	switch (where & ~3) {
	case PCI_BASE_ADDRESS_0:
		if (size == 4)
			host->bar0_value = val;
		break;
	case PCI_BASE_ADDRESS_1:
		if (size == 4)
			host->bar1_value = val;
		break;
	default:
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops ucie_dummy_ops = {
	.read	= ucie_dummy_config_read,
	.write	= ucie_dummy_config_write,
};

/*
 * UCIe interrupt handler
 */
static irqreturn_t ucie_dummy_irq(int irq, void *data)
{
	struct ucie_dummy_host *host = data;
	struct ucie_dummy *ucie = &host->ucie;
	u32 status;

	status = ucie_read_reg(ucie, UCIE_INT_STS);
	if (!status)
		return IRQ_NONE;

	dev_dbg(ucie->dev, "UCIe interrupt: status=0x%x\n", status);

	/* Clear all interrupts */
	ucie_write_reg(ucie, status, UCIE_CLR_INT);

	return IRQ_HANDLED;
}

/*
 * Sysfs interface for UCIe mapping control
 */
static ssize_t mapping_control_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ucie_dummy_host *host = platform_get_drvdata(pdev);
	struct ucie_dummy *ucie = &host->ucie;
	u32 mapping_en, mapping_size;
	u64 src_addr, dst_addr;

	mapping_en = ucie_read_reg(ucie, UCIE_MAPPING_EN);
	mapping_size = ucie_read_reg(ucie, UCIE_MAPPING_SIZE);
	src_addr = ((u64)ucie_read_reg(ucie, UCIE_UPPER_SRC_ADDR) << 32) |
		   ucie_read_reg(ucie, UCIE_LOWER_SRC_ADDR);
	dst_addr = ((u64)ucie_read_reg(ucie, UCIE_UPPER_DST_ADDR) << 32) |
		   ucie_read_reg(ucie, UCIE_LOWER_DST_ADDR);

	return sprintf(buf,
		"UCIe Mapping Control:\n"
		"  Enabled: %s\n"
		"  File Mapping Enabled: %s\n"
		"  Mapping Status: %s\n"
		"  Source Address: 0x%llx\n"
		"  Destination Address: 0x%llx\n"
		"  Size: 0x%x (4-byte units)\n"
		"\nUsage: echo 'src_addr dst_addr size' > mapping_control\n"
		"  Addresses in hex, size in 4-byte units (decimal)\n",
		(mapping_en & MAPPING_EN) ? "Yes" : "No",
		(mapping_en & MAPPING_FILE_EN) ? "Yes" : "No",
		(mapping_en & MAPPING_FILE_STS) ? "Active" : "Inactive",
		src_addr, dst_addr, mapping_size);
}

static ssize_t mapping_control_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ucie_dummy_host *host = platform_get_drvdata(pdev);
	struct ucie_dummy *ucie = &host->ucie;
	u64 src_addr, dst_addr;
	u32 size;
	int ret;

	/* Parse input: src_addr dst_addr size (size in 4-byte units, decimal) */
	ret = sscanf(buf, "%llx %llx %u", &src_addr, &dst_addr, &size);
	if (ret != 3) {
		dev_err(dev, "Invalid format. Use: src_addr dst_addr size\n");
		return -EINVAL;
	}

	/* Validate alignment (must be page-aligned for file mapping) */
	if ((src_addr & 0xFFF) || (dst_addr & 0xFFF))
		dev_warn(dev, "Addresses not page-aligned, file mapping may fail\n");

	dev_info(dev, "Configuring UCIe mapping:\n");
	dev_info(dev, "  Src: 0x%llx, Dst: 0x%llx, Size: 0x%x\n",
		 src_addr, dst_addr, size);

	/* Program source address */
	ucie_write_reg(ucie, src_addr >> 32, UCIE_UPPER_SRC_ADDR);
	ucie_write_reg(ucie, src_addr & 0xFFFFFFFF, UCIE_LOWER_SRC_ADDR);

	/* Program destination address */
	ucie_write_reg(ucie, dst_addr >> 32, UCIE_UPPER_DST_ADDR);
	ucie_write_reg(ucie, dst_addr & 0xFFFFFFFF, UCIE_LOWER_DST_ADDR);

	/* Program size */
	ucie_write_reg(ucie, size, UCIE_MAPPING_SIZE);

	/* Enable mapping (both direct and file mapping) - uses host wrapper */
	ucie_host_write_reg(host, MAPPING_FILE_EN | MAPPING_EN, UCIE_MAPPING_EN);

	dev_info(dev, "UCIe mapping configured and enabled\n");

	return count;
}

static ssize_t mapping_disable_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ucie_dummy_host *host = platform_get_drvdata(pdev);

	/* Disable mapping - uses host wrapper */
	ucie_host_write_reg(host, 0, UCIE_MAPPING_EN);

	/* Clear mapping file - uses host wrapper */
	ucie_host_write_reg(host, MAPPING_FILE_CLR, UCIE_MAPPING_CLR);

	dev_info(dev, "UCIe mapping disabled\n");

	return count;
}

static ssize_t bar_info_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ucie_dummy_host *host = platform_get_drvdata(pdev);
	struct ucie_dummy *ucie = &host->ucie;

	return sprintf(buf,
		"BAR0 Physical Address: 0x%llx\n"
		"BAR0 Size: 0x%llx\n"
		"VID:DID: %04x:%04x\n"
		"\n"
		"User Space Access Methods:\n"
		"==========================\n"
		"1. Direct sysfs mapping (requires root):\n"
		"   # Find PCI device\n"
		"   PCI_DEV=$(lspci -d %04x:%04x | awk '{print $1}')\n"
		"   # Example: Read UCIE_MAPPING_EN (offset 0x64)\n"
		"   devmem 0x%llx 32\n"
		"\n"
		"2. PCI resource mapping (requires root):\n"
		"   # Map resource0 and access registers\n"
		"   mmap /sys/bus/pci/devices/0001:00:00.0/resource0\n"
		"\n"
		"3. UIO driver (recommended for user space):\n"
		"   # Bind to uio_pci_generic driver\n"
		"   echo \"%04x %04x\" > /sys/bus/pci/drivers/uio_pci_generic/new_id\n"
		"   # Then access via /dev/uioX\n"
		"\n"
		"Register Offsets (from ucie.adoc):\n"
		"  UCIE_UPPER_SRC_ADDR:  0x50\n"
		"  UCIE_LOWER_SRC_ADDR:  0x54\n"
		"  UCIE_UPPER_DST_ADDR:  0x58\n"
		"  UCIE_LOWER_DST_ADDR:  0x5C\n"
		"  UCIE_MAPPING_SIZE:    0x60\n"
		"  UCIE_MAPPING_EN:      0x64\n"
		"  UCIE_CLR_INT:         0x70\n"
		"  UCIE_INT_STS:         0x74\n",
		(u64)ucie->bar0_phys, (u64)ucie->bar0_size,
		UCIE_DUMMY_VENDOR_ID, UCIE_DUMMY_DEVICE_ID,
		UCIE_DUMMY_VENDOR_ID, UCIE_DUMMY_DEVICE_ID,
		(u64)ucie->bar0_phys + UCIE_MAPPING_EN,
		UCIE_DUMMY_VENDOR_ID, UCIE_DUMMY_DEVICE_ID);
}

static DEVICE_ATTR_RW(mapping_control);
static DEVICE_ATTR_WO(mapping_disable);
static DEVICE_ATTR_RO(bar_info);

static struct attribute *ucie_dummy_attrs[] = {
	&dev_attr_mapping_control.attr,
	&dev_attr_mapping_disable.attr,
	&dev_attr_bar_info.attr,
	NULL,
};

static const struct attribute_group ucie_dummy_attr_group = {
	.attrs = ucie_dummy_attrs,
};

/*
 * Probe and initialization
 */
static int ucie_dummy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ucie_dummy_host *host;
	struct ucie_dummy *ucie;
	struct pci_host_bridge *bridge;
	struct resource *res;
	int err;

	dev_info(dev, "UCIe dummy host driver probe\n");

	/* Allocate host bridge structure */
	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*host));
	if (!bridge)
		return -ENOMEM;

	host = pci_host_bridge_priv(bridge);
	ucie = &host->ucie;
	ucie->dev = dev;
	host->bridge = bridge;	/* Store bridge reference for cleanup */
	platform_set_drvdata(pdev, host);

	/* Get register base from reg property */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ucie->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ucie->base)) {
		dev_err(dev, "failed to map registers\n");
		return PTR_ERR(ucie->base);
	}
	dev_info(dev, "Controller registers mapped at 0x%llx\n", (u64)res->start);

	/*
	 * Parse PCI ranges to get the memory window start address for BAR0.
	 * This makes BAR0 sit within the valid PCI address space.
	 *
	 * ranges format: <type pci_addr_hi pci_addr_lo cpu_addr_hi cpu_addr_lo size_hi size_lo>
	 * We extract the CPU address (cpu_addr_hi/lo) from the first memory range.
	 */
	{
		const __be32 *ranges;
		int rlen;

		ranges = of_get_property(dev->of_node, "ranges", &rlen);
		if (ranges && rlen >= 28) {  /* Need at least 7 u32 values */
			u32 type = be32_to_cpup(ranges);

			/* Check if this is a memory space (0x02000000 or 0x03000000) */
			if ((type & 0x03000000) == 0x02000000) {
				u64 cpu_addr_hi = be32_to_cpup(ranges + 3);
				u64 cpu_addr_lo = be32_to_cpup(ranges + 4);

				ucie->bar0_phys = (cpu_addr_hi << 32) | cpu_addr_lo;
				dev_info(dev, "Using ranges start for BAR0: 0x%llx\n",
					 (u64)ucie->bar0_phys);
			} else {
				dev_warn(dev, "First range is not memory space, using reg for BAR0\n");
				ucie->bar0_phys = res->start;
			}
		} else {
			dev_warn(dev, "Failed to parse ranges, using reg address for BAR0\n");
			ucie->bar0_phys = res->start;
		}
	}

	ucie->bar0_size = UCIE_BAR0_SIZE;
	dev_info(dev, "BAR0 will be exposed at: 0x%llx size=0x%llx\n",
		 (u64)ucie->bar0_phys, (u64)ucie->bar0_size);

	/* Get optional clock */
	host->bus_clk = devm_clk_get_optional(dev, "ucie");
	if (IS_ERR(host->bus_clk))
		return PTR_ERR(host->bus_clk);

	err = clk_prepare_enable(host->bus_clk);
	if (err) {
		dev_err(dev, "failed to enable clock: %d\n", err);
		return err;
	}

	/* Enable runtime PM */
	pm_runtime_enable(dev);
	err = pm_runtime_get_sync(dev);
	if (err < 0) {
		dev_err(dev, "pm_runtime_get_sync failed: %d\n", err);
		goto err_pm_disable;
	}

	/* Initialize hardware */
	err = ucie_dummy_hw_init(ucie);
	if (err) {
		dev_err(dev, "hardware initialization failed: %d\n", err);
		goto err_pm_put;
	}

	/* Get interrupt (optional) */
	host->irq = platform_get_irq(pdev, 0);
	if (host->irq > 0) {
		err = devm_request_irq(dev, host->irq, ucie_dummy_irq,
				       IRQF_SHARED, "ucie-dummy", host);
		if (err) {
			dev_warn(dev, "failed to request IRQ %d: %d\n", host->irq, err);
			/* Continue without interrupt support */
		} else {
			dev_info(dev, "IRQ %d registered\n", host->irq);
		}
	}

	/*
	 * Auto-configure UCIe shared memory mapping.
	 *
	 * When a "shared-region" phandle is present in the DT node, programme
	 * the UCIe controller's src/dst address and size registers and enable
	 * MAPPING_EN so that accesses from both sides reach the same physical
	 * memory.  This replicates the manual:
	 *   echo 'PHYS PHYS SIZE' > mapping_control
	 * that would otherwise be required before the first transfer.
	 */
	{
		struct device_node *shm_np;
		u32 reg[4];

		shm_np = of_parse_phandle(dev->of_node, "shared-region", 0);
		if (shm_np) {
			if (of_property_read_u32_array(shm_np, "reg", reg,
						       ARRAY_SIZE(reg)) == 0) {
				phys_addr_t shm_phys =
					((phys_addr_t)reg[0] << 32) | reg[1];
				u32 shm_size = reg[3]; /* raw bytes, same unit user passes */

				ucie_write_reg(ucie, lower_32_bits(shm_phys),
					       UCIE_LOWER_SRC_ADDR);
				ucie_write_reg(ucie, upper_32_bits(shm_phys),
					       UCIE_UPPER_SRC_ADDR);
				ucie_write_reg(ucie, lower_32_bits(shm_phys),
					       UCIE_LOWER_DST_ADDR);
				ucie_write_reg(ucie, upper_32_bits(shm_phys),
					       UCIE_UPPER_DST_ADDR);
				ucie_write_reg(ucie, shm_size, UCIE_MAPPING_SIZE);
				/* Use host wrapper so mapping_active state is updated */
				ucie_host_write_reg(host,
						    MAPPING_FILE_EN | MAPPING_EN,
						    UCIE_MAPPING_EN);
				dev_info(dev,
					 "UCIe shared-mem mapping: phys=0x%llx size=0x%x\n",
					 (unsigned long long)shm_phys, shm_size);
			} else {
				dev_warn(dev,
					 "shared-region: failed to read reg property\n");
			}
			of_node_put(shm_np);
		}
	}

	/* Configure PCI host bridge */
	bridge->sysdata = host;
	bridge->ops = &ucie_dummy_ops;

	/* Register PCI host bridge */
	err = pci_host_probe(bridge);
	if (err) {
		dev_err(dev, "failed to register PCI host: %d\n", err);
		goto err_pm_put;
	}

	/* Create sysfs interface */
	err = sysfs_create_group(&dev->kobj, &ucie_dummy_attr_group);
	if (err) {
		dev_warn(dev, "failed to create sysfs group: %d\n", err);
		/* Continue without sysfs, not critical */
	}

	dev_info(dev, "UCIe dummy host driver initialized successfully\n");
	dev_info(dev, "Hardcoded VID:DID = 0x%04x:0x%04x\n",
		 UCIE_DUMMY_VENDOR_ID, UCIE_DUMMY_DEVICE_ID);
	dev_info(dev, "BAR0 exposed at 0x%llx for user space access\n",
		 (u64)ucie->bar0_phys);
	dev_info(dev, "User space access via: /sys/bus/pci/devices/*/resource0\n");
	dev_info(dev, "Use lspci -d %04x:%04x to find device\n",
		 UCIE_DUMMY_VENDOR_ID, UCIE_DUMMY_DEVICE_ID);

	return 0;

err_pm_put:
	pm_runtime_put(dev);
err_pm_disable:
	pm_runtime_disable(dev);
	clk_disable_unprepare(host->bus_clk);
	return err;
}

static int ucie_dummy_remove(struct platform_device *pdev)
{
	struct ucie_dummy_host *host = platform_get_drvdata(pdev);
	struct ucie_dummy *ucie = &host->ucie;
	struct pci_host_bridge *bridge = host->bridge;

	/* Remove sysfs interface */
	sysfs_remove_group(&ucie->dev->kobj, &ucie_dummy_attr_group);

	/* Remove PCI bus and devices */
	if (bridge && bridge->bus) {
		pci_stop_root_bus(bridge->bus);
		pci_remove_root_bus(bridge->bus);
		dev_info(ucie->dev, "PCI bus removed\n");
	}

	/* Disable and cleanup any active mapping (using host-specific function) */
	if (host->mapping_active) {
		dev_info(ucie->dev, "Cleaning up active mapping on remove\n");
		ucie_host_disable_mapping(host);
	}

	/* Final cleanup - disable mapping registers */
	ucie_write_reg(ucie, 0, UCIE_MAPPING_EN);
	ucie_write_reg(ucie, MAPPING_FILE_CLR, UCIE_MAPPING_CLR);

	pm_runtime_put(ucie->dev);
	pm_runtime_disable(ucie->dev);
	clk_disable_unprepare(host->bus_clk);

	dev_info(ucie->dev, "UCIe dummy host driver removed\n");
	return 0;
}

static const struct of_device_id ucie_dummy_of_match[] = {
	{ .compatible = "renesas,ucie-dummy-rcar", },
	{ .compatible = "renesas,r8a78000-ucie-dummy", },
	{},
};
MODULE_DEVICE_TABLE(of, ucie_dummy_of_match);

static struct platform_driver ucie_dummy_driver = {
	.driver = {
		.name = "ucie-dummy-rcar-host",
		.of_match_table = ucie_dummy_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = ucie_dummy_probe,
	.remove = ucie_dummy_remove,
};

module_platform_driver(ucie_dummy_driver);

MODULE_DESCRIPTION("UCIe Host mode driver for Renesas R-Car SoCs");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hau Vo");
