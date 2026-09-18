# =============================================================================
# no_os_add_stm32_trustzone_app() - reusable STM32 (Armv8-M / CMSE) TrustZone
# superbuild, mirroring cmake/maxim/maxim_trustzone.cmake's API shape.
#
# Unlike Maxim (which links against static, pre-baked SDK linker scripts via
# MAXIM_LIBRARIES), STM32's Secure/Non-Secure HAL bring-up, linker scripts and
# partition header are NOT static SDK assets: they come from STM32CubeMX's own
# native TrustZone project generation (Project Manager > Enable TrustZone in
# the .ioc -> Mcu.ContextProject=TrustZoneEnabled), invoked the same way the
# ordinary (non-TZ) STM32 flow already invokes CubeMX headlessly per project
# (see cmake/stm32/stm32_platform_sdk.cmake). Generation produces, next to the
# .ioc's usual output dir:
#   Secure/     - Core/{Inc,Src,Startup}, own CMakeLists.txt + mx-generated.cmake,
#                 own linker script (<chip>_FLASH_s.ld), own partition_<chip>.h
#   NonSecure/  - same shape, <chip>_FLASH_ns.ld, no partition header
#   Drivers/    - CMSIS + HAL, shared at the project root (not duplicated)
#   Secure_nsclib/ - CubeMX's own NSC gateway stub (secure_nsc.h); this
#                 framework does not use it -- projects define their own
#                 __attribute__((cmse_nonsecure_entry)) gateways instead, same
#                 as the MAX32657 TZ projects, so the gateway code stays fully
#                 portable between vendors.
# Each world's mx-generated.cmake is the SAME "stm32cubemx INTERFACE lib +
# STM32_Drivers OBJECT lib" shape config_stm32_sdk() already normalizes the
# non-TZ output to -- just already in that shape and split per-world, so no
# Linux/Windows layout patching is needed here. It targets ${CMAKE_PROJECT_NAME}
# directly (not a parameter), so this module temporarily aliases that variable
# to our own build target before including it.
#
# Like Maxim, CMAKE_C_FLAGS/CMAKE_EXE_LINKER_FLAGS carrying -mcmse are set
# GLOBALLY per build tree (drivers/platform/stm32/toolchain.cmake), so Secure
# and Non-Secure genuinely cannot coexist in one configure; a combined app
# still needs a nested Non-Secure build. Unlike Maxim though, STM32 needs no
# two-pass link or embedded NS blob: CubeMX's own Secure mx-generated.cmake
# already emits --out-implib on a single ordinary link, and ST's own examples
# treat Secure/Non-Secure as two independently flashable images in disjoint
# flash regions -- so a "combined" app here is simply: build Secure (hex +
# implib) -> nested build Non-Secure against that implib (hex) -> merge hexes
# (reusing the vendor-neutral cmake/maxim/maxim_tz_merge_hex.py as-is).
#
# The outer build must be configured with MSECURITY_MODE=SECURE. A project
# declares this without a preset via a projects/<name>/trustzone.cmake marker
# (picked up before the toolchain loads); -DMSECURITY_MODE=SECURE also works.
#
# Usage (projects/<name>/CMakeLists.txt):
#   include(stm32_trustzone)
#   no_os_add_stm32_trustzone_app(<name>
#       SECURE_SRC     src/platform/stm32/secure/main.c
#       SECURE_INC     src/platform/stm32/secure
#       NONSECURE_SRC  src/platform/stm32/nonsecure/main.c
#       NONSECURE_INC  src/platform/stm32/nonsecure
#       [SECURE_CONF    secure.conf]         # default <name>/secure.conf
#       [NONSECURE_CONF nonsecure.conf])     # default <name>/nonsecure.conf
# =============================================================================

include(project_utils)
include(stm32_platform_sdk)

# Resolve a list of (possibly relative) paths to absolute against the caller's
# source directory (the project dir), storing the result in OUT_VAR.
function(_no_os_stm32_tz_abspaths OUT_VAR)
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

