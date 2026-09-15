# =============================================================================
# no_os_add_maxim_trustzone_app() - reusable MAX32657 TrustZone superbuild.
#
# Produces a SINGLE combined ELF holding both the Secure and Non-Secure worlds,
# mirroring the MSDK Hello_World_TZ make flow (max32657.mk) in CMake. A project
# supplies only its sources/includes; this module drives the whole two-tree
# superbuild:
#
#   1. Compile the Secure objects (-mcmse, MSECURITY_MODE=SECURE).
#   2. Pass-A link -> secure_implib.o        (--cmse-implib, no NS image)
#   3. Nested NON-SECURE build (links secure_implib.o) -> nonsecure.bin
#   4. Assemble nonsecure_load.S (.incbin nonsecure.bin) -> nonsecure.o
#   5. Pass-B link: Secure objects + nonsecure.o -> combined ELF
#
# Two build trees are required because the Maxim toolchain fixes the CPU flags,
# -mcmse, and the -T<linker script> globally per build tree (forced cache vars
# in drivers/platform/maxim/toolchain.cmake). Secure needs -mcmse + max32657_s.ld;
# Non-Secure needs neither + max32657_ns.ld. So the SECURE tree (the outer build)
# drives a nested NON-SECURE tree and relinks the result into itself. The same
# call also handles the nested NON-SECURE tree (entered when the root is
# reconfigured with MSECURITY_MODE=NONSECURE by step 3), so a project calls this
# exactly once and this module dispatches on MSECURITY_MODE.
#
# The outer build must be configured with MSECURITY_MODE=SECURE. A project
# declares this without a preset via a projects/<name>/trustzone.cmake marker
# (picked up before the toolchain loads); -DMSECURITY_MODE=SECURE also works.
#
# Usage (projects/<name>/CMakeLists.txt):
#   include(maxim_trustzone)
#   no_os_add_maxim_trustzone_app(<name>
#       SECURE_SRC     src/secure/main.c
#       SECURE_INC     src/secure            # dir holding partition_max32657.h
#       NONSECURE_SRC  src/nonsecure/main.c
#       NONSECURE_INC  src/nonsecure
#       [SECURE_CONF    secure.conf]         # default <name>/secure.conf
#       [NONSECURE_CONF nonsecure.conf])     # default <name>/nonsecure.conf
# =============================================================================

include(project_utils)

# Loader stub template shipped with the framework (not per-project).
set(_NO_OS_MAXIM_TZ_LOADER_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/maxim_trustzone_load.S.in"
    CACHE INTERNAL "Framework .incbin loader stub for TrustZone superbuilds")

# Resolve a list of (possibly relative) paths to absolute against the caller's
# source directory (the project dir), storing the result in OUT_VAR.
function(_no_os_tz_abspaths OUT_VAR)
    set(_result "")
    foreach(_p ${ARGN})
        if(IS_ABSOLUTE "${_p}")
            list(APPEND _result "${_p}")
        else()
            list(APPEND _result "${CMAKE_CURRENT_SOURCE_DIR}/${_p}")
        endif()
    endforeach()
    set(${OUT_VAR} "${_result}" PARENT_SCOPE)
endfunction()

