/*******************************************************************************
 *   @file   maxim_capi_i2c.c
 *   @brief  Implementation of I2C functions with CAPI
 *   @author Edelweise Escala (edelweise.escala@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "i2c.h"
#include "i2c_reva.h"
#include "max32690.h"
#include "mxc_errors.h"
#include "capi_alloc.h"
#include "capi_irq.h"
#include "maxim_capi_i2c.h"
#include "maxim_capi_i2c_priv.h"
#include "maxim_capi_irq.h"

/** Static variables **********************************************************/

static struct capi_i2c_controller_handle *i2c[MXC_I2C_INSTANCES] = {NULL};

/** Helper functions **********************************************************/

/**
 * @brief Enable the peripheral clock and configure the pins for an instance.
 *
 * Done here instead of letting MXC_I2C_Init() do it, for two reasons the
 * no-OS MAX32690 driver documents in drivers/platform/maxim/max32690:
 *
 * - MXC_I2C_Init() always configures the pins at VDDIO (1.8 V) and then
 *   immediately runs bus recovery, which fails on boards whose wired pin set
 *   needs VDDIOH (3.3 V).
 * - Per the MAX32690 Rev A2 errata #16, MXC_I2C_Init() muxes both the default
 *   and the alternate pin set to AF1. Having both live at once corrupts the
 *   peripheral's SCL/SDA status readback and breaks recovery even when the
 *   wired bus is electrically fine. Only the wired set is muxed here.
 *
 * @param identifier - the I2C instance number.
 * @param vssel - the voltage level of the interface.
 * @return 0 on success, -EINVAL otherwise.
 */
static int _max_capi_i2c_pins_config(uint32_t identifier,
				     enum max_capi_gpio_vssel vssel)
{
	mxc_gpio_cfg_t i2c_pins;

	switch (identifier) {
	case 0:
		MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_I2C0);
		i2c_pins = gpio_cfg_i2c0;
		break;
	case 1:
		MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_I2C1);
		i2c_pins = gpio_cfg_i2c1;
		break;
	case 2:
		MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_I2C2);
		i2c_pins = gpio_cfg_i2c2;
		break;
	default:
		return -EINVAL;
	}

	i2c_pins.vssel = (mxc_gpio_vssel_t)vssel;
	i2c_pins.pad = MXC_GPIO_PAD_PULL_UP;
	MXC_GPIO_Config(&i2c_pins);

	return 0;
}

/**
 * @brief Translate a CAPI speed enum into a bus frequency in Hz
 * @param speed - the CAPI speed mode
 * @return the frequency in Hz, or 0 if unsupported on this part
 */
static uint32_t _max_capi_i2c_speed_to_hz(enum capi_i2c_speed speed)
{
	switch (speed) {
	case CAPI_I2C_SPEED_STANDARD:
		return MAX_CAPI_I2C_SPEED_STANDARD;
	case CAPI_I2C_SPEED_FAST:
		return MAX_CAPI_I2C_SPEED_FAST;
	case CAPI_I2C_SPEED_FAST_PLUS:
		return MAX_CAPI_I2C_SPEED_FAST_PLUS;
	default:
		/* High-speed and ultra-fast modes are not supported. */
		return 0;
	}
}

/**
 * @brief Resolve the controller handle backing a CAPI device
 * @param device - the CAPI device descriptor
 * @param priv - receives the controller's private structure
 * @return 0 on success, negative error code otherwise
 */
static int _max_capi_i2c_priv_from_device(struct capi_i2c_device *device,
		struct max_capi_i2c_priv **priv)
{
	if (!device || !device->controller || !device->controller->priv)
		return -EINVAL;

	*priv = device->controller->priv;

	return 0;
}

/**
 * @brief Report an event to the registered user callback, if any
 * @param priv - the controller's private structure
 * @param event - the event to report
 * @param event_extra - optional platform-specific detail
 */
