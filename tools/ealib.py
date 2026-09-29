#!/usr/bin/env python3
"""EALIB archive reader for SEAL Team (1993) data files (*.lib).

Layout (little endian):
    char  magic[5]      "EALIB"
    u16   count         number of real entries
    entry dir[count+1]  the extra entry is a terminator whose offset marks EOF
entry (18 bytes):
    char  name[13]      8.3 name, NUL padded
    u8    method        0 = stored, otherwise compressed (see decompress())
    u32   offset        absolute file offset of the entry data

An entry's stored size is the next entry's offset minus its own offset.
"""
import argparse
import os
import struct
import sys

ENTRY = struct.Struct('<13sBI')


class Entry:
    def __init__(self, name, method, offset, size):
        self.name, self.method, self.offset, self.size = name, method, offset, size

    def __repr__(self):
        return f'{self.name:<13} m={self.method} off=0x{self.offset:06x} size={self.size}'


def read_dir(data):
    if data[:5] != b'EALIB':
        raise ValueError('not an EALIB archive')
    count = struct.unpack_from('<H', data, 5)[0]
    raw = [ENTRY.unpack_from(data, 7 + i * ENTRY.size) for i in range(count + 1)]
    entries = []
    for i in range(count):
        name, method, off = raw[i]
        nxt = raw[i + 1][2] or len(data)
        entries.append(Entry(name.split(b'\0')[0].decode('ascii'), method, off, nxt - off))
    return entries


def lzss(src, pos, outlen):
    """Okumura-style LZSS: 4 KB ring (pre-filled with spaces, write pos N-18),
    flag byte LSB-first (1 = literal), reference = 12-bit ring pos + 4-bit len-3."""
    N, F = 4096, 18
    ring = bytearray(b' ' * N)
    r = N - F
    out = bytearray()
    flags = nb = 0
    while len(out) < outlen and pos < len(src):
        if nb == 0:
            flags, nb = src[pos], 8
            pos += 1
        if flags & 1:
            c = src[pos]; pos += 1
            out.append(c); ring[r] = c; r = (r + 1) & (N - 1)
        else:
            lo, hi = src[pos], src[pos + 1]; pos += 2
            i, n = lo | ((hi & 0xf0) << 4), (hi & 0x0f) + 3
            for k in range(n):
                c = ring[(i + k) & (N - 1)]
                out.append(c); ring[r] = c; r = (r + 1) & (N - 1)
        flags >>= 1; nb -= 1
    return bytes(out[:outlen])


def unpack(raw, method):
    """Return the decoded bytes of an entry. Method 1 = u32 size + LZSS.
    Method 3 = PXPK picture: 20-byte header (kept) + LZSS pixel data."""
    if method == 0:
        return raw
    if method == 1:
        return lzss(raw, 4, struct.unpack_from('<I', raw, 0)[0])
    if method == 3:
        return raw[:20] + lzss(raw, 20, struct.unpack_from('<I', raw, 16)[0])
    raise ValueError(f'unknown method {method}')


def load(path):
    data = open(path, 'rb').read()
    return data, read_dir(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('libs', nargs='+')
    ap.add_argument('-x', '--extract', metavar='DIR', help='extract decoded entries into DIR/<lib>/')
    a = ap.parse_args()
    for path in a.libs:
        data, ents = load(path)
        print(f'== {path}: {len(ents)} entries')
        for e in ents:
            print('  ', e)
        if a.extract:
            out = os.path.join(a.extract, os.path.splitext(os.path.basename(path))[0])
            os.makedirs(out, exist_ok=True)
            for e in ents:
                with open(os.path.join(out, e.name), 'wb') as f:
                    f.write(unpack(data[e.offset:e.offset + e.size], e.method))


if __name__ == '__main__':
    sys.exit(main())