# Gate 0: make sure the .ioc actually requests a TrustZone project. A plain
# (non-TZ) .ioc would silently produce a single-context build with none of the
# Secure/NonSecure/Drivers layout this module depends on, so fail loudly with
# a clear fix instead of a confusing "file not found" later.
function(_no_os_stm32_tz_require_enabled IOC_FILE)
    file(STRINGS "${IOC_FILE}" _ctx REGEX "^Mcu\\.ContextProject=")
    if(NOT _ctx MATCHES "TrustZoneEnabled")
        message(FATAL_ERROR
            "${IOC_FILE} does not have TrustZone enabled "
            "(Mcu.ContextProject must be TrustZoneEnabled). Enable it in "
            "STM32CubeMX: Project Manager -> Enable TrustZone, then re-save the .ioc.")
    endif()
endfunction()

# Rename CubeMX's generated main() to stm32_init() and strip its infinite loop
# so it returns, exactly like the non-TZ flow's cmake/stm32/stm32_patch_cubemx.cmake
# does for the ordinary (single-context) generated project. Every STM32 project
# calls stm32_init() first thing in its own main() (drivers/platform/stm32/stm32_hal.h);
# this keeps that same convention for each TrustZone world, so CAPI drivers get a
# fully brought-up peripheral (e.g. huart3.Instance already set by
# MX_USART3_UART_Init()) before our own code touches it. Applied once per fresh
# generation (NEED_REGEN), not on every configure -- the strings would no longer
# match a second time and silently no-op, which is fine, but there is no need to
# redo the file(WRITE) every reconfigure.
#
# Also strips syscalls.c from mx-generated.cmake's source list, same as the
# non-TZ patch and for the same reason: stm32_capi_uart.c defines its own
# _write/_close/_fstat/_isatty/_lseek newlib syscall overrides, which conflict
# (multiple definition) with CubeMX's generated syscalls.c.
function(_no_os_stm32_tz_patch_main GEN_DIR)
    set(_main "${GEN_DIR}/Core/Src/main.c")
    file(READ "${_main}" _content)
    string(REPLACE "main(" "stm32_init(" _content "${_content}")
    string(REPLACE
"  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }"
"  /* Initialization complete, return to caller */
  return 0;"
        _content "${_content}")
    file(WRITE "${_main}" "${_content}")

    set(_gen_cmake "${GEN_DIR}/mx-generated.cmake")
    file(READ "${_gen_cmake}" _gen_content)
    string(REGEX REPLACE "[^\n]*syscalls\\.c[^\n]*\n" "" _gen_content "${_gen_content}")
    file(WRITE "${_gen_cmake}" "${_gen_content}")

    # CubeMX generates its own do-nothing SecureFault_Handler() (an infinite
    # loop) into the Secure world's Core/Src/stm32h5xx_it.c. Weaken it so a
    # project's own strong definition (e.g. trustzone_secure_only's
    # src/platform/stm32/secure/main.c) can override it -- a plain second
    # definition would be a multiple-definition link error. No-op on the
    # Non-Secure tree: that world's stm32h5xx_it.c never defines this handler.
    set(_it "${GEN_DIR}/Core/Src/stm32h5xx_it.c")
    if(EXISTS "${_it}")
        file(READ "${_it}" _it_content)
        string(REPLACE "void SecureFault_Handler(void)"
            "void __attribute__((weak)) SecureFault_Handler(void)"
            _it_content "${_it_content}")
        file(WRITE "${_it}" "${_it_content}")
    endif()
endfunction()

