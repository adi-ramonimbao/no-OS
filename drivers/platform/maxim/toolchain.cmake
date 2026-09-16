# CFS path can be set via -DCFS_PATH=..., the CFS_PATH environment variable, or auto-detected
find_path(CFS
          NAMES
            cfs.json
          PATHS
            "${CFS_PATH}" "$ENV{CFS_PATH}" "$ENV{HOME}/cfs"
)

if (CFS)
    set(MAXIM_LIBRARIES ${CFS}/SDK/MAX/Libraries)
elseif(DEFINED ENV{MAXIM_LIBRARIES})
    set(MAXIM_LIBRARIES $ENV{MAXIM_LIBRARIES})
else()
    message(FATAL_ERROR
        "MAXIM_LIBRARIES is not set. Point it at the Maxim SDK Libraries directory "
        "(set the MAXIM_LIBRARIES environment variable), or provide a CFS install "
        "via -DCFS_PATH=... / the CFS_PATH environment variable.")
endif()

# MAXIM_LIBRARIES is dereferenced into the linker script path (-T...) below; a
# wrong path would otherwise fail much later at link time with a missing .ld.
if(NOT EXISTS "${MAXIM_LIBRARIES}")
    message(FATAL_ERROR "MAXIM_LIBRARIES directory does not exist: ${MAXIM_LIBRARIES}")
endif()

message(STATUS "MAXIM_LIBRARIES: ${MAXIM_LIBRARIES}")

# TrustZone custom memory settings (MAX32657 only). When USE_CUSTOM_MEMORY_SETTINGS
# is enabled, generate the Secure/Non-Secure linker scripts from the project's split
# via the MSDK setup_memory_tz.py so a non-default memory map stays consistent across
# the linker scripts, the SAU partition (Gate 1, in maxim_trustzone.cmake) and the
# __MXC_* defines (memory_layout.cmake, below). Generation runs only in the SECURE
# (producer/outer) tree; a NONSECURE consumer never regenerates -- it links the
# linker scripts the producer shipped (via NO_OS_TZ_GEN_DIR from its contract),
# which are the authoritative memory-map contract. Skipped during try-compile.
get_property(_no_os_in_try_compile GLOBAL PROPERTY IN_TRY_COMPILE)
if(TARGET_NUM STREQUAL "32657" AND USE_CUSTOM_MEMORY_SETTINGS
   AND MSECURITY_MODE STREQUAL "SECURE" AND NOT _no_os_in_try_compile)
    # Physical memory (MAX32657): 1 MiB flash, 256 KiB SRAM.
    set(_tz_phys_flash 0x00100000)
    set(_tz_phys_sram  0x00040000)

    # Resolve size pairs: default to today's split; if only one side of a pair is
    # given, auto-derive the complement (S + NS must equal the physical total).
    if(S_FLASH_SIZE AND NS_FLASH_SIZE)
        set(_tz_s_flash "${S_FLASH_SIZE}")
        set(_tz_ns_flash "${NS_FLASH_SIZE}")
    elseif(S_FLASH_SIZE)
        set(_tz_s_flash "${S_FLASH_SIZE}")
        math(EXPR _tz_ns_flash "${_tz_phys_flash} - ${S_FLASH_SIZE}" OUTPUT_FORMAT HEXADECIMAL)
    elseif(NS_FLASH_SIZE)
        set(_tz_ns_flash "${NS_FLASH_SIZE}")
        math(EXPR _tz_s_flash "${_tz_phys_flash} - ${NS_FLASH_SIZE}" OUTPUT_FORMAT HEXADECIMAL)
    else()
        set(_tz_s_flash 0x00080000)
        set(_tz_ns_flash 0x00080000)
    endif()
    if(S_SRAM_SIZE AND NS_SRAM_SIZE)
        set(_tz_s_sram "${S_SRAM_SIZE}")
        set(_tz_ns_sram "${NS_SRAM_SIZE}")
    elseif(S_SRAM_SIZE)
        set(_tz_s_sram "${S_SRAM_SIZE}")
        math(EXPR _tz_ns_sram "${_tz_phys_sram} - ${S_SRAM_SIZE}" OUTPUT_FORMAT HEXADECIMAL)
    elseif(NS_SRAM_SIZE)
        set(_tz_ns_sram "${NS_SRAM_SIZE}")
        math(EXPR _tz_s_sram "${_tz_phys_sram} - ${NS_SRAM_SIZE}" OUTPUT_FORMAT HEXADECIMAL)
    else()
        set(_tz_s_sram 0x00020000)
        set(_tz_ns_sram 0x00020000)
    endif()

    # NSC default: match today's split (the SDK's pre-baked _s.ld reserves 32 KiB
    # for the NSC region), NOT setup_memory_tz.py's own 8 KiB default -- otherwise
    # an omitted NSC_SIZE would silently shrink the NSC region vs the default path.
    set(_tz_nsc "${NSC_SIZE}")
    if(NOT _tz_nsc)
        set(_tz_nsc 0x00008000)
    endif()
    # Starts are all-or-nothing (the script errors on a partial set), so a project
    # either sets all four or none; 0 lets the script auto-place them.
    foreach(_v S_FLASH_START NS_FLASH_START S_SRAM_START NS_SRAM_START)
        if(${_v})
            set(_tz_${_v} "${${_v}}")
        else()
            set(_tz_${_v} 0)
        endif()
    endforeach()
    set(_tz_exec "${EXECUTE_CODE_MEM}")
    if(NOT _tz_exec)
        set(_tz_exec FLASH)
    endif()

    find_program(_tz_python NAMES python3 python REQUIRED)
    get_filename_component(_tz_msdk_root "${MAXIM_LIBRARIES}" DIRECTORY)
    set(_tz_script "${MAXIM_LIBRARIES}/CMSIS/Device/Maxim/MAX${TARGET_NUM}/Source/GCC/setup_memory_tz.py")
    set(NO_OS_TZ_GEN_DIR "${CMAKE_BINARY_DIR}/tz_gen" CACHE INTERNAL "Generated TrustZone linker-script dir")
    file(MAKE_DIRECTORY "${NO_OS_TZ_GEN_DIR}/secure" "${NO_OS_TZ_GEN_DIR}/nonsecure")
    execute_process(
        COMMAND ${_tz_python} ${_tz_script}
            ${_tz_msdk_root} ${NO_OS_TZ_GEN_DIR}/secure ${NO_OS_TZ_GEN_DIR}/nonsecure
            ${_tz_s_flash} ${_tz_ns_flash} ${_tz_s_sram} ${_tz_ns_sram} ${_tz_nsc}
            ${_tz_S_FLASH_START} ${_tz_NS_FLASH_START} ${_tz_S_SRAM_START} ${_tz_NS_SRAM_START}
            ${_tz_exec}
        RESULT_VARIABLE _tz_res OUTPUT_VARIABLE _tz_out ERROR_VARIABLE _tz_err)
    if(NOT _tz_res EQUAL 0 OR _tz_out MATCHES "ERROR")
        message(FATAL_ERROR
            "setup_memory_tz.py failed (custom TrustZone memory):\n${_tz_out}\n${_tz_err}")
    endif()
    message(STATUS "TrustZone custom memory: generated linker scripts in ${NO_OS_TZ_GEN_DIR}")
