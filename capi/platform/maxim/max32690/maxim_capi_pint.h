/***************************************************************************//**
 *   @file   maxim_capi_pint.h
 *   @brief  Header for the Maxim CAPI PINT (pin interrupt) driver.
 *   @author Edelweise Escala (edelweise.escala@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#ifndef MAXIM_CAPI_PINT_H_
#define MAXIM_CAPI_PINT_H_

#include "capi_pint.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/**
 * PINT ops table for the MAX32690 GPIO controller.
 *
 * Ports 0 through 3 are supported. Port 4 has no interrupt line of its own
 * (MXC_GPIO_GET_IRQ(4) resolves to the shared GPIOWAKE_IRQn), so port_init()
 * rejects it.
 */
extern const struct capi_pint_ops max_capi_pint_ops;

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* MAXIM_CAPI_PINT_H_ */
