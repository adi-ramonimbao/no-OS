/***************************************************************************//**
 * @file parameters.h
 * @brief Definitions specific to Maxim platforms used by capi_selftest project.
 * Copyright (c) 2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 ******************************************************************************/

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include "maxim_capi_uart.h"
#include "maxim_capi_gpio.h"
#include "maxim_capi_spi.h"
#include "maxim_capi_irq.h"
#include "maxim_capi_timer.h"
#include "maxim_capi_dma.h"
#include "maxim_capi_i2c.h"
#include "capi_uart.h"
#ifdef CONFIG_CAPI_USB_UART
#include "maxim_capi_usb_uart.h"
#endif

#if (TARGET_NUM == 32657)

#define UART_IDENTIFIER		0U
#define UART_OPS		&max_capi_uart_ops
#define UART_BAUDRATE		115200
#define UART_EXTRA_TYPE		struct max_capi_uart_extra
#define UART_EXTRA_INIT		{ .use_irq = true }
#define PLATFORM_NAME		"MAX32657"

/**
 * GPIO loopback pair on MAX32657EVKIT:
 * 	P0.7 (output) wired to P0.8 (input)
 * Requires the on-board jumpers on JP15 to be removed.
 */
#define GPIO_OUTPUT_IDENTIFIER	0U
#define GPIO_OUTPUT_NUM_PINS	9U
#define GPIO_OUTPUT_OPS		&max_capi_gpio_ops
#define GPIO_OUTPUT_NAME	"P0.7"
#define GPIO_OUTPUT_EXTRA	struct max_capi_gpio_extra_config
#define GPIO_OUTPUT_EXTRA_INIT	{ .func = MAX_CAPI_GPIO_FUNC_OUT }

#define GPIO_INPUT_IDENTIFIER	0U
#define GPIO_INPUT_NUM_PINS	9U
#define GPIO_INPUT_OPS		&max_capi_gpio_ops
#define GPIO_INPUT_NAME		"P0.8"
#define GPIO_INPUT_EXTRA	struct max_capi_gpio_extra_config
#define GPIO_INPUT_EXTRA_INIT	{ .func = MAX_CAPI_GPIO_FUNC_IN }

#define GPIO_HAS_PORT_LOOPBACK	0
#define GPIO_HAS_PIN_LOOPBACK	1

#define GPIO_OUTPUT_PIN_NUMBERS	{ 7 }
#define GPIO_INPUT_PIN_NUMBERS	{ 8 }

/*
 * SPI async delivery mode. SPI_HAS_IRQ and SPI_HAS_DMA are mutually exclusive:
 * the loopback test configures exactly one delivery path per build. MAX32657
 * uses IRQ/FIFO delivery here; DMA delivery would need IRQ-driven DMA
 * completion, which the shared dma_config (extra = NULL) does not wire.
 */
#define SPI_HAS_IRQ	1
#define SPI_HAS_DMA	0

/* IRQ controller - NVIC, no base address needed. */
#define IRQ_CTRL_IDENTIFIER	0U

/**
 * SPI0 on MAX32657EVKIT:
 * 	P0.6 = SCLK, P0.4 = MISO, P0.2 = MOSI
 * 	External loopback requires P0.4 physically wired to P0.2.
 */
#define SPI_IDENTIFIER		0U
#define SPI_OPS			&max_capi_spi_ops
#define SPI_EXTRA_TYPE		struct max_capi_spi_extra
#define SPI_EXTRA_INIT		{ .device_role = MAX_CAPI_SPI_DEVICE_ROLE_CONTROLLER, \
				  .bus_width = MAX_CAPI_SPI_BUS_WIDTH_STANDARD, \
				  .num_targets = 1, \
				  .polarity_mask = 0b111, \
				  .chip_select = MAX_CAPI_SPI_CS0, \
				  .clock_phase = MAX_CAPI_SPI_CLOCK_PHASE_0, \
				  .clock_polarity = MAX_CAPI_SPI_CLOCK_POLARITY_0, \
				  .use_irq = true }
#define SPI_CLK_FREQ		25000000

