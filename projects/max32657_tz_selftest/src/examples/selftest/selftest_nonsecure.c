/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   selftest_nonsecure.c
 * @brief  Portable Non-Secure test runner for the TrustZone CAPI self-test.
 *
 * Reached from the Secure world via NonSecure_Init(). This runner is a
 * **superset** of the maxim capi_selftest: its Non-Secure world runs the full
 * capi_loopback group set (GPIO, IRQ, SPI, TIMER, I2C, UART, DMA) reused
 * verbatim from projects/capi_selftest, plus two TrustZone-specific groups:
 *
 *   TRUSTZONE    - secure-gateway tests (examples/selftest/tests/test_trustzone.c).
 *   DMA_INSTANCE - reports which DMA controller this world drives (DMA0 in the
 *                  Non-Secure world), run just before the DMA transfers.
 *
 * Uses only CAPI, the test framework and parameters.h, so it is reused
 * unchanged by any platform's Non-Secure world; platform-only concerns (the
 * GPIO-IRQ hooks, and initializing CAPI IRQ before this runs) stay in
 * platform/<platform>/nonsecure/main.c.
 */

#include <stdbool.h>
#include <stdint.h>

#include "parameters.h"
#include "common_data.h"
#include "test_framework.h"
#include "capi_uart.h"

#include "tests/test_gpio.h"
#include "tests/test_irq.h"
#include "tests/test_spi.h"
#include "tests/test_timer.h"
#include "tests/test_i2c.h"
#include "tests/test_uart.h"
#include "tests/test_dma.h"
#include "tests/test_dma_instance.h"
#include "tests/test_trustzone.h"

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

int example_main(void)
{
	struct capi_uart_handle *uart = NULL;
	struct test_framework_config framework_config;
	int first_error = 0;
	int ret;

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
