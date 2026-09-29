# Library / engine segments 4511 – 5122

Scope: code segments `4511 4592 474d 481c 482e 49d2 49f2 49f8 4a1c 4a37 4db9 4dcf 4dec
4e0c 4e16 4e87 4e98 4eac 4eaf 4ec8 4edb 4ff8 50e8 … 5122`. Function names are in
`tools/re/symbols/seg_<SEG>.tsv` (the tiny EMS wrappers `50e8`–`5122` are combined in
`seg_50e8.tsv`), DGROUP globals in `tools/re/symbols/globals_libs.tsv`.

Everything below is written from the disassembly; all constants were checked in the
`.asm`/raw bytes. "Port note" paragraphs say what a faithful SDL2 re-implementation
needs to keep and what can be replaced.

## 1. Segment map

| seg | module (original .c/.asm guess) | purpose |
|---|---|---|
| 4511 | object alloc helpers | far calloc/free wrappers, entity/fx block allocators, waypoint lists, listener-pointer helper |
| 4592 | sound.c | game sound layer: driver loading, positional sound effects (6 channels), digital streaming, XMIDI music, GTL timbres, game-timer install |
| 474d | AIL.ASM (Miles AIL/2 v2.x) | Audio Interface Library: timer services (INT 08h / PIT), driver registry + dispatcher, API stubs |
| 481c | ems.c (front end) | EMS allocate / map / free / copy used by the game |
| 482e | emsmgr.c | EMS heap manager (descriptor tables in EMS, sub-page allocation) |
| 49d2 | mouse.c | INT 33h mouse: init, range, position, event callback |
| 49f2 | mouse ISR stub (+ start of fade upload) | INT 33h user-handler thunk; `pal_apply` entry |
| 49f8 | fade.asm | palette fade to black / to red, 32 Hz fade stepping |
| 4a1c | tick.asm | 256 Hz tick callback, frame limiter, screen shake |
| 4a37 | game logic | see section 14 |
| 4db9 | font4x6.c | loader/converter for the fixed 4x6 debug font |
| 4dcf | text4x6.asm | Mode-X 4x6 string/char renderer |
| 4dec | font.asm | Mode-X proportional DeluxeFont glyph renderer |
| 4e0c | vgadet.c | VGA detection |
| 4e16 | memmgr.c | near / far / VGA heaps, memory report overlay |
| 4e87 | polar.c | move an int32 3D position along (pitch, heading) |
| 4e98 | joystick.asm | game-port joystick |
| 4eac | fatal.c | fatal-error exits |
| 4eaf | fileload.c | load/save whole DOS files |
| 4ec8 | dosio.asm | INT 21h file wrappers (pascal) |
| 4edb | ealib.c + ealib.asm | EALIB archive search/loader, LZSS, PXPK picture loader, INT 24h handler |
| 4ff8 | los.asm | line-of-sight / collision queries (see section 15) |
| 50e8–5122 | ems.asm | one-function INT 67h wrappers |

Calling conventions vary per module: `4ec8`, `4eaf`, `4edb`, `49d2`, `4dcf`, `4e87` use
the Pascal convention (first argument pushed first, callee pops); `4e16` and several
`49f8` entries take their argument in AX/BX/DX (register convention); the rest is
cdecl. The `pushf; cli; … ; push cs; call <addr of an IRET byte>` idiom (used all over
`474d`, `49f2`, `2255`) is a POPF replacement: the IRET pops the pushed flags.

## 2. Memory management (4e16, 4511, 4eac)

### Heaps
Three instances of one paragraph-granular heap allocator (code in segment 2255):

| descriptor | heap | created by |
|---|---|---|
| `DS:5476` | near heap: the free part of DGROUP between `(DS:0048+15)&~15` and `DS:004C` | `heap_near_init` |
| `DS:5482` | far heap: the largest free DOS block (MCB walk, `_dos_allocmem`, retry 4 paragraphs smaller) | `heap_far_init` |
| `DS:548E` | VGA heap: off-screen video memory, flags 7 | `heap_vga_init(seg, paras)` |

Heap descriptor (12 bytes): +0 base segment, +2 anchor block segment, +4 rover (next-fit
start), +6 end segment, +8 flags (bit0 heap lives in VGA memory → reset the GC mode
register before touching headers; bit1 carve allocations from the top of a free block;
bit2 fill freed blocks with 0xAA), +9 initialised, +0xA owner tag stamped into new blocks.

Block header = one paragraph just before the returned segment: +0 type (0x0F free,
0x0B used, 0 anchor), +2 size in paragraphs including the header, +4 next, +6 prev
(circular list, segments), +8 owner tag, +0xA 0xABCD, +0xC 0xEAEA, +0xE 0x1298 (used).
Allocation = next-fit from the rover, request rounded to `(bytes+15)/16 + 1`
paragraphs; remainder < 8 paragraphs is not split. Free checks the three magics
(fatal error 0x71 / 0x72 otherwise) and merges with the following and preceding free blocks.

API (all return/accept a *segment*, offset 0): `heap_far_alloc(AX=bytes)`,
`heap_far_free(AX=seg)` (fatal 0x58 when rejected), `heap_far_realloc`, `heap_near_alloc`
(returns a DS offset), `heap_near_free(BX)`, `heap_vga_alloc/free`,
`heap_set/clear_alloc_top`, owner-tag helpers (unused). `mem_far_calloc(n)` (4511:0049)
allocates + zeroes and exits fatally on failure; `mem_far_free(farptr)` ignores NULL.

`mem_draw_report` (debug) builds `RAM  Blk:<largest far>  Far:<total far>  Near:<n>
bytes  VGA:<n>  EMS:<n>` (values ≥ 0x7FF paragraphs are printed in Kb, otherwise in
bytes), clears the strip y=192..199 and prints it with the 4x6 font at (4,193) on both
pages. Only shown when a video mode, the 4x6 font and all three heaps are up.

Fatal exits (`4eac`): restore the BIOS video mode + equipment byte saved in
`DS:5057/5058` (2255:0D92), return the far-heap DOS block, `exit(g_exit_code)`. The
message pointer in BX is not printed by this routine (the messages used by the
libraries are empty strings except "Not enough memory.").

### Entity blocks (4511)
* `ent_alloc(flags)`: 0x34-byte zeroed block; bit0 → 0x1C bytes at +6, bit1 → 0x48 at
  +0xA, bit2 → 0x26 at +0xE, bit3 → 0xD0 at +0x12 (far pointers). `ent2_alloc(flags)`:
  0x34-byte block, bit3 → 0x8E-byte order block at +0x28. `fx_alloc(flags)`: 0x50
  bytes, bit1 → 0x48 bytes at +6. Failure of any sub-allocation frees everything and
  returns NULL (after the fatal handler, which does not return).
