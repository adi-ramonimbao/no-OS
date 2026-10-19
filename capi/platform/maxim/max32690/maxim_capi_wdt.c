/*******************************************************************************
 *   @file   maxim_capi_wdt.c
 *   @brief  Implementation of WDT functions
 *   @author Edelweise Escala (edelweise.escala@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#include <errno.h>
#include <string.h>
#include "maxim_capi_irq.h"
#include "maxim_capi_wdt.h"
#include "maxim_capi_wdt_priv.h"
#include "capi_wdt.h"
#include "capi_irq.h"
#include "capi_alloc.h"
#include "max32690.h"

/** Static variables **********************************************************/

static struct capi_wdt_handle *wdt[MXC_CFG_WDT_INSTANCES] = {NULL};

/** Forward declarations ******************************************************/

static void max_capi_wdt_isr(struct capi_wdt_handle *handle);

/** Helper functions **********************************************************/

/**
 * @brief Get the watchdog clock frequency
 *
 * The MAX32690 watchdog is clocked from the peripheral clock and offers no
 * source selection, so there is nothing to choose between here.
 *
 * @return Clock frequency in Hz
 */
static uint32_t _max_capi_wdt_get_clock_freq(void)
{
	return PeripheralClock;
}

/**
 * @brief Convert microseconds to period (enum max_capi_wdt_period)
 * @param timeout_us Timeout in microseconds
 * @param clock_freq_hz Clock frequency in Hz
 * @param period Period
 * @return 0 on success, negative error code otherwise
 */
static int _max_capi_wdt_us_to_period(uint64_t timeout_us,
				      uint32_t clock_freq_hz,
				      enum max_capi_wdt_period *period)
{
	uint64_t required_ticks;
	int power;

	if (!period)
		return -EINVAL;

	required_ticks = (timeout_us * clock_freq_hz) / 1000000ULL;

	for (power = 16; power <= 31; power++) {
		if ((1ULL << power) >= required_ticks) {
			*period = (enum max_capi_wdt_period)(31 - power);
			return 0;
		}
	}

	return -ERANGE;
}

/** WDT functions implementations *********************************************/

/**
 * @brief Initialize the WDT peripheral. Does not start the WDT.
 * 	  WDT starts with the first feed.
 * @param handle The WDT handle
 * @param config The config struct
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_wdt_init(struct capi_wdt_handle **handle,
			     const struct capi_wdt_config *config)
{
	int ret;
	struct capi_wdt_handle *wdt_handle;
	struct max_capi_wdt_priv *wdt_priv;
	mxc_wdt_cfg_t wdt_config;
	uint32_t rst_flags;

	if (!handle || !config)
		return -EINVAL;

	if (config->identifier >= MXC_CFG_WDT_INSTANCES)
		return -EINVAL;

	if (wdt[config->identifier] != NULL) {
		*handle = wdt[config->identifier];
		return 0;
	}

	if (*handle == NULL) {
		wdt_handle = capi_calloc(1, sizeof(*wdt_handle));
		if (!wdt_handle)
			return -ENOMEM;

		wdt_priv = capi_calloc(1, sizeof(*wdt_priv));
		if (!wdt_priv) {
			capi_free(wdt_handle);
			return -ENOMEM;
		}

		wdt_handle->priv = wdt_priv;
		wdt_handle->init_allocated = true;
	} else {
		wdt_handle = *handle;

		if (!wdt_handle->priv)
			return -EINVAL;

		wdt_priv = wdt_handle->priv;

		wdt_handle->init_allocated = false;
	}

	wdt_priv->id = config->identifier;
	wdt_priv->callback = config->callback;
	wdt_priv->regs = (config->identifier == 0) ? MXC_WDT0 : MXC_WDT1;
	wdt_priv->irqn = (config->identifier == 0) ? WDT0_IRQn : WDT1_IRQn;
	wdt_priv->clock_freq_hz = _max_capi_wdt_get_clock_freq();
	if (wdt_priv->clock_freq_hz == 0) {
		ret = -EINVAL;
		goto free_handle;
	}

	rst_flags = MXC_WDT_GetResetFlag(wdt_priv->regs);

	memset(&wdt_config, 0, sizeof(wdt_config));
	wdt_config.upperIntPeriod = MXC_WDT_PERIOD_2_31;
	wdt_config.upperResetPeriod = MXC_WDT_PERIOD_2_31;
	ret = MXC_WDT_Init(wdt_priv->regs, &wdt_config);
	MXC_WDT_SetIntPeriod(wdt_priv->regs, &wdt_config);
	MXC_WDT_SetResetPeriod(wdt_priv->regs, &wdt_config);
	MXC_WDT_DisableInt(wdt_priv->regs);
	MXC_WDT_DisableReset(wdt_priv->regs);
	MXC_WDT_Disable(wdt_priv->regs);
	MXC_WDT_ClearResetFlag(wdt_priv->regs);
	if (ret)
		goto free_handle;

	/** Execute the WDT feed sequence and disable the WDT */
	__disable_irq();
	MXC_WDT_Disable(wdt_priv->regs);

	/** Set WDTn_CTRL.clk_rdy_ie = 1 to generate a
	 * WDT-enabled interrupt event */
	wdt_priv->regs->ctrl |= MXC_F_WDT_CTRL_CLKRDY_IE;

	/** Re-enable global interrupts */
	__enable_irq();

	/* The MAX32690 watchdog has no clock-source select; it runs from PCLK. */

	/** Restore reset flags */
	wdt_priv->regs->ctrl |= rst_flags;

	wdt_priv->configured = false;
	wdt_priv->enabled = false;
	wdt_priv->irq_connected = false;

	wdt_handle->ops = config->ops;

	wdt[config->identifier] = wdt_handle;
	*handle = wdt_handle;

	return 0;

