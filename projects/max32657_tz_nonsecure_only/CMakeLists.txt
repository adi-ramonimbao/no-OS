# =============================================================================
# MAX32657 TrustZone Non-Secure-only CONSUMER demo.
#
# Builds only the Non-Secure world and combines it with a Secure image produced
# elsewhere (max32657_tz_secure_only) into one flashable HEX. The Secure world is
# NOT rebuilt: the Non-Secure link resolves the gateway veneers against the
# Secure import library, and the two HEX images are merged.
#
# Point at the Secure deliverables one of two ways:
#   (a) include the producer's emitted contract, which sets both paths:
#         cmake --preset max32657evkit -B build-ns \
#           -DPROJECT_DEFCONFIG=max32657_tz_nonsecure_only/nonsecure.conf \
#           -DSECURE_CONTRACT=/abs/path/to/max32657_tz_secure_only_contract.cmake
#   (b) pass the paths directly:
#           -DSECURE_HEX=/abs/.../max32657_tz_secure_only.hex \
#           -DSECURE_IMPLIB=/abs/.../max32657_tz_secure_only_implib.o
#   cmake --build build-ns --target max32657_tz_nonsecure_only
#
# The outer build runs in the Non-Secure world (MSECURITY_MODE=NONSECURE), set by
# trustzone.cmake here, so the plain board preset works.
# =============================================================================

# Resolve the Secure deliverables: a producer contract (sets TZ_SECURE_HEX /
# TZ_SECURE_IMPLIB) and/or direct -D overrides.
#
# All three point at build outputs of the producer (max32657_tz_secure_only),
# found in its runtime output dir (e.g. build-tzsec/build/). Use SECURE_CONTRACT
# alone, or the SECURE_HEX + SECURE_IMPLIB pair. For example:
#
#   SECURE_CONTRACT  .../build-tzsec/build/max32657_tz_secure_only_contract.cmake
#       generated cmake include; sets TZ_SECURE_HEX + TZ_SECURE_IMPLIB (below)
#       and records the Non-Secure memory map. The "one file that points at the
#       other two" convenience.
#   SECURE_HEX       .../build-tzsec/build/max32657_tz_secure_only.hex
#       the Secure firmware image (Intel HEX) merged with this Non-Secure image.
#   SECURE_IMPLIB    .../build-tzsec/build/max32657_tz_secure_only_implib.o
#       the CMSE import library (a small object holding the Secure __ns_entry
#       gateway veneer addresses) the Non-Secure link resolves against.
if(SECURE_CONTRACT)
    include(${SECURE_CONTRACT})
endif()
if(SECURE_HEX)
    set(TZ_SECURE_HEX ${SECURE_HEX})
endif()
if(SECURE_IMPLIB)
    set(TZ_SECURE_IMPLIB ${SECURE_IMPLIB})
endif()

include(maxim_trustzone)

no_os_add_maxim_trustzone_nonsecure_app(max32657_tz_nonsecure_only
    NONSECURE_SRC  src/nonsecure/main.c
    NONSECURE_INC  src/nonsecure
    SECURE_IMPLIB  ${TZ_SECURE_IMPLIB}
    SECURE_HEX     ${TZ_SECURE_HEX})