#define SPI_DEVICE_NATIVE_CS	MAX_CAPI_SPI_CS0
#define SPI_DEVICE_MODE		CAPI_SPI_MODE_0
#define SPI_DEVICE_SPEED_HZ	100000

/**
 * TIM0 on MAX32657EVKIT: 32-bit general purpose timer
 */
#define TIMER_IDENTIFIER		0U
#define TIMER_OPS			&max_capi_timer_ops
#define TIMER_INPUT_CLK_HZ		0
#define TIMER_OUTPUT_FREQ_HZ		1000000U
#define TIMER_EXTRA_TYPE		struct max_capi_timer_extra
#define TIMER_EXTRA_INIT		{ .use_irq = true }

#define TIMER_DIRECTION			CAPI_TIMER_COUNT_UP
#define TIMER_COUNTER_MAX		0x40000U
#define TIMER_COMPARE_VALUE		0x8000U
#define TIMER_RATE_WINDOW_US		10000U
#define TIMER_RATE_COUNTER_MASK		0xFFFFFFFFU
#define TIMER_RATE_TOLERANCE_PCT	5U
#define TIMER_HAS_IRQ			1
#define TIMER_HAS_COMPARE		1

/**
 * I2C on MAX32657EVKIT
 *
 * Initiator only: the part has a single I2C-capable block, so no second
 * controller can be wired as a target and the I2C group collapses to one
 * skipping BASIC.
 */
#define I2C_IDENTIFIER		0U
#define I2C_OPS			&max_capi_i2c_ops
#define I2C_EXTRA_TYPE		struct max_capi_i2c_extra
#define I2C_EXTRA_INIT		{ .use_irq = true }
#define I2C_TARGET_ADDR		0x42
#define I2C_HAS_IRQ		1

/**
 * DMA1_S on MAX32657EVKIT:
 */
#define DMA_OPS			&max_capi_dma_ops
#define DMA_IDENTIFIER		0U
#define DMA_NUM_CHANS		4U
#define DMA_XFER_EXTRA_TYPE	struct max_capi_dma_xfer_extra
#define DMA_XFER_EXTRA_INIT	{ .reqsel = MAX_CAPI_DMA_REQUEST_MEMTOMEM }
#define DMA_PLATFORM_INIT()
#define DMA_XFER_SIZE		64U
#define DMA_CONFIG_EXTRA	&(struct max_capi_dma_extra){ .use_irq = true }

/** IRQ */
#define IRQ_CTRL_EXTRA		&(struct max_capi_irq_extra_config) {}

#elif (TARGET_NUM == 32690)

/* Console: UART2 (P1.9/P1.10) is the one routed to the EVKIT USB-UART bridge. */
#define UART_IDENTIFIER		2U
#define UART_OPS		&max_capi_uart_ops
#define UART_BAUDRATE		115200
#define UART_EXTRA_TYPE		struct max_capi_uart_extra
#define UART_EXTRA_INIT		{ .use_irq = true }
#define PLATFORM_NAME		"MAX32690"

/*
 * UART loopback runner for test_uart.c (must NOT be the console UART).
 * MAX32690EVKIT: use UART0 on P2.11 (RX) / P2.12 (TX) and strap them together.
 * P2.12 also drives LED1, so the LED flickers while the UART group runs.
 */
#define UART_ASYNC_IDENTIFIER	0U
#define UART_ASYNC_OPS		&max_capi_uart_ops
#define UART_ASYNC_EXTRA_TYPE	struct max_capi_uart_extra
#define UART_ASYNC_EXTRA_INIT	{ .vssel = MAX_CAPI_GPIO_VSSEL_VDDIOH, .use_irq = true }

#define UART_ASYNC_HAS_IRQ		1
#define UART_ASYNC_HAS_LINE_CONFIG	1
#define UART_ASYNC_HAS_IRQ_CTL		1
#define UART_ASYNC_HAS_RX_TIMEOUT	0

