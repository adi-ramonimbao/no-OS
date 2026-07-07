/***************************************************************************//**
 * @file parameters.h
 * @brief Definitions specific to Xilinx platform used by capi_selftest project.
 * Copyright (c) 2025-2026 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include <xparameters.h>
#include "capi_uart.h"
#include "xilinx_capi_uart.h"
#include "xilinx_capi_gpio.h"
#include "xilinx_capi_spi.h"
#include "xilinx_capi_timer.h"
#include "xilinx_capi_i2c.h"
#include "xilinx_capi_irq.h"
#include "capi_timer.h"
#include "xinterrupt_wrap.h"

/*
 * ======================= BACKEND SELECTOR PANEL =======================
 * One place to force which hardware backend each peripheral role maps to.
 * Uncomment a line to pin that role; leave it commented to auto-detect the
 * form present in the BSP (the per-peripheral blocks further down do the
 * auto-detect when no override is defined here). The comment after each knob
 * lists the accepted values.
 *
 * These must be defined BEFORE the per-peripheral selection blocks below,
 * which is why the panel sits at the very top of the file.
 */
/* #define GPIO_SEL_PS */		/* GPIO_SEL_PS / GPIO_SEL_PL */
/* #define IRQ_SEL_GIC */		/* IRQ_SEL_GIC / IRQ_SEL_CASCADE */
#define SPI_SEL_PS			/* SPI_SEL_PS / SPI_SEL_PL */
/* #define TIMER_SELECT 1 */		/* 1=TTC / 2=AXI / 3=SCU (TIMER_SEL_*) */
/* #define I2C_SEL_PL	*/		/* I2C_SEL_PS / I2C_SEL_PL (initiator) */
/* #define I2C_TARGET_SEL_PS */		/* I2C_TARGET_SEL_PS / I2C_TARGET_SEL_PL */
/* ====================================================================== */

#define UART_IDENTIFIER		XPAR_XUARTPS_0_BASEADDR

#define UART_OPS		&capi_uart_xilinx_ps_ops
#define UART_BAUDRATE		115200U
#define UART_EXTRA_TYPE		struct capi_uart_xilinx_config
#define UART_EXTRA_INIT		{ .use_irq = false }
#define PLATFORM_NAME		"XILINX"

/*
 * GPIO backend selection. Two loopback paths are supported:
 *
 *   PS GPIO (XGpioPs, EMIO routed to a PMOD):
 *     One EMIO pin drives, an adjacent one reads back — wired together on the
 *     board. Uses base_pin (the global EMIO pin index).
 *
 *   PL GPIO (XGpio, dual-channel AXI GPIO loopback core):
 *     Channel 1 (output) and channel 2 (input) wired together in HDL. Drive
 *     channel 1, read back on channel 2. Uses channel. Carries an interrupt
 *     (ip2intc_irpt) so the GPIO-IRQ test has a source.
 *
 * PS is preferred when XGpioPs exists in the BSP; otherwise fall back to the PL
 * AXI GPIO. Define GPIO_SEL_PL / GPIO_SEL_PS before this point to force one.
 * The PS base_pin values below are board-specific: set them to the wired EMIO
 * loopback pair for your board.
 */
#if !defined(GPIO_SEL_PS) && !defined(GPIO_SEL_PL)
#if defined(XPAR_XGPIOPS_NUM_INSTANCES) || defined(XPAR_XGPIOPS_0_BASEADDR)
#define GPIO_SEL_PS
#elif defined(XPAR_XGPIO_NUM_INSTANCES) || defined(XPAR_XGPIO_1_BASEADDR)
#define GPIO_SEL_PL
#endif
/* No GPIO instance in the BSP: leave GPIO_*_OPS undefined so the GPIO test
 * compiles out (common_data.h gates on #ifdef GPIO_OUTPUT_OPS). */
#endif

#if defined(GPIO_SEL_PS)

#define GPIO_OUTPUT_IDENTIFIER	XPAR_XGPIOPS_0_BASEADDR
#define GPIO_OUTPUT_NUM_PINS	1U
#define GPIO_OUTPUT_OPS		&capi_gpio_xilinx_ps_ops
#define GPIO_OUTPUT_EXTRA	struct capi_gpio_xilinx_ps_config
#define GPIO_OUTPUT_EXTRA_INIT	\
	{ .base_pin = 55U }

