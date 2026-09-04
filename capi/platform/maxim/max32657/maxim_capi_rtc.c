/*******************************************************************************
 *   @file   maxim_capi_rtc.c
 *   @brief  Implementation of RTC functions
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include "capi_alloc.h"
#include "maxim_capi_irq.h"
#include "maxim_capi_rtc.h"
#include "maxim_capi_rtc_priv.h"
#include "rtc.h"

#ifdef CONFIG_CAPI_RTC_DIRECT_API
#define CAPI_DIRECT_API
#include <capi_direct.h>
#define MAX_RTC_DIRECT_ALIAS(name) \
	ADD_OPTIONAL_CAPI_DIRECT_ALIAS(capi_rtc, max_capi_rtc, name)
#else
#define MAX_RTC_DIRECT_ALIAS(name)
#endif

/** Static variables **********************************************************/

static struct capi_rtc_handle *rtc = NULL;

/** Forward declarations ******************************************************/

void max_capi_rtc_isr(void *handle);

/** Function implementations **************************************************/

/**
 * @brief Initialize the RTC peripheral
 * @param handle Pointer to RTC handle pointer
 * @param config RTC configuration
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_init(struct capi_rtc_handle **handle,
		      const struct capi_rtc_config *config)
{
	int ret;
	struct capi_rtc_handle *rtc_handle;
	struct max_capi_rtc_priv *rtc_priv;
	const struct max_capi_rtc_extra *rtc_extra;
	uint32_t actual_freq;

	if (!handle || !config)
		return -EINVAL;

	if (rtc != NULL) {
		*handle = rtc;
		return 0;
	}

	if (config->initial_subsec >= MXC_RTC_MAX_SSEC)
		return -EINVAL;

	if (*handle == NULL) {
		rtc_handle = capi_calloc(1, sizeof(*rtc_handle));
		if (!rtc_handle)
			return -ENOMEM;

		rtc_priv = capi_calloc(1, sizeof(*rtc_priv));
		if (!rtc_priv) {
			capi_free(rtc_handle);
			return -ENOMEM;
		}

		rtc_handle->init_allocated = true;
	} else {
		rtc_handle = *handle;

		if (!rtc_handle->priv)
			return -EINVAL;

		rtc_priv = rtc_handle->priv;

		rtc_handle->init_allocated = false;
	}

	ret = MXC_RTC_Init(config->initial_sec, config->initial_subsec);
	if (ret) {
		ret = -EIO;
		goto free_handle;
	}

	rtc_priv->freq = config->freq ? config->freq : 32768;
	rtc_priv->measure_freq = false;
	rtc_priv->callback = NULL;
	rtc_priv->event_ctx = NULL;
	rtc_priv->events_enabled = 0;
	rtc_priv->is_running = false;
	rtc_priv->use_irq = false;

	if (config->extra) {
		rtc_extra = config->extra;
		rtc_priv->measure_freq = rtc_extra->measure_freq;
		rtc_priv->use_irq = rtc_extra->use_irq;
	}

	if (rtc_priv->measure_freq) {
		actual_freq = MXC_RTC_TrimCrystal();
		if (actual_freq > 0)
			rtc_priv->freq = actual_freq;
	}

	if (rtc_priv->use_irq) {
		ret = capi_irq_connect(RTC_IRQn, max_capi_rtc_isr, rtc_handle);
		if (ret)
			goto reset_rtc;

		ret = capi_irq_set_priority(RTC_IRQn, config->irq_priority);
		if (ret)
			goto reset_rtc;

		ret = capi_irq_enable(RTC_IRQn);
		if (ret)
			goto reset_rtc;
	}

	rtc_handle->ops = config->ops;

	rtc = rtc_handle;
	*handle = rtc_handle;

	return 0;

reset_rtc:
	MXC_RTC_DisableInt(MXC_RTC_INT_EN_LONG | MXC_RTC_INT_EN_SHORT |
			   MXC_RTC_INT_EN_READY);
	MXC_RTC_Stop();
free_handle:
	if (rtc_handle->init_allocated) {
		capi_free(rtc_priv);
		capi_free(rtc_handle);
	}

	rtc = NULL;

	return ret;
}
MAX_RTC_DIRECT_ALIAS(init)

/**
 * @brief Deinitialize the RTC peripheral
 * @param handle The RTC handle
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_deinit(struct capi_rtc_handle *handle)
{
	int ret;
	struct max_capi_rtc_priv *rtc_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	if (handle != rtc)
		return -EINVAL;

	rtc_priv = handle->priv;

	MXC_RTC_DisableInt(MXC_RTC_INT_EN_LONG |
			   MXC_RTC_INT_EN_SHORT |
			   MXC_RTC_INT_EN_READY);

	if (rtc_priv->use_irq)
		capi_irq_disable(RTC_IRQn);

	ret = MXC_RTC_Stop();

	if (handle->init_allocated) {
		capi_free(rtc_priv);
		capi_free(handle);
	}

	rtc = NULL;

	return ret;
}
MAX_RTC_DIRECT_ALIAS(deinit)

/**
 * @brief Start the RTC
 * @param handle The RTC handle
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_start(struct capi_rtc_handle *handle)
{
	int ret;
	struct max_capi_rtc_priv *rtc_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	rtc_priv = handle->priv;

	ret = MXC_RTC_Start();
	if (ret != E_NO_ERROR)
		return -EIO;

	rtc_priv->is_running = true;

	return 0;
}
MAX_RTC_DIRECT_ALIAS(start)

/**
 * @brief Stop the RTC
 * @param handle The RTC handle
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_stop(struct capi_rtc_handle *handle)
{
	int ret;
	struct max_capi_rtc_priv *rtc_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	rtc_priv = handle->priv;

	ret = MXC_RTC_Stop();
	if (ret != E_NO_ERROR)
		return -EIO;

	rtc_priv->is_running = false;

	return 0;
}
MAX_RTC_DIRECT_ALIAS(stop)

/**
 * @brief Get the current time
 * @param handle The RTC handle
 * @param time Where to store the current time
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_get_time(struct capi_rtc_handle *handle,
			  struct capi_rtc_time *time)
{
	int ret;
	uint32_t sec, subsec;

	if (!handle || !time)
		return -EINVAL;

	ret = MXC_RTC_GetTime(&sec, &subsec);
	if (ret != E_NO_ERROR)
		return (ret == E_BUSY) ? -EBUSY : -EIO;

	time->sec = sec;
	time->subsec = subsec;
	time->subsec_resolution = MXC_RTC_MAX_SSEC;

	return 0;
}
MAX_RTC_DIRECT_ALIAS(get_time)

/**
 * @brief Set the current time
 * @param handle The RTC handle
 * @param time The time struct
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_set_time(struct capi_rtc_handle *handle,
			  const struct capi_rtc_time *time)
{
	int ret;
	const struct max_capi_rtc_priv *rtc_priv;

	if (!handle || !handle->priv || !time)
		return -EINVAL;

	if (time->subsec >= MXC_RTC_MAX_SSEC)
		return -EINVAL;

	rtc_priv = handle->priv;

	ret = MXC_RTC_Stop();
	if (ret != E_NO_ERROR)
		return -EIO;

	ret = MXC_RTC_Init(time->sec, time->subsec);
	if (ret != E_NO_ERROR)
		return -EIO;

	if (rtc_priv->is_running) {
		ret = MXC_RTC_Start();
		if (ret != E_NO_ERROR)
			return -EIO;
	}

	return 0;
}
MAX_RTC_DIRECT_ALIAS(set_time)

/**
 * @brief Get the current datetime - not supported since the MAX32657 does not
 * 	  have calendar-related functions
 * @param handle The RTC handle
 * @param datetime Where to store the datetime
 * @return -ENOSYS
 */