# Ensure STM32CubeMX has generated the Secure/NonSecure project pair for this
# .ioc, regenerating (like config_stm32_sdk()) only when the .ioc or the CubeMX
# binary changed. Sets SECURE_GEN_DIR / NONSECURE_GEN_DIR / DRIVERS_GEN_DIR in
# the caller's scope.
function(_no_os_stm32_tz_ensure_generated)
    resolve_stm32_ioc_file()
    get_filename_component(IOC_NAME "${IOC_FILE}" NAME_WE)
    set(_board_build_dir "${CMAKE_CURRENT_BINARY_DIR}/${BOARD}_build")
    set(_project_dir "${_board_build_dir}/${IOC_NAME}")

    _no_os_stm32_tz_require_enabled("${IOC_FILE}")

    stm32_check_cubemx_stale("${IOC_FILE}" "${_project_dir}" "${_project_dir}/Secure")
    if(NEED_REGEN)
        file(REMOVE_RECURSE "${_project_dir}")

        no_os_run_checked(
            WHAT "patching the STM32CubeMX config"
            COMMAND ${CMAKE_COMMAND}
            -D STM32_TEMPLATE_FILE=${NO_OS_DIR}/cmake/stm32/stm32_cubemx_template.cubemx
            -D STM32_TARGET_BUILD=${CMAKE_CURRENT_BINARY_DIR}
            -D STM32_IOC_PATH=${CONFIG_STM32_IOC_PATH}
            -D SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -D BOARD=${BOARD}
            -P "${NO_OS_DIR}/cmake/stm32/stm32_patch_cubemx_config.cmake")

        message(STATUS "Generating STM32CubeMX TrustZone project (Secure+NonSecure)...")
        execute_process(
            COMMAND ${STM32CUBEMX_LAUNCH_COMMAND} -q "${CMAKE_CURRENT_BINARY_DIR}/stm32cubemx_config.cubemx"
            RESULT_VARIABLE CUBEMX_RESULT
            OUTPUT_VARIABLE CUBEMX_OUTPUT
            ERROR_VARIABLE CUBEMX_ERROR)
        message(STATUS "CubeMX exit code: ${CUBEMX_RESULT}")
        if(CUBEMX_OUTPUT)
            message(STATUS "CubeMX stdout:\n${CUBEMX_OUTPUT}")
        endif()
        if(CUBEMX_ERROR)
            message(STATUS "CubeMX stderr:\n${CUBEMX_ERROR}")
        endif()
        if(NOT CUBEMX_RESULT EQUAL 0 OR
           NOT EXISTS "${_project_dir}/Secure/mx-generated.cmake" OR
           NOT EXISTS "${_project_dir}/NonSecure/mx-generated.cmake")
            message(FATAL_ERROR
                "STM32CubeMX TrustZone project generation failed or produced an "
                "unexpected layout (launch command: ${STM32CUBEMX_LAUNCH_COMMAND}). "
                "Expected ${_project_dir}/{Secure,NonSecure}/mx-generated.cmake. "
                "See the CubeMX output above.")
        endif()

        _no_os_stm32_tz_patch_main("${_project_dir}/Secure")
        _no_os_stm32_tz_patch_main("${_project_dir}/NonSecure")

        stm32_write_cubemx_stamp("${IOC_FILE}" "${_project_dir}")
    endif()

    set(SECURE_GEN_DIR    "${_project_dir}/Secure"    PARENT_SCOPE)
    set(NONSECURE_GEN_DIR "${_project_dir}/NonSecure" PARENT_SCOPE)
    set(DRIVERS_GEN_DIR   "${_project_dir}/Drivers"   PARENT_SCOPE)
    set(IOC_NAME_OUT      "${IOC_NAME}"                PARENT_SCOPE)
endfunction()

