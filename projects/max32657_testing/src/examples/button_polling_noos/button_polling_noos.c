/***************************************************************************//**
 *   @file   button_polling_noos.c
 *   @brief  Button polling example using no-OS API for MAX32657
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

#include "maxim_gpio.h"
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"
#include "no_os_alloc.h"
#include "no_os_error.h"
#include "max32657.h"

#define BUTTON_PORT 0
#define BUTTON_PIN 12

void button_callback(void *context)
{
	printf("Button callback called!\n\r");
}

int example_main(void)
{
	int ret;
	uint8_t button_state;
	bool button_has_been_pressed;
	uint32_t count = 0;

	/* UART setup */
	struct no_os_uart_desc *uart;
	struct no_os_uart_init_param param = {
		.baud_rate = 115200,
		.size = NO_OS_UART_CS_8,
		.parity = NO_OS_UART_PAR_NO,
		.stop = NO_OS_UART_STOP_1_BIT,
		.platform_ops = &max_uart_ops,
	};
	/* Button GPIO setup */
	struct no_os_gpio_desc *button_desc;
	struct max_gpio_init_param button_extra_param = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
	};
	struct no_os_gpio_init_param button_init_param = {
		.extra = &button_extra_param,
		.platform_ops = &max_gpio_ops,
		.port = BUTTON_PORT,
		.number = BUTTON_PIN,
		.pull = NO_OS_PULL_UP_WEAK,
	};

	ret = no_os_uart_init(&uart, &param);
	if (ret)
		return ret;

	no_os_uart_stdio(uart);

	/* Initialize button GPIO */
	printf("Initializing button GPIO\n\r");
	ret = no_os_gpio_get(&button_desc, &button_init_param);
	if (ret)
		goto free_uart;

	printf("Setting button direction to INPUT\n\r");
	ret = no_os_gpio_direction_input(button_desc);
	if (ret)
		goto free_button;

	/* Set initial state for button_state */
	ret = no_os_gpio_get_value(button_desc, &button_state);
	if (ret)
		goto free_button;
	button_has_been_pressed = !button_state;

	while (1)
	{
		/* Getting current state of button */
		ret = no_os_gpio_get_value(button_desc, &button_state);
		if (ret)
			goto free_button;

		if (!button_state)
		{
			if (!button_has_been_pressed)
			{
				count++;
				button_has_been_pressed = true;
				printf("Button pressed! %d\n\r", count);
			}
		}
		else
		{
			if (button_has_been_pressed)
			{
				printf("Button released! %d\n\r", count);
				button_has_been_pressed = false;
			}
		}

		no_os_mdelay(100);
	}

	return 0;

free_button:
	no_os_gpio_remove(button_desc);
free_uart:
	no_os_uart_remove(uart);
	return ret;
}
