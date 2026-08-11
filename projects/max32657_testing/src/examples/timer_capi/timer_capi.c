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
#include "dma.h"
#include "capi_dma.h"
#include "capi_irq.h"
#include "capi_timer.h"
#include "capi_uart.h"
#include "capi_time.h"
#include "maxim_capi_dma.h"
#include "maxim_capi_gpio.h"
#include "maxim_capi_irq.h"
#include "maxim_capi_timer.h"
#include "maxim_capi_uart.h"

#define PWM_STEP 1
#define MARKER_PORT 0
#define MARKER_PIN 13

static uint32_t cb_count = 0;

// void tmr_callback(uint32_t event, uint32_t chan, void *arg, int event_extra) {
void tmr_callback(uint32_t event, void *arg, int event_extra) {
	printf("Callback called!\r\n");
}

int example_main(void)
{
	int ret;
	uint8_t count = 0;
	uint32_t ticks;

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

	/* GPIO marker on pin 13 to timestamp the TMR3 one-shot start */
	struct capi_gpio_port_handle *marker_port = NULL;
	struct max_capi_gpio_extra_config marker_extra = {
		.vssel = MAX_CAPI_GPIO_VSSEL_VDDIO,
		.drvstr = MAX_CAPI_GPIO_DRVSTR_0,
		.func = MAX_CAPI_GPIO_FUNC_OUT,
		.pad = MAX_CAPI_GPIO_PAD_NONE,
	};
	struct capi_gpio_port_config marker_port_config = {
		.ops = &max_capi_gpio_ops,
		.identifier = MARKER_PORT,
		.flags = NULL,
		.extra = &marker_extra,
		.num_pins = 32,
	};

	ret = capi_gpio_port_init(&marker_port, &marker_port_config);
	if (ret)
		return ret;

	struct capi_gpio_pin marker_pin = {
		.flags = CAPI_GPIO_ACTIVE_HIGH,
		.number = MARKER_PIN,
		.port_handle = marker_port,
	};

	ret = capi_gpio_pin_set_direction(&marker_pin, CAPI_GPIO_OUTPUT);
	if (ret)
		return ret;

	ret = capi_gpio_pin_set_value(&marker_pin, CAPI_GPIO_LOW);
	if (ret)
		return ret;

	/* TMR0 setup: dual 16-bit mode
	   Timer A: counter @ 10 kHz
	   Timer B: counter @ 5 kHz */
	struct capi_timer_handle *tmr0 = NULL;
	struct max_capi_timer_extra tmr0_extra = {
		.bit_mode = MAX_CAPI_TIMER_BIT_MODE_16BIT_DUAL,
	};
	struct capi_timer_config tmr0_config = {
		.ops = &max_capi_timer_ops,
		.identifier = 0, /* TMR0 */
		.input_clock_identifier = MAX_CAPI_TIMER_CLOCK_APB,
		.output_freq_hz = 25000000,
		.extra = &tmr0_extra,
	};

	printf("Initializing TMR0 in dual 16-bit mode...\r\n");
	printf("TMR0A: Counter @ 10 kHz\r\n");
	printf("TMR0B: Counter @  5 kHz\r\n");
	ret = capi_timer_init(&tmr0, &tmr0_config);
	if (ret)
		return ret;

	ret = capi_timer_channel_init(tmr0, 0);
	if (ret)
		return ret;

	ret = capi_timer_channel_init(tmr0, 1);
	if (ret)
		return ret;

	/* 10 kHz square wave => 50 us half-period => 1250 ticks @ 25 MHz */
	ret = capi_timer_nsec_to_ticks(tmr0, 50000, &ticks);
	if (ret)
		return ret;

	struct max_capi_timer_channel_extra tmr0a_extra = {
		.init_pin = true,
		.use_alternate_pin = false,
	};
	struct capi_timer_channel_config tmr0a_config = {
		.mode = MAX_CAPI_TIMER_MODE_CONTINUOUS,
		.config.compare = {
			.match_value = ticks,
			.generate_pulse_on_match = true,
			.polarity = CAPI_TIMER_ON_COMPARE_TOGGLE,
		},
		.extra = &tmr0a_extra,
	};

	ret = capi_timer_channel_config(tmr0, 0, &tmr0a_config);
	if (ret)
		return ret;

	/* 5 kHz square wave => 100 us half-period => 2500 ticks @ 25 MHz */
	ret = capi_timer_nsec_to_ticks(tmr0, 100000, &ticks);
	if (ret)
		return ret;

	struct capi_timer_channel_config tmr0b_config = {
		.mode = MAX_CAPI_TIMER_MODE_CONTINUOUS,
		.config.compare = {
			.match_value = ticks,
			.polarity = CAPI_TIMER_ON_COMPARE_TOGGLE,
		},
	};

	ret = capi_timer_channel_config(tmr0, 1, &tmr0b_config);
	if (ret)
		return ret;

	ret = capi_timer_channel_enable(tmr0, 0);
	if (ret)
		return ret;

	ret = capi_timer_channel_enable(tmr0, 1);
	if (ret)
		return ret;

	ret = capi_timer_start(tmr0);
	if (ret)
		return ret;

	/* TMR1 setup (PWM at 10 kHz, 10% duty cycle) */
	struct capi_timer_handle *tmr1 = NULL;
	struct capi_timer_config tmr1_config = {
		.ops = &max_capi_timer_ops,
		.identifier = 1, /* TMR1 */
		.input_clock_identifier = MAX_CAPI_TIMER_CLOCK_APB,
		.output_freq_hz = 25000000,
	};
	struct max_capi_timer_channel_extra tmr1_extra = {
		.init_pin = true,
	};
	struct capi_timer_channel_config tmr1_channel_config = {
		.mode = CAPI_TIMER_PWM_MODE,
		.config.pwm = {
			.inverted_polarity = false,
			.period_ns = 100000,
			.active_ns = 10000,
		},
		.extra = &tmr1_extra,
	};

	printf("Initializing TMR1 in 32-bit mode...\r\n");
	printf("TMR1: PWM @ 10 kHz, 90 percent duty cycle\r\n");
	ret = capi_timer_init(&tmr1, &tmr1_config);
	if (ret)
		return ret;

	ret = capi_timer_channel_init(tmr1, 0);
	if (ret)
		return ret;

	ret = capi_timer_channel_config(tmr1, 0, &tmr1_channel_config);
	if (ret)
		return ret;

	ret = capi_timer_channel_enable(tmr1, 0);
	if (ret)
		return ret;

	ret = capi_timer_start(tmr1);
	if (ret)
		return ret;

	/** TMR4 setup (compare mode) */
	struct capi_timer_handle *tmr4 = NULL;
	struct capi_timer_config tmr4_config = {
		.ops = &max_capi_timer_ops,
		.identifier = 4,
		.input_clock_identifier = MAX_CAPI_TIMER_CLOCK_APB,
		.output_freq_hz = 25000000,
	};

	printf("Initializing TMR4 in 32-bit mode...\r\n");
	printf("TMR4: Compare mode with match value @ 1 ms\r\n");
	ret = capi_timer_init(&tmr4, &tmr4_config);
	if (ret)
		return ret;

	ret = capi_timer_channel_init(tmr4, 0);
	if (ret)
		return ret;

	ret = capi_timer_nsec_to_ticks(tmr4, 1000000, &ticks);
	if (ret)
		return ret;

	struct max_capi_timer_channel_extra tmr4_extra = {
		.init_pin = true,
	};
	struct capi_timer_channel_config tmr4_channel_config = {
		.mode = CAPI_TIMER_COMPARE_MODE,
		.config.compare = {
			.match_value = ticks,
			.generate_pulse_on_match = true,
			.polarity = CAPI_TIMER_ON_COMPARE_TOGGLE,
		},
		.extra = &tmr4_extra,
	};

	ret = capi_timer_channel_config(tmr4, 0, &tmr4_channel_config);
	if (ret)
		return ret;

	ret = capi_timer_channel_enable(tmr4, 0);
	if (ret)
		return ret;

	ret = capi_timer_start(tmr4);
	if (ret)
		return ret;

	/* TMR3 setup (one-shot) */
	struct capi_timer_handle *tmr3 = NULL;
	struct capi_timer_config tmr3_config = {
		.ops = &max_capi_timer_ops,
		.identifier = 3,
		.input_clock_identifier = MAX_CAPI_TIMER_CLOCK_APB,
		.output_freq_hz = 25000000U,
	};

	printf("Initializing TMR3 in 32-bit mode...\r\n");
	printf("TMR3: One-shot after 400 ns, repeating every 3 s\r\n");
	ret = capi_timer_init(&tmr3, &tmr3_config);
	if (ret)
		return ret;

	ret = capi_timer_channel_init(tmr3, 0);
	if (ret)
		return ret;

	ret = capi_timer_nsec_to_ticks(tmr3, 400, &ticks);
	if (ret)
		return ret;

	struct max_capi_timer_channel_extra tmr3_channel_extra = {
		.init_pin = true,
	};
	struct capi_timer_channel_config tmr3_channel_config = {
		.mode = MAX_CAPI_TIMER_MODE_ONESHOT,
		.config.compare = {
			.match_value = ticks,
			.generate_pulse_on_match = true,
			.polarity = CAPI_TIMER_ON_COMPARE_TOGGLE,
		},
		.extra = &tmr3_channel_extra,
	};

	ret = capi_timer_channel_config(tmr3, 0, &tmr3_channel_config);
	if (ret)
		return ret;

	ret = capi_timer_register_event_callback(tmr3, tmr_callback, NULL);
	if (ret)
		return ret;

	ret = capi_timer_event_irq_enable(tmr3, CAPI_TIMER_GLOBAL_EVENT_COUNTER_OVERFLOW);
	if (ret)
		return ret;

	ret = capi_timer_channel_enable(tmr3, 0);
	if (ret)
		return ret;

	capi_wait_ms(1000);
	while (1) {
		printf("Restarting TMR3...\r\n");

		/* Raise the marker just before start; its rising edge marks t=0.
		 * The TMR3 one-shot output pulse should follow ~400 ns later. */
		capi_gpio_pin_set_value(&marker_pin, CAPI_GPIO_HIGH);
		ret = capi_timer_start(tmr3);
		if (ret)
			return ret;
		capi_gpio_pin_set_value(&marker_pin, CAPI_GPIO_LOW);

		capi_wait_ms(3000);

		count++;

		// if (count >= 25)
			// break;
	}

	printf("Deinitializing TMR0...\r\n");
	ret = capi_timer_deinit(tmr0);
	if (ret)
		return ret;

	printf("Deinitializing TMR1...\r\n");
	ret = capi_timer_deinit(tmr1);
	if (ret)
		return ret;

	printf("Deinitializing TMR4...\r\n");
	ret = capi_timer_deinit(tmr4);
	if (ret)
		return ret;

	printf("Deinitializing TMR3...\r\n");
	ret = capi_timer_deinit(tmr3);
	if (ret)
		return ret;

	ret = capi_gpio_port_deinit(&marker_port);
	if (ret)
		return ret;

	printf("Deinitializing UART...\r\n");
	ret = capi_uart_deinit(uart_handle);
	if (ret)
		return ret;

	while (1);

	return 0;
}