endif()

# The linker-script pair is the authoritative memory-map contract for a custom
# split: whether generated here (SECURE) or shipped by the producer and pointed to
# via NO_OS_TZ_GEN_DIR (NONSECURE consumer), it must be present. There is no
# regeneration fallback -- fail loudly if a script is missing.
if(DEFINED NO_OS_TZ_GEN_DIR)
    foreach(_tz_req "${NO_OS_TZ_GEN_DIR}/secure/${TARGET}_s.ld"
                    "${NO_OS_TZ_GEN_DIR}/nonsecure/${TARGET}_ns.ld")
        if(NOT EXISTS "${_tz_req}")
            message(FATAL_ERROR
                "TrustZone custom-memory linker script not found:\n  ${_tz_req}\n"
                "The Secure producer must ship the tz_gen/{secure,nonsecure} pair "
                "(recorded in its contract as NO_OS_TZ_GEN_DIR). No regeneration is "
                "performed on the consumer side.")
        endif()
    endforeach()
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# For bare-metal toolchains, only compile (not link) during CMake's compiler test.
# This avoids linker failures caused by missing startup files, linker scripts,
# and syscall stubs (e.g. _sbrk needing 'end') during the detection phase.
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/max${TARGET_NUM}/memory_layout.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/max${TARGET_NUM}/memory_layout.cmake")
endif()