int max_capi_rtc_get_datetime(struct capi_rtc_handle *handle,
			      struct capi_rtc_datetime *datetime)
{
	return -ENOSYS;
}
MAX_RTC_DIRECT_ALIAS(get_datetime)

/**
 * @brief Set the current datetime - not supported since the MAX32657 does not
 * 	  have calendar-related functions
 * @param handle The RTC handle
 * @param datetime The datetime struct
 * @return -ENOSYS
 */
int max_capi_rtc_set_datetime(struct capi_rtc_handle *handle,
			      const struct capi_rtc_datetime *datetime)
{
	return -ENOSYS;
}
MAX_RTC_DIRECT_ALIAS(set_datetime)

/**
 * @brief Set an alarm
 * @param handle The RTC handle
 * @param type Alarm type
 * @param alarm_value Pointer to alarm value
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_set_alarm(struct capi_rtc_handle *handle,
			   enum capi_rtc_alarm_type type,
			   const void *alarm_value)
{
	const struct capi_rtc_time *alarm;
	const uint32_t *subsec;
	int ret;

	if (!handle || !alarm_value)
		return -EINVAL;

	switch (type) {
	case CAPI_RTC_ALARM_TIME:
		alarm = alarm_value;

		if (alarm->sec > MXC_F_RTC_TODA_TOD_ALARM)
			return -EINVAL;

		ret = MXC_RTC_SetTimeofdayAlarm(alarm->sec);
		if (ret != E_NO_ERROR)
			return -EIO;

		return 0;

	case CAPI_RTC_ALARM_SUBSEC:
		subsec = alarm_value;

		ret = MXC_RTC_SetSubsecondAlarm(*subsec);
		if (ret != E_NO_ERROR)
			return -EIO;

		return 0;

	case CAPI_RTC_ALARM_DATETIME:
		return -ENOTSUP;

	default:
		return -EINVAL;
	}
}
MAX_RTC_DIRECT_ALIAS(set_alarm)

/**
 * @brief Disable an alarm
 * @param handle The RTC handle
 * @param type Alarm type
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_disable_alarm(struct capi_rtc_handle *handle,
			       enum capi_rtc_alarm_type type)
{
	int ret;

	if (!handle)
		return -EINVAL;

	switch (type) {
	case CAPI_RTC_ALARM_TIME:
		ret = MXC_RTC_DisableInt(MXC_RTC_INT_EN_LONG);
		break;
	case CAPI_RTC_ALARM_SUBSEC:
		ret = MXC_RTC_DisableInt(MXC_RTC_INT_EN_SHORT);
		break;
	case CAPI_RTC_ALARM_DATETIME:
		return -ENOTSUP;
	default:
		return -EINVAL;
	}

	if (ret != E_NO_ERROR)
		return -EIO;

	return 0;
}
MAX_RTC_DIRECT_ALIAS(disable_alarm)

/**
 * @brief Enable square wave output
 * @param handle The RTC handle
 * @param freq The square wave frequency
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_sqwave_enable(struct capi_rtc_handle *handle,
			       enum capi_rtc_sqwave_freq freq)
{
	int ret;
	mxc_rtc_freq_sel_t msdk_freq;

	if (!handle)
		return -EINVAL;

	switch (freq) {
	case CAPI_RTC_SQWAVE_1HZ:
		msdk_freq = MXC_RTC_F_1HZ;
		break;
	case CAPI_RTC_SQWAVE_512HZ:
		msdk_freq = MXC_RTC_F_512HZ;
		break;
	case CAPI_RTC_SQWAVE_4KHZ:
		msdk_freq = MXC_RTC_F_4KHZ;
		break;
	case CAPI_RTC_SQWAVE_32KHZ:
		msdk_freq = MXC_RTC_F_32KHZ;
		break;
	default:
		return -EINVAL;
	}

	ret = MXC_RTC_SquareWaveStart(msdk_freq);
	if (ret != E_NO_ERROR)
		return -EIO;

	return 0;
}
MAX_RTC_DIRECT_ALIAS(sqwave_enable)

/**
 * @brief Disable square wave output
 * @param handle The RTC handle
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_sqwave_disable(struct capi_rtc_handle *handle)
{
	int ret;

	if (!handle)
		return -EINVAL;

	ret = MXC_RTC_SquareWaveStop();
	if (ret != E_NO_ERROR)
		return -EIO;

	return 0;
}
MAX_RTC_DIRECT_ALIAS(sqwave_disable)

/**
 * @brief Trim/calibrate the RTC frequency
 * @param handle The RTC handle
 * @param trim Trim value
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_trim(struct capi_rtc_handle *handle, int8_t trim)
{
	int ret;
	struct max_capi_rtc_priv *rtc_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	rtc_priv = handle->priv;

	ret = MXC_RTC_Trim(trim);
	if (ret != E_NO_ERROR)
		return -EIO;

	if (rtc_priv->measure_freq) {
		uint32_t actual_freq = MXC_RTC_TrimCrystal();
		if (actual_freq > 0)
			rtc_priv->freq = actual_freq;
	}

	return 0;
}
MAX_RTC_DIRECT_ALIAS(trim)

/**
 * @brief Register event callback
 * @param handle The RTC handle
 * @param callback Callback function
 * @param event_ctx Context pointer for callback
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_register_callback(struct capi_rtc_handle *handle,
				   capi_rtc_event_callback_t callback,
				   void *event_ctx)
{
	struct max_capi_rtc_priv *rtc_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	rtc_priv = handle->priv;
	rtc_priv->callback = callback;
	rtc_priv->event_ctx = event_ctx;

	return 0;
}
MAX_RTC_DIRECT_ALIAS(register_callback)

/**
 * @brief Enable an RTC event
 * @param handle The RTC handle
 * @param event Event to enable
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_enable_event(struct capi_rtc_handle *handle, uint32_t event)
{
	int ret;
	struct max_capi_rtc_priv *rtc_priv;
	uint32_t msdk_mask = 0;

	if (!handle || !handle->priv)
		return -EINVAL;

	rtc_priv = handle->priv;

	if (!rtc_priv->use_irq)
		return -ENOTSUP;

	if (event == CAPI_RTC_EVENT_ALARM)
		msdk_mask |= MXC_RTC_INT_EN_LONG;
	else if (event == CAPI_RTC_EVENT_SUBSEC_ALARM)
		msdk_mask |= MXC_RTC_INT_EN_SHORT;
	else if (event == CAPI_RTC_EVENT_READY)
		msdk_mask |= MXC_RTC_INT_EN_READY;
	else
		return -EINVAL;

	ret = MXC_RTC_EnableInt(msdk_mask);
	if (ret != E_NO_ERROR)
		return -EIO;

	rtc_priv->events_enabled |= (1U << event);

	return 0;
}
MAX_RTC_DIRECT_ALIAS(enable_event)

/**
 * @brief Disable an RTC event
 * @param handle The RTC handle
 * @param event Event to disable
 * @return 0 on success, negative error code otherwise
 */
