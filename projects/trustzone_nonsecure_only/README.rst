MAX32657 TrustZone Non-Secure-Only (Consumer)
=============================================
.. no-os-doxygen::

.. contents::
	:depth: 3

Overview
--------

``trustzone_nonsecure_only`` builds **only the Non-Secure world** of a
MAX32657 (Cortex-M33, Armv8-M **TrustZone**) application against a Secure image
produced elsewhere, then merges the two into one flashable HEX. The Secure world
is **not** rebuilt: only the memory-map + gateway contract is consumed.

It is the **consumer** half of a producer/consumer split. Its companion,
``trustzone_secure_only`` (the **producer**), emits the Secure image, the CMSE
import library and a contract; this project links against the import library so
the gateway veneers resolve, and combines its Non-Secure image with the Secure
HEX.

What the Non-Secure world does (``src/examples/keystore/keystore_nonsecure.c``) - an ordinary no-OS
CAPI application on the handed-over peripherals: routes ``printf`` through the
CAPI UART, then drives the Secure **keystore** across the boundary and shows the
boundary holding, before blinking the board LED (P0.13) through CAPI GPIO every
500 ms as a heartbeat. Five checks run once at startup:

* ``SELFTEST`` - ``KeystoreSelfTest_S()`` has the Secure world verify its key
  against a **Secure-held** known-answer and return only pass/fail, so the
  Non-Secure side proves key correctness without holding any reference answer.
* ``ROUNDTRIP`` - transforming twice restores the input (XOR is involutive): the
  key worked but never crossed the boundary.
* ``NON_IDENTITY`` - one transform changes the data, proving a non-trivial key
  is applied, without the Non-Secure side needing to know the output.
* ``REJECT_SECURE_DST`` - ``KeystoreTransform_S()`` aimed at Secure SRAM returns
  ``-EINVAL``: the gateway's CMSE check refuses the pointer.
* ``REJECT_DIRECT_READ`` - a direct Non-Secure read of Secure memory faults; the
  Secure world traps and recovers it, and ``KeystoreFaultCount_S()`` confirms
  the trap fired.

All three gateways resolve from the producer's import library at link time.

Consuming the Secure deliverables
---------------------------------

All three inputs are build outputs of the producer, found in its runtime output
directory (e.g. ``build-sec/build/``). Supply them on the cmake configure line;
the project's ``CMakeLists.txt`` reads ``SECURE_CONTRACT`` / ``SECURE_HEX`` /
``SECURE_IMPLIB``.

.. list-table::
	:header-rows: 1
	:widths: 24 30 46

	* - CMake var
	  - File
	  - What it is
	* - ``SECURE_CONTRACT``
	  - ``..._secure_only_contract.cmake``
	  - Generated cmake include; sets ``TZ_SECURE_HEX`` + ``TZ_SECURE_IMPLIB``
	    and records the Non-Secure memory map. The "one file that points at the
	    other two" convenience.
	* - ``SECURE_HEX``
	  - ``..._secure_only.hex``
	  - The Secure firmware image (Intel HEX) merged with this Non-Secure image.
	* - ``SECURE_IMPLIB``
	  - ``..._secure_only_implib.o``
	  - The CMSE import library (holds the Secure ``__ns_entry`` gateway veneer
	    addresses) the Non-Secure link resolves against.

Use ``SECURE_CONTRACT`` alone, **or** the ``SECURE_HEX`` + ``SECURE_IMPLIB``
pair. A pure Non-Secure app that calls no Secure gateway needs no import library
at all - omit it.

.. note::

	Without an import library (or with a mismatched one), the Non-Secure link
	fails with undefined references to ``KeystoreTransform_S`` /
	``KeystoreFaultCount_S``.

How CMake produces the combined image
-------------------------------------

The consumer uses the reusable ``no_os_add_maxim_trustzone_nonsecure_app()``
framework (``cmake/maxim/maxim_trustzone.cmake``); the project's ``CMakeLists.txt``
just declares its Non-Secure sources and forwards the Secure deliverables. The
outer build runs entirely in the Non-Secure world (``MSECURITY_MODE=NONSECURE``,
set by ``trustzone.cmake``). The Non-Secure memory map comes from the selected
``max32657_ns.ld`` -- the SDK's default-split script, or, for a custom split, the
exact ``max32657_ns.ld`` the producer **ships** (its contract points
``NO_OS_TZ_GEN_DIR`` at the shipped ``tz_gen/{secure,nonsecure}`` pair). Those
scripts are linked as-is -- there is no regeneration on the consumer side, and the
build hard-fails if they are missing -- so the map always matches the Secure image.

Steps: link the Non-Secure ELF against ``SECURE_IMPLIB`` -> ``objcopy -O ihex``
to ``..._nonsecure.hex`` (the Non-Secure image at ``0x01080000``) -> merge with
``SECURE_HEX`` into ``..._nonsecure_only.hex`` via
``cmake/maxim/maxim_tz_merge_hex.py`` (an Intel-HEX record-stream concatenation; the
two worlds occupy disjoint flash regions, so the merge keeps their absolute
addresses and checks for overlap).

