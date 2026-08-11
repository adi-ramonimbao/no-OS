# Build in Secure mode always
CFLAGS += -DCONFIG_TRUSTED_EXECUTION_SECURE=1

SRCS += $(PROJECT)/src/main.c \
	$(NO-OS)/capi/platform/maxim/max32657/maxim_capi_time.c