* Waypoint list hanging off the 0x8E-byte order block: head far ptr at +0x88, cursor
  at +0x84. Node = 0x14 bytes: +0 twelve-byte record (3 × int32 position), +0xC prev,
  +0x10 next. `ent_add_waypoint` appends (first node also becomes the cursor);
  `ent_clear_waypoints` frees all and clears head/cursor.

## 3. EMS (481c, 482e, 50e8–5122)

Only used for sound samples/GTL (4592) and by segment 348e. Game-level API (`481c`):
`ems_alloc(bytes≤64K) → handle | -1`, `ems_map(handle) → far ptr`, `ems_free(handle)`,
`ems_copy(dst, src, n)`, `ems_query_free_bytes()`, `ems_init/shutdown`. A running total
of allocated bytes is kept at 53ba:2688.

Paging scheme (`482e`):
* Two EMS handles: *descriptor handle* (`F08C`) whose 16 KB pages hold descriptor
  tables, *data handle* (`F08A`) that is grown with EMS 51h as blocks need pages.
* A descriptor page holds 0x492 (1170) descriptors of 14 bytes: +0 id (= its own global
  index when used, 0xFFFF free), +2 size in bytes (for free fragments: free bytes),
  +4..+0xA up to four logical data pages, +0xC byte offset inside the first page.
  Global index = descriptor_page × 0x492 + slot; it is the block handle.
  Descriptor pages are paged in through physical page 0 (`F08E`), a second descriptor
  page through physical page 1 (`F092`).
* Blocks are ≤ 64 KB (≤ 4 pages). Whole pages are allocated per block; a partial
  last page is shared: its unused tail is recorded as a free-fragment descriptor and
  later allocations are best-fit placed into fragments (`emsm_find_slot`).
* Every public call brackets its work with EMS 47h/48h (save/restore page map).
* `ems_map` maps the block's pages into a free run of physical pages of the page frame
  (normally 4 contiguous pages at the frame segment, found with EMS 58h) using EMS 50h
  and returns frame_seg + 0x400·k : offset. **Each map call re-programs the whole frame,
  so any pointer returned by a previous map becomes invalid.**
* Error codes: 0xD0 bad handle, 0xD1 not enough EMS (needs ≥ 2 free pages / realloc
  failed), 0xD2 descriptor table full, 0xD3 zero-size request, 0xD4 no room in frame.

Port note: replace with plain heap blocks; `ems_map` just returns the block pointer.

## 4. DOS file I/O (4ec8, 4eaf) and the INT 24h handler

`4ec8` are thin INT 21h wrappers (open/create/close/delete/read/write/seek/size, DTA and
find-first helpers). `file_load(name, size*, mode, buf)`: mode 0xFF reads into the
caller's buffer, 1 allocates from the far heap, 0 from the near heap; files ≥ 64 KB
fail. `file_save(name, far buf, n)` deletes the file if the write is short.
`4x6.fnt` and the sound driver files (`*.adv`, `*.adi`) are loaded this way (loose files
in the game directory).

`dos_crit_handler_install(1)` hooks INT 24h with a handler that stores `DI+1` in
`g_io_error` (DS:5BF9) and answers "ignore"; the loaders check `g_io_error` after every
DOS call.

## 5. EALIB archives (4edb)

Archive layout: see `tools/ealib.py` ("EALIB", u16 count, count+1 entries of 18 bytes:
name[13], method, u32 offset; size = next offset − offset).

Search tables passed to `ealib_init(0xC4, 5124:0000, 41, 5124:023E, 1)`:
* Library table at 5124:0000, 41 entries × 14 bytes: name[10] (not always
  NUL-terminated inside the field; DOS truncates the extension), +0xA disk id char
  (for a never-implemented "insert disk" hook), +0xC searched flag, +0xD drive letter
  (0 = current directory). Order: main, main2, main3, us, uq, u, c, p, cs, ps, cp, uf, ca,
  pi, cf, ua, uc, uw, up, cr, cw, pa, worlds, fxmu, fxsm, msns, fxhand, str, se, lbtn,
  bull, exsm, exlg, new, port, mdl, sound, point, camp, main4, d (all `.lib`).
* Name→library map (1 entry): `cursor.pic` → library 0 (main.lib). Map patterns of the
  form `*.ext` match by case-insensitive suffix.
* Max directory entries 0xC4 (fatal error 0x45 if an archive has more).

Lookup order (`ealib_find`): (1) the currently open archive's directory, (2) the
archive named by the map, (3) every not-yet-searched archive in table order (opening
each: first the plain name, then the same name on drives A:, B:, … for each floppy
drive reported by INT 11h). Name comparison is case-insensitive. The archive that
matched stays open for the next lookup. No name occurs in two archives, so a port may
build one global index.

`ealib_load(name, size*, mode, buf)`: entry method 0 = stored, method 1 = u32 unpacked
size + LZSS; anything else fails (method 3 pictures go through `pxpk_load`). If the name
is in no archive it falls back to a plain DOS file of that name. Results must be
< 64 KB.

LZSS (`lzss_decode_to_mem`, also the PXPK variant): 4096-byte ring pre-filled with
0x20 (the first 0xFEE bytes; the tail is not cleared), write position starts at 0xFEE;
flag byte read LSB first, bit = 1 → literal byte, bit = 0 → two bytes `lo, hi`:
position = `lo | (hi & 0xF0) << 4`, length = `(hi & 0x0F) + 3`; copied bytes are
written back into the ring. Decoding stops exactly when the requested number of bytes
has been produced. Input is read through a 1 KB buffer.

## 6. PXPK pictures and bitmaps

### File (archive entry, method 3)
| off | type | meaning |
|---|---|---|
| 0 | char[4] | "PXPK" |
| 4 | u16 | depth: 0x100 = 8 bpp, 0x10 = 4 bpp (anything else rejected) |
| 6 | u16 | width in pixels |
| 8 | u16 | row stride in 16-bit words (bytes per decoded row = 2 × this) |
| 10 | u16 | height |
| 12 | u32 | unused (skipped; 0 in all files) |
| 16 | u32 | decoded byte count (must be < 64 KB) |
| 20 | … | LZSS stream |

All 23 pictures in the data are 8 bpp with stride×2 = width (320×200 screens, 16×14
cursor/wait/sight). Decoded data is row-major, one byte per pixel (4 bpp: two pixels per
byte, high nibble = left pixel). In the small sprites the pixels outside the shape are
0xFF (the shape itself comes from the matching `.msk`).

### Bitmap / surface struct (10 bytes, owned by segment 2255)
| off | type | meaning |
|---|---|---|
| +0 | u8 | flags; bit0 = "screen": rows are addressed through the row table `g_row_offsets` |
| +2 | u16 | width (pixels) |
| +4 | u16 | height |
| +6 | u16 | stride = ((width + 7) >> 3) × 2 bytes per plane row (Mode X: 4 pixels per byte address, rounded to a word) |
| +8 | u16 | segment of the pixel memory (off-screen VGA memory from the VGA heap in mode 6, far heap otherwise) |

