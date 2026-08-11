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
#include "no_os_delay.h"
#include "maxim_pwm.h"
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"
#include "tmr.h"

#define PWM_STEP 1

int example_main(void)
{
	int ret;

	uint32_t period_ns = 100000;
	uint32_t duty_cycle_ns = 50000;
	uint8_t direction = 1; // 0 = down, 1 = up

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

	/* PWM setup */
	struct no_os_pwm_desc *tmr0 = NULL;
	mxc_tmr_cfg_t tmr0_config = {
		.bitMode = MXC_TMR_BIT_MODE_32,
		.clock = MXC_TMR_APB_CLK,
		.mode = MXC_TMR_MODE_PWM,
		.cmp_cnt = 25000000,
		.pol = 1,
		.pres = MXC_TMR_PRES_1,
	};
	struct max_pwm_extra pwm_extra = {
		.tmr_cfg = tmr0_config,
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
	};
	struct no_os_pwm_init_param tmr0_param = {
		.id = 0,
		.platform_ops = &max_pwm_ops,
		.extra = &pwm_extra,
		.polarity = NO_OS_PWM_POLARITY_HIGH,
		.period_ns = period_ns, /* 10kHz */
		.duty_cycle_ns = duty_cycle_ns, /* 50% duty cycle*/
	};

	no_os_pwm_init(&tmr0, &tmr0_param);

	while (1) {
		if (direction) {
			if (duty_cycle_ns < period_ns) {
				duty_cycle_ns += PWM_STEP;
			} else {
				direction = 0;
				duty_cycle_ns -= PWM_STEP;
			}
		} else {
			if (duty_cycle_ns > 0) {
				duty_cycle_ns -= PWM_STEP;
			} else {
				direction = 1;
				duty_cycle_ns += PWM_STEP;
			}
		}

		ret = no_os_pwm_set_duty_cycle(tmr0, duty_cycle_ns);
		if (ret)
			return ret;
	}

	return 0;
}
