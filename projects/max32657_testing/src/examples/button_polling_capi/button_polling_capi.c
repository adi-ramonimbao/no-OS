/***************************************************************************//**
 *   @file   button_polling_capi.c
 *   @brief  Button polling example using CAPI for MAX32657
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
#include "capi_alloc.h"
#include "capi_time.h"
#include "capi_gpio.h"
#include "capi_uart.h"
#include "maxim_capi_gpio.h"
#include "maxim_capi_uart.h"

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
	struct capi_uart_handle *uart = NULL;
	struct max_capi_uart_extra uart_extra_config = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
	};
	struct capi_uart_line_config uart_line_config = {
		.baudrate = 115200,
		.size = CAPI_UART_DATA_BITS_8,
		.parity = CAPI_UART_PARITY_NONE,
		.stop_bits = CAPI_UART_STOP_1_BIT,
	};
	struct capi_uart_config uart_capi_config = {
		.line_config = &uart_line_config,
		.ops = &max_capi_uart_ops,
		.extra = &uart_extra_config,
	};
	/* Button GPIO setup */
	struct capi_gpio_port_handle *gpio_port = NULL;
	struct max_capi_gpio_extra_config gpio_extra_config = {
		.pad = MXC_GPIO_PAD_WEAK_PULL_UP,
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
		.drvstr = MXC_GPIO_DRVSTR_0,
	};
	struct capi_gpio_port_config gpio_port_config = {
		.identifier = 0,
		.flags = NULL,
		.ops = &max_capi_gpio_ops,
		.extra = &gpio_extra_config,
		.num_pins = 32,
	};

	struct capi_irq_config irq_config = {
		.irq_ctrl_id = 0,
	};
	ret = capi_irq_init(&irq_config);
	if (ret)
		return ret;

	ret = capi_uart_init(&uart, &uart_capi_config);
	if (ret)
		return ret;

	max_capi_uart_stdio_enable(uart);

	/* Initialize button GPIO */
	printf("Initializing button GPIO\n\r");
	ret = capi_gpio_port_init(&gpio_port, &gpio_port_config);
	if (ret)
		return ret;

	/* Initialize button after GPIO port is initialized */
	struct capi_gpio_pin button_pin = {
		.port_handle = gpio_port,
		.number = BUTTON_PIN,
		.flags = CAPI_GPIO_ACTIVE_LOW,
	};

	printf("Setting button direction to INPUT\n\r");
	ret = capi_gpio_pin_set_direction(&button_pin, CAPI_GPIO_INPUT);
	if (ret)
		return ret;

	/* Set initial state for button_state */
	ret = capi_gpio_pin_get_value(&button_pin, &button_state);
	if (ret)
		return ret;
	button_has_been_pressed = button_state;

	while (1)
	{
		/* Getting current state of button */
		ret = capi_gpio_pin_get_value(&button_pin, &button_state);
		if (ret)
			return ret;

		if (button_state)
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

		if (count >= 25)
			break;
	}

	printf("Deinitializing the GPIO port...\n\r");
	ret = capi_gpio_port_deinit(&gpio_port);
	if (ret)
		return ret;

	printf("Deinitializing UART...\n\r");
	ret = capi_uart_deinit(uart);
	if (ret)
		return ret;

	while (1);

	return 0;
}
