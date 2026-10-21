CAPI Selftest
=============
.. no-os-doxygen::

.. contents::
	:depth: 3

Overview
--------

``capi_selftest`` exercises the CAPI (Common API) platform abstraction layer
against real hardware. Each peripheral is driven through its CAPI contract only
-- never the vendor BSP directly - so the same test sources run unchanged on
every platform that provides a CAPI backend (Xilinx, STM32, ...). The tests are
integration tests, not unit tests: a GPIO edge really fires an interrupt, a SPI
transfer really loops MOSI back to MISO, an I2C initiator really addresses a
second controller wired up as a target, and so on.

The project ships three examples, selected by ``PROJECT_DEFCONFIG``:

* ``basic.conf`` - the **basic** example (``src/examples/basic``). UART only:
  it brings up the console UART and runs the test framework over it. Use it to
  confirm the most fundamental wiring - that the board boots, the UART is
  mapped correctly and characters reach your terminal - before trusting any
  richer result. If ``basic`` does not print, nothing else will.

* ``loopback.conf`` - the **capi_loopback** example
  (``src/examples/capi_loopback``). The full self-test suite: GPIO, IRQ, SPI,
  timer, I2C, UART and DMA, each a table of subtests run back-to-back with a
  summary at the end.

* ``usb_uart.conf`` - the **usb_uart** example (``src/examples/usb_uart``).
  Runs the test framework over a USB CDC-ACM virtual serial port instead of
  the console UART, exercising the CAPI USB UART backend
  (``CONFIG_CAPI_USB_UART``). Only on platforms that provide one - currently
  the MAX32690.

All examples share ``src/common`` (the test framework and ``common_data``) and
a per-platform ``src/platform/<plat>/`` directory that supplies ``main.c`` and
``parameters.h``. ``parameters.h`` is the only file that knows the board: it
maps each peripheral to a CAPI backend and publishes the capability macros the
tests gate on.

What gets tested
----------------

The loopback suite runs seven groups, in this order, each a table of subtests
driven by ``test_framework_run_cases()``. A subtest is either run, or reported
as a SKIP with a category - it never silently disappears.

Each group below lists what it proves, then which subtests always run and which
are conditional. A conditional subtest runs only when its gate macro is set;
otherwise it reports as a SKIP.

``GPIO`` - output port drives an input port over the wire, read back
	* Always: ``LOOPBACK`` (value), ``PORT_ACTIVE_LOW``, ``PIN_TOGGLE``.
	* ``PORT_TOGGLE`` - gated on ``GPIO_HAS_TOGGLE``.
	* ``PIN_LOOPBACK`` - gated on ``GPIO_HAS_PIN_LOOPBACK``.

``IRQ`` - a real GPIO edge reaches a connected callback
	Whole group gated on ``IRQ_CTRL_IDENTIFIER``.

	* ``FIRE_AND_LISTEN`` (edge delivered), ``ENABLE_GATES`` (enable/disable
	  masks it), ``RECONNECT`` (re-point the callback), ``REPEATED_EDGES``
	  (N pulses deliver N events, scaled by ``GPIO_IRQ_EVENTS_PER_EDGE``).

``SPI`` - external MOSI->MISO loopback
	* Always: ``BASIC``, ``MODES`` (all four), ``LSB_FIRST``, ``DATA``.
	* ``ASYNC_IRQ``, ``MANUAL_ISR``, ``ABORT_IRQ`` - gated on ``SPI_HAS_IRQ``.
	* ``ASYNC_DMA``, ``ABORT_DMA`` - gated on ``SPI_HAS_DMA``.

``TIMER`` - counter advances; rate cross-checks against the uptime clock
	* Always: ``BASIC`` (rate within tolerance).
	* ``COMPARE`` - gated on ``TIMER_HAS_COMPARE``.
	* ``ASYNC_IRQ``, ``IRQ_RATE`` (overflow interrupt fires at expected rate)
	  - gated on ``TIMER_HAS_IRQ``.

