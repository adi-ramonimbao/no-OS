/***************************************************************************//**
 * @file main.c
 * @brief Main file for Maxim MAX32657 platform of capi_selftest project.
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 ******************************************************************************/

#include "parameters.h"
#include "common_data.h"

extern int example_main(void);

/**
 * @brief Main function execution for MAX32657 platform.
 * @return Result of the enabled example execution.
 */
int main(void)
{
	SysTick_Config(SystemCoreClock / 1000);
	/* This has to be performed so the en state of SysTick is saved. */
	MXC_Delay(1);

#if SPI_HAS_IRQ || TIMER_HAS_IRQ
	if (capi_irq_init(&irq_config) == 0)
		(void)capi_irq_global_enable();
#endif

	return example_main();
}
