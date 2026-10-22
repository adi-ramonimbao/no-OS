add_executable(capi_selftest)

target_sources(capi_selftest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/${PLATFORM}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/common_data.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/test_framework.c
)

if(PLATFORM STREQUAL "stm32" AND CONFIG_CAPI_I2C)
	target_sources(capi_selftest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/platform/stm32/i2c_platform_init.c
	)
endif()

if(CONFIG_CAPI_SELFTEST_BASIC_EXAMPLE)
    target_sources(capi_selftest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/basic/basic_example.c
    )
    target_include_directories(capi_selftest PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/basic
    )
endif()

if(CONFIG_CAPI_SELFTEST_LOOPBACK_EXAMPLE)
    target_sources(capi_selftest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/capi_loopback/capi_loopback.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/capi_loopback/tests/test_gpio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/capi_loopback/tests/test_irq.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/capi_loopback/tests/test_spi.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/capi_loopback/tests/test_timer.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/capi_loopback/tests/test_i2c.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/capi_loopback/tests/test_uart.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/capi_loopback/tests/test_dma.c
    )
    target_include_directories(capi_selftest PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/capi_loopback
    )
endif()

if(CONFIG_CAPI_SELFTEST_USB_UART_EXAMPLE)
    target_sources(capi_selftest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/examples/usb_uart/usb_uart_example.c
    )
endif()

target_include_directories(capi_selftest PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/${PLATFORM}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# Several boards share one SoC (TARGET_NUM), e.g. MAX32690EVKIT and
# AD-APARD32690-SL, so parameters.h also needs the board to pick its pin map:
# BOARD=ad-apard32690-sl -> CAPI_SELFTEST_BOARD_AD_APARD32690_SL.
if(DEFINED BOARD AND NOT BOARD STREQUAL "")
    string(MAKE_C_IDENTIFIER "${BOARD}" _capi_selftest_board)
    string(TOUPPER "${_capi_selftest_board}" _capi_selftest_board)
    target_compile_definitions(capi_selftest PRIVATE
        CAPI_SELFTEST_BOARD_${_capi_selftest_board}
    )
endif()

target_link_libraries(capi_selftest no-os)

# BSP + flash go through the shared cmake/xilinx dispatchers for every platform.
config_platform_sdk(capi_selftest)
add_flash_target(capi_selftest)
post_build_config(capi_selftest)
