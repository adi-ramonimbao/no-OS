/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Secure-world entry for the TrustZone hello demo (STM32 platform).
 *
 * The Secure world:
 *   1. calls stm32_init() (CubeMX bring-up: SystemInit()'s SAU setup already
 *      ran before main(); this runs clocks, MX_GTZC_S_Init() -- which
 *      delegates USART3 and the LED pin to Non-Secure per the .ioc's
 *      per-peripheral/per-pin security context, no manual HAL_GTZC_*
 *      / HAL_GPIO_ConfigPinAttributes calls needed here -- and MX_GPIO_Init()),
 *   2. branches into the Non-Secure image. Unlike Maxim's SPC (delegated at
 *      runtime, so Secure can use a peripheral briefly first), CubeMX's
 *      per-peripheral context is static: USART3 belongs to Non-Secure from
 *      boot, so there is no Secure banner here (and no HAL UART driver source
 *      even compiled into this build - see stm32_trustzone.cmake).
 *
 * IncrementCount_S() is the secure gateway (cmse_nonsecure_entry) the
 * Non-Secure app calls back into; it is defined in
 * src/examples/hello/hello_secure.c, which uses no platform header and is
 * reused unchanged by any platform's Secure world.
 */

#include <stdint.h>

#include "parameters.h"

/* Non-Secure image's flash origin (NonSecure/*_FLASH_ns.ld ORIGIN(FLASH)). */
#define VTOR_TABLE_NS_START_ADDR	0x08100000U

typedef void (*funcptr_ns)(void) __attribute__((cmse_nonsecure_call));

extern int stm32_init(void);

/* Set the Non-Secure vector table, its initial MSP, and branch to its reset
 * handler. Standard ARMv8-M Secure->Non-Secure handoff (mirrors ST's own
 * TrustZone example main.c); CubeMX's MX_GTZC_S_Init() already delegated the
 * peripherals the Non-Secure world needs before this runs. */
static void jump_to_nonsecure(void)
{
	funcptr_ns reset_handler_ns;

	SCB_NS->VTOR = VTOR_TABLE_NS_START_ADDR;
	__TZ_set_MSP_NS(*(uint32_t *)VTOR_TABLE_NS_START_ADDR);
	reset_handler_ns = (funcptr_ns)(*(uint32_t *)(VTOR_TABLE_NS_START_ADDR + 4U));
	reset_handler_ns();
}

int main(void)
{
	/* CubeMX's per-peripheral security context (the .ioc) assigns USART3 to
	 * Non-Secure statically: unlike Maxim's SPC (which the Secure app
	 * delegates to Non-Secure at runtime, letting Secure use a peripheral
	 * briefly first), stm32_init()'s MX_GTZC_S_Init() has already handed
	 * USART3 to Non-Secure before this runs, and the Secure build was never
	 * even given the HAL UART driver source to compile. So there is no
	 * Secure banner here (nothing to print with) - Secure only brings the
	 * board up and hands off. */
	stm32_init();

	jump_to_nonsecure();

	/* Should never return; nothing to report through (no UART driver in
	 * this build - see the comment above). */
	while (1)
		;
}