function(no_os_add_maxim_trustzone_app APP_NAME)
    cmake_parse_arguments(TZ
        ""                                              # options
        "SECURE_CONF;NONSECURE_CONF"                    # one-value
        "SECURE_SRC;SECURE_INC;NONSECURE_SRC;NONSECURE_INC"  # multi-value
        ${ARGN})

    if(NOT TZ_SECURE_SRC OR NOT TZ_NONSECURE_SRC)
        message(FATAL_ERROR
            "no_os_add_maxim_trustzone_app(${APP_NAME}): SECURE_SRC and "
            "NONSECURE_SRC are required.")
    endif()

    # Default the defconfigs to <project>/{secure,nonsecure}.conf. PROJECT_DEFCONFIG
    # paths are relative to projects/, so key them off NO_OS_PROJECT_NAME.
    if(NOT TZ_SECURE_CONF)
        set(TZ_SECURE_CONF "${NO_OS_PROJECT_NAME}/secure.conf")
    endif()
    if(NOT TZ_NONSECURE_CONF)
        set(TZ_NONSECURE_CONF "${NO_OS_PROJECT_NAME}/nonsecure.conf")
    endif()

    _no_os_tz_abspaths(_secure_src    ${TZ_SECURE_SRC})
    _no_os_tz_abspaths(_secure_inc    ${TZ_SECURE_INC})
    _no_os_tz_abspaths(_nonsecure_src ${TZ_NONSECURE_SRC})
    _no_os_tz_abspaths(_nonsecure_inc ${TZ_NONSECURE_INC})

    # -------------------------------------------------------------------------
    # NON-SECURE tree (nested): ordinary no-OS CAPI application that links the
    # Secure import library so the __ns_entry SG veneers resolve. Entered when
    # the root is reconfigured with MSECURITY_MODE=NONSECURE by step 3 below.
    # -------------------------------------------------------------------------
    if(DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "NONSECURE")
        add_executable(${APP_NAME} ${_nonsecure_src})
        if(_nonsecure_inc)
            target_include_directories(${APP_NAME} PRIVATE ${_nonsecure_inc})
        endif()
        target_link_libraries(${APP_NAME} no-os)
        # secure_implib.o full path passed by the outer build; NS link time only.
        if(DEFINED TZ_SECURE_IMPLIB)
            target_link_libraries(${APP_NAME} ${TZ_SECURE_IMPLIB})
        endif()
        config_platform_sdk(${APP_NAME})
        return()
    endif()

    # -------------------------------------------------------------------------
    # SECURE tree (outer / default). Requires MSECURITY_MODE=SECURE.
    # -------------------------------------------------------------------------
    if(NOT (DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "SECURE"))
        message(FATAL_ERROR
            "${APP_NAME} is a TrustZone project: the outer build must be configured "
            "with MSECURITY_MODE=SECURE. This is set automatically by "
            "projects/${NO_OS_PROJECT_NAME}/trustzone.cmake; if that marker is "
            "missing, add -DMSECURITY_MODE=SECURE to the cmake configure line.")
    endif()

    set(_ns_build_dir  ${CMAKE_BINARY_DIR}/nonsecure_build)
    set(_ns_elf        ${_ns_build_dir}/build/${APP_NAME}.elf)
    set(_secure_implib ${CMAKE_CURRENT_BINARY_DIR}/secure_implib.o)
    set(_ns_bin        ${CMAKE_CURRENT_BINARY_DIR}/nonsecure.bin)
    set(_ns_obj        ${CMAKE_CURRENT_BINARY_DIR}/nonsecure.o)
    set(_gen_loader_s  ${CMAKE_CURRENT_BINARY_DIR}/nonsecure_load.S)
    set(_ns_conf_abs   ${CMAKE_SOURCE_DIR}/projects/${TZ_NONSECURE_CONF})

    # system_max32657.c (compiled into no-os) includes partition_max32657.h under
    # -mcmse (__ARM_FEATURE_CMSE==3): the Secure app's SAU config. Expose the
    # Secure include dirs to no-os for this (Secure) tree.
    if(_secure_inc)
        target_include_directories(no-os PRIVATE ${_secure_inc})
    endif()

    # ---- 1. Secure objects, shared by both Secure links ----------------------
    # One OBJECT library so pass A (implib) and pass B (final) link byte-identical
    # objects -> identical SG veneer addresses in both images.
    add_library(${APP_NAME}_app OBJECT ${_secure_src})
    if(_secure_inc)
        target_include_directories(${APP_NAME}_app PRIVATE ${_secure_inc})
    endif()
    target_link_libraries(${APP_NAME}_app PRIVATE no-os)

    # ---- 2. Pass-A link: emit secure_implib.o --------------------------------
    # A throwaway Secure image (no NS image) whose sole purpose is the
    # --out-implib byproduct. It must NOT include nonsecure.o, or the implib
    # would depend on the NS image that depends on the implib.
    add_executable(${APP_NAME}_implib $<TARGET_OBJECTS:${APP_NAME}_app>)
    target_link_libraries(${APP_NAME}_implib no-os)
    target_link_options(${APP_NAME}_implib PRIVATE
        -Wl,--cmse-implib -Wl,--out-implib=${_secure_implib})
    add_custom_command(TARGET ${APP_NAME}_implib POST_BUILD
        BYPRODUCTS ${_secure_implib}
        COMMAND ${CMAKE_COMMAND} -E true
        COMMENT "secure_implib.o emitted via --out-implib")

    # ---- 3. Nested Non-Secure build -> nonsecure.bin -------------------------
    # The SDK location is NOT a -D cache var: drivers/platform/maxim/toolchain.cmake
    # resolves MAXIM_LIBRARIES only from CFS or the MAXIM_LIBRARIES *environment*
    # variable. Pass the parent's already-resolved path to the nested configure
    # through the environment (via `cmake -E env`) so it works whether the parent
    # got it from CFS or from $MAXIM_LIBRARIES.
    add_custom_command(
        OUTPUT ${_ns_bin}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_ns_build_dir}
        COMMAND ${CMAKE_COMMAND} -E env "MAXIM_LIBRARIES=${MAXIM_LIBRARIES}"
            ${CMAKE_COMMAND}
            -S ${CMAKE_SOURCE_DIR} -B ${_ns_build_dir} -G ${CMAKE_GENERATOR}
            -DPLATFORM=${PLATFORM}
            -DBOARD=${BOARD}
            -DTARGET=${TARGET}
            -DTARGET_NUM=${TARGET_NUM}
            -DBOARD_CONFIG_FILE=${BOARD_CONFIG_FILE}
            -DUSE_VENDOR_TOOLCHAIN=${USE_VENDOR_TOOLCHAIN}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DMSECURITY_MODE=NONSECURE
            -DPROJECT_DEFCONFIG=${TZ_NONSECURE_CONF}
            -DTZ_SECURE_IMPLIB=${_secure_implib}
        COMMAND ${CMAKE_COMMAND} --build ${_ns_build_dir} --target ${APP_NAME}
        COMMAND ${CMAKE_OBJCOPY} -O binary ${_ns_elf} ${_ns_bin}
        DEPENDS ${APP_NAME}_implib ${_secure_implib} ${_nonsecure_src} ${_ns_conf_abs}
        COMMENT "Building nested Non-Secure image -> nonsecure.bin"
        VERBATIM)

    # ---- 4. Assemble nonsecure_load.S -> nonsecure.o -------------------------
    # The linker script's KEEP(*nonsecure.o) pins the embedded NS image, so the
    # object MUST be named literally nonsecure.o. configure_file bakes the
    # absolute nonsecure.bin path into the .incbin via @NS_BIN@.
    set(NS_BIN ${_ns_bin})
    configure_file(${_NO_OS_MAXIM_TZ_LOADER_TEMPLATE} ${_gen_loader_s} @ONLY)
    separate_arguments(_ns_asm_flags UNIX_COMMAND "${CMAKE_ASM_FLAGS}")
    add_custom_command(
        OUTPUT ${_ns_obj}
        COMMAND ${CMAKE_ASM_COMPILER} ${_ns_asm_flags} -c -o ${_ns_obj} ${_gen_loader_s}
        DEPENDS ${_ns_bin} ${_gen_loader_s}
        COMMENT "Embedding nonsecure.bin -> nonsecure.o"
        VERBATIM)
    add_custom_target(${APP_NAME}_nsobj DEPENDS ${_ns_obj})

    # ---- 5. Pass-B link: combined Secure + Non-Secure ELF -------------------
    add_executable(${APP_NAME} $<TARGET_OBJECTS:${APP_NAME}_app>)
    add_dependencies(${APP_NAME} ${APP_NAME}_nsobj)
    target_link_libraries(${APP_NAME} no-os ${_ns_obj})

    # ---- 6. Post-build / flash on the single combined ELF -------------------
    # The generic post_build_config() is deliberately NOT used: it runs
    # `objcopy -O binary`, which zero-fills the huge gap between the Non-Secure
    # alias region (.nonsecure_flash @ 0x01080000) and the Secure alias region
    # (.text @ 0x11000000) -> a multi-hundred-MB .bin. The Intel HEX carries
    # per-region address records, so it stays small and is the flashable artifact.
    config_platform_sdk(${APP_NAME})
    generate_openocd_config()
    add_flash_target(${APP_NAME})
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${APP_NAME}> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}.hex
        COMMAND ${CMAKE_COMMAND} -E echo "Binary size:" && (${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${APP_NAME}> || ${CMAKE_COMMAND} -E true)
        COMMENT "Generating ${APP_NAME}.hex (combined Secure+Non-Secure)")
endfunction()
