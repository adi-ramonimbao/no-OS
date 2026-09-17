/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Non-Secure entry point for the TrustZone hello demo (STM32 platform).
 *
 * Reached from the Secure world's Reset_Handler jump. The application logic
 * lives in src/examples/hello/hello_nonsecure.c (example_main()), which is
 * reused unchanged by any platform's Non-Secure world. This file only runs
 * stm32_init() first (CubeMX bring-up for this world: MX_GTZC_NS_Init(),
 * MX_GPIO_Init(), MX_USART3_UART_Init() -- every STM32 project's main.c calls
 * stm32_init() first, TrustZone or not) before forwarding to it.
 */

extern int stm32_init(void);
extern int example_main(void);

int main(void)
{
	stm32_init();

	return example_main();
}
