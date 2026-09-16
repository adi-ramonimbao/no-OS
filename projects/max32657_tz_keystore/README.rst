MAX32657 TrustZone Keystore Demo
================================
.. no-os-doxygen::

.. contents::
	:depth: 3

Overview
--------

``max32657_tz_keystore`` is a minimal, self-contained example built on the
reusable ``no_os_add_maxim_trustzone_app()`` framework
(``cmake/maxim_trustzone.cmake``), alongside ``max32657_tz_hello``. Where the
hello demo shows the world-switch, this one puts a *protected asset* behind the
boundary: a secret key that the Non-Secure world can use but never read.

Like the hello demo it is just two source files - a Secure ``main.c`` and a
Non-Secure ``main.c`` - with no test framework or shared helpers. The Secure
world owns the key and exposes it only as an operation (a transform gateway),
never as data. The Non-Secure world drives the key through that gateway and then
tries, two ways, to reach past the boundary; both attempts are stopped by the
hardware and reported over the console.

.. note::

   The cipher is a repeating-key XOR. It is deliberately trivial and is **not**
   real cryptography - do not reuse it. It stands in for whatever real operation
   (AES, ECDSA sign, ...) a production Secure world would run behind the same
   boundary. The security feature on show is the boundary, not the cipher.

What it demonstrates
--------------------

The Non-Secure ``main()`` runs four checks in sequence and prints a
``PASS``/``FAIL`` line for each:

* ``ENCRYPT`` - ``KeystoreTransform_S()`` over the known-answer plaintext yields
  the expected ciphertext. It only matches if the Secure world holds the right
  key, so this confirms the key is present *without the Non-Secure world ever
  seeing it*. The key lives solely in ``src/secure/main.c``; no gateway returns
  it and it is never named in the shared header.
* ``ROUNDTRIP`` - transforming twice restores the plaintext (XOR is
  involutive). The key did the work on both passes but never crossed the
  boundary.
* ``REJECT_SECURE_DST`` - ``KeystoreTransform_S()`` given a pointer into Secure
  SRAM returns ``-EINVAL``: the gateway's ``cmse_check_address_range()`` guard
  finds the buffer is not Non-Secure-accessible and refuses. A hostile
  Non-Secure pointer cannot make the Secure key touch Secure memory. The address
  is only passed, never dereferenced by the Non-Secure world.
* ``REJECT_DIRECT_READ`` - the Non-Secure world dereferences a Secure address
  *itself*. The hardware raises a ``SecureFault``, which is always taken in the
  Secure world; the Secure handler (``SecureFault_Handler()`` in
  ``src/secure/main.c``) records the violation and recovers by returning from
  the offending function to its caller, so the run continues. Reaching the
  check at all proves the recovery worked; the Secure fault counter, read back
  through ``KeystoreFaultCount_S()``, confirms the trap fired.

How the fault is caught and recovered
-------------------------------------

A Non-Secure load/store to a Secure-attributed address is a security violation.
On Armv8-M it raises the ``SecureFault`` exception, taken in the Secure world.
The Secure world enables it explicitly (``SCB->SHCSR |=
SCB_SHCSR_SECUREFAULTENA_Msk``) so it does not escalate to a HardFault.

``SecureFault_Handler()`` recovers gracefully instead of hanging:

#. It locates the stacked Non-Secure exception frame on the active Non-Secure
   stack (``PSP_NS`` or ``MSP_NS``, decoded from ``CONTROL_NS``).
#. It clears the sticky ``SFSR`` status bits (write-1-to-clear).
#. It rewrites the stacked return ``PC`` to the stacked ``LR``, so the exception
   return resumes as if the faulting Non-Secure function had simply returned to
   its caller. The offending access never completes and no Secure data is
   exposed.

The provoking function (``provoke_secure_read()``) is written ``naked`` so it
has no prologue/epilogue: the faulting load is its whole body, which keeps the
Non-Secure stack balanced for that "return from the function" recovery. This is
a demo convenience so the run can continue; a production Secure world would more
likely log the violation and reset.

Expected console output (115200 8N1)::

	**** MAX32657 TrustZone keystore demo (Secure world) ****
	Secret key held Secure; Non-Secure world drives it via gateways.

	Non-Secure world: driving the Secure keystore.
	[KEYSTORE] ENCRYPT            PASS
	[KEYSTORE] ROUNDTRIP          PASS
	[KEYSTORE] REJECT_SECURE_DST  PASS
	[KEYSTORE] REJECT_DIRECT_READ PASS

	All keystore checks passed.

Structure
---------

The Secure/Non-Secure split, the superbuild and the combined-HEX post-build all
live in the framework; this project only declares its two sources::

	src/tz_gateways.h                shared Secure <-> Non-Secure contract + KAT
	src/secure/main.c                key, gateways, SecureFault handler, handover
	src/secure/parameters.h          Secure console-UART parameters
	src/secure/partition_max32657.h  SAU/IDAU partition (default split)
	src/nonsecure/main.c             Non-Secure demo (the four checks)
	src/nonsecure/parameters.h       Non-Secure console-UART parameters

Build
-----

The SDK location comes from the environment, not a ``-D`` cache variable::

	export MAXIM_LIBRARIES=/path/to/msdk/Libraries
	cmake --preset max32657evkit -B build \
	      -DPROJECT_DEFCONFIG=max32657_tz_keystore/secure.conf
	cmake --build build --target max32657_tz_keystore

Artifacts land in ``build/build/``: ``max32657_tz_keystore.elf`` (combined
Secure + Non-Secure image) and ``max32657_tz_keystore.hex`` (the flashable
Intel HEX). Flash with ``cmake --build build --target flash``.
