#!/usr/bin/env python3
"""Fails when a dynamic relocation of ftl.elf lands outside the writable segment.

crt0 applies the relocations before anything else runs, and the Homebrew loader
maps .text and .rodata without write access: one there crashes ftl.nro on a
console before main, with no log (emulators don't enforce it).
    switch/check-relocs.py ftl.elf
"""
import re
import subprocess
import sys

elf = sys.argv[1]
readelf = "llvm-readelf"
segs = []
for line in subprocess.run([readelf, "-l", "--wide", elf], capture_output=True, text=True, check=True).stdout.splitlines():
    p = line.split()
    if p and p[0] == "LOAD":
        segs.append((int(p[2], 16), int(p[5], 16), "W" in "".join(p[6:-1])))
rel = subprocess.run([readelf, "-r", elf], capture_output=True, text=True, check=True).stdout
bad = []
for line in rel.splitlines():
    m = re.match(r"^(?:\d{4}:\s+[0-9a-f]{16}\s+)?\s*([0-9a-f]{16})\s+(\S+)", line)
    if not m:
        continue
    a = int(m.group(1), 16)
    if not any(lo <= a < lo + size and w for lo, size, w in segs):
        bad.append("%#x %s" % (a, m.group(2)))
if bad:
    print("%s: %d relocations outside the writable segment (move their input sections to "
          ".data.rel.ro in switch/switch-lld.ld):" % (elf, len(bad)), file=sys.stderr)
    print("\n".join("  " + b for b in bad[:20]), file=sys.stderr)
    sys.exit(1)
print("%s: all relocations in the writable segment" % elf)