if(USE_VENDOR_TOOLCHAIN)
    if(CFS)
        cmake_path(SET CROSS_COMPILER_BIN NORMALIZE "${CFS}/Tools/gcc/arm-none-eabi/bin")
    else()
        # Stock MaximSDK installs the bundled GCC under a versioned directory
        # (Tools/GNUTools/<version>/bin). Other frameworks may place it directly in
        # Tools/GNUTools/bin. Glob both layouts.
        file(GLOB CROSS_COMPILER_BIN
             "${MAXIM_LIBRARIES}/../Tools/GNUTools/*/bin"
             "${MAXIM_LIBRARIES}/../Tools/GNUTools/bin")
    endif()

    find_program(CMAKE_C_COMPILER arm-none-eabi-gcc HINTS ${CROSS_COMPILER_BIN})
    find_program(CMAKE_CXX_COMPILER arm-none-eabi-g++ HINTS ${CROSS_COMPILER_BIN})
    find_program(CMAKE_ASM_COMPILER arm-none-eabi-gcc HINTS ${CROSS_COMPILER_BIN})
    find_program(CMAKE_LINKER arm-none-eabi-ld HINTS ${CROSS_COMPILER_BIN})
    find_program(CMAKE_SIZE arm-none-eabi-size HINTS ${CROSS_COMPILER_BIN})
    find_program(CMAKE_OBJCOPY arm-none-eabi-objcopy HINTS ${CROSS_COMPILER_BIN})

    # Report a missing cross-compiler here, with the directory we searched,
    # rather than letting CMake fail later with a generic compiler-detection error.
    if(NOT CMAKE_C_COMPILER)
        message(FATAL_ERROR
            "arm-none-eabi-gcc not found under CROSS_COMPILER_BIN=${CROSS_COMPILER_BIN}. "
            "Check the CFS/MAXIM_LIBRARIES location, or install the ARM GCC toolchain. "
            "To use a compiler already on PATH instead, configure with -DUSE_VENDOR_TOOLCHAIN=OFF.")
    endif()
else()
    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
    set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
    set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
    set(CMAKE_LINKER arm-none-eabi-ld)
    set(CMAKE_SIZE arm-none-eabi-size)
    set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
endif()

set(CMAKE_EXECUTABLE_SUFFIX_ASM ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

# CPU/FPU flags are target-aware. The MAX32657 is a Cortex-M33 (Armv8-M with
# the FPv5 single-precision FPU); every other supported Maxim part in this file
# is a Cortex-M4. Selecting the wrong core is not merely a missed optimisation:
# TrustZone/CMSE (-mcmse, below) requires Armv8-M, so the M33 flags are a hard
# prerequisite for the secure/non-secure split. -mno-unaligned-access matches
# the Maxim SDK's own MAX32657 build (Armv8-M without the Main Extension traps
# unaligned access; the SDK plays it safe and the startup code sets UNALIGN_TRP).
if(TARGET_NUM STREQUAL "32657")
    set(COMMON_CPU_FLAGS "-mthumb -mcpu=cortex-m33 -mfloat-abi=softfp -mfpu=fpv5-sp-d16 -mno-unaligned-access")
else()
    set(COMMON_CPU_FLAGS "-mthumb -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16")
endif()

# TrustZone security context for this build tree. MSECURITY_MODE is unset for an
# ordinary single-image build (today's behaviour, unchanged), SECURE for the
# secure half of a TrustZone build, and NONSECURE for the non-secure half. Only
# the secure side compiles with -mcmse (which sets __ARM_FEATURE_CMSE==3 and
# activates the SAU/veneer code paths); the non-secure side must NOT get it.
# Each side also links against a different linker script (see the linker flags
# below): <target>_s.ld / <target>_ns.ld, versus the plain <target>.ld default.
set(TZ_CMSE_FLAGS "")
set(LINKER_SCRIPT_SUFFIX "")
if(DEFINED MSECURITY_MODE)
    if(MSECURITY_MODE STREQUAL "SECURE")
        set(TZ_CMSE_FLAGS "-mcmse")
        set(LINKER_SCRIPT_SUFFIX "_s")
    elseif(MSECURITY_MODE STREQUAL "NONSECURE")
        set(LINKER_SCRIPT_SUFFIX "_ns")
    else()
        message(FATAL_ERROR
            "MSECURITY_MODE='${MSECURITY_MODE}' is invalid. "
            "Use SECURE, NONSECURE, or leave it unset for a single-image build.")
    endif()
endif()

# Common flags for all build types
set(CMAKE_C_FLAGS "${COMMON_CPU_FLAGS} ${TZ_CMSE_FLAGS} -ffunction-sections -fdata-sections -MD" CACHE STRING "C compiler flags" FORCE)
set(CMAKE_CXX_FLAGS "${COMMON_CPU_FLAGS} ${TZ_CMSE_FLAGS} -ffunction-sections -fdata-sections -MD" CACHE STRING "C++ compiler flags" FORCE)
set(CMAKE_ASM_FLAGS "${COMMON_CPU_FLAGS} -x assembler-with-cpp" CACHE STRING "ASM compiler flags" FORCE)

# Debug build flags - Full debug info, no optimization
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -DDEBUG" CACHE STRING "C compiler flags for Debug" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -DDEBUG" CACHE STRING "C++ compiler flags for Debug" FORCE)
set(CMAKE_ASM_FLAGS_DEBUG "-g3" CACHE STRING "ASM compiler flags for Debug" FORCE)