free_handle:
	if (wdt_handle->init_allocated) {
		capi_free(wdt_priv);
		capi_free(wdt_handle);
	}

	wdt[config->identifier] = NULL;

	return ret;
}

/**
 * @brief Deinitialize the WDT peripheral
 * @param handle The WDT handle
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_wdt_deinit(struct capi_wdt_handle *handle)
{
	const struct max_capi_wdt_priv *wdt_priv;
	uint8_t id;
	int ret;

	if (!handle || !handle->priv)
		return -EINVAL;

	wdt_priv = handle->priv;
	id = wdt_priv->id;

	ret = MXC_WDT_Shutdown(wdt_priv->regs);

	if (wdt_priv->irq_connected)
		capi_irq_disable(wdt_priv->irqn);

	if (handle->init_allocated) {
		capi_free(handle->priv);
		capi_free(handle);
	}

	wdt[id] = NULL;

	return ret;
}

/**
 * @brief Get available channels for WDT. In the case of MAX32690, it is single
 * 	  channel, and will return 1 as per CAPI.
 * @param handle The WDT handle
 * @param channels Where to store the number of channels.
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_wdt_get_chan_count(struct capi_wdt_handle *handle,
				       int *channels)
{
	if (!handle || !channels)
		return -EINVAL;

	*channels = 1;

	return 0;
}

/**
 * @brief Set up a WDT channel. Does not start the WDT.
 * 	  WDT starts with the first feed.
 * @param handle The WDT handle
 * @param chan_id The channel ID - must be 0
 * @param chan_config The configuration struct
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_wdt_setup_chan(struct capi_wdt_handle *handle, int chan_id,
				   const struct capi_wdt_chan_config *chan_config)
{
	int ret;
	struct max_capi_wdt_priv *wdt_priv;
	struct max_capi_wdt_chan_extra *wdt_chan_extra;
	enum max_capi_wdt_period late_period;
	enum max_capi_wdt_mode mode = MAX_CAPI_WDT_MODE_COMPATIBILITY;
	mxc_wdt_cfg_t wdt_config;

	if (!handle || !handle->priv || !chan_config)
		return -EINVAL;

	if (chan_id != 0)
		return -EINVAL;

	wdt_priv = handle->priv;

	/**
	 * Configure the standard thresholds
	 * If using the optional windowed WDT feature...
	 */
	memset(&wdt_config, 0, sizeof(wdt_config));
	if (chan_config->extra) {
		wdt_chan_extra = chan_config->extra;
		wdt_config.upperIntPeriod = (mxc_wdt_period_t)wdt_chan_extra->late_interrupt;
		wdt_config.upperResetPeriod = (mxc_wdt_period_t)wdt_chan_extra->late_reset;
		wdt_config.lowerIntPeriod = (mxc_wdt_period_t)wdt_chan_extra->early_interrupt;
		wdt_config.lowerResetPeriod = (mxc_wdt_period_t)wdt_chan_extra->early_reset;
		mode = wdt_chan_extra->mode;
	} else if (chan_config->timeout_us > 0) {
		ret = _max_capi_wdt_us_to_period(chan_config->timeout_us,
						 wdt_priv->clock_freq_hz,
						 &late_period);
		if (ret)
			return ret;

		wdt_config.upperIntPeriod = (mxc_wdt_period_t)late_period;
		wdt_config.upperResetPeriod = (mxc_wdt_period_t)late_period;
	} else {
		return -EINVAL;
	}

	MXC_WDT_SetIntPeriod(wdt_priv->regs, &wdt_config);
	MXC_WDT_SetResetPeriod(wdt_priv->regs, &wdt_config);
	if (mode == MAX_CAPI_WDT_MODE_WINDOWED)
		wdt_priv->regs->ctrl |= MXC_F_WDT_CTRL_WIN_EN;
	else
		wdt_priv->regs->ctrl &= ~MXC_F_WDT_CTRL_WIN_EN;

	if (chan_config->irq_enabled) {
		if (!wdt_priv->irq_connected) {
			ret = capi_irq_connect(wdt_priv->irqn,
					       (capi_isr_callback_t)max_capi_wdt_isr,
					       handle);
			if (ret)
				return ret;

			ret = capi_irq_enable(wdt_priv->irqn);
			if (ret)
				return ret;

			wdt_priv->irq_connected = true;
		}

		/**
		 * Set WDTn_CTRL.wdt_int_en to generate an interrupt when a
		 * WDT late interrupt event occurs...
		 */
		MXC_WDT_EnableInt(wdt_priv->regs);
		MXC_WDT_DisableReset(wdt_priv->regs);
	} else {
		if (wdt_priv->irq_connected) {
			capi_irq_disable(wdt_priv->irqn);
			wdt_priv->irq_connected = false;
		}

		/**
		 * Set WDTn_CTRL.wdt_rst_en to generate an interrupt when a
		 * WDT late reset event occurs...
		 */
		MXC_WDT_EnableReset(wdt_priv->regs);
		MXC_WDT_DisableInt(wdt_priv->regs);
	}

	wdt_priv->configured = true;
	wdt_priv->enabled = false;

	return 0;
}