#define GPIO_INPUT_IDENTIFIER	XPAR_XGPIOPS_0_BASEADDR
#define GPIO_INPUT_NUM_PINS	1U
#define GPIO_INPUT_OPS		&capi_gpio_xilinx_ps_ops
#define GPIO_INPUT_EXTRA	struct capi_gpio_xilinx_ps_config
#define GPIO_INPUT_EXTRA_INIT	\
	{ .base_pin = 54U }

#elif defined(GPIO_SEL_PL)

/*
 * PL loopback core is a single dual-channel AXI GPIO: both ports share one
 * base address, distinguished by channel (1 = output, 2 = input).
 */
#define GPIO_OUTPUT_IDENTIFIER	XPAR_XGPIO_1_BASEADDR
#define GPIO_OUTPUT_NUM_PINS	1U
#define GPIO_OUTPUT_OPS		&capi_gpio_xilinx_pl_ops
#define GPIO_OUTPUT_EXTRA	struct capi_gpio_xilinx_pl_config
#define GPIO_OUTPUT_EXTRA_INIT	\
	{ .channel = 1U }

#define GPIO_INPUT_IDENTIFIER	XPAR_XGPIO_1_BASEADDR
#define GPIO_INPUT_NUM_PINS	1U
#define GPIO_INPUT_OPS		&capi_gpio_xilinx_pl_ops
#define GPIO_INPUT_EXTRA	struct capi_gpio_xilinx_pl_config
#define GPIO_INPUT_EXTRA_INIT	\
	{ .channel = 2U }

#endif /* GPIO_SEL_* */

/*
 * Pin-level loopback: pin numbers are port-local indices (0..num_pins-1); the
 * driver maps index i to physical pin base_pin+i (PS) or channel bit i (PL).
 * The single loopback pair is index 0 on each 1-pin port.
 */
#define GPIO_HAS_PIN_LOOPBACK	1
#define GPIO_OUTPUT_PIN_NUMBERS	{ 0U }
#define GPIO_INPUT_PIN_NUMBERS	{ 0U }

/*
 * Physical (global) input pin the GPIO-IRQ hooks configure directly through
 * XGpioPs. This is the EMIO line the test samples for its edge interrupt;
 * board-specific, match it to the wired input of the loopback pair.
 */
#define GPIO_INPUT_PIN		54U

/*
 * IRQ controller topology, selected from the BSP. IRQ_CTRL_IDENTIFIER is the
 * root controller capi_irq_init() brings up; the IRQ test and every IRQ-backed
 * async peripheral initialize through it. IRQ_CTRL_EXTRA is config->extra.
 *
 *   1. Cascade (GIC + AXI INTC) - GIC root, INTC sub-controller. extra = &cascade
 *   2. PS GIC only              - GIC is root, no PL INTC.        extra = NULL
 *   3. AXI INTC only            - MicroBlaze root, no GIC.        extra = NULL
 *      !! NOT DESIGNED YET, NEEDS SPECIAL MAILBOX INTERMEDIATE
 *
 * Note the AXI INTC's presence macro is XPAR_XINTC_NUM_INSTANCES (the canonical
 * XIntc name the SDT BSP emits), NOT XPAR_AXI_INTC_NUM_INSTANCES.
 *
 * When both controllers exist the hardware is a cascade (the AXI INTC's output
 * is a GIC SPI), so cascade is the default. Define IRQ_SEL_GIC before this
 * point to force GIC-only regardless (the INTC is then left unused by the API).
 */
#if !defined(IRQ_SEL_GIC) && !defined(IRQ_SEL_CASCADE)
#if defined(XPAR_XSCUGIC_NUM_INSTANCES) && defined(XPAR_XINTC_NUM_INSTANCES)
#define IRQ_SEL_CASCADE
#endif
#endif

#if defined(IRQ_SEL_CASCADE)
/* Cascade: the GIC owns the API as root, the AXI INTC hangs off it. */
#define IRQ_CTRL_IDENTIFIER	XPAR_XSCUGIC_0_BASEADDR
#define IRQ_CTRL_EXTRA		(&(struct capi_irq_xilinx_extra) { \
			.subctrl = &xilinx_capi_irq_intc_subctrl, \
			.subctrl_ctrl_id = XPAR_AXI_INTC_0_BASEADDR, \
			.cascade_gic_irq = CAPI_IRQ_XILINX_CASCADE_AUTO })
#elif defined(XPAR_XSCUGIC_NUM_INSTANCES)
/* PS GIC only. On Zynq the GIC is always present, so a polled ("noirq") build
 * still lands here; the GPIO-IRQ test skips at runtime when no fabric IRQ is
 * wired (main.c returns -ENOTSUP), not by leaving the root undefined. */
