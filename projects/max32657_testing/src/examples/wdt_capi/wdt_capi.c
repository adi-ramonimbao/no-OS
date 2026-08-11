/***************************************************************************//**
 *   @file   timer_capi.c
 *   @brief  Timer example using CAPI for MAX32657
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES, INC. “AS IS” AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL ANALOG DEVICES, INC. BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "capi_wdt.h"
#include "capi_uart.h"
#include "maxim_capi_wdt.h"
#include "maxim_capi_uart.h"
#include "wdt.h"  // For MXC_WDT register access

#define CORRECT_PERIOD_MS	30000
#define INCORRECT_PERIOD_MS	 1000
#define CHOSEN_PERIOD	CORRECT_PERIOD_MS

void wdt_callback(int chan_id, void *arg, uint32_t extra_flags)
{
	printf("Watchdog triggered!\n\r");
}

int example_main(void)
{
	int ret;
	uint8_t count = 0;

	/* UART setup */
	struct capi_uart_handle *uart_handle = NULL;
	struct max_capi_uart_extra uart_extra = {
		.vssel = MAX_CAPI_GPIO_VSSEL_VDDIOH,
	};
	struct capi_uart_line_config uart_line_config = {
		.baudrate = 115200,
		.size = CAPI_UART_DATA_BITS_8,
		.parity = CAPI_UART_PARITY_NONE,
		.stop_bits = CAPI_UART_STOP_1_BIT,
	};
	struct capi_uart_config uart_config = {
		.identifier = 0, /* UART0 */
		.line_config = &uart_line_config,
		.ops = &max_capi_uart_ops,
		.extra = &uart_extra,
	};

	ret = capi_uart_init(&uart_handle, &uart_config);
	if (ret)
		return ret;
	max_capi_uart_stdio_enable(uart_handle);

	/* WDT setup */
	struct capi_wdt_handle *wdt_handle = NULL;
	struct max_capi_wdt_extra wdt_extra = {
		.clock_source = MAX_CAPI_WDT_CLOCK_PCLK,
	};
	struct capi_wdt_config wdt_config = {
		.identifier = 0,
		.extra = &wdt_extra,
		.ops = &max_capi_wdt_ops,
		.callback = wdt_callback,
	};

	ret = capi_wdt_init(&wdt_handle, &wdt_config);
	if (ret)
		return ret;

	uint32_t flags = 0;
	bool system_was_reset = false;
	bool irq_mode = false;
	enum max_capi_wdt_period late_period = MAX_CAPI_WDT_PERIOD_2_16;
	enum max_capi_wdt_period early_period = MAX_CAPI_WDT_PERIOD_2_16;
	ret = max_capi_wdt_get_flags(wdt_handle, &flags);
	if (ret)
		return ret;

	if (flags & (MAX_CAPI_WDT_FLAG_RST_LATE | MAX_CAPI_WDT_FLAG_RST_EARLY)) {
		printf("System was reset!\n\r");
		system_was_reset = true;
		max_capi_wdt_clear_flags(wdt_handle);
	} else {
		printf("No WDT reset detected\n\r");
	}

	printf("Starting watchdog\n\r");

	if (system_was_reset) {
		/* With PCLK = 25 MHz:
		 * Interrupt periods: 2^29=21.5s (early), 2^31=85.9s (late)
		 * Reset periods: Disable by setting to maximum (2^31)
		 * This allows interrupts to fire without causing resets
		 */
		late_period = MAX_CAPI_WDT_PERIOD_2_31;
		early_period = MAX_CAPI_WDT_PERIOD_2_29;
		irq_mode = true;
	}

	struct max_capi_wdt_chan_extra wdt_chan_extra = {
		.mode = system_was_reset ? MAX_CAPI_WDT_MODE_WINDOWED : MAX_CAPI_WDT_MODE_COMPATIBILITY,
		.late_interrupt = late_period,
		.late_reset = system_was_reset ? MAX_CAPI_WDT_PERIOD_2_31 : late_period,  // Disable reset: set to max
		.early_interrupt = early_period,
		.early_reset = system_was_reset ? MAX_CAPI_WDT_PERIOD_2_31 : early_period, // Disable reset: set to max
	};
	struct capi_wdt_chan_config wdt_chan_config = {
		.extra = &wdt_chan_extra,
		.irq_mode = irq_mode,
	};
	ret = capi_wdt_setup(wdt_handle, &wdt_chan_config);
	if (ret)
		return ret;

	ret = capi_wdt_feed(wdt_handle);
	if (ret)
		return ret;

	uint32_t delay_ms = system_was_reset ? CHOSEN_PERIOD : INCORRECT_PERIOD_MS;
	printf("[t=0s] Initial feed, waiting %u ms...\n\r", delay_ms);
	capi_wait_ms(delay_ms);

	while (1) {
		ret = capi_wdt_feed(wdt_handle);
		if (ret)
			return ret;
		count++;
		printf("[t=%us] Feed #%d\n\r", count * (delay_ms / 1000), count);

		if (count >= 3)
			break;

		printf("Waiting %u ms...\n\r", delay_ms);
		capi_wait_ms(delay_ms);
	}

	printf("Deinitializing WDT...\n\r");
	ret = capi_wdt_deinit(wdt_handle);
	if (ret)
		return ret;

	printf("Deinitializing UART...\n\r");
	ret = capi_uart_deinit(uart_handle);
	if (ret)
		return ret;

	while (1);

	return 0;
}
