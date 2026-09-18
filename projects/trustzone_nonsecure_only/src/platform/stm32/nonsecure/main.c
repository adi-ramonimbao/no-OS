/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Non-Secure entry point for the TrustZone split consumer (STM32).
 *
 * Reached from the Secure world's Reset_Handler jump. The application logic
 * lives in src/examples/keystore/keystore_nonsecure.c (example_main()),
 * reused unchanged by any platform's Non-Secure world. This file only runs
 * stm32_init() first (CubeMX bring-up for this world: MX_GTZC_NS_Init(),
 * MX_GPIO_Init(), MX_USART3_UART_Init()) before forwarding to it.
 */

extern int stm32_init(void);
extern int example_main(void);

int main(void)
{
	stm32_init();

	return example_main();
}
