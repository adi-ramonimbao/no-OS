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

struct max_capi_trng_priv {
	/** Callback function for async */
	capi_trng_callback_t callback;
	/** Callback argument for async */
	void *callback_arg;
};

extern const struct capi_trng_ops max_capi_trng_ops;

#endif /* MAXIM_CAPI_TRNG_H_ */