static void _max_capi_i2c_notify(struct max_capi_i2c_priv *priv,
				 enum capi_i2c_async_event event,
				 int event_extra)
{
	if (priv->callback)
		priv->callback(event, priv->callback_arg, event_extra);
}

/**
 * @brief Map an MSDK completion code onto a CAPI async event
 * @param result - the MSDK result code
 * @return the matching CAPI event
 */
static enum capi_i2c_async_event _max_capi_i2c_event_from_result(int result)
{
	switch (result) {
	case E_NO_ERROR:
		return CAPI_I2C_XFR_DONE;
	case E_NO_RESPONSE:
		return CAPI_I2C_NAKD;
	case E_OVERFLOW:
		return CAPI_I2C_RXOVER;
	case E_UNDERFLOW:
		return CAPI_I2C_TXUNDER;
	case E_COMM_ERR:
		return CAPI_I2C_ALOSS;
	default:
		return CAPI_I2C_NONE;
	}
}

/**
 * @brief MSDK completion callback for async initiator transfers
 * @param req - the completed request
 * @param result - the MSDK result code
 */
static void _max_capi_i2c_async_complete(mxc_i2c_req_t *req, int result)
{
	struct max_capi_i2c_priv *priv = NULL;
	uint32_t i;

	if (!req)
		return;

	for (i = 0; i < MXC_I2C_INSTANCES; i++) {
		if (i2c[i] && i2c[i]->priv) {
			struct max_capi_i2c_priv *candidate = i2c[i]->priv;

			if (candidate->regs == req->i2c) {
				priv = candidate;
				break;
			}
		}
	}

	if (!priv)
		return;

	priv->async_transfer_in_progress = false;

	if (priv->async.tx_buf) {
		capi_free(priv->async.tx_buf);
		priv->async.tx_buf = NULL;
		priv->async.tx_len = 0;
	}

	_max_capi_i2c_notify(priv, _max_capi_i2c_event_from_result(result),
			     result);
}

/**
 * @brief MSDK target-mode event handler
 *
 * Services the buffers armed by capi_i2c_transmit_async() (initiator reads)
 * and capi_i2c_receive_async() (initiator writes), then reports completion
 * through the user callback.
 *
 * @param regs - the I2C instance reporting the event
 * @param event - the bus event
 * @param data - event-specific data (unused)
 */
static int _max_capi_i2c_target_handler(mxc_i2c_regs_t *regs,
					mxc_i2c_slave_event_t event, void *data)
{
	struct max_capi_i2c_priv *priv = NULL;
	struct max_capi_i2c_target_state *target;
	uint32_t i;

	(void)data;

	for (i = 0; i < MXC_I2C_INSTANCES; i++) {
		if (i2c[i] && i2c[i]->priv) {
			struct max_capi_i2c_priv *candidate = i2c[i]->priv;

			if (candidate->regs == regs) {
				priv = candidate;
				break;
			}
		}
	}

	if (!priv)
		return E_NO_ERROR;

	target = &priv->target;

	switch (event) {
	case MXC_I2C_EVT_MASTER_WR:
		/* The initiator is about to write; restart the RX cursor. */
		target->rx_pos = 0;
		_max_capi_i2c_notify(priv, CAPI_I2C_SRXREQ, 0);
		break;

	case MXC_I2C_EVT_MASTER_RD:
		/* The initiator is about to read; restart the TX cursor. */
		target->tx_pos = 0;
		_max_capi_i2c_notify(priv, CAPI_I2C_STXREQ, 0);
		break;

	case MXC_I2C_EVT_RX_THRESH:
	case MXC_I2C_EVT_OVERFLOW:
		/*
		 * Drain the FIFO even past rx_len, discarding the excess: an
		 * initiator may send more bytes than this listen armed for,
		 * and an unread RX FIFO holds off the ACK on the next byte
		 * (clock stretching), which hangs the initiator's write with
		 * no error ever raised on either side.
		 */
		while (!(regs->status & MXC_F_I2C_STATUS_RX_EM)) {
			uint8_t byte = (uint8_t)regs->fifo;

			if (target->rx_buf && target->rx_pos < target->rx_len)
				target->rx_buf[target->rx_pos++] = byte;
		}
		if (event == MXC_I2C_EVT_OVERFLOW)
			_max_capi_i2c_notify(priv, CAPI_I2C_RXOVER, 0);
		break;

	case MXC_I2C_EVT_TX_THRESH:
	case MXC_I2C_EVT_UNDERFLOW:
		if (target->tx_buf) {
			while (target->tx_pos < target->tx_len &&
			       !(regs->status & MXC_F_I2C_STATUS_TX_FULL)) {
				regs->fifo = target->tx_buf[target->tx_pos++];
			}
		}
		if (event == MXC_I2C_EVT_UNDERFLOW)
			_max_capi_i2c_notify(priv, CAPI_I2C_TXUNDER, 0);
		break;

	case MXC_I2C_EVT_TRANS_COMP:
		target->listen_armed = false;
		_max_capi_i2c_notify(priv, CAPI_I2C_XFR_DONE, 0);
		break;

	default:
		break;
	}

	return E_NO_ERROR;
}

