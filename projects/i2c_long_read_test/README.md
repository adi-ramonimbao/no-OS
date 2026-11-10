# I2C long-read bench test (Maxim, no-OS)

Hardware test for the Maxim no-OS I2C driver's handling of transfers over
256 bytes (`_max_i2c_long_read()`, the prologue path, `uint32_t`
`bytes_number`). Every transfer under test goes through
`no_os_i2c_write()` / `no_os_i2c_read()`. It is a bench project kept on a
dev branch only, not meant for main.

## Setups

| Run | Controller (no-OS, under test) | Target (MSDK, I2C2 @ 0x51) | Image(s) |
|---|---|---|---|
| A | MAX32690EVKIT I2C0 (P2.7 SDA, P2.8 SCL) | same chip, I2C2 (P1.7 SDA, P1.8 SCL) | `loopback` on the MAX32690 |
| B | MAX32655FTHR I2C2 (P0.31 SDA, P0.30 SCL) | MAX32690EVKIT I2C2 (P1.7 SDA, P1.8 SCL) | `controller` on the MAX32655, `target` on the MAX32690 |

Wiring:

- **A:** P1.7 ↔ P2.7, P1.8 ↔ P2.8, JP9/JP10 installed (as in the 2026-11-09
  MSDK bench run).
- **B:** MAX32655 P0.31 ↔ MAX32690 P1.7 (SDA), P0.30 ↔ P1.8 (SCL), GND ↔ GND.
  Remove the A loopback wires. Keep the same pull-up jumpers.
- MAX32690 errata: enabling I2C0 also drives P0.30/P0.31 and I2C2 also
  drives P0.13/P0.14. Leave those unconnected.

Consoles (115200 8N1):

| Board | Console | Port |
|---|---|---|
| MAX32690EVKIT | UART2, FTDI (`usbipd attach`) | `/dev/ttyUSB0` |
| MAX32655FTHR | UART0, DAPLink VCOM | `/dev/ttyACM1` (check with `ls /dev/ttyACM*`) |

## Build

From the repo root:

```sh
for v in "loopback max32690evkit" "target max32690evkit" "controller max32655fthr"; do
  set -- $v; B=build/i2c_long_read_test-$1-$2
  cmake -B $B --preset $2 -DPROJECT_DEFCONFIG=i2c_long_read_test/$1.conf &&
  cmake --build $B --target i2c_long_read_test -j16
done
```

The ELF is `build/i2c_long_read_test-<variant>-<board>/build/i2c_long_read_test.elf`.

For families with no bench setup, the `build_only` variant compiles and links
the controller code on I2C0 for any Maxim target, with SPI disabled. It is
never flashed. max32672 has no preset, so configure it directly:

```sh
cmake -B build/i2c_long_read_test-build_only-max32670evkit --preset max32670evkit \
  -DPROJECT_DEFCONFIG=i2c_long_read_test/build_only.conf
cmake -B build/i2c_long_read_test-build_only-max32672 -G Ninja -DPLATFORM=maxim \
  -DBOARD=max32672evkit -DTARGET=max32672 -DTARGET_NUM=32672 -DUSE_VENDOR_TOOLCHAIN=ON \
  -DBOARD_CONFIG_FILE=max32670evkit_defconfig -DPROJECT_DEFCONFIG=i2c_long_read_test/build_only.conf
```

SPI is disabled because max32672's `maxim_spi.c` doesn't build against
current MSDK (`int_fl`), which is a separate issue that's also on main.

## Flash

The two probes are told apart by serial number:

```sh
O=/home/rimbao/msdk/Tools/OpenOCD
flash() { # <serial> <target cfg> <elf>
  $O/bin/openocd -s $O/share/openocd/scripts -f interface/cmsis-dap.cfg \
    -c "adapter serial $1" -f target/$2 -c "program $3 verify reset exit"
}
M90=04161701f1aacaab00000000000000000000000097969906   # MAX32690EVKIT
M55=042317025eb6cae400000000000000000000000097969906   # MAX32655FTHR

# A
flash $M90 max32690.cfg build/i2c_long_read_test-loopback-max32690evkit/build/i2c_long_read_test.elf
# B: target first, so it is listening when the controller starts
flash $M90 max32690.cfg build/i2c_long_read_test-target-max32690evkit/build/i2c_long_read_test.elf
flash $M55 max32655.cfg build/i2c_long_read_test-controller-max32655fthr/build/i2c_long_read_test.elf
```

Open the console before flashing. Each image waits 2 s after reset before
printing anything.

## What runs

The controller runs the whole list at 100 kHz, then again at 400 kHz,
re-creating the descriptors in between. Each test passes only if all three
checks pass:

1. Every no-OS call returns what is expected.
2. Every read returns the target's pattern `lrt_pat(0..n-1)` unbroken. The
   target restarts the pattern on each read address match, so a read split
   by a repeated START fails at byte 256 (`(split at 256)`).
3. The target's record of the transaction matches. The controller fetches
   it over the bus (`0xA5` + Sr + 32-byte read), so this also proves the bus
   was left usable. It must show:
   - exactly one new transaction (none for the NACK tests);
   - the expected `rd_match` / `wr_match` (a split read gives `rd_match=2`);
   - the bytes written and their FNV-1a hash;
   - no target-side error flags.

| ID | Transfer | Path under test |
|---|---|---|
| R1, R2 | read 16, 256 | MSDK path; 256 is the largest single message it sends |
| R3–R7 | read 257, 300, 512, 600, 1024 | long read: IRXM for `n-256` bytes, then the byte count. 257 = 1 IRXM byte; 512 = hand-off at exactly 256 |
| P1, P2 | write 1 (no STOP) + read 257 / 600 | prologue + long read, i.e. the SMBus block-read shape (ADP4202) |
| P3 | write 300 (no STOP) + read 257 | prologue over 255 bytes (widened `prologue_size`), long read's TX-FIFO refill loop |
| P4, P5 | write 300 / 1 (no STOP) + read 16 | prologue through the MSDK path |
| W1, W2 | write 300, 1024 | long writes through the MSDK path |
| S1, S2 | read 257 / 600 without STOP, then read 16 / 256 | long read ending in a repeated START (`_max_i2c_wait_restart()`). Target expects one transaction, `rd_match=2` |
| S3 | read 16 without STOP, then read 257 | documented limitation. The long read must return `-EBUSY`, and a following read 16 must close the bus. Target record is not checked |
| E1, E2 | read 257 / write 1 + read 257 at absent 0x52 | NACK error path: `-EIO`, STOP sent, bus still usable |
| E3 | read 16 at absent 0x52 | same through the MSDK path, for comparison (any error) |
| R3b | read 257 | still works after the error tests |
| X1 | 200 × (write 1 + read 257) | repeatability. The target must count exactly 200 transactions |

After each speed it compares heap use before and after the tests, which
catches a leaked prologue. A failed test re-creates the controller
descriptors so later tests still run.

Pass looks like `=== done: PASS (0 failed) ===` on the controller's console.
In run B, the MAX32690 console also logs one line per transaction. During X1
the console can't keep up and reports dropped lines, which doesn't matter
because the controller checks the transaction count itself.