`pxpk_load(name, bm)`: if `bm.seg == 0` a surface of the file's size is allocated
(2255:2867), otherwise the file's width/height must equal the bitmap's. During decode
`bm.stride` temporarily becomes the file row size. Each decoded row is written by the
depth-specific writer: 8 bpp → plane p (map mask 1<<p) receives pixels p, p+4, p+8, …
of the row (Mode-X unchained layout); 4 bpp → pixel values 0..15 written the same
way. After a row, the destination advances by stride/4 (8 bpp) or stride/2 (4 bpp)
bytes, or to the next entry of `g_row_offsets` if flags bit0 is set. For a port the
result is simply a W×H 8-bit indexed image.

### `.msk` masks (cursor.msk, wait.msk, sight.msk)
1 bit per pixel, rows of `ceil(width/8)` bytes (2 bytes for the 16×14 pointers),
MSB = leftmost pixel, bit = 1 → opaque. For cursor and sight the mask equals
"pixel ≠ 0xFF" exactly; wait.msk differs in 4 pixels, so draw with the mask, not with
the colour key.

## 7. Graphics state and text rendering (4dcf, 4dec, 4db9, 4e0c)

Video layer facts visible from these segments (the blitters live in segment 2255):
* `g_video_mode` (DS:4F92) is a driver index; the per-mode dispatch tables in these
  segments only have entries for index 6 = **Mode X, 320×200×256, planar**. Pixel x of
  row y is byte `g_row_offsets[y] + x/4` of plane `x & 3` in segment `g_draw_seg`
  (DS:F3F6, the page being drawn). `g_row_offsets` (DS:F266) holds 80·y.
* Clip rectangle (inclusive): `g_clip_x0/x1/y0/y1` = DS:F23C/F23E/F240/F242, set by
  2255:0FD0(x, y, w, h) (also stores w, h and the centre at F244..F24A).
* Row state table `g_row_state[200]` (DS:F0AA): per row either a solid colour or 0xFF
  ("mixed, must be copied"); the linear line-copy presenter 2255:0DA6 compares it with
  DS:F172 (what is on screen) to skip unchanged solid rows. `text4x6_draw_string` stores
  0xFF for its rows; `font_draw_char` stores the low byte of DS (AX was just reloaded
  with DS — an original bug; a port should simply mark the rows as mixed).
* Frame present (1000:1E87): clip ← (0,0,320,200), flip pages (2255:0649, which calls
  `frame_limit_wait`), `frame_limit_reset`.
* Fill rectangle 2255:25CE is called as (x, y, w, h, colour | 0xFF00) (Pascal).
* `vga_detect`: INT 10h AH=12h BL=10h, then AX=1A00h with the BIOS equipment byte
  temporarily forced to colour; VGA if the active display code is 7 or 8
  → `g_is_vga`=1, `g_vga_flags`=0x40.

### 4x6 debug font (`4x6.fnt`, 4db9/4dcf)
`font4x6_load` reads the loose file `4x6.fnt` (DeluxeFont format, see §8; 4-pixel
glyphs at bit column 4·c) and converts characters 0x15..0x7F into 6 bytes each (one
per row; the 4-pixel nibble — high nibble for even codes, low nibble for odd codes of
byte `row·64 + c/2` — mapped through `g_nibble_to_planes`, a bit reversal, so bit i =
pixel i).
* `text4x6_draw_string(str, x, y)`: x must be a multiple of 4; each character
  (0x15..0x7F only, no range check) occupies exactly 4 pixels; set bits are written in
  `g_text_fg` (DS:5074, default 15), clear bits are transparent. If `g_text_opaque`
  (DS:F424) is set, first fill (x, y, 4·strlen, 6) with `g_text_bg` (DS:5075). Rows
  y..y+5 are marked as mixed (0xFF) in `g_row_state`.
* `text4x6_draw_char(c, x, y)`: aligned x → the string routine; otherwise the same
  4 pixels are written straddling two byte columns (same result as drawing at x).

### Proportional glyphs (`font_draw_char(c, x, y)`, 4dec)
Uses the DeluxeFont header pointed to by `g_font` (DS:EF00, set by the font loader in
another segment). Width w = loc[c+1] − loc[c]; nothing is drawn if w = 0 or if any part
of the w×height box lies outside the clip rectangle (no partial clipping). Otherwise,
for each row r and column i, if bit loc[c]+i of bitmap row r is set, pixel (x+i, y+r)
is set to `g_text_fg`; clear bits are transparent. The caller advances x by w (the
glyphs include their spacing column). Rows y..y+height−1 are marked in `g_row_state`
(see the bug note above).

## 8. DeluxeFont `.fnt` format (verified on all four fonts)

| file off | size | meaning |
|---|---|---|
| 0x000 | 13 | "[DeluxeFont]" + NUL |
| 0x00D | 1 | version, must be 2 |
| 0x00E | 18 | font name, NUL terminated (rest of the field is junk) |
| 0x020 | u16 | (?) 8/12/10/8 for 4x6/memo/prop/propbold — likely line spacing; not read by these segments |
| 0x022 | u16 | bitmap bytes per row (stride) |
| 0x024 | u16 | height in rows |
| 0x026 | u16 | (?) 4/9/6/6 — likely nominal/space width; not read here |
| 0x028 | u16 | runtime slot: segment of the bitmap (file value is junk) |
| 0x02A | u16[257] | `loc[c]`: first bit column of glyph c; glyph c spans columns loc[c] .. loc[c+1]−1 |
| 0x22C | stride×height | 1-bpp bitmap strip, row-major, MSB = leftmost column |

In memory the header (0x21E bytes from file offset 0x0E) is addressed with offsets
+0x12 (?), +0x14 stride, +0x16 height, +0x18 (?), +0x1A bitmap segment, +0x1C loc[].
File size = 0x22C + stride × height (4x6: 64×6 → 940; memo 82×12 → 1540; prop 72×9 →
1204; propbold 102×8 → 1372).

| font | name field | height | glyph range | notes |
|---|---|---|---|---|
| 4x6.fnt | "4x6" | 6 | 0x00–0x7F, all 4 px (0x00–0x14 blank, 0x15–0x1F icons) | debug/HUD font |
| memo.fnt | "29704" | 12 | 0x20–0x7E | serif memo font |
| prop.fnt | "Proportional" | 9 | 0x17–0x7E | |
| propbold.fnt | "prop_bold" | 8 | 0x01–0x7F | low codes are icons: arrows, `SPACE`/`ESC`/`PgDn`/`PgUp` key caps, ©, ™ |

