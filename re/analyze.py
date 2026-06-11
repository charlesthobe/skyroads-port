#!/usr/bin/env python3
"""Static analysis of the SkyRoads load image.

Code segment: image 0x0000..0x66E0 (CS=0)
Data segment: image 0x66E0..      (DS=0x66E)
Entry: 0000:60D0 (Borland C0 startup)
"""
import re
import struct
import sys
from collections import defaultdict

IMG = open('re/image.bin', 'rb').read()
DS_BASE = 0x66E0
CODE_END = DS_BASE  # code occupies [0, DS_BASE)


def load_disasm(path='re/full.asm'):
    insns = []  # (addr, bytes, text)
    rx = re.compile(r'^([0-9A-F]{8})\s+([0-9A-F]+)\s+(.*)$')
    for line in open(path):
        m = rx.match(line)
        if m:
            addr = int(m.group(1), 16)
            insns.append((addr, m.group(2), m.group(3).strip()))
    return insns


def find_functions(insns):
    """Function starts: targets of `call` + addresses with enter/push-bp prologue."""
    starts = set()
    call_targets = defaultdict(int)
    for addr, _b, text in insns:
        if addr >= CODE_END:
            break
        m = re.match(r'call (?:word )?0x([0-9a-f]+)$', text)
        if m:
            t = int(m.group(1), 16)
            if t < CODE_END:
                call_targets[t] += 1
                starts.add(t)
    return sorted(starts), call_targets


def strings_in_data(min_len=4):
    out = []
    i = DS_BASE
    while i < len(IMG):
        j = i
        while j < len(IMG) and 32 <= IMG[j] < 127:
            j += 1
        if j - i >= min_len:
            out.append((i - DS_BASE, IMG[i:j].decode('ascii')))
        i = j + 1
    return out


def find_imm_refs(insns, value):
    """Find instructions embedding an immediate equal to a DS offset."""
    hits = []
    pat = re.compile(r'0x%x\b' % value)
    for addr, _b, text in insns:
        if addr >= CODE_END:
            break
        if pat.search(text):
            hits.append((addr, text))
    return hits


def main():
    insns = load_disasm()
    starts, call_targets = find_functions(insns)
    print(f"{len(starts)} called functions found")

    # Most-called functions (likely RTL helpers)
    top = sorted(call_targets.items(), key=lambda kv: -kv[1])[:25]
    print("\n== most-called ==")
    for t, n in top:
        print(f"  0x{t:04x}: {n} calls")

    print("\n== data-segment strings ==")
    for off, s in strings_in_data():
        print(f"  ds:{off:04x}  {s!r}")

    # Which code references each filename string?
    print("\n== refs to filename strings ==")
    for off, s in strings_in_data():
        if any(k in s.lower() for k in ('.lzs', '.snd', '.dat', '.rec', '.cfg')):
            for addr, text in find_imm_refs(insns, off):
                print(f"  ds:{off:04x} {s!r:24} <- 0x{addr:04x}: {text}")

    # int usage map
    print("\n== interrupt usage ==")
    ints = defaultdict(list)
    for addr, _b, text in insns:
        if addr >= CODE_END:
            break
        m = re.match(r'int (?:byte )?0x([0-9a-f]+)', text)
        if m:
            ints[int(m.group(1), 16)].append(addr)
    for num in sorted(ints):
        addrs = ints[num]
        print(f"  int {num:02x}h: {len(addrs)} sites  e.g. {['0x%04x' % a for a in addrs[:8]]}")


if __name__ == '__main__':
    main()
