MAX32657 TrustZone Hello
========================
.. no-os-doxygen::

.. contents::
	:depth: 3

Overview
--------

``max32657_tz_hello`` ports the MSDK ``Hello_World_TZ`` example into no-OS and
its CAPI (Common API) layer. It demonstrates the Armv8-M **TrustZone** split on
the MAX32657 (Cortex-M33): the device boots **Secure**, configures the security
controllers, hands a set of peripherals to the **Non-Secure** world, and
branches into a Non-Secure application that does the actual work while calling
back into the Secure world through a gateway function.

The whole thing links into **one flashable image** that carries both worlds -
the Secure code at the Secure flash alias and the Non-Secure image embedded at
the Non-Secure flash origin - exactly as the MSDK example produces a single
combined ``.elf``.

What it does at run time:

* **Secure world** (``src/secure/main.c``) - brings up the console UART through
  CAPI, prints a banner, then sets the Non-Secure-Callable code region and hands
  GPIO0, the GCR and the UART to the Non-Secure world with
  ``MXC_SPC_SetNonSecure()``. It exposes one gateway,
  ``__ns_entry int IncrementCount_S(volatile int *)``, which validates the
  caller-supplied pointer with ``cmse_check_pointed_object()`` and increments a
  counter that lives in Non-Secure memory. It finally calls ``NonSecure_Init()``
  to jump to the Non-Secure world.

* **Non-Secure world** (``src/nonsecure/main.c``) - an ordinary no-OS CAPI
  application on the handed-over peripherals: it routes ``printf`` through the
  CAPI UART, blinks the board LED (P0.13) through CAPI GPIO every 500 ms, and on
  each iteration calls ``IncrementCount_S()`` to advance the counter in the
  Secure world, printing the running ``count``.

Expected console output (115200 8N1)::

	**** MAX32657 Hello World with TrustZone (no-OS CAPI) ****
	Currently in the Secure world.
	Beginning transition to the Non-Secure world.
	Hello from the Non-Secure world (no-OS CAPI)!
	count = 1
	count = 2
	count = 3
	...

with LED0 (P0.13) toggling every 500 ms.

Does this need a new CAPI header?
---------------------------------

**No new peripheral CAPI header.** TrustZone is not a peripheral: the
world-switch, the SAU/SPC/MPC setup and the secure-alias memory map are
inherently platform-specific, and the secure gateway (``__ns_entry``) functions
are application-defined. This project reuses the MSDK's
``system_max32657.c::NonSecure_Init()``, the ``MXC_SPC_*`` / ``MXC_MPC_*``
drivers and ``partition_max32657.h`` directly. The existing per-peripheral CAPI
backends (UART, GPIO, time, ...) are security-agnostic and run unmodified in the
Non-Secure world once the peripheral is handed over.

One existing-code change was required: ``capi/platform/maxim/max32657``'s
backend asserted ``CONFIG_TRUSTED_EXECUTION_SECURE == 1`` (in
``maxim_capi_dma.h``, which every CAPI translation unit pulls in via
``maxim_capi_uart.h``). That assertion made a Non-Secure CAPI build impossible.
It has been relaxed to require the symbol only be *defined* (``0`` or ``1``);
``0`` is legitimate for the Non-Secure half, and the single-image all-Secure
build (``capi_selftest`` and friends) is unaffected because it still defines the
symbol as ``1``.

How CMake produces one binary
-----------------------------

The Maxim toolchain fixes the CPU flags, ``-mcmse`` and the ``-T<linker
script>`` **globally per build tree** (forced cache variables in
``drivers/platform/maxim/toolchain.cmake``). Secure and Non-Secure need
different values of all three, so they cannot share one CMake build tree. The
project is therefore a **superbuild**: the outer (Secure) build drives a nested
(Non-Secure) build and relinks the result into itself. A new cache variable,
``MSECURITY_MODE`` (``SECURE`` / ``NONSECURE``, unset = today's single-image
behaviour), selects the memory layout, the linker script and ``-mcmse`` per
tree.

The superbuild is provided by the reusable ``no_os_add_maxim_trustzone_app()``
framework (``cmake/maxim_trustzone.cmake``); the project's ``CMakeLists.txt`` just
declares its sources. Its dependency chain mirrors the MSDK ``max32657.mk``:

#. **Compile** the Secure objects with ``-mcmse`` and ``max32657_s.ld``
   (``__ARM_FEATURE_CMSE == 3`` activates the SAU/veneer code paths in the MSDK
   sources).
#. **Pass-A link** -> ``secure_implib.o``. A throwaway Secure image linked with
   ``--cmse-implib --out-implib`` to emit the SG-veneer import library. It must
   *not* include the Non-Secure image, to break the circular dependency.
