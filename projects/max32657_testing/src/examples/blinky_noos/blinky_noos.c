/***************************************************************************//**
 *   @file   blinky_noos.c
 *   @brief  Blinky example using no-OS API for MAX32657
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

#include "maxim_gpio.h"
#include "no_os_alloc.h"
#include "no_os_gpio.h"
#include "no_os_error.h"

#define LED_PORT 0
#define LED_NUMBER 13

int example_main(void)
{
	int ret;
	uint8_t led_state;
	struct no_os_gpio_desc *led_descriptor;
	struct max_gpio_init_param led_extra_param = {
		.vssel = MXC_GPIO_VSSEL_VDDIOH,
	};

	struct no_os_gpio_init_param led_init_param = {
		.extra = &led_extra_param,
		.platform_ops = &max_gpio_ops,
		.port = LED_PORT,
		.number = LED_NUMBER,
		.pull = NO_OS_PULL_NONE,
	};

	led_descriptor = no_os_calloc(1, sizeof(*led_descriptor));
	if (!led_descriptor)
		return -ENOMEM;

	ret = no_os_gpio_get(&led_descriptor, &led_init_param);
	if (ret)
		return ret;

	/* Set LED to direction output HIGH */
	ret = no_os_gpio_direction_output(led_descriptor, NO_OS_GPIO_HIGH);
	if (ret)
		return ret;

	while (1)
	{
		/* Get LED state*/
		ret = no_os_gpio_get_value(led_descriptor, &led_state);
		if (ret)
			return ret;

		led_state = !led_state;

		/* Set LED state */
		ret = no_os_gpio_set_value(led_descriptor,
					   led_state ? NO_OS_GPIO_HIGH : NO_OS_GPIO_LOW);
		if (ret)
			return ret;

		no_os_mdelay(1000);
	}

	return 0;
}
