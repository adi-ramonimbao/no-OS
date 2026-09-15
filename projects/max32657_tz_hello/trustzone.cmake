# Marks max32657_tz_hello as a Secure TrustZone application.
#
# The top-level CMakeLists includes this before the toolchain is processed (see
# the "TrustZone project" hook), so MSECURITY_MODE is set in the cache and the
# plain board preset works with no -DMSECURITY_MODE flag. The framework
# (no_os_add_maxim_trustzone_app) then drives the Secure outer / Non-Secure
# nested superbuild. The nested Non-Secure tree passes MSECURITY_MODE=NONSECURE
# explicitly, so the top-level guard skips this marker there.
set(MSECURITY_MODE SECURE CACHE STRING "TrustZone security world")
