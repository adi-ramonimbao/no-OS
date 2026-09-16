# Marks max32657_tz_nonsecure_only as a Non-Secure TrustZone application.
#
# The top-level CMakeLists includes this before the toolchain is processed (see
# the "TrustZone project" hook), so MSECURITY_MODE is set in the cache and the
# plain board preset works with no -DMSECURITY_MODE flag. This is a Non-Secure-
# only consumer: it links against a Secure image built elsewhere and merges the
# two into one flashable HEX (no Secure rebuild).
set(MSECURITY_MODE NONSECURE CACHE STRING "TrustZone security world")

# --- Secure deliverables to consume ---------------------------------------------
# The Non-Secure build links the gateway veneers against a Secure import library
# and merges its image with the Secure HEX, both produced by max32657_tz_secure_only.
# Supply them on the cmake line (preferred, keeps paths out of the tree):
#   -DSECURE_CONTRACT=/abs/.../max32657_tz_secure_only_contract.cmake
#   (or  -DSECURE_HEX=/abs/....hex  -DSECURE_IMPLIB=/abs/..._implib.o )
# The project CMakeLists reads SECURE_CONTRACT / SECURE_HEX / SECURE_IMPLIB.
#
# Or hardcode them here by uncommenting one of the following (cache vars are read
# before the project CMakeLists; a -D on the cmake line still overrides them, and
# editing these after a build is configured needs a fresh build dir):
#
# set(SECURE_CONTRACT "/abs/path/to/max32657_tz_secure_only_contract.cmake"
#     CACHE FILEPATH "Producer contract (sets TZ_SECURE_HEX + TZ_SECURE_IMPLIB)")
#
# set(SECURE_HEX    "/abs/path/to/max32657_tz_secure_only.hex"
#     CACHE FILEPATH "Secure image to merge with the Non-Secure image")
# set(SECURE_IMPLIB "/abs/path/to/max32657_tz_secure_only_implib.o"
#     CACHE FILEPATH "Secure CMSE import library the gateway veneers link against")

# A non-default Secure/Non-Secure split needs no extra flags: the producer ships
# its linker-script pair as a deliverable and the contract points NO_OS_TZ_GEN_DIR
# at it, so this build links those exact scripts (the authoritative memory-map
# contract -- no regeneration). If the scripts are missing the build hard-fails.
# With the default split nothing is shipped and the SDK's max32657_ns.ld is used.
#
# Include the producer contract here -- before the toolchain -- so NO_OS_TZ_GEN_DIR
# (and the Secure HEX/import-library paths) are set in time. Placed last so an
# uncommented set(SECURE_CONTRACT ...) above (or -DSECURE_CONTRACT) is honored. The
# project CMakeLists re-reads it too, which is harmless.
if(SECURE_CONTRACT)
    include(${SECURE_CONTRACT})
endif()
