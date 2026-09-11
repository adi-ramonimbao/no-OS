/*******************************************************************************
 *   @file   maxim_capi_trng.c
 *   @brief  Implementation of TRNG functions
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#include <stdlib.h>
#include <errno.h>
#include "capi_trng.h"
#include "capi_alloc.h"
#include "maxim_capi_trng.h"
#include "maxim_capi_trng_priv.h"
#include "maxim_capi_irq.h"
#include "trng.h"
#include "max32657.h"

/** Static variables **********************************************************/

static struct capi_trng_handle *trng = NULL;

/** Forward declarations ******************************************************/

static void max_capi_trng_isr(void *handle);

/** Helper functions **********************************************************/

/**
 * @brief Callback function to pass to MXC_TRNG_RandomAsync
 * @param req MSDK-managed parameter (not used)
 * @param result The result of the callback
 */
static void _max_capi_trng_msdk_callback(void *req, int result)
{
	struct max_capi_trng_priv *trng_priv;
	enum capi_trng_async_event event;

	if (!trng || !trng->priv)
		return;

	trng_priv = trng->priv;
	trng_priv->busy = false;

	if (trng_priv->callback) {
		event = (result == E_NO_ERROR)
			? CAPI_TRNG_EVENT_GENERATION_COMPLETE
			: CAPI_TRNG_EVENT_ERROR;
		trng_priv->callback(event, trng_priv->callback_arg, result);
	}
}

/** Function implementations **************************************************/

