/*******************************************************************************
 *   @file   maxim_capi_i2c_priv.h
 *   @brief  Header file for the I2C private handle
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#ifndef MAXIM_CAPI_I2C_PRIV_H_
#define MAXIM_CAPI_I2C_PRIV_H_

#include <stdbool.h>
#include <stdint.h>
#include "i2c.h"
#include "capi_i2c.h"
#include "maxim_capi_i2c.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/**
 * @struct max_capi_i2c_target_state
 * @brief Bookkeeping for a controller operating in target mode
 *
 * The MSDK drives target mode through a handler callback that is invoked for
 * each bus event. The buffers an application arms with
 * capi_i2c_transmit_async() / capi_i2c_receive_async() are tracked here so
 * the handler can service them.
 */
struct max_capi_i2c_target_state {
	/** Buffer the initiator reads from, armed by transmit_async() */
	uint8_t *tx_buf;
	/** Number of bytes available in tx_buf */
	uint32_t tx_len;
	/** Number of bytes already handed to the initiator */
	volatile uint32_t tx_pos;
	/** Buffer the initiator writes into, armed by receive_async() */
	uint8_t *rx_buf;
	/** Capacity of rx_buf in bytes */
	uint32_t rx_len;
	/** Number of bytes received so far */
	volatile uint32_t rx_pos;
	/** True once the controller has been registered as a target */
	bool registered;
	/** Address the controller answers to in target mode */
	uint16_t address;
	/**
	 * True while a MXC_I2C_SlaveTransactionAsync() listen is armed and has
	 * not yet completed. MXC_I2C_RevA_AsyncCallback() dereferences the
	 * MSDK's internal AsyncRequests[] slot without checking it for NULL,
	 * so MXC_I2C_AbortAsync() must only be called while this is true.
	 */
	volatile bool listen_armed;
};

/**
 * @struct max_capi_i2c_async_state
 * @brief Bookkeeping for a non-blocking initiator transfer
 */
struct max_capi_i2c_async_state {
	/** Request handed to MXC_I2C_MasterTransactionAsync() */
	mxc_i2c_req_t req;
	/** Scratch buffer holding sub-address + payload for writes */
	uint8_t *tx_buf;
	/** Size of the scratch buffer in bytes */
	uint32_t tx_len;
};

/**
 * @struct max_capi_i2c_priv
 * @brief Private structure for an I2C controller
 */
struct max_capi_i2c_priv {
	/** I2C controller handle */
	struct capi_i2c_controller_handle *i2c_handle;
	/** I2C device ID; 0 through MXC_I2C_INSTANCES-1 are valid */
	uint32_t identifier;
	/** I2C registers for this instance */
	mxc_i2c_regs_t *regs;
	/** NVIC line for this instance */
	IRQn_Type irqn;
	/** Voltage level of the I2C pins */
	enum max_capi_gpio_vssel vssel;
	/** Async/target event callback */
	capi_i2c_callback callback;
	/** Callback argument */
	void *callback_arg;
	/** Bus frequency in Hz */
	uint32_t freq;
	/** True when this controller was brought up as a target */
	bool initiator;
	/** Target mode state */
	struct max_capi_i2c_target_state target;
	/** Async initiator state */
	struct max_capi_i2c_async_state async;
	/** Async transfer in progress flag */
	volatile bool async_transfer_in_progress;
	/** Whether to enable IRQ connection during init */
	bool use_irq;
};

#define CAPI_I2C_HANDLE_MAXIM_INIT()				\
	(&(struct capi_i2c_controller_handle) {			\
		.ops = NULL,					\
		.init_allocated = false,			\
		.priv = &(struct max_capi_i2c_priv){0}		\
	})

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* MAXIM_CAPI_I2C_PRIV_H_ */
