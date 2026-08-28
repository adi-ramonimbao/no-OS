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

#define INCORRECT_PERIOD_MS	1000

static volatile bool wdt_irq_fired;

void wdt_callback(int chan_id, void *arg, uint32_t extra_flags)
{
	wdt_irq_fired = true;
}

int example_main(void)
{
	int ret;

	/* IRQ setup */
	struct capi_irq_handle *irq_handle = NULL;
	struct capi_irq_config irq_config = {
		.irq_ctrl_id = 0,
	};
	ret = capi_irq_init(&irq_config);
	if (ret)
		return ret;

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
	bool irq_enabled = false;
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
		/* Interrupt mode: fire the late interrupt ~2.68 s after a feed
		 * (PCLK = 25 MHz) while the reset output stays disabled, so the
		 * callback runs without resetting the SoC.
		 */
		late_period = MAX_CAPI_WDT_PERIOD_2_26;
		irq_enabled = true;
	}

	struct max_capi_wdt_chan_extra wdt_chan_extra = {
		.mode = MAX_CAPI_WDT_MODE_COMPATIBILITY,
		.late_interrupt = late_period,
		/* Disable reset in interrupt mode by pinning it to the max period */
		.late_reset = system_was_reset ? MAX_CAPI_WDT_PERIOD_2_31 : late_period,
		.early_interrupt = early_period,
		.early_reset = early_period,
	};
	struct capi_wdt_chan_config wdt_chan_config = {
		.extra = &wdt_chan_extra,
		.irq_enabled = irq_enabled,
	};

	ret = capi_wdt_setup(wdt_handle, &wdt_chan_config);
	if (ret)
		return ret;

	if (!system_was_reset) {
		/* Reset-mode demo: start the WDT and stop feeding so it resets
		 * the SoC. The next boot detects the reset flag and switches to
		 * interrupt mode.
		 */
		printf("Reset mode: starting WDT, withholding feed "
		       "(expect a reset in ~2.6 ms)...\n\r");

		ret = capi_wdt_feed(wdt_handle);
		if (ret)
			return ret;

		capi_wait_ms(INCORRECT_PERIOD_MS);
		printf("ERROR: expected a WDT reset but none occurred\n\r");
	} else {
		/* Interrupt-mode demo: start the WDT and stop feeding so the
		 * late interrupt fires. Reset is disabled, so the callback runs
		 * without resetting.
		 */
		uint32_t waited = 0;

		printf("Interrupt mode: starting WDT, withholding feed "
		       "(expect the callback in ~2.7 s, no reset)...\n\r");

		ret = capi_wdt_feed(wdt_handle);
		if (ret)
			return ret;

		while (!wdt_irq_fired && waited < 5000) {
			capi_wait_ms(100);
			waited += 100;
		}

		if (wdt_irq_fired)
			printf("WDT interrupt confirmed after ~%u ms (no reset)\n\r",
			       waited);
		else
			printf("ERROR: WDT interrupt never fired\n\r");
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
