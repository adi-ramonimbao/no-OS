MAX32657 TrustZone Secure-Only (Producer)
=========================================
.. no-os-doxygen::

.. contents::
	:depth: 3

Overview
--------

``max32657_tz_secure_only`` builds **only the Secure world** of a MAX32657
(Cortex-M33, Armv8-M **TrustZone**) application and emits it as a set of
**separately deliverable artifacts**, for the case where the Secure firmware is
owned, signed or provisioned by one party and the Non-Secure application is
built and iterated independently by another.

It is the **producer** half of a producer/consumer split. Its companion,
``max32657_tz_nonsecure_only`` (the **consumer**), links against this project's
import library and merges its Non-Secure image with this project's Secure HEX -
the Secure world is provisioned once and never rebuilt to iterate the
Non-Secure side.

This contrasts with ``max32657_tz_hello`` / ``max32657_tz_selftest``, which link
**both** worlds into one combined ELF in a single superbuild. Here the two
worlds are produced by two separate builds around a shared contract.

What the Secure world does (``src/platform/maxim/secure/main.c`` for bring-up,
``src/examples/keystore/keystore_secure.c`` for the gateways):

* brings up the console UART through CAPI and prints a banner,
* enables the SecureFault exception so a Non-Secure access to Secure memory is
  trapped and recovered here instead of escalating,
* exposes the flash code region as Non-Secure-Callable so the gateway veneers
  are reachable from Non-Secure code,
* hands GPIO0 / GCR / UART to the Non-Secure world with ``MXC_SPC_SetNonSecure()``,
* branches into the Non-Secure image with ``NonSecure_Init()`` **only if** a
  Non-Secure image has actually been programmed - so this Secure-only image can
  be flashed standalone without faulting on an erased region.

The Secure world owns a **secret key** and exposes it only as an operation,
never as data. Three secure gateways (``__ns_entry``) are exported for the
Non-Secure world and recorded in the emitted import library:

* ``int KeystoreTransform_S(uint8_t *buf_ns, size_t len)`` - XOR a Non-Secure
  buffer with the Secure-held key, after validating the whole buffer with
  ``cmse_check_address_range()`` (a pointer into Secure memory is rejected,
  never dereferenced). The key never leaves the Secure world.
* ``int KeystoreSelfTest_S(void)`` - verify the key against a **Secure-held**
  known-answer entirely inside the Secure world and return only pass/fail, so
  the Non-Secure world can confirm key correctness without holding any
  reference answer of its own.
* ``uint32_t KeystoreFaultCount_S(void)`` - report how many Non-Secure accesses
  to Secure memory the ``SecureFault_Handler()`` has trapped and recovered.

The cipher is a trivial repeating-key XOR standing in for whatever real
operation (AES, ECDSA sign, ...) a production Secure world would run behind the
same boundary; the security feature on show is the boundary, not the cipher.

Standalone run behaviour
~~~~~~~~~~~~~~~~~~~~~~~~~~

Flashed on its own (no Non-Secure image present), it does **not** fault. The
``nonsecure_image_present()`` guard reads the Non-Secure reset vector at
``0x01080000``; on erased flash (``0xFFFFFFFF``) it prints and spins in the
Secure world::

	**** MAX32657 TrustZone Secure producer (no-OS CAPI) ****
	Currently in the Secure world.
	No Non-Secure image programmed; staying in the Secure world.

Once a Non-Secure image is merged/flashed alongside (see the consumer project),
the guard passes and it hands over as usual.

Deliverables
------------

Building the ``max32657_tz_secure_only`` target produces, in ``build/build/``:

.. list-table::
	:header-rows: 1
	:widths: 34 66

	* - File
	  - What it is
	* - ``max32657_tz_secure_only.hex``
	  - The Secure firmware image (Intel HEX; the Secure code at the Secure
	    flash alias, plus the SG veneers). ``.nonsecure_flash`` is empty.
	* - ``max32657_tz_secure_only_implib.o``
	  - The **CMSE import library**: a small object holding the addresses of the
	    Secure ``__ns_entry`` gateway veneers. A separately built Non-Secure
	    image links against it to resolve the gateway calls.
	* - ``max32657_tz_secure_only_contract.cmake``
	  - A generated cmake include that ``set()``\ s ``TZ_SECURE_HEX`` and
	    ``TZ_SECURE_IMPLIB`` to the two files above, and records the Non-Secure
	    memory map (flash / SRAM / NSC origins) the consumer must match. For a
	    custom split it also **ships** the generated
	    ``tz_gen/{secure,nonsecure}`` linker-script pair and records
	    ``NO_OS_TZ_GEN_DIR`` pointing at it, so the consumer links those exact
	    scripts (no regeneration).

How CMake produces the Secure-only image
-----------------------------------------

