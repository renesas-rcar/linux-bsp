#ifndef __WATCHDOG_PRETIMEOUT_H
#define __WATCHDOG_PRETIMEOUT_H

#define WATCHDOG_GOV_NAME_LEN	20

struct module;
struct watchdog_device;

struct watchdog_governor {
	const char		name[WATCHDOG_GOV_NAME_LEN];
	void			(*pretimeout)(struct watchdog_device *wdd);
	struct module		*owner;
};

/* Interfaces to watchdog pretimeout governors */
int watchdog_register_governor(struct watchdog_governor *gov);
void watchdog_unregister_governor(struct watchdog_governor *gov);

/* Interfaces to watchdog_core.c */
#ifdef CONFIG_WATCHDOG_PRETIMEOUT_GOV

#if IS_ENABLED(CONFIG_WATCHDOG_PRETIMEOUT_DEFAULT_GOV_PANIC)
#define WATCHDOG_PRETIMEOUT_DEFAULT_GOV	"panic"
#elif IS_ENABLED(CONFIG_WATCHDOG_PRETIMEOUT_DEFAULT_GOV_NOOP)
#define WATCHDOG_PRETIMEOUT_DEFAULT_GOV	"noop"
#elif IS_ENABLED(CONFIG_WATCHDOG_PRETIMEOUT_DEFAULT_GOV_USERSPACE)
#define WATCHDOG_PRETIMEOUT_DEFAULT_GOV	"userspace"
#else
#error "Default watchdog pretimeout governor is not set."
#endif

int watchdog_register_pretimeout(struct watchdog_device *wdd, struct device *dev);
void watchdog_unregister_pretimeout(struct watchdog_device *wdd);
#else
static inline int watchdog_register_pretimeout(struct watchdog_device *wdd,
					       struct device *dev)
{
	return 0;
}
static inline void watchdog_unregister_pretimeout(struct watchdog_device *wdd) {}
#endif

#endif
