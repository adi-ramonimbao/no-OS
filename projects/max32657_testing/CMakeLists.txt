add_executable(max32657_testing)

target_sources(max32657_testing PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src/platform/${PLATFORM}/main.c
)

if (CONFIG_MAX32657_TESTING_BLINKY_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/blinky_capi/blinky_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_BUTTON_IRQ_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/button_irq_capi/button_irq_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_BUTTON_POLLING_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/button_polling_capi/button_polling_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_DMA_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/dma_capi/dma_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_I2C_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/i2c_capi/i2c_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_SPI_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/spi_capi/spi_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_TIMER_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/timer_capi/timer_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_TMR0_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/tmr0/tmr0.c
	)
endif()

if (CONFIG_MAX32657_TESTING_TRNG_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/trng_capi/trng_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_UART_ASYNC_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/uart_async_capi/uart_async_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_UART_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/uart_capi/uart_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_WDT_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/wdt_capi/wdt_capi.c
	)
endif()

if (CONFIG_MAX32657_TESTING_COUNT_EXACT_EXAMPLE)
	target_sources(max32657_testing PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/examples/count_exact/count_exact.c
	)
endif()

target_link_libraries(max32657_testing no-os)

config_platform_sdk(max32657_testing)
add_flash_target(max32657_testing)
post_build_config(max32657_testing)
