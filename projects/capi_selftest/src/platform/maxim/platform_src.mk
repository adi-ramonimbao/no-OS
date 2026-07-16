# Build in Secure mode
CFLAGS += -DCONFIG_TRUSTED_EXECUTION_SECURE=1

CAPI_MAX32657_DRIVERS := $(NO-OS)/capi/platform/maxim/max32657

NO_OS_INC_DIRS += \
	$(CAPI_MAX32657_DRIVERS) \
	$(NO-OS)/include

SRCS += $(CAPI_MAX32657_DRIVERS)/maxim_capi_uart.c \
	$(CAPI_MAX32657_DRIVERS)/maxim_capi_irq.c \
	$(CAPI_MAX32657_DRIVERS)/maxim_capi_gpio.c \
	$(CAPI_MAX32657_DRIVERS)/maxim_capi_spi.c \
	$(CAPI_MAX32657_DRIVERS)/maxim_capi_i2c.c \
	$(CAPI_MAX32657_DRIVERS)/maxim_capi_alloc.c \
	$(CAPI_MAX32657_DRIVERS)/maxim_capi_time.c \
	$(CAPI_MAX32657_DRIVERS)/maxim_capi_timer.c \
	$(CAPI_MAX32657_DRIVERS)/maxim_capi_dma.c