/**
 * @brief Re-arm the MSDK target-mode listener for a controller
 * @param priv - the controller's private structure
 * @return 0 on success, -EIO otherwise
 */
static int _max_capi_i2c_target_listen(struct max_capi_i2c_priv *priv)
{
	int ret;

	/*
	 * A new transmit_async()/receive_async() call (or register_target()'s
	 * own call to this) may arrive while a listen is already outstanding.
	 * MXC_I2C_SlaveTransactionAsync() would return E_BUSY in that case, so
	 * this does not re-arm the hardware -- it leaves the existing listen
	 * running and relies on the caller having already updated
	 * priv->target.{tx,rx}_buf/len before calling here, so the same
	 * _max_capi_i2c_target_handler callback (already registered) services
	 * the new buffer on the next FIFO event.
	 *
	 * The alternative -- cancel the stale listen with MXC_I2C_AbortAsync()
	 * and re-arm -- is unsafe on this MSDK: MXC_I2C_RevA_AsyncCallback()
	 * unconditionally casts the MSDK's internal AsyncRequests[] slot to a
	 * master mxc_i2c_reva_req_t* and dereferences req->callback, but for a
	 * slave listen that slot holds a bare mxc_i2c_slave_handler_t function
	 * pointer instead, not a struct -- the type confusion hangs the part.
	 * There is also no public API to cancel a slave listen without hitting
	 * that path.
	 */
	if (priv->target.listen_armed)
		return 0;

	ret = MXC_I2C_SlaveTransactionAsync(priv->regs,
					    _max_capi_i2c_target_handler);
	if (ret)
		return -EIO;

	priv->target.listen_armed = true;

	return 0;
}

/** I2C functions implementation **********************************************/

