# =============================================================================
# MAX32657 TrustZone "hello" demo — superbuild.
#
# Produces a SINGLE combined ELF holding both the Secure and Non-Secure worlds,
# mirroring the MSDK Hello_World_TZ make flow (max32657.mk) in CMake.
#
# Two build trees are required because the Maxim toolchain fixes the CPU flags,
# -mcmse, and the -T<linker script> globally per build tree (forced cache vars
# in drivers/platform/maxim/toolchain.cmake). Secure needs -mcmse + max32657_s.ld;
# Non-Secure needs neither + max32657_ns.ld. So the SECURE tree (this, the outer
# build) drives a nested NON-SECURE tree and relinks the result into itself.
#
# Dependency chain (matches max32657.mk):
#   1. Compile the Secure objects (-mcmse, MSECURITY_MODE=SECURE).
#   2. Pass-A link -> secure_implib.o        (--cmse-implib, no NS image)
#   3. Nested NS build (links secure_implib.o) -> nonsecure.bin
#   4. Assemble nonsecure_load.S (.incbin nonsecure.bin) -> nonsecure.o
#   5. Pass-B link: Secure objects + nonsecure.o -> combined ELF
#
# Configure the outer tree with MSECURITY_MODE=SECURE (the max32657evkit-tz
# preset does this), e.g.:
#   cmake --preset max32657evkit-tz \
#         -DPROJECT_DEFCONFIG=max32657_tz_hello/secure.conf \
#         -DMAXIM_LIBRARIES=/path/to/msdk/Libraries
#   cmake --build build --target max32657_tz_hello
# =============================================================================

include(project_utils)

# -----------------------------------------------------------------------------
# NON-SECURE tree (nested). Entered when the root is configured with
# MSECURITY_MODE=NONSECURE by the SECURE tree's custom command below. Builds an
# ordinary no-OS CAPI application that links the Secure import library so the
# __ns_entry veneers (IncrementCount_S) resolve.
# -----------------------------------------------------------------------------
if(DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "NONSECURE")
    add_executable(max32657_tz_hello ${CMAKE_CURRENT_SOURCE_DIR}/src/nonsecure/main.c)
    target_include_directories(max32657_tz_hello PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/nonsecure)
    target_link_libraries(max32657_tz_hello no-os)

    # Secure import library (secure_implib.o), a full path passed by the outer
    # build. Supplies the SG veneer symbols; needed at NS link time only.
    if(DEFINED TZ_SECURE_IMPLIB)
        target_link_libraries(max32657_tz_hello ${TZ_SECURE_IMPLIB})
    endif()

    config_platform_sdk(max32657_tz_hello)
    return()
endif()

# -----------------------------------------------------------------------------
# SECURE tree (outer / default). Must be configured with MSECURITY_MODE=SECURE.
# -----------------------------------------------------------------------------
if(NOT (DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "SECURE"))
    message(FATAL_ERROR
        "max32657_tz_hello is a TrustZone project: the outer build must be "
        "configured with MSECURITY_MODE=SECURE. Use the 'max32657evkit-tz' "
        "preset, or add -DMSECURITY_MODE=SECURE to the cmake configure line.")
endif()

set(SECURE_SRC_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/src/secure)
set(NS_BUILD_DIR    ${CMAKE_BINARY_DIR}/nonsecure_build)
set(NS_ELF          ${NS_BUILD_DIR}/build/max32657_tz_hello.elf)
set(SECURE_IMPLIB   ${CMAKE_CURRENT_BINARY_DIR}/secure_implib.o)
set(NS_BIN          ${CMAKE_CURRENT_BINARY_DIR}/nonsecure.bin)
set(NS_OBJ          ${CMAKE_CURRENT_BINARY_DIR}/nonsecure.o)
set(GEN_LOADER_S    ${CMAKE_CURRENT_BINARY_DIR}/nonsecure_load.S)

# system_max32657.c (compiled into no-os) includes partition_max32657.h under
# -mcmse (__ARM_FEATURE_CMSE==3). That header is the Secure application's SAU
# config, so put src/secure on the no-os include path for this (Secure) tree.
target_include_directories(no-os PRIVATE ${SECURE_SRC_DIR})

# ---- 1. Secure application objects, shared by both Secure links -------------
# One OBJECT library so pass A (implib) and pass B (final) link byte-identical
# objects -> identical SG veneer addresses in both images.
add_library(max32657_tz_hello_app OBJECT ${SECURE_SRC_DIR}/main.c)
target_include_directories(max32657_tz_hello_app PRIVATE ${SECURE_SRC_DIR})
target_link_libraries(max32657_tz_hello_app PRIVATE no-os)

