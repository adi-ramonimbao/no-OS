# =============================================================================
# no_os_add_maxim_trustzone_app() - reusable Maxim (Armv8-M / CMSE) TrustZone
# superbuild.
#
# Produces a SINGLE combined ELF holding both the Secure and Non-Secure worlds,
# mirroring the MSDK Hello_World_TZ make flow in CMake. A project supplies only
# its sources/includes; this module drives the whole two-tree superbuild:
#
#   1. Compile the Secure objects (-mcmse, MSECURITY_MODE=SECURE).
#   2. Pass-A link -> secure_implib.o        (--cmse-implib, no NS image)
#   3. Nested NON-SECURE build (links secure_implib.o) -> nonsecure.bin
#   4. Assemble nonsecure_load.S (.incbin nonsecure.bin) -> nonsecure.o
#   5. Pass-B link: Secure objects + nonsecure.o -> combined ELF
#
# Two build trees are required because the Maxim toolchain fixes the CPU flags,
# -mcmse, and the -T<linker script> globally per build tree (forced cache vars
# in drivers/platform/maxim/toolchain.cmake). Secure is built with -mcmse and
# links <target>_s.ld; Non-Secure is built without -mcmse and links
# <target>_ns.ld. So the SECURE tree (the outer build)
# drives a nested NON-SECURE tree and relinks the result into itself. The same
# call also handles the nested NON-SECURE tree (entered when the root is
# reconfigured with MSECURITY_MODE=NONSECURE by step 3), so a project calls this
# exactly once and this module dispatches on MSECURITY_MODE.
#
# The module is part-agnostic: it hardcodes no specific device. A Maxim part is
# usable with it when its MSDK TrustZone linker scripts follow the standard
# convention -- <target>_s.ld places section .nonsecure_flash and pins
# KEEP(*nonsecure.o) there. MAX32657 is the only Maxim part wired for TrustZone
# today and is the reference example used throughout these comments.
#
# The outer build must be configured with MSECURITY_MODE=SECURE. A project
# declares this without a preset via a projects/<name>/trustzone.cmake marker
# (picked up before the toolchain loads); -DMSECURITY_MODE=SECURE also works.
#
# Usage (projects/<name>/CMakeLists.txt):
#   include(maxim_trustzone)
#   no_os_add_maxim_trustzone_app(<name>
#       SECURE_SRC     src/secure/main.c
#       SECURE_INC     src/secure            # dir holding partition_<target>.h
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

# Resolve the ACTIVE Secure linker script: the generated one on the custom-memory
# path (NO_OS_TZ_GEN_DIR set), otherwise the SDK default. Single source of truth
# for the partition SAU table (Gate 1) and the Secure contract (§ producer).
function(_no_os_tz_active_sld OUT_VAR)
    if(DEFINED NO_OS_TZ_GEN_DIR)
        set(_sld "${NO_OS_TZ_GEN_DIR}/secure/${TARGET}_s.ld")
    else()
        set(_sld "${MAXIM_LIBRARIES}/CMSIS/Device/Maxim/MAX${TARGET_NUM}/Source/GCC/${TARGET}_s.ld")
    endif()
    set(${OUT_VAR} "${_sld}" PARENT_SCOPE)
endfunction()

