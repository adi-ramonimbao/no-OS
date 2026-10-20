/***************************************************************************//**
 *   @file   maxim_capi_irq.c
 *   @brief  Implementation of IRQ functions with CAPI.
 *   @author Edelweise Escala (edelweise.escala@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#include <string.h>
#include <errno.h>
#include "maxim_capi_irq.h"
#include "max32690.h"
#include "gpio.h"
#include "uart.h"
#include "tmr.h"
#include "dma.h"
#include "spi.h"
#include "i2c.h"
#include "wdt.h"

/**
 * @def MAX_CAPI_IRQ_NUM_GPIO_PORTS
 * @brief Number of GPIO ports owning a dedicated NVIC line (GPIO0..GPIO3).
 *
 * Port 4 is excluded: it has no GPIOn_IRQn of its own, and
 * MXC_GPIO_GET_IRQ(4) resolves to the shared GPIOWAKE_IRQn.
 */
#define MAX_CAPI_IRQ_NUM_GPIO_PORTS	4U

/** Static variables **********************************************************/

static bool irq_initialized = false;
static struct max_capi_irq_entry irq_table[MXC_IRQ_COUNT];
static uint32_t default_priority = 0;

/** Helper functions ***********************************************************/

/**
 * @brief Check if IRQ is valid
 * @param irq - the IRQ to check
 * @return true if valid, false if not
 */
static inline bool _max_capi_irq_is_valid_irq(uint32_t irq)
{
	return irq < MXC_IRQ_COUNT;
}

/**
 * @brief Check whether an IRQ is already enabled in the NVIC
 *
 * The CMSIS core_cm4.h bundled with the MSDK predates NVIC_GetEnableIRQ(), so
 * read the Interrupt Set Enable Register directly. Core exceptions (negative
 * IRQn) are not NVIC-maskable and are reported as disabled.
 *
 * @param irq - the IRQ to query
 * @return true if the line is enabled, false otherwise
 */
static inline bool _max_capi_irq_is_enabled(uint32_t irq)
{
	int32_t irqn = (int32_t)irq;

	if (irqn < 0)
		return false;

	return (NVIC->ISER[((uint32_t)irqn) >> 5U] &
		(1UL << (((uint32_t)irqn) & 0x1FUL))) != 0UL;
}

/**
 * @brief Resolve a GPIO port interrupt to its port index
 * @param irq - the IRQ to look up
 * @param port_idx - pointer receiving the port index when matched
 * @return true if the IRQ is a GPIO port interrupt, false otherwise
 */
static bool _max_capi_irq_get_gpio_port(uint32_t irq, uint32_t *port_idx)
{
	uint32_t i;

	for (i = 0; i < MAX_CAPI_IRQ_NUM_GPIO_PORTS; i++) {
		if (irq == (uint32_t)MXC_GPIO_GET_IRQ(i)) {
			if (port_idx)
				*port_idx = i;
			return true;
		}
	}

	return false;
}

/**
 * @brief Check if IRQ is a GPIO port interrupt
 * @param irq - the IRQ to check
 * @return true if a GPIO port IRQ, false if not
 */
static inline bool _max_capi_irq_is_gpio_irq(uint32_t irq)
{
	return _max_capi_irq_get_gpio_port(irq, NULL);
}

/** Function implementations **************************************************/

/**
 * @brief Initialize the IRQ
 * @param config - IRQ configuration struct
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_init(struct capi_irq_config *config)
{
	const struct max_capi_irq_extra_config *extra;

	if (!config)
		return -EINVAL;

	if (irq_initialized)
		return -EBUSY;

	/* Clear callback table */
	memset(irq_table, 0, sizeof(irq_table));

	if (config->extra) {
		extra = config->extra;
		default_priority = extra->default_priority;
	}

	irq_initialized = true;

	return 0;
}

