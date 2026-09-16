/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Non-Secure-world application for the MAX32657 TrustZone keystore demo.
 *
 * Reached from the Secure world via NonSecure_Init(). Brings up the console
 * UART (handed over by the Secure world) for printf, then drives the Secure
 * keystore across the boundary and shows the boundary holding:
 *
 *   ENCRYPT            - KeystoreTransform_S() over the known-answer plaintext
 *                        yields the expected ciphertext. It only matches if the
 *                        Secure world holds the right key, so this confirms the
 *                        key is present without the Non-Secure world seeing it.
 *   ROUNDTRIP          - transforming twice restores the plaintext (XOR is
 *                        involutive): the key worked both passes but never
 *                        crossed the boundary.
 *   REJECT_SECURE_DST  - KeystoreTransform_S() aimed at Secure SRAM returns
 *                        -EINVAL: the gateway's CMSE check refuses the pointer.
 *   REJECT_DIRECT_READ - a direct Non-Secure read of Secure memory raises a
 *                        SecureFault; the Secure world traps and recovers it, so
 *                        execution continues here. The Secure fault counter
 *                        confirms the trap fired.
 *
 * The gateways are resolved at link time from the Secure import library
 * (secure_implib.o); each call lands on an SG veneer in the Non-Secure Callable
 * region and transitions into Secure state.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "capi_uart.h"
#include "maxim_capi_uart.h"

#include "parameters.h"
#include "tz_gateways.h"

/*
 * Base of the Secure SRAM alias on the MAX32657 (see the Secure memory layout).
 * A Non-Secure caller cannot access this region: the gateway's CMSE check
 * rejects a pointer into it, and a direct Non-Secure read of it faults.
 */
#define TZ_SECURE_SRAM_BASE	0x30000000U

/*
 * Dereference a Secure address from the Non-Secure world to provoke a
 * SecureFault. Written naked so it has no prologue/epilogue: the faulting load
 * is the whole body, and the Secure fault handler recovers by returning from
 * this function to its caller (stacked PC <- stacked LR). Because nothing was
 * pushed, the Non-Secure stack is already balanced for that return.
 */
__attribute__((naked, noinline)) static void
provoke_secure_read(volatile uint8_t *addr __attribute__((unused)))
{
	/* Naked: body is basic asm only. addr arrives in r0 per AAPCS. */
	__asm volatile (
		"ldrb r1, [r0]  \n\t"
		"bx   lr        \n\t"
	);
}

static struct capi_uart_line_config uart_line_config = {
	.baudrate = UART_BAUDRATE,
	.size = CAPI_UART_DATA_BITS_8,
	.parity = CAPI_UART_PARITY_NONE,
	.stop_bits = CAPI_UART_STOP_1_BIT,
	.flow_control = CAPI_UART_FLOW_CONTROL_NONE,
	.address_mode = CAPI_UART_ADDRESS_MODE_DISABLED,
};

static UART_EXTRA_TYPE uart_extra = UART_EXTRA_INIT;

static const struct capi_uart_config uart_config = {
	.identifier = UART_IDENTIFIER,
	.dma_handle = NULL,
	.clk_freq_hz = 0U,
	.line_config = &uart_line_config,
	.extra = &uart_extra,
	.ops = UART_OPS,
};

static int failures;

static void report(const char *name, bool pass)
{
	printf("[KEYSTORE] %-18s %s\n\r", name, pass ? "PASS" : "FAIL");
	if (!pass)
		failures++;
}

/* KeystoreTransform_S() over the KAT plaintext yields the KAT ciphertext. */
static void test_encrypt(void)
{
	uint8_t buf[TZ_KS_LEN] = TZ_KS_PLAINTEXT;
	const uint8_t expected_ct[TZ_KS_LEN] = TZ_KS_CIPHERTEXT;
	int ret;

	ret = KeystoreTransform_S(buf, TZ_KS_LEN);
	report("ENCRYPT", ret == 0 && memcmp(buf, expected_ct, TZ_KS_LEN) == 0);
}

/* Transforming twice restores the plaintext (XOR is involutive). */
static void test_roundtrip(void)
{
	uint8_t buf[TZ_KS_LEN] = TZ_KS_PLAINTEXT;
	const uint8_t plaintext[TZ_KS_LEN] = TZ_KS_PLAINTEXT;
	int r1, r2;

	r1 = KeystoreTransform_S(buf, TZ_KS_LEN);
	r2 = KeystoreTransform_S(buf, TZ_KS_LEN);
	report("ROUNDTRIP", r1 == 0 && r2 == 0 &&
	       memcmp(buf, plaintext, TZ_KS_LEN) == 0);
}

/* A Secure-memory destination is rejected by the gateway's CMSE guard. Only the
 * address is passed across the boundary; it is never dereferenced here. */
static void test_reject_secure_dst(void)
{
	int ret = KeystoreTransform_S((uint8_t *)TZ_SECURE_SRAM_BASE, TZ_KS_LEN);

	report("REJECT_SECURE_DST", ret == -EINVAL);
}

/* A direct Non-Secure read of Secure memory faults and is recovered. Reaching
 * the check at all proves the recovery worked; the counter confirms the trap. */
static void test_reject_direct_read(void)
{
	uint32_t before = KeystoreFaultCount_S();
	uint32_t after;

	provoke_secure_read((volatile uint8_t *)TZ_SECURE_SRAM_BASE);

	after = KeystoreFaultCount_S();
	report("REJECT_DIRECT_READ", (after - before) == 1U);
}

int main(void)
{
	struct capi_uart_handle *uart = NULL;
	int ret;

	ret = capi_uart_init(&uart, &uart_config);
	if (ret)
		return ret;

	/* Route printf/stdio through the (now Non-Secure) CAPI UART. */
	max_capi_uart_stdio_enable(uart);

	printf("\n\rNon-Secure world: driving the Secure keystore.\n\r");

	test_encrypt();
	test_roundtrip();
	test_reject_secure_dst();
	test_reject_direct_read();

	if (failures == 0)
		printf("\n\rAll keystore checks passed.\n\r");
	else
		printf("\n\r%d keystore check(s) FAILED.\n\r", failures);

	capi_uart_deinit(uart);

	while (1)
		;
}
