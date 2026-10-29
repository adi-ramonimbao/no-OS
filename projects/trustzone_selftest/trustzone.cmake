# Marks trustzone_selftest as a Secure TrustZone application.
#
# The top-level CMakeLists includes this before the toolchain is processed (see
# the "TrustZone project" hook), so MSECURITY_MODE is set in the cache and the
# plain board preset works with no -DMSECURITY_MODE flag. The framework
# (no_os_add_maxim_trustzone_app) then drives the Secure outer / Non-Secure
# nested superbuild. The nested Non-Secure tree passes MSECURITY_MODE=NONSECURE
# explicitly, so the top-level guard skips this marker there.
set(MSECURITY_MODE SECURE CACHE STRING "TrustZone security world")

# --- Custom TrustZone memory settings (optional) --------------------------------
# Off by default: the build links the SDK's default linker scripts and derives the
# partition SAU table from them. To use a non-default split, set
# USE_CUSTOM_MEMORY_SETTINGS to 1 and any sizes you want to change (hex strings).
# Omitted sizes default to today's split; giving one side of a flash/SRAM pair
# auto-derives the other (S + NS = physical total: 1 MiB flash / 256 KiB SRAM).
# Starts are all-or-nothing (set all four or none; empty = script auto-places).
#
# These are cache variables read by drivers/platform/maxim/toolchain.cmake. Prefer
# passing overrides on the cmake line (e.g. -DUSE_CUSTOM_MEMORY_SETTINGS=1
# -DNS_FLASH_SIZE=0x000a0000); editing values here after a build is configured
# needs a fresh build directory to take effect (the cache is not force-overwritten).
set(USE_CUSTOM_MEMORY_SETTINGS 0  CACHE STRING "Enable a non-default TrustZone memory split")
set(S_FLASH_SIZE   "" CACHE STRING "Secure flash size (hex); empty = default/derived")
set(NS_FLASH_SIZE  "" CACHE STRING "Non-Secure flash size (hex); empty = default/derived")
set(S_SRAM_SIZE    "" CACHE STRING "Secure SRAM size (hex); empty = default/derived")
set(NS_SRAM_SIZE   "" CACHE STRING "Non-Secure SRAM size (hex); empty = default/derived")
set(NSC_SIZE       "" CACHE STRING "Non-Secure Callable size (hex); empty = 0x8000 (today's split)")
set(S_FLASH_START  "" CACHE STRING "Secure flash start (hex); empty = auto")
set(NS_FLASH_START "" CACHE STRING "Non-Secure flash start (hex); empty = auto")
set(S_SRAM_START   "" CACHE STRING "Secure SRAM start (hex); empty = auto")
set(NS_SRAM_START  "" CACHE STRING "Non-Secure SRAM start (hex); empty = auto")
set(EXECUTE_CODE_MEM "" CACHE STRING "Execute code from FLASH or SRAM; empty = FLASH")
