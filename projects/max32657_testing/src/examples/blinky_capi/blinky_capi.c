/***************************************************************************//**
 *   @file   blinky_capi.c
 *   @brief  Blinky example using CAPI for MAX32657
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

#include <stdlib.h>
#include "maxim_capi_gpio.h"

#define LED_PORT 0
#define LED_NUMBER 13

int example_main(void)
{
	int ret;
	uint8_t led_state;
	uint8_t count = 0;

	struct capi_gpio_port_handle *gpio_port = NULL;

	struct max_capi_gpio_extra_config extra_config = {
		.vssel = MAX_CAPI_GPIO_VSSEL_VDDIO,
		.drvstr = MAX_CAPI_GPIO_DRVSTR_0,
		.func = MAX_CAPI_GPIO_FUNC_OUT,
		.pad = MAX_CAPI_GPIO_PAD_NONE,
	};

	struct capi_gpio_port_config port_config = {
		.ops = &max_capi_gpio_ops,
		.identifier = LED_PORT,
		.flags = NULL,
		.extra = &extra_config,
		.num_pins = 32,
	};

	ret = capi_gpio_port_init(&gpio_port, &port_config);
	if (ret)
		return ret;

	struct capi_gpio_pin led_pin = {
		.flags = CAPI_GPIO_ACTIVE_HIGH,
		.number = LED_NUMBER,
		.port_handle = gpio_port,
	};

	ret = capi_gpio_pin_set_direction(&led_pin, CAPI_GPIO_OUTPUT);
	if (ret)
		return ret;

	while (1)
	{
		// /* Get LED state*/
		// ret = capi_gpio_pin_get_value(&led_pin, &led_state);
		// if (ret)
		// 	return ret;

		// led_state = !led_state;

		// /* Set LED state */
		// ret = capi_gpio_pin_set_value(&led_pin,
		// 			      led_state ? CAPI_GPIO_HIGH : CAPI_GPIO_LOW);
		// if (ret)
		// 	return ret;

		// ret = capi_gpio_pin_toggle(&led_pin);
		// if (ret)
		// 	return ret;
		ret = capi_gpio_port_toggle(gpio_port, 1U << led_pin.number);
		if (ret)
			return ret;

		capi_wait_ms(250);

		// count++;

		// if (count >= 25)
		// 	break;
	}

	ret = capi_gpio_port_deinit(&gpio_port);
	if (ret)
		return ret;

	while (1);

	return 0;
}