#ifdef CONFIG_CAPI_USB_UART
/* USB CDC-ACM UART on the MAX32690EVKIT high-speed USB connector */
#define USB_UART_IDENTIFIER	0U	/* ignored: single USB controller */
#define USB_UART_OPS		&max_capi_usb_uart_ops
#define USB_UART_EXTRA_TYPE	struct max_capi_usb_uart_extra
#define USB_UART_EXTRA_INIT	{ .vid = MAX_CAPI_USB_UART_DEFAULT_VID, \
				  .pid = MAX_CAPI_USB_UART_DEFAULT_PID }
#endif /* CONFIG_CAPI_USB_UART */

#define GPIO_OUTPUT_IDENTIFIER	0U
#define GPIO_OUTPUT_NUM_PINS	1U
#define GPIO_OUTPUT_OPS		&max_capi_gpio_ops
#define GPIO_OUTPUT_NAME	"P0.9"
#define GPIO_OUTPUT_EXTRA	struct max_capi_gpio_extra_config
#define GPIO_OUTPUT_EXTRA_INIT	{ .func = MAX_CAPI_GPIO_FUNC_OUT, .pin_offset = 9U }

/*
 * GPIO loopback pair on MAX32690EVKIT: P0.9 (output) wired to P0.8 (input).
 * P2.12 is taken by UART0 TX for the UART loopback.
 */
#define GPIO_INPUT_IDENTIFIER	0U
#define GPIO_INPUT_NUM_PINS	1U
#define GPIO_INPUT_OPS		&max_capi_gpio_ops
#define GPIO_INPUT_NAME		"P0.8"
#define GPIO_INPUT_EXTRA	struct max_capi_gpio_extra_config
#define GPIO_INPUT_EXTRA_INIT	{ .func = MAX_CAPI_GPIO_FUNC_IN, .pin_offset = 8U }

/*
 * Output and input share port 0, but each handle covers a single pin via
 * pin_offset and writes are masked, so port-wide writes only touch P0.9.
 */
#define GPIO_HAS_PORT_LOOPBACK	1
#define GPIO_HAS_PIN_LOOPBACK	1

#define GPIO_OUTPUT_PIN_NUMBERS	{ 0 }
#define GPIO_INPUT_PIN_NUMBERS	{ 0 }
#define GPIO_INPUT_IRQ_PIN	8U

/*
 * SPI async delivery mode. SPI_HAS_IRQ and SPI_HAS_DMA are mutually exclusive:
 * the loopback test configures exactly one delivery path per build.
 */
#define SPI_HAS_IRQ	0
#define SPI_HAS_DMA	1

/* IRQ controller - NVIC, no base address needed. */
#define IRQ_CTRL_IDENTIFIER	0U

/**
 * SPI0 on MAX32690EVKIT:
 * 	P2.29 = SCK, P2.27 = MISO, P2.28 = MOSI, P0.22 = SS0
 * 	External loopback requires P2.27 physically wired to P2.28.
 */
#define SPI_IDENTIFIER		0U
#define SPI_OPS			&max_capi_spi_ops
#define SPI_EXTRA_TYPE		struct max_capi_spi_extra
#define SPI_EXTRA_INIT		{ .device_role = MAX_CAPI_SPI_DEVICE_ROLE_CONTROLLER, \
				  .bus_width = MAX_CAPI_SPI_BUS_WIDTH_STANDARD, \
				  .num_targets = 1, \
				  .polarity_mask = 0b111, \
				  .chip_select = MAX_CAPI_SPI_CS0, \
				  .clock_phase = MAX_CAPI_SPI_CLOCK_PHASE_0, \
				  .clock_polarity = MAX_CAPI_SPI_CLOCK_POLARITY_0, \
				  .use_irq = true }
#define SPI_CLK_FREQ		60000000

#define SPI_DEVICE_NATIVE_CS	MAX_CAPI_SPI_CS0
#define SPI_DEVICE_MODE		CAPI_SPI_MODE_0
#define SPI_DEVICE_SPEED_HZ	100000

