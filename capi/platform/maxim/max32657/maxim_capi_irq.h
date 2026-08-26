/***************************************************************************//**
 *   @file   maxim_capi_irq.h
 *   @brief  Header file for IRQ functions with CAPI.
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#ifndef MAXIM_CAPI_IRQ_H_
#define MAXIM_CAPI_IRQ_H_

#include "uart.h"
#include "capi_gpio.h"
#include "capi_irq.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

struct max_capi_irq_entry {
	/** Callback function */
	capi_isr_callback_t callback;
	/** Callback argument */
	void *arg;
	/* IRQ enabled flag */
	bool enabled;
};

struct max_capi_irq_extra_config {
	uint32_t default_priority;
};

extern const struct capi_irq_ops max_capi_irq_ops;

/**
 * @brief Callback for DMA
 * @param ch DMA channel
 * @param reason Not used
 */
void max_capi_dma_callback(int ch, int reason);

/**
 * @brief Connect a GPIO pin to a callback
 * @param pin - the GPIO pin
 * @param isr - the callback function to connect to the IRQ
 * @param arg - the arguments to pass to the callback function
 * @return 0 on success, negative error code otherwise
 */
int max_capi_gpio_irq_connect(struct capi_gpio_pin *pin,
			      capi_isr_callback_t isr, void *arg);

/**
 * @brief Disconnect a GPIO from a callback
 * @param pin - the GPIO pin
 * @return 0 on success, negative error code otherwise
 */
int max_capi_gpio_irq_disconnect(const struct capi_gpio_pin *pin);

/**
 * @brief Enable an interrupt on a specific pin
 * @param pin - the GPIO pin
 * @return 0 on success, negative error code otherwise
 */
int max_capi_gpio_irq_enable(struct capi_gpio_pin *pin);

/**
 * @brief Disable an interrupt on a specific pin
 * @param pin - the GPIO pin
 * @return 0 on success, negative error code oherwise
 */
int max_capi_gpio_irq_disable(struct capi_gpio_pin *pin);

/**
 * @brief Set level/edge trigger for a specific pin
 * @param pin - the GPIO pin
 * @param trigger  - the trigger to set the pin to
 * @return 0 on success, negative error code otherwise
 */
int max_capi_gpio_irq_set_level_edge_trigger(struct capi_gpio_pin *pin,
		enum capi_irq_trig_level trigger);

/**
 * @brief Enable interrupts on all pins
 * @return 0
 */
int max_capi_gpio_irq_global_enable(void);

/**
 * @brief Disable interrupts on all pins
 * @return 0
 */
int max_capi_gpio_irq_global_disable(void);

#if defined(__cplusplus)
}
#endif /* __cplusplus*/

#endif /* MAXIM_CAPI_IRQ_H_ */
