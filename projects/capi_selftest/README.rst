CAPI Selftest
=============
.. no-os-doxygen::

.. contents::
	:depth: 3

Overview
--------

``capi_selftest`` exercises the CAPI (Common API) platform abstraction layer
against real hardware. Each peripheral is driven through its CAPI contract only
-- never the vendor BSP directly -- so the same test sources run unchanged on
every platform that provides a CAPI backend (Xilinx, STM32, ...). The tests are
integration tests, not unit tests: a GPIO edge really fires an interrupt, a SPI
transfer really loops MOSI back to MISO, and so on.

The project ships two examples, selected by ``PROJECT_DEFCONFIG``:

* ``basic.conf`` -- the **basic** example (``src/examples/basic``). UART only:
  it brings up the console UART and runs the test framework over it. Use it to
  confirm the most fundamental wiring -- that the board boots, the UART is
  mapped correctly and characters reach your terminal -- before trusting any
  richer result. If ``basic`` does not print, nothing else will.

* ``loopback.conf`` -- the **capi_loopback** example
  (``src/examples/capi_loopback``). The full self-test suite: GPIO, IRQ, SPI,
  timer, I2C and DMA, each a table of subtests run back-to-back with a summary
  at the end.

Both examples share ``src/common`` (the test framework and ``common_data``) and
a per-platform ``src/platform/<plat>/`` directory that supplies ``main.c`` and
``parameters.h``.

Build
-----

Configure with a board preset and point ``HARDWARE`` at the matching ``.xsa``
(Xilinx). The preset sets ``PLATFORM``, ``BOARD`` and ``BOARD_CONFIG_FILE``; the
defconfig picks the example.

.. code-block:: bash

	# Full loopback suite on a ZedBoard
	cmake -B build --preset zed \
	    -DPROJECT_DEFCONFIG=capi_selftest/loopback.conf \
	    -DHARDWARE=$(pwd)/projects/capi_selftest/zed_gic.xsa
	cmake --build build --target capi_selftest -j$(nproc)

	# UART-only sanity check
	cmake -B build-basic --preset zed \
	    -DPROJECT_DEFCONFIG=capi_selftest/basic.conf \
	    -DHARDWARE=$(pwd)/projects/capi_selftest/zed_gic.xsa

The suite is designed to compile and link against **any** BSP. A peripheral the
current hardware design does not expose simply drops out of the build (see
`How a test is skipped`_), so the same sources cover a GIC build, an AXI INTC
build and a polled ("noirq") build with no source edits.

How a test is skipped
---------------------

There are three independent skip mechanisms, from coarsest to finest. Prefer
the coarsest one that fits: a peripheral that is absent from the hardware should
disappear at compile time, not fail at runtime.

1. Compile out an entire group -- **do not map its ops**
	Each test group is gated on the mapping macros that ``common_data``
	publishes from ``parameters.h``. If the backend is not mapped, the group's
	data and body are ``#ifdef``-ed out and the group's entry point collapses
	to a stub that returns ``0``.

	* Peripheral groups gate on ``<MODULE>_OPS`` -- ``GPIO_OUTPUT_OPS``,
	  ``SPI_OPS``, ``TIMER_OPS``, ``I2C_OPS``, ``DMA_OPS``. Leave the
	  ``*_OPS`` (and the identifier/extra) undefined in ``parameters.h`` and
	  the group is gone.
	* The IRQ group is special: it gates on ``IRQ_CTRL_IDENTIFIER``. If the
	  BSP maps no interrupt controller, leave ``IRQ_CTRL_IDENTIFIER``
	  undefined and ``test_irq.c`` compiles to a skipping stub.

	The recommended pattern in ``parameters.h`` is to auto-detect the backend
	from the BSP's ``XPAR_..._NUM_INSTANCES`` macros and only define the
	mapping when an instance exists, so an absent peripheral leaves its
	``*_OPS`` naturally undefined.

