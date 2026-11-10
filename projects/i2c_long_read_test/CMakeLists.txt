# Bench test for the Maxim no-OS I2C driver's long-read path (reads over
# 256 bytes, interactive receive mode). Not for upstream.
#
# The variant (defconfig name) picks the role:
#   loopback   - max32690evkit: no-OS controller on I2C0 + target on I2C2
#   controller - max32655fthr:  no-OS controller on I2C2
#   target     - max32690evkit: target on I2C2 only, logs each transaction
#   build_only - any Maxim target: controller on I2C0, to compile and link
#                the driver for families with no bench setup
add_executable(i2c_long_read_test)

get_filename_component(LRT_ROLE "${PROJECT_DEFCONFIG}" NAME_WE)

if(LRT_ROLE STREQUAL "loopback")
    if(NOT BOARD STREQUAL "max32690evkit")
        message(FATAL_ERROR "loopback needs max32690evkit, not ${BOARD}")
    endif()
    set(LRT_DEFS LRT_CONTROLLER=1 LRT_TARGET=1 LRT_CTL_I2C=0 LRT_UART=2 LRT_ROLE_NAME="loopback")
elseif(LRT_ROLE STREQUAL "target")
    if(NOT BOARD STREQUAL "max32690evkit")
        message(FATAL_ERROR "target needs max32690evkit, not ${BOARD}")
    endif()
    set(LRT_DEFS LRT_CONTROLLER=0 LRT_TARGET=1 LRT_UART=2 LRT_ROLE_NAME="target")
elseif(LRT_ROLE STREQUAL "controller")
    if(NOT BOARD STREQUAL "max32655fthr")
        message(FATAL_ERROR "controller needs max32655fthr, not ${BOARD}")
    endif()
    set(LRT_DEFS LRT_CONTROLLER=1 LRT_TARGET=0 LRT_CTL_I2C=2 LRT_UART=0 LRT_ROLE_NAME="controller")
elseif(LRT_ROLE STREQUAL "build_only")
    set(LRT_DEFS LRT_CONTROLLER=1 LRT_TARGET=0 LRT_CTL_I2C=0 LRT_UART=0 LRT_ROLE_NAME="build_only")
else()
    message(FATAL_ERROR "unknown variant '${LRT_ROLE}'")
endif()

target_sources(i2c_long_read_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lrt_controller.c
)
if(LRT_ROLE STREQUAL "loopback" OR LRT_ROLE STREQUAL "target")
    target_sources(i2c_long_read_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lrt_target.c
    )
    # i2c_reva.h, for MXC_I2C_RevA_Init()
    target_include_directories(i2c_long_read_test PRIVATE
        ${MAXIM_LIBRARIES}/PeriphDrivers/Source/I2C
        ${MAXIM_LIBRARIES}/PeriphDrivers/Source/DMA
    )
endif()

target_compile_definitions(i2c_long_read_test PRIVATE ${LRT_DEFS})
target_include_directories(i2c_long_read_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(i2c_long_read_test no-os)

config_platform_sdk(i2c_long_read_test)
add_flash_target(i2c_long_read_test)
post_build_config(i2c_long_read_test)
