# =============================================================================
# TrustZone "hello" demo - platform-neutral project.
#
# Produces a single combined artifact holding both the Secure and Non-Secure
# worlds. Only src/platform/${PLATFORM}/{secure,nonsecure}/ differs per
# platform (bring-up + whatever isn't portable); src/examples/hello/ is fully
# portable CAPI application code, reused unchanged by every platform. All the
# two-tree superbuild machinery (implib generation, nested Non-Secure build,
# combined artifact post-build) lives in the reusable
# no_os_add_maxim_trustzone_app() framework, so this project only declares
# its sources.
#
# The outer build must run in the Secure world (MSECURITY_MODE=SECURE); that is
# set automatically by trustzone.cmake in this directory, so the plain board
# preset works.
#
# Maxim (MAX32657evkit; SDK location from the MAXIM_LIBRARIES/CFS environment,
# not a -D cache var):
#   export MAXIM_LIBRARIES=/path/to/msdk/Libraries
#   cmake --preset max32657evkit -B build \
#         -DPROJECT_DEFCONFIG=trustzone_hello/secure.conf
#   cmake --build build --target trustzone_hello
# =============================================================================

if(PLATFORM STREQUAL "maxim")
    include(maxim_trustzone)
    no_os_add_maxim_trustzone_app(trustzone_hello
        SECURE_SRC     src/platform/${PLATFORM}/secure/main.c
                       src/examples/hello/hello_secure.c
        SECURE_INC     src/platform/${PLATFORM}/secure    # holds partition_max32657.h + parameters.h
        NONSECURE_SRC  src/platform/${PLATFORM}/nonsecure/main.c
                       src/examples/hello/hello_nonsecure.c
        NONSECURE_INC  src/platform/${PLATFORM}/nonsecure)
else()
    message(FATAL_ERROR "trustzone_hello: unsupported PLATFORM '${PLATFORM}' (supported: maxim).")
endif()

