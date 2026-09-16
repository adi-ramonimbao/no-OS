/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Non-Secure-world runner for the MAX32657 TrustZone CAPI self-test.
 *
 * Reached from the Secure world via NonSecure_Init(). Brings up the console
 * UART (handed over by the Secure world) as the test report transport, then
 * runs the shared capi_selftest framework over two groups:
 *
 *   TRUSTZONE - secure-gateway tests (test_trustzone.c, new here).
 *   DMA       - memory-to-memory transfers (test_dma.c, reused verbatim from
 *               projects/capi_selftest); from the Non-Secure world these run on
 *               the hardwired-Non-Secure DMA0 instance.
 *
 * The framework, common_data and test_dma are compiled straight out of the
 * capi_selftest project (see this project's CMakeLists.txt) - only this runner
 * and the TrustZone group are project-specific.
 */

#include <stdbool.h>

#include "capi_uart.h"
#include "common_data.h"
#include "test_framework.h"
#include "tests/test_dma.h"
#include "tests/test_trustzone.h"

/** @brief Signature of a top-level test group. */
typedef int (*test_func_t)(void);

/** @brief A top-level test group in the run. */
struct test_entry {
	const char *name;
	test_func_t func;
};

static const struct test_entry tests[] = {
	{ "TRUSTZONE", test_trustzone },
	{ "DMA",       test_dma },
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