# Parse ORIGIN/LENGTH of a MEMORY region from a linker script. The region name
# must be followed by "(" so FLASH does not also match FLASH_NS.
function(_no_os_tz_ld_region ld name out_origin out_len)
    file(STRINGS "${ld}" _lines REGEX "^[ \t]*${name}[ \t]*\\(")
    if(NOT _lines OR NOT "${_lines}" MATCHES
       "ORIGIN[ \t]*=[ \t]*(0x[0-9A-Fa-f]+).*LENGTH[ \t]*=[ \t]*(0x[0-9A-Fa-f]+)")
        message(FATAL_ERROR "maxim_trustzone: region '${name}' not found in ${ld}")
    endif()
    set(${out_origin} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    set(${out_len} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

# Gate 1: (re)generate the Secure partition_<target>.h to the no-OS SAU policy,
# and expose the Secure include dirs to no-os (system_<target>.c, compiled into
# no-os under -mcmse, #includes the partition). Shared by the combined superbuild
# and the Secure-only producer so both apply the identical SAU policy. The SAU
# region addresses come from the ACTIVE secure linker script (see above); the
# generator writes only when the content changes (no needless recompile).
function(_no_os_tz_prepare_secure_partition)
    cmake_parse_arguments(P "" "" "SECURE_INC" ${ARGN})
    if(NOT P_SECURE_INC)
        message(FATAL_ERROR
            "maxim_trustzone: SECURE_INC (the dir holding partition_${TARGET}.h) "
            "is required for a Secure TrustZone build.")
    endif()
    target_include_directories(no-os PRIVATE ${P_SECURE_INC})

    set(_tz_template "${MAXIM_LIBRARIES}/CMSIS/Device/Maxim/MAX${TARGET_NUM}/Source/Template/partition_${TARGET}.h")
    _no_os_tz_active_sld(_tz_active_sld)
    list(GET P_SECURE_INC 0 _tz_secure_dir)
    no_os_require_path("${_tz_template}"
        "TrustZone partition template not found: ${_tz_template}")
    no_os_require_path("${_tz_active_sld}"
        "TrustZone secure linker script not found: ${_tz_active_sld}")
    no_os_run_checked(
        WHAT "generating ${TARGET} TrustZone partition (${_tz_secure_dir}/partition_${TARGET}.h)"
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/maxim_tz_gen_partition.py
            --template ${_tz_template}
            --sld ${_tz_active_sld}
            --out ${_tz_secure_dir}/partition_${TARGET}.h)
endfunction()

# Write the Secure deliverable "contract" next to the Secure HEX: the paths a
# Non-Secure consumer feeds to no_os_add_maxim_trustzone_nonsecure_app()
# (SECURE_HEX / SECURE_IMPLIB) plus the Non-Secure memory map (flash/SRAM/NSC)
# the consumer must match. Parsed from the active _s.ld so it can never drift.
function(_no_os_tz_write_contract APP_NAME)
    _no_os_tz_active_sld(_sld)
    _no_os_tz_ld_region("${_sld}" FLASH_NS   _ns_flash_base _ns_flash_size)
    _no_os_tz_ld_region("${_sld}" SRAM_NS    _ns_sram_base  _ns_sram_size)
    _no_os_tz_ld_region("${_sld}" NSC_REGION _nsc_base      _nsc_size)
    set(_out ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}_contract.cmake)
    file(WRITE "${_out}"
        "# Auto-generated Secure deliverable contract for ${APP_NAME}.\n"
        "# Pass these to no_os_add_maxim_trustzone_nonsecure_app() and keep the\n"
        "# Non-Secure image inside the memory map below. Paths are relative to this\n"
        "# file (CMAKE_CURRENT_LIST_DIR), so the deliverable folder is relocatable:\n"
        "# keep the hex/implib/tz_gen next to this contract and it works from anywhere.\n"
        "set(TZ_SECURE_HEX    \"\${CMAKE_CURRENT_LIST_DIR}/${APP_NAME}.hex\")\n"
        "set(TZ_SECURE_IMPLIB \"\${CMAKE_CURRENT_LIST_DIR}/${APP_NAME}_implib.o\")\n"
        "set(TZ_NS_FLASH_ORIGIN ${_ns_flash_base})\n"
        "set(TZ_NS_FLASH_SIZE   ${_ns_flash_size})\n"
        "set(TZ_NS_SRAM_ORIGIN  ${_ns_sram_base})\n"
        "set(TZ_NS_SRAM_SIZE    ${_ns_sram_size})\n"
        "set(TZ_NSC_ORIGIN      ${_nsc_base})\n"
        "set(TZ_NSC_SIZE        ${_nsc_size})\n")
    # Custom split: the Secure linker-script pair IS the memory-map contract. Ship
    # it as a first-class deliverable next to the hex/implib and point the consumer
    # at it; the consumer links these exact scripts -- there is no regeneration
    # downstream. A default split ships nothing (the SDK's stock _ns.ld is used).
    if(DEFINED NO_OS_TZ_GEN_DIR)
        set(_ship "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tz_gen")
        file(COPY "${NO_OS_TZ_GEN_DIR}/secure/${TARGET}_s.ld"
            DESTINATION "${_ship}/secure")
        file(COPY "${NO_OS_TZ_GEN_DIR}/nonsecure/${TARGET}_ns.ld"
            DESTINATION "${_ship}/nonsecure")
        file(APPEND "${_out}"
            "# Custom split: the shipped linker-script pair below IS the memory-map\n"
            "# contract; the consumer links these exact scripts (no regeneration).\n"
            "set(NO_OS_TZ_GEN_DIR \"\${CMAKE_CURRENT_LIST_DIR}/tz_gen\" CACHE INTERNAL \"Shipped TrustZone linker scripts\")\n")
    endif()
    message(STATUS "TrustZone Secure contract: ${_out}")
endfunction()

# Create a `flash` target that programs a combined HEX (Secure + Non-Secure) via
# whichever probe is configured. The generic add_flash_target() flashes the ELF
# on OpenOCD, which for a Non-Secure-only build would carry only the Non-Secure
# world; here the merged HEX is the deployment artifact, so program that instead.
function(_no_os_tz_add_hex_flash_target APP_NAME HEX)
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

    # The platform's system_<target>.c (compiled into no-os) includes
    # partition_<target>.h under -mcmse (__ARM_FEATURE_CMSE==3): the Secure app's
    # SAU config. Gate 1 regenerates it to the no-OS SAU policy from the active
    # secure linker script and exposes the Secure include dirs to no-os. Shared
    # with the Secure-only producer so both apply the identical policy.
    _no_os_tz_prepare_secure_partition(SECURE_INC ${_secure_inc})

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
    #
    # Custom TrustZone memory: the outer (Secure) tree generated the linker scripts
    # in NO_OS_TZ_GEN_DIR; hand that dir (and the flag) to the nested tree so it links
    # the generated <target>_ns.ld. Passed only when actually set, so an empty
    # -DNO_OS_TZ_GEN_DIR can never make the nested toolchain treat it as defined.
    set(_tz_ns_custom_args "")
    if(USE_CUSTOM_MEMORY_SETTINGS AND DEFINED NO_OS_TZ_GEN_DIR)
        list(APPEND _tz_ns_custom_args
            -DUSE_CUSTOM_MEMORY_SETTINGS=${USE_CUSTOM_MEMORY_SETTINGS}
            -DNO_OS_TZ_GEN_DIR=${NO_OS_TZ_GEN_DIR})
    endif()
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
            ${_tz_ns_custom_args}
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
    # alias region (.nonsecure_flash, MAX32657 @ 0x01080000) and the Secure alias
    # region (.text, @ 0x11000000) -> a multi-hundred-MB .bin. The Intel HEX
    # carries per-region address records, so it stays small and is flashable.
    config_platform_sdk(${APP_NAME})
    generate_openocd_config()
    add_flash_target(${APP_NAME})
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${APP_NAME}> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}.hex
        COMMAND ${CMAKE_COMMAND} -E echo "Binary size:" && (${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${APP_NAME}> || ${CMAKE_COMMAND} -E true)
        COMMENT "Generating ${APP_NAME}.hex (combined Secure+Non-Secure)")
