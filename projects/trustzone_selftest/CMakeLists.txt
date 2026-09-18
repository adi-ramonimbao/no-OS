# =============================================================================
# MAX32657 TrustZone CAPI self-test.
#
# A second project on the reusable no_os_add_maxim_trustzone_app() framework
# (proving it is not coupled to max32657_tz_hello). It produces a single
# combined ELF whose Non-Secure world is a SUPERSET of the maxim capi_selftest:
# it runs the full capi_loopback group set (GPIO, IRQ, SPI, TIMER, I2C, UART,
# DMA) reused verbatim from projects/capi_selftest with the same maxim
# parameters.h, plus two TrustZone-specific groups:
#
#   TRUSTZONE    - security-gateway tests. Exercise the __ns_entry veneers from
#               the Non-Secure world: a valid pointer increments; a NULL pointer
#               and a Secure-memory pointer are both rejected with -EINVAL by the
#               veneer's cmse_check_pointed_object() guard; a value-returning
#               gateway proves the Secure -> Non-Secure return path.
#   DMA_INSTANCE - reports which DMA controller this world drives (DMA0 in the
#               Non-Secure world), run just before the DMA transfers.
#
# The loopback groups, framework, common_data and the maxim parameters.h are
# compiled from projects/capi_selftest (no duplication); only the Non-Secure
# runner (with the GPIO-IRQ platform hooks) and the two TrustZone groups are new
# here. The Secure world hands over the console UART, GPIO0, SPI, I3C ("I2C") and
# TMR0 - and targets their NVIC lines to Non-Secure - before the tests run.
# Loopback wiring is identical to the maxim capi_selftest.
#
# Build (SDK location comes from the environment, not a -D cache var):
#   export MAXIM_LIBRARIES=/path/to/msdk/Libraries
#   cmake --preset max32657evkit -B build \
#         -DPROJECT_DEFCONFIG=trustzone_selftest/secure.conf
#   cmake --build build --target trustzone_selftest
# =============================================================================

set(_selftest_common  ${CMAKE_CURRENT_SOURCE_DIR}/../capi_selftest/src/common)
set(_selftest_loopback ${CMAKE_CURRENT_SOURCE_DIR}/../capi_selftest/src/examples/capi_loopback)
set(_selftest_maxim ${CMAKE_CURRENT_SOURCE_DIR}/../capi_selftest/src/platform/maxim)
set(_selftest_stm32 ${CMAKE_CURRENT_SOURCE_DIR}/../capi_selftest/src/platform/stm32)

if(PLATFORM STREQUAL "maxim")
    include(maxim_trustzone)
    no_os_add_maxim_trustzone_app(trustzone_selftest
        SECURE_SRC     src/platform/${PLATFORM}/secure/main.c
                       src/examples/selftest/selftest_secure.c
        SECURE_INC     src/platform/${PLATFORM}/secure    # holds partition_max32657.h + parameters.h
                       src/examples/selftest              # holds tz_gateways.h (shared contract)
        NONSECURE_SRC  src/platform/${PLATFORM}/nonsecure/main.c
                       src/platform/${PLATFORM}/nonsecure/tests/test_dma_instance.c
                       src/examples/selftest/selftest_nonsecure.c
                       src/examples/selftest/tests/test_trustzone.c
                       ${_selftest_common}/common_data.c
                       ${_selftest_common}/test_framework.c
                       ${_selftest_loopback}/tests/test_gpio.c
                       ${_selftest_loopback}/tests/test_irq.c
                       ${_selftest_loopback}/tests/test_spi.c
                       ${_selftest_loopback}/tests/test_timer.c
                       ${_selftest_loopback}/tests/test_i2c.c
                       ${_selftest_loopback}/tests/test_uart.c
                       ${_selftest_loopback}/tests/test_dma.c
        NONSECURE_INC  src/platform/${PLATFORM}/nonsecure
                       src/examples/selftest              # holds tz_gateways.h (shared contract)
                       ${_selftest_common}
                       ${_selftest_loopback}
                       ${_selftest_maxim})     # full maxim capi_selftest parameters.h
elseif(PLATFORM STREQUAL "stm32")
    include(stm32_trustzone)
    no_os_add_stm32_trustzone_app(trustzone_selftest
        SECURE_SRC     src/platform/${PLATFORM}/secure/main.c
                       src/examples/selftest/selftest_secure.c
        SECURE_INC     src/platform/${PLATFORM}/secure
                       src/examples/selftest
        NONSECURE_SRC  src/platform/${PLATFORM}/nonsecure/main.c
                       src/platform/${PLATFORM}/nonsecure/tests/test_dma_instance.c
                       ${_selftest_stm32}/i2c_platform_init.c
                       src/examples/selftest/selftest_nonsecure.c
                       src/examples/selftest/tests/test_trustzone.c
                       ${_selftest_common}/common_data.c
                       ${_selftest_common}/test_framework.c
                       ${_selftest_loopback}/tests/test_gpio.c
                       ${_selftest_loopback}/tests/test_irq.c
                       ${_selftest_loopback}/tests/test_spi.c
                       ${_selftest_loopback}/tests/test_timer.c
                       ${_selftest_loopback}/tests/test_i2c.c
                       ${_selftest_loopback}/tests/test_uart.c
                       ${_selftest_loopback}/tests/test_dma.c
        NONSECURE_INC  src/platform/${PLATFORM}/nonsecure
                       src/examples/selftest
                       ${_selftest_common}
                       ${_selftest_loopback})
else()
    message(FATAL_ERROR "trustzone_selftest: unsupported PLATFORM '${PLATFORM}' (supported: maxim, stm32).")
endif()