/**
 * @brief Initialize an I2C controller
 * @param handle - the I2C controller handle
 * @param config - the I2C controller configuration
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_init(struct capi_i2c_controller_handle **handle,
			     const struct capi_i2c_config *config)
{
	struct capi_i2c_controller_handle *i2c_handle;
	struct max_capi_i2c_priv *i2c_priv;
	const struct max_capi_i2c_extra *i2c_extra;
	uint32_t freq;
	int ret;

	if (!handle || !config)
		return -EINVAL;

	if (config->identifier >= MXC_I2C_INSTANCES)
		return -EINVAL;

	if (i2c[config->identifier] != NULL) {
		*handle = i2c[config->identifier];
		return 0;
	}

	if (*handle == NULL) {
		i2c_handle = capi_calloc(1, sizeof(*i2c_handle));
		if (!i2c_handle)
			return -ENOMEM;

		i2c_priv = capi_calloc(1, sizeof(*i2c_priv));
		if (!i2c_priv) {
			capi_free(i2c_handle);
			return -ENOMEM;
		}

		i2c_handle->priv = i2c_priv;
		i2c_handle->init_allocated = true;
	} else {
		i2c_handle = *handle;

		if (!i2c_handle->priv)
			return -EINVAL;

		i2c_priv = i2c_handle->priv;
		i2c_handle->init_allocated = false;
	}

	i2c_handle->ops = config->ops;

	i2c_priv->i2c_handle = i2c_handle;
	i2c_priv->identifier = config->identifier;
	i2c_priv->regs = MXC_I2C_GET_I2C(config->identifier);
	i2c_priv->irqn = MXC_I2C_GET_IRQ(config->identifier);
	i2c_priv->initiator = config->initiator;
	i2c_priv->vssel = MAX_CAPI_GPIO_VSSEL_VDDIO;
	i2c_priv->use_irq = false;

	if (config->extra) {
		i2c_extra = config->extra;
		i2c_priv->vssel = i2c_extra->vssel;
		i2c_priv->use_irq = i2c_extra->use_irq;
	}

	MXC_I2C_Shutdown(i2c_priv->regs);

	ret = _max_capi_i2c_pins_config(config->identifier, i2c_priv->vssel);
	if (ret)
		goto free_handle;

	/*
	 * MXC_I2C_RevA_Init() is called directly rather than MXC_I2C_Init()
	 * since the clock and pins are already configured above at the right
	 * vssel; MXC_I2C_Init() would put them back to VDDIO before running
	 * bus recovery. The target address is applied separately below.
	 */
	if (MXC_I2C_RevA_Init((mxc_i2c_reva_regs_t *)i2c_priv->regs,
			      config->initiator ? 1 : 0,
			      config->initiator ? 0 : config->address)
	    != E_NO_ERROR) {
		ret = -EIO;
		goto free_handle;
	}

	freq = config->clk_freq_hz ? config->clk_freq_hz :
	       MAX_CAPI_I2C_SPEED_STANDARD;
	if (MXC_I2C_SetFrequency(i2c_priv->regs, freq) < 0) {
		ret = -EINVAL;
		goto shutdown_i2c;
	}
	i2c_priv->freq = MXC_I2C_GetFrequency(i2c_priv->regs);

	if (!config->initiator) {
		i2c_priv->target.address = config->address;
		i2c_priv->target.registered = true;
	}

	if (i2c_priv->use_irq) {
		ret = capi_irq_enable(i2c_priv->irqn);
		if (ret)
			goto shutdown_i2c;
	}

	i2c[config->identifier] = i2c_handle;
	*handle = i2c_handle;

	return 0;

shutdown_i2c:
	MXC_I2C_Shutdown(i2c_priv->regs);
free_handle:
	if (i2c_handle->init_allocated) {
		capi_free(i2c_priv);
		capi_free(i2c_handle);
	}

	return ret;
}

/**
 * @brief Deinitialize an I2C controller
 * @param handle - the I2C controller handle
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_deinit(struct capi_i2c_controller_handle *handle)
{
	struct max_capi_i2c_priv *i2c_priv;
	uint32_t id;

	if (!handle || !handle->priv)
		return -EINVAL;

	i2c_priv = handle->priv;
	id = i2c_priv->identifier;

	if (i2c_priv->use_irq)
		capi_irq_disable(i2c_priv->irqn);

	/*
	 * Clear any outstanding MSDK async ownership before shutdown.
	 *
	 * - For master async, this releases AsyncRequests[] without invoking the
	 *   user's completion callback during teardown.
	 * - For target listens, this is the safe way to drop a stale armed listen:
	 *   unlike AbortAsync, AsyncStop does not call RevA_AsyncCallback() and
	 *   therefore avoids the slave-listen callback type confusion in MSDK.
	 *
	 * Without this, a stale target listen can leave AsyncRequests[] occupied
	 * across test cases, making the next SlaveTransactionAsync() fail E_BUSY.
	 */
	MXC_I2C_AsyncStop(i2c_priv->regs);

	i2c_priv->async_transfer_in_progress = false;
	i2c_priv->target.listen_armed = false;

	MXC_I2C_Shutdown(i2c_priv->regs);

	if (i2c_priv->async.tx_buf) {
		capi_free(i2c_priv->async.tx_buf);
		i2c_priv->async.tx_buf = NULL;
	}

	if (handle->init_allocated) {
		capi_free(handle->priv);
		capi_free(handle);
	}

	i2c[id] = NULL;

	return 0;
}