endfunction()

# =============================================================================
# no_os_add_maxim_trustzone_secure_app() - Secure-only PRODUCER.
#
# Builds just the Secure world and emits it as separately deliverable artifacts,
# for the case where the Secure firmware is owned/signed/provisioned by one party
# and the Non-Secure application is built and iterated independently by another
# (see no_os_add_maxim_trustzone_nonsecure_app, the consumer).
#
# With no Non-Secure image to embed there is no Secure<->Non-Secure circular
# dependency, so the two-pass implib dance of the combined superbuild collapses
# to a single link: one Secure executable linked with --cmse-implib, which yields
# both the Secure image and the import library in one step.
#
# Deliverables (in CMAKE_RUNTIME_OUTPUT_DIRECTORY):
#   <name>.hex           - the Secure image (Intel HEX; .nonsecure_flash is empty)
#   <name>_implib.o      - CMSE import library (the __ns_entry gateway addresses)
#   <name>_contract.cmake- SECURE_HEX/SECURE_IMPLIB paths + the Non-Secure memory
#                          map the consumer must match
#
# The outer build must be configured with MSECURITY_MODE=SECURE (a project ships
# projects/<name>/trustzone.cmake to set it; -DMSECURITY_MODE=SECURE also works).
#
# Usage (projects/<name>/CMakeLists.txt):
#   include(maxim_trustzone)
#   no_os_add_maxim_trustzone_secure_app(<name>
#       SECURE_SRC  src/secure/main.c
#       SECURE_INC  src/secure)          # dir holding partition_<target>.h
# =============================================================================
function(no_os_add_maxim_trustzone_secure_app APP_NAME)
    cmake_parse_arguments(TZ
        ""
        "SECURE_CONF"
        "SECURE_SRC;SECURE_INC"
        ${ARGN})

    if(NOT TZ_SECURE_SRC)
        message(FATAL_ERROR
            "no_os_add_maxim_trustzone_secure_app(${APP_NAME}): SECURE_SRC is required.")
    endif()
    if(NOT (DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "SECURE"))
        message(FATAL_ERROR
            "${APP_NAME} is a Secure-only TrustZone project: the build must be configured "
            "with MSECURITY_MODE=SECURE. This is set automatically by "
            "projects/${NO_OS_PROJECT_NAME}/trustzone.cmake; if that marker is missing, "
            "add -DMSECURITY_MODE=SECURE to the cmake configure line.")
    endif()

    _no_os_tz_abspaths(_secure_src ${TZ_SECURE_SRC})
    _no_os_tz_abspaths(_secure_inc ${TZ_SECURE_INC})

    # Gate 1: regenerate partition_<target>.h + expose the Secure includes to no-os.
    _no_os_tz_prepare_secure_partition(SECURE_INC ${_secure_inc})

    set(_implib ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}_implib.o)

    add_executable(${APP_NAME} ${_secure_src})
    if(_secure_inc)
        target_include_directories(${APP_NAME} PRIVATE ${_secure_inc})
    endif()
    target_link_libraries(${APP_NAME} no-os)
    # Emit the CMSE import library as a first-class deliverable so a separately
    # built Non-Secure image can resolve the __ns_entry gateway veneers.
    target_link_options(${APP_NAME} PRIVATE
        -Wl,--cmse-implib -Wl,--out-implib=${_implib})

    config_platform_sdk(${APP_NAME})
    generate_openocd_config()
    add_flash_target(${APP_NAME})

    # Deliverables: Secure Intel HEX (no `objcopy -O binary`; the empty
    # .nonsecure_flash at the Non-Secure alias would balloon a raw binary) and
    # the implib (a link byproduct, declared so Ninja tracks it).
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        BYPRODUCTS ${_implib}
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${APP_NAME}> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}.hex
        COMMAND ${CMAKE_COMMAND} -E echo "Binary size:" && (${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${APP_NAME}> || ${CMAKE_COMMAND} -E true)
        COMMENT "Generating ${APP_NAME}.hex + ${APP_NAME}_implib.o (Secure-only deliverables)")

    _no_os_tz_write_contract(${APP_NAME})