Renders of every glyph were produced from these rules (scratch `font_*_sheet.png`) and
are correct.

## 9. Palette and fades (49f8, 49f2:0064)

* Palette source: `g_palette` (far ptr at DS:0130), 3 bytes per colour, 6-bit DAC
  values, `g_pal_count` (DS:F262) colours (16 or 256). Uploads start at DAC index 0
  (port 3C8h) and write all colours to 3C9h, after waiting until bit 3 of port 3DAh
  equals `g_vsync_wait_state`.
* Fade level L (`g_pal_level`, signed):
  * L ≥ 0 (towards black): out = (v × (256 − L)) >> 8 for every component.
  * L < 0 (towards red), with k = −L: R' = R + (((63 − R) × k) >> 8); G' = G − ((G × k) >> 8);
    B' = B − ((B × k) >> 8). L = −256 gives pure red (63,0,0).
* Fading is enabled only with ≥ 16 colours and a VGA (`pal_fade_init`).
* `pal_set_level(L)`: target = current = L, upload immediately (used with 0x100 to black
  out instantly and 0 to restore).
* `pal_request_fade(v)` (called every frame with v = 0x700 "fade out" or 0 "normal"):
  if v ≥ 0x600, add `g_frame_dt` to the black accumulator (while < 0x100); once it
  reaches 0x100 ticks (1 s) set target = 0x100, before that target = 0. If v ≤ −0x400
  the red accumulator works the same way (target −0x100). Any other v clears both
  accumulators and sets target 0.
* `pal_fade_tick` runs from the timer every 8 ticks (32 Hz): if current ≠ target, move
  it 5 units towards the target (snap if closer than 5) and upload. A full fade
  (0 → 256) takes 52 steps ≈ 1.6 s.
* `pal_suspend_fade` / `pal_resume_fade` save/zero and restore both levels.
* The music starter `snd_music_start_once` refuses to start while `g_pal_level` ≠ 0.

## 10. Timer, frame pacing, screen shake (474d, 4a1c, 4592:0BCB)

* There is no custom keyboard or timer ISR in the game code. The only INT 08h hook is
  the AIL timer service (474d). The game registers `tick_callback` (4a1c:0028) as an
  AIL timer at **256 Hz** (`timer_install`, period 1 000 000/256 = 3906 µs).
* AIL timer service: up to 16 client timers + slot 16 = BIOS clock chain (54 925 µs).
  The PIT (channel 0, mode 3) runs at the shortest active period: divisor =
  period_µs × 10000 / 8380 (4661 for 3906 µs, ≈ 256.0 Hz); every interrupt adds the PIT
  period to each running timer's accumulator and calls it when the accumulator ≥ its
  period (subtracting the period). Drivers with a service rate get their own timer
  (driver function 103 called at that rate). Timer states: 0 free, 1 stopped, 2 running.
* `tick_callback` (per tick): `g_ticks` (u32, DS:ECB0) += 1, `g_frame_ticks` += 1,
  `g_tick_flag` = 1; every 8th tick: `shake_tick` and `pal_fade_tick`; if the control
  device is the joystick (`g_input_device` == 1), every 4th tick read the joystick
  buttons and count rising edges into `g_fire1_presses` / `g_fire2_presses`.
* Game time (segment 1000): once per frame `g_time` = `g_ticks`, `g_frame_dt` =
  `g_time − g_time_prev` (minimum 1). All game timing is in 1/256 s units.
* Frame limiter: `frame_limit_wait` (called by the page flip) busy-waits until
  `g_frame_ticks` ≥ 5 when `g_frame_limit_on` and bit 1 of DS:F23A are set;
  `frame_limit_reset` zeroes the counter after the flip → at most 256/5 = 51.2 fps.
* Screen shake: `shake_horizontal(n)` / `shake_vertical(n)` reset that axis' phase to 0
  and extend its end time to `g_time + n` if later. Every 8 ticks the horizontal phase toggles 0/1 and the
  vertical phase 0/0xFF while `g_time` < end, else they are cleared; then (mode 6 only)
  CRTC register 0Dh (start address low) = h + (v & 0x50), i.e. the displayed image
  shifts by 4 pixels horizontally and/or by one scan line (80 bytes) vertically.
  `shake_stop` clears both.

## 11. Input

* Keyboard: read through BIOS INT 16h by 19ac:2B3B (AH=1 poll, AH=0 read; returns 0 if
  no key, the ASCII code if non-zero, else scan code × 256). No INT 09h handler and no
  key-state table exist.
* Mouse (49d2/49f2): `mouse_init(3)` checks the INT 33h vector, resets the driver, sets
  mickey ratio 4:6 and double-speed threshold 48 (argument ≠ 3 would give 8:12 / 96),
  installs an all-events handler. All driver calls temporarily set the BIOS video mode
  byte 0040:0049 to 4 so the driver behaves as in a standard 320×200 CGA mode (it does
  not understand Mode X); the game programs its own ranges with doubled values (sets 2x,
  2y; divides reported values by 2). `mouse_init_stick` sets the range to
  0..211 in both axes and centres it (105,105). Event handler: `g_mouse_x/y` = cx/2,
  dx/2, `g_mouse_buttons`; when `g_input_device` ≥ 3 (mouse control),
  `g_stick_x/y` = x − 105 / y − 105 and button 1/2 rising edges increment the fire
  counters.
* Joystick (4e98, port 201h): two descriptors (8 bytes: centre x, centre y, x-bit mask,
  y-bit mask, both masks, button shift) — DS:55C4 = stick B (masks 4/8, buttons bits
  6–7), DS:55CC = stick A (masks 1/2, buttons bits 4–5). `joy_detect` times each stick
  (interrupts off, count until the one-shot bits drop); a stick is valid if both counts
  are 16..1000 and those counts become its centre. Logical stick 0 is B if B was found,
  else A. `joy_read_axes(n)` → v = (count − centre) × 128 / centre (signed), then dead
  zone: |v| ≤ 5 → 0, otherwise v moved 5 towards 0 (range ≈ ±123). `joy_read_buttons(n)`
  → bit0 fire 1, bit1 fire 2 (port bits inverted).
* Joystick calibration UI and control selection live in segments 1000/19ac.

## 12. Sound (4592 over AIL 474d)