/**
 * @brief Deinitialize the IRQ
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_deinit(void)
{
	uint32_t i;

	if (!irq_initialized)
		return -EINVAL;

	for (i = 0; i < MXC_IRQ_COUNT; i++) {
		if (irq_table[i].enabled) {
			NVIC_DisableIRQ((IRQn_Type)i);
			irq_table[i].enabled = false;
		}
		irq_table[i].callback = NULL;
		irq_table[i].arg = NULL;
	}

	irq_initialized = false;

	return 0;
}

/**
 * @brief Enable the IRQ globally
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_global_enable(void)
{
	if (!irq_initialized)
		return -EINVAL;

	__enable_irq();
	return 0;
}

/**
 * @brief Disable the IRQ globally
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_global_disable(void)
{
	if (!irq_initialized)
		return -EINVAL;

	__disable_irq();
	return 0;
}

/**
 * @brief Enable an IRQ
 * @param irq - the IRQ number to enable
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_enable(uint32_t irq)
{
	if (!irq_initialized)
		return -EINVAL;

	if (!_max_capi_irq_is_valid_irq(irq))
		return -EINVAL;

	if (!irq_table[irq].enabled && !_max_capi_irq_is_enabled(irq))
		NVIC_SetPriority(irq, default_priority);

	NVIC_EnableIRQ(irq);
	irq_table[irq].enabled = true;

	return 0;
}

/**
 * @brief Disable an IRQ
 * @param irq - the IRQ to disable
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_disable(uint32_t irq)
{
	if (!irq_initialized)
		return -EINVAL;

	if (!_max_capi_irq_is_valid_irq(irq))
		return -EINVAL;

	NVIC_DisableIRQ(irq);
	irq_table[irq].enabled = false;

	return 0;
}

/**
 * @brief Connect an IRQ to a callback. Does not automatically enable it.
 * @param irq - the IRQ to connect to
 * @param isr - the callback function to connect to the IRQ
 * @param arg - the arguments to pass to the callback function
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_connect(uint32_t irq, capi_isr_callback_t isr,
				void *arg)
{
	uint32_t i;

	if (!irq_initialized)
		return -EINVAL;

	if (!isr)
		return -EINVAL;

	if (!_max_capi_irq_is_valid_irq(irq))
		return -EINVAL;

	irq_table[irq].callback = isr;
	irq_table[irq].arg = arg;

	/* Unmask the timer's own interrupt when connecting a timer line. */
	for (i = 0; i < MXC_CFG_TMR_INSTANCES; i++) {
		if (irq == (uint32_t)MXC_TMR_GET_IRQ(i)) {
			MXC_TMR_EnableInt(MXC_TMR_GET_TMR(i));
			break;
		}
	}

	return 0;
}

/**
 * @brief Clear a pending IRQ
 * @param irq - the IRQ to clear
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_clear_pending(uint32_t irq)
{
	if (!irq_initialized)
		return -EINVAL;

	if (!_max_capi_irq_is_valid_irq(irq))
		return -EINVAL;

	NVIC_ClearPendingIRQ(irq);

	return 0;
}

/**
 * @brief Get the status of an IRQ
 * @param irq - the IRQ
 * @param pactive - pointer to where the status will be stored
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_get_status(uint32_t irq, uint32_t *pactive)
{
	if (!irq_initialized)
		return -EINVAL;

	if (!_max_capi_irq_is_valid_irq(irq) || !pactive)
		return -EINVAL;

	*pactive = NVIC_GetPendingIRQ(irq) || NVIC_GetActive(irq);

	return 0;
}

/**
 * @brief Set the priority of an IRQ
 * @param irq - the IRQ
 * @param priority - the priority
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_set_priority(uint32_t irq, uint32_t priority)
{
	if (!irq_initialized)
		return -EINVAL;

	if (!_max_capi_irq_is_valid_irq(irq))
		return -EINVAL;

	NVIC_SetPriority(irq, priority);

	return 0;
}

/**
 * @brief Get the priority of an IRQ
 * @param irq - the IRQ
 * @param priority - pointer to where the priority will be stored
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_get_priority(uint32_t irq, uint32_t *priority)
{
	if (!irq_initialized)
		return -EINVAL;

	if (!_max_capi_irq_is_valid_irq(irq) || !priority)
		return -EINVAL;

	*priority = NVIC_GetPriority(irq);

	return 0;
}

/**
 * @brief Set the level/edge trigger for all pins on a GPIO port IRQ. If other
 * 	  IRQs are specified, the function will return -ENOTSUP.
 * @param irq - the IRQ
 * @param trigger - the level/edge trigger
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_irq_set_level_edge_trigger(uint32_t irq,
		enum capi_irq_trig_level trigger)
{
	mxc_gpio_cfg_t config;
	uint32_t port_idx;
	int32_t trig;

	if (!irq_initialized)
		return -EINVAL;

	if (!_max_capi_irq_is_valid_irq(irq))
		return -EINVAL;

	if (!_max_capi_irq_get_gpio_port(irq, &port_idx))
		return -ENOTSUP;

	switch (trigger) {
	case CAPI_IRQ_EDGE_RISING:
		trig = MXC_GPIO_INT_RISING;
		break;
	case CAPI_IRQ_EDGE_FALLING:
		trig = MXC_GPIO_INT_FALLING;
		break;
	case CAPI_IRQ_EDGE_BOTH:
		trig = MXC_GPIO_INT_BOTH;
		break;
	case CAPI_IRQ_LEVEL_LOW:
		trig = MXC_GPIO_INT_LOW;
		break;
	case CAPI_IRQ_LEVEL_HIGH:
		trig = MXC_GPIO_INT_HIGH;
		break;
	default:
		return -EINVAL;
	}

	config = (mxc_gpio_cfg_t) {
		.port = MXC_GPIO_GET_GPIO(port_idx),
		/**
		 * Let's set the trigger for all the pins for now since there is
		 * no mechanism to select the pin with capi_irq_set_level_edge_trigger
		 **/
		.mask = 0xFFFFFFFF,
		.func = MXC_GPIO_FUNC_IN,
	};
	MXC_GPIO_IntConfig(&config, trig);

	return 0;
}

