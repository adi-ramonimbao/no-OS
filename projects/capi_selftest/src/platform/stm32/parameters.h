/***************************************************************************//**
 * @file parameters.h
 * @brief Definitions specific to STM32 platform used by capi_selftest project.
 * Copyright (c) 2025-2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include "stm32_hal.h"
#include "stm32_capi_uart.h"
#include "stm32_capi_gpio.h"
#include "stm32_capi_spi.h"
#include "stm32_capi_irq.h"
#include "stm32_capi_timer.h"
#include "stm32_capi_i2c.h"
#include "stm32_capi_dma.h"
#include "capi_uart.h"

#if defined(STM32F469xx)
extern UART_HandleTypeDef huart5;
#else
extern UART_HandleTypeDef huart3;
#endif
extern SPI_HandleTypeDef hspi1;

#define UART_IDENTIFIER		0U
#define UART_OPS		&stm32_capi_uart_ops
#define UART_BAUDRATE		115200U
#define UART_EXTRA_TYPE		struct stm32_uart_extra_config
#if defined(STM32F469xx)
#define UART_EXTRA_INIT		{ .huart = &huart5 }	/* SDP-CK1Z console UART5 */
#else
#define UART_EXTRA_INIT		{ .huart = &huart3 }	/* NUCLEO-F767ZI console USART3 */
#endif
#define PLATFORM_NAME		"STM32"

/*
 * GPIO loopback pair.
 *
 * NUCLEO-F767ZI: PE0 (output, bit 0) <-> PC0 (input, bit 0). Both endpoints are
 *   bit 0, so the port-wide bitmask tests and the pin tests share one wire and
 *   PC0 drives EXTI0 for the IRQ suite.
 *   Jumper: PE0 (CN10/D34) <-> PC0 (CN9, Arduino A1).
 *
 * SDP-CK1Z (EVAL-SDP-CK1Z / SDP-K1) -- Arduino Uno header only, for easy
 *   jumpering: PA0 (output, D1, bit 0) <-> PC1 (input, A3, bit 1). The IRQ test
 *   drives the output port's bit 0, so the output pin must be bit 0 (PA0 is the
 *   only bit-0 pin on the header); the input is a different pin (PC1) and drives
 *   EXTI1. The two pins sit at different bit positions, so the port-wide bitmask
 *   tests are disabled (GPIO_HAS_PORT_LOOPBACK 0) and only the pin tests run.
 *   Jumper: PA0 (Arduino D1) <-> PC1 (Arduino A3).
 */
#if defined(STM32F469xx)
#define GPIO_OUTPUT_IDENTIFIER		((uint64_t)(uintptr_t)GPIOA)
#define GPIO_OUTPUT_NAME		"PA0"
#define GPIO_INPUT_IDENTIFIER		((uint64_t)(uintptr_t)GPIOC)
#define GPIO_INPUT_NAME			"PC1"
#else
#define GPIO_OUTPUT_IDENTIFIER		((uint64_t)(uintptr_t)GPIOE)
#define GPIO_OUTPUT_NAME		"PE0"
#define GPIO_INPUT_IDENTIFIER		((uint64_t)(uintptr_t)GPIOC)
#define GPIO_INPUT_NAME			"PC0"
#endif
#define GPIO_OUTPUT_NUM_PINS		1U
#define GPIO_OUTPUT_OPS			&stm32_capi_gpio_ops
#define GPIO_OUTPUT_EXTRA		struct stm32_capi_gpio_port_config
#define GPIO_OUTPUT_EXTRA_INIT		{ .mode = GPIO_MODE_OUTPUT_PP, \
					  .speed = GPIO_SPEED_FREQ_LOW, \
					  .alternate = 0U, \
					  .pull = GPIO_NOPULL }

#define GPIO_INPUT_NUM_PINS		1U
#define GPIO_INPUT_OPS			&stm32_capi_gpio_ops
#define GPIO_INPUT_EXTRA		struct stm32_capi_gpio_port_config
#define GPIO_INPUT_EXTRA_INIT		{ .mode = GPIO_MODE_INPUT, \
					  .speed = GPIO_SPEED_FREQ_LOW, \
					  .alternate = 0U, \
					  .pull = GPIO_NOPULL }

/*
 * The STM32 CAPI GPIO backend has no toggle op, so the toggle subtests are
 * skipped on this platform.
 */
#define GPIO_HAS_TOGGLE		0