# Release build flags - Optimize for speed, disable assertions
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C compiler flags for Release" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C++ compiler flags for Release" FORCE)
set(CMAKE_ASM_FLAGS_RELEASE "" CACHE STRING "ASM compiler flags for Release" FORCE)

# RelWithDebInfo build flags - Optimize with debug info
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG" CACHE STRING "C compiler flags for RelWithDebInfo" FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG" CACHE STRING "C++ compiler flags for RelWithDebInfo" FORCE)
set(CMAKE_ASM_FLAGS_RELWITHDEBINFO "-g" CACHE STRING "ASM compiler flags for RelWithDebInfo" FORCE)

# MinSizeRel build flags - Optimize for size
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG" CACHE STRING "C compiler flags for MinSizeRel" FORCE)
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG" CACHE STRING "C++ compiler flags for MinSizeRel" FORCE)
set(CMAKE_ASM_FLAGS_MINSIZEREL "" CACHE STRING "ASM compiler flags for MinSizeRel" FORCE)

# Linker script path. Default: the SDK's pre-baked GCC script (<target>.ld,
# <target>_s.ld, or <target>_ns.ld via LINKER_SCRIPT_SUFFIX). Custom TrustZone
# memory (NO_OS_TZ_GEN_DIR set): the linker-script pair generated here (SECURE) or
# shipped by the producer and consumed via the contract (NONSECURE); verified to
# exist above.
if(DEFINED NO_OS_TZ_GEN_DIR)
    if(MSECURITY_MODE STREQUAL "NONSECURE")
        set(_tz_linker_script "${NO_OS_TZ_GEN_DIR}/nonsecure/${TARGET}_ns.ld")
    else()
        set(_tz_linker_script "${NO_OS_TZ_GEN_DIR}/secure/${TARGET}_s.ld")
    endif()
else()
    set(_tz_linker_script "${MAXIM_LIBRARIES}/CMSIS/Device/Maxim/MAX${TARGET_NUM}/Source/GCC/${TARGET}${LINKER_SCRIPT_SUFFIX}.ld")
endif()

# Linker flags (common for all build types). -mcmse is carried onto the secure link
# too (empty otherwise) so gcc selects the CMSE-aware spec.
set(CMAKE_EXE_LINKER_FLAGS "${COMMON_CPU_FLAGS} ${TZ_CMSE_FLAGS} -specs=nosys.specs -Wl,--gc-sections,--undefined=_sbrk ${MCU_LINKER_FLAGS} \
    -T${_tz_linker_script} --entry=Reset_Handler" CACHE STRING "Linker flags for MCU" FORCE)

# Work around a Ninja restat bug with arm-none-eabi-gcc: on link failure the
# toolchain deletes the output .elf, and Ninja restat then reports success
# because the output is "unchanged".  Wrapping the link command ensures a
# failure always leaves a touched output so restat never masks the error.
if(CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_C_LINK_EXECUTABLE
        "( <CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES> ) || ( <CMAKE_COMMAND> -E touch <TARGET> && false )"
    )
    set(CMAKE_CXX_LINK_EXECUTABLE
        "( <CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES> ) || ( <CMAKE_COMMAND> -E touch <TARGET> && false )"
    )
endif()

# Find OpenOCD - handles different names on Linux (openocd) and Windows (openocd.exe)
if (CFS)
    cmake_path(SET OPENOCD_SEARCH_PATH NORMALIZE "${CFS}/Tools/openocd")
else()
    cmake_path(SET OPENOCD_SEARCH_PATH NORMALIZE "${MAXIM_LIBRARIES}/../Tools/OpenOCD")
endif()

find_program(OPENOCD_PATH
    NAMES openocd
    HINTS ${OPENOCD_SEARCH_PATH}
    PATH_SUFFIXES bin
    DOC "Path to OpenOCD executable"
)

if(OPENOCD_PATH)
    if (CFS)
        cmake_path(SET OPENOCD_SCRIPTS NORMALIZE "${CFS}/Tools/openocd/share/openocd/scripts")
    else()
        cmake_path(SET OPENOCD_SCRIPTS NORMALIZE "${MAXIM_LIBRARIES}/../Tools/OpenOCD/scripts")
    endif()

    if (NOT DEFINED OPENOCD_INTERFACE)
        set(OPENOCD_INTERFACE "interface/cmsis-dap.cfg")
    endif()
    set(OPENOCD_CHIPNAME ${TARGET})
    set(OPENOCD_TARGETCFG "target/${TARGET}.cfg")

    if (NOT PROBE)
        set(PROBE "openocd")
    endif()

    message(STATUS "Found OpenOCD: ${OPENOCD_PATH}")
else()
    message(STATUS "OpenOCD not found in ${OPENOCD_SEARCH_PATH}")
endif()