/** Peripheral IRQ Handlers ***************************************************/

/*
 * The handlers below override the weak defaults in startup_max32690.S. Every
 * instance of a peripheral the CAPI backend can drive needs its own handler:
 * a missing one silently falls back to the weak default, which presents as a
 * hang rather than a link error.
 */

static void _uart_handler(uint32_t idx)
{
	uint32_t irq = MXC_UART_GET_IRQ(idx);

	if (irq_table[irq].callback)
		irq_table[irq].callback(irq_table[irq].arg);
	else
		MXC_UART_AsyncHandler(MXC_UART_GET_UART(idx));
}

void UART0_IRQHandler(void)
{
	_uart_handler(0);
}

void UART1_IRQHandler(void)
{
	_uart_handler(1);
}

void UART2_IRQHandler(void)
{
	_uart_handler(2);
}

void UART3_IRQHandler(void)
{
	_uart_handler(3);
}

static void _dma_handler(uint32_t channel)
{
	uint32_t irq = MXC_DMA_CH_GET_IRQ(channel);

	if (irq_table[irq].callback)
		irq_table[irq].callback(irq_table[irq].arg);

	/* MAX32690 has a single DMA controller; the handler takes no argument. */
	MXC_DMA_Handler();
}

void DMA0_IRQHandler(void)
{
	_dma_handler(0);
}

void DMA1_IRQHandler(void)
{
	_dma_handler(1);
}

void DMA2_IRQHandler(void)
{
	_dma_handler(2);
}

void DMA3_IRQHandler(void)
{
	_dma_handler(3);
}

void DMA4_IRQHandler(void)
{
	_dma_handler(4);
}

void DMA5_IRQHandler(void)
{
	_dma_handler(5);
}

void DMA6_IRQHandler(void)
{
	_dma_handler(6);
}

void DMA7_IRQHandler(void)
{
	_dma_handler(7);
}

void DMA8_IRQHandler(void)
{
	_dma_handler(8);
}

void DMA9_IRQHandler(void)
{
	_dma_handler(9);
}

void DMA10_IRQHandler(void)
{
	_dma_handler(10);
}

void DMA11_IRQHandler(void)
{
	_dma_handler(11);
}

void DMA12_IRQHandler(void)
{
	_dma_handler(12);
}

void DMA13_IRQHandler(void)
{
	_dma_handler(13);
}

void DMA14_IRQHandler(void)
{
	_dma_handler(14);
}

void DMA15_IRQHandler(void)
{
	_dma_handler(15);
}

static void _timer_handler(mxc_tmr_regs_t *tmr)
{
	uint32_t irq = MXC_TMR_GET_IRQ(MXC_TMR_GET_IDX(tmr));

	if (irq_table[irq].callback)
		irq_table[irq].callback(irq_table[irq].arg);

	MXC_TMR_ClearFlags(tmr);
}

void TMR0_IRQHandler(void)
{
	_timer_handler(MXC_TMR0);
}

void TMR1_IRQHandler(void)
{
	_timer_handler(MXC_TMR1);
}

void TMR2_IRQHandler(void)
{
	_timer_handler(MXC_TMR2);
}

void TMR3_IRQHandler(void)
{
	_timer_handler(MXC_TMR3);
}

void TMR4_IRQHandler(void)
{
	_timer_handler(MXC_TMR4);
}

void TMR5_IRQHandler(void)
{
	_timer_handler(MXC_TMR5);
}

