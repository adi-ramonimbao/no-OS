/***************************************************************************//**
 *   @file   timer_noos.c
 *   @brief  Timer example using no-OS API for MAX32657
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

#include "no_os_alloc.h"
#include "no_os_error.h"
#include "maxim_timer.h"
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"
#include "tmr.h"

int example_main(void)
{
	int ret;

	/* UART setup */
	struct no_os_uart_desc *uart = NULL;
	struct max_uart_init_param uart_extra = {
	    .vssel = MXC_GPIO_VSSEL_VDDIOH,
	};
	struct no_os_uart_init_param uart_param = {
	    .baud_rate = 115200,
	    .size = NO_OS_UART_CS_8,
	    .parity = NO_OS_UART_PAR_NO,
	    .stop = NO_OS_UART_STOP_1_BIT,
	    .platform_ops = &max_uart_ops,
	    .extra = &uart_extra,
	};

	ret = no_os_uart_init(&uart, &uart_param);
	if (ret)
		return ret;
	no_os_uart_stdio(uart);

	/* Timer setup */
	struct no_os_timer_desc *tmr0 = NULL;
	struct no_os_timer_desc *tmr1 = NULL;
	struct no_os_timer_desc *tmr3 = NULL;
	struct no_os_timer_desc *tmr4 = NULL;
	struct no_os_timer_desc *tmr5 = NULL;
	struct max_timer_extra timer_extra = {
		.init_pins = true,
	};
	struct no_os_timer_init_param tmr0_param = {
		.extra = &timer_extra,
		.platform_ops = &max_timer_ops,
		.freq_hz = 25000000, /* 25 MHz */
		.ticks_count = 1250000, /* Should give us a 10 Hz timer */
		.id = 0,
	};
	struct no_os_timer_init_param tmr1_param = {
		.extra = &timer_extra,
		.platform_ops = &max_timer_ops,
		.freq_hz = 25000000,
		.ticks_count = 625000, /* Should give us a 20 Hz timer */
		.id = 1,
	};
	struct no_os_timer_init_param tmr3_param = {
		.extra = &timer_extra,
		.platform_ops = &max_timer_ops,
		.freq_hz = 25000000,
		.ticks_count = 312500, /* Should give us a 40 Hz timer */
		.id = 3,
	};
	struct no_os_timer_init_param tmr4_param = {
		.extra = &timer_extra,
		.platform_ops = &max_timer_ops,
		.freq_hz = 25000000,
		.ticks_count = 156250, /* Should give us an 80 Hz timer */
		.id = 4,
	};
	struct no_os_timer_init_param tmr5_param = {
		.extra = &timer_extra,
		.platform_ops = &max_timer_ops,
		.freq_hz = 25000000,
		.ticks_count = 78125, /* Should give us an 80 Hz timer */
		.id = 5,
	};
	ret = no_os_timer_init(&tmr0, &tmr0_param);
	if (ret)
		return ret;
	ret = no_os_timer_init(&tmr1, &tmr1_param);
	if (ret)
		return ret;
	ret = no_os_timer_init(&tmr3, &tmr3_param);
	if (ret)
		return ret;
	ret = no_os_timer_init(&tmr4, &tmr4_param);
	if (ret)
		return ret;
	ret = no_os_timer_init(&tmr5, &tmr5_param);
	if (ret)
		return ret;

	ret = MXC_GPIO_Config(&gpio_cfg_tmr3);
	if (ret != E_SUCCESS)
		return ret;
	ret = MXC_GPIO_Config(&gpio_cfg_tmr4);
	if (ret != E_SUCCESS)
		return ret;

	ret = no_os_timer_start(tmr0);
	if (ret)
		return ret;
	ret = no_os_timer_start(tmr1);
	if (ret)
		return ret;
	ret = no_os_timer_start(tmr3);
	if (ret)
		return ret;
	ret = no_os_timer_start(tmr4);
	if (ret)
		return ret;
	ret = no_os_timer_start(tmr5);
	if (ret)
		return ret;

	while (1);

	return 0;
}