#define IRQ_CTRL_IDENTIFIER	XPAR_XSCUGIC_0_BASEADDR
#define IRQ_CTRL_EXTRA		NULL
#elif defined(XPAR_XINTC_NUM_INSTANCES)
/* AXI INTC only (MicroBlaze root). */
#define IRQ_CTRL_IDENTIFIER	XPAR_AXI_INTC_0_BASEADDR
#define IRQ_CTRL_EXTRA		NULL
#else
/* No interrupt controller in the BSP: leave IRQ_CTRL_IDENTIFIER undefined so
 * the IRQ test and every IRQ-backed async path compile out. */
#endif

/*
 * SPI async delivery mode selection.
 *
 * SPI_HAS_IRQ is derived after the backend block below, from whether the BSP
 * actually describes an interrupt for the SELECTED controller -- it cannot be
 * pinned here because which controller that is has not been decided yet.
 * SPI_HAS_DMA stays a manual switch (no BSP evidence distinguishes it).
 */
#define SPI_HAS_DMA  0   /* async via DMA available */

/*
 * SPI backend selection, mirroring the GPIO scheme:
 *
 *   PS SPI (XSpiPs, SPI0 EMIO routed to a PMOD):
 *     SCLK / MOSI / MISO / SS0 (CS0) on the PMOD. External loopback needs MOSI
 *     wired to MISO. 3 native CS: CS0, CS1, CS2. The PS SPI interrupt is a
 *     fixed PS SPI, always a GIC id.
 *
 *   PL SPI (XSpi, AXI Quad SPI): base at XPAR_XSPI_0_BASEADDR; its fabric line
 *     feeds the GIC (SPI) or the AXI INTC input depending on the build, chosen
 *     from XPAR_XSPI_0_INTERRUPT_PARENT low bit (1 = INTC).
 *
 * PS is preferred when XSpiPs exists in the BSP; otherwise fall back to the PL
 * AXI SPI. Define SPI_SEL_PL / SPI_SEL_PS before this point to force one
 * (see the BACKEND SELECTOR PANEL at the top of this file).
 */
#if !defined(SPI_SEL_PS) && !defined(SPI_SEL_PL)
#if defined(XPAR_XSPIPS_NUM_INSTANCES) || defined(XPAR_XSPIPS_0_BASEADDR)
#define SPI_SEL_PS
#elif defined(XPAR_XSPI_NUM_INSTANCES) || defined(XPAR_XSPI_0_BASEADDR)
#define SPI_SEL_PL
#endif
#endif

#if defined(SPI_SEL_PS)

#define SPI_IDENTIFIER		XPAR_XSPIPS_0_BASEADDR
#define SPI_OPS			&capi_spi_xilinx_ps_ops
#define SPI_EXTRA_TYPE		struct capi_spi_xilinx_config
#if defined(XPAR_XSPIPS_0_INTERRUPTS)
#define SPI_IRQ_ID		(XGet_IntrId(XPAR_XSPIPS_0_INTERRUPTS) + \
				 XGet_IntrOffset(XPAR_XSPIPS_0_INTERRUPTS))
#define SPI_EXTRA_INIT		{ .use_irq = true, \
				  .irq_id = CAPI_IRQ_XILINX_GIC(SPI_IRQ_ID) }
#else
/* No interrupt entry in the BSP (polled build): sync transfers only. */
#define SPI_EXTRA_INIT		{ .use_irq = false }
#endif /* XPAR_XSPIPS_0_INTERRUPTS */

#elif defined(SPI_SEL_PL)

#define SPI_IDENTIFIER		XPAR_XSPI_0_BASEADDR
#define SPI_OPS			&capi_spi_xilinx_pl_ops
#define SPI_EXTRA_TYPE		struct capi_spi_xilinx_config
/*
 * An XSA built without fabric interrupts emits no XPAR_XSPI_0_INTERRUPTS at
 * all, so the presence of that macro -- not its value -- is what decides
 * whether an IRQ exists. Testing XPAR_XSPI_0_INTERRUPT_PARENT directly would
 * silently evaluate an undefined identifier as 0 and then reference the
 * equally-undefined XPAR_XSPI_0_INTERRUPTS in the #else branch. Mirrors the
 * I2C PL block below.
 */
#if defined(XPAR_XSPI_0_INTERRUPTS)
#if (XPAR_XSPI_0_INTERRUPT_PARENT & 0x1U)
/* Fabric line is an AXI INTC input (raw local number). */
#define SPI_IRQ_ID		XPAR_FABRIC_XSPI_0_INTR
#define SPI_EXTRA_INIT		{ .use_irq = true, \
				  .irq_id = CAPI_IRQ_XILINX_INTC(SPI_IRQ_ID) }