static void _gpio_handler(uint32_t idx)
{
	mxc_gpio_regs_t *port = MXC_GPIO_GET_GPIO(idx);
	uint32_t irq = MXC_GPIO_GET_IRQ(idx);
	uint32_t flags = MXC_GPIO_GetFlags(port);

	if (irq_table[irq].callback)
		irq_table[irq].callback(irq_table[irq].arg);

	/* Clear only the flags latched at entry, after dispatch: the PINT path
	 * needs them set so MXC_GPIO_Handler can pick the per-pin callback. */
	MXC_GPIO_ClearFlags(port, flags);
}

void GPIO0_IRQHandler(void)
{
	_gpio_handler(0);
}

void GPIO1_IRQHandler(void)
{
	_gpio_handler(1);
}

void GPIO2_IRQHandler(void)
{
	_gpio_handler(2);
}

void GPIO3_IRQHandler(void)
{
	_gpio_handler(3);
}

static void _spi_handler(uint32_t idx)
{
	uint32_t irq = MXC_SPI_GET_IRQ(idx);

	if (irq_table[irq].callback)
		irq_table[irq].callback(irq_table[irq].arg);
	else
		MXC_SPI_ClearFlags(MXC_SPI_GET_SPI(idx));
}

void SPI0_IRQHandler(void)
{
	_spi_handler(0);
}

void SPI1_IRQHandler(void)
{
	_spi_handler(1);
}

void SPI2_IRQHandler(void)
{
	_spi_handler(2);
}

#if (MXC_SPI_INSTANCES > 3)
void SPI3_IRQHandler(void)
{
	_spi_handler(3);
}

void SPI4_IRQHandler(void)
{
	_spi_handler(4);
}
#endif /* MXC_SPI_INSTANCES > 3 */

static void _i2c_handler(uint32_t idx)
{
	uint32_t irq = MXC_I2C_GET_IRQ(idx);

	if (irq_table[irq].callback)
		irq_table[irq].callback(irq_table[irq].arg);
	else
		MXC_I2C_AsyncHandler(MXC_I2C_GET_I2C(idx));
}

void I2C0_IRQHandler(void)
{
	_i2c_handler(0);
}

void I2C1_IRQHandler(void)
{
	_i2c_handler(1);
}

void I2C2_IRQHandler(void)
{
	_i2c_handler(2);
}

static void _wdt_handler(uint32_t irq, mxc_wdt_regs_t *wdt)
{
	if (wdt->ctrl & (MXC_F_WDT_CTRL_CLKRDY_IE | MXC_F_WDT_CTRL_CLKRDY))
		wdt->ctrl &= ~MXC_F_WDT_CTRL_CLKRDY_IE;

	if (irq_table[irq].callback)
		irq_table[irq].callback(irq_table[irq].arg);
}

void WDT0_IRQHandler(void)
{
	_wdt_handler((uint32_t)WDT0_IRQn, MXC_WDT0);
}

void WDT1_IRQHandler(void)
{
	_wdt_handler((uint32_t)WDT1_IRQn, MXC_WDT1);
}

void USB_IRQHandler(void)
{
	uint32_t irq = (uint32_t)USB_IRQn;

	if (irq_table[irq].callback)
		irq_table[irq].callback(irq_table[irq].arg);
}

/** CAPI functions implementation **********************************************/

int capi_irq_init(struct capi_irq_config *config)
{
	return max_capi_irq_init(config);
}

int capi_irq_deinit(void)
{
	return max_capi_irq_deinit();
}

int capi_irq_global_enable(void)
{
	return max_capi_irq_global_enable();
}

int capi_irq_global_disable(void)
{
	return max_capi_irq_global_disable();
}

int capi_irq_enable(uint32_t irq)
{
	return max_capi_irq_enable(irq);
}

int capi_irq_disable(uint32_t irq)
{
	return max_capi_irq_disable(irq);
}

int capi_irq_connect(uint32_t irq, capi_isr_callback_t isr, void *arg)
{
	return max_capi_irq_connect(irq, isr, arg);
}

int capi_irq_clear_pending(uint32_t irq)
{
	return max_capi_irq_clear_pending(irq);
}

int capi_irq_get_status(uint32_t irq, uint32_t *pactive)
{
	return max_capi_irq_get_status(irq, pactive);
}

int capi_irq_set_priority(uint32_t irq, uint32_t priority)
{
	return max_capi_irq_set_priority(irq, priority);
}

int capi_irq_get_priority(uint32_t irq, uint32_t *priority)
{
	return max_capi_irq_get_priority(irq, priority);
}

int capi_irq_set_level_edge_trigger(uint32_t irq,
				    enum capi_irq_trig_level trigger)
{
	return max_capi_irq_set_level_edge_trigger(irq, trigger);
}
