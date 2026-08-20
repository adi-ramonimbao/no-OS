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

struct max_capi_rtc_extra {
	/** Whether to measure the crystal frequency on startup and during trim */
	bool measure_freq;
};

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
};

extern const struct capi_rtc_ops max_capi_rtc_ops;

#endif /* MAXIM_CAPI_RTC_H_ */