#else
/* Fabric line is a GIC SPI: resolve the SDT-encoded id to a GIC id. */
#define SPI_IRQ_ID		(XGet_IntrId(XPAR_XSPI_0_INTERRUPTS) + \
				 XGet_IntrOffset(XPAR_XSPI_0_INTERRUPTS))
#define SPI_EXTRA_INIT		{ .use_irq = true, \
				  .irq_id = CAPI_IRQ_XILINX_GIC(SPI_IRQ_ID) }
#endif
#else
/* No fabric interrupt wired (polled build): sync transfers only. */
#define SPI_EXTRA_INIT		{ .use_irq = false }
#endif /* XPAR_XSPI_0_INTERRUPTS */

#endif /* SPI_SEL_* */

/*
 * Async-via-interrupt availability, derived from the selected backend rather
 * than pinned by hand: on an XSA with no fabric interrupt the branches above
 * fall back to .use_irq = false, and the async cases must SKIP rather than run
 * and fail -ENOTSUP. Keyed on SPI_IRQ_ID, which only those branches define.
 */
#if defined(SPI_IRQ_ID)
#define SPI_HAS_IRQ  1
#else
#define SPI_HAS_IRQ  0
#endif

/*
 * clk_freq_hz is the controller REFERENCE clock, not the requested SCLK. Leave
 * it 0 so the driver keeps the BSP value (XPAR_XSPIPS_0_SPI_CLK_FREQ_HZ,
 * ~166.67 MHz); the requested bus rate is set via SPI_DEVICE_SPEED_HZ
 * (max_speed_hz) below. Passing the intended SCLK here instead would overwrite
 * InputClockHz and make the prescaler divide the wrong base, running SCLK far
 * too fast.
 */
#define SPI_CLK_FREQ		0U

#define SPI_DEVICE_NATIVE_CS	0x01U
#define SPI_DEVICE_MODE		CAPI_SPI_MODE_0
/*
 * max_speed_hz. PS SPI (XSpiPs) has a runtime prescaler and accepts a requested
 * rate. PL AXI Quad SPI has NO runtime divider — its SCLK ratio is fixed in HDL
 * (C_SCK_RATIO), so any non-zero max_speed_hz makes the driver return -ENOTSUP
 * (=134 in newlib baremetal). Request 0 for PL to keep the HDL-fixed rate.
 */
#if defined(SPI_SEL_PL)
#define SPI_DEVICE_SPEED_HZ	0U
#else
#define SPI_DEVICE_SPEED_HZ	1000000U
#endif

/*
 * Timer selection. Xilinx exposes three timer flavours and the test drives one
 * at a time (a build maps a single TIMER_OPS). Which one is chosen two ways:
 *
 *   1. Override: define TIMER_SELECT (e.g. -DTIMER_SELECT=TIMER_SEL_AXI) to
 *      force a specific timer, provided its instances exist in the BSP.
 *   2. Auto: with no override, pick the first flavour present in the BSP,
 *      preferring the PS TTC — it is the validated path on this board (its
 *      overflow interrupt is a level-high GIC SPI, the same delivery route as
 *      the PS SPI/UART; the AXI timer's fabric IRQ_F2P never reached the GIC).
 *
 * Each selected block below emits the full mapping (TIMER_OPS, identifier,
 * clock, IRQ) plus the capability flags and counter shape test_timer.c reads,
 * so the type-agnostic test exercises exactly the paths that timer supports.
 */
#define TIMER_SEL_TTC	1
#define TIMER_SEL_AXI	2
#define TIMER_SEL_SCU	3

#ifndef TIMER_SELECT
#if defined(XPAR_XTTCPS_NUM_INSTANCES)
#define TIMER_SELECT	TIMER_SEL_TTC
#elif defined(XPAR_TMRCTR_NUM_INSTANCES) || defined(XPAR_AXI_TIMER_NUM_INSTANCES)
#define TIMER_SELECT	TIMER_SEL_AXI
#elif defined(XPAR_XSCUTIMER_NUM_INSTANCES) || defined(XPAR_SCUTIMER_NUM_INSTANCES)
#define TIMER_SELECT	TIMER_SEL_SCU
#else
#error "No supported Xilinx timer (TTC/AXI/SCU) in the BSP; set TIMER_SELECT"
#endif
#endif /* TIMER_SELECT */