``I2C`` - initiator addresses a second controller wired as a target
	Without a target mapped the whole group collapses to one skipping ``BASIC``.

	* Always (target mapped): ``BASIC``, ``DATA``, ``READDRESS``, ``SUBADDR``
	  (register read), ``BUS_SPEED`` (on-wire slow/fast), ``TARGET_TX``.
	* ``MASTER_ASYNC`` - gated on ``I2C_MASTER_ASYNC`` (async initiator).

``UART`` - a second UART strapped TX->RX, never the console
	* Always: ``BASIC`` (sync echo).
	* ``ASYNC_BASIC`` - gated on ``UART_ASYNC_HAS_IRQ``.
	* ``ASYNC_SPEED`` (baud-change timing) - also needs
	  ``UART_ASYNC_HAS_LINE_CONFIG``.
	* ``ASYNC_IRQ`` (per-source IRQ masking) - also needs
	  ``UART_ASYNC_HAS_IRQ_CTL``.

``DMA`` - memory-to-memory
	Whole group gated on ``DMA_OPS``.

	* ``BASIC`` (copy), ``PATTERN`` (fill/verify).

Build
-----

Configure with a board preset and, on Xilinx, point ``HARDWARE`` at the matching
``.xsa``. The preset sets ``PLATFORM``, ``BOARD`` and ``BOARD_CONFIG_FILE``; the
defconfig picks the example; the ``.xsa`` decides which peripherals and
interrupts the BSP exposes.

The suite is designed to compile and link against **any** BSP. A peripheral the
hardware design does not expose simply drops out of the build (see
`How a test is skipped`_), so **the same sources cover all three Cora Z7-07S
builds with no source edits** - a GIC build, a cascaded AXI-INTC build and a
polled ("noirq") build.

Every build is the same three commands - configure, build, flash - changing
only the ``.xsa`` and the build directory. Pick a mode and run:

.. code-block:: bash

	MODE=gic        # gic | intc | noirq

	cmake -B build-coraz7s-$MODE --preset coraz7s \
	    -DPROJECT_DEFCONFIG=capi_selftest/loopback.conf \
	    -DHARDWARE=$(pwd)/projects/capi_selftest/coraz7s_$MODE.xsa
	cmake --build build-coraz7s-$MODE --target capi_selftest -j$(nproc)
	cmake --build build-coraz7s-$MODE --target flash       # program the board

The three modes differ only in the ``.xsa`` (see `Which IP per build`_):

* ``gic`` - interrupts through the GIC.
* ``intc`` - fabric interrupts cascaded through an AXI INTC into the GIC.
* ``noirq`` - no fabric interrupt wired; async paths skip, sync still runs.

For a first-boot smoke test, build the UART-only ``basic`` example instead of
the full suite (any ``.xsa`` works):

.. code-block:: bash

	cmake -B build-basic --preset coraz7s \
	    -DPROJECT_DEFCONFIG=capi_selftest/basic.conf \
	    -DHARDWARE=$(pwd)/projects/capi_selftest/coraz7s_gic.xsa
	cmake --build build-basic --target capi_selftest -j$(nproc)

Notes:

* One build directory per ``.xsa`` so they do not clobber each other.
* ``--target flash`` programs the attached board; only one board attaches at a
  time, so flash the build that matches it.
* Console is PS UART0 on the USB-UART - set jumper **JP3 to USB**.
* Other presets exist (``zed``, ``zc702``, ``zc706``, ``zcu102``, ``kcu105``
  ...); ``zed_{gic,intc,noirq}.xsa`` mirror this three-build split on a
  ZedBoard.

Wiring - Cora Z7-07S
--------------------

All three builds use the **same PMOD straps** - only the IP behind each slot
changes (see `Which IP per build`_). Wire the board once, reflash freely.
Console is PS UART0 on the USB-UART; set jumper **JP3 to USB**.