#. **Nested Non-Secure build** -> ``nonsecure.bin``. A second CMake tree
   configured with ``MSECURITY_MODE=NONSECURE`` and
   ``PROJECT_DEFCONFIG=max32657_tz_hello/nonsecure.conf``, linking
   ``secure_implib.o`` so the veneer symbols resolve. Its ELF is ``objcopy``'d
   to a raw binary.
#. **Embed** -> ``nonsecure.o``. ``nonsecure_load.S`` ``.incbin``\ s
   ``nonsecure.bin`` into section ``.nonsecure_flash``. The object must be named
   literally ``nonsecure.o`` because ``max32657_s.ld`` pins it with
   ``KEEP(*nonsecure.o)`` at the Non-Secure flash origin.
#. **Pass-B link** (final) -> the combined ELF: the Secure objects **plus**
   ``nonsecure.o``. ``max32657_s.ld`` places the Non-Secure image at
   ``0x01080000`` and the SG veneers (``.gnu.sgstubs``) into the NSC region at
   ``0x11078000``.

Reused MSDK artifacts (from ``${MAXIM_LIBRARIES}/CMSIS/Device/Maxim/MAX32657``):
``GCC/max32657_s.ld`` / ``GCC/max32657_ns.ld`` (linker scripts),
``Source/system_max32657.c`` (``NonSecure_Init()`` / SAU setup) and the SPC/MPC
drivers under ``PeriphDrivers/Source/TZ``. ``partition_max32657.h`` (the SAU
config) is copied into ``src/secure/`` because the Secure build must find it on
its include path, matching the MSDK example.

Build
-----

Point the build at your MSDK by exporting ``MAXIM_LIBRARIES`` (or provide a CFS
install via ``CFS_PATH``); the Maxim toolchain reads the SDK location from the
environment, not from a ``-D`` cache variable. Then configure with the plain
``max32657evkit`` preset. The project ships a ``trustzone.cmake`` marker, so the
outer tree is placed in the Secure world (``MSECURITY_MODE=SECURE``)
automatically - no extra flag is needed. Building the ``max32657_tz_hello``
target runs the whole superbuild - the nested Non-Secure build, the two Secure
links and the embed - automatically (the nested build inherits ``MAXIM_LIBRARIES``
from this environment).

.. code-block:: bash

	export MAXIM_LIBRARIES=/path/to/msdk/Libraries
	cmake --preset max32657evkit -B build \
	    -DPROJECT_DEFCONFIG=max32657_tz_hello/secure.conf
	cmake --build build --target max32657_tz_hello

Artifacts land in ``build/build/``:

* ``max32657_tz_hello.elf`` - the combined Secure + Non-Secure image.
* ``max32657_tz_hello.hex`` - Intel HEX, the flashable artifact.

.. note::

	No flat ``.bin`` is emitted. ``objcopy -O binary`` would zero-fill the
	~256 MB gap between the Non-Secure alias region (``0x01080000``) and the
	Secure alias region (``0x11000000``), producing a multi-hundred-MB file.
	The ELF and the Intel HEX both carry per-region addresses and stay small;
	flashing uses one of those.

Flash (J-Link on the MAX32657EVKIT)::

	cmake --build build --target flash

Inspecting the combined image
-----------------------------

.. code-block:: bash

	arm-none-eabi-objdump -h build/build/max32657_tz_hello.elf
	arm-none-eabi-nm      build/build/max32657_tz_hello.elf | grep IncrementCount_S

Expect ``.nonsecure_flash`` at ``0x01080000``, ``.text`` at ``0x11000000`` and
``.gnu.sgstubs`` at ``0x11078000``; ``IncrementCount_S`` resolves to a veneer in
the NSC region (``0x11078000``) whose ``__acle_se_IncrementCount_S``
implementation sits in Secure ``.text``.

Wiring - MAX32657 (MAX32657EVKIT)
---------------------------------

No external strapping is required. The console UART (UART0) is exposed on the
board's on-board USB-serial bridge; open it at **115200 8N1**. The blinking LED
is the on-board **LED0 (P0.13)**.

Layout
------

::

	projects/max32657_tz_hello/
	├── CMakeLists.txt          # minimal: calls no_os_add_maxim_trustzone_app()
	├── trustzone.cmake         # marker: build Secure world (MSECURITY_MODE=SECURE)
	├── Kconfig
	├── secure.conf             # Secure-world defconfig (console UART)
	├── nonsecure.conf          # Non-Secure-world defconfig (UART/GPIO/TIME)
	├── README.rst
	└── src/
	    ├── secure/
	    │   ├── main.c              # banner, SPC handover, IncrementCount_S gateway
	    │   ├── parameters.h        # console UART params
	    │   └── partition_max32657.h# SAU config (from the MSDK example)
	    └── nonsecure/
	        ├── main.c              # CAPI UART print + LED blink + veneer call
	        └── parameters.h        # UART + LED (P0.13) params