#if TIMER_SELECT == TIMER_SEL_TTC
/*
 * PS TTC (XTtcPs, triple timer counter) — 1 channel per instance,
 * capi_timer_xilinx_ps_ttc_ops. Overflow IRQ and output-compare match
 * registers, but no input capture. The Zynq-7000 (ARMA9) TTC counter is 16-bit
 * (XTTCPS_MAX_INTERVAL_COUNT == 0xFFFF); the driver's default /2 prescaler on
 * the ~111 MHz source rolls a 16-bit span over in ~1.2 ms, well inside the 1 s
 * IRQ timeout.
 */
#define TIMER_IDENTIFIER	XPAR_XTTCPS_0_BASEADDR
#define TIMER_OPS		&capi_timer_xilinx_ps_ttc_ops
#define TIMER_INPUT_CLK_HZ	XPAR_XTTCPS_0_CLOCK_FREQ
#define TIMER_OUTPUT_FREQ_HZ	0U	/* TTC free-runs, no target frequency */
#define TIMER_EXTRA_TYPE	struct capi_timer_xilinx_config
#define TIMER_IRQ_ID		(XGet_IntrId(XPAR_XTTCPS_0_INTERRUPTS) + \
				 XGet_IntrOffset(XPAR_XTTCPS_0_INTERRUPTS))
#define TIMER_EXTRA_INIT	{ .use_irq = true, \
				  .irq_id = CAPI_IRQ_XILINX_GIC(TIMER_IRQ_ID) }

#define TIMER_HAS_IRQ		1
#define TIMER_HAS_COMPARE	1

#define TIMER_DIRECTION		CAPI_TIMER_COUNT_UP
#define TIMER_COUNTER_MAX	0x0000FFFFU	/* 16-bit TTC, ~1.2 ms rollover */
#define TIMER_COUNTER_WIDTH	16U
#define TIMER_COMPARE_VALUE	0x00001000U

#define TIMER_RATE_WINDOW_US	100U
#define TIMER_RATE_COUNTER_MASK	0x0000FFFFU
#define TIMER_RATE_TOLERANCE_PCT 10U

/*
 * IRQ-count case: interrupt every TIMER_IRQ_PERIOD_US and count exactly
 * TIMER_IRQ_EXPECTED_COUNT over PERIOD_US*COUNT of run time. 1 ms (not 10 ms):
 * the 16-bit TTC at ~55 MHz holds only ~1.18 ms per interval, so 1 ms x 200 =>
 * 200 interrupts over 200 ms is the widest period this counter can carry.
 */
#define TIMER_IRQ_PERIOD_US	1000U
#define TIMER_IRQ_EXPECTED_COUNT 200U

#elif TIMER_SELECT == TIMER_SEL_AXI
/*
 * AXI Timer (XTmrCtr, PL fabric) — capi_timer_xilinx_pl_ops, 32-bit counter
 * with output-compare and input-capture channels. The fabric IRQ (IRQ_F2P) may
 * not be wired to the GIC on every board, so the overflow-IRQ case is gated off
 * by default here; enable TIMER_HAS_IRQ once the fabric line is routed.
 *
 * On an XSA that does route it the BSP emits XPAR_FABRIC_XTMRCTR_0_INTR, which
 * is what TIMER_IRQ_ID would resolve from -- the same pattern the PL I2C/SPI
 * blocks above use, including the INTERRUPT_PARENT check to tell an AXI INTC
 * input from a GIC SPI. Left disabled until it has actually been run.
 */
#define TIMER_IDENTIFIER	XPAR_XTMRCTR_0_BASEADDR
#define TIMER_OPS		&capi_timer_xilinx_pl_ops
#define TIMER_INPUT_CLK_HZ	XPAR_XTMRCTR_0_CLOCK_FREQUENCY
#define TIMER_OUTPUT_FREQ_HZ	1000U
#define TIMER_EXTRA_TYPE	struct capi_timer_xilinx_config
#define TIMER_EXTRA_INIT	{ .use_irq = false }

#define TIMER_HAS_IRQ		0
#define TIMER_HAS_CAPTURE	1
#define TIMER_HAS_COMPARE	1

#define TIMER_DIRECTION		CAPI_TIMER_COUNT_UP
#define TIMER_COUNTER_MAX	0xFFFFFFFFU	/* 32-bit AXI counter */
#define TIMER_COUNTER_WIDTH	32U
#define TIMER_COMPARE_VALUE	0x00010000U

