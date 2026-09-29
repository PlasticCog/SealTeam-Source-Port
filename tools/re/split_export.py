#!/usr/bin/env python3
"""Split the Ghidra export (re/export/decompiled.c, disassembly.asm) into one
file per code segment under re/export/by_seg/ and dump DGROUP strings."""
import collections
import os
import re

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
EXP = os.path.join(ROOT, 're', 'export')
EXE = os.path.join(ROOT, 'Game', 'st.exe')
DG_FILE = 0x3cf0 + (0x56bf - 0x1000) * 16


def split(src_name, header_re, ext):
    text = open(os.path.join(EXP, src_name), encoding='latin1').read()
    parts = re.split(header_re, text)
    by_seg = collections.defaultdict(list)
    for i in range(1, len(parts), 3):
        by_seg[parts[i + 1]].append(parts[i] + parts[i + 2])
    for seg, chunks in by_seg.items():
        with open(os.path.join(EXP, 'by_seg', f'{seg}.{ext}'), 'w', encoding='latin1') as f:
            f.write(''.join(chunks))
    return len(by_seg)


def main():
    os.makedirs(os.path.join(EXP, 'by_seg'), exist_ok=True)
    n = split('decompiled.c', r'(\n// ==== \S+ @ ([0-9a-f]{4}):[0-9a-f]{4} \(size \d+\) ====\n)', 'c')
    split('disassembly.asm', r'(\n;==== \S+ @ ([0-9a-f]{4}):[0-9a-f]{4} ====\n)', 'asm')
    dg = open(EXE, 'rb').read()[DG_FILE:]
    with open(os.path.join(EXP, 'dgroup_strings.txt'), 'w') as f:
        for m in re.finditer(rb'[\x20-\x7e\n\r\t]{3,}\x00', dg):
            f.write('%04x  %r\n' % (m.start(), m.group()[:-1].decode('latin1')))
    print(f'split into {n} segments')


if __name__ == '__main__':
    main()
