# SEAL Team data formats

All values are little endian. Offsets in hex. This file covers the formats
verified so far; module notes in `docs/re/` describe the rest as they are
reverse engineered.

## EALIB archive (`*.lib`)

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 5 | magic `EALIB` |
| 5 | 2 | entry count *n* |
| 7 | 18 × (*n*+1) | directory |

Directory entry (18 bytes):

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 13 | 8.3 file name, NUL padded |
| d | 1 | storage method |
| e | 4 | absolute offset of the entry data |

The extra last entry is a terminator; its offset is the end of the data, so
an entry's stored size is `next.offset - offset`.

Storage methods:

| Method | Used for | Layout |
|-------:|----------|--------|
| 0 | most sprites, palettes, masks, sounds | raw bytes |
| 1 | text (`.S`), worlds (`.W`/`.WD`), missions (`.MCI`/`.MTM`), personnel (`.SE`), music/sfx | u32 unpacked size, then LZSS |
| 3 | pictures (`.pic`) | 20-byte PXPK header (uncompressed), then LZSS pixel data |

The game opens 41 archives at start-up; their names are stored in `st.exe`
at far address `5124:0000` as 14-byte records (10-character name, not
always NUL terminated, plus a 32-bit runtime field).

## LZSS

Classic Okumura LZSS, as decoded by the game at `4edb:0dec`:

* 4096-byte ring buffer, bytes `0x000`–`0xFED` pre-filled with `0x20` (space),
  writing starts at `0xFEE`.
* A flag byte precedes each group of 8 items and is consumed LSB first:
  `1` = literal byte, `0` = back reference.
* A back reference is two bytes `lo, hi`: ring position
  `lo | (hi & 0xF0) << 4`, length `(hi & 0x0F) + 3`.
* Decoding stops once the unpacked size is reached.

## PXPK picture

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 4 | magic `PXPK` |
| 4 | 2 | colour depth: `0x100` = 256 colours (`0x10` = 16 colours) |
| 6 | 2 | width in pixels |
| 8 | 2 | row stride in 16-bit words (stride in bytes = 2 × value) |
| a | 2 | height |
| c | 4 | reserved (0) |
| 10 | 4 | unpacked pixel data size (stride × height) |
| 14 | … | LZSS-compressed 8-bit pixels, row major |

Full-screen pictures are 320 × 200. The mouse cursor pictures (`cursor`,
`wait`, `sight`) are 16 × 14 and have a separate `.msk` mask.

## Palette (`*.PAL`)

768 bytes: 256 × (R, G, B), 6-bit VGA DAC values (0–63). `ST.PAL` is the
title/default palette; `C0`–`C5.PAL` belong to the campaign pictures
`CM0`–`CM5.PIC`.

## Executable-resident data

`st.exe` is a Microsoft C large-model MZ executable (header 0x3CF0 bytes).
DGROUP is at file offset `0x4A8E0`; see `docs/re/CONVENTIONS.md` for address
conversion. The port reads text and tables from the user's `st.exe` rather
than duplicating them.
