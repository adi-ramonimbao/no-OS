/***************************************************************************//**
 *   @file   dma_noos.c
 *   @brief  DMA example using no-OS API for MAX32657
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

#include "dma.h"
#include "no_os_alloc.h"
#include "no_os_dma.h"
#include "no_os_error.h"
#include "maxim_dma.h"
#include "maxim_uart.h"
#include "maxim_uart_stdio.h"


int example_main(void)
{
    uint8_t dma_src_buffer[64];
    uint8_t dma_dst_buffer[64];
    struct no_os_dma_desc *dma;
    struct no_os_dma_ch *dma_channel;
    uint32_t i;
    int ret;
    bool has_error;

    struct no_os_dma_init_param dma_init_param = {
        .id = MXC_DMA1_S,
        .num_ch = MXC_DMA_CHANNELS,
        .platform_ops = &max_dma_ops,
    };

    struct no_os_dma_xfer_desc dma_xfer_config = {
        .src = dma_src_buffer,
        .dst = dma_dst_buffer,
        .length = sizeof(dma_src_buffer),
        .xfer_type = MEM_TO_MEM,
        .periph = NO_OS_DMA_IRQ,
    };

    struct no_os_uart_desc *uart;
    struct max_uart_init_param uart_extra = {
	.vssel = MXC_GPIO_VSSEL_VDDIOH,
    };
    struct no_os_uart_init_param param = {
        .baud_rate = 115200,
        .size = NO_OS_UART_CS_8,
        .parity = NO_OS_UART_PAR_NO,
        .stop = NO_OS_UART_STOP_1_BIT,
        .platform_ops = &max_uart_ops,
	.extra = &uart_extra,
    };

    ret = no_os_uart_init(&uart, &param);
    if (ret)
        return ret;
    no_os_uart_stdio(uart);

    printf("Initializing DMA\n\r");

    ret = no_os_dma_init(&dma, &dma_init_param);
    if (ret)
        return ret;

    printf("Setting up test buffers\n\r");
    /* Initialize source buffer with test pattern */
    for (i = 0; i < sizeof(dma_src_buffer); i++)
        dma_src_buffer[i] = (uint8_t)(i ^ 0xAA);

    /* Clear destination buffer */
    memset(dma_dst_buffer, 0, sizeof(dma_dst_buffer));

    printf("Acquiring DMA channel\n\r");
    ret = no_os_dma_acquire_channel(dma, &dma_channel);
    if (ret)
        return ret;

    printf("Configuring DMA transfer\n\r");
    ret = no_os_dma_config_xfer(dma, &dma_xfer_config, 1, dma_channel);
    if (ret)
        return ret;

    printf("Starting DMA transfer\n\r");
    ret = no_os_dma_xfer_start(dma, dma_channel);
    if (ret)
        return ret;

    while (!no_os_dma_in_progress(dma, dma_channel)) {}

    has_error = false;
    printf("Verifying data\n\r");
    for (i = 0; i< sizeof(dma_src_buffer); i++) {
        printf("[i]: src = %d, dst = %d\n\r", dma_src_buffer[i], dma_dst_buffer[i]);
        if (dma_dst_buffer[i] != dma_src_buffer[i]) {
            has_error = true;
            // break;
        }
    }

    if (has_error) {
        printf("ERROR: src and dst are not the same!\n\r");
    } else {
        printf("DMA transfer complete\n\r");
    }

    return 0;
}