.. list-table::
	:header-rows: 1
	:widths: 22 30 48

	* - Loopback
	  - Strap
	  - Notes
	* - GPIO
	  - JA7 (out) to JA8 (in)
	  - One pin drives, the adjacent one reads back.
	* - SPI
	  - JB7 (MOSI) to JB8 (MISO)
	  - SCLK (JB3) and CS (JB4) are driven, not strapped - only the data pair
	    loops back.
	* - UART async
	  - JA1 (TX) to JA2 (RX)
	  - The second UART, never the console; its own TX feeds its own RX.
	* - I2C
	  - JA9-JB1 (SDA), JA10-JB2 (SCL)
	  - Two controllers on one bus: one is the initiator, the other the target.

Which IP per build
~~~~~~~~~~~~~~~~~~

The build is keyed to its interrupt mode, auto-detected from the BSP (override
with ``-DIRQ_SEL_GIC`` / ``-DIRQ_SEL_CASCADE`` / ``-DIRQ_SEL_NOIRQ``). The
straps above never move; the grid only says which silicon answers at each slot.
Read down a column for one build, across a row to see how a peripheral moves.

.. list-table::
	:header-rows: 1
	:widths: 28 24 24 24

	* - Peripheral
	  - ``gic``
	  - ``intc``
	  - ``noirq``
	* - GPIO
	  - PS
	  - PL
	  - PS
	* - SPI
	  - PL
	  - PS
	  - PL
	* - I2C initiator
	  - PL
	  - PS
	  - PL
	* - I2C target
	  - PS
	  - PL
	  - PS
	* - Timer
	  - TTC
	  - AXI
	  - SCU
	* - UART async
	  - NS550
	  - Lite
	  - PS UART

Reading the columns:

* **gic** - every async path is live through the GIC.
* **intc** - the fabric peripherals' interrupts arrive via an AXI INTC cascaded
  into the GIC. UART async is UART Lite, whose line format is fixed in the IP, so
  ``ASYNC_SPEED`` and ``ASYNC_IRQ`` skip.
* **noirq** - no fabric interrupt is wired, so the fabric peripherals' async
  subtests SKIP (sync still runs). The SCU timer and PS UART keep their own GIC
  lines, so timer and UART async still fire.

The two-way peripherals (GPIO, SPI, I2C) swap PS<->PL between ``gic`` and
``intc``; ``noirq`` mirrors ``gic``. The three-way ones (timer, UART) take a
third option under ``noirq`` - the SCU timer and the PS UART, both of which keep
a GIC line even with no fabric interrupt.

Not physically strapped
~~~~~~~~~~~~~~~~~~~~~~~~

Three groups need no jumper - their loopback is internal to the SoC or the BSP:

* **IRQ** - reuses the GPIO strap. The GPIO output pin is driven, the wired
  input pin is armed as an interrupt source; no separate wiring. The whole
  group gates on ``IRQ_CTRL_IDENTIFIER`` (the GIC, always present on Zynq), and
  each case skips at runtime (``SKIP_IRQ_ABSENT``) when the build wired no
  fabric GPIO interrupt.
* **Timer** - a free-running counter and its overflow interrupt, entirely on
  chip. Nothing external to connect; the rate check times it against the
  independent uptime clock.
* **DMA** - memory-to-memory, no peripheral and no pins. (Not mapped on Cora
  today; present on STM32.)

Wiring - STM32 (NUCLEO-F767ZI)
------------------------------

Same groups, on-chip peripherals.
brings up clocks/pins/NVIC by hand, so the platform hooks are real, not no-ops.

