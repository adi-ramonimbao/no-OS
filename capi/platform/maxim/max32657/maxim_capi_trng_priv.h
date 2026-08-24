/*******************************************************************************
 *   @file   maxim_capi_trng_priv.h
 *   @brief  Header file for private TRNG handle
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#ifndef MAXIM_CAPI_TRNG_PRIV_H_
#define MAXIM_CAPI_TRNG_PRIV_H_

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

#include "maxim_capi_trng.h"

struct max_capi_trng_priv {
	/** Callback function for async */
	capi_trng_callback_t callback;
	/** Callback argument for async */
	void *callback_arg;
	/** Async in progress */
	volatile bool busy;
};

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* MAXIM_CAPI_TRNG_PRIV_H_ */
