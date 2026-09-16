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
 * peripherals: prints over the CAPI UART, blinks the board LED through CAPI
 * GPIO, and on every iteration calls back into the Secure world through the
 * gateway veneers.
 *
 * IncrementCount_S() / GetSecureMagic_S() are resolved at link time from the
 * Secure import library (passed as SECURE_IMPLIB); each call lands on an SG
 * veneer in the Non-Secure Callable region and transitions into Secure state.
 */

#include <stdio.h>
#include <stdint.h>

#include "capi_uart.h"
#include "capi_gpio.h"
#include "capi_time.h"
#include "maxim_capi_uart.h"

#include "parameters.h"

/* Secure gateways, resolved from the Secure import library. */
extern int IncrementCount_S(volatile int *count_ns);
extern uint32_t GetSecureMagic_S(void);

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

int main(void)
{
	struct capi_uart_handle *uart = NULL;
	struct capi_gpio_port_handle *led_port = NULL;
	struct capi_gpio_pin led;
	volatile int count = 0;
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
	/* Read a Secure-owned constant across the boundary (S -> NS return path). */
	printf("Secure magic = 0x%08lX\n\r", (unsigned long)GetSecureMagic_S());

	while (1) {
		capi_gpio_pin_toggle(&led);
		capi_wait_ms(500);

		/* Advance the counter inside the Secure world via the veneer. */
		ret = IncrementCount_S(&count);
		if (ret)
			printf("IncrementCount_S failed: %d\n\r", ret);
		else
			printf("count = %d\n\r", count);
	}
}