### AIL layer (474d)
Miles Design AIL/2 (driver files say "Copyright (C) 1991,1992 Miles Design"). Driver
image: word 0 = offset of a (function number, offset) table terminated by 0xFFFF,
"Copyright…" at +2. The stubs load AX = function number and jump to the dispatcher.
Function numbers used: 100 describe, 101 detect, 102 init, 103 serve, 104 shutdown;
digital 0x78 index_VOC_block, 0x79 register_sound_buffer, 0x86 format_sound_buffer,
0x7A sound_buffer_status (3 = done), 0x7B play_VOC_file, 0x85 format_VOC_file,
0x7C VOC_playback_status, 0x7D start, 0x7E stop, 0x81 set volume (0..127); XMIDI 0x96
state_table_size, 0x97 register_sequence, 0x98 release, 0x99/0x9A timbre cache,
0x9B timbre_request, 0x9C install_timbre, 0xAA start, 0xAB stop, 0xAE status (2 =
done), 0xB1 set_relative_volume(percent, ms). Driver descriptor: +0 minimum API
version (must be ≤ 0xD3), +2 type (2 digital, 3 XMIDI), +4 data suffix (e.g. "AD",
"MT", "OPL"), +0xC/+0xE/+0x10/+0x12 default IO/IRQ/DMA/DRQ, +0x14 service rate (Hz, −1
none).

Port note: replace AIL with an SDL mixer (digital VOC samples) and an XMIDI player
(OPL emulation or General MIDI); keep the game-level behaviour below.

### Initialisation (`snd_init`)
1. Load XMIDI driver `<g_midi_driver_name>.adv` (type 3). If that fails load `pcspkr.adv`
   and disable music (`g_music_enabled` = 0). `g_fm_sfx_ok` = first load succeeded.
   Driver loading: file must not be the 4-byte stub (`nodigi.adv`/`nomidi.adv`), must
   register and describe with the wanted type, detect with the default IO/IRQ/DMA/DRQ;
   if not detected, read `<name>.adi` (u16 IO, u16 IRQ; 0xFFFF = keep) and try again.
2. Init the XMIDI driver with its defaults. If its data suffix starts with 'M'
   (Roland), `g_snd_bank` = 2.
3. If `g_digi_wanted`, load `soundrv.adv` (type 2; `setd.exe` copies the chosen digital
   driver to that name) and init it → `g_digi_ok`.
4. Load the Global Timbre Library `SAMPLE.<suffix>` (SAMPLE.AD / SAMPLE.OPL / SAMPLE.MT
   in sound.lib), define the default timbre cache, install the 256 Hz game timer.
5. Roland only: play sequence 0 of `msc03.xmi` to completion (polling every 128 ticks),
   then release it.
6. Allocate two 3600-byte digital staging buffers.

Timbres: `AIL_timbre_request` yields bank<<8|patch; the GTL directory is a list of
6-byte entries (u8 patch, u8 bank, u32 offset; bank 0xFF terminates); the timbre data
at the offset starts with its u16 size (0 treated as 2); a copy is installed with
`AIL_install_timbre`.

### Sound-effect table (in st.exe, segment 520d)
`520d:0000` = listener position (3 × int32, set by `snd_set_listener_pos`). Descriptor
of effect id (1..53) at `520d:000C + id × 0x16`:

| off | type | meaning |
|---|---|---|
| +0 | u16 | audible distance (map units, same scale as 1000:321F) |
| +2 | u16 | flags OR-ed into the channel (bit1 = positional: do not start if inaudible; FM: keep extending while playing) |
| +4 | u8 | has an FM/MIDI version (sequence id+10 of the effects bank XMI) |
| +5 | u8 | has a digital version (`sfxNN.voc`) |
| +6 | far ptr | runtime: sample data |
| +0xA | u16 | runtime: sequence number (id+10) |
| +0xC | far ptr | runtime: XMI image holding the sequence |
| +0x10 | s16 | runtime: EMS handle of the sample (−1) |
| +0x12 | u16 | priority (for the single digital voice) |
| +0x14 | u16 | runtime: sample size |

The static values (distance, flags, fm, digi, priority) for all 53 effects are in the
EXE; the port must read them from there. Examples: ids 1–9 distance 1500–3000,
priority 10; 15–17 distance 6000–9000, priority 0x1C; 44–51 distance 90, priority 0x18.

### Channels
Six channel records at 53ba:258C, 0x2A bytes each: +0 effect id, +2 flags (bit0 active,
bit1 from descriptor, bit3 no position, bit4 playing on the digital voice), +4 FM
parameter byte, +6 start time (u32 `g_time`), +0xA lifetime (s32 ticks), +0xE position
(3 × int32), +0x1C FM sequence handle (−1), +0x1E FM state table, +0x22 FM controller
table (2 bytes: FM parameter, 0x7F), +0x24 attached object (far; its +2 far pointer + 6
is a live position copied every update), +0x28 digital chunk counter.

`snd_play_sfx(id, lifetime, pos, fm_param, obj)` → channel index or −1:
1. Fail if time compression is on (DS:D7ED), sound disabled, id ∉ 1..53, or all 6
   channels busy.
2. Fill the first free channel; no `pos` → flag 8 (unpositioned, full volume).
3. Volume v = `snd_channel_volume` (below). If the descriptor has flag bit1 and v = 0 →
   fail (channel freed).
4. Digital path if the digital driver is up and the effect has a digital version, and
   the digital voice is free, or the new priority P_new > P_cur, or P_new = P_cur ≥ 0x18
   (then the current digital channel is stopped). The sample (EMS-mapped if needed) is
   streamed in 3600-byte chunks through two alternating sound buffers; volume = v.
5. Otherwise FM path if an XMIDI driver is present, the effect has an FM version and its
   XMI is known: register sequence id+10 with controller table (fm_param, 0x7F), start it,
   relative volume = min(v × 100 / 127, 90) %.
6. Otherwise fail.

`snd_channel_volume`: position refreshed from the attached object; if unpositioned →
0x7F; d = ground distance(channel pos, listener) from 1000:321F =
`(max(|dx|,|dz|) + 3·min(|dx|,|dz|)/8) >> 8` on the int32 x/z coordinates, clamped to
0x7FFF (y ignored); if d > audible distance → 0; else
max − d × max / distance (integer, clamped ≥ 0) with max = 0x50 for the digital voice,
0x7F otherwise.

`snd_update(unused, pos)` once per frame: refill free digital buffers; then for each
active channel whose effect has no digital version and that owns an FM sequence: if the
sequence is done (status 2) → stop the channel; else if flag bit1 is set and the
lifetime has expired → lifetime += 256 (keeps looping FM effects alive). Then, if
now > start + lifetime → stop; else recompute and apply the volume with
`snd_channel_volume` (always relative to the listener stored by `snd_set_listener_pos`;
the distance to `pos` (300 if NULL) is computed but only passed to an argument that is
ignored).

Loading: `snd_load_sfx_bank` loads `msc01.xmi` (Roland: `msc02.xmi`) and then every
effect 53..1: digital data from `sfxNN.voc` via EALIB (kept in EMS when `g_ems_mode`
(DS:CF60) > 3); FM effects just remember the bank XMI (Roland bank: only
priorities 12, 16, 28 get FM versions).

