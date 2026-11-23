add_executable(max32657_i2c_long_read)

target_sources(max32657_i2c_long_read PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
)

target_link_libraries(max32657_i2c_long_read no-os)

config_platform_sdk(max32657_i2c_long_read)
add_flash_target(max32657_i2c_long_read)
post_build_config(max32657_i2c_long_read)