/**
 * @brief Blocking transmit
 * @param device - the I2C target-device descriptor
 * @param transfer - the I2C transfer
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_transmit(struct capi_i2c_device *device,
				 struct capi_i2c_transfer *transfer)
{
	struct max_capi_i2c_priv *i2c_priv;
	mxc_i2c_req_t req;
	uint8_t *tx_buf = NULL;
	uint32_t tx_len;
	int ret;

	if (!transfer)
		return -EINVAL;

	ret = _max_capi_i2c_priv_from_device(device, &i2c_priv);
	if (ret)
		return ret;

	if (i2c_priv->async_transfer_in_progress)
		return -EBUSY;

	/*
	 * A sub-address is sent as the leading bytes of the same write, so
	 * concatenate it with the payload when one is supplied.
	 */
	tx_len = transfer->sub_address_len + transfer->len;
	if (tx_len == 0)
		return -EINVAL;

	if (transfer->sub_address_len) {
		tx_buf = capi_malloc(tx_len);
		if (!tx_buf)
			return -ENOMEM;

		memcpy(tx_buf, transfer->sub_address, transfer->sub_address_len);
		if (transfer->len)
			memcpy(tx_buf + transfer->sub_address_len, transfer->buf,
			       transfer->len);
	} else {
		tx_buf = transfer->buf;
	}

	req = (mxc_i2c_req_t) {
		.i2c = i2c_priv->regs,
		.addr = transfer->target_addr ? transfer->target_addr :
		device->address,
		.tx_buf = tx_buf,
		.tx_len = tx_len,
		.rx_buf = NULL,
		.rx_len = 0,
		.restart = transfer->no_stop ? 1 : 0,
		.callback = NULL,
	};

	ret = MXC_I2C_MasterTransaction(&req);

	if (transfer->sub_address_len)
		capi_free(tx_buf);

	return ret ? -EIO : 0;
}

/**
 * @brief Blocking receive
 * @param device - the I2C target-device descriptor
 * @param transfer - the I2C transfer
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_receive(struct capi_i2c_device *device,
				struct capi_i2c_transfer *transfer)
{
	struct max_capi_i2c_priv *i2c_priv;
	mxc_i2c_req_t req;
	int ret;

	if (!transfer || !transfer->buf || transfer->len == 0)
		return -EINVAL;

	ret = _max_capi_i2c_priv_from_device(device, &i2c_priv);
	if (ret)
		return ret;

	if (i2c_priv->async_transfer_in_progress)
		return -EBUSY;

	req = (mxc_i2c_req_t) {
		.i2c = i2c_priv->regs,
		.addr = transfer->target_addr ? transfer->target_addr :
		device->address,
		/*
		 * A register read is a write of the sub-address followed by a
		 * repeated start and the read, which is exactly what a request
		 * carrying both tx and rx performs.
		 */
		.tx_buf = transfer->sub_address_len ?
		transfer->sub_address : NULL,
		.tx_len = transfer->sub_address_len,
		.rx_buf = transfer->buf,
		.rx_len = transfer->len,
		.restart = transfer->no_stop ? 1 : 0,
		.callback = NULL,
	};

	ret = MXC_I2C_MasterTransaction(&req);

	return ret ? -EIO : 0;
}