endfunction()

# =============================================================================
# no_os_add_maxim_trustzone_nonsecure_app() - Non-Secure-only CONSUMER.
#
# Builds just the Non-Secure world against a Secure image produced elsewhere
# (by no_os_add_maxim_trustzone_secure_app, or any matching Secure deliverable),
# then merges the two HEX files into one flashable image. The Secure world is
# NOT rebuilt; only the memory-map + gateway contract is consumed.
#
# The Non-Secure link resolves the __ns_entry gateway veneers against the Secure
# import library (SECURE_IMPLIB). A Non-Secure app that calls no Secure gateway
# needs no implib -- omit it. SECURE_HEX is the Secure image to combine with;
# omit it to produce only the standalone Non-Secure HEX.
#
# The outer build runs entirely in the Non-Secure world (MSECURITY_MODE=NONSECURE,
# set by projects/<name>/trustzone.cmake). The Non-Secure memory map comes from
# the selected <target>_ns.ld (the default split, or a generated one via
# NO_OS_TZ_GEN_DIR); it must match the Secure image's map (see its contract).
#
# Usage (projects/<name>/CMakeLists.txt):
#   include(maxim_trustzone)
#   no_os_add_maxim_trustzone_nonsecure_app(<name>
#       NONSECURE_SRC  src/nonsecure/main.c
#       NONSECURE_INC  src/nonsecure
#       [SECURE_IMPLIB <path/to/secure_implib.o>]
#       [SECURE_HEX    <path/to/secure.hex>])
# =============================================================================
function(no_os_add_maxim_trustzone_nonsecure_app APP_NAME)
    cmake_parse_arguments(TZ
        ""
        "SECURE_IMPLIB;SECURE_HEX"
        "NONSECURE_SRC;NONSECURE_INC"
        ${ARGN})

    if(NOT TZ_NONSECURE_SRC)
        message(FATAL_ERROR
            "no_os_add_maxim_trustzone_nonsecure_app(${APP_NAME}): NONSECURE_SRC is required.")
    endif()
    if(NOT (DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "NONSECURE"))
        message(FATAL_ERROR
            "${APP_NAME} is a Non-Secure-only TrustZone project: the build must be "
            "configured with MSECURITY_MODE=NONSECURE. This is set automatically by "
            "projects/${NO_OS_PROJECT_NAME}/trustzone.cmake; if that marker is missing, "
            "add -DMSECURITY_MODE=NONSECURE to the cmake configure line.")
    endif()

    _no_os_tz_abspaths(_ns_src ${TZ_NONSECURE_SRC})
    _no_os_tz_abspaths(_ns_inc ${TZ_NONSECURE_INC})

    add_executable(${APP_NAME} ${_ns_src})
    if(_ns_inc)
        target_include_directories(${APP_NAME} PRIVATE ${_ns_inc})
    endif()
    target_link_libraries(${APP_NAME} no-os)

    # Link the provided Secure import library so the __ns_entry gateway veneers
    # resolve. Optional: a pure Non-Secure app that calls no Secure gateway needs
    # nothing from the Secure side at link time.
    if(TZ_SECURE_IMPLIB)
        no_os_require_path("${TZ_SECURE_IMPLIB}"
            "SECURE_IMPLIB not found: ${TZ_SECURE_IMPLIB}")
        target_link_libraries(${APP_NAME} ${TZ_SECURE_IMPLIB})
    endif()

    config_platform_sdk(${APP_NAME})
    generate_openocd_config()

    # The Non-Secure image on its own (Intel HEX at its Non-Secure alias origin).
    set(_ns_hex ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}_nonsecure.hex)
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${APP_NAME}> ${_ns_hex}
        COMMAND ${CMAKE_COMMAND} -E echo "Binary size:" && (${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${APP_NAME}> || ${CMAKE_COMMAND} -E true)
        COMMENT "Generating ${APP_NAME}_nonsecure.hex")

    if(TZ_SECURE_HEX)
        # Combine the provided Secure image with this Non-Secure image into a
        # single flashable HEX. The two occupy disjoint flash regions, so the
        # merge is a record-stream concatenation (see maxim_tz_merge_hex.py).
        no_os_require_path("${TZ_SECURE_HEX}"
            "SECURE_HEX not found: ${TZ_SECURE_HEX}")
        set(_combined_hex ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}.hex)
        add_custom_command(TARGET ${APP_NAME} POST_BUILD
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/maxim_tz_merge_hex.py
                ${_combined_hex} ${TZ_SECURE_HEX} ${_ns_hex}
            COMMENT "Merging Secure + Non-Secure -> ${APP_NAME}.hex")
        _no_os_tz_add_hex_flash_target(${APP_NAME} ${_combined_hex})
    else()
        # No Secure image supplied: the Non-Secure image is the deployment
        # artifact. Publish it under the conventional <name>.hex too and flash
        # that HEX (program writes only the Non-Secure sectors, so a Secure image
        # already on the part is left untouched -- an NS-only reflash).
        set(_ns_only_hex ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${APP_NAME}.hex)
        add_custom_command(TARGET ${APP_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${_ns_hex} ${_ns_only_hex}
            COMMENT "Publishing ${APP_NAME}.hex (Non-Secure only)")
        _no_os_tz_add_hex_flash_target(${APP_NAME} ${_ns_only_hex})
    endif()
endfunction()
