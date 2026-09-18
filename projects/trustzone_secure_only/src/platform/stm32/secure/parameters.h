/*
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file   parameters.h
 * @brief  Secure-world platform parameters for the STM32H5 TZ producer demo.
 *
 * No UART here: USART3 is statically Non-Secure per the .ioc's per-peripheral
 * GTZC context (see src/platform/stm32/secure/main.c), so the Secure world
 * cannot print a banner on this platform, unlike Maxim's dynamic SPC handoff.
 */

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include <stdint.h>

/*
 * Non-Secure image's flash origin (NonSecure/*_FLASH_ns.ld ORIGIN(FLASH)).
 * Used to sanity-check that a Non-Secure image has actually been programmed
 * before branching into it, so a Secure-only deliverable flashed on its own
 * does not fault on an erased region.
 */
#define NS_FLASH_ORIGIN		0x08100000U

#endif /* __PARAMETERS_H__ */