# Wire one world's CubeMX-generated mx-generated.cmake onto BUILD_TARGET.
# mx-generated.cmake targets ${CMAKE_PROJECT_NAME} directly (it is normally
# include()'d from a CubeMX-owned project() of that exact name), so this
# temporarily aliases that variable to our own target rather than patching the
# generated file (which is regenerated -- and would need re-patching -- on
# every CubeMX run). It already adds EVERYTHING CubeMX generated: application
# sources (main.c/stm32h5xx_it.c/hal_msp.c/sysmem.c/syscalls.c/startup .s -- via
# MX_Application_Src), the STM32_Drivers OBJECT lib (HAL + BSP .c sources, e.g.
# stm32h5xx_hal_gtzc.c which implements the per-peripheral/per-pin delegation
# MX_GTZC_{S,NS}_Init() calls -- called from the now-renamed stm32_init(), so
# projects need NO manual HAL_GTZC_*/HAL_GPIO_ConfigPinAttributes calls of their
# own), and the stm32cubemx INTERFACE lib (include dirs/defines, propagated
# transitively through STM32_Drivers). Do NOT also list these sources/dirs
# manually in a project's CMakeLists -- they would be added twice.
function(_no_os_stm32_tz_apply_mx_generated BUILD_TARGET GEN_DIR)
    if(NOT EXISTS "${GEN_DIR}/mx-generated.cmake")
        message(FATAL_ERROR "stm32_trustzone: ${GEN_DIR}/mx-generated.cmake not found.")
    endif()
    set(CMAKE_PROJECT_NAME ${BUILD_TARGET})
    # mx-generated.cmake also builds its source/include paths off
    # ${CMAKE_CURRENT_SOURCE_DIR} (e.g. "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/main.c"),
    # expecting to be add_subdirectory()'d FROM GEN_DIR. include() does not
    # change directory scope (only add_subdirectory() does), so without this
    # override those paths resolve against the INCLUDING project's own source
    # dir instead and every target_sources() call fails to find its files.
    set(CMAKE_CURRENT_SOURCE_DIR ${GEN_DIR})
    include("${GEN_DIR}/mx-generated.cmake")
endfunction()

# CubeMX's own generated NonSecure/mx-generated.cmake unconditionally links
# "../Secure_nsclib/secure_nsclib.o" (its own convention for where the Secure
# CMSE import library lives, relative to the project dir) -- baked into the
# generated file itself, not something we control. That path only resolves
# naturally when Secure and Non-Secure are generated together AND the Secure
# world was built INTO that same tree first; a nested/separate Non-Secure build
# (this framework's normal case, since -mcmse is a whole-tree toolchain flag)
# has its own, separately generated copy of that tree with no Secure link ever
# run in it. Stage a copy of the REAL Secure implib at that exact expected path
# as a build-order dependency of BUILD_TARGET, regardless of where it actually
# came from (nested combined build or a separate producer project), so the
# generated link line always resolves.
function(_no_os_stm32_tz_stage_secure_implib BUILD_TARGET NONSECURE_GEN_DIR SECURE_IMPLIB)
    set(_staged ${NONSECURE_GEN_DIR}/../Secure_nsclib/secure_nsclib.o)
    add_custom_command(
        OUTPUT ${_staged}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NONSECURE_GEN_DIR}/../Secure_nsclib
        COMMAND ${CMAKE_COMMAND} -E copy ${SECURE_IMPLIB} ${_staged}
        DEPENDS ${SECURE_IMPLIB}
        COMMENT "Staging Secure implib at the path CubeMX's generated NonSecure link expects")
    add_custom_target(${BUILD_TARGET}_secure_implib_stage DEPENDS ${_staged})
    add_dependencies(${BUILD_TARGET} ${BUILD_TARGET}_secure_implib_stage)
endfunction()

# Create a `flash` target that programs a combined HEX (Secure + Non-Secure) via
# whichever probe is configured, mirroring maxim_trustzone.cmake's identically-
# named helper. The generic add_flash_target() flashes <name>.elf on OpenOCD,
# which for any TrustZone app here is only ONE world (STM32 has no single ELF
# holding both, unlike Maxim's embedded-NS-blob combined image); the merged HEX
# is the real deployment artifact, so program that instead.
function(_no_os_stm32_tz_add_hex_flash_target APP_NAME HEX)
    if(PROBE STREQUAL "jlink")
        add_custom_target(flash
            COMMAND "${VENV_PYTHON_EXE}" "${NO_OS_DIR}/tools/scripts/jlink.py"
                --device "${TARGET}" --file "${HEX}"
            DEPENDS ${APP_NAME}
            COMMENT "Flashing ${TARGET} (combined Secure+Non-Secure)..."
            VERBATIM)
    elseif(OPENOCD_PATH)
        add_custom_target(flash
            COMMAND ${OPENOCD_PATH}
                -s ${OPENOCD_SCRIPTS}
                -f ${CMAKE_CURRENT_BINARY_DIR}/openocd.cfg
                -c "program ${HEX} verify reset exit"
            DEPENDS ${APP_NAME}
            COMMENT "Flashing ${TARGET} (combined Secure+Non-Secure)..."
            VERBATIM)
    else()
        message(STATUS
            "No flash probe available for ${APP_NAME}; combined HEX at ${HEX}")
    endif()
