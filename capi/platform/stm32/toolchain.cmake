# STM32 toolchain for CAPI projects. Identical to the drivers one for now; it
# exists so capi/ does not depend on drivers/, which it replaces.

message(STATUS "STM32 Platform")

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# This toolchain file is re-included for CMake's internal try_compile() checks,
# but cache variables (e.g. TARGET from the board preset) are not forwarded
# into those scratch projects by default. Forward TARGET so the MCU-flag
# selection below resolves identically in the main build and the compiler
# checks; otherwise TARGET is empty there and selection fails.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TARGET)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
find_package(STM32CubeMX REQUIRED)
find_package(STM32CubeIDE REQUIRED)

if(USE_VENDOR_TOOLCHAIN)
    # Locate the GCC toolchain bundled with CubeIDE
    file(GLOB _cubeide_gcc_bin
        "${CUBEIDE_DIR}/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin"
    )
    if(_cubeide_gcc_bin)
        list(GET _cubeide_gcc_bin 0 CROSS_COMPILER_BIN)
        message(STATUS "Using CubeIDE-bundled GCC: ${CROSS_COMPILER_BIN}")
    else()
        message(WARNING
            "Could not locate the CubeIDE-bundled GCC under ${CUBEIDE_DIR}/plugins. "
            "Falling back to arm-none-eabi-gcc on PATH.")
    endif()

    find_program(CMAKE_C_COMPILER   arm-none-eabi-gcc HINTS ${CROSS_COMPILER_BIN})
    find_program(CMAKE_CXX_COMPILER arm-none-eabi-g++ HINTS ${CROSS_COMPILER_BIN})
    find_program(CMAKE_ASM_COMPILER arm-none-eabi-gcc HINTS ${CROSS_COMPILER_BIN})
    find_program(CMAKE_LINKER       arm-none-eabi-ld  HINTS ${CROSS_COMPILER_BIN})
    find_program(CMAKE_SIZE         arm-none-eabi-size HINTS ${CROSS_COMPILER_BIN})
    find_program(CMAKE_OBJCOPY      arm-none-eabi-objcopy HINTS ${CROSS_COMPILER_BIN})

    # Report a missing cross-compiler here, with the directory we searched,
    # rather than letting CMake fail later with a generic compiler-detection error.
    if(NOT CMAKE_C_COMPILER)
        message(FATAL_ERROR
            "arm-none-eabi-gcc not found (searched CROSS_COMPILER_BIN=${CROSS_COMPILER_BIN} and PATH). "
            "Install the ARM GCC toolchain or STM32CubeIDE. "
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

# Find OpenOCD — an explicit -DOPENOCD_PATH wins, then CubeIDE-bundled, then
# system. The CLI override matters on hosts where the CubeIDE-bundled fork is
# unusable from the command line (its st_scripts hit an "Infinite eval
# recursion" TCL bug when driven outside the IDE); point it at a mainline
# OpenOCD with -DOPENOCD_PATH=/usr/bin/openocd -DOPENOCD_SCRIPTS=... instead.
#
# Only search when CUBEIDE_DIR is set; an empty value would expand the glob to
# "/plugins/*/openocd" (filesystem root) and match unrelated host paths.
# The recursive glob can also match *directories* named "openocd" (CubeIDE ships
# several), so pick the first entry that is an actual executable file.
if(OPENOCD_PATH)
    # Explicit -DOPENOCD_PATH (or a cached value): respect it verbatim and do
    # not pull in the CubeIDE scripts, so a mainline OpenOCD is driven with its
    # own scripts (supply -DOPENOCD_SCRIPTS to match).
    message(STATUS "Using provided OpenOCD: ${OPENOCD_PATH}")
elseif(CUBEIDE_DIR)
    file(GLOB_RECURSE _cubeide_openocd ${CUBEIDE_DIR}/plugins/*/openocd)
    foreach(_candidate ${_cubeide_openocd})
        if(NOT IS_DIRECTORY "${_candidate}")
            set(OPENOCD_PATH "${_candidate}")
            break()
        endif()
    endforeach()
    if(OPENOCD_PATH)
        file(GLOB_RECURSE _cubeide_scripts ${CUBEIDE_DIR}/plugins/*/mem_helper.tcl)
        if(_cubeide_scripts)
            list(GET _cubeide_scripts 0 _scripts_file)
            cmake_path(GET _scripts_file PARENT_PATH OPENOCD_SCRIPTS)
        endif()
        message(STATUS "Found CubeIDE-bundled OpenOCD: ${OPENOCD_PATH}")
    endif()
endif()

if(NOT OPENOCD_PATH)
    find_program(OPENOCD_PATH
        NAMES openocd
        PATH_SUFFIXES bin
        DOC "Path to OpenOCD executable"
    )
    if(OPENOCD_PATH)
        cmake_path(GET OPENOCD_PATH PARENT_PATH _ocd_bin_dir)
        cmake_path(GET _ocd_bin_dir PARENT_PATH _ocd_prefix)
        cmake_path(SET _ocd_scripts NORMALIZE "${_ocd_prefix}/share/openocd/scripts")
        if(EXISTS "${_ocd_scripts}")
            set(OPENOCD_SCRIPTS "${_ocd_scripts}")
        endif()
        message(STATUS "Found system OpenOCD: ${OPENOCD_PATH}")
    else()
        message(STATUS "OpenOCD not found")
    endif()
endif()

set(CMAKE_EXECUTABLE_SUFFIX_ASM ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")

# Detect MCU type and set appropriate flags
if("${TARGET}" MATCHES "^stm32f7")
    # Cortex-M7 MCUs (STM32F7xx)
    set(STM32_MCU_FLAGS "-mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16")
    message(STATUS "Detected Cortex-M7 MCU: ${TARGET}")
elseif("${TARGET}" MATCHES "^stm32h7")
    # Cortex-M7 MCUs (STM32H7xx)
    set(STM32_MCU_FLAGS "-mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16")
    message(STATUS "Detected Cortex-M7 MCU: ${TARGET}")
elseif("${TARGET}" MATCHES "^stm32f4")
    # Cortex-M4 MCUs (STM32F4xx)
    set(STM32_MCU_FLAGS "-mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16")
    message(STATUS "Detected Cortex-M4 MCU: ${TARGET}")
elseif("${TARGET}" MATCHES "^stm32l4")
    # Cortex-M4 MCUs (STM32L4xx)
    set(STM32_MCU_FLAGS "-mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16")
    message(STATUS "Detected Cortex-M4 MCU: ${TARGET}")
elseif("${TARGET}" MATCHES "^stm32h5")
    # Cortex-M33 MCUs (STM32H5xx)
    set(STM32_MCU_FLAGS "-mthumb -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16")
    message(STATUS "Detected Cortex-M33 MCU: ${TARGET}")
else()
    # An unset or unrecognized TARGET would otherwise silently produce a
    # binary with wrong -mcpu/-mfpu flags (e.g. Cortex-M4 flags for a
    # Cortex-M7 part), so fail loudly instead of guessing.
    message(FATAL_ERROR "Unknown or unset STM32 TARGET '${TARGET}'. Set TARGET in the board preset (e.g. stm32f756).")
endif()

# TrustZone security context for this build tree. MSECURITY_MODE is unset for an
# ordinary single-image build (today's behaviour, unchanged), SECURE for the
# secure half of a TrustZone build, and NONSECURE for the non-secure half. Only
# the secure side compiles with -mcmse (which sets __ARM_FEATURE_CMSE==3 and
# activates the SAU/veneer code paths); the non-secure side must NOT get it.
# Unlike Maxim, the linker script itself is NOT selected here: this platform
# already sets it per-target downstream (config_platform_sdk()/stm32_trustzone.cmake
# via target_link_options(... -T...)), so only the compile/link flag differs.
# STM32H5 is the only supported family with an Armv8-M/CMSE core (F4/F7/H7/L4
# are Cortex-M4/M7, no CMSE extension), so reject MSECURITY_MODE on anything
# else with a clear message instead of letting -mcmse reach one of those
# compiles and fail with an opaque GCC error.
set(TZ_CMSE_FLAGS "")
if(DEFINED MSECURITY_MODE)
    if(NOT "${TARGET}" MATCHES "^stm32h5")
        message(FATAL_ERROR
            "MSECURITY_MODE is set but TARGET='${TARGET}' is not a TrustZone-capable "
            "STM32 family (only STM32H5/Cortex-M33 supports it).")
    endif()
    if(MSECURITY_MODE STREQUAL "SECURE")
        set(TZ_CMSE_FLAGS "-mcmse")
    elseif(NOT MSECURITY_MODE STREQUAL "NONSECURE")
        message(FATAL_ERROR
            "MSECURITY_MODE='${MSECURITY_MODE}' is invalid. "
            "Use SECURE, NONSECURE, or leave it unset for a single-image build.")
    endif()
endif()

# Common flags for all build types
set(CMAKE_C_FLAGS "${STM32_MCU_FLAGS} ${TZ_CMSE_FLAGS} -ffunction-sections -fdata-sections -MD" CACHE STRING "C compiler flags" FORCE)
set(CMAKE_CXX_FLAGS "${STM32_MCU_FLAGS} ${TZ_CMSE_FLAGS} -ffunction-sections -fdata-sections -MD" CACHE STRING "C++ compiler flags" FORCE)
set(CMAKE_ASM_FLAGS "${STM32_MCU_FLAGS} -x assembler-with-cpp" CACHE STRING "ASM compiler flags" FORCE)

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

# Linker flags (common for all build types). -mcmse is carried onto the secure
# link too (empty otherwise) so gcc selects the CMSE-aware spec.
set(CMAKE_EXE_LINKER_FLAGS "${STM32_MCU_FLAGS} ${TZ_CMSE_FLAGS} -specs=nosys.specs -Wl,--gc-sections ${MCU_LINKER_FLAGS} --entry=Reset_Handler" CACHE STRING "Linker flags for MCU" FORCE)

if (NOT PROBE)
    set(PROBE "openocd")
endif()