/**
 * @brief Register a callback for async events
 * @param handle - the I2C controller handle
 * @param callback - the user callback
 * @param callback_arg - the user callback argument
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_register_callback(struct capi_i2c_controller_handle
		*handle,
		capi_i2c_callback const callback,
		void *const callback_arg)
{
	struct max_capi_i2c_priv *i2c_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	i2c_priv = handle->priv;
	i2c_priv->callback = callback;
	i2c_priv->callback_arg = callback_arg;

	return 0;
}

/**
 * @brief Configure the bus speed
 * @param handle - the I2C controller handle
 * @param speed - the desired speed mode
 * @param duty_cycle - requested SCLK duty cycle (not configurable here)
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_configure_bus_speed(struct capi_i2c_controller_handle
		*handle,
		enum capi_i2c_speed speed, uint8_t duty_cycle)
{
	struct max_capi_i2c_priv *i2c_priv;
	uint32_t freq;
	int ret;

	(void)duty_cycle;

	if (!handle || !handle->priv)
		return -EINVAL;

	i2c_priv = handle->priv;

	freq = _max_capi_i2c_speed_to_hz(speed);
	if (freq == 0)
		return -ENOTSUP;

	ret = MXC_I2C_SetFrequency(i2c_priv->regs, freq);
	if (ret < 0)
		return -EINVAL;

	i2c_priv->freq = MXC_I2C_GetFrequency(i2c_priv->regs);

	return 0;
}

/**
 * @brief Non-blocking transmit
 *
 * On an initiator this starts an asynchronous write. On a controller running
 * in target mode it arms the buffer the initiator will read from.
 *
 * @param device - the I2C target-device descriptor
 * @param transfer - the I2C transfer
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_transmit_async(struct capi_i2c_device *device,
				       struct capi_i2c_transfer *transfer)
{
	struct max_capi_i2c_priv *i2c_priv;
	uint32_t tx_len;
	int ret;

	if (!transfer)
		return -EINVAL;

	ret = _max_capi_i2c_priv_from_device(device, &i2c_priv);
	if (ret)
		return ret;

	if (!i2c_priv->initiator) {
		if (!transfer->buf || transfer->len == 0)
			return -EINVAL;

		i2c_priv->target.tx_buf = transfer->buf;
		i2c_priv->target.tx_len = transfer->len;
		i2c_priv->target.tx_pos = 0;

		return _max_capi_i2c_target_listen(i2c_priv);
	}

	if (!i2c_priv->use_irq)
		return -ENOTSUP;

	if (i2c_priv->async_transfer_in_progress)
		return -EBUSY;

	tx_len = transfer->sub_address_len + transfer->len;
	if (tx_len == 0)
		return -EINVAL;

	/*
	 * The request and its payload must outlive this call, so the combined
	 * sub-address + data buffer is kept in the controller's async state
	 * and released by the completion callback.
	 */
	i2c_priv->async.tx_buf = capi_malloc(tx_len);
	if (!i2c_priv->async.tx_buf)
		return -ENOMEM;

	i2c_priv->async.tx_len = tx_len;

	if (transfer->sub_address_len)
		memcpy(i2c_priv->async.tx_buf, transfer->sub_address,
		       transfer->sub_address_len);
	if (transfer->len)
		memcpy(i2c_priv->async.tx_buf + transfer->sub_address_len,
		       transfer->buf, transfer->len);

	i2c_priv->async.req = (mxc_i2c_req_t) {
		.i2c = i2c_priv->regs,
		.addr = transfer->target_addr ? transfer->target_addr :
		device->address,
		.tx_buf = i2c_priv->async.tx_buf,
		.tx_len = tx_len,
		.rx_buf = NULL,
		.rx_len = 0,
		.restart = transfer->no_stop ? 1 : 0,
		.callback = _max_capi_i2c_async_complete,
	};

	i2c_priv->async_transfer_in_progress = true;

	ret = MXC_I2C_MasterTransactionAsync(&i2c_priv->async.req);
	if (ret) {
		i2c_priv->async_transfer_in_progress = false;
		capi_free(i2c_priv->async.tx_buf);
		i2c_priv->async.tx_buf = NULL;
		return -EIO;
	}

	return 0;
}