### Music
`snd_load_music(base, id)` loads `mscNN.xmi`, NN = base + id (or base + 1 when id = 0),
two digits, as the current XMI and pre-installs the timbres of its sequences 0..10
(id ≠ 0) or 0..15 (id = 0). `snd_music_play(n)`: if music is enabled, register sequence
n, start it, set its volume to `g_music_volume` (96 %). `snd_music_stop`,
`snd_music_done` (status 2), `snd_music_start_once(n)` (only once per load and only
when the palette is not faded). sound.lib contains MSC01–MSC15.XMI, SFX01–34/38–53.VOC,
NULL.VOC, SAMPLE.AD/.MT/.OPL.

## 13. 4e87: polar move

`pos_move_polar(d, pitch, heading, pos)` (angles in 2880 units per turn, rotation by
2255:62D0 with the sine table in segment 5327): if d ≤ 0 nothing; rotate (d, 0) by
pitch → (h, v); pos.y += v; rotate (0, h) by heading → (dx, dz); pos.x += dx; pos.z +=
dz (pos = 3 × int32: x +0, y +4, z +8). Zero angles skip their rotation. If d > 0x4000
the computation uses d >> 8 and multiplies the three deltas by 256.

## 14. 4a37: tactical group / soldier AI

Not a library: this module is the AI for every unit group (SEAL teammates, enemy
soldiers, support units, civilians, prisoners): perception, alerting, patrol waypoints,
reserve deployment, member command queues, combat/surrender/fire control. Names use the
prefix `ai_`. Four functions are dead (no near/far references): 0248, 110E, 1242, 1435.
The string references Ghidra attached to 0425 (joystick prompts etc.) are bogus — its
switch at 4a37:0570 was mis-decoded.

Callers: `ai_init`/`ai_shutdown` at mission start/end (1000:01DE); `ai_update(dt)` from
the main loop when the game clock advanced and DS:02BA (AI enabled) is set; group setup
in 365e (`ai_group_orders_from_mission`, `ai_group_orders_reset`, `ai_mind_attach`);
per-frame command executors 2dbd:6653 and 348e (`ai_member_current_command`,
`ai_group_commands_consumed`, `ai_mind_best_contact`); damage handler 1000:521C
(`ai_find_reserve_group`, `ai_group_deploy`, `ai_member_deactivate`). Times are game
ticks (1/256 s); positions 3 × int32 world units (map unit << 8); distances from
1000:321F are map units, `(max(|dx|,|dz|) + 3·min/8) >> 8` clamped to 0x7FFF. `rand(n)`
= 2255:78D6 = `(rand() & 0x7FFF) % n`.

### Data
* Group object (entries of `g_groups`, DS:12E2, NULL-terminated, [0] = the player's
  squad whose member[0] is the player): +0 NULL-terminated member far-pointer array,
  +0x20 formation, +0x22 mode, +0x24 subtype, +0x26 kind (0 SEAL squad, 1–3 support /
  vehicle groups firing at the aim point in unit-desc +0x28, 4/5 enemy soldiers, 6
  neutral civilians, 7 hostages (?)), +0x28 far ptr to the group AI block. Hostility:
  kinds 4/5 vs 0–3 and 7; kind 6 treats every other kind as hostile; a kind-2 group
  with subtype 4 is never sighted.
* Group AI block: +0 destination (12 bytes), +0xC int32 deployment timer, +0x10 think
  timer, +0x18 5 × 0x14 group command ring (+0x7C index, +0x7D pending; never written),
  +0x7E group script active, +0x7F "blocked", +0x81 look counter, +0x82 order type (0
  none, 1 patrol, 3 timed reinforcement/pursue), +0x83 behaviour (0x10 hunting, 0x20
  guard, 0x30 ?), +0x84 current waypoint, +0x88 waypoint list (4511 nodes), +0x8C patrol
  direction (1/0xFF forward, 0 backward, ping-pong).
* Member object: +2 3D object (position at +6, heading stored as degrees × 8), +6 body
  (attribute bytes +6, +8, +0xB, +0xC (skill?); +0xF bit 0x40 dead; wounds +0x12..+0x14),
  +0xA motion (+0x24 move mode 0 stop/1 walk/2 run/3 back, +0x25 posture 0 stand/1
  kneel/2 prone, +0x26 bit 0x20 captured), +0x12 mind, +0x16 loadout (+4 primary, +8
  secondary; weapon type byte +0, jam flag +8), +0x1A inventory (item type 1 = first
  aid), +0x1E group.
* Mind (≥ 0xD0 bytes): +0 ring of 5 commands (0x14 bytes each), +0x64 write index
  (newest = (idx+4) % 5), +0x65 pending, +0x66 surrendered, +0x68 group, +0x6C flags (01
  alerted, 04 engaging, 08 contact/fired, 10 suppressed — set on a hit with timer +0xCE =
  0x1400 + rand(0xA00), 20 fleeing, 40 running a script, 80 active), +0x6E/+0x70 script
  timer/step, +0x74 three contacts of 0x1E bytes (+0 in use, +2 last seen position, +0xE
  target member, +0x12 keep flag, +0x14 target object, +0x18 distance, +0x1A score,
  +0x1B real sighting, +0x1C obstruction).
* Command (0x14 bytes): +0 arg, +1 class, +2 flag, +4 position, +0x10 byte, +0x12
  heading. `class<<8 | arg`: 0x100/0x101/0x102 stop/walk/run, 0x200 turn to heading
  (+0x10 = 1 when AI issued), 0x300/0x301/0x302 stand/kneel/prone, 0x400|w fire at
  position (w bit0 primary, bit1 secondary), 0x500 reload, 0x800 look/face (?), 0x900
  surrender.
* Script sequences at 53ba:268C (3 heads, nodes of 0x1A bytes: command +0, pre-delay
  +0x14, next +0x16): 0 = stop, turn to 180, run (after 0xFF00 ticks); 1 = stop, kneel;
  2 = stop, prone. A step is issued when its delay expires, then the next step's delay is
  loaded (the end of a chain reads through NULL — treat as end). Only the first scripted
  member per group advances each tick.
* Mission group record (0x66 bytes): +0x22 order type, +0x23 behaviour, +0x24 delay
  code, +0x2A five waypoints (12 bytes; x/z used, y ignored; (0,0) unused). Deployment
  delay by code 1..9: rand(10), 10+rand(20), 30+rand(30), 60+rand(60), 120+rand(120),
  240+rand(240), 480+rand(240), 720+rand(240), 960+rand(240) (stored << 8); other codes
  overflow to `((rand(240) + 0xC0) & 0xFF) << 8` sign-extended. Initial think timer
  `slot·0x180 + 0x100`, look counter `6 − slot`, slot = (group − g_first_enemy_group) % 5.
