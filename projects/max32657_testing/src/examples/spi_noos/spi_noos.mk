INCS += $(INCLUDE)/no_os_delay.h     \
		$(INCLUDE)/no_os_error.h     \
		$(INCLUDE)/no_os_gpio.h      \
		$(INCLUDE)/no_os_i2c.h       \
		$(INCLUDE)/no_os_print_log.h \
		$(INCLUDE)/no_os_spi.h       \
		$(INCLUDE)/no_os_irq.h       \
		$(INCLUDE)/no_os_init.h      \
		$(INCLUDE)/no_os_dma.h       \
		$(INCLUDE)/no_os_list.h      \
		$(INCLUDE)/no_os_uart.h      \
		$(INCLUDE)/no_os_units.h     \
		$(INCLUDE)/no_os_lf256fifo.h \
		$(INCLUDE)/no_os_util.h      \
		$(INCLUDE)/no_os_alloc.h     \
		$(INCLUDE)/no_os_mutex.h

SRCS += $(DRIVERS)/api/no_os_gpio.c     \
		$(DRIVERS)/api/no_os_i2c.c      \
		$(NO-OS)/util/no_os_lf256fifo.c \
		$(DRIVERS)/api/no_os_irq.c      \
		$(DRIVERS)/api/no_os_spi.c      \
		$(DRIVERS)/api/no_os_uart.c     \
		$(DRIVERS)/api/no_os_dma.c      \
		$(NO-OS)/util/no_os_list.c      \
		$(NO-OS)/util/no_os_util.c      \
		$(NO-OS)/util/no_os_alloc.c     \
		$(NO-OS)/util/no_os_mutex.c

SRCS += $(NO-OS)/capi/platform/maxim/max32657/maxim_init.c

INCS +=	$(NO-OS)/capi/platform/maxim/max32657/../common/maxim_dma.h \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_irq.h \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_spi.h \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_uart.h \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_uart_stdio.h

SRCS +=	$(NO-OS)/capi/platform/maxim/max32657/../common/maxim_dma.c \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_irq.c \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_spi.c \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_uart.c \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_uart_stdio.c

INCS +=	${NO-OS}/capi/inc/capi_time.h

SRCS +=	${NO-OS}/capi/src/capi_time.c