int max_capi_rtc_disable_event(struct capi_rtc_handle *handle, uint32_t event)
{
	int ret;
	struct max_capi_rtc_priv *rtc_priv;
	uint32_t msdk_mask = 0;

	if (!handle || !handle->priv)
		return -EINVAL;

	rtc_priv = handle->priv;

	if (event == CAPI_RTC_EVENT_ALARM)
		msdk_mask |= MXC_RTC_INT_EN_LONG;
	else if (event == CAPI_RTC_EVENT_SUBSEC_ALARM)
		msdk_mask |= MXC_RTC_INT_EN_SHORT;
	else if (event == CAPI_RTC_EVENT_READY)
		msdk_mask |= MXC_RTC_INT_EN_READY;
	else
		return -EINVAL;

	ret = MXC_RTC_DisableInt(msdk_mask);
	if (ret != E_NO_ERROR)
		return -EIO;

	rtc_priv->events_enabled &= ~(1U << event);

	return 0;
}
MAX_RTC_DIRECT_ALIAS(disable_event)

/**
 * @brief RTC interrupt handler
 * @param handle The RTC handle
 */
void max_capi_rtc_isr(void *handle)
{
	struct capi_rtc_handle *rtc_handle = (struct capi_rtc_handle *)handle;
	struct max_capi_rtc_priv *rtc_priv;
	int flags;

	if (!rtc_handle || !rtc_handle->priv)
		return;

	rtc_priv = rtc_handle->priv;

	flags = MXC_RTC_GetFlags();

	if (flags & MXC_RTC_INT_FL_LONG) {
		MXC_RTC_ClearFlags(MXC_RTC_INT_FL_LONG);

		if (rtc_priv->callback
		    && (rtc_priv->events_enabled & (1 << CAPI_RTC_EVENT_ALARM)))
			rtc_priv->callback(rtc_handle, CAPI_RTC_EVENT_ALARM, rtc_priv->event_ctx);
	}

	if (flags & MXC_RTC_INT_FL_SHORT) {
		MXC_RTC_ClearFlags(MXC_RTC_INT_FL_SHORT);

		if (rtc_priv->callback
		    && (rtc_priv->events_enabled & (1 << CAPI_RTC_EVENT_SUBSEC_ALARM)))
			rtc_priv->callback(rtc_handle, CAPI_RTC_EVENT_SUBSEC_ALARM,
					   rtc_priv->event_ctx);
	}

	if (flags & MXC_RTC_INT_FL_READY) {
		MXC_RTC_ClearFlags(MXC_RTC_INT_FL_READY);

		if (rtc_priv->callback
		    && (rtc_priv->events_enabled & (1 << CAPI_RTC_EVENT_READY)))
			rtc_priv->callback(rtc_handle, CAPI_RTC_EVENT_READY, rtc_priv->event_ctx);
	}
}
MAX_RTC_DIRECT_ALIAS(isr)

const struct capi_rtc_ops max_capi_rtc_ops = {
	.init = max_capi_rtc_init,
	.deinit = max_capi_rtc_deinit,
	.start = max_capi_rtc_start,
	.stop = max_capi_rtc_stop,
	.get_time = max_capi_rtc_get_time,
	.set_time = max_capi_rtc_set_time,
	.get_datetime = max_capi_rtc_get_datetime,
	.set_datetime = max_capi_rtc_set_datetime,
	.set_alarm = max_capi_rtc_set_alarm,
	.disable_alarm = max_capi_rtc_disable_alarm,
	.sqwave_enable = max_capi_rtc_sqwave_enable,
	.sqwave_disable = max_capi_rtc_sqwave_disable,
	.trim = max_capi_rtc_trim,
	.register_callback = max_capi_rtc_register_callback,
	.enable_event = max_capi_rtc_enable_event,
	.disable_event = max_capi_rtc_disable_event,
	.isr = max_capi_rtc_isr,
};

