/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Non-Secure-world consumer for the MAX32657 TrustZone split demo.
 *
 * Built on its own (Non-Secure-only) and merged with a Secure image produced
 * elsewhere (max32657_tz_secure_only). Reached from the Secure world via
 * NonSecure_Init(), it runs an ordinary no-OS CAPI application on the delegated
 * peripherals: prints over the CAPI UART, then drives the Secure keystore
 * across the boundary and shows the boundary holding:
 *
 *   ENCRYPT            - KeystoreTransform_S() over the known-answer plaintext
 *                        yields the expected ciphertext, proving the Secure
 *                        world holds the right key without ever exposing it.
 *   ROUNDTRIP          - transforming twice restores the plaintext (XOR is
 *                        involutive): the key worked but never crossed over.
 *   REJECT_SECURE_DST  - KeystoreTransform_S() aimed at Secure SRAM returns
 *                        -EINVAL: the gateway's CMSE check refuses the pointer.
 *   REJECT_DIRECT_READ - a direct Non-Secure read of Secure memory faults; the
 *                        Secure world traps and recovers it, so execution
 *                        continues here and the fault counter confirms the trap.
 *
 * It then blinks the board LED through CAPI GPIO as a heartbeat. The gateways
 * are resolved at link time from the Secure import library (SECURE_IMPLIB);
 * each call lands on an SG veneer in the Non-Secure Callable region and
 * transitions into Secure state.
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
#include "maxim_capi_uart.h"

#include "parameters.h"

/* Secure gateways, resolved at link time from the Secure import library. The
 * key they use lives only in the Secure world; the Non-Secure side knows only
 * the known-answer vector below. */
extern int KeystoreTransform_S(uint8_t *buf_ns, size_t len);
extern uint32_t KeystoreFaultCount_S(void);

/* Known-answer test vector: plaintext and the ciphertext the Secure key is
 * expected to produce. Matching it proves the Secure world holds the right key
 * without the key ever crossing the boundary. Regenerate if the Secure key in
 * max32657_tz_secure_only changes. */
#define TZ_KS_LEN		9U
#define TZ_KS_PLAINTEXT		{ 0x54, 0x72, 0x75, 0x73, 0x74, 0x5A, 0x6F, 0x6E, 0x65 }
#define TZ_KS_CIPHERTEXT	{ 0xF1, 0x28, 0x49, 0xB0, 0xD1, 0x00, 0x53, 0xAD, 0xC0 }

/* Base of the Secure SRAM alias; a Non-Secure caller cannot access it. */
#define TZ_SECURE_SRAM_BASE	0x30000000U

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

static LED_EXTRA_TYPE led_extra = LED_EXTRA_INIT;

static const struct capi_gpio_port_config led_port_config = {
	.ops = LED_OPS,
	.identifier = LED_IDENTIFIER,
	.num_pins = LED_NUM_PINS,
	.flags = NULL,
	.extra = &led_extra,
};

static int failures;

static void report(const char *name, bool pass)
{
	printf("[KEYSTORE] %-18s %s\n\r", name, pass ? "PASS" : "FAIL");
	if (!pass)
		failures++;
}

/* KeystoreTransform_S() over the KAT plaintext yields the KAT ciphertext: the
 * Secure world holds the right key, proven without the key crossing over. */
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

int main(void)
{
	struct capi_uart_handle *uart = NULL;
	struct capi_gpio_port_handle *led_port = NULL;
	struct capi_gpio_pin led;
	int ret;

	ret = capi_uart_init(&uart, &uart_config);
	if (ret)
		return ret;

	/* Route printf/stdio through the (now Non-Secure) CAPI UART. */
	max_capi_uart_stdio_enable(uart);

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

	test_encrypt();
	test_roundtrip();
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
