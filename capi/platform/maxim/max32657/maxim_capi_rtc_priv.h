/*******************************************************************************
 *   @file   maxim_capi_rtc_priv.h
 *   @brief  Header file for private RTC handle
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#ifndef MAXIM_CAPI_RTC_PRIV_H_
#define MAXIM_CAPI_RTC_PRIV_H_

#include "maxim_capi_rtc.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/**
 * @struct max_capi_rtc_priv
 * @brief Private RTC data structure
 */
struct max_capi_rtc_priv {
	/** Frequency in Hz (measured, typically 32768) */
	uint32_t freq;
	/** Whether to measure the crystal frequency on startup and during trim */
	bool measure_freq;
	/** Event callback function */
	capi_rtc_event_callback_t callback;
	/** Context pointer for callback */
	void *event_ctx;
	/** Enabled events mask */
	uint32_t events_enabled;
	/** RTC running state */
	bool is_running;
	/** Whether to enable IRQ connection during init */
	bool use_irq;
};

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* MAXIM_CAPI_RTC_PRIV_H_ */
