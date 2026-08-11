# CAPI headers
INCS +=	${NO-OS}/capi/inc/capi_alloc.h \
	${NO-OS}/capi/inc/capi_time.h \
	${NO-OS}/capi/inc/capi_dma.h \
	${NO-OS}/capi/inc/capi_gpio.h \
	${NO-OS}/capi/inc/capi_irq.h \
	${NO-OS}/capi/inc/capi_trng.h \
	${NO-OS}/capi/inc/capi_uart.h \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_dma.h \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_gpio.h \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_irq.h \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_trng.h \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_uart.h

# CAPI sources
SRCS +=	${NO-OS}/capi/src/capi_alloc.c \
	${NO-OS}/capi/src/capi_time.c \
	${NO-OS}/capi/src/capi_dma.c \
	${NO-OS}/capi/src/capi_gpio.c \
	${NO-OS}/capi/src/capi_irq.c \
	${NO-OS}/capi/src/capi_trng.c \
	${NO-OS}/capi/src/capi_uart.c \
	${PLATFORM_DRIVERS}/maxim_capi_alloc.c \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_dma.c \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_gpio.c \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_irq.c \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_trng.c \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_uart.c
