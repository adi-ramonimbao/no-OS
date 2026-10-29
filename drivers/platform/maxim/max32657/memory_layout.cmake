# Bit 28 of an address selects the security alias: 0 = Non-Secure, 1 = Secure.
# The device boots Secure. Three layouts are emitted here, selected by
# MSECURITY_MODE (a -D cache variable set before the toolchain runs):
#
#   unset      Single Secure image (default, unchanged behaviour): the whole
#              physical Flash/SRAM with the Secure alias bit set on the bases.
#   SECURE     Secure half of a TrustZone build: Secure Flash 0x11000000,
#              Secure SRAM 0x30000000, plus the Non-Secure regions as the
#              __MXC_*_NS_* defines.
#   NONSECURE  Non-Secure half: Non-Secure Flash 0x01080000, Non-Secure SRAM
#              0x20020000 (bit 28 clear).
#
# The values follow the MSDK max32657_memory.mk, which max32657.h names as the
# source of these defines. The Secure region includes the Non-Secure Callable
# (NSC) region: max32657_s.ld splits Secure Flash into FLASH (0x78000) and
# NSC_REGION (0x8000 @ 0x11078000), but __MXC_FLASH_MEM_SIZE covers both.
# The MSDK flash driver also relies on this: MXC_FLC_ME30_GetPhysicalAddress()
# masks with (MXC_FLASH_MEM_SIZE - 1), which needs a power-of-two size.

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
	_no_os_ld_region("${_tz_sld}" FLASH      _fs_base  _fs_size)
	_no_os_ld_region("${_tz_sld}" FLASH_NS   _fns_base _fns_size)
	_no_os_ld_region("${_tz_sld}" SRAM       _ss_base  _ss_size)
	_no_os_ld_region("${_tz_sld}" SRAM_NS    _sns_base _sns_size)
	_no_os_ld_region("${_tz_sld}" NSC_REGION _nsc_base _nsc_size)
	if(DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "NONSECURE")
		set(__MXC_FLASH_MEM_BASE ${_fns_base})
		set(__MXC_FLASH_MEM_SIZE ${_fns_size})
		set(__MXC_SRAM_MEM_BASE  ${_sns_base})
		set(__MXC_SRAM_MEM_SIZE  ${_sns_size})
	else()
		# setup_memory_tz.py carves the NSC region off the end of the Secure
		# region it sits in (Flash or SRAM, per EXECUTE_CODE_MEM); fold it
		# back in so the Secure size is the user's S_*_SIZE, as in the MSDK.
		math(EXPR _nsc_base_d "${_nsc_base}")
		math(EXPR _fs_end_d "${_fs_base} + ${_fs_size}")
		math(EXPR _ss_end_d "${_ss_base} + ${_ss_size}")
		if(_nsc_base_d EQUAL _fs_end_d)
			math(EXPR _fs_size "${_fs_size} + ${_nsc_size}" OUTPUT_FORMAT HEXADECIMAL)
		elseif(_nsc_base_d EQUAL _ss_end_d)
			math(EXPR _ss_size "${_ss_size} + ${_nsc_size}" OUTPUT_FORMAT HEXADECIMAL)
		else()
			message(FATAL_ERROR
				"memory_layout: NSC_REGION @ ${_nsc_base} does not follow the "
				"Secure FLASH or SRAM region in ${_tz_sld}")
		endif()
		set(__MXC_FLASH_MEM_BASE ${_fs_base})
		set(__MXC_FLASH_MEM_SIZE ${_fs_size})
		set(__MXC_SRAM_MEM_BASE  ${_ss_base})
		set(__MXC_SRAM_MEM_SIZE  ${_ss_size})
		set(__MXC_FLASH_NS_MEM_BASE ${_fns_base})
		set(__MXC_FLASH_NS_MEM_SIZE ${_fns_size})
		set(__MXC_SRAM_NS_MEM_BASE  ${_sns_base})
		set(__MXC_SRAM_NS_MEM_SIZE  ${_sns_size})
	endif()
elseif(DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "SECURE")
	# Secure: first half of each, NSC region included. Non-Secure: second half.
	set(__MXC_FLASH_MEM_BASE 0x11000000)
	set(__MXC_FLASH_MEM_SIZE 0x00080000)
	set(__MXC_SRAM_MEM_BASE  0x30000000)
	set(__MXC_SRAM_MEM_SIZE  0x00020000)
	set(__MXC_FLASH_NS_MEM_BASE 0x01080000)
	set(__MXC_FLASH_NS_MEM_SIZE 0x00080000)
	set(__MXC_SRAM_NS_MEM_BASE  0x20020000)
	set(__MXC_SRAM_NS_MEM_SIZE  0x00020000)
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

# Secure code also knows the Non-Secure regions (max32657.h, under
# CONFIG_TRUSTED_EXECUTION_SECURE == 1). Emitted for the Secure half of a
# TrustZone build only, as in the MSDK; a single-image build has no NS world.
if(DEFINED __MXC_FLASH_NS_MEM_BASE)
	add_compile_definitions(
		__MXC_FLASH_NS_MEM_BASE=${__MXC_FLASH_NS_MEM_BASE}
		__MXC_FLASH_NS_MEM_SIZE=${__MXC_FLASH_NS_MEM_SIZE}
		__MXC_SRAM_NS_MEM_BASE=${__MXC_SRAM_NS_MEM_BASE}
		__MXC_SRAM_NS_MEM_SIZE=${__MXC_SRAM_NS_MEM_SIZE}
	)
endif()