endfunction()

# =============================================================================
# no_os_add_stm32_trustzone_secure_app() - Secure-only PRODUCER. See the
# Maxim equivalent's doc comment for the producer/consumer split rationale.
#
# Deliverables (in CMAKE_RUNTIME_OUTPUT_DIRECTORY):
#   <name>.hex            - the Secure image (Intel HEX)
#   <name>_implib.o        - CMSE import library (the gateway veneer addresses)
#   <name>_contract.cmake  - SECURE_HEX/SECURE_IMPLIB paths for the consumer
#
# NO_FLASH: skip creating the generic `flash` target. Used internally by
# no_os_add_stm32_trustzone_app(), whose combined HEX (not this Secure-only
# ELF) is the real deployment artifact -- it creates the `flash` target itself
# once the merge is known to have succeeded.
# =============================================================================
function(no_os_add_stm32_trustzone_secure_app APP_NAME)
    cmake_parse_arguments(TZ "NO_FLASH" "SECURE_CONF" "SECURE_SRC;SECURE_INC" ${ARGN})

    if(NOT TZ_SECURE_SRC)
        message(FATAL_ERROR
            "no_os_add_stm32_trustzone_secure_app(${APP_NAME}): SECURE_SRC is required.")
    endif()
    if(NOT (DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "SECURE"))
        message(FATAL_ERROR
            "${APP_NAME} is a Secure-only TrustZone project: the build must be "
            "configured with MSECURITY_MODE=SECURE. This is set automatically by "
            "projects/${NO_OS_PROJECT_NAME}/trustzone.cmake; if that marker is "
            "missing, add -DMSECURITY_MODE=SECURE to the cmake configure line.")
    endif()

    _no_os_stm32_tz_abspaths(_secure_src ${TZ_SECURE_SRC})
    _no_os_stm32_tz_abspaths(_secure_inc ${TZ_SECURE_INC})

    _no_os_stm32_tz_ensure_generated()

    add_executable(${APP_NAME} ${_secure_src})
    if(_secure_inc)
        target_include_directories(${APP_NAME} PRIVATE ${_secure_inc})
    endif()
    target_link_libraries(${APP_NAME} no-os)
    # secure_nsc.c is CubeMX's own NSC-gateway stub; not used (see file header
    # comment: projects define their own portable cmse_nonsecure_entry gateways).
    # Everything else CubeMX generated (main.c -> stm32_init(), HAL, BSP,
    # include dirs) comes from this call; see its doc comment.
    _no_os_stm32_tz_apply_mx_generated(${APP_NAME} ${SECURE_GEN_DIR})
    # no-os's own STM32 platform driver sources (e.g. stm32_delay.c -> stm32_hal.h
    # -> "main.h") need CubeMX's include dirs/defines too, same as the non-TZ flow.
    target_link_libraries(no-os PUBLIC stm32cubemx)
    target_compile_definitions(no-os PRIVATE -DSTM32_PLATFORM=1)

    file(GLOB _secure_ld ${SECURE_GEN_DIR}/*_FLASH_s.ld)
    if(NOT _secure_ld)
        message(FATAL_ERROR "stm32_trustzone: no *_FLASH_s.ld found under ${SECURE_GEN_DIR}")
    endif()
    # Emit the implib where CubeMX's OWN generated NonSecure/mx-generated.cmake
    # expects to find it (Secure_nsclib/secure_nsclib.o, relative to the project
    # dir) -- satisfies a Non-Secure world generated in THIS SAME tree without
    # any extra staging. A copy is also published as the conventional
    # <name>_implib.o deliverable below, for a standalone consumer elsewhere
    # (which stages it back via _no_os_stm32_tz_stage_secure_implib()).
    set(_implib_primary ${SECURE_GEN_DIR}/../Secure_nsclib/secure_nsclib.o)
    set(_implib_deliverable ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}_implib.o)
    target_link_options(${APP_NAME} PRIVATE
        -T${_secure_ld}
        -Wl,--cmse-implib -Wl,--out-implib=${_implib_primary})

    generate_openocd_config()
    if(NOT TZ_NO_FLASH)
        add_flash_target(${APP_NAME})
    endif()
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        BYPRODUCTS ${_implib_primary} ${_implib_deliverable}
        COMMAND ${CMAKE_COMMAND} -E copy ${_implib_primary} ${_implib_deliverable}
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${APP_NAME}> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}.hex
        COMMAND ${CMAKE_COMMAND} -E echo "Binary size:" && (${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${APP_NAME}> || ${CMAKE_COMMAND} -E true)
        COMMENT "Generating ${APP_NAME}.hex + ${APP_NAME}_implib.o (Secure-only deliverables)")

    set(_out ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}_contract.cmake)
    file(WRITE "${_out}"
        "# Auto-generated Secure deliverable contract for ${APP_NAME}.\n"
        "set(TZ_SECURE_HEX    \"\${CMAKE_CURRENT_LIST_DIR}/${APP_NAME}.hex\")\n"
        "set(TZ_SECURE_IMPLIB \"\${CMAKE_CURRENT_LIST_DIR}/${APP_NAME}_implib.o\")\n")
    message(STATUS "TrustZone Secure contract: ${_out}")
endfunction()

# =============================================================================
# no_os_add_stm32_trustzone_nonsecure_app() - Non-Secure-only CONSUMER.
# See the Maxim equivalent's doc comment; behaves the same way here.
#
# SECURE_IMPLIB is effectively REQUIRED on STM32 (unlike Maxim, where it is
# optional): CubeMX's own generated NonSecure/mx-generated.cmake unconditionally
# links a Secure implib at a fixed relative path (see
# _no_os_stm32_tz_stage_secure_implib()'s doc comment) regardless of whether
# this Non-Secure app calls any Secure gateway, so something must always be
# staged there.
# =============================================================================
function(no_os_add_stm32_trustzone_nonsecure_app APP_NAME)
    cmake_parse_arguments(TZ
        "" "SECURE_IMPLIB;SECURE_HEX" "NONSECURE_SRC;NONSECURE_INC" ${ARGN})

    if(NOT TZ_NONSECURE_SRC)
        message(FATAL_ERROR
            "no_os_add_stm32_trustzone_nonsecure_app(${APP_NAME}): NONSECURE_SRC is required.")
    endif()
    if(NOT (DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "NONSECURE"))
        message(FATAL_ERROR
            "${APP_NAME} is a Non-Secure-only TrustZone project: the build must be "
            "configured with MSECURITY_MODE=NONSECURE. This is set automatically by "
            "projects/${NO_OS_PROJECT_NAME}/trustzone.cmake; if that marker is "
            "missing, add -DMSECURITY_MODE=NONSECURE to the cmake configure line.")
    endif()

    _no_os_stm32_tz_abspaths(_ns_src ${TZ_NONSECURE_SRC})
    _no_os_stm32_tz_abspaths(_ns_inc ${TZ_NONSECURE_INC})

    _no_os_stm32_tz_ensure_generated()

    add_executable(${APP_NAME} ${_ns_src})
    if(_ns_inc)
        target_include_directories(${APP_NAME} PRIVATE ${_ns_inc})
    endif()
    target_link_libraries(${APP_NAME} no-os)
    # Everything CubeMX generated (main.c -> stm32_init(), HAL, BSP, include
    # dirs) comes from this call; see its doc comment. Its own generated link
    # line references a Secure implib at a fixed relative path -- staged below.
    _no_os_stm32_tz_apply_mx_generated(${APP_NAME} ${NONSECURE_GEN_DIR})
    # no-os's own STM32 platform driver sources (e.g. stm32_delay.c -> stm32_hal.h
    # -> "main.h") need CubeMX's include dirs/defines too, same as the non-TZ flow.
    target_link_libraries(no-os PUBLIC stm32cubemx)
    target_compile_definitions(no-os PRIVATE -DSTM32_PLATFORM=1)

    if(NOT TZ_SECURE_IMPLIB)
        message(FATAL_ERROR
            "no_os_add_stm32_trustzone_nonsecure_app(${APP_NAME}): SECURE_IMPLIB is "
            "required on STM32 (CubeMX's generated link always references a Secure "
            "implib; see this function's doc comment).")
    endif()
    no_os_require_path("${TZ_SECURE_IMPLIB}"
        "SECURE_IMPLIB not found: ${TZ_SECURE_IMPLIB}")
    _no_os_stm32_tz_stage_secure_implib(${APP_NAME} ${NONSECURE_GEN_DIR} ${TZ_SECURE_IMPLIB})

    file(GLOB _ns_ld ${NONSECURE_GEN_DIR}/*_FLASH_ns.ld)
    if(NOT _ns_ld)
        message(FATAL_ERROR "stm32_trustzone: no *_FLASH_ns.ld found under ${NONSECURE_GEN_DIR}")
    endif()
    target_link_options(${APP_NAME} PRIVATE -T${_ns_ld})

    generate_openocd_config()

    set(_ns_hex ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}_nonsecure.hex)
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${APP_NAME}> ${_ns_hex}
        COMMAND ${CMAKE_COMMAND} -E echo "Binary size:" && (${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${APP_NAME}> || ${CMAKE_COMMAND} -E true)
        COMMENT "Generating ${APP_NAME}_nonsecure.hex")

    if(TZ_SECURE_HEX)
        no_os_require_path("${TZ_SECURE_HEX}" "SECURE_HEX not found: ${TZ_SECURE_HEX}")
        set(_combined_hex ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}.hex)
        add_custom_command(TARGET ${APP_NAME} POST_BUILD
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/maxim/maxim_tz_merge_hex.py
                ${_combined_hex} ${TZ_SECURE_HEX} ${_ns_hex}
            COMMENT "Merging Secure + Non-Secure -> ${APP_NAME}.hex")
        _no_os_stm32_tz_add_hex_flash_target(${APP_NAME} ${_combined_hex})
    else()
        set(_ns_only_hex ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}.hex)
        add_custom_command(TARGET ${APP_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${_ns_hex} ${_ns_only_hex}
            COMMENT "Publishing ${APP_NAME}.hex (Non-Secure only)")
        _no_os_stm32_tz_add_hex_flash_target(${APP_NAME} ${_ns_only_hex})
    endif()
endfunction()

# =============================================================================
# no_os_add_stm32_trustzone_app() - combined single-project superbuild:
# builds Secure directly (this tree), drives a nested Non-Secure configure+
# build against the freshly-built implib, then merges the two hex outputs.
# No two-pass link, no embedded NS blob (see file header comment).
# =============================================================================
function(no_os_add_stm32_trustzone_app APP_NAME)
    cmake_parse_arguments(TZ
        ""
        "SECURE_CONF;NONSECURE_CONF"
        "SECURE_SRC;SECURE_INC;NONSECURE_SRC;NONSECURE_INC"
        ${ARGN})

    if(NOT TZ_SECURE_SRC OR NOT TZ_NONSECURE_SRC)
        message(FATAL_ERROR
            "no_os_add_stm32_trustzone_app(${APP_NAME}): SECURE_SRC and "
            "NONSECURE_SRC are required.")
    endif()
    if(NOT TZ_SECURE_CONF)
        set(TZ_SECURE_CONF "${NO_OS_PROJECT_NAME}/secure.conf")
    endif()
    if(NOT TZ_NONSECURE_CONF)
        set(TZ_NONSECURE_CONF "${NO_OS_PROJECT_NAME}/nonsecure.conf")
    endif()

    if(DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "NONSECURE")
        # Nested tree (see below): an ordinary Non-Secure no-OS CAPI app that
        # links the Secure import library so the gateway veneers resolve.
        no_os_add_stm32_trustzone_nonsecure_app(${APP_NAME}
            NONSECURE_SRC  ${TZ_NONSECURE_SRC}
            NONSECURE_INC  ${TZ_NONSECURE_INC}
            SECURE_IMPLIB  ${TZ_SECURE_IMPLIB})
        return()
    endif()

    if(NOT (DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "SECURE"))
        message(FATAL_ERROR
            "${APP_NAME} is a TrustZone project: the outer build must be configured "
            "with MSECURITY_MODE=SECURE. This is set automatically by "
            "projects/${NO_OS_PROJECT_NAME}/trustzone.cmake; if that marker is "
            "missing, add -DMSECURITY_MODE=SECURE to the cmake configure line.")
    endif()

    # ---- Outer (Secure) tree: build Secure directly as the producer --------
    # NO_FLASH: the merged HEX below (not this Secure-only ELF) is the real
    # deployment artifact; the flash target is created once, further down,
    # once the merge step is wired in.
    no_os_add_stm32_trustzone_secure_app(${APP_NAME}
        SECURE_SRC ${TZ_SECURE_SRC}
        SECURE_INC ${TZ_SECURE_INC}
        NO_FLASH)

    set(_secure_implib ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}_implib.o)
    set(_secure_hex    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}.hex)
    set(_ns_build_dir  ${CMAKE_BINARY_DIR}/nonsecure_build)
    set(_ns_hex        ${_ns_build_dir}/build/${APP_NAME}_nonsecure.hex)

    # ---- Nested Non-Secure build, wired directly onto ${APP_NAME} -----------
    # POST_BUILD on ${APP_NAME} itself (not a separate DEPENDS-tracked custom
    # target) so `cmake --build --target ${APP_NAME}` alone drives the whole
    # chain -- matching the Maxim flow, where the nested Non-Secure build is a
    # real dependency of the Pass-B link that literally produces ${APP_NAME}.
    # Runs after the secure_app POST_BUILD above (multiple POST_BUILD commands
    # on one target execute in the order they were added), so _secure_hex
    # already exists when the merge step runs. Output overwrites _secure_hex
    # (${APP_NAME}.hex, written by secure_app above) rather than a separate
    # "_combined.hex" -- like Maxim, there is exactly ONE <name>.hex deliverable,
    # not three differently-named hex files for one project. Safe to overwrite
    # in place: maxim_tz_merge_hex.py fully reads both inputs into memory before
    # it opens the output path for writing.
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        BYPRODUCTS ${_secure_hex}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_ns_build_dir}
        COMMAND ${CMAKE_COMMAND}
            -S ${CMAKE_SOURCE_DIR} -B ${_ns_build_dir} -G ${CMAKE_GENERATOR}
            -DPLATFORM=${PLATFORM}
            -DBOARD=${BOARD}
            -DTARGET=${TARGET}
            -DBOARD_CONFIG_FILE=${BOARD_CONFIG_FILE}
            -DUSE_VENDOR_TOOLCHAIN=${USE_VENDOR_TOOLCHAIN}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DMSECURITY_MODE=NONSECURE
            -DPROJECT_DEFCONFIG=${TZ_NONSECURE_CONF}
            -DTZ_SECURE_IMPLIB=${_secure_implib}
        COMMAND ${CMAKE_COMMAND} --build ${_ns_build_dir} --target ${APP_NAME}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/maxim/maxim_tz_merge_hex.py
            ${_secure_hex} ${_secure_hex} ${_ns_hex}
        USES_TERMINAL
        COMMENT "Building nested Non-Secure image and merging -> ${APP_NAME}.hex")
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${_ns_build_dir})

    # Real flash target, created only now that ${_secure_hex} is known to end
    # up holding the merged Secure+Non-Secure image (see the POST_BUILD above).
    _no_os_stm32_tz_add_hex_flash_target(${APP_NAME} ${_secure_hex})
endfunction()