/**
 * @brief Disable a channel. Feed is required to restart.
 * @param handle The WDT handle
 * @param chan_id The channel ID - must be 0
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_wdt_disable_chan(struct capi_wdt_handle *handle,
				     int chan_id)
{
	struct max_capi_wdt_priv *wdt_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	if (chan_id != 0)
		return -EINVAL;

	wdt_priv = handle->priv;

	if (wdt_priv->enabled) {
		MXC_WDT_Disable(wdt_priv->regs);
		wdt_priv->enabled = false;
	}

	return 0;
}

/**
 * @brief Feed (restart) a channel
 * @param handle The WDT handle
 * @param chan_id The channel ID - must be 0
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_wdt_feed_chan(struct capi_wdt_handle *handle, int chan_id)
{
	struct max_capi_wdt_priv *wdt_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	if (chan_id != 0)
		return -EINVAL;

	wdt_priv = handle->priv;

	if (!wdt_priv->configured)
		return -EAGAIN;

	if (wdt_priv->enabled) {
		MXC_WDT_ResetTimer(wdt_priv->regs);
	} else {
		/** Execute the WDT feed sequence and enable the WDT */
		__disable_irq();
		MXC_WDT_Enable(wdt_priv->regs);

		/** Verify the peripheral is enabled before proceeding */
		while (!(wdt_priv->regs->ctrl & MXC_F_WDT_CTRL_CLKRDY));

		/**
		 * Set WDTn_CTRL.clkrdy_ie = 1 to generate WDT-enabled
		 * event interrupt
		 */
		wdt_priv->regs->ctrl |= MXC_F_WDT_CTRL_CLKRDY_IE;

		/** Re-enable global interrupts */
		__enable_irq();

		wdt_priv->enabled = true;
	}

	return 0;
}

