# =============================================================================
# MAX32657 TrustZone "hello" demo.
#
# Produces a single combined ELF holding both the Secure and Non-Secure worlds.
# All the two-tree superbuild machinery (implib generation, nested Non-Secure
# build, image embedding, two-pass link, combined-HEX post-build) lives in the
# reusable no_os_add_maxim_trustzone_app() framework, so this project only
# declares its sources.
#
# The outer build must run in the Secure world (MSECURITY_MODE=SECURE); that is
# set automatically by trustzone.cmake in this directory, so the plain board
# preset works. The SDK location comes from the environment (MAXIM_LIBRARIES or
# CFS), not a -D cache var:
#   export MAXIM_LIBRARIES=/path/to/msdk/Libraries
#   cmake --preset max32657evkit -B build \
#         -DPROJECT_DEFCONFIG=max32657_tz_hello/secure.conf
#   cmake --build build --target max32657_tz_hello
# =============================================================================

include(maxim_trustzone)

no_os_add_maxim_trustzone_app(max32657_tz_hello
    SECURE_SRC     src/platform/${PLATFORM}/secure/main.c
                   src/examples/hello/hello_secure.c
    SECURE_INC     src/platform/${PLATFORM}/secure    # holds partition_max32657.h + parameters.h
    NONSECURE_SRC  src/platform/${PLATFORM}/nonsecure/main.c
                   src/examples/hello/hello_nonsecure.c
    NONSECURE_INC  src/platform/${PLATFORM}/nonsecure)
