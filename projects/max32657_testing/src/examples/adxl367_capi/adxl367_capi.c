/***************************************************************************//**
 *   @file   adxl367_capi.c
 *   @brief  ADXL367 I2C device-ID read example using CAPI for MAX32657
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES, INC. “AS IS” AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL ANALOG DEVICES, INC. BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "capi_i2c.h"
#include "capi_time.h"
#include "capi_uart.h"
#include "maxim_capi_gpio.h"
#include "maxim_capi_i2c.h"
#include "maxim_capi_uart.h"

/* ADXL367 registers */
#define ADXL367_REG_DEVID_AD	0x00
#define ADXL367_REG_XDATA_H	0x0E
#define ADXL367_REG_SOFT_RESET	0x1F
#define ADXL367_REG_POWER_CTL	0x2D

/* Expected ID values */
#define ADXL367_DEVID_AD_VAL	0xAD
#define ADXL367_DEVID_MST_VAL	0x1D
#define ADXL367_PARTID_VAL	0xF7

/* Command / mode values */
#define ADXL367_RESET_KEY	0x52
#define ADXL367_MEASURE_MODE	0x02

/* 7-bit I2C address is 0x53 (ASEL high) or 0x1D (ASEL grounded). */
#define ADXL367_ADDR_ASEL_HIGH	0x53
#define ADXL367_ADDR_ASEL_LOW	0x1D

/**
 * @brief Read a block of ADXL367 registers over I2C.
 * @param dev - CAPI I2C device.
 * @param reg - Starting register address.
 * @param buf - Destination buffer.
 * @param len - Number of registers to read.
 * @return 0 on success, negative error code otherwise.
 */
static int adxl367_read_regs(struct capi_i2c_device *dev, uint8_t reg,
			     uint8_t *buf, uint32_t len)
{
	uint8_t sub = reg;
	struct capi_i2c_transfer xfer = {
		.buf = buf,
		.len = len,
		.sub_address = &sub,
		.sub_address_len = 1,
		.no_stop = false,
	};

	return capi_i2c_receive(dev, &xfer);
}

/**
 * @brief Write a single ADXL367 register over I2C.
 * @param dev - CAPI I2C device.
 * @param reg - Register address.
 * @param val - Value to write.
 * @return 0 on success, negative error code otherwise.
 */
static int adxl367_write_reg(struct capi_i2c_device *dev, uint8_t reg,
			     uint8_t val)
{
	uint8_t sub = reg;
	uint8_t data = val;
	struct capi_i2c_transfer xfer = {
		.buf = &data,
		.len = 1,
		.sub_address = &sub,
		.sub_address_len = 1,
		.no_stop = false,
	};

	return capi_i2c_transmit(dev, &xfer);
}

/**
 * @brief Combine a 14-bit two's-complement sample from its two data registers.
 * @param hi - High data byte (bits 13:6).
 * @param lo - Low data byte (bits 5:0 held in [7:2]).
 * @return Sign-extended sample.
 */
static int16_t adxl367_raw14(uint8_t hi, uint8_t lo)
{
	int16_t val = ((int16_t)hi << 6) | (lo >> 2);

	if (val & 0x2000)
		val |= 0xC000;

	return val;
}

int example_main(void)
{
	int ret, i;
	bool found = false;
	const uint16_t addrs[2] = {
		ADXL367_ADDR_ASEL_HIGH,
		ADXL367_ADDR_ASEL_LOW,
	};

	/* UART setup for stdio */
	struct capi_uart_handle *uart_handle = NULL;
	struct max_capi_uart_extra uart_extra = {
		.vssel = MAX_CAPI_GPIO_VSSEL_VDDIOH,
	};
	struct capi_uart_line_config uart_line_config = {
		.baudrate = 115200,
		.size = CAPI_UART_DATA_BITS_8,
		.parity = CAPI_UART_PARITY_NONE,
		.stop_bits = CAPI_UART_STOP_1_BIT,
	};
	struct capi_uart_config uart_config = {
		.identifier = 0, /* UART0 */
		.line_config = &uart_line_config,
		.ops = &max_capi_uart_ops,
		.extra = &uart_extra,
	};
	ret = capi_uart_init(&uart_handle, &uart_config);
	if (ret)
		return ret;
	max_capi_uart_stdio_enable(uart_handle);

	printf("ADXL367 I2C ID read (CAPI)\n\r");

	/* I2C setup (controller mode) */
	struct capi_i2c_controller_handle *i2c_handle = NULL;
	struct max_capi_i2c_extra i2c_extra = {
		.vssel = MAX_CAPI_GPIO_VSSEL_VDDIOH,
	};
	const struct capi_i2c_config i2c_config = {
		.identifier = 0,
		.extra = &i2c_extra,
		.ops = &max_capi_i2c_ops,
		.initiator = true,
		.clk_freq_hz = MAX_CAPI_I2C_SPEED_FAST,
	};
	ret = capi_i2c_init(&i2c_handle, &i2c_config);
	if (ret) {
		printf("I2C init failed: %d\n\r", ret);
		return ret;
	}

	/* The onboard ADXL367 address depends on the ASEL pin strap, so probe
	 * both valid addresses and report which one answers. */
	struct capi_i2c_device adxl = {
		.controller = i2c_handle,
	};

	for (i = 0; i < 2; i++) {
		uint8_t id[3] = {0};

		adxl.address = addrs[i];

		ret = adxl367_read_regs(&adxl, ADXL367_REG_DEVID_AD, id, sizeof(id));
		if (ret) {
			printf("addr 0x%02X: no response (%d)\n\r", addrs[i], ret);
			continue;
		}

		printf("addr 0x%02X: DEVID_AD=0x%02X DEVID_MST=0x%02X PARTID=0x%02X\n\r",
		       addrs[i], id[0], id[1], id[2]);

		if (id[0] == ADXL367_DEVID_AD_VAL &&
		    id[1] == ADXL367_DEVID_MST_VAL &&
		    id[2] == ADXL367_PARTID_VAL) {
			printf("  -> ADXL367 detected\n\r");
			found = true;
			break;
		}
	}

	if (!found) {
		printf("ADXL367 not detected\n\r");
		capi_i2c_deinit(i2c_handle);
		capi_uart_deinit(uart_handle);
		while (1);
		return 0;
	}

	/* Reset to defaults (+/-2g, 100 Hz ODR), then enter measurement mode */
	adxl367_write_reg(&adxl, ADXL367_REG_SOFT_RESET, ADXL367_RESET_KEY);
	capi_wait_ms(10);
	adxl367_write_reg(&adxl, ADXL367_REG_POWER_CTL, ADXL367_MEASURE_MODE);
	capi_wait_ms(100);

	printf("Streaming X/Y/Z (raw 14-bit)\n\r");

	while (1) {
		uint8_t raw[6];
		int16_t x, y, z;

		ret = adxl367_read_regs(&adxl, ADXL367_REG_XDATA_H, raw, sizeof(raw));
		if (ret) {
			printf("read failed: %d\n\r", ret);
			capi_wait_ms(500);
			continue;
		}

		x = adxl367_raw14(raw[0], raw[1]);
		y = adxl367_raw14(raw[2], raw[3]);
		z = adxl367_raw14(raw[4], raw[5]);

		printf("X=%6d Y=%6d Z=%6d\n\r", x, y, z);

		capi_wait_ms(100);
	}

	return 0;
}
