#!/usr/bin/env python3
#
# Copyright 2026(c) Analog Devices, Inc.
#
# SPDX-License-Identifier: BSD-3-Clause
#
"""Generate a no-OS Maxim TrustZone partition_<device>.h from the MSDK template.

Part of the no-OS build system (not run by users directly). It:

  1. copies the SDK CMSIS partition template,
  2. rewrites the SAU region table to the no-OS security policy -- Non-Secure
     flash / Non-Secure SRAM / Non-Secure peripheral space + a single
     Non-Secure-Callable window -- using the region addresses taken from the
     *active* secure linker script (SDK default or generated),
  3. trims the Apache license *body* (keeping the copyright lines and the
     SPDX-License-Identifier tag), and
  4. writes the result only if it differs from the existing output, so an
     unchanged partition is left untouched (no needless recompile).

The SDK partition template hardcodes a different, incomplete SAU policy (marks
all Secure flash/SRAM as Non-Secure-Callable and omits the Non-Secure
peripheral region); this script replaces that table with the no-OS policy while
reusing the ~1270 lines of CMSIS TZ_SAU_Setup boilerplate verbatim.
"""

import argparse
import re
import sys

# Non-Secure peripheral alias is architecturally fixed on these parts (bit 28 =
# security alias: 0x4xxxxxxx Non-Secure, 0x5xxxxxxx Secure), so it does not come
# from the linker script.
PERIPH_NS_START = 0x40000000
PERIPH_NS_END = 0x4FFFFFFF


def _die(msg):
    sys.exit(f"[maxim_tz_gen_partition] {msg}")


def parse_ld_region(text, name):
    """Return (origin, inclusive_end) for a MEMORY region in a linker script."""
    m = re.search(
        rf"^\s*{re.escape(name)}\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+)"
        rf"\s*,\s*LENGTH\s*=\s*(0x[0-9A-Fa-f]+)",
        text, re.MULTILINE)
    if not m:
        _die(f"region '{name}' not found in linker script")
    origin = int(m.group(1), 16)
    length = int(m.group(2), 16)
    return origin, origin + length - 1


def set_define(text, name, value):
    """Overwrite the value of a `#define <name> <value>` (keeping any trailing
    comment). Asserts the define appears exactly once."""
    pat = re.compile(rf"(#define\s+{re.escape(name)}\s+)(\S+)(.*)$", re.MULTILINE)
    text, n = pat.subn(lambda m: f"{m.group(1)}{value}{m.group(3)}", text)
    if n != 1:
        _die(f"expected exactly one '#define {name}', found {n} "
             "(SDK partition template SAU block may have changed)")
    return text


def set_region_label(text, idx, label):
    """Update the cosmetic `Initialize SAU Region <idx> (...)` comment label.
    Matches to end of line so the template's nested parens (e.g. "(Secure SRAM
    (0-2))") are replaced wholesale rather than leaving a stray ")"."""
    pat = re.compile(rf"(Initialize SAU Region {idx} ).*$", re.MULTILINE)
    return pat.sub(rf"\g<1>({label})", text)


def trim_license(text):
    """Drop the Apache license body between the SPDX tag and the comment
    terminator, keeping the copyright lines and the SPDX tag. Idempotent."""
    lines = text.split("\n")
    spdx = next((i for i, l in enumerate(lines)
                 if "SPDX-License-Identifier:" in l), None)
    if spdx is None:
        return text
    end = next((i for i in range(spdx + 1, len(lines))
                if lines[i].strip().endswith("*/")), None)
    if end is None:
        return text
    return "\n".join(lines[:spdx + 1] + lines[end:])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--template", required=True, help="SDK partition template")
    ap.add_argument("--sld", required=True, help="active secure linker script")
    ap.add_argument("--out", required=True, help="partition_<device>.h to write")
    args = ap.parse_args()

    with open(args.template) as f:
        text = f.read()
    with open(args.sld) as f:
        ld = f.read()

    ns_flash = parse_ld_region(ld, "FLASH_NS")
    ns_sram = parse_ld_region(ld, "SRAM_NS")
    nsc = parse_ld_region(ld, "NSC_REGION")

    # no-OS SAU policy: enumerate the Non-Secure regions + the NSC window; every
    # Secure region falls through to the SAU default (Secure).
    regions = [
        (ns_flash[0], ns_flash[1], 0, "Non-Secure Flash"),
        (ns_sram[0], ns_sram[1], 0, "Non-Secure SRAM"),
        (PERIPH_NS_START, PERIPH_NS_END, 0, "Non-Secure Peripheral Space"),
        (nsc[0], nsc[1], 1, "Secure, Non-Secure Callable"),
    ]
    for i, (start, end, nsc_flag, label) in enumerate(regions):
        text = set_define(text, f"SAU_INIT_START{i}", f"0x{start:08x}")
        text = set_define(text, f"SAU_INIT_END{i}", f"0x{end:08x}")
        text = set_define(text, f"SAU_INIT_NSC{i}", str(nsc_flag))
        text = set_region_label(text, i, label)

    text = trim_license(text)

    try:
        with open(args.out) as f:
            if f.read() == text:
                return  # unchanged -> leave the file (and its mtime) alone
    except FileNotFoundError:
        pass
    with open(args.out, "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
