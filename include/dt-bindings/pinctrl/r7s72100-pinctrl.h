/*
 * Defines macros and constants for Renesas RZ/A1 pin controller pin
 * muxing functions.
 */
#ifndef __DT_BINDINGS_PINCTRL_RENESAS_RZA1_H
#define __DT_BINDINGS_PINCTRL_RENESAS_RZA1_H

#define RZA1_PINS_PER_PORT	16

/* Create the pin index from its bank and position numbers */
#define PIN(b, p)		((b) * RZA1_PINS_PER_PORT + (p))

/*
 * Flags to apply to alternate function configuration
 * All of the following are mutually exclusive.
 */

/*
 * Pin is bi-directional.
 * An alternate function that needs both input/output functionalities shall
 * be configured as bidirectional.
 * Eg. SDA/SCL pins of an I2c interface.
 */
#define BI_DIR			(1 << 3)

/*
 * Flags used to ask software to drive the pin I/O direction overriding the
 * alternate function configuration.
 * Some alternate functions require software to force I/O direction of a pin,
 * overriding the designated one.
 * Refer to the HW manual to know when this flag shall be used.
 */
#define SWIO_IN			(1 << 4)
#define SWIO_OUT		(1 << 5)

#endif