/*
 * Pin-level loopback: pin numbers are physical bit indices within the port.
 * NUCLEO-F767ZI wires PE0 (bit 0) <-> PC0 (bit 0). SDP-CK1Z wires PA0 (bit 0)
 * <-> PC1 (bit 1) and disables the port-wide bitmask tests, whose readback
 * assumes the output and input pins share a bit position.
 */
#if defined(STM32F469xx)
#define GPIO_HAS_PORT_LOOPBACK	0
#define GPIO_OUTPUT_PIN_NUMBERS	{ 0U }
#define GPIO_INPUT_PIN_NUMBERS	{ 1U }
#else
#define GPIO_OUTPUT_PIN_NUMBERS	{ 0U }
#define GPIO_INPUT_PIN_NUMBERS	{ 0U }
#endif
#define GPIO_HAS_PIN_LOOPBACK	1

/* SPI async delivery mode selection. */
#define SPI_HAS_IRQ  1
#define SPI_HAS_DMA  0

/* IRQ controller — NVIC, no base address and no extra config needed. */
#define IRQ_CTRL_IDENTIFIER		0U
#define IRQ_CTRL_EXTRA			NULL

/*
 * SPI1 external loopback.
 *   NUCLEO-F767ZI: PA5 = SCK, PA6 = MISO, PA7 = MOSI; wire PA7 <-> PA6.
 *   SDP-CK1Z:      PB3 = SCK, PB4 = MISO, PA7 = MOSI; wire PA7 <-> PB4
 *                  (Arduino D11 MOSI <-> D12 MISO).
 */
#define SPI_IDENTIFIER		((uint64_t)(uintptr_t)SPI1)
#define SPI_OPS			&stm32_capi_spi_ops
#define SPI_EXTRA_TYPE		struct stm32_spi_extra_config
#define SPI_EXTRA_INIT		{ .hspi = &hspi1, \
				  .get_input_clock = NULL, \
				  .alternate = 0U, \
				  .dma_handle = NULL, \
				  .rxdma_ch_id = 0U, \
				  .txdma_ch_id = 0U, \
				  .irq_num = SPI1_IRQn }
#if defined(STM32F469xx)
#define SPI_CLK_FREQ		22500000U	/* SPI1 on APB2 @ 22.5 MHz (SDP-CK1Z) */
#else
#define SPI_CLK_FREQ		96000000U	/* SPI1 on APB2 @ 96 MHz (NUCLEO-F767ZI) */
#endif

#define SPI_DEVICE_NATIVE_CS	0x01U
#define SPI_DEVICE_MODE		CAPI_SPI_MODE_0
#define SPI_DEVICE_SPEED_HZ	1000000U

/*
 * TIM2 on NUCLEO-F767ZI: 32-bit general-purpose timer on APB1.
 * The driver uses identifier=2 to select TIM2 via get_timer_base_from_identifier()
 * and auto-detects the APB1 clock. output_freq_hz=1 MHz gives 1 us resolution.
 */
#define TIMER_IDENTIFIER	2U
#define TIMER_OPS		&stm32_capi_timer_ops
#define TIMER_INPUT_CLK_HZ	0U		/* auto-detected from APB1 */
#define TIMER_OUTPUT_FREQ_HZ	1000000U	/* 1 MHz -> 1 us resolution */
#define TIMER_EXTRA_TYPE	struct stm32_capi_timer_extra_config
#define TIMER_EXTRA_INIT	{ .htim = NULL, \
				  .get_input_clock = NULL, \
				  .irq_num = TIM2_IRQn }

#define TIMER_DIRECTION		CAPI_TIMER_COUNT_UP
/*
 * Counter wrap point. Although TIM2 is 32 bits wide, the rollover period must
 * sit between two test windows: wider than the BASIC rate window (10 ms) so a
 * rate sample never straddles more than one wrap, yet narrower than the
 * ASYNC_IRQ overflow timeout (1 s) so the counter-overflow interrupt actually
 * fires within it. At the 1 MHz output rate a full 0xFFFFFFFF span rolls over
 * only every ~71 min, so ASYNC_IRQ never sees an overflow; 0x1FFFF gives a
 * ~131 ms period (13x the rate window, ~7x under the IRQ timeout).
 *
 * TIMER_RATE_COUNTER_MASK must equal TIMER_COUNTER_MAX so the BASIC delta
 * (second - first) & mask stays correct across a wrap; that requires max+1 to
 * be a power of two, which 0x1FFFF satisfies.
 */
