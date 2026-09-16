# Bit 28 of an address selects the security alias: 0 = Non-Secure, 1 = Secure.
# The device boots Secure. Three layouts are emitted here, selected by
# MSECURITY_MODE (a -D cache variable set before the toolchain runs):
#
#   unset      Single Secure image (default, unchanged behaviour): the whole
#              physical Flash/SRAM with the Secure alias bit set on the bases.
#   SECURE     Secure half of a TrustZone build: Secure Flash 0x11000000 sized
#              to leave the top 0x8000 for the Non-Secure Callable (NSC) region,
#              Secure SRAM 0x30000000.
#   NONSECURE  Non-Secure half: Non-Secure Flash 0x01080000, Non-Secure SRAM
#              0x20020000 (bit 28 clear).
#
# The bases/sizes MUST match the regions in the linker scripts the toolchain
# selects (max32657.ld / max32657_s.ld / max32657_ns.ld) or the C headers and
# the linker will disagree on the memory map.

# Physical memory map (documented for reference; each layout below hardcodes
# the bases/sizes it commits to). Secure alias = physical base | (1 << 28).
#   Flash: 0x01000000, 0x00100000 (1 MiB)
#   SRAM:  0x20000000, 0x00040000 (256 KiB)

# Extract ORIGIN/LENGTH of a MEMORY region from a linker script. The region name
# must be followed by "(" (after optional spaces), so FLASH does not match FLASH_NS.
function(_no_os_ld_region ld name out_origin out_len)
	file(STRINGS "${ld}" _lines REGEX "^[ \t]*${name}[ \t]*\\(")
	if(NOT _lines OR NOT "${_lines}" MATCHES
	   "ORIGIN[ \t]*=[ \t]*(0x[0-9A-Fa-f]+).*LENGTH[ \t]*=[ \t]*(0x[0-9A-Fa-f]+)")
		message(FATAL_ERROR "memory_layout: region '${name}' not found in ${ld}")
	endif()
	set(${out_origin} "${CMAKE_MATCH_1}" PARENT_SCOPE)
	set(${out_len} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

if(DEFINED NO_OS_TZ_GEN_DIR)
	# Custom TrustZone memory: derive every base/size from the generated secure
	# linker script (single source of truth; it carries all regions), so the
	# __MXC_* defines, the SAU partition and the linker map cannot disagree.
	set(_tz_sld "${NO_OS_TZ_GEN_DIR}/secure/${TARGET}_s.ld")
	_no_os_ld_region("${_tz_sld}" FLASH    _fs_base _fs_size)
	_no_os_ld_region("${_tz_sld}" FLASH_NS _fns_base _fns_size)
	_no_os_ld_region("${_tz_sld}" SRAM     _ss_base _ss_size)
	_no_os_ld_region("${_tz_sld}" SRAM_NS  _sns_base _sns_size)
	if(DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "NONSECURE")
		set(__MXC_FLASH_MEM_BASE ${_fns_base})
		set(__MXC_FLASH_MEM_SIZE ${_fns_size})
		set(__MXC_SRAM_MEM_BASE  ${_sns_base})
		set(__MXC_SRAM_MEM_SIZE  ${_sns_size})
	else()
		set(__MXC_FLASH_MEM_BASE ${_fs_base})
		set(__MXC_FLASH_MEM_SIZE ${_fs_size})
		set(__MXC_SRAM_MEM_BASE  ${_ss_base})
		set(__MXC_SRAM_MEM_SIZE  ${_ss_size})
		# The MSDK max32657.h references these NS-variant defines under
		# CONFIG_TRUSTED_EXECUTION_SECURE==1; emit them for the Secure build.
		set(__MXC_FLASH_NS_MEM_BASE ${_fns_base})
		set(__MXC_FLASH_NS_MEM_SIZE ${_fns_size})
		set(__MXC_SRAM_NS_MEM_BASE  ${_sns_base})
		set(__MXC_SRAM_NS_MEM_SIZE  ${_sns_size})
	endif()
elseif(DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "SECURE")
	# Secure Flash: first half of physical Flash (0x80000), minus the top
	# 0x8000 that max32657_s.ld reserves for the NSC region (NSC_REGION @
	# 0x11078000, LENGTH 0x8000; holds .gnu.sgstubs / SG veneers).
	# 0x78000 + 0x8000 = 0x80000.
	set(__MXC_FLASH_MEM_BASE 0x11000000)
	set(__MXC_FLASH_MEM_SIZE 0x00078000)
	set(__MXC_SRAM_MEM_BASE  0x30000000)
	set(__MXC_SRAM_MEM_SIZE  0x00020000)
elseif(DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "NONSECURE")
	# Non-Secure Flash/SRAM: second half of each, Secure alias bit clear.
	set(__MXC_FLASH_MEM_BASE 0x01080000)
	set(__MXC_FLASH_MEM_SIZE 0x00080000)
	set(__MXC_SRAM_MEM_BASE  0x20020000)
	set(__MXC_SRAM_MEM_SIZE  0x00020000)
else()
	# Default single-image build: whole physical memory, Secure alias set.
	set(__MXC_FLASH_MEM_BASE 0x11000000)
	set(__MXC_FLASH_MEM_SIZE 0x00100000)
	set(__MXC_SRAM_MEM_BASE  0x30000000)
	set(__MXC_SRAM_MEM_SIZE  0x00040000)
endif()

add_compile_definitions(
	__MXC_FLASH_MEM_BASE=${__MXC_FLASH_MEM_BASE}
	__MXC_FLASH_MEM_SIZE=${__MXC_FLASH_MEM_SIZE}
	__MXC_SRAM_MEM_BASE=${__MXC_SRAM_MEM_BASE}
	__MXC_SRAM_MEM_SIZE=${__MXC_SRAM_MEM_SIZE}
)

# NS-variant defines, emitted only when derived (custom Secure TrustZone build).
if(DEFINED __MXC_FLASH_NS_MEM_BASE)
	add_compile_definitions(
		__MXC_FLASH_NS_MEM_BASE=${__MXC_FLASH_NS_MEM_BASE}
		__MXC_FLASH_NS_MEM_SIZE=${__MXC_FLASH_NS_MEM_SIZE}
		__MXC_SRAM_NS_MEM_BASE=${__MXC_SRAM_NS_MEM_BASE}
		__MXC_SRAM_NS_MEM_SIZE=${__MXC_SRAM_NS_MEM_SIZE}
	)
endif()