/**
 * TMR0 on MAX32690EVKIT: 32-bit general purpose timer.
 *
 * Clocked straight from PCLK (60 MHz with the default 120 MHz core) at
 * prescaler 1. At 1 MHz the prescaler rounds to /64 and one tick spans ~128
 * CPU cycles, so BASIC's two back-to-back counter reads land in the same tick
 * and COUNTER_MOVED fails; at 60 MHz a tick is two CPU cycles.
 *
 * TIMER_COUNTER_MAX must be one less than a power of two so the BASIC rate
 * delta stays correct across a wrap, and the wrap period must sit between the
 * rate window (10 ms) and the ASYNC_IRQ overflow timeout (1 s) so an overflow
 * actually fires: 0x1FFFFFF at 60 MHz is ~559 ms.
 */
#define TIMER_IDENTIFIER		0U
#define TIMER_OPS			&max_capi_timer_ops
#define TIMER_INPUT_CLK_HZ		0
#define TIMER_OUTPUT_FREQ_HZ		60000000U
#define TIMER_EXTRA_TYPE		struct max_capi_timer_extra
#define TIMER_EXTRA_INIT		{ .use_irq = true }

#define TIMER_DIRECTION			CAPI_TIMER_COUNT_UP
#define TIMER_COUNTER_MAX		0x1FFFFFFU
#define TIMER_COMPARE_VALUE		0x8000U
#define TIMER_RATE_WINDOW_US		10000U
#define TIMER_RATE_COUNTER_MASK		0x1FFFFFFU
#define TIMER_RATE_TOLERANCE_PCT	5U
#define TIMER_HAS_IRQ			1
#define TIMER_HAS_COMPARE		1

/**
 * I2C initiator/target loopback on MAX32690EVKIT:
 * 	Initiator = I2C0 (P2.7, P2.8)
 * 	Target    = I2C2 (P1.7, P1.8)
 *
 * Both sides are pinned explicitly to VDDIO (1.8 V) rather than relying on
 * max_capi_i2c_extra's VDDIO default, so the level is unambiguous here if the
 * bus needs to move to VDDIOH later.
 */
#define I2C_IDENTIFIER		0U
#define I2C_OPS			&max_capi_i2c_ops
#define I2C_EXTRA_TYPE		struct max_capi_i2c_extra
#define I2C_EXTRA_INIT		{ .vssel = MAX_CAPI_GPIO_VSSEL_VDDIO, .use_irq = true }
#define I2C_TARGET_ADDR		0x42
#define I2C_HAS_IRQ		1

#define I2C_TARGET_IDENTIFIER	2U
#define I2C_TARGET_OPS		&max_capi_i2c_ops
#define I2C_TARGET_EXTRA_TYPE	struct max_capi_i2c_extra
#define I2C_TARGET_EXTRA_INIT	{ .vssel = MAX_CAPI_GPIO_VSSEL_VDDIO, .use_irq = true }

#define I2C_TARGET_HAS_IRQ	1

/*
 * The CAPI I2C backend enables each instance's peripheral clock and muxes its
 * own pins during init (see maxim_capi_i2c.c), and routes target-mode events
 * through the shared I2Cn_IRQHandler, so no extra board bring-up is needed
 * here. The STM32 port needs these hooks because CubeMX only generates the
 * MSP for one of its two I2C instances.
 */
#define I2C_PLATFORM_INIT()		0
#define I2C_PLATFORM_DEINIT()		((void)0)
#define I2C_PLATFORM_SET_TARGET(h)	((void)(h))

/**
 * DMA on MAX32690EVKIT: single controller, 16 channels, memory-to-memory.
 */
#define DMA_OPS			&max_capi_dma_ops
#define DMA_IDENTIFIER		0U
#define DMA_NUM_CHANS		16U
#define DMA_XFER_EXTRA_TYPE	struct max_capi_dma_xfer_extra
#define DMA_XFER_EXTRA_INIT	{ .reqsel = MAX_CAPI_DMA_REQUEST_MEMTOMEM }
#define DMA_PLATFORM_INIT()
#define DMA_XFER_SIZE		64U
#define DMA_CONFIG_EXTRA	&(struct max_capi_dma_extra){ .use_irq = true }