/**
 * @brief Non-blocking receive
 *
 * On an initiator this starts an asynchronous read. On a controller running
 * in target mode it arms the buffer the initiator will write into.
 *
 * @param device - the I2C target-device descriptor
 * @param transfer - the I2C transfer
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_receive_async(struct capi_i2c_device *device,
				      struct capi_i2c_transfer *transfer)
{
	struct max_capi_i2c_priv *i2c_priv;
	int ret;

	if (!transfer || !transfer->buf || transfer->len == 0)
		return -EINVAL;

	ret = _max_capi_i2c_priv_from_device(device, &i2c_priv);
	if (ret)
		return ret;

	if (!i2c_priv->initiator) {
		i2c_priv->target.rx_buf = transfer->buf;
		i2c_priv->target.rx_len = transfer->len;
		i2c_priv->target.rx_pos = 0;

		return _max_capi_i2c_target_listen(i2c_priv);
	}

	if (!i2c_priv->use_irq)
		return -ENOTSUP;

	if (i2c_priv->async_transfer_in_progress)
		return -EBUSY;

	i2c_priv->async.req = (mxc_i2c_req_t) {
		.i2c = i2c_priv->regs,
		.addr = transfer->target_addr ? transfer->target_addr :
		device->address,
		.tx_buf = transfer->sub_address_len ?
		transfer->sub_address : NULL,
		.tx_len = transfer->sub_address_len,
		.rx_buf = transfer->buf,
		.rx_len = transfer->len,
		.restart = transfer->no_stop ? 1 : 0,
		.callback = _max_capi_i2c_async_complete,
	};

	i2c_priv->async_transfer_in_progress = true;

	ret = MXC_I2C_MasterTransactionAsync(&i2c_priv->async.req);
	if (ret) {
		i2c_priv->async_transfer_in_progress = false;
		return -EIO;
	}

	return 0;
}

/**
 * @brief Recover the bus when a device is holding SDA low
 * @param handle - the I2C controller handle
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_recover_bus(struct capi_i2c_controller_handle *handle)
{
	struct max_capi_i2c_priv *i2c_priv;
	int ret;

	if (!handle || !handle->priv)
		return -EINVAL;

	i2c_priv = handle->priv;

	ret = MXC_I2C_Recover(i2c_priv->regs, 16);

	/*
	 * MSDK recover toggles GPIO-style bus-bit-bang and exits with EN cleared,
	 * so the peripheral must be re-enabled/re-initialized before use.
	 * Also clear any stale async ownership without invoking callbacks.
	 */
	MXC_I2C_AsyncStop(i2c_priv->regs);
	i2c_priv->async_transfer_in_progress = false;
	i2c_priv->target.listen_armed = false;

	if (MXC_I2C_RevA_Init((mxc_i2c_reva_regs_t *)i2c_priv->regs,
			      i2c_priv->initiator ? 1 : 0,
			      i2c_priv->initiator ? 0 :
			      i2c_priv->target.address) != E_NO_ERROR)
		return -EIO;

	if (MXC_I2C_SetFrequency(i2c_priv->regs, i2c_priv->freq) < 0)
		return -EINVAL;

	return ret ? -EIO : 0;
}

