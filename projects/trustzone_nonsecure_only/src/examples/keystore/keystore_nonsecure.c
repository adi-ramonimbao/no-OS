/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   keystore_nonsecure.c
 * @brief  Portable Non-Secure application for the TrustZone split demo.
 *
 * An ordinary no-OS CAPI application on the delegated peripherals: prints
 * over the CAPI UART, then drives the Secure keystore across the boundary and
 * shows the boundary holding, before blinking the board LED as a heartbeat.
 * Uses only CAPI plus parameters.h, so it is reused unchanged by any
 * platform's Non-Secure world (called from platform/<platform>/nonsecure/main.c).
 *
 *   SELFTEST           - KeystoreSelfTest_S() has the Secure world check its key
 *                        against a Secure-held known-answer and return only
 *                        pass/fail, so the Non-Secure side holds no answer.
 *   ROUNDTRIP          - transforming twice restores the input (XOR is
 *                        involutive): the key worked but never crossed over.
 *   NON_IDENTITY       - one transform changes the data, proving a key is
 *                        applied, without the Non-Secure side knowing the output.
 *   REJECT_SECURE_DST  - KeystoreTransform_S() aimed at Secure SRAM returns
 *                        -EINVAL: the gateway's CMSE check refuses the pointer.
 *   REJECT_DIRECT_READ - a direct Non-Secure read of Secure memory faults; the
 *                        Secure world traps and recovers it, so execution
 *                        continues here and the fault counter confirms the trap.
 *
 * The gateways are resolved at link time from the Secure import library
 * (SECURE_IMPLIB); each call lands on an SG veneer in the Non-Secure Callable
 * region and transitions into Secure state.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "capi_uart.h"
#include "capi_gpio.h"
#include "capi_time.h"

#include "common_data.h"
#include "parameters.h"

/* Secure gateways, resolved at link time from the Secure import library. The
 * key they use lives only in the Secure world, and so does the known-answer:
 * KeystoreSelfTest_S() verifies the key inside Secure and returns only pass/fail,
 * so the Non-Secure side holds no reference answer. */
extern int KeystoreTransform_S(uint8_t *buf_ns, size_t len);
extern int KeystoreSelfTest_S(void);
extern uint32_t KeystoreFaultCount_S(void);

/* Test input - just some bytes to transform. There is deliberately NO expected
 * ciphertext here; key correctness is proven by KeystoreSelfTest_S() and by
 * transform properties (round-trip, non-identity). */
#define TZ_KS_LEN		9U
#define TZ_KS_INPUT		{ 0x54, 0x72, 0x75, 0x73, 0x74, 0x5A, 0x6F, 0x6E, 0x65 }

/* Provoke a SecureFault by reading Secure memory from the Non-Secure world.
 * Naked so it has no prologue/epilogue: the faulting load is the whole body and
 * the Secure fault handler recovers by returning to the caller (PC <- LR). */
__attribute__((naked, noinline)) static void
provoke_secure_read(volatile uint8_t *addr __attribute__((unused)))
{
	__asm volatile (
		"ldrb r1, [r0]  \n\t"
		"bx   lr        \n\t"
	);
}

static int failures;

static void report(const char *name, bool pass)
{
	printf("[KEYSTORE] %-18s %s\n\r", name, pass ? "PASS" : "FAIL");
	if (!pass)
		failures++;
}

/* Ask the Secure world to verify its own key against a Secure-held known-answer.
 * Only the pass/fail result crosses the boundary; no answer lives here. */
static void test_selftest(void)
{
	report("SELFTEST", KeystoreSelfTest_S() == 0);
}

/* Transforming twice restores the input (XOR is involutive) - a property check
 * that needs no expected ciphertext. */
static void test_roundtrip(void)
{
	uint8_t buf[TZ_KS_LEN] = TZ_KS_INPUT;
	const uint8_t input[TZ_KS_LEN] = TZ_KS_INPUT;
	int r1, r2;

	r1 = KeystoreTransform_S(buf, TZ_KS_LEN);
	r2 = KeystoreTransform_S(buf, TZ_KS_LEN);
	report("ROUNDTRIP", r1 == 0 && r2 == 0 &&
	       memcmp(buf, input, TZ_KS_LEN) == 0);
}

/* One transform changes the data, proving a non-trivial key is applied, without
 * the Non-Secure side needing to know the output. */
static void test_non_identity(void)
{
	uint8_t buf[TZ_KS_LEN] = TZ_KS_INPUT;
	const uint8_t input[TZ_KS_LEN] = TZ_KS_INPUT;
	int ret;

	ret = KeystoreTransform_S(buf, TZ_KS_LEN);
	report("NON_IDENTITY", ret == 0 && memcmp(buf, input, TZ_KS_LEN) != 0);
}

/* A Secure-memory destination is rejected by the gateway's CMSE guard; only the
 * address crosses the boundary, it is never dereferenced here. */
static void test_reject_secure_dst(void)
{
	int ret = KeystoreTransform_S((uint8_t *)TZ_SECURE_SRAM_BASE, TZ_KS_LEN);

	report("REJECT_SECURE_DST", ret == -EINVAL);
}

/* A direct Non-Secure read of Secure memory faults and is recovered; reaching
 * the check proves recovery, the counter confirms the trap fired. */
static void test_reject_direct_read(void)
{
	uint32_t before = KeystoreFaultCount_S();
	uint32_t after;

	provoke_secure_read((volatile uint8_t *)TZ_SECURE_SRAM_BASE);

	after = KeystoreFaultCount_S();
	report("REJECT_DIRECT_READ", (after - before) == 1U);
}

int example_main(void)
{
	struct capi_uart_handle *uart = NULL;
	struct capi_gpio_port_handle *led_port = NULL;
	struct capi_gpio_pin led;
	int ret;

	ret = capi_uart_init(&uart, &uart_config);
	if (ret)
		return ret;

	/* Route printf/stdio through the (now Non-Secure) CAPI UART. Resolved
	 * per platform via parameters.h. */
	CAPI_UART_STDIO_ENABLE(uart);

	ret = capi_gpio_port_init(&led_port, &led_port_config);
	if (ret)
		return ret;

	led.port_handle = led_port;
	led.number = LED_PIN_NUMBER;
	led.flags = CAPI_GPIO_ACTIVE_HIGH;

	ret = capi_gpio_pin_set_direction(&led, CAPI_GPIO_OUTPUT);
	if (ret)
		return ret;

	printf("Hello from the Non-Secure world (no-OS CAPI)!\n\r");
	printf("Driving the Secure keystore across the boundary.\n\r");

	test_selftest();
	test_roundtrip();
	test_non_identity();
	test_reject_secure_dst();
	test_reject_direct_read();

	if (failures == 0)
		printf("\n\rAll keystore checks passed.\n\r");
	else
		printf("\n\r%d keystore check(s) FAILED.\n\r", failures);

	/* Heartbeat: blink the delegated LED so the board shows liveness. */
	while (1) {
		capi_gpio_pin_toggle(&led);
		capi_wait_ms(500);
	}
}
