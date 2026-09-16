# =============================================================================
# MAX32657 TrustZone CAPI self-test.
#
# A second project on the reusable no_os_add_maxim_trustzone_app() framework
# (proving it is not coupled to max32657_tz_hello). It produces a single
# combined ELF whose Non-Secure world runs the shared capi_selftest framework
# with two groups:
#
#   TRUSTZONE - security-gateway tests. Exercise the __ns_entry veneers from the
#               Non-Secure world: a valid pointer increments; a NULL pointer and
#               a Secure-memory pointer are both rejected with -EINVAL by the
#               veneer's cmse_check_pointed_object() guard; a value-returning
#               gateway proves the Secure -> Non-Secure return path.
#   DMA       - memory-to-memory transfers. From the Non-Secure world these run
#               on DMA0 (hardwired Non-Secure), so the run also proves the
#               world-correct DMA instance is selected automatically.
#
# The framework and the DMA test are reused verbatim from projects/capi_selftest
# (no duplication); only the Non-Secure runner and the TrustZone test group are
# new here. All transfers stay inside SRAM, so no external wiring is required.
#
# Build (SDK location comes from the environment, not a -D cache var):
#   export MAXIM_LIBRARIES=/path/to/msdk/Libraries
#   cmake --preset max32657evkit -B build \
#         -DPROJECT_DEFCONFIG=max32657_tz_selftest/secure.conf
#   cmake --build build --target max32657_tz_selftest
# =============================================================================

include(maxim_trustzone)

set(_selftest_common  ${CMAKE_CURRENT_SOURCE_DIR}/../capi_selftest/src/common)
set(_selftest_loopback ${CMAKE_CURRENT_SOURCE_DIR}/../capi_selftest/src/examples/capi_loopback)

no_os_add_maxim_trustzone_app(max32657_tz_selftest
    SECURE_SRC     src/secure/main.c
    SECURE_INC     src/secure            # holds partition_max32657.h + parameters.h
                   src                   # holds tz_gateways.h (shared contract)
    NONSECURE_SRC  src/nonsecure/main.c
                   src/nonsecure/tests/test_trustzone.c
                   ${_selftest_common}/common_data.c
                   ${_selftest_common}/test_framework.c
                   ${_selftest_loopback}/tests/test_dma.c
    NONSECURE_INC  src/nonsecure
                   src                   # holds tz_gateways.h (shared contract)
                   ${_selftest_common}
                   ${_selftest_loopback})
