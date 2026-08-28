/*******************************************************************************
 *   @file   maxim_capi_trng.h
 *   @brief  Header file for TRNG functions
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#ifndef MAXIM_CAPI_TRNG_H_
#define MAXIM_CAPI_TRNG_H_

#include "capi_trng.h"
#include "capi_irq.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

struct max_capi_trng_extra {
	/** Whether to enable IRQ connection during init */
	bool use_irq;
};

extern const struct capi_trng_ops max_capi_trng_ops;

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* MAXIM_CAPI_TRNG_H_ */