#define TIMER_RATE_WINDOW_US	100U
#define TIMER_RATE_COUNTER_MASK	0xFFFFFFFFU
#define TIMER_RATE_TOLERANCE_PCT 10U

/*
 * IRQ-count case sizing (compiled even though TIMER_HAS_IRQ gates the subtest
 * off here). The 32-bit AXI counter can hold the full 10 ms period, so this
 * matches the literal "every 10 ms, 20 interrupts over 200 ms" once the fabric
 * IRQ is routed and TIMER_HAS_IRQ is set.
 */
#define TIMER_IRQ_PERIOD_US	10000U
#define TIMER_IRQ_EXPECTED_COUNT 20U

#elif TIMER_SELECT == TIMER_SEL_SCU
/*
 * PS SCU private timer (XScuTimer) — capi_timer_xilinx_ps_scu_ops, a 32-bit
 * down-counter with auto-reload and an overflow IRQ. It supports compare mode
 * but has no input capture. Runs at half the CPU (3x3) clock behind the GIC.
 */
#define TIMER_IDENTIFIER	XPAR_XSCUTIMER_0_BASEADDR
#define TIMER_OPS		&capi_timer_xilinx_ps_scu_ops
/*
 * The A9 private (SCU) timer has no clock-frequency entry of its own in the
 * BSP: it is clocked from the CPU 3x2x domain, i.e. half the CPU core clock,
 * so derive it rather than referencing a macro the BSP never emits.
 */
#define TIMER_INPUT_CLK_HZ	(XPAR_CPU_CORE_CLOCK_FREQ_HZ / 2U)
/*
 * Do NOT ask for a low output frequency here. The SCU prescaler is 8 bits, so
 * the slowest tick this timer can produce is input/256 -- about 1.3 MHz from a
 * 333 MHz source. Requesting 1 kHz silently clamps the prescaler to 255 and
 * still yields ~1.3 MHz, but combined with a 32-bit reload it stretches one
 * period to roughly 55 minutes: the counter then looks frozen across the
 * test's microsecond-scale sampling window and the overflow interrupt never
 * arrives. Run it unprescaled and let the reload value set the period.
 */
#define TIMER_OUTPUT_FREQ_HZ	TIMER_INPUT_CLK_HZ
#define TIMER_EXTRA_TYPE	struct capi_timer_xilinx_config
#define TIMER_IRQ_ID		CAPI_IRQ_XILINX_GIC(XPS_SCU_TMR_INT_ID)
#define TIMER_EXTRA_INIT	{ .use_irq = true, \
				  .irq_id = TIMER_IRQ_ID }

#define TIMER_HAS_IRQ		1
#define TIMER_HAS_COMPARE	1

#define TIMER_DIRECTION		CAPI_TIMER_COUNT_DOWN
/*
 * Reload value, not the counter's architectural width (that is 32 bits, see
 * TIMER_COUNTER_WIDTH). Unprescaled at ~333 MHz a full 0xFFFFFFFF reload is
 * ~12.9 s per period, so the overflow-driven cases would sit waiting most of a
 * minute for a handful of interrupts. 0x00100000 is ~3.1 ms, which keeps the
 * rate and async-IRQ cases in the same millisecond ballpark as the TTC mapping
 * above while leaving the counter comfortably fast-moving between samples.
 */
#define TIMER_COUNTER_MAX	0x00100000U
#define TIMER_COUNTER_WIDTH	32U
#define TIMER_COMPARE_VALUE	0x00080000U

#define TIMER_RATE_WINDOW_US	100U
/*
 * Masks the counter delta, so it must track the RELOAD value the counter wraps
 * at (TIMER_COUNTER_MAX), not the 32-bit register width -- a sample pair that
 * straddles a reload would otherwise compute a delta off by the difference.
 */
#define TIMER_RATE_COUNTER_MASK	(TIMER_COUNTER_MAX - 1U)
#define TIMER_RATE_TOLERANCE_PCT 10U

/*
 * IRQ-count case: the 32-bit SCU down-counter easily holds a 10 ms period, so
 * this is the literal "every 10 ms, 20 interrupts over 200 ms" — each period the
 * counter reloads and reaches zero once, firing one expiry interrupt.
 */
#define TIMER_IRQ_PERIOD_US	10000U
#define TIMER_IRQ_EXPECTED_COUNT 20U

#else
#error "TIMER_SELECT must be TIMER_SEL_TTC, TIMER_SEL_AXI or TIMER_SEL_SCU"
#endif /* TIMER_SELECT */

