/*
 * I2C long-read bench test for the Maxim no-OS I2C driver. See
 * ../README.md for the setups.
 */

#include <stdio.h>

#include "no_os_delay.h"
#include "no_os_uart.h"
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"
#include "lrt_controller.h"
#include "lrt_target.h"

static struct max_uart_init_param uart_extra = {
	.flow = MAX_UART_FLOW_DIS,
};

static struct no_os_uart_init_param uart_ip = {
	.device_id = LRT_UART,
	.baud_rate = 115200,
	.size = NO_OS_UART_CS_8,
	.parity = NO_OS_UART_PAR_NO,
	.stop = NO_OS_UART_STOP_1_BIT,
	.extra = &uart_extra,
	.platform_ops = &max_uart_ops,
};

int main(void)
{
	struct no_os_uart_desc *uart;
	int ret;

	ret = no_os_uart_init(&uart, &uart_ip);
	if (ret)
		return ret;
	no_os_uart_stdio(uart);

	/* Give the host time to open the console after a reset */
	no_os_mdelay(2000);
	printf("\n\n=== no-OS I2C long-read test (" LRT_ROLE_NAME ") ===\n");

#if LRT_TARGET
	ret = lrt_target_init();
	printf("target 0x51 on I2C2 (P1.7 SDA / P1.8 SCL): %s\n",
	       ret ? "init FAILED" : "ready");
	if (ret)
		while (1);
#endif

#if LRT_CONTROLLER
	ret = lrt_controller_run(LRT_CTL_I2C);
	printf("\n=== done: %s (%d failed) ===\n", ret ? "FAIL" : "PASS", ret);
#endif

	while (1) {
#if LRT_TARGET && !LRT_CONTROLLER
		lrt_target_print_log();
#endif
	}
}