/**
 * @brief Initialize the TRNG peripheral
 * @param handle Pointer to the TRNG handle
 * @param config Configuration struct
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_trng_init(struct capi_trng_handle **handle,
		       const struct capi_trng_config *config)
{
	int ret;
	struct capi_trng_handle *trng_handle;
	struct max_capi_trng_priv *trng_priv;
	bool use_irq = false;

	if (!handle || !config)
		return -EINVAL;

	if (config->identifier != 0)
		return -EINVAL;

	if (trng != NULL) {
		*handle = trng;
		return 0;
	}

	if (*handle == NULL) {
		trng_handle = capi_calloc(1, sizeof(*trng_handle));
		if (!trng_handle)
			return -ENOMEM;

		trng_priv = capi_calloc(1, sizeof(*trng_priv));
		if (!trng_priv) {
			capi_free(trng_handle);
			return -ENOMEM;
		}

		trng_handle->init_allocated = true;
	} else {
		trng_handle = *handle;

		if (!trng_handle->priv)
			return -EINVAL;

		trng_priv = trng_handle->priv;

		trng_handle->init_allocated = false;
	}

	trng_handle->priv = trng_priv;
	trng_priv->busy = false;
	trng_priv->callback = NULL;
	trng_priv->callback_arg = NULL;
	trng_priv->use_irq = false;
	trng_handle->ops = config->ops;

	if (config->extra) {
		struct max_capi_trng_extra *trng_extra = config->extra;
		use_irq = trng_extra->use_irq;
	}

	ret = MXC_TRNG_Init();
	if (ret)
		goto free_handle;

	if (use_irq) {
		ret = capi_irq_connect(TRNG_IRQn, max_capi_trng_isr, trng_handle);
		if (ret)
			goto shutdown_trng;

		ret = capi_irq_set_priority(TRNG_IRQn, config->irq_priority);
		if (ret)
			goto shutdown_trng;

		ret = capi_irq_enable(TRNG_IRQn);
		if (ret)
			goto shutdown_trng;

		trng_priv->use_irq = true;
	}

	trng = trng_handle;
	*handle = trng_handle;

	return 0;

shutdown_trng:
	MXC_TRNG_Shutdown();
free_handle:
	if (trng_handle->init_allocated) {
		capi_free(trng_priv);
		capi_free(trng_handle);
	}

	trng = NULL;

	return ret;
}

/**
 * @brief Deinitialize the TRNG peripheral
 * @param handle The TRNG handle
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_trng_deinit(struct capi_trng_handle *handle)
{
	int ret;
	struct max_capi_trng_priv *trng_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	if (handle != trng)
		return -EINVAL;

	trng_priv = handle->priv;

	if (trng_priv->use_irq) {
		capi_irq_disable(TRNG_IRQn);
		MXC_TRNG_DisableInt();
	}

	ret = MXC_TRNG_Shutdown();

	if (handle->init_allocated) {
		capi_free(trng_priv);
		capi_free(handle);
	}

	trng = NULL;

	return ret;
}

/**
 * @brief Generate an unsigned 32-bit number (blocking)
 * @param handle The TRNG handle
 * @param value Where to store the unsigned 32-bit number
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_trng_generate_u32(struct capi_trng_handle *handle, uint32_t *value)
{
	if (!handle || !handle->priv || !value)
		return -EINVAL;

	*value = (uint32_t)MXC_TRNG_RandomInt();

	return 0;
}

/**
 * @brief Fill a buffer with random bytes (blocking)
 * @param handle The TRNG handle
 * @param buffer The data buffer to store bytes to
 * @param length The length of the buffer
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_trng_fill_buffer(struct capi_trng_handle *handle, uint8_t *buffer,
			      uint32_t length)
{
	int ret;

	if (!handle || !handle->priv || !buffer)
		return -EINVAL;

	if (length == 0)
		return -EINVAL;

	ret = MXC_TRNG_Random(buffer, length);
	if (ret != E_NO_ERROR)
		return -EINVAL;

	return 0;
}

/**
 * @brief Fill a buffer with random bytes (non-blocking)
 * @param handle The TRNG handle
 * @param buffer The data buffer to store bytes to
 * @param length The length of the buffer
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_trng_fill_buffer_async(struct capi_trng_handle *handle,
				    uint8_t *buffer, uint32_t length)
{
	struct max_capi_trng_priv *trng_priv;

	if (!handle || !handle->priv || !buffer)
		return -EINVAL;

	if (length == 0)
		return -EINVAL;

	trng_priv = handle->priv;

	if (!trng_priv->use_irq)
		return -ENOTSUP;

	if (trng_priv->busy)
		return -EBUSY;

	trng_priv->busy = true;
	MXC_TRNG_RandomAsync(buffer, length, _max_capi_trng_msdk_callback);

	return 0;
}

/**
 * @brief Abort the async fill operation
 * @param handle The TRNG handle
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_trng_abort_async(struct capi_trng_handle *handle)
{
	struct max_capi_trng_priv *trng_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	trng_priv = handle->priv;

	if (!trng_priv->use_irq)
		return -ENOTSUP;

	capi_irq_disable(TRNG_IRQn);
	if (!trng_priv->busy) {
		capi_irq_enable(TRNG_IRQn);
		return 0;
	}

	MXC_TRNG_DisableInt();
	capi_irq_clear_pending(TRNG_IRQn);
	trng_priv->busy = false;
	capi_irq_enable(TRNG_IRQn);

	if (trng_priv->callback)
		trng_priv->callback(CAPI_TRNG_EVENT_ABORTED,
				    trng_priv->callback_arg, 0);

	return 0;
}

/**
 * @brief Check whether the TRNG is busy or not
 * @param handle The TRNG handle
 * @param is_busy True if TRNG in progress, False if not
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_trng_is_busy(struct capi_trng_handle *handle, bool *is_busy)
{
	struct max_capi_trng_priv *trng_priv;

	if (!handle || !handle->priv || !is_busy)
		return -EINVAL;

	trng_priv = handle->priv;
	*is_busy = trng_priv->busy;

	return 0;
}

/**
 * @brief Register a callback
 * @param handle The TRNG handle
 * @param callback The callback function
 * @param callback_arg The callback function argument
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_trng_register_callback(struct capi_trng_handle *handle,
				    capi_trng_callback_t callback,
				    void *callback_arg)
{
	struct max_capi_trng_priv *trng_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	trng_priv = handle->priv;

	trng_priv->callback = callback;
	trng_priv->callback_arg = callback_arg;

	return 0;
}

/**
 * @brief Perform entropy source health test
 * @param handle The TRNG handle
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_trng_health_test(struct capi_trng_handle *handle)
{
	int ret;

	if (!handle || !handle->priv)
		return -EINVAL;

	ret = MXC_TRNG_HealthTest();
	if (ret != E_NO_ERROR)
		return -EINVAL;

	return 0;
}

/**
 * @brief Interrupt handler for the TRNG peripheral
 * @param handle The TRNG handle
 */
static void max_capi_trng_isr(void *handle)
{
	struct capi_trng_handle *trng_handle = (struct capi_trng_handle *)handle;

	if (!trng_handle || !trng_handle->priv)
		return;

	MXC_TRNG_Handler();
}

const struct capi_trng_ops max_capi_trng_ops = {
	.init = max_capi_trng_init,
	.deinit = max_capi_trng_deinit,
	.generate_u32 = max_capi_trng_generate_u32,
	.fill_buffer = max_capi_trng_fill_buffer,
	.fill_buffer_async = max_capi_trng_fill_buffer_async,
	.abort_async = max_capi_trng_abort_async,
	.is_busy = max_capi_trng_is_busy,
	.register_callback = max_capi_trng_register_callback,
	.health_test = max_capi_trng_health_test,
	.isr = max_capi_trng_isr,
};