The producer uses the reusable ``no_os_add_maxim_trustzone_secure_app()``
framework (``cmake/maxim/maxim_trustzone.cmake``); the project's ``CMakeLists.txt``
just declares its Secure sources. With no Non-Secure image to embed there is no
Secure <-> Non-Secure circular dependency, so the two-pass implib dance of the
combined superbuild collapses to a **single link**: one Secure executable linked
with ``-Wl,--cmse-implib -Wl,--out-implib=...``, which yields both the Secure
image and the import library in one step.

Gate 1 (partition regeneration) still runs: ``partition_max32657.h`` is
regenerated from the active ``max32657_s.ld`` to the no-OS SAU policy, so the
Secure-only build applies the identical policy the combined superbuild does.

The Secure HEX is emitted with ``objcopy -O ihex`` (no ``-O binary``: the empty
``.nonsecure_flash`` at the Non-Secure alias would balloon a raw binary).

Build
-----

Point the build at your MSDK by exporting ``MAXIM_LIBRARIES`` (or provide a CFS
install via ``CFS_PATH``). The project ships a ``trustzone.cmake`` marker, so the
outer tree is placed in the Secure world (``MSECURITY_MODE=SECURE``)
automatically - the plain ``max32657evkit`` preset works with no extra flag.

.. code-block:: bash

	export MAXIM_LIBRARIES=/path/to/msdk/Libraries
	cmake --preset max32657evkit -B build-sec \
	    -DPROJECT_DEFCONFIG=max32657_tz_secure_only/secure.conf
	cmake --build build-sec --target max32657_tz_secure_only

Flash the Secure image (this writes only the Secure sectors at ``0x11000000...``)::

	cmake --build build-sec --target flash

.. note::

	This is the "provision Secure once" step. To then build and iterate a
	Non-Secure application against these deliverables - including flashing only
	the Non-Secure region so this Secure image is left untouched - see
	``projects/max32657_tz_nonsecure_only``.

Custom memory split (optional)
------------------------------

The default build uses the SDK's pre-baked Secure/Non-Secure split. To ship a
non-default split, enable ``USE_CUSTOM_MEMORY_SETTINGS`` and set any size(s) you
want to change (hex); a size pair's missing half is auto-derived, so setting just
the Non-Secure flash size is enough:

.. code-block:: bash

	cmake --preset max32657evkit -B build-sec \
	    -DPROJECT_DEFCONFIG=max32657_tz_secure_only/secure.conf \
	    -DUSE_CUSTOM_MEMORY_SETTINGS=1 -DNS_FLASH_SIZE=0x000a0000
	cmake --build build-sec --target max32657_tz_secure_only

The full set of knobs (all hex, all optional) is ``S_FLASH_SIZE`` /
``NS_FLASH_SIZE`` / ``S_SRAM_SIZE`` / ``NS_SRAM_SIZE`` / ``NSC_SIZE``, the matching
``*_START`` origins, and ``EXECUTE_CODE_MEM`` (``FLASH`` or ``SRAM``). They can be
passed on the cmake line as above or declared in this project's
``trustzone.cmake``; changing them after a build is configured needs a fresh build
dir.

A custom split makes the generated ``tz_gen/{secure,nonsecure}`` linker-script
pair part of the deliverables: the contract records ``NO_OS_TZ_GEN_DIR`` pointing
at the shipped pair, and the consumer links **those exact scripts** (no
regeneration; it hard-fails if they are missing). Both worlds must therefore be
built from the **same** producer deliverables.

Inspecting the Secure-only image
--------------------------------

.. code-block:: bash

	arm-none-eabi-objdump -h build-sec/build/max32657_tz_secure_only.elf
	arm-none-eabi-nm      build-sec/build/max32657_tz_secure_only_implib.o

Expect ``.text`` at ``0x11000000`` and ``.gnu.sgstubs`` at ``0x11078000``, and
**no** ``.nonsecure_flash`` (it is empty). The import library exports
``KeystoreTransform_S``, ``KeystoreSelfTest_S`` and ``KeystoreFaultCount_S`` at
their NSC-region veneer addresses.

Wiring - MAX32657 (MAX32657EVKIT)
---------------------------------

No external strapping is required. The console UART (UART0) is exposed on the
board's on-board USB-serial bridge; open it at **115200 8N1**.

Layout
------

::

	projects/max32657_tz_secure_only/
	├── CMakeLists.txt          # minimal: calls no_os_add_maxim_trustzone_secure_app()
	├── trustzone.cmake         # marker: build Secure world (MSECURITY_MODE=SECURE)
	├── Kconfig
	├── secure.conf             # Secure-world defconfig (console UART)
	├── README.rst
	└── src/
	    └── secure/
	        ├── main.c              # banner, SPC handover, guarded NonSecure_Init,
	        │                       #   secret key + KeystoreTransform_S/
	        │                       #   KeystoreSelfTest_S/KeystoreFaultCount_S
	        │                       #   gateways + SecureFault
	        └── parameters.h        # console UART params + NS_FLASH_ORIGIN
