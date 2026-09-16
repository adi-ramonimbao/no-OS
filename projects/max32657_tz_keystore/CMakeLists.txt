# =============================================================================
# MAX32657 TrustZone keystore demo.
#
# A minimal, self-contained example on the reusable
# no_os_add_maxim_trustzone_app() framework, alongside max32657_tz_hello. Where
# the hello demo shows the world-switch, this one puts a protected asset behind
# the boundary: a secret key that the Non-Secure world can use but never read.
#
# Produces a single combined ELF holding both worlds. All the two-tree
# superbuild machinery (implib generation, nested Non-Secure build, image
# embedding, two-pass link, combined-HEX post-build) lives in the framework, so
# this project only declares its two sources - no test framework, no shared
# helpers.
#
#   Secure     - owns the key, exposes a transform gateway (never the key), and
#                traps + recovers a Non-Secure access to Secure memory.
#   Non-Secure - drives the key through the gateway (a known-answer test proves
#                the key is present without revealing it) and shows the boundary
#                rejecting a Secure pointer and trapping a direct Secure read.
#
# The cipher is a trivial XOR and is NOT real cryptography (see tz_gateways.h);
# it stands in for whatever real Secure operation would run behind the boundary.
#
# Build (SDK location comes from the environment, not a -D cache var):
#   export MAXIM_LIBRARIES=/path/to/msdk/Libraries
#   cmake --preset max32657evkit -B build \
#         -DPROJECT_DEFCONFIG=max32657_tz_keystore/secure.conf
#   cmake --build build --target max32657_tz_keystore
# =============================================================================

include(maxim_trustzone)

no_os_add_maxim_trustzone_app(max32657_tz_keystore
    SECURE_SRC     src/secure/main.c
    SECURE_INC     src/secure            # holds partition_max32657.h + parameters.h
                   src                   # holds tz_gateways.h (shared contract)
    NONSECURE_SRC  src/nonsecure/main.c
    NONSECURE_INC  src/nonsecure
                   src)                  # holds tz_gateways.h (shared contract)
