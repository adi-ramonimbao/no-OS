/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  STM32 Non-Secure entry point for TrustZone selftest.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "parameters.h"
#include "common_data.h"
#include "stm32_hal.h"
#include "capi_irq.h"

extern int example_main(void);

#if defined(IRQ_CTRL_IDENTIFIER) && defined(GPIO_OUTPUT_OPS)
#define GPIO_IRQ_PIN		GPIO_PIN_0
#if defined(STM32H5)
#define GPIO_IRQ_PORT		GPIOG
#else
#define GPIO_IRQ_PORT		GPIOC
#endif
#define GPIO_IRQ_IRQN		EXTI0_IRQn

int platform_gpio_irq_arm(uint32_t *irq_line)
{
	GPIO_InitTypeDef init = { 0 };

	if (!irq_line)
		return -EINVAL;

#if defined(STM32H5)
	__HAL_RCC_SBS_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
#else
	__HAL_RCC_SYSCFG_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
#endif

	init.Pin = GPIO_IRQ_PIN;
	init.Mode = GPIO_MODE_IT_RISING;
	init.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIO_IRQ_PORT, &init);

	__HAL_GPIO_EXTI_CLEAR_IT(GPIO_IRQ_PIN);
	HAL_NVIC_ClearPendingIRQ(GPIO_IRQ_IRQN);

	*irq_line = (uint32_t)GPIO_IRQ_IRQN;

	return 0;
}

bool platform_gpio_irq_ack(void)
{
	if (__HAL_GPIO_EXTI_GET_IT(GPIO_IRQ_PIN) != 0U) {
		__HAL_GPIO_EXTI_CLEAR_IT(GPIO_IRQ_PIN);
		return true;
	}

	return false;
}

void platform_gpio_irq_disarm(void)
{
#if defined(STM32H5)
	EXTI->IMR1 &= ~GPIO_IRQ_PIN;
#else
	EXTI->IMR &= ~GPIO_IRQ_PIN;
#endif
	__HAL_GPIO_EXTI_CLEAR_IT(GPIO_IRQ_PIN);
	HAL_NVIC_ClearPendingIRQ(GPIO_IRQ_IRQN);
}

void EXTI0_IRQHandler(void)
{
	stm32_capi_exti_handler(0U);
}
#endif /* IRQ_CTRL_IDENTIFIER && GPIO_OUTPUT_OPS */

int main(void)
{
	stm32_init();

#if SPI_HAS_IRQ || TIMER_HAS_IRQ
	if (capi_irq_init(&irq_config) == 0)
		(void)capi_irq_global_enable();
#endif

	return example_main();
}
