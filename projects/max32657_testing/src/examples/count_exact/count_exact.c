/***************************************************************************//**
 *   @file   tmr0.c
 *   @brief  TMR0 testing for MAX32657 CAPI
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
#include "dma.h"
#include "capi_dma.h"
#include "capi_irq.h"
#include "capi_timer.h"
#include "capi_uart.h"
#include "maxim_capi_dma.h"
#include "maxim_capi_irq.h"
#include "maxim_capi_timer.h"
#include "maxim_capi_uart.h"

#define PWM_STEP 1

#define TIMER_IRQ_PERIOD_US		1000U
#define TIMER_IRQ_EXPECTED_COUNT	200U

static volatile unsigned int cb_count = 0;
static volatile uint32_t cb_event;
static volatile int cb_extra;

// void tmr_callback(uint32_t event, uint32_t chan, void *arg, int event_extra) {
void tmr_callback(uint32_t event, void *arg, int event_extra) {
	cb_event = event;
	cb_extra = event_extra;
	cb_count++;
}

void empty_callback(uint32_t event, void *arg, int event_extra) {

}

int example_main(void)
{
	int ret;
	uint64_t uptime_start = 0U;
	uint64_t uptime_end = 0U;
	uint64_t period_ticks = 0U;

	const uint32_t window_us = TIMER_IRQ_PERIOD_US * TIMER_IRQ_EXPECTED_COUNT;

	/* UART setup */
	struct capi_uart_handle *uart_handle = NULL;
	struct max_capi_uart_extra uart_extra = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
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

	/* TMR0 setup: 32-bit mode */
	struct capi_timer_handle *tmr0 = NULL;
	struct capi_timer_config tmr0_config = {
		.ops = &max_capi_timer_ops,
		.identifier = 0, /* TMR0 */
		.input_clock_identifier = MAX_CAPI_TIMER_CLOCK_APB,
		.output_freq_hz = 25000000U,
	};

	ret = capi_timer_init(&tmr0, &tmr0_config);
	if (ret)
		return ret;

	int period_ret = capi_timer_nsec_to_ticks(tmr0,
			 (uint64_t)TIMER_IRQ_PERIOD_US * 1000ULL, &period_ticks);
	if (period_ret)
		return period_ret;

	struct capi_timer_counter_config counter = {
		.direction = CAPI_TIMER_COUNT_UP,
		.min = 0U,
		.max = period_ticks,
		.rollover = true,
	};

	ret = capi_timer_counter_config(tmr0, &counter);
	if (ret)
		return ret;

	cb_count = 0;
	ret = capi_timer_register_event_callback(tmr0, tmr_callback, NULL);
	if (ret)
		return ret;

	ret = capi_timer_event_irq_enable(tmr0,
		CAPI_TIMER_GLOBAL_EVENT_COUNTER_OVERFLOW);
	if (ret)
		return ret;

	ret = capi_timer_start(tmr0);
	if (ret)
		return ret;

	int uptime_start_ret = capi_uptime(&uptime_start);
	int uptime_end_ret;
	do {
		uptime_end_ret = capi_uptime(&uptime_end);
	} while (uptime_end_ret == 0 && (uptime_end - uptime_start) < window_us);
	unsigned int fired = cb_count;

	ret = capi_timer_stop(tmr0);
	if (ret)
		return ret;

	ret = capi_timer_event_irq_disable(tmr0,
		CAPI_TIMER_GLOBAL_EVENT_COUNTER_OVERFLOW);
	if (ret)
		return ret;

	if (uptime_start_ret)
		return uptime_start_ret;

	if (uptime_end_ret)
		return uptime_end_ret;

	ret = capi_timer_deinit(tmr0);
	if (ret)
		return ret;

	printf("fired: %u\n\r", fired);
	printf("expected: %u\n\r", TIMER_IRQ_EXPECTED_COUNT);

	return 0;
}
