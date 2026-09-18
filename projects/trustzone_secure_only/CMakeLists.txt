# =============================================================================
# TrustZone Secure-only PRODUCER demo - platform-neutral project.
#
# Builds only the Secure world and emits it as separately deliverable artifacts
# (Secure HEX + CMSE import library + a memory-map/gateway contract). A
# Non-Secure application built elsewhere (see trustzone_nonsecure_only) links
# against the import library and merges its image with the Secure HEX -- the
# Secure world is provisioned once and never rebuilt to iterate the Non-Secure
# side.
#
# The outer build runs in the Secure world (MSECURITY_MODE=SECURE), set by
# trustzone.cmake in this directory, so the plain board preset works.
#
# Maxim (MAX32657evkit; SDK location from the MAXIM_LIBRARIES/CFS environment):
#   export MAXIM_LIBRARIES=/path/to/msdk/Libraries
#   cmake --preset max32657evkit -B build-sec \
#         -DPROJECT_DEFCONFIG=trustzone_secure_only/secure.conf
#   cmake --build build-sec --target trustzone_secure_only
#
# STM32 (NUCLEO-H563ZI):
#   cmake --preset nucleo-h563zi -B build-sec \
#         -DPROJECT_DEFCONFIG=trustzone_secure_only/secure.conf
#   cmake --build build-sec --target trustzone_secure_only
#
# Deliverables land in build-sec/<...>/: trustzone_secure_only.hex,
# trustzone_secure_only_implib.o, trustzone_secure_only_contract.cmake.
# =============================================================================

if(PLATFORM STREQUAL "maxim")
    include(maxim_trustzone)
    no_os_add_maxim_trustzone_secure_app(trustzone_secure_only
        SECURE_SRC  src/platform/${PLATFORM}/secure/main.c
                    src/examples/keystore/keystore_secure.c
        SECURE_INC  src/platform/${PLATFORM}/secure)   # holds partition_max32657.h + parameters.h
elseif(PLATFORM STREQUAL "stm32")
    include(stm32_trustzone)
    no_os_add_stm32_trustzone_secure_app(trustzone_secure_only
        SECURE_SRC  src/platform/${PLATFORM}/secure/main.c
                    src/examples/keystore/keystore_secure.c
        SECURE_INC  src/platform/${PLATFORM}/secure)
else()
    message(FATAL_ERROR "trustzone_secure_only: unsupported PLATFORM '${PLATFORM}' (supported: maxim, stm32).")
endif()