/**
 * @brief The ISR for the WDT peripheral
 * @param handle The WDT handle
 */
static void max_capi_wdt_isr(struct capi_wdt_handle *handle)
{
	struct capi_wdt_handle *wdt_handle = handle;
	struct max_capi_wdt_priv *wdt_priv;
	uint32_t flags;
	bool windowed_mode;

	if (!wdt_handle || !wdt_handle->priv)
		return;

	wdt_priv = wdt_handle->priv;

	windowed_mode = wdt_priv->regs->ctrl & MXC_F_WDT_CTRL_WIN_EN;
	flags = 0;
	if (windowed_mode) {
		if (wdt_priv->regs->ctrl & MXC_F_WDT_CTRL_INT_EARLY)
			flags |= MAX_CAPI_WDT_FLAG_INT_EARLY;
		if (wdt_priv->regs->ctrl & MXC_F_WDT_CTRL_RST_EARLY)
			flags |= MAX_CAPI_WDT_FLAG_RST_EARLY;
	}
	if (wdt_priv->regs->ctrl & MXC_F_WDT_CTRL_INT_LATE)
		flags |= MAX_CAPI_WDT_FLAG_INT_LATE;
	if (wdt_priv->regs->ctrl & MXC_F_WDT_CTRL_RST_LATE)
		flags |= MAX_CAPI_WDT_FLAG_RST_LATE;

	if (flags && wdt_priv->callback)
		wdt_priv->callback(0, handle, flags);

	/* Acknowledge the interrupt; reset-cause flags are left for the app */
	MXC_WDT_ClearIntFlag(wdt_priv->regs);
}

struct capi_wdt_ops max_capi_wdt_ops = {
	.init = max_capi_wdt_init,
	.deinit = max_capi_wdt_deinit,
	.get_chan_count = max_capi_wdt_get_chan_count,
	.setup_chan = max_capi_wdt_setup_chan,
	.disable_chan = max_capi_wdt_disable_chan,
	.feed_chan = max_capi_wdt_feed_chan,
	.isr = max_capi_wdt_isr,
};

/** Platform-specific functions ***********************************************/

/**
 * @brief Get the interrupt and reset flags from the WDT peripheral
 * @param handle The WDT handle
 * @param flags Where to store the flags
 * @return 0 on success, negative error code otherwise
 */
int max_capi_wdt_get_flags(struct capi_wdt_handle *handle, uint32_t *flags)
{
	const struct max_capi_wdt_priv *wdt_priv;

	if (!handle || !handle->priv || !flags)
		return -EINVAL;

	wdt_priv = handle->priv;
	*flags = 0;

	if (wdt_priv->regs->ctrl & MXC_F_WDT_CTRL_INT_LATE)
		*flags |= MAX_CAPI_WDT_FLAG_INT_LATE;

	if (wdt_priv->regs->ctrl & MXC_F_WDT_CTRL_INT_EARLY)
		*flags |= MAX_CAPI_WDT_FLAG_INT_EARLY;

	if (wdt_priv->regs->ctrl & MXC_F_WDT_CTRL_RST_LATE)
		*flags |= MAX_CAPI_WDT_FLAG_RST_LATE;

	if (wdt_priv->regs->ctrl & MXC_F_WDT_CTRL_RST_EARLY)
		*flags |= MAX_CAPI_WDT_FLAG_RST_EARLY;

	return 0;
}

/**
 * @brief Clear the interrupt and reset flags from the WDT peripheral
 * @param handle The WDT handle
 * @return 0 on success, negative error code otherwise
 */
int max_capi_wdt_clear_flags(struct capi_wdt_handle *handle)
{
	const struct max_capi_wdt_priv *wdt_priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	wdt_priv = handle->priv;

	MXC_WDT_ClearIntFlag(wdt_priv->regs);
	MXC_WDT_ClearResetFlag(wdt_priv->regs);

	return 0;
}