# ---- 2. Pass-A link: emit secure_implib.o ----------------------------------
# A throwaway Secure image (empty .nonsecure_flash) whose sole purpose is the
# --out-implib byproduct. It must NOT include nonsecure.o, or the implib would
# depend on the NS image that depends on the implib.
add_executable(max32657_tz_hello_implib $<TARGET_OBJECTS:max32657_tz_hello_app>)
target_link_libraries(max32657_tz_hello_implib no-os)
target_link_options(max32657_tz_hello_implib PRIVATE
    -Wl,--cmse-implib -Wl,--out-implib=${SECURE_IMPLIB})
add_custom_command(TARGET max32657_tz_hello_implib POST_BUILD
    BYPRODUCTS ${SECURE_IMPLIB}
    COMMAND ${CMAKE_COMMAND} -E true
    COMMENT "secure_implib.o emitted via --out-implib")

# ---- 3. Nested Non-Secure build -> nonsecure.bin ----------------------------
add_custom_command(
    OUTPUT ${NS_BIN}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${NS_BUILD_DIR}
    COMMAND ${CMAKE_COMMAND}
        -S ${CMAKE_SOURCE_DIR} -B ${NS_BUILD_DIR} -G ${CMAKE_GENERATOR}
        -DPLATFORM=${PLATFORM}
        -DBOARD=${BOARD}
        -DTARGET=${TARGET}
        -DTARGET_NUM=${TARGET_NUM}
        -DBOARD_CONFIG_FILE=${BOARD_CONFIG_FILE}
        -DUSE_VENDOR_TOOLCHAIN=${USE_VENDOR_TOOLCHAIN}
        -DMAXIM_LIBRARIES=${MAXIM_LIBRARIES}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DMSECURITY_MODE=NONSECURE
        -DPROJECT_DEFCONFIG=max32657_tz_hello/nonsecure.conf
        -DTZ_SECURE_IMPLIB=${SECURE_IMPLIB}
    COMMAND ${CMAKE_COMMAND} --build ${NS_BUILD_DIR} --target max32657_tz_hello
    COMMAND ${CMAKE_OBJCOPY} -O binary ${NS_ELF} ${NS_BIN}
    DEPENDS max32657_tz_hello_implib ${SECURE_IMPLIB}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/nonsecure/main.c
            ${CMAKE_CURRENT_SOURCE_DIR}/src/nonsecure/parameters.h
            ${CMAKE_CURRENT_SOURCE_DIR}/nonsecure.conf
    COMMENT "Building nested Non-Secure image -> nonsecure.bin"
    VERBATIM)

# ---- 4. Assemble nonsecure_load.S -> nonsecure.o ---------------------------
# The linker script's KEEP(*nonsecure.o) pins the embedded NS image, so the
# object MUST be named literally nonsecure.o. configure_file bakes the absolute
# nonsecure.bin path into the .incbin.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nonsecure_load.S.in ${GEN_LOADER_S} @ONLY)
separate_arguments(NS_ASM_FLAGS UNIX_COMMAND "${CMAKE_ASM_FLAGS}")
add_custom_command(
    OUTPUT ${NS_OBJ}
    COMMAND ${CMAKE_ASM_COMPILER} ${NS_ASM_FLAGS} -c -o ${NS_OBJ} ${GEN_LOADER_S}
    DEPENDS ${NS_BIN} ${GEN_LOADER_S}
    COMMENT "Embedding nonsecure.bin -> nonsecure.o"
    VERBATIM)
add_custom_target(max32657_tz_hello_nsobj DEPENDS ${NS_OBJ})

# ---- 5. Pass-B link: combined Secure + Non-Secure ELF ----------------------
add_executable(max32657_tz_hello $<TARGET_OBJECTS:max32657_tz_hello_app>)
add_dependencies(max32657_tz_hello max32657_tz_hello_nsobj)
target_link_libraries(max32657_tz_hello no-os ${NS_OBJ})

# ---- 6. Post-build / flash on the single combined ELF ----------------------
# NOTE: the generic post_build_config() is deliberately NOT used here. It runs
# `objcopy -O binary`, which zero-fills the ~256 MB gap between the Non-Secure
# alias region (.nonsecure_flash @ 0x01080000) and the Secure alias region
# (.text @ 0x11000000) -> a multi-hundred-MB .bin (the same failure the shared
# function already documents and skips for xilinx/MicroBlaze). The Intel HEX
# carries per-region address records, so it stays small and is the flashable
# artifact. Emit .hex + size only.
config_platform_sdk(max32657_tz_hello)
generate_openocd_config()
add_flash_target(max32657_tz_hello)
add_custom_command(TARGET max32657_tz_hello POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:max32657_tz_hello> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/max32657_tz_hello.hex
    COMMAND ${CMAKE_COMMAND} -E echo "Binary size:" && (${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:max32657_tz_hello> || ${CMAKE_COMMAND} -E true)
    COMMENT "Generating max32657_tz_hello.hex (combined Secure+Non-Secure)")