/**
 * @brief Register the controller in target mode
 * @param handle - the I2C controller handle
 * @param addr - the address the controller answers to
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_register_target(struct capi_i2c_controller_handle
					*handle,
					uint16_t addr)
{
	struct max_capi_i2c_priv *i2c_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	i2c_priv = handle->priv;

	/* Ensure no stale async ownership leaks into the role switch. */
	MXC_I2C_AsyncStop(i2c_priv->regs);
	i2c_priv->async_transfer_in_progress = false;
	i2c_priv->target.listen_armed = false;

	/*
	 * MXC_I2C_Shutdown() clock-gates and resets the peripheral, so the
	 * clock and pins must be reapplied before RevA_Init() can bring it
	 * back up -- the same ordering init() uses. This does not clear a
	 * stale armed listen from an earlier register_target() -- see the
	 * comment in _max_capi_i2c_target_listen() -- but that is fine: the
	 * listen below is skipped in that case and the existing one (still
	 * routed to this same _max_capi_i2c_target_handler) carries on.
	 */
	MXC_I2C_Shutdown(i2c_priv->regs);

	if (_max_capi_i2c_pins_config(i2c_priv->identifier, i2c_priv->vssel))
		return -EINVAL;

	if (MXC_I2C_RevA_Init((mxc_i2c_reva_regs_t *)i2c_priv->regs, 0, addr)
	    != E_NO_ERROR)
		return -EIO;

	if (MXC_I2C_SetFrequency(i2c_priv->regs, i2c_priv->freq) < 0)
		return -EINVAL;

	i2c_priv->initiator = false;
	i2c_priv->target.address = addr;
	i2c_priv->target.registered = true;

	return _max_capi_i2c_target_listen(i2c_priv);
}

/**
 * @brief Unregister the controller from target mode
 * @param handle - the I2C controller handle
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_i2c_unregister_target(struct capi_i2c_controller_handle
		*handle)
{
	struct max_capi_i2c_priv *i2c_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	i2c_priv = handle->priv;

	/*
	 * Drop any armed target listen before leaving target mode; otherwise the
	 * MSDK AsyncRequests[] slot can remain busy across subsequent operations.
	 */
	MXC_I2C_AsyncStop(i2c_priv->regs);
	i2c_priv->target.listen_armed = false;

	/* See the comment in max_capi_i2c_register_target(). */
	MXC_I2C_Shutdown(i2c_priv->regs);

	if (_max_capi_i2c_pins_config(i2c_priv->identifier, i2c_priv->vssel))
		return -EINVAL;

	if (MXC_I2C_RevA_Init((mxc_i2c_reva_regs_t *)i2c_priv->regs, 1, 0)
	    != E_NO_ERROR)
		return -EIO;

	if (MXC_I2C_SetFrequency(i2c_priv->regs, i2c_priv->freq) < 0)
		return -EINVAL;

	i2c_priv->initiator = true;
	i2c_priv->target.registered = false;
	i2c_priv->target.tx_buf = NULL;
	i2c_priv->target.rx_buf = NULL;

	return 0;
}

/**
 * @brief I2C interrupt handler
 * @param handle - the I2C controller handle
 */
static void max_capi_i2c_isr(void *handle)
{
	struct capi_i2c_controller_handle *i2c_handle = handle;
	struct max_capi_i2c_priv *i2c_priv;

	if (!i2c_handle || !i2c_handle->priv)
		return;

	i2c_priv = i2c_handle->priv;

	MXC_I2C_AsyncHandler(i2c_priv->regs);
}

const struct capi_i2c_ops max_capi_i2c_ops = {
	.init = max_capi_i2c_init,
	.deinit = max_capi_i2c_deinit,
	.transmit = max_capi_i2c_transmit,
	.receive = max_capi_i2c_receive,
	.register_callback = max_capi_i2c_register_callback,
	.configure_bus_speed = max_capi_i2c_configure_bus_speed,
	.transmit_async = max_capi_i2c_transmit_async,
	.receive_async = max_capi_i2c_receive_async,
	.recover_bus = max_capi_i2c_recover_bus,
	.register_target = max_capi_i2c_register_target,
	.unregister_target = max_capi_i2c_unregister_target,
	.isr = max_capi_i2c_isr,
};