/*
 * I2C initiator/target loopback. Each role picks its backend independently
 * (mirroring the GPIO/SPI schemes), so all four wirings are expressible:
 * PS/PL initiator x PS/PL target.
 *
 *   PS I2C (XIicPs, EMIO): behind the GIC, its interrupt is always a GIC id.
 *   PL AXI IIC (XIic, fabric): its IRQ_F2P line feeds the GIC (SPI) or the AXI
 *     INTC input depending on the build, chosen from the INTERRUPT_PARENT low
 *     bit (1 = INTC). A polled ("noirq") build has the core but no wired
 *     interrupt (no XPAR_XIIC_0_INTERRUPTS), so use_irq falls back to false.
 *
 * Selection per role (define before this point to force):
 *   Initiator: I2C_SEL_PS / I2C_SEL_PL
 *   Target:    I2C_TARGET_SEL_PS / I2C_TARGET_SEL_PL
 * With no override each role auto-detects the form present in the BSP,
 * preferring the one used now: PL initiator, PS target. A role whose backend is
 * absent from the BSP leaves its macros undefined so the build still compiles.
 * Wire the two buses together (SCL<->SCL, SDA<->SDA) with pull-ups; the target
 * answers I2C_TARGET_ADDR, the initiator addresses that same address.
 */
#define I2C_TARGET_ADDR		0x42U

/*
 * input_clock_hz (PL only) enables the AXI IIC runtime SCL timing writes
 * (THIGH/TLOW). The core is synthesized at C_S_AXI_ACLK_FREQ_HZ = 100 MHz;
 * feeding it lets configure_bus_speed reprogram the bus rate, at the driver
 * default register offsets (0x13C/0x140) and 50% duty.
 */
#define I2C_PL_INPUT_CLK_HZ	100000000U

/* --- Initiator role: PL AXI IIC preferred, else PS I2C. --- */
#if !defined(I2C_SEL_PS) && !defined(I2C_SEL_PL)
#if defined(XPAR_XIIC_NUM_INSTANCES) || defined(XPAR_XIIC_0_BASEADDR)
#define I2C_SEL_PL
#elif defined(XPAR_XIICPS_NUM_INSTANCES) || defined(XPAR_XIICPS_0_BASEADDR)
#define I2C_SEL_PS
#endif
#endif

#if defined(I2C_SEL_PL)

#define I2C_IDENTIFIER		XPAR_XIIC_0_BASEADDR
#define I2C_OPS			&capi_i2c_xilinx_pl_ops
#define I2C_EXTRA_TYPE		struct capi_i2c_xilinx_config
#if defined(XPAR_XIIC_0_INTERRUPTS)
#if (XPAR_XIIC_0_INTERRUPT_PARENT & 0x1U)
/* Fabric line is an AXI INTC input (raw local number). */
#define I2C_IRQ_ID		XPAR_FABRIC_XIIC_0_INTR
#define I2C_EXTRA_INIT		{ .use_irq = true, \
				  .irq_id = CAPI_IRQ_XILINX_INTC(I2C_IRQ_ID), \
				  .input_clock_hz = I2C_PL_INPUT_CLK_HZ }
#else
/*
 * Fabric line is a GIC SPI. CAPI_IRQ_XILINX_GIC() wants the fully-resolved GIC
 * id in its low half; the GIC backend passes it straight to XScuGic_Connect()
 * with no offset added. XPAR_FABRIC_XIIC_0_INTR is the raw fabric input (36),
 * NOT a GIC id, so resolve it as XGet_IntrId() + the SPI base from
 * XGet_IntrOffset() (+32) => real GIC id 68.
 */
#define I2C_IRQ_ID		(XGet_IntrId(XPAR_XIIC_0_INTERRUPTS) + \
				 XGet_IntrOffset(XPAR_XIIC_0_INTERRUPTS))
#define I2C_EXTRA_INIT		{ .use_irq = true, \
				  .irq_id = CAPI_IRQ_XILINX_GIC(I2C_IRQ_ID), \
				  .input_clock_hz = I2C_PL_INPUT_CLK_HZ }
#endif
#else
/* No fabric interrupt wired (polled build): initiator runs sync only. */
#define I2C_EXTRA_INIT		{ .use_irq = false, \
				  .input_clock_hz = I2C_PL_INPUT_CLK_HZ }
#endif /* XPAR_XIIC_0_INTERRUPTS */

#elif defined(I2C_SEL_PS)

