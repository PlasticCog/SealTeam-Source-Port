#!/usr/bin/env python3
"""Summarise the Ghidra export (re/export) per function: callees, DGROUP strings
referenced, and globals touched. Output: re/export/summary.txt

DGROUP (DS) is Ghidra segment 0x56bf == file offset 0x4a8e0 in st.exe.
"""
import re, sys, os, collections
ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
EXP = os.path.join(ROOT, 're', 'export')
EXE = os.path.join(ROOT, 'SealTeam-DOS', 'st.exe')
DG_FILE = 0x3cf0 + (0x56bf - 0x1000) * 16

exe = open(EXE, 'rb').read()
dg = exe[DG_FILE:]

def cstr(off):
    if off >= len(dg): return None
    end = dg.find(b'\0', off)
    if end < 0 or end - off < 3 or end - off > 80: return None
    s = dg[off:end]
    if all(32 <= c < 127 for c in s):
        return s.decode('ascii')
    return None

src = open(os.path.join(EXP, 'decompiled.c'), encoding='latin1').read()
parts = re.split(r'\n// ==== (\S+) @ (\S+) \(size (\d+)\) ====\n', src)
out = open(os.path.join(EXP, 'summary.txt'), 'w')
for i in range(1, len(parts), 4):
    name, addr, size, body = parts[i], parts[i+1], parts[i+2], parts[i+3]
    callees = sorted(set(re.findall(r'\b(FUN_[0-9a-f]{4}_[0-9a-f]{4})\s*\(', body)) - {name})
    refs = set(int(x, 16) for x in re.findall(r'DAT_56bf_([0-9a-f]{4})', body))
    refs |= set(int(x, 16) for x in re.findall(r'\b0x([0-9a-f]{3,4})\b', body))
    strs = sorted(set(s for s in (cstr(r) for r in refs) if s))
    out.write(f'{addr} {name} size={size}\n')
    if callees: out.write('   calls: ' + ' '.join(callees) + '\n')
    if strs: out.write('   strings: ' + ' | '.join(strs) + '\n')
print('ok')
