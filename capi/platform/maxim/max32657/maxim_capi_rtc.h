/*******************************************************************************
 *   @file   maxim_capi_rtc.h
 *   @brief  Header file for RTC functions
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#ifndef MAXIM_CAPI_RTC_H_
#define MAXIM_CAPI_RTC_H_

#include "capi_irq.h"
#include "capi_rtc.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/**
 * @struct max_capi_rtc_extra
 * @brief MAX32657 platform-specific extra RTC configuration
 */
struct max_capi_rtc_extra {
	/** Whether to measure the crystal frequency on startup and during trim */
	bool measure_freq;
};

extern const struct capi_rtc_ops max_capi_rtc_ops;

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* MAXIM_CAPI_RTC_H_ */