.. list-table::
	:header-rows: 1
	:widths: 16 26 58

	* - Group
	  - Peripheral
	  - Strap / notes
	* - GPIO
	  - PE0 -> PC0
	  - Jumper PE0 (CN10/D34) to PC0 (CN9, Arduino A1). No toggle op. Input line 0
	    routes to EXTI0 for IRQ.
	* - SPI
	  - SPI1
	  - Jumper PA7 (MOSI) to PA6 (MISO); PA5 is SCK. IRQ, no DMA.
	* - I2C
	  - I2C1 <-> I2C2
	  - Jumper PB6/PB9 (I2C1) to PB10/PB11 (I2C2). Polled (``I2C_HAS_IRQ`` 0).
	* - IRQ / Timer / DMA
	  - NVIC / TIM2 / DMA2
	  - No strap. IRQ reuses the GPIO edge; TIM2 is a 32-bit on-chip counter (1 us
	    resolution); DMA2 Stream0 is memory-to-memory, polled.

USART3 is the console only

Wiring - MAX32657 (MAX32657EVKIT)
---------------------------------

Same groups, on-chip peripherals. All async paths are delivered through the
NVIC (IRQ/FIFO); DMA is memory-to-memory only.

.. list-table::
	:header-rows: 1
	:widths: 16 26 58

	* - Group
	  - Peripheral
	  - Strap / notes
	* - GPIO
	  - P0.7 -> P0.8
	  - Jumper P0.7 (output) to P0.8 (input). Remove the on-board JP15 jumpers
	    first so nothing else drives the pair. Pin-loopback only
	    (``GPIO_HAS_PORT_LOOPBACK`` 0); P0.8 is also armed as the IRQ source.
	* - SPI
	  - SPI0
	  - Jumper P0.2 (MOSI) to P0.4 (MISO); P0.6 is SCLK, driven not strapped.
	    IRQ/FIFO delivery, no DMA.
	* - I2C
	  - I2C0
	  - Initiator only - no second controller is wired as a target, so the whole
	    I2C group collapses to one skipping ``BASIC``.
	* - IRQ / Timer / DMA
	  - NVIC / TIM0 / DMA
	  - No strap. IRQ reuses the P0.8 GPIO edge; TIM0 is a 32-bit on-chip counter
	    (1 us resolution); DMA is memory-to-memory.

UART0 is the console only; the second (async) UART is not mapped, so the whole
UART group skips.

Wiring - MAX32690 (MAX32690EVKIT)
---------------------------------

Same groups, on-chip peripherals. All async paths are delivered through the
NVIC (IRQ/FIFO); DMA is memory-to-memory only. Unlike the MAX32657, this part
has three I2C blocks, so the I2C group runs a real two-controller loopback.

.. list-table::
	:header-rows: 1
	:widths: 16 26 58

	* - Group
	  - Peripheral
	  - Strap / notes
	* - GPIO
	  - P0.9 -> P0.8
	  - Jumper P0.9 (output) to P0.8 (input). Both pins share port 0, but
	    each CAPI handle covers a single pin, so port and pin loopback both
	    run; P0.8 is also armed as the IRQ source.
	* - SPI
	  - SPI0
	  - Jumper P2.27 (MISO) to P2.28 (MOSI); P2.29 is SCK and P0.22 is SS0,
	    driven not strapped. IRQ/FIFO delivery, no DMA.
	* - I2C
	  - I2C0 <-> I2C2
	  - Jumper P2.7/P2.8 (I2C0) to P1.7/P1.8 (I2C2) - SCL to SCL, SDA to
	    SDA. I2C0 is the initiator, I2C2 the target. This is the one Maxim
	    board here that can run the paired I2C tests.
	* - UART
	  - UART0
	  - Jumper P2.11 (RX) to P2.12 (TX). P2.12 also drives LED1, so the LED
	    flickers while the UART group runs.
	* - IRQ / Timer / DMA
	  - NVIC / TMR0 / DMA
	  - No strap. IRQ reuses the P0.8 GPIO edge; TMR0 is a 32-bit on-chip
	    counter clocked from the 60 MHz PCLK; DMA is memory-to-memory.