/** IRQ */
#define IRQ_CTRL_EXTRA		&(struct max_capi_irq_extra_config) {}

#elif (TARGET_NUM == 32655)

/* Console: UART0 (P0.0/P0.1) is the one routed to the FTHR DAPLink USB-UART. */
#define UART_IDENTIFIER		0U
#define UART_OPS		&max_capi_uart_ops
#define UART_BAUDRATE		115200
#define UART_EXTRA_TYPE		struct max_capi_uart_extra
#define UART_EXTRA_INIT		{ .use_irq = true }
#define PLATFORM_NAME		"MAX32655"

/*
 * UART loopback runner for test_uart.c (must NOT be the console UART).
 * MAX32655FTHR: UART3 (LPUART) is the Feather RX (P2.6) / TX (P2.7) pair;
 * strap them together. It runs from IBRO, the driver's default for UART3.
 */
#define UART_ASYNC_IDENTIFIER	3U
#define UART_ASYNC_OPS		&max_capi_uart_ops
#define UART_ASYNC_EXTRA_TYPE	struct max_capi_uart_extra
#define UART_ASYNC_EXTRA_INIT	{ .vssel = MAX_CAPI_GPIO_VSSEL_VDDIO, .use_irq = true }

#define UART_ASYNC_HAS_IRQ		1
#define UART_ASYNC_HAS_LINE_CONFIG	1
#define UART_ASYNC_HAS_IRQ_CTL		1
#define UART_ASYNC_HAS_RX_TIMEOUT	0

/*
 * GPIO loopback pair on MAX32655FTHR: P1.6 (Feather D13, output) wired to
 * P1.7 (Feather D12, input). Both pins share port 1, but each handle covers a
 * single pin via pin_offset and writes are masked, so port-wide writes only
 * touch P1.6.
 */
#define GPIO_OUTPUT_IDENTIFIER	1U
#define GPIO_OUTPUT_NUM_PINS	1U
#define GPIO_OUTPUT_OPS		&max_capi_gpio_ops
#define GPIO_OUTPUT_NAME	"P1.6"
#define GPIO_OUTPUT_EXTRA	struct max_capi_gpio_extra_config
#define GPIO_OUTPUT_EXTRA_INIT	{ .func = MAX_CAPI_GPIO_FUNC_OUT, .pin_offset = 6U }

#define GPIO_INPUT_IDENTIFIER	1U
#define GPIO_INPUT_NUM_PINS	1U
#define GPIO_INPUT_OPS		&max_capi_gpio_ops
#define GPIO_INPUT_NAME		"P1.7"
#define GPIO_INPUT_EXTRA	struct max_capi_gpio_extra_config
#define GPIO_INPUT_EXTRA_INIT	{ .func = MAX_CAPI_GPIO_FUNC_IN, .pin_offset = 7U }

#define GPIO_HAS_PORT_LOOPBACK	1
#define GPIO_HAS_PIN_LOOPBACK	1

#define GPIO_OUTPUT_PIN_NUMBERS	{ 0 }
#define GPIO_INPUT_PIN_NUMBERS	{ 0 }
#define GPIO_INPUT_IRQ_PIN	7U

/*
 * SPI async delivery mode. SPI_HAS_IRQ and SPI_HAS_DMA are mutually exclusive:
 * the loopback test configures exactly one delivery path per build.
 */
#define SPI_HAS_IRQ	1
#define SPI_HAS_DMA	0

/* IRQ controller - NVIC, no base address needed. */
#define IRQ_CTRL_IDENTIFIER	0U

/**
 * SPI1 on MAX32655FTHR (the Feather SPI header):
 * 	P0.23 = SCK, P0.22 = MISO, P0.21 = MOSI, P0.20 = SS0
 * 	External loopback requires P0.21 (J9.12) physically wired to P0.22
 * 	(J9.13).
 */
