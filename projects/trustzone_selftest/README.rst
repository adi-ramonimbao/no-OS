MAX32657 TrustZone CAPI Self-Test
=================================
.. no-os-doxygen::

.. contents::
	:depth: 3

Overview
--------

``trustzone_selftest`` is a second project built on the reusable
``no_os_add_maxim_trustzone_app()`` framework (``cmake/maxim/maxim_trustzone.cmake``),
alongside ``max32657_tz_hello``. Where the hello demo shows the world-switch,
this project *tests* it: the Secure world hands its peripherals (and their
interrupt lines) to the Non-Secure world and branches into it, and the
Non-Secure world runs the shared ``capi_selftest`` framework.

It is a **superset of the maxim** ``capi_selftest``: the Non-Secure world runs
the full ``capi_loopback`` group set (GPIO, IRQ, SPI, TIMER, I2C, UART, DMA),
reused verbatim from ``projects/capi_selftest`` with the same maxim
``parameters.h``, **plus** two TrustZone-specific groups (TRUSTZONE and
DMA_INSTANCE). Because it exists and builds with only its own runner + gateway
sources, it also demonstrates that the TrustZone framework is not coupled to the
hello demo.

What it tests
-------------

**TRUSTZONE** (``src/examples/selftest/tests/test_trustzone.c``) - the security
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

**DMA_INSTANCE** (``src/platform/maxim/nonsecure/tests/test_dma_instance.c``) - a display group
run just before DMA that prints both controller bases and the one this world
selected, confirming the Non-Secure world drives ``DMA0_NS`` (not the Secure
``DMA1_S``).

**GPIO / IRQ / SPI / TIMER / I2C / UART / DMA** (reused verbatim from
``projects/capi_selftest`` with the maxim ``parameters.h``) - the full
``capi_loopback`` suite, run in the Non-Secure world exactly as the standalone
maxim ``capi_selftest`` runs it. From the Non-Secure world the CAPI DMA backend
selects the hardwired-Non-Secure ``DMA0`` instance automatically
(``CONFIG_TRUSTED_EXECUTION_SECURE == 0``); the MAX32657 "I2C" group drives the
I3C block.

Wiring is identical to the maxim ``capi_selftest`` loopback: the GPIO, SPI and
I2C groups need the board loopback straps documented in
``projects/capi_selftest`` (e.g. GPIO ``P0.7``↔``P0.8`` with JP15 removed, SPI
``P0.4``↔``P0.2``). The Secure world hands over the console UART, GPIO0 (all
pins), SPI, I3C and TMR0 - and targets their NVIC lines to Non-Secure with
``NVIC_SetTargetState()`` - before the tests run; a group whose peripheral or
interrupt is not delegated would SecureFault at runtime.

Expected console output (115200 8N1)::

	**** MAX32657 TrustZone CAPI self-test (Secure world) ****
	Handing peripherals to the Non-Secure world; tests run there.
	... framework run header ...
	[TRUSTZONE] ... PASS ...
	[DMA_INSTANCE] ... DMA0_NS (Non-Secure) ...
	[GPIO] ... [IRQ] ... [SPI] ... [TIMER] ... [I2C] ... [UART] ... [DMA] ...
	... framework summary (passed/failed/skipped) ...

Structure
---------

The Secure/Non-Secure split, the superbuild and the combined-HEX post-build all
live in the framework; this project only declares its sources. The framework and
the reused ``capi_selftest`` files are compiled directly from their source trees
(no duplication).

::

	projects/trustzone_selftest/
	├── CMakeLists.txt          # calls no_os_add_maxim_trustzone_app()
	├── trustzone.cmake         # marker: build Secure world (MSECURITY_MODE=SECURE)
	├── Kconfig
	├── secure.conf             # Secure-world defconfig (console UART)
	├── nonsecure.conf          # Non-Secure-world defconfig (full CAPI class set)
	├── README.rst
	└── src/
	    ├── tz_gateways.h           # shared gateway contract (both worlds)
	    ├── secure/
	    │   ├── main.c              # banner, SPC + NVIC handover, gateway defs
	    │   ├── parameters.h        # console UART params
	    │   └── partition_max32657.h# SAU config (from the MSDK example)
	    └── nonsecure/
	        ├── main.c              # runner (full loopback set + TZ groups) + GPIO-IRQ hooks
	        └── tests/
	            ├── test_trustzone.c    # secure-gateway tests
	            ├── test_trustzone.h
	            ├── test_dma_instance.c # DMA controller report
	            └── test_dma_instance.h

	The GPIO/IRQ/SPI/TIMER/I2C/UART/DMA groups, the framework, common_data and
	the maxim parameters.h are compiled from projects/capi_selftest (no copies).

Build
-----

.. code-block:: bash

	export MAXIM_LIBRARIES=/path/to/msdk/Libraries
	cmake --preset max32657evkit -B build \
	    -DPROJECT_DEFCONFIG=trustzone_selftest/secure.conf
	cmake --build build --target trustzone_selftest

Artifacts land in ``build/build/``: ``trustzone_selftest.elf`` (combined
Secure + Non-Secure image) and ``trustzone_selftest.hex`` (the flashable
Intel HEX). Flash with ``cmake --build build --target flash``.
