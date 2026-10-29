/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Maxim entry point for the MAX32657 TrustZone CAPI self-test.
 *
 * Reached from the Secure world via NonSecure_Init(). The test runner lives in
 * src/examples/selftest/selftest_nonsecure.c (example_main()), which is reused
 * unchanged by any platform's Non-Secure world. This file keeps only what is
 * genuinely platform-specific: the GPIO-driven IRQ hooks used by
 * test_irq.c / test_gpio.c, and initializing CAPI IRQ before the runner starts
 * (the console UART (use_irq) and the SPI/TIMER/I2C async paths deliver
 * through it; the Secure world has targeted these NVIC lines to Non-Secure).
 */

#include <stdbool.h>
#include <stdint.h>

#include "parameters.h"
#include "common_data.h"
#include "capi_irq.h"
#include "mxc_delay.h"
#include "mxc_sys.h"

extern int example_main(void);

#ifdef GPIO_OUTPUT_OPS
/*
 * GPIO-driven IRQ platform hooks used by test_irq.c / test_gpio.c. Identical to
 * the maxim capi_selftest platform main.c (which cannot be reused wholesale
 * because it also defines main()): the loopback input pin doubles as the
 * interrupt source. The pin comes from parameters.h (gpio_input_pin_numbers) so
 * moving the loopback pair needs no edit here.
 */
int platform_gpio_irq_arm(uint32_t *irq_line)
{
	mxc_gpio_cfg_t gpio_irq = {
		.port = MXC_GPIO0,
		.mask = (1U << gpio_input_pin_numbers[0]),
		.func = MXC_GPIO_FUNC_IN,
		.pad = MXC_GPIO_PAD_NONE,
		.vssel = MXC_GPIO_VSSEL_VDDIO,
	};

	if (MXC_GPIO_Config(&gpio_irq) != E_NO_ERROR)
		return -1;

	MXC_GPIO_IntConfig(&gpio_irq, MXC_GPIO_INT_RISING);
	MXC_GPIO_ClearFlags(gpio_irq.port, gpio_irq.mask);
	MXC_GPIO_EnableInt(gpio_irq.port, gpio_irq.mask);

	*irq_line = GPIO0_IRQn;
	return 0;
}

void platform_gpio_irq_disarm(void)
{
	MXC_GPIO_DisableInt(MXC_GPIO0, (1U << gpio_input_pin_numbers[0]));
}

bool platform_gpio_irq_ack(void)
{
	/* Clear the input pin's latched flag; harmless if already cleared. Also
	 * called from test-body context to swallow stale edges before re-enabling
	 * the NVIC line. */
	MXC_GPIO_ClearFlags(MXC_GPIO0, (1U << gpio_input_pin_numbers[0]));
	return true;
}
#endif /* GPIO_OUTPUT_OPS */

int main(void)
{
#if SPI_HAS_IRQ || TIMER_HAS_IRQ || I2C_HAS_IRQ
	if (capi_irq_init(&irq_config) == 0)
		(void)capi_irq_global_enable();
#endif

	return example_main();
}

