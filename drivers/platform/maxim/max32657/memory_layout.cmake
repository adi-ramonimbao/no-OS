# Bit 28 of an address selects the security alias: 0 = Non-Secure, 1 = Secure.
# The device boots Secure. Three layouts are emitted here, selected by
# MSECURITY_MODE (a -D cache variable set before the toolchain runs):
#
#   unset      Single Secure image (default, unchanged behaviour): the whole
#              physical Flash/SRAM with the Secure alias bit set on the bases.
#   SECURE     Secure half of a TrustZone build: Secure Flash 0x11000000 sized
#              to leave the top 0x2000 for the Non-Secure Callable (NSC) region,
#              Secure SRAM 0x30000000.
#   NONSECURE  Non-Secure half: Non-Secure Flash 0x01080000, Non-Secure SRAM
#              0x20020000 (bit 28 clear).
#
# The bases/sizes MUST match the regions in the linker scripts the toolchain
# selects (max32657.ld / max32657_s.ld / max32657_ns.ld) or the C headers and
# the linker will disagree on the memory map.

# Physical memory settings
set(PHY_FLASH_START 0x01000000)
set(PHY_FLASH_SIZE  0x00100000) # 1 MiB
set(PHY_SRAM_START  0x20000000)
set(PHY_SRAM_SIZE   0x00040000) # 256 KiB

# Secure alias bit (bit 28).
math(EXPR SECURE_BIT "1 << 28" OUTPUT_FORMAT HEXADECIMAL)

if(DEFINED MSECURITY_MODE AND MSECURITY_MODE STREQUAL "SECURE")
	# Secure Flash: first half of physical Flash, minus the top 0x2000 that
	# max32657_s.ld reserves for the NSC region (.gnu.sgstubs / SG veneers).
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
	math(EXPR __MXC_FLASH_MEM_BASE "${PHY_FLASH_START} | ${SECURE_BIT}" OUTPUT_FORMAT HEXADECIMAL)
	math(EXPR __MXC_SRAM_MEM_BASE "${PHY_SRAM_START} | ${SECURE_BIT}" OUTPUT_FORMAT HEXADECIMAL)
	set(__MXC_FLASH_MEM_SIZE ${PHY_FLASH_SIZE})
	set(__MXC_SRAM_MEM_SIZE ${PHY_SRAM_SIZE})
endif()

add_compile_definitions(
	__MXC_FLASH_MEM_BASE=${__MXC_FLASH_MEM_BASE}
	__MXC_FLASH_MEM_SIZE=${__MXC_FLASH_MEM_SIZE}
	__MXC_SRAM_MEM_BASE=${__MXC_SRAM_MEM_BASE}
	__MXC_SRAM_MEM_SIZE=${__MXC_SRAM_MEM_SIZE}
)
