/***************************************************************************//**
 * @file main.c
 * @brief Main file for Maxim MAX32657 platform of capi_selftest project.
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 ******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include "parameters.h"
#include "common_data.h"
#include "mxc_delay.h"
#include "mxc_sys.h"

extern int example_main(void);

/**
 * @brief Platform hook: Arm (enable) GPIO IRQ for loopback test.
 * @param irq_line Pointer to store the CAPI IRQ line number.
 * @return 0 on success, -ENOTSUP if board has no GPIO-IRQ path.
 */
int platform_gpio_irq_arm(uint32_t *irq_line)
{
	mxc_gpio_cfg_t gpio_irq = {
		.port = MXC_GPIO0,
		.mask = MXC_GPIO_PIN_1,  /* P0.1 - input pin from parameters.h */
		.func = MXC_GPIO_FUNC_IN,
		.pad = MXC_GPIO_PAD_NONE,
		.vssel = MXC_GPIO_VSSEL_VDDIO,
	};

	/* Configure pin as input */
	if (MXC_GPIO_Config(&gpio_irq) != E_NO_ERROR)
		return -1;

	/*
	 * Configure interrupt on rising edge.
	 * The test has already registered its callback via capi_irq_register(GPIO0_IRQn, ...),
	 * so we just need to configure the hardware to trigger that IRQ line.
	 */
	MXC_GPIO_IntConfig(&gpio_irq, MXC_GPIO_INT_RISING);
	MXC_GPIO_ClearFlags(gpio_irq.port, gpio_irq.mask);
	MXC_GPIO_EnableInt(gpio_irq.port, gpio_irq.mask);

	/* Return GPIO0 IRQ line number for the test to register with CAPI IRQ */
	*irq_line = GPIO0_IRQn;
	return 0;
}

/**
 * @brief Platform hook: Disarm (disable) GPIO IRQ for loopback test.
 */
void platform_gpio_irq_disarm(void)
{
	/* Disable interrupt on P0.1 */
	MXC_GPIO_DisableInt(MXC_GPIO0, MXC_GPIO_PIN_1);
}

/**
 * @brief Platform hook: Acknowledge GPIO IRQ for loopback test.
 * @return true if the input pin was the source and was cleared.
 *
 * Note: By the time this is called from the IRQ callback, MXC_GPIO_Handler()
 * has already cleared the interrupt flags. Since P0.1 is the only pin configured
 * with an interrupt in this test, we can safely assume it was the source if
 * the GPIO0 IRQ fired.
 */
bool platform_gpio_irq_ack(void)
{
	/*
	 * Always return true because:
	 * 1. P0.1 is the only pin with interrupt enabled in this test
	 * 2. MXC_GPIO_Handler() already cleared the flags before this is called
	 * 3. If GPIO0_IRQn fired, it must have been P0.1
	 */
	return true;
}

/**
 * @brief Main function execution for MAX32657 platform.
 * @return Result of the enabled example execution.
 */
int main(void)
{
#if SPI_HAS_IRQ || TIMER_HAS_IRQ || I2C_HAS_IRQ
	if (capi_irq_init(&irq_config) == 0)
		(void)capi_irq_global_enable();
#endif

	return example_main();
}
