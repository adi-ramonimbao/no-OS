MAX32657 TrustZone CAPI Self-Test
=================================
.. no-os-doxygen::

.. contents::
	:depth: 3

Overview
--------

``max32657_tz_selftest`` is a second project built on the reusable
``no_os_add_maxim_trustzone_app()`` framework (``cmake/maxim/maxim_trustzone.cmake``),
alongside ``max32657_tz_hello``. Where the hello demo shows the world-switch,
this project *tests* it: the Secure world hands the console UART to the
Non-Secure world and branches into it, and the Non-Secure world runs the shared
``capi_selftest`` framework over two groups.

Because it exists and builds with only its own sources, it also demonstrates
that the TrustZone framework is not coupled to the hello demo.

What it tests
-------------

**TRUSTZONE** (``src/nonsecure/tests/test_trustzone.c``) - the security
gateway, exercised across the boundary from the Non-Secure world:

* ``SECURE_RETURN`` - ``GetSecureMagic_S()`` returns a Secure-owned constant,
  proving the plain Secure -> Non-Secure return path.
* ``INCREMENT`` - ``IncrementCount_S()`` with a valid Non-Secure pointer
  advances a counter in Non-Secure memory.
* ``REJECT_NULL`` - ``IncrementCount_S(NULL)`` returns ``-EINVAL``: the
  gateway's ``cmse_check_pointed_object()`` guard rejects it.
* ``REJECT_SECURE_PTR`` - ``IncrementCount_S()`` given a pointer into Secure
  SRAM returns ``-EINVAL``: the object is not accessible to a Non-Secure caller,
  so the CMSE check returns NULL. This is the isolation guarantee - a hostile
  Non-Secure pointer cannot make Secure code touch Secure memory. The address
  is only passed, never dereferenced by the Non-Secure world.

**DMA** (reused verbatim from ``projects/capi_selftest``) - memory-to-memory
transfers. From the Non-Secure world the Maxim CAPI DMA backend selects the
hardwired-Non-Secure ``DMA0`` instance automatically
(``CONFIG_TRUSTED_EXECUTION_SECURE == 0``), so the run also confirms the
world-correct DMA engine is picked. All transfers stay inside SRAM.

No external wiring is required. The remaining ``capi_selftest`` groups (GPIO,
SPI, I2C, UART async) need board loopback straps and are intentionally left out;
adding a group is a matter of handing over its peripheral in the Secure world
(and, for an IRQ-driven group, routing its NVIC line with
``NVIC_SetTargetState()``) and adding it to the runner table.

Expected console output (115200 8N1)::

	**** MAX32657 TrustZone CAPI self-test (Secure world) ****
	Handing peripherals to the Non-Secure world; tests run there.
	... framework run header ...
	[TRUSTZONE] ... PASS ...
	[DMA] ... PASS ...
	... framework summary (passed/failed/skipped) ...

Structure
---------

The Secure/Non-Secure split, the superbuild and the combined-HEX post-build all
live in the framework; this project only declares its sources. The framework and
the reused ``capi_selftest`` files are compiled directly from their source trees
(no duplication).

::

	projects/max32657_tz_selftest/
	├── CMakeLists.txt          # calls no_os_add_maxim_trustzone_app()
	├── trustzone.cmake         # marker: build Secure world (MSECURITY_MODE=SECURE)
	├── Kconfig
	├── secure.conf             # Secure-world defconfig (console UART)
	├── nonsecure.conf          # Non-Secure-world defconfig (UART/TIME/ALLOC/DMA)
	├── README.rst
	└── src/
	    ├── tz_gateways.h           # shared gateway contract (both worlds)
	    ├── secure/
	    │   ├── main.c              # banner, SPC handover, gateway definitions
	    │   ├── parameters.h        # console UART params
	    │   └── partition_max32657.h# SAU config (from the MSDK example)
	    └── nonsecure/
	        ├── main.c              # framework runner (TRUSTZONE + DMA groups)
	        ├── parameters.h        # console UART + DMA params
	        └── tests/
	            ├── test_trustzone.c# secure-gateway tests
	            └── test_trustzone.h

Build
-----

.. code-block:: bash

	export MAXIM_LIBRARIES=/path/to/msdk/Libraries
	cmake --preset max32657evkit -B build \
	    -DPROJECT_DEFCONFIG=max32657_tz_selftest/secure.conf
	cmake --build build --target max32657_tz_selftest

Artifacts land in ``build/build/``: ``max32657_tz_selftest.elf`` (combined
Secure + Non-Secure image) and ``max32657_tz_selftest.hex`` (the flashable
Intel HEX). Flash with ``cmake --build build --target flash``.