#define TIMER_COUNTER_MAX	0x1FFFFU
#define TIMER_COMPARE_VALUE	0x8000U
#define TIMER_RATE_WINDOW_US	10000U
#define TIMER_RATE_COUNTER_MASK	0x1FFFFU
#define TIMER_RATE_TOLERANCE_PCT 5U
#define TIMER_HAS_IRQ		1
#define TIMER_HAS_COMPARE	1

/*
 * I2C initiator/target loopback. CubeMX only sets up the initiator (I2C1), so
 * i2c_platform_init() brings up the target bus (clock, pins, NVIC) and installs
 * the IRQ vectors that dispatch to capi_i2c_isr; the test calls
 * I2C_PLATFORM_SET_TARGET() after init so those vectors reach the target handle.
 *
 * NUCLEO-F767ZI: initiator I2C1 (PB6/PB9) <-> target I2C2 (PB10/PB11).
 *
 * The SDP-CK1Z (SDP-K1) Arduino header exposes only one I2C bus (I2C1 on
 * D14/D15); the F469 has no second I2C peripheral that muxes onto header pins,
 * so a jumpered dual-bus loopback is not possible there. I2C is therefore left
 * unmapped on that board and test_i2c compiles out to a skipping stub.
 */
#if !defined(STM32F469xx)
#define I2C_IDENTIFIER		1U
#define I2C_OPS			&stm32_capi_i2c_ops
#define I2C_EXTRA_TYPE		struct stm32_i2c_extra_config
#define I2C_EXTRA_INIT		{ .hi2c = NULL, .i2c_timing = 0x20303E5D }
#define I2C_TARGET_ADDR		0x42U
#define I2C_HAS_IRQ		0

#define I2C_TARGET_IDENTIFIER	2U
#define I2C_TARGET_OPS		&stm32_capi_i2c_ops
#define I2C_TARGET_EXTRA_TYPE	struct stm32_i2c_extra_config
#define I2C_TARGET_EXTRA_INIT	{ .hi2c = NULL, .i2c_timing = 0x20303E5D }

struct capi_i2c_controller_handle;
int i2c_platform_init(void);
void i2c_platform_deinit(void);
void i2c_platform_set_target_handle(struct capi_i2c_controller_handle *handle);
#define I2C_PLATFORM_INIT()		i2c_platform_init()
#define I2C_PLATFORM_DEINIT()		i2c_platform_deinit()
#define I2C_PLATFORM_SET_TARGET(h)	i2c_platform_set_target_handle(h)
#endif /* !STM32F469xx */

/*
 * DMA2 on NUCLEO-F767ZI: only DMA2 supports memory-to-memory transfers.
 * Stream 0, channel 0 is used (no peripheral trigger needed for mem-to-mem).
 * Polling mode (irq_num=0): the driver blocks in xfer_start until the
 * transfer completes — no interrupt infrastructure required.
 */
#define DMA_OPS			&stm32_capi_dma_ops
#define DMA_IDENTIFIER		0U
#define DMA_NUM_CHANS		1U
#define DMA_XFER_EXTRA_TYPE	struct stm32_dma_chan_extra_config
#define DMA_XFER_EXTRA_INIT	{ .hdma = &(DMA_HandleTypeDef){ \
					.Instance = DMA2_Stream0 }, \
				  .ch_num = DMA_CHANNEL_0, \
				  .mem_increment = true, \
				  .per_increment = true, \
				  .mem_data_alignment = CAPI_DMA_DATA_ALIGN_BYTE, \
				  .per_data_alignment = CAPI_DMA_DATA_ALIGN_BYTE, \
				  .dma_mode = CAPI_DMA_NORMAL_MODE, \
				  .trig = NULL }
#define DMA_PLATFORM_INIT()	__HAL_RCC_DMA2_CLK_ENABLE()
#define DMA_XFER_SIZE		64U

/*
 * The STM32 CAPI DMA backend is polling-only: it implements no
 * register_complete_callback op, so the CAPI wrapper returns -EINVAL and no
 * completion interrupt can be delivered. xfer_start() blocks until the transfer
 * finishes. The DMA ASYNC case is therefore skipped on this platform.
 */
#define DMA_HAS_IRQ		0

/* Largest buffer the DMA sizes sweep / increment cases allocate (bytes). */
#define DMA_MAX_XFER_SIZE	256U

#endif /* __PARAMETERS_H__ */
