/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Non-Secure-world runner for the MAX32657 TrustZone CAPI self-test.
 *
 * Reached from the Secure world via NonSecure_Init(). This runner is a
 * **superset** of the maxim capi_selftest: its Non-Secure world runs the full
 * capi_loopback group set (GPIO, IRQ, SPI, TIMER, I2C, UART, DMA) reused
 * verbatim from projects/capi_selftest, plus two TrustZone-specific groups:
 *
 *   TRUSTZONE    - secure-gateway tests (test_trustzone.c, new here).
 *   DMA_INSTANCE - reports which DMA controller this world drives (DMA0 in the
 *                  Non-Secure world), run just before the DMA transfers.
 *
 * The shared groups, framework, common_data and parameters are compiled
 * straight out of projects/capi_selftest (see this project's CMakeLists.txt);
 * only this runner, the GPIO-IRQ platform hooks below, and the two TrustZone
 * groups are project-specific. The console UART, GPIO0, SPI, I3C ("I2C") and
 * TMR0 - and their interrupt lines - are handed to the Non-Secure world by the
 * Secure world (see src/secure/main.c) before this runs. Loopback wiring is
 * identical to the maxim capi_selftest (see projects/capi_selftest parameters).
 */

#include <stdbool.h>
#include <stdint.h>

#include "parameters.h"
#include "common_data.h"
#include "test_framework.h"
#include "capi_uart.h"
#include "capi_irq.h"
#include "mxc_delay.h"
#include "mxc_sys.h"

#include "tests/test_gpio.h"
#include "tests/test_irq.h"
#include "tests/test_spi.h"
#include "tests/test_timer.h"
#include "tests/test_i2c.h"
#include "tests/test_uart.h"
#include "tests/test_dma.h"
#include "tests/test_dma_instance.h"
#include "tests/test_trustzone.h"

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

/** @brief Signature of a top-level test group. */
typedef int (*test_func_t)(void);

/** @brief A top-level test group in the run. */
struct test_entry {
	const char *name;
	test_func_t func;
};

static const struct test_entry tests[] = {
	{ "TRUSTZONE",    test_trustzone },
	{ "GPIO",         test_gpio },
	{ "IRQ",          test_irq },
	{ "SPI",          test_spi },
	{ "TIMER",        test_timer },
	{ "I2C",          test_i2c },
	{ "UART",         test_uart },
	{ "DMA_INSTANCE", test_dma_instance },
	{ "DMA",          test_dma },
};

#define NUM_TESTS	(sizeof(tests) / sizeof(tests[0]))

/**
 * @brief Run every group, remembering the first failure but not stopping early.
 * @return 0 if every group passed, first non-zero group error otherwise.
 */
static int run_all_tests(void)
{
	int first_error = 0;

	for (unsigned int i = 0U; i < NUM_TESTS; i++) {
		int ret = tests[i].func();

		if (ret != 0 && first_error == 0)
			first_error = ret;
	}

	return first_error;
}

int main(void)
{
	struct capi_uart_handle *uart = NULL;
	struct test_framework_config framework_config;
	int first_error = 0;
	int ret;

#if SPI_HAS_IRQ || TIMER_HAS_IRQ || I2C_HAS_IRQ
	/* The console UART (use_irq) and the SPI/TIMER/I2C async paths deliver
	 * through CAPI IRQ, so it must be initialized before any driver connects a
	 * handler. The Secure world has targeted these NVIC lines to Non-Secure. */
	if (capi_irq_init(&irq_config) == 0)
		(void)capi_irq_global_enable();
#endif

	ret = capi_uart_init(&uart, &uart_config);
	if (ret != 0)
		return ret;

	get_test_framework_config(&framework_config, uart);
	ret = test_framework_init(&framework_config);
	if (ret != 0) {
		capi_uart_deinit(uart);
		return ret;
	}

	TEST_RUN_START();
	first_error = run_all_tests();
	TEST_RUN_END();

	test_framework_remove();
	ret = capi_uart_deinit(uart);
	if (first_error != 0)
		return first_error;

	return ret;
}