Build and flash
---------------

Export ``MAXIM_LIBRARIES`` (or provide ``CFS_PATH``) as usual, and point the
build at the producer's deliverables. Two flashing behaviours are available,
selected by whether a Secure HEX is provided.

Combined flash (Secure + Non-Secure in one image)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Pass the contract (which sets both the HEX and the import library). The ``flash``
target then programs the **combined** image, so it flashes the Secure world too:

.. code-block:: bash

	export MAXIM_LIBRARIES=/path/to/msdk/Libraries
	cmake --preset max32657evkit -B build-ns \
	    -DPROJECT_DEFCONFIG=trustzone_nonsecure_only/nonsecure.conf \
	    -DSECURE_CONTRACT=$PWD/build-sec/build/trustzone_secure_only_contract.cmake
	cmake --build build-ns --target trustzone_nonsecure_only
	cmake --build build-ns --target flash        # programs the combined Secure+NS HEX

Non-Secure-only flash (iterate NS, leave Secure untouched)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Provision the Secure world once (flash ``trustzone_secure_only``), then
configure the consumer with the **import library only** (no ``SECURE_HEX`` /
``SECURE_CONTRACT``). The ``flash`` target then programs only the Non-Secure
region at ``0x01080000...``; the already-flashed Secure world is left in place
(``program`` erases/writes only the sectors the HEX spans):

.. code-block:: bash

	cmake --preset max32657evkit -B build-ns \
	    -DPROJECT_DEFCONFIG=trustzone_nonsecure_only/nonsecure.conf \
	    -DSECURE_IMPLIB=$PWD/build-sec/build/trustzone_secure_only_implib.o
	# iterate:
	cmake --build build-ns --target trustzone_nonsecure_only
	cmake --build build-ns --target flash        # programs only the NS region

.. note::

	Do **not** run the ``erase`` target (a full-chip erase) between flashes - it
	wipes both worlds. Rely on ``flash`` (sector-scoped). If you ever rebuild the
	Secure side, the gateway veneer addresses can shift, so you must re-flash
	Secure and reconfigure/rebuild the Non-Secure side against the new import
	library. As long as Secure is frozen, pure Non-Secure iteration is safe.

Expected console output (115200 8N1), once both worlds are on the part::

	**** MAX32657 TrustZone Secure producer (no-OS CAPI) ****
	Currently in the Secure world.
	Secret key held Secure; Non-Secure world drives it via gateways.
	Beginning transition to the Non-Secure world.
	Hello from the Non-Secure world (no-OS CAPI)!
	Driving the Secure keystore across the boundary.
	[KEYSTORE] SELFTEST          PASS
	[KEYSTORE] ROUNDTRIP          PASS
	[KEYSTORE] NON_IDENTITY       PASS
	[KEYSTORE] REJECT_SECURE_DST  PASS
	[KEYSTORE] REJECT_DIRECT_READ PASS

	All keystore checks passed.

with LED0 (P0.13) toggling every 500 ms.

Artifacts
---------

In ``build-ns/build/``:

* ``trustzone_nonsecure_only_nonsecure.hex`` - the Non-Secure image on its own
  (at ``0x01080000``). This is what an NS-only flash programs.
* ``trustzone_nonsecure_only.hex`` - with a Secure HEX supplied, the
  **combined** image; otherwise a copy of the Non-Secure-only image.

Inspecting
----------

.. code-block:: bash

	arm-none-eabi-objdump -h build-ns/build/trustzone_nonsecure_only.elf
	# combined HEX should carry three upper-address regions: 0108 (NS flash),
	# 1100 (Secure .text), 1107 (NSC veneers)
	grep -iE '^:02000004' build-ns/build/trustzone_nonsecure_only.hex | sort -u

Wiring - MAX32657 (MAX32657EVKIT)
---------------------------------

No external strapping is required. The console UART (UART0) is exposed on the
board's on-board USB-serial bridge; open it at **115200 8N1**. The blinking LED
is the on-board **LED0 (P0.13)**.

Layout
------

::

	projects/trustzone_nonsecure_only/
	├── CMakeLists.txt          # calls no_os_add_maxim_trustzone_nonsecure_app();
	│                           #   resolves SECURE_CONTRACT / SECURE_HEX / SECURE_IMPLIB
	├── trustzone.cmake         # marker: build Non-Secure world (MSECURITY_MODE=NONSECURE)
	├── Kconfig
	├── nonsecure.conf          # Non-Secure-world defconfig (UART/GPIO/TIME)
	├── README.rst
	└── src/
	    └── nonsecure/
	        ├── main.c              # CAPI UART print + keystore boundary tests +
	        │                       #   LED heartbeat + gateway calls
	        └── parameters.h        # UART + LED (P0.13) params

See also
--------

* ``projects/trustzone_secure_only`` - the producer that emits the Secure HEX,
  import library and contract this project consumes.
* ``projects/max32657_tz_hello`` - the same two worlds linked into one combined
  image by a single superbuild (no producer/consumer split).