* Other: 53ba:28BA 32 shot records (0x3C bytes, byte 0 = free), 525b:0000 int16[4]
  auto-first-aid patients, weapon type table DS:4902 (0x22 bytes: +0 class, +8 max range,
  +0x1A reload ticks).

### `ai_update(dt)`, per group (member[1] leads group 0; group 0 with only the player is skipped)
1. Reserves (kind ≠ 0): without alarm mode (`g_ai_alarm_mode`, DS:ECFA) a group whose
   leader is not active counts down its deployment timer and is deployed 720–1200 units
   from the player when it expires (`ai_group_deploy`: 3-point patrol 120–600 units
   around the target, alerted, flags 0x81, behaviour 0x10); in alarm mode inactive groups
   are skipped (deployment then comes from 1000:521C).
2. Suppression timers; kind 0: automatic first aid (a member with a first-aid item treats
   the teammate with the highest wound level ≥ 2 not already listed in 525b:0000).
3. Captives are released unless the player's squad lives and the leader is within 225.
4. Scan (`ai_group_scan`); kind 6 that sees anything runs flee script 0 (+ flag 0x20); a
   newly sighting group is alerted and fights immediately; losing sight while engaging
   clears flags 4 and 1.
5. Scripts (kind ≠ 0).
6. Noise (1000:892E) unless engaging/fleeing/fired: alert; ≤ 10 units → face it and
   rescan; else move towards it if it is closer than the destination or behaviour is
   0x10 (which also sets order 3).
7. Think timer: if > 0 decrement and stop.
8. Every 5th decision while travelling issue 0x800 (look).
9. Engaging (or kind 1–3 subtype 5, not 6) → `ai_group_combat`.
10. Movement with orders: leader standing → after a turn walk (run if alerted), else turn
    towards the destination; leader moving and within 150 (running) / 30 units → next
    destination (`ai_group_next_destination`) and stop.
11. Think timer = 0x180 (reload time for 0x500, 0x400 if blocked) + three delay-table
    lookups that, because the lookup loop never compares, always add 30 + 30 + 77; forced
    to 0 when engaging or for the player's squad.

### Perception (`ai_group_scan`)
Range 495, or `((a − 40)/5 + 165)·3` when the leader's attribute +0xC > 40; +60 if
alerted; leader attribute +6 ≥ 60 adds (v−15)·3, ≤ 45 adds (v−55)·3; minimum 90. Per
target: +60 if its attribute +8 > 75 and +300 more if > 90; 135 if the target is prone and
the observer not engaging; kind 0 in mode 1 uses the leader's longest weapon range.
Targets must be alive, not captured/surrendered and active. Rear blind spot ±25° (±20°
when alerted) around facing+180 unless closer than 90 or kind 0 mode 1. The visibility
score (19ac:746D) must beat the best contact score and the LOS obstruction (1000:3D40,
section 15) must be < 100. A sighting stores a contact (free slot or lowest score),
alerts the group, sets flag 4 (not kind 6) and behaviour 0x10. Quirk: the range and
already-tracked pre-checks use the last member iterated, not the scanning member.

### Combat (`ai_group_combat`, per member; kinds 1–3 use `ai_group_support_fire`)
Kind 6 flees. Captured/dead/surrendered/unarmed members are skipped. After a reload,
50 % chance to go prone. Kinds 4/5 within 180 of the player roll surrender: 30 (< 150
units) or 20, +5 per SEAL beyond this group's size, 10 in alarm mode with order 3, 100
under 90 units; on success rand(100) < 60 surrender (0x900), < 85 flee. Standing kind 4/5
soldiers kneel or go prone (50/50). Contact: own best, else borrowed from a groupmate
(score 25), else clear the alert and stop. Turn if more than 15° off (kind ≠ 0). Jams
cleared with probability (attr+0xC / 3)·15 + 25. No ammo at all: flee if > 210 from the
player else surrender; empty weapon in range → reload. Fire: primary if loaded;
secondary with probability 40 (65 if the target group has ≥ 3 members; support fire
30/65) when both are in range; weapon classes 10/12/13/14 get aim correction
(19ac:7BA8); issue 0x400|w at the contact and set flag 8. Support fire acts only within
2400 units of its aim point; without a contact it fires at the aim point with 50 %
probability (synthetic contact, score 50).

### Next destination when alerted (`ai_group_next_destination`)
With contacts: midpoint between leader and contact (unless engaging). Without: guard
behaviour near (≤ 50) a mission objective → objective + (50, 50); else with an intact
objective: order 1, d = rand(6)·300, new 5-point square patrol P = obj+(d,d),
(Px, Pz−d), (Px−d, Pz−d), (Px−d, Pz), P; else offset the destination randomly by 600–900
units per axis (1000:38E2). Not alerted: next ping-pong patrol waypoint.

## 15. 4ff8: line-of-sight / collision queries

A static quadtree over world objects plus a precise segment-versus-object test.

### Objects and models (as seen by this module)
Object positions are int32 world units = map units << 8. Objects live in the object heap
segment (DS:07AE): +0 near pointer to a model descriptor, +2 flags, +4 next object,
+6/+0xA/+0xE int32 x/y/z (y up), +0x12 heading (2880 units per turn; only present when
flag 2 is clear; header size 0x12 with flag 2, else 0x18). Flags used here: 0x0001
active (checked at query time only), 0x0002 compact/no heading, 0x0020 container (word
at +0x18 = child list head; apparently unused), 0x0100 member of tree B, 0x0800 has a
near pointer P right after the header — the object is skipped when the query's
include-flagged byte is 0 and `word[P+0xC] & 8`, 0x1000 member of tree A, 0x2000
arbitrary headings allowed, 0x4000 static feature stored in grid cells (objects without
it go to the dynamic list at DS:07B0).

Model descriptor (DGROUP): +8 int32 bounding radius, +0xC flags, +0x30..+0x44 int32
local xmin, xmax, ymin, ymax, zmin, zmax, +0x48 near pointer to an optional heightmap
descriptor (+0 far ptr to height bytes, +4 width, +6 depth, +8 x offset, +0xA z offset,
+0xC cell shift, +0xE height shift).

World grid (struct at DS:07A4): +4 width, +6 depth, +8 cell table offset, +0xA object
segment, +0xC dynamic list head, +0xE cell table segment. Cell (col,row) = 4 bytes at
table + (row·width + col)·4, word +0 = list head. One cell covers 2^24 world units; the
world builder uses 24000 map units, so in practice one cell and the quadtree does all
partitioning.

### Quadtree (`los_init` → trees A (mask 0x1000) and B (mask 0x0100))
Block of 5000 bytes, packed, root at offset 0. Internal node (13 bytes): byte 0, u16
midX, u16 midZ, four child offsets for (x<mid,z<mid), (x≥mid,z<mid), (x<mid,z≥mid),
(x≥mid,z≥mid). Leaf: byte 1, list of object offsets (u16), 0-terminated. Coordinates
are "hi16" = high word of the int32 world coordinate. After building, the block is
shrunk to its used size (failure fatal); overflow is fatal ("Not enough memory.").

