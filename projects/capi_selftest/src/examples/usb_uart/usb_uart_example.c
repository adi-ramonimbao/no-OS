/***************************************************************************//**
 *   @file   usb_uart_example.c
 *   @brief  USB CDC-ACM UART example for the capi_selftest project.
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "capi_time.h"
#include "capi_uart.h"
#include "common_data.h"
#include "test_framework.h"

/* How long to wait for a terminal to open the port before giving up */
#define USB_UART_HOST_WAIT_MS		60000U
#define USB_UART_HOST_POLL_MS		100U
/* How long the interactive RX test waits for the user to type */
#define USB_UART_RX_TIMEOUT_US		30000000U
#define USB_UART_RX_LEN			4U

static volatile bool tx_done;
static volatile bool rx_done;

/**
 * @brief CAPI UART async completion callback.
 * @param event - Completion event.
 * @param arg - Unused.
 * @param event_extra - Unused.
 */
static void usb_uart_callback(enum capi_uart_async_event event, void *arg,
			      int event_extra)
{
	(void)arg;
	(void)event_extra;

	if (event == CAPI_UART_EVENT_TX_DONE)
		tx_done = true;
	else if (event == CAPI_UART_EVENT_RX_DONE)
		rx_done = true;
}

/**
 * @brief Wait until a host terminal opens the virtual serial port.
 *
 * The USB UART reports -ENODEV from transmit until the host asserts DTR, so
 * nothing can be reported before that.
 *
 * @param uart - USB UART handle.
 * @return 0 once the host is attached, negative error code otherwise.
 */
static int usb_uart_wait_for_host(struct capi_uart_handle *uart)
{
	static uint8_t banner[] = "\r\nCAPI USB UART connected\r\n";
	uint32_t waited;
	int ret;

	for (waited = 0U; waited < USB_UART_HOST_WAIT_MS;
	     waited += USB_UART_HOST_POLL_MS) {
		ret = capi_uart_transmit(uart, banner, sizeof(banner) - 1U);
		if (ret != -ENODEV)
			return ret;

		capi_wait_ms(USB_UART_HOST_POLL_MS);
	}

	return -ETIMEDOUT;
}

/**
 * @brief Blocking transmit over USB.
 * @param uart - USB UART handle.
 * @return 0 on pass, negative on assertion failure.
 */
static int test_usb_uart_transmit(struct capi_uart_handle *uart)
{
	static uint8_t message[] = "Hello World from CAPI USB UART!\r\n";
	int ret;

	TEST_BEGIN("USB_UART", "TRANSMIT");
	ret = capi_uart_transmit(uart, message, sizeof(message) - 1U);
	TEST_ASSERT_EQ(ret, 0, "transmit_ret");

	return 0;
}

/**
 * @brief Line coding is reported from, and owned by, the host.
 * @param uart - USB UART handle.
 * @return 0 on pass, negative on assertion failure.
 */
static int test_usb_uart_line_config(struct capi_uart_handle *uart)
{
	struct capi_uart_line_config line_config;
	int ret;

	TEST_BEGIN("USB_UART", "GET_LINE_CONFIG");
	ret = capi_uart_get_line_config(uart, &line_config);
	TEST_ASSERT_EQ(ret, 0, "get_line_config_ret");
	TEST_VALUE("baudrate", line_config.baudrate);

	TEST_BEGIN("USB_UART", "SET_LINE_CONFIG_NOTSUP");
	ret = capi_uart_set_line_config(uart, &line_config);
	TEST_ASSERT_EQ(ret, -ENOTSUP, "set_line_config_ret");

	return 0;
}

/**
 * @brief transmit_async() reports CAPI_UART_EVENT_TX_DONE.
 * @param uart - USB UART handle.
 * @return 0 on pass, negative on assertion failure.
 */
static int test_usb_uart_transmit_async(struct capi_uart_handle *uart)
{
	static uint8_t message[] = "transmit_async\r\n";
	int ret;

	TEST_BEGIN("USB_UART", "TRANSMIT_ASYNC");
	tx_done = false;
	ret = capi_uart_transmit_async(uart, message, sizeof(message) - 1U);
	TEST_ASSERT_EQ(ret, 0, "transmit_async_ret");
	TEST_WAIT_UNTIL(tx_done, 1000000U, 1000U);
	TEST_ASSERT(tx_done, "tx_done");

	return 0;
}

/**
 * @brief Flush, then receive_async() bytes typed on the host terminal and
 *	  echo them back.
 * @param uart - USB UART handle.
 * @return 0 on pass/skip, negative on assertion failure.
 */
static int test_usb_uart_receive_async(struct capi_uart_handle *uart)
{
	static uint8_t prompt[] = "Type 4 characters to finish the run...\r\n";
	static uint8_t rx_buf[USB_UART_RX_LEN];
	uint16_t count;
	int ret;

	TEST_BEGIN("USB_UART", "FLUSH_RX");
	ret = capi_uart_flush_rx_fifo(uart);
	TEST_ASSERT_EQ(ret, 0, "flush_rx_ret");
	ret = capi_uart_get_rx_fifo_count(uart, &count);
	TEST_ASSERT_EQ(ret, 0, "rx_fifo_count_ret");
	TEST_ASSERT_EQ(count, 0, "rx_fifo_count");

	TEST_BEGIN("USB_UART", "RECEIVE_ASYNC");
	ret = capi_uart_transmit(uart, prompt, sizeof(prompt) - 1U);
	TEST_ASSERT_EQ(ret, 0, "prompt_ret");

	rx_done = false;
	ret = capi_uart_receive_async(uart, rx_buf, sizeof(rx_buf));
	TEST_ASSERT_EQ(ret, 0, "receive_async_ret");

	/* A second request must be refused while the first is pending */
	if (!rx_done) {
		ret = capi_uart_receive_async(uart, rx_buf, sizeof(rx_buf));
		TEST_ASSERT_EQ(ret, -EBUSY, "receive_async_busy");
	}

	TEST_WAIT_UNTIL(rx_done, USB_UART_RX_TIMEOUT_US, 10000U);
	if (!rx_done)
		TEST_SKIP_CAT(SKIP_BOARD_STATE, "no input typed on the terminal");

	ret = capi_uart_transmit(uart, rx_buf, sizeof(rx_buf));
	TEST_ASSERT_EQ(ret, 0, "echo_ret");

	return 0;
}

/**
 * @brief Main function for the USB UART example.
 * @return 0 in case of success, negative error code otherwise.
 */
int example_main(void)
{
	struct capi_uart_handle *uart = NULL;
	struct test_framework_config framework_config;
	int ret;

	ret = capi_uart_init(&uart, &usb_uart_config);
	if (ret)
		return ret;

	ret = capi_uart_register_callback(uart, usb_uart_callback, NULL);
	if (ret)
		goto out;

	ret = usb_uart_wait_for_host(uart);
	if (ret)
		goto out;

	get_test_framework_config(&framework_config, uart);
	ret = test_framework_init(&framework_config);
	if (ret)
		goto out;

	TEST_RUN_START();

	/* Failures are recorded in the counts; keep going for a full tally */
	(void)test_usb_uart_transmit(uart);
	(void)test_usb_uart_line_config(uart);
	(void)test_usb_uart_transmit_async(uart);
	(void)test_usb_uart_receive_async(uart);

	TEST_RUN_END();
	test_framework_remove();

	/* Let the host drain the report before the port disappears */
	capi_wait_ms(500U);
out:
	capi_uart_deinit(uart);

	return ret;
}
