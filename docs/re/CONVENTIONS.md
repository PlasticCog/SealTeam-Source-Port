# Reverse-engineering conventions

Target: `st.exe` from SEAL Team (1993, Andre Gagnon / Electronic Arts), a 16-bit
real-mode DOS program compiled with Microsoft C (1990 runtime), large memory model.

## Addresses

* Ghidra loads the MZ image at segment `0x1000`. All addresses in this repo use
  **Ghidra `seg:off` form** (e.g. `19ac:016d`). Subtract `0x1000` from the
  segment to get the load-relative segment used in the MZ image.
* File offset of Ghidra address `S:O` = `0x3cf0 + (S - 0x1000) * 16 + O`
  (`0x3cf0` = MZ header size).
* **DGROUP** (near data, `DS == SS` for the whole run) is segment `0x56bf`,
  file offset `0x4a8e0`. Globals are identified by their DGROUP offset
  (`DAT_56bf_d7e4` → global `0xd7e4`). `_BSS` (zero-initialised) spans
  `0xceac`–`0xf720`.
* Segments `0x5124`–`0x53ba` are far data segments (Ghidra labels them CODE_nn).
* Each Microsoft C large-model source module gets its own code segment, so a
  Ghidra code segment roughly equals one original `.c` file.

## Decompiler quirks

* A literal `0x56bf` pushed next to an offset is the DGROUP segment of a far
  pointer: `FUN_x(0x23e, 0x56bf)` passes `DS:0x23e`.
* Far pointers are often split into two `undefined2` halves or `CONCAT22(seg, off)`.
* `FUN_18c0_*` are C runtime helpers (`tolower`, `exit`, ...).

## Output files

Function names — `tools/re/symbols/seg_<SEG>.tsv`, tab separated, one per line:

    <seg:off>	<new_name>	<one line description>

Global variables — `tools/re/symbols/globals_<SEG>.tsv`:

    <dgroup offset hex, 4 digits>	<name>	<C type>	<one line description>

Names are `snake_case`, prefixed with a short module tag (e.g. `snd_`, `gfx_`,
`ui_`). Globals start with `g_`.

Module notes — `docs/re/seg_<SEG>.md`: purpose of the module, data structures
(field offsets, sizes, meanings), algorithms, constants, state machines, file
formats read or written, and hardware access. Describe behaviour precisely
enough to reimplement it faithfully (exact constants, ordering, rounding,
random-number usage), but write it in your own words and pseudo-code. Do not
paste large blocks of decompiled code: the repository must not contain
code copied from the original program.

## Data policy

The original game files (`Game/`), the Ghidra install and everything
under `re/` (Ghidra projects, exports, extracted assets) are git-ignored and
must never be committed. The source port loads all game data, including text
and tables stored inside `st.exe`, from the user's own copy at runtime.
