/***************************************************************************//**
 *   @file   maxim_capi_gpio.h
 *   @brief  Header file for GPIO functions with CAPI.
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#ifndef MAXIM_CAPI_GPIO_H_
#define MAXIM_CAPI_GPIO_H_

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

#include "gpio.h"
#include "capi_gpio.h"

/**
 * @def MAX_CAPI_GPIO0_NUM_PINS
 * @brief Number of usable pins on GPIO port 0 (P0.0 - P0.31).
 */
#define MAX_CAPI_GPIO0_NUM_PINS		32U

/**
 * @def MAX_CAPI_GPIO1_NUM_PINS
 * @brief Number of usable pins on GPIO port 1 (P1.0 - P1.9).
 */
#define MAX_CAPI_GPIO1_NUM_PINS		10U

/**
 * @def MAX_CAPI_GPIO2_NUM_PINS
 * @brief Number of usable pins on GPIO port 2 (P2.0 - P2.7, low-power domain).
 */
#define MAX_CAPI_GPIO2_NUM_PINS		8U

/**
 * @def MAX_CAPI_GPIO3_NUM_PINS
 * @brief Number of usable pins on GPIO port 3.
 *
 * On the MAX32655, port 3 is not a regular GPIO block: it has only P3.0 and
 * P3.1, its state lives in the Misc Control (MXC_MCR) registers, and it only
 * supports the VDDIOH supply. See the port == MXC_GPIO3 branches in
 * gpio_me17.c.
 */
#define MAX_CAPI_GPIO3_NUM_PINS		2U

/**
 * @enum max_capi_gpio_vssel
 * @brief GPIO voltage selection
 */
enum max_capi_gpio_vssel {
	/** Standard voltage level */
	MAX_CAPI_GPIO_VSSEL_VDDIO = MXC_GPIO_VSSEL_VDDIO,
	/** High voltage level */
	MAX_CAPI_GPIO_VSSEL_VDDIOH = MXC_GPIO_VSSEL_VDDIOH,
};

/**
 * @enum max_capi_gpio_func
 * @brief GPIO function selection
 */
enum max_capi_gpio_func {
	/** Input */
	MAX_CAPI_GPIO_FUNC_IN = MXC_GPIO_FUNC_IN,
	/** Output */
	MAX_CAPI_GPIO_FUNC_OUT = MXC_GPIO_FUNC_OUT,
	/** Alternate Function 1 */
	MAX_CAPI_GPIO_FUNC_ALT_1 = MXC_GPIO_FUNC_ALT1,
	/** Alternate Function 2 */
	MAX_CAPI_GPIO_FUNC_ALT_2 = MXC_GPIO_FUNC_ALT2,
	/** Alternate Function 3 */
	MAX_CAPI_GPIO_FUNC_ALT_3 = MXC_GPIO_FUNC_ALT3,
	/** Alternate Function 4 */
	MAX_CAPI_GPIO_FUNC_ALT_4 = MXC_GPIO_FUNC_ALT4,
};

/**
 * @enum max_capi_gpio_pad
 * @brief GPIO terminating resistor setting
 */
enum max_capi_gpio_pad {
	/** No termination */
	MAX_CAPI_GPIO_PAD_NONE = MXC_GPIO_PAD_NONE,
	/** Weak pull-up on the pad */
	MAX_CAPI_GPIO_PAD_PULL_UP = MXC_GPIO_PAD_WEAK_PULL_UP,
	/** Weak pull-down on the pad */
	MAX_CAPI_GPIO_PAD_PULL_DOWN = MXC_GPIO_PAD_WEAK_PULL_DOWN,
	/** Strong pull-up on the pad */
	MAX_CAPI_GPIO_PAD_STRONG_PULL_UP = MXC_GPIO_PAD_PULL_UP,
	/** Strong pull-down on the pad */
	MAX_CAPI_GPIO_PAD_STRONG_PULL_DOWN = MXC_GPIO_PAD_PULL_DOWN,
};

/**
 * @enum max_capi_gpio_drvstr
 * @brief GPIO drive strength
 */
enum max_capi_gpio_drvstr {
	/** Drive strength 0 */
	MAX_CAPI_GPIO_DRVSTR_0 = MXC_GPIO_DRVSTR_0,
	/** Drive strength 1 */
	MAX_CAPI_GPIO_DRVSTR_1 = MXC_GPIO_DRVSTR_1,
	/** Drive strength 2 */
	MAX_CAPI_GPIO_DRVSTR_2 = MXC_GPIO_DRVSTR_2,
	/** Drive strength 3 */
	MAX_CAPI_GPIO_DRVSTR_3 = MXC_GPIO_DRVSTR_3,
};

/**
 * @struct max_capi_gpio_extra_config
 * @brief MAX32655 platform-specific GPIO extra configuration
 */
struct max_capi_gpio_extra_config {
	/** Voltage selection */
	enum max_capi_gpio_vssel vssel;
	/** Function */
	enum max_capi_gpio_func func;
	/** Pad resistor termination */
	enum max_capi_gpio_pad pad;
	/** Output drive strength (ignored on port 3) */
	enum max_capi_gpio_drvstr drvstr;
	/** Logical pin 0 physical offset within port */
	uint8_t pin_offset;
};

extern const struct capi_gpio_ops max_capi_gpio_ops;

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* MAXIM_CAPI_GPIO_H_ */