Console is UART2 on the USB-UART (P1.9/P1.10) - note this differs from the
MAX32657EVKIT, where the console is UART0.

USB UART example
~~~~~~~~~~~~~~~~

The ``usb_uart`` example needs no strap: connect the EVKIT's high-speed USB
device connector to the host, in addition to the debug/console USB.

.. code-block:: bash

	cmake -B build-usb --preset max32690evkit \
	    -DPROJECT_DEFCONFIG=capi_selftest/usb_uart.conf
	cmake --build build-usb --target capi_selftest -j$(nproc)
	cmake --build build-usb --target flash

The board enumerates as a CDC-ACM device (VID ``0x0B6A``, PID ``0x003C``,
``/dev/ttyACM*`` on Linux). The example waits up to 60 s for a terminal to
open the port, then prints the report on it. ``RECEIVE_ASYNC`` prompts for
four characters and echoes them back; it reports a ``BOARD_STATE`` skip if
nothing is typed within 30 s. The baud rate shown by ``GET_LINE_CONFIG`` is
whatever the terminal requested - it has no effect on a USB link.

Wiring - MAX32655 (MAX32655FTHR)
--------------------------------

Same groups, on-chip peripherals, all strapped on the Feather headers. All
async paths are delivered through the NVIC (IRQ/FIFO); DMA is memory-to-memory
only. Build with the ``max32655fthr`` preset.

.. list-table::
	:header-rows: 1
	:widths: 16 26 58

	* - Group
	  - Peripheral
	  - Strap / notes
	* - GPIO
	  - P1.6 -> P1.7
	  - Jumper P1.6 (Feather D13, output) to P1.7 (Feather D12, input). Both
	    pins share port 1, but each CAPI handle covers a single pin, so port
	    and pin loopback both run; P1.7 is also armed as the IRQ source.
	* - SPI
	  - SPI1
	  - Jumper P0.21 (MOSI, J9.12) to P0.22 (MISO, J9.13); P0.23 is SCK and
	    P0.20 is SS0, driven not strapped. IRQ/FIFO delivery, no DMA. The
	    CAPI SPI identifier is the hardware block number (1 = SPI1), not the
	    MSDK index, which is reversed on this part.
	* - I2C
	  - I2C2
	  - Initiator only on the Feather SCL/SDA pins (P0.30/P0.31). I2C1 is the
	    on-board PMIC bus and I2C0 is not brought out, so the whole I2C group
	    collapses to one skipping ``BASIC``.
	* - UART
	  - UART3 (LPUART)
	  - Jumper the Feather RX (P2.6) and TX (P2.7) pins together. UART3 runs
	    from the 7.3728 MHz IBRO.
	* - IRQ / Timer / DMA
	  - NVIC / TMR0 / DMA
	  - No strap. IRQ reuses the P1.7 GPIO edge; TMR0 is a 32-bit on-chip
	    counter clocked from the 50 MHz PCLK; DMA is memory-to-memory over the
	    part's four channels.

Console is UART0 on the DAPLink USB-UART (P0.0/P0.1).

How a test is skipped
---------------------

There are three independent skip mechanisms, from coarsest to finest. Prefer
the coarsest one that fits: a peripheral that is absent from the hardware should
disappear at compile time, not fail at runtime.

1. Compile out an entire group - **do not map its ops**
	Each test group is gated on the mapping macros that ``common_data``
	publishes from ``parameters.h``. If the backend is not mapped, the group's
	data and body are ``#ifdef``-ed out and the group's entry point collapses
	to a stub that returns ``0``.

	* Peripheral groups gate on ``<MODULE>_OPS`` - ``GPIO_OUTPUT_OPS``,
	  ``SPI_OPS``, ``TIMER_OPS``, ``I2C_OPS``, ``UART_ASYNC_OPS``, ``DMA_OPS``.
	  Leave the ``*_OPS`` (and the identifier/extra) undefined in
	  ``parameters.h`` and the group is gone.
	* The IRQ group is special: it gates on ``IRQ_CTRL_IDENTIFIER``. If the
	  BSP maps no interrupt controller, leave ``IRQ_CTRL_IDENTIFIER``
	  undefined and ``test_irq.c`` compiles to a skipping stub.

	The recommended pattern in ``parameters.h`` is to auto-detect the backend
	from the BSP's ``XPAR_..._NUM_INSTANCES`` macros and only define the
	mapping when an instance exists, so an absent peripheral leaves its
	``*_OPS`` naturally undefined.