2. Skip a single subtest at compile time -- **the** ``skip`` **flag**
	Every entry in a group's ``struct test_case`` table carries a ``skip``
	bool. Set it from a capability macro to drop just that subtest while the
	rest of the group still runs. For example the GPIO table gates its
	pin-level cases on ``GPIO_HAS_PIN_LOOPBACK``::

		static const struct test_case gpio_subtests[] = {
			{ "LOOPBACK", gpio_loopback, !GPIO_HAS_PORT_LOOPBACK },
		#if GPIO_HAS_PIN_LOOPBACK
			{ "PIN_LOOPBACK", gpio_pin_loopback, !GPIO_HAS_PIN_LOOPBACK },
		#endif
		};

	A ``skip == true`` case is reported as ``SKIP_FEATURE_DISABLED``; a case
	whose ``run`` pointer is ``NULL`` is reported as ``SKIP_NOT_IMPLEMENTED``.

3. Skip at runtime -- ``TEST_SKIP_CAT``
	When absence can only be discovered at runtime (a hook reports the board
	has no path for this test, an optional feature is off), call
	``TEST_SKIP_CAT(category, message)`` and return. The categories live in
	``test_framework.h``: ``SKIP_HW_ABSENT``, ``SKIP_IRQ_ABSENT``,
	``SKIP_BSP_ABSENT``, ``SKIP_UNSAFE``, ``SKIP_NOT_IMPLEMENTED``,
	``SKIP_BOARD_STATE``, ``SKIP_FEATURE_DISABLED``. The IRQ suite uses this:
	on a GIC-only build the GPIO-IRQ arm hook returns ``-ENOTSUP`` and each
	case skips with ``SKIP_IRQ_ABSENT`` rather than failing.

.. note::

	The ``skip_all`` field on the **top-level** registry in ``capi_loopback.c``
	(``struct test_entry``) is **not** a hardware-capability switch. It exists
	only for personal, ad-hoc testing -- flip it to ``true`` to temporarily
	omit a whole group from the default all-run while you focus on another.
	Leave every entry ``false`` in committed code; real "this hardware does not
	have it" skipping belongs in mechanisms 1--3 above, which are driven by the
	BSP, not by a hand-edited flag.

Extending the suite
-------------------

To add a new peripheral test group:

1. **Write the test** in ``src/examples/capi_loopback/tests/test_<mod>.c`` with
   a matching ``.h`` declaring ``int test_<mod>(void);``. Drive only the CAPI
   ``capi_<mod>_*`` API -- no vendor calls in the test body. Put board- or
   vendor-specific setup behind platform hooks (see ``main.c`` and the IRQ
   arm/ack/disarm hooks in ``common_data.h``) so the test stays
   platform-agnostic.

2. **Gate the whole file** on the mapping macro so it compiles everywhere::

	#ifdef <MODULE>_OPS
	... tests, table, test_<mod>() ...
	#else
	int test_<mod>(void) { return 0; }
	#endif

3. **Table-drive the subtests** with ``struct test_case`` and run them through
   ``test_framework_run_cases()``. Use the ``skip`` flag for optional-feature
   subtests and ``TEST_SKIP_CAT`` for runtime-discovered absence.

4. **Publish the mapping** in every ``src/platform/<plat>/parameters.h``:
   ``<MODULE>_IDENTIFIER``, ``<MODULE>_OPS``, and any ``<MODULE>_EXTRA_TYPE`` /
   ``<MODULE>_EXTRA_INIT`` the backend needs. Auto-detect the backend from the
   BSP so an absent peripheral leaves ``*_OPS`` undefined and the group drops
   out. Consume it in ``src/common/common_data.h`` under ``#ifdef <MODULE>_OPS``.

5. **Register the group** in ``capi_loopback.c``: add ``#include
   "tests/test_<mod>.h"`` and a ``{ 0, "<MOD>", test_<mod>, false }`` row in the
   ``tests[]`` table. Keep ``skip_all`` ``false``.

Rules
-----

* CAPI only in test bodies. Anything vendor-specific goes behind a platform hook.
* Absent hardware must never fail. Compile it out (undefined ``*_OPS`` /
  ``IRQ_CTRL_IDENTIFIER``), gate the subtest, or skip at runtime with a category.
* Every subtest is visited even if an earlier one fails; the first non-zero
  return is propagated so the end-of-run summary stays complete.
* ``skip_all`` in the registry is a developer convenience, not a capability gate
  -- committed code keeps it ``false``.