#define SPI_IDENTIFIER		1U
#define SPI_OPS			&max_capi_spi_ops
#define SPI_EXTRA_TYPE		struct max_capi_spi_extra
#define SPI_EXTRA_INIT		{ .device_role = MAX_CAPI_SPI_DEVICE_ROLE_CONTROLLER, \
				  .bus_width = MAX_CAPI_SPI_BUS_WIDTH_STANDARD, \
				  .num_targets = 1, \
				  .polarity_mask = 0b111, \
				  .chip_select = MAX_CAPI_SPI_CS0, \
				  .clock_phase = MAX_CAPI_SPI_CLOCK_PHASE_0, \
				  .clock_polarity = MAX_CAPI_SPI_CLOCK_POLARITY_0, \
				  .use_irq = true }
#define SPI_CLK_FREQ		25000000

#define SPI_DEVICE_NATIVE_CS	MAX_CAPI_SPI_CS0
#define SPI_DEVICE_MODE		CAPI_SPI_MODE_0
#define SPI_DEVICE_SPEED_HZ	100000

/**
 * TMR0 on MAX32655FTHR: 32-bit general purpose timer.
 *
 * Clocked straight from PCLK (50 MHz with the default 100 MHz core) at
 * prescaler 1, so a tick spans two CPU cycles and BASIC's back-to-back counter
 * reads see it move.
 *
 * TIMER_COUNTER_MAX must be one less than a power of two so the BASIC rate
 * delta stays correct across a wrap, and the wrap period must sit between the
 * rate window (10 ms) and the ASYNC_IRQ overflow timeout (1 s) so an overflow
 * actually fires: 0x1FFFFFF at 50 MHz is ~671 ms.
 */
#define TIMER_IDENTIFIER		0U
#define TIMER_OPS			&max_capi_timer_ops
#define TIMER_INPUT_CLK_HZ		0
#define TIMER_OUTPUT_FREQ_HZ		50000000U
#define TIMER_EXTRA_TYPE		struct max_capi_timer_extra
#define TIMER_EXTRA_INIT		{ .use_irq = true }

#define TIMER_DIRECTION			CAPI_TIMER_COUNT_UP
#define TIMER_COUNTER_MAX		0x1FFFFFFU
#define TIMER_COMPARE_VALUE		0x8000U
#define TIMER_RATE_WINDOW_US		10000U
#define TIMER_RATE_COUNTER_MASK		0x1FFFFFFU
#define TIMER_RATE_TOLERANCE_PCT	5U
#define TIMER_HAS_IRQ			1
#define TIMER_HAS_COMPARE		1

/**
 * I2C2 on MAX32655FTHR (the Feather SCL P0.30 / SDA P0.31 header pins).
 *
 * Initiator only: I2C1 is the on-board MAX20303 PMIC bus and I2C0 is not
 * brought out, so no second controller can be wired as a target and the I2C
 * group collapses to one skipping BASIC.
 */
#define I2C_IDENTIFIER		2U
#define I2C_OPS			&max_capi_i2c_ops
#define I2C_EXTRA_TYPE		struct max_capi_i2c_extra
#define I2C_EXTRA_INIT		{ .vssel = MAX_CAPI_GPIO_VSSEL_VDDIOH, .use_irq = true }
#define I2C_TARGET_ADDR		0x42
#define I2C_HAS_IRQ		1

/**
 * DMA on MAX32655FTHR: single controller, 4 channels, memory-to-memory.
 */
#define DMA_OPS			&max_capi_dma_ops
#define DMA_IDENTIFIER		0U
#define DMA_NUM_CHANS		4U
#define DMA_XFER_EXTRA_TYPE	struct max_capi_dma_xfer_extra
#define DMA_XFER_EXTRA_INIT	{ .reqsel = MAX_CAPI_DMA_REQUEST_MEMTOMEM }
#define DMA_PLATFORM_INIT()
#define DMA_XFER_SIZE		64U
#define DMA_CONFIG_EXTRA	&(struct max_capi_dma_extra){ .use_irq = true }

/** IRQ */
#define IRQ_CTRL_EXTRA		&(struct max_capi_irq_extra_config) {}

#else
#error "Unsupported Maxim target for capi_selftest (expected 32655, 32657 or 32690)."
#endif /* TARGET_NUM */

#endif /* __PARAMETERS_H__ */