2. Skip a single subtest at compile time - **the** ``skip`` **flag**
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

	This is the mechanism that makes the polled ("noirq") build behave: the
	fabric peripherals' ``*_HAS_IRQ`` macros come out 0, so their async
	subtests SKIP instead of running against a controller that would reject
	them with ``-ENOTSUP``. **The capability macro must be derived from the
	same** ``XPAR_*_INTERRUPTS`` **macro that decides** ``use_irq``, or the two
	disagree and a SKIP turns into a spurious FAIL.

3. Skip at runtime - ``TEST_SKIP_CAT``
	When absence can only be discovered at runtime (a hook reports the board
	has no path for this test, an optional feature is off), call
	``TEST_SKIP_CAT(category, message)`` and return. The categories live in
	``test_framework.h``: ``SKIP_HW_ABSENT``, ``SKIP_IRQ_ABSENT``,
	``SKIP_BSP_ABSENT``, ``SKIP_UNSAFE``, ``SKIP_NOT_IMPLEMENTED``,
	``SKIP_BOARD_STATE``, ``SKIP_FEATURE_DISABLED``. The IRQ suite uses this:
	on a GIC-only build with no fabric GPIO interrupt the arm hook returns
	``-ENOTSUP`` and each case skips with ``SKIP_IRQ_ABSENT`` rather than
	failing.

.. note::

	The ``skip_all`` field on the **top-level** registry in ``capi_loopback.c``
	(``struct test_entry``) is **not** a hardware-capability switch. It exists
	only for personal, ad-hoc testing - flip it to ``true`` to temporarily
	omit a whole group from the default all-run while you focus on another.
	Leave every entry ``false`` in committed code; real "this hardware does not
	have it" skipping belongs in mechanisms 1-3 above, which are driven by the
	BSP, not by a hand-edited flag.

Extending the suite
-------------------

To add a new peripheral test group:

1. **Write the test** in ``src/examples/capi_loopback/tests/test_<mod>.c`` with
   a matching ``.h`` declaring ``int test_<mod>(void);``. Drive only the CAPI
   ``capi_<mod>_*`` API - no vendor calls in the test body. Put board- or
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
   out. Derive every ``*_HAS_*`` capability macro from the same interrupt macro
   that sets ``use_irq``. Consume it in ``src/common/common_data.h`` under
   ``#ifdef <MODULE>_OPS``, giving each tunable an ``#ifndef`` default there so a
   platform overrides only what it must in its ``parameters.h``.

5. **Register the group** in ``capi_loopback.c``: add ``#include
   "tests/test_<mod>.h"`` and a ``{ 0, "<MOD>", test_<mod>, false }`` row in the
   ``tests[]`` table. Keep ``skip_all`` ``false``.

Rules
-----

* CAPI only in test bodies. Anything vendor-specific goes behind a platform hook.
* Absent hardware must never fail. Compile it out (undefined ``*_OPS`` /
  ``IRQ_CTRL_IDENTIFIER``), gate the subtest, or skip at runtime with a category.
  A FAIL on a board that lacks the peripheral means the gate is wrong - fix the
  gate, not the test.
* Every subtest is visited even if an earlier one fails; the first non-zero
  return is propagated so the end-of-run summary stays complete.
* ``skip_all`` in the registry is a developer convenience, not a capability gate
  - committed code keeps it ``false``.