#define I2C_IDENTIFIER		XPAR_XIICPS_0_BASEADDR
#define I2C_OPS			&capi_i2c_xilinx_ps_ops
#define I2C_EXTRA_TYPE		struct capi_i2c_xilinx_config
#if defined(XPAR_XIICPS_0_INTERRUPTS)
#define I2C_IRQ_ID		(XGet_IntrId(XPAR_XIICPS_0_INTERRUPTS) + \
				 XGet_IntrOffset(XPAR_XIICPS_0_INTERRUPTS))
#define I2C_EXTRA_INIT		{ .use_irq = true, \
				  .irq_id = CAPI_IRQ_XILINX_GIC(I2C_IRQ_ID) }
#else
#define I2C_EXTRA_INIT		{ .use_irq = false }
#endif /* XPAR_XIICPS_0_INTERRUPTS */

#endif /* I2C_SEL_* */

/* --- Target role: PS I2C preferred, else PL AXI IIC. --- */
#if !defined(I2C_TARGET_SEL_PS) && !defined(I2C_TARGET_SEL_PL)
#if defined(XPAR_XIICPS_NUM_INSTANCES) || defined(XPAR_XIICPS_0_BASEADDR)
#define I2C_TARGET_SEL_PS
#elif defined(XPAR_XIIC_NUM_INSTANCES) || defined(XPAR_XIIC_0_BASEADDR)
#define I2C_TARGET_SEL_PL
#endif
#endif

#if defined(I2C_TARGET_SEL_PS)

#define I2C_TARGET_IDENTIFIER	XPAR_XIICPS_0_BASEADDR
#define I2C_TARGET_OPS		&capi_i2c_xilinx_ps_ops
#define I2C_TARGET_EXTRA_TYPE	struct capi_i2c_xilinx_config
#if defined(XPAR_XIICPS_0_INTERRUPTS)
#define I2C_TARGET_PS_IRQ_ID	(XGet_IntrId(XPAR_XIICPS_0_INTERRUPTS) + \
				 XGet_IntrOffset(XPAR_XIICPS_0_INTERRUPTS))
#define I2C_TARGET_EXTRA_INIT	{ .use_irq = true, \
				  .irq_id = CAPI_IRQ_XILINX_GIC(I2C_TARGET_PS_IRQ_ID) }
#else
#define I2C_TARGET_EXTRA_INIT	{ .use_irq = false }
#endif /* XPAR_XIICPS_0_INTERRUPTS */

#elif defined(I2C_TARGET_SEL_PL)

#define I2C_TARGET_IDENTIFIER	XPAR_XIIC_0_BASEADDR
#define I2C_TARGET_OPS		&capi_i2c_xilinx_pl_ops
#define I2C_TARGET_EXTRA_TYPE	struct capi_i2c_xilinx_config
#if defined(XPAR_XIIC_0_INTERRUPTS)
#if (XPAR_XIIC_0_INTERRUPT_PARENT & 0x1U)
#define I2C_TARGET_IRQ_ID	XPAR_FABRIC_XIIC_0_INTR
#define I2C_TARGET_EXTRA_INIT	{ .use_irq = true, \
				  .irq_id = CAPI_IRQ_XILINX_INTC(I2C_TARGET_IRQ_ID), \
				  .input_clock_hz = I2C_PL_INPUT_CLK_HZ }
#else
#define I2C_TARGET_IRQ_ID	(XGet_IntrId(XPAR_XIIC_0_INTERRUPTS) + \
				 XGet_IntrOffset(XPAR_XIIC_0_INTERRUPTS))
#define I2C_TARGET_EXTRA_INIT	{ .use_irq = true, \
				  .irq_id = CAPI_IRQ_XILINX_GIC(I2C_TARGET_IRQ_ID), \
				  .input_clock_hz = I2C_PL_INPUT_CLK_HZ }
#endif
#else
#define I2C_TARGET_EXTRA_INIT	{ .use_irq = false, \
				  .input_clock_hz = I2C_PL_INPUT_CLK_HZ }
#endif /* XPAR_XIIC_0_INTERRUPTS */

#endif /* I2C_TARGET_SEL_* */

/*
 * On Xilinx the CAPI GIC IRQ singleton routes each controller's interrupt to
 * the ISR connected at capi_i2c_init(), and the BSP handles clocking/pinmux.
 * So the test's platform hooks reduce to no-ops (unlike STM32, where I2C2's
 * clock, pins and IRQ vectors must be brought up by hand).
 */
#define I2C_PLATFORM_INIT()		0
#define I2C_PLATFORM_DEINIT()		((void)0)
#define I2C_PLATFORM_SET_TARGET(h)	((void)(h))

#endif /* __PARAMETERS_H__ */
