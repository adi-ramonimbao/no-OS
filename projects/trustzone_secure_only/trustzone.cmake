# Marks trustzone_secure_only as a Secure TrustZone application.
#
# The top-level CMakeLists includes this before the toolchain is processed (see
# the "TrustZone project" hook), so MSECURITY_MODE is set in the cache and the
# plain board preset works with no -DMSECURITY_MODE flag. This is a Secure-only
# producer (no nested Non-Secure tree); the framework emits the Secure image and
# its import library as separate deliverables.
set(MSECURITY_MODE SECURE CACHE STRING "TrustZone security world")

# --- Custom TrustZone memory settings (optional) --------------------------------
# Off by default: the SDK's default linker scripts are used and the partition SAU
# table + Non-Secure memory map (published in the contract) are derived from them.
# To ship a non-default split, set USE_CUSTOM_MEMORY_SETTINGS to 1 and any sizes
# you want to change (hex). A Non-Secure consumer must then match this split.
set(USE_CUSTOM_MEMORY_SETTINGS 0  CACHE STRING "Enable a non-default TrustZone memory split")
set(S_FLASH_SIZE   "" CACHE STRING "Secure flash size (hex); empty = default/derived")
set(NS_FLASH_SIZE  "" CACHE STRING "Non-Secure flash size (hex); empty = default/derived")
set(S_SRAM_SIZE    "" CACHE STRING "Secure SRAM size (hex); empty = default/derived")
set(NS_SRAM_SIZE   "" CACHE STRING "Non-Secure SRAM size (hex); empty = default/derived")
set(NSC_SIZE       "" CACHE STRING "Non-Secure Callable size (hex); empty = default split")
set(S_FLASH_START  "" CACHE STRING "Secure flash start (hex); empty = auto")
set(NS_FLASH_START "" CACHE STRING "Non-Secure flash start (hex); empty = auto")
set(S_SRAM_START   "" CACHE STRING "Secure SRAM start (hex); empty = auto")
set(NS_SRAM_START  "" CACHE STRING "Non-Secure SRAM start (hex); empty = auto")
set(EXECUTE_CODE_MEM "" CACHE STRING "Execute code from FLASH or SRAM; empty = FLASH")
