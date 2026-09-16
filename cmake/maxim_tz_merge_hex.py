#!/usr/bin/env python3
# =============================================================================
# maxim_tz_merge_hex.py - merge two or more Intel HEX files into one.
#
# Pure Python, no dependencies. Used by no_os_add_maxim_trustzone_nonsecure_app()
# to combine a separately built Secure image with a freshly built Non-Secure
# image into a single flashable HEX.
#
# It concatenates the record streams of the inputs, dropping every input's
# End-Of-File record (:00000001FF) and appending a single EOF at the very end.
# objcopy emits an Extended Linear Address (type 04) record at the start of each
# file and whenever the upper 16 address bits change, so each region keeps its
# own absolute addresses after concatenation. Inputs are expected to occupy
# disjoint address ranges (Secure vs Non-Secure flash); overlap is reported.
#
#   usage: maxim_tz_merge_hex.py OUT IN1 IN2 [IN3 ...]
# =============================================================================
import sys


def _record_ranges(path):
    """Yield (upper<<16 | addr, length) data spans in a HEX file, for overlap
    detection. Tracks the current Extended Linear Address (type 04)."""
    upper = 0
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            count = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            rectype = line[7:9]
            if rectype == "04":
                upper = int(line[9:9 + 4], 16)
            elif rectype == "00":
                yield ((upper << 16) | addr, count)


def _overlaps(a, b):
    for (a0, an) in _record_ranges(a):
        a1 = a0 + an
        for (b0, bn) in _record_ranges(b):
            b1 = b0 + bn
            if a0 < b1 and b0 < a1:
                return (a0, a1, b0, b1)
    return None


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(
            "usage: maxim_tz_merge_hex.py OUT IN1 IN2 [IN3 ...]\n")
        return 2

    out_path = argv[1]
    in_paths = argv[2:]

    # Cheap pairwise overlap check so a mismatched Secure/Non-Secure memory map
    # is reported here rather than silently producing a corrupt image.
    for i in range(len(in_paths)):
        for j in range(i + 1, len(in_paths)):
            hit = _overlaps(in_paths[i], in_paths[j])
            if hit:
                sys.stderr.write(
                    "maxim_tz_merge_hex: address overlap between "
                    "'%s' and '%s' (0x%08x-0x%08x vs 0x%08x-0x%08x); the "
                    "Secure and Non-Secure memory maps disagree.\n" % (
                        in_paths[i], in_paths[j], hit[0], hit[1], hit[2],
                        hit[3]))
                return 1

    out_lines = []
    for path in in_paths:
        with open(path, "r") as f:
            for line in f:
                line = line.strip()
                if not line.startswith(":"):
                    continue
                if line[7:9] == "01":  # EOF: drop; one is appended at the end
                    continue
                out_lines.append(line)
    out_lines.append(":00000001FF")

    with open(out_path, "w") as f:
        f.write("\n".join(out_lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
