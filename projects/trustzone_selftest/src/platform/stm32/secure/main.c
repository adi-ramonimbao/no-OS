/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   main.c
 * @brief  Secure-world entry for STM32 TrustZone selftest.
 *
 * This world performs CubeMX secure bring-up and then branches to the
 * Non-Secure reset handler where the CAPI loopback + TrustZone tests run.
 */

#include <stdint.h>
#include <stdbool.h>

#include "stm32_hal.h"
#include "parameters.h"

typedef void (*funcptr_ns)(void) __attribute__((cmse_nonsecure_call));

extern int stm32_init(void);

static bool nonsecure_image_present(void)
{
	const volatile uint32_t *ns_vectors = (const volatile uint32_t *)NS_FLASH_ORIGIN;

	return ns_vectors[1] != 0xFFFFFFFFU;
}

static void jump_to_nonsecure(void)
{
	funcptr_ns reset_handler_ns;

	SCB_NS->VTOR = NS_FLASH_ORIGIN;
	__TZ_set_MSP_NS(*(uint32_t *)NS_FLASH_ORIGIN);
	reset_handler_ns = (funcptr_ns)(*(uint32_t *)(NS_FLASH_ORIGIN + 4U));
	reset_handler_ns();
}

int main(void)
{
	stm32_init();

	if (!nonsecure_image_present()) {
		while (1)
			;
	}

	jump_to_nonsecure();

	while (1)
		;
}