`build_node(xmin, zmin, xmax, zmax)`:
```
xspan = xmax - xmin + 1; midX = xmin + xspan/2        (same for z)
scan cells col = max(hibyte(xmin)-1, 0) .. min(hibyte(xmax)+1, width-1), rows likewise
limit = (xspan <= 8 or zspan <= 8) ? 65535 : 10
for every object of those cells with (flags & mask) != 0:
    e = model.radius + 200
    keep if hi16(x+e) >= xmin and hi16(x-e) < xmax and hi16(z+e) >= zmin and hi16(z-e) < zmax (signed)
    append its offset; if --limit == 0: make this node internal and build the 4 children
      (ranges split at midX/midZ, children share the mid line), in the order listed above
otherwise terminate the leaf with 0
```
A leaf holds at most 9 objects unless its span is ≤ 8. Leaf lookup: from node 0 take
child `(x_hi >= midX) + 2·(z_hi >= midZ)` (unsigned compares) until a leaf.

### `los_trace(ignore, from, to, radius, hit_out, mask, include_flagged, test_ground, test_dynamic, unused)`
```
tree = mask==0x1000 ? A : mask==0x0100 ? B : fatal 0x31     (mask is OR-ed with 1 = "active")
L1 = leaf(from.x_hi, from.z_hi); L2 = leaf(to.x_hi, to.z_hi)
r = test_leaf(L1);                 if r: return r
if L2 != L1: r = test_leaf(L2);    if r: return r
if test_dynamic: r = test_list(dynamic list); if r: return r
if test_ground and ground_hit():   return 0xFFFF
return 0
```
Only the two endpoint leaves are searched (not the leaves in between), and the first
object in list order that is hit is returned (not the nearest).

Per object (leaf or list): require `(flags & mask) == mask` and object ≠ ignore; apply
the 0x0800 filter; `los_test_object`; if it hits and the object is a container, the
result of its child list is returned instead (if non-zero); else return the object.

`los_test_object`:
1. Coarse reject per axis: hi16(c − R) greater than both endpoints' hi16(p + r), or
   hi16(c + R) less than both hi16(p − r) (signed; R = model radius).
2. Local points p = (endpoint >> 8) − (object pos >> 8) per axis; any value outside int16
   → miss.
3. Heading h (0 when flag 2): h = 0 nothing; 720: (x,z) := (z,−x); 1440: (−x,−z); 2160:
   (−z,x); other values: rotate (x,z) by −h only if flag 0x2000, else ignore the heading.
   Original quirk: the 2160 case additionally gets the general rotation when flag 0x2000
   is set (the inverse transform does the same, so hit points stay consistent).
   Rotation (2255:62D0): angle normalised to [0,2880); S, C = 2·sin, 2·cos from the table
   at 5327:000A (720 words per quarter, max 16383); rnd(v) = (2v + 0x8000) >> 16;
   x' = rnd(x·C) − rnd(z·S), z' = rnd(x·S) + rnd(z·C).
4. Box = model bounds grown by the radius, each `>> 8` into int16 (min − r, max + r).
5. Cohen-Sutherland clip (outcodes x<min 4, x>max 8, y<min 2, y>max 1, z<min 0x10,
   z>max 0x20, signed): up to 15 iterations; accept if both outcodes are 0 or the
   counter runs out; reject if they share a bit; otherwise clip p1 (if its outcode is
   non-zero, else p2) against the first set plane in the order 4, 8, 1, 2, 0x10, 0x20:
   e.g. for an x plane B: t = B − p.x; p.y += t·d.y / d.x; p.z += t·d.z / d.x; p.x = B
   (d = p2 − p1; 16×16→32 multiply, 32/16 division truncating toward zero).
6. On acceptance: if the model has a heightmap, sample it at clipped p1, at the midpoint
   ((p1+p2)>>1 per axis) and at p2: col = (x >> cellshift) + xoff, row = (z >> cellshift)
   + zoff (outside → no hit for that sample), h = byte << heightshift; hit if y ≤ h, then
   p1 = (x, h, z). No sample hit → miss.
7. Undo the transform on p1 and, if the hit pointer is non-null, store
   ((int32)p1 + objpos>>8) << 8 per axis.

Ground test (in >>8 units, y < 0 = underground): if to.y ≥ 0: miss when from.y ≥ 0, else
hit at `from` (full precision). If to.y < 0: relative to base = min(from, to) in x and z,
crossing x = x1 − dx·from.y/dy (same for z), hit = ((base+x)<<8, 0, (base+z)<<8)
(from.y == to.y < 0 divides by zero in the original).

Hit output: both callers pass the address of the global hit buffer `g_los_hit`
(DS:ECFE, 3 × int32), which is also where the pointer is stored; a second write in the
same query would therefore go astray (only possible after a container hit).

### Users (segment 1000)
* 1000:314C: `los_trace(self, &self.pos, target, 0x300, &g_los_hit, 0x1000, 0, 1, 1)`,
  ground hit mapped to 0 — step function of the line-of-sight/cover sum 1000:3D40,
  which re-casts from each hit feature (ignoring it) towards the target, adding each
  feature's cover value (byte +0xC of its record in the table at DS:26F4, index
  (obj − 2)/0x18) until the total exceeds 99, nothing is hit, the same object repeats or
  the target is reached; totals below 100 count as "visible".
* 1000:318E(obj): `los_trace(obj, &obj.pos, pos + (0x600, 0, 0x600), 0x600,
  &g_los_hit, 0x100, 0, 1, 1)` — probe which flag-0x100 map feature lies under an object
  (used by 1000:3757/3E4C/3E85 and 2dbd:4429); 2dbd:1B05 moves an entity to the hit point.


## 16. Open questions

* DeluxeFont header words +0x12 and +0x18 are not read by the code in these segments
  (likely line spacing and space width); check the proportional-font loader elsewhere.
* The exact EMS fragment bookkeeping in `emsm_alloc_in_fragment` /
  `emsm_coalesce_*` is only summarised; it has no visible effect for a port.
* AIL XMIDI function numbers above 0xB2 are only named generically (unused by the game).
* 4a37: the delay-table lookup (`ai_delay_table_lookup`) never compares, so reaction
  delays are constant (+137 ticks); the port should reproduce this, not the evident intent.
* 4ff8: only the two endpoint leaves of the quadtree are searched and the first hit in
  list order is returned; keep this for faithful visibility results.
* `g_pal_track_dirty` (single-colour re-upload) is never set; `g_timbres_on_register`
  is never set.
