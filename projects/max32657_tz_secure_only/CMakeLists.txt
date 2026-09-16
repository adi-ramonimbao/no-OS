# =============================================================================
# MAX32657 TrustZone Secure-only PRODUCER demo.
#
# Builds only the Secure world and emits it as separately deliverable artifacts
# (Secure HEX + CMSE import library + a memory-map/gateway contract). A
# Non-Secure application built elsewhere (see max32657_tz_nonsecure_only) links
# against the import library and merges its image with the Secure HEX -- the
# Secure world is provisioned once and never rebuilt to iterate the Non-Secure
# side.
#
# The outer build runs in the Secure world (MSECURITY_MODE=SECURE), set by
# trustzone.cmake in this directory, so the plain board preset works:
#   export MAXIM_LIBRARIES=/path/to/msdk/Libraries
#   cmake --preset max32657evkit -B build-sec \
#         -DPROJECT_DEFCONFIG=max32657_tz_secure_only/secure.conf
#   cmake --build build-sec --target max32657_tz_secure_only
#
# Deliverables land in build-sec/<...>/: max32657_tz_secure_only.hex,
# max32657_tz_secure_only_implib.o, max32657_tz_secure_only_contract.cmake.
# =============================================================================

include(maxim_trustzone)

no_os_add_maxim_trustzone_secure_app(max32657_tz_secure_only
    SECURE_SRC  src/secure/main.c
    SECURE_INC  src/secure)            # holds partition_max32657.h + parameters.h
