# Segment 365e, upper half (365e:8000–eb3a) — campaign front-end screens, fonts, mission scoring

Status: every function in 365e:8000–eb3a is analysed (104 Ghidra functions plus 14 entry points Ghidra
missed, listed in `tools/re/symbols/seg_365e_b.tsv` with "(missed by Ghidra)"). Several Ghidra
"functions" (365e:a54c, a670, a695, a6a6, a6bc, a787, a7a7, a7b2) are only case blocks of
`cmpscr_handle_input` that share its stack frame; 365e:9998 and 365e:e967 are badly decompiled and were
read from the disassembly. The Ghidra output also mislabels many `push cs; call near` calls in this half
(for example 9aa6 appears to call 4edb:0ef0/0f2b; it really calls 97db/96c0/96fb) — trust the listing.

**What this half is not:** the task hints (worlds.lib, .W/.WD, .PNT, 3-D view, gradients, remaps, the object
model table at DGROUP 0x5d20..) do not apply to this address range. No code here reads worlds.lib, .PNT,
REMAP*.BIN or GRADSKY/GRADGRN; the 3-D renderer is 2255:3090 (`r3d_render_view`), sky/ground is
1000:1afc (`tod_draw_sky_ground`), camp worlds are loaded by 19ac:64d4. This half only *calls* them from the
debriefing map view and the insertion/extraction cut-scenes.

Conventions: times are ticks of the 256 Hz clock `g_timer` (DS:eca6, u32; 0x100 = 1 s). `random(n)` is
2255:78d6 = `(rng & 0x7fff) % n`. Coordinates are 320x200 screen pixels. "SE" = 144-byte personnel record
(sealNN.se), "roster entry" = 0x18-byte campaign personnel record, "MCI" = mission header (g_mci, DS:ed32,
pointer g_mci_ptr DS:eeb8). Names of callees in other segments come from the other `tools/re/symbols` files.

## 1. Module purposes

| area | entry | purpose |
|---|---|---|
| 8001–91d8 | `recruit_screen` 8095 | "UDT/SEAL Training School" (new campaign): choose starting year and point man, Biography/Rating pages, nickname |
| 921e–953c | `font_*` | DeluxeFont (.fnt) loader and proportional text output (register-argument helpers) |
| 9540–a862 | `cmpscr_screen` 9aa6 | "SEAL Campaign" screen (continue campaign): 8 save and 8 load dog tags, info window (campaign stats, biography, rating), award/promotion/new-member messages |
| a862–af75 | `bull_screen` abb9 | "SEAL Bull Session": animated SEALs and a scripted conversation before the mission |
| afdc–cca0 | `dbrf_screen` b511 | "Mission Debriefing": map fly-over talk by the OIC, Post-Mission Report and Historic Report clipboards; then records the mission and runs the speech screen |
| cea8–d659 | `cut_insertion_screen` d285, `cut_extraction_screen` d659 | 3-D cut-scenes in the SEAL camp before and after a mission |
| d7d6–dbe6 | `speech_screen` d94a | Chaplain (funeral) / Commander speeches (end of year, end of campaign, relieved of command) |
| dbe6–e554 | `score_mission` deb0 | End-of-mission score, promotions, Purple Hearts, medals, unit citations, skill training |
| e554–eb3a | `diff_screen` e5bd | "Difficulty" options screen (F10 from the main menu), saves st1.dfr |

Place in the campaign flow (main loop 19ac:016d, step counter 0..8, skipped steps in demo mode 6):
0 `recruit_screen` (only when g_game_mode == 2) → 1 `cmpscr_screen` (does nothing unless mode 1) →
2 `intel_screen` (365e:74e8) → 3 briefing / Patrol Order / Marching Order (365e:54bd) → 4 `bull_screen` →
5 `cut_insertion_screen` → 6 mission (1000:01de) → 7 `cut_extraction_screen` → 8 `dbrf_screen`.
Screen results: 0 = quit game (Alt-X), 1 = back / main menu, 2 = next step. 19ac:00b6 calls `score_mission`
right after the mission.

## 2. Shared UI conventions

### 2.1 Button lists
All screens use the button toolkit of the lower half (365e:2af6 … 3712). A button record is 16 bytes, the
list ends with a record whose key is 0:

| off | type | meaning |
|---|---|---|
| +0 | i16 | x |
| +2 | i16 | y |
| +4 | i16 | w |
| +6 | i16 | h |
| +8 | u16 | key code injected when the button fires (ASCII or BIOS scan<<8) |
| +0xa | i8 | index of the underlined hot-key character, 0xff none |
| +0xb | u8 | flags: 0x01 drawn/enabled, 0x02 disabled look (hatched), 0x10 highlighted/selected, 0x20 shadow frame, 0x40 invisible hot zone |
| +0xc | | unused here (0) |

Labels come from a parallel table of near string pointers passed to `ui_draw_buttons(list far*, labels*)`.
Input pattern of every handler `X_handle_input(key, dx, dy)`: if the pointer moved, `ui_pointer_update(dx,
dy, list)` (redraw if focus changed); `ui_button_release(key)`; if `g_ui_release_count` counts down to 0 the
handler calls itself with `list[g_ui_focus].key` (so a clicked button behaves like its hot key after 3
frames); `input_toggle_keys` (Alt-S/D/M) and `menu_arrow_keys` are tried first; Enter presses the focused
button (`ui_button_press`), Alt-X (0x2d00) returns 0.

### 2.2 Screen loop pattern
Enter: `ui_screen_transition(palette, wait)` (365e:3180), load background picture `pic_load(n)` (names: 0
mapscr, 3 pwo, 7 debrf, 9 load, 10–13 new..new4, 14–19 cm0..cm5), sprite sets, `g_redraw_frames = 2` or 4,
`input_reset_repeat_timers`, `input_flush_keyboard`, `snd_load_music(n, 0)`. Each frame:
`snd_music_start_once(m, 0)`, `pal_request_fade(0)`, render, `gfx_present`, `clk_update`, optional
`msg_queue_tick` / `tod_update` / timers, `key = input_get_key()`, `(dx,dy) = input_get_motion()`, handler.
Render: when `g_redraw_frames != 0` restore the background (`pic_blit_to_screen(&g_screen_rect)`), draw
everything, decrement the counter; otherwise only `frame_limit_wait` + `cursor_erase`; then clip 0,0,320,200,
`frame_limit_wait`, `cursor_draw`. Leave: `cursor_set_wait(1)`, one last render, `gfx_present`, stop music.

### 2.3 Text output
Three DeluxeFonts (section 3.1) plus the fixed 4x6 font of 4dcf:000e. The font helpers take register
arguments: AX = x, DX = y, BX = near string (DS). Colours are 8-bit palette indices written to `g_text_color`
as 0xFF00|c.

* `font_draw_text_shadow(x, y, s, cA, cB)` (9489): `g_clip_y0++`, `g_clip_y1++`; draw s at (x+1, y+1) in
  cA; restore the clip; draw s at (x, y) in cB. Typical pairs: (0, 0x0f) white text with black shadow for
  titles, (0x10, 0) or (0x0f, 0) black text with a light shadow for panel text.
* `font_draw_text_emboss(x, y, s, a, b, c)` (9505): shadow pass (x+1, y+1, a then c) followed by the shadow
  pass (x, y, b then c): visible result c at (x,y), b at (x+1,y+1), a at (x+2,y+2).
* `font_draw_text` (93bc) draws char by char with `font_draw_char(c, x, y)` (4dec:0002), x += advance;
  nothing happens when `g_font_cur` is NULL. `font_draw_text_far` (93fc) is the far-string variant,
  `font_draw_text_far_centered` (9438) starts at x = g_clip_cx - width/2.
* `text4x6_draw_string(y, x, s)` (4dcf:000e) — note the **first C argument is y**; x must be a multiple of
  4, every character is 4 px wide and 6 px high. `text4x6_draw_centered` (94de, AX = y) uses
  x = (g_clip_cx - 2*strlen + 1) & ~3.
* Numbers are formatted with `str_utoa` (19ac:5f6a, unsigned) or `str_itoa_pad(v, buf, 2, '0', ...)`.

## 3. File formats handled here

### 3.1 DeluxeFont `.fnt` (memo.fnt, prop.fnt, propbold.fnt, also 4x6.fnt)
Verified against all four files (file size = 0x22c + bytes_per_row * rows exactly).

| file off | size | meaning |
|---|---|---|
| 0x00 | 13 | "[DeluxeFont]\0" |
| 0x0d | 1 | version, must be 2 (`font_load` rejects anything else) |
| 0x0e | 0x21e | header block, copied verbatim into a 0x21e-byte far block (offsets below are relative to it) |
| 0x22c | bpr*rows | glyph bitmap, 1 bpp, MSB = leftmost pixel, row stride = bpr bytes |

Header block: +0x00 font name (NUL padded, 0x10 bytes), +0x10 u16 and +0x12 u16 unknown (memo 0/12,
prop 20/10, propbold 0/8, 4x6 0/8 — probably line spacing), +0x14 u16 bpr (bytes per bitmap row),
+0x16 u16 rows (glyph height: memo 12, prop 9, propbold 8, 4x6 6), +0x18 u16 unknown (9/6/6/4),
+0x1a u16 runtime: segment of the loaded bitmap (garbage in the file), +0x1c u16[257] x offset of each glyph
in the bitmap; glyph c occupies columns off[c] .. off[c+1]-1, advance = off[c+1]-off[c] (0 = no glyph).
Codes below 32 hold key-cap pictures (SPACE, ESC, PgDn, PgUp, arrows...).

`font_load` (9271, near, BX = name): allocate 0x21e bytes; `file_load_far(name)`; require byte 0x0d == 2;
copy 0x21e bytes from +0x0e; allocate bpr*rows bytes, store its segment at +0x1a, copy the bitmap from
+0x22c; free the file image. Any failure frees everything and returns NULL. `font_load_all` (921e) loads
memo → `g_font_clipboard`, propbold → `g_font_title`, prop → `g_font_dialog`, then selects propbold.

### 3.2 Personnel record `sealNN.se` / `vcNN.se` … (se.lib, method 1, 144 bytes decoded)
Loaded by `roster_load_seal_se` (365e:384f). Fields read by this half:

| off | type | meaning |
|---|---|---|
| 0x00 | char[12] | first name |
| 0x0c | char[14] | last name (used as the speaker name in dialogues) |
| 0x20 | char[26] | nickname in the file (not used here; the campaign nickname is used instead) |
| 0x3a | char[] | home town ("Lexington, KY\n") |
| 0x5a | u8 | height, feet |
| 0x5b | u8 | height, inches |
| 0x5c | u8 | weight, lbs |
| 0x5d | u8 | BUD/S class index (g_buds_class_names); bit 0 also selects "SEAL Team 2" on the Biography panel |
| 0x5e | u8 | rating/specialty index (g_rating_names), 0 = none |
| 0x5f | u8 | age at the starting year (displayed age = value + campaign year index) |
| 0x61 | u8 | rank index (g_rank_names), updated by promotions |
| 0x62 | u8 | camouflage index (g_camo_names) |
| 0x63 | u8 | best weapon id (g_weapon_table), copied to campaign+0x14a |
| 0x64 | u8 | second weapon id, copied to campaign+0x14b |
| 0x74 | u8[12] | skills 0..90: Rifle, Pistol, Mortar, Shoulder, Automatic, Throw, Observe, Radio, Size, Strength, Agility, Intelligence |

Example SEAL01: Dave "Rebel" Casings, 6'4", 210 lbs, class 78, Engineman, age 20, Blue Jeans, best weapon 9
(M79), second 13 (M26).

### 3.3 Text files `.s` (str.lib and main4.lib, method 1): fixed-length lines, NUL padded

* `cYmNN.s` (Y = year 1..4, NN = mission 01..20), 1920 bytes = 24 lines x 80: lines 0–8 intel briefing
  (used by the intel screen), **9–13 Bull Session**, **14–18 debriefing after success**, **19–23 debriefing
  after failure**. Loaded with `msn_load_text(NULL, year*20+index)`.
* `sealNN.s`, 504 bytes = 6 lines x 84 (0x54): personal Bull Session chatter of one SEAL.
* `hYmNN.s`, 2240 bytes = 28 lines x 80: Historic Report (only for missions whose flow entry has +0xc != 0).
* `c0.s` … `c5.s` (main4.lib), 1920 bytes: speech lines 0..10 used (80 bytes each): c0 funeral (Chaplain),
  c1–c3 end of a year, c4 end of the campaign, c5 relieved of command (Commander).

### 3.4 `st1.dfr` — difficulty settings, 16 bytes = 8 little-endian words, copied to/from 5178:0148

| word | setting | values (index: meaning) |
|---|---|---|
| 0 | Ammo | 0 Unlimited, 1 Real |
| 1 | Enemy Wounds | 0 Death, 1 Heavy, 2 Real |
| 2 | Intelligence | 0 Minimal, 1 Decreased, 2 Real |
| 3 | Player Wounds | 0 None, 1 Decreased, 2 Real |
| 4 | Reload Time | 0 Instant, 1 Timed |
| 5 | Team Size | 0 Decreased, 1 Real, 2 Enhanced |
| 6 | Weapons | 0 Unlimited, 1 Real |
| 7 | Map | 0 Freeze, 1 Real |

The shipped file is {1,2,2,2,1,1,1,1} (all "Real"). It is read at start-up by `cfg_load_difficulty`
(365e:48f1) and written here by `diff_handle_input` with `file_save("st1.dfr", 5178:0148, 16)`.

### 3.5 Sprite sets and pictures used (sprite sets are RLE images `<name>F<f>R<r>.RLE`, loaded by
`spr_load_set(desc, nsets, name, rot_count?, flag)`, fetched by `spr_get_frame_image(desc, f, r)` (both
0-based) and drawn with `gfx_sprite_scaled(x, y, w, h, img)`; the first two words of an image are w, h)

| descriptor (seg 53ba) | set | lib | use |
|---|---|---|---|
| 0x22bc | port (2 sets) | port.lib | recruit portraits: f = year>>1, r = slot%8 |
| 0x22bc | prt2 (2 sets) | port.lib | campaign screen portraits: f = campaign year>>1, r = g_recruit_slot%8 |
| 0x235c | bos | lbtn.lib | dog tag of a used save slot (r = slot) |
| 0x23ac | bin | lbtn.lib | focused dog tag of an empty save slot |
| 0x23fc | bis | lbtn.lib | focused dog tag of a used save slot |
| 0x244c | bil | lbtn.lib | focused load dog tag of a used slot |
| 0x2140 | mdl (2 sets) | mdl.lib | medal ribbons (f 0, r = award bit 0..6) |
| 0x249c | bull (3 sets) | bull.lib | Bull Session backgrounds (f 1, r 2 and 3: 219x200 and 219x120) and figures (f 0..2) |
| 0x21e0 | camp (1 set) | camp.lib | debriefing backdrop (f 0, r = g_area_camp_frame[area] + 4) |

Other files: `cliph.RLE` (Historic Report clipboard picture, loaded with `ealib_load_far`), pictures
new/new2/new3/new4, load, pwo, debrf, mapscr, cm0..cm5 (`.pic`, PXPK).

### 3.6 Tables inside st.exe used here

* Weapon table DGROUP 0x48fe, 34 entries of 0x22 bytes: +0 short name ptr, +2 long name ptr (Biography
  "Best Weapon: <short> <long>"); other fields belong to the loadout code (365e_a).
* Mission flow: `g_campaign_flow[year]` (DGROUP 0x3c5a) → far 5275:0000/0118/0230/0348, 20 entries of 14
  bytes per year. Used here: +2 month (1..12), +0xc (byte) historic report available. (+8 / +0xa are the
  next-mission indices used by `cmp_record_mission`.)
* Promotion thresholds 52bb:0002, u16[10] indexed by current rank:
  {2000, 5000, 9000, 12500, 17000, 25000, 32000, 40000, 50000, 60000}.
* Medal of Honor table 52bb:001a, 9 records {u16 year, u16 mission index, u16 minimum score}:
  (0,14,2900) (1,18,2750) (1,19,3450) (2,5,4200) (3,7,2925) (3,10,3400) (3,14,3700) (3,17,2875) (3,19,4100).
* Recruit table 51d0:0078 u8[16] = {10,10,10,10, 30,30,30,30, 6,6,6,6, 26,26,26,26}: SE id of recruit slot
  s (s = year*4 + i) is s + table[s] → year 0: ids 10–13, year 1: 34–37, year 2: 14–17, year 3: 38–41
  (files SEAL11..14, 35..38, 15..18, 39..42). Inverse table 51d9:0140 u8[48] (id → offset) gives the
  portrait slot on the campaign screen: slot = id - table[id].
* Bull Session far data 51f1: +0x40 figure positions i16 {x,y}[6] = (0,40) (52,0) (42,0) (118,104) (64,226)
  (0,120); +0x58 animation rotation table i16[6][12]:
  k0 {0,1,2,2,2,2,2,1,0,0,0,0}, k1 {0,1,1,1,2,3,3,2,2,1,0,0}, k2 {0,0,0,0,0,0,1,1,1,1,1,1},
  k3 {1,1,2,2,1,0,1,1,1,1,0,0}, k4 {0,0,0,1,2,3,3,3,3,3,2,1}, k5 {1,2,3,3,3,3,3,2,1,0,1,0}.
* Speech music DGROUP 0x45a6 u8[6] = {1,0,0,0,0,2}.
* Button lists (x,y,w,h,key,flags; see 2.1):
  * recruit 51d0:0018: (10,88,112,16,'p') (10,106,112,16,'n') (198,106,112,16,'1') (198,88,112,16,'s')
    (97,127,84,16,'r'/'b'), labels 0x246e = Previous Recruit, Next Recruit, <year>, Start Campaign,
    <Rating|Biography>. The label buffers at *[0x2472] and *[0x2476] and the key at 51d0:0060 are rewritten
    every frame (year text "1966"+y; page button shows the *other* page: key 'r' + "Rating" while the
    Biography is shown, key 'b' + "Biography" while the Rating is shown).
  * campaign 51d9:0000: 0 (0,176,320,24,'s', flags 2), 1–8 save tags x = 26,61,100,134,168,202,237,274,
    y 29, 18x26, key 'v', flags 0x41; 9–16 load tags x = 32,66,104,138,173,207,242,279, y 61, 18x16, key 'l';
    17 More (101,85,56,16,'m'); 18 Next (276,4,40,16,'n').
  * bull 51f1:0000: (0,16,32,143,'n',0x41) (60,28,128,112,'b',0x41) Next (276,4,40,16,'n').
  * debriefing 5200:0000: (0,176,320,24,'n',0x42) screen (116,28,128,112,'d') Post-Mission clipboard
    (42,31,48,56,'p') Historic clipboard (42,103,48,56,'h'; flag bit 0 set only when a report exists)
    (274,0,46,200,'n') Next/Exit (276,4,40,16,'n').
  * cut-scenes 5207:0000: (0,16,32,143,'n',0x41) (276,4,40,16,'n'); speech 520a:0000: (0,176,320,24,'s',0x41)
    Next (276,4,40,16,'n').
  * difficulty 52c0:0040: settings at x 30, y 25+17*i, 80x16, keys a e i p r t w m; Save (70,161,'s'),
    Quit (155,161,'q'); option buttons 10–29 (60x16, flags 2) in rows at x 115/180/245;
    52c0:0000 u16[8][4] option button indices per setting: {10,11} {12,13,14} {15,16,17} {18,19,20}
    {21,22} {23,24,25} {26,27} {28,29}.

## 4. Data structures (fields used here)

### 4.1 Campaign state (`g_campaign`, 0x14c bytes at DGROUP 0x3b0e)

| off | type | meaning |
|---|---|---|
| 0x00 | u8 | save slot 0..8 (8 = autosave c8.cmp) |
| 0x01 | u8 | campaign year 0..3 (1966+y) |
| 0x02 | u8 | mission index in the year 0..19, 0xff = campaign over |
| 0x04 | u16 | point man SE id |
| 0x06 | u32 | total score (panel prints the low word) |
| 0x0a | u32 | best mission score |
| 0x0e | char[] | nickname typed on the recruit screen (≤ 14 chars) |
| 0x2a + 12k | 12 bytes | history entry k (k = point man mission count before the mission), 24 entries up to 0x14a (the flow table plays 6 missions per year, "Tours" = missions/6, so 24 is the maximum) |
| 0x14a | u8 | point man best weapon (recruit screen), later the loadout choice |
| 0x14b | u8 | point man second weapon |

History entry: +0 u16 mission number (year*20+index), +2 u32 score, +6 u16 awards of that mission (bit
layout as `g_medal_names`; only the last award written survives, see section 12), +8 u16 casualty flags of the
point man (+9 bit 0x40 = killed), +0xa u8 rank after the mission (0 = no promotion), +0xb u8 mission won.
Readers use two different bases: `cmpscr_msg_awards` reads awards at campaign+0x24+12n and rank at
campaign+0x28+12n with n = missions *after* the increment (= entry n-1); `dbrf_screen` tests
campaign+0x33+12n (entry n +9) before the increment.

### 4.2 Roster entry (0x18 bytes, `g_roster`, `roster_find(id)`)
+0 u8 SE id, +1 u8 missions flown, +2 u8 missions won (Victories), +3 u8 rank, +4 u16 accumulated award bits,
+8 u16 casualty flags (byte +9 bit 0x40 = KIA), +0xa u8[8] weapon skills (trained by `score_train_skill`),
+0x12 u8 flags (0x01 in team, 0x02 in hospital, 0x04 reserve recruit not yet introduced, 0x08 dead),
+0x14 far* SE record. The recruit screen uses the 24-byte record at 51d0:0000 as a scratch roster entry.

### 4.3 Other state
* Team slots 5178:000c, 4 x 12 bytes (+0 SE id): Point Man, OIC (0x18), Corpsman (0x24), Rear (0x30).
* Soldier (unit) fields used by the scoring code: +2 far* world object (+6 position 3 x i32 in 1/256 units:
  x @+6, altitude @+0xa, y @+0xe), +6 far* STATUS (+0x0f bit 0x40 dead), +0xa far* MOVER (+0x26 bit 0x10
  searched/captured, 0x20 secured, 0x40 aboard the extraction craft), +0x28 far* roster entry.
  `g_groups` (DGROUP 0x12e2) = NULL-terminated group pointers, group = NULL-terminated unit far pointers,
  group+0x26 = type (0 SEALs, 1–3 craft, 4 VC, 5 NVA, 6 civilians, 7 friendly).
* Objective records inside g_mci: primary +0x76 (DS:eda8), secondary +0xd8 (DS:ee0a), tertiary +0x13a
  (DS:ee6c): +0 type (1 Patrol, 2 Ambush, 3 Demolition, 4 Observe, 5 Rescue, 6 Snatch, 7 Recover),
  +0xe target group index relative to `g_first_mtm_group`, +0x10 target argument (world object index).
* Mission statistics (far seg 53ba): 0x303a rounds fired, 0x303c rounds hit, 0x303e grenades thrown,
  0x3040 grenades hit, 0x3042 bonus counter (+250 each), 0x3044 mission score, 0x3046 team size at start,
  0x3048 / 0x304a difficulty-screen done flags. `score_reset_stats` zeroes 303a..3044.
* Campaign directory (far 5178, s.cnf image): 0x63 last slot, 0x64 u8[8] slot used, 0x6c + 26*slot names.

## 5. Recruit screen — `recruit_screen(campaign)` (365e:8095)

Enter: `g_cur_roster` = 51d0:0000; `g_recruit_slot %= 4`; `g_recruit_campaign` = campaign;
`g_recruit_slot += year*4`; `ui_screen_transition(6, 1)`; `recruit_enter` (pic_load(year+10), sprite set
"port" with 2 sets, `voice_load(year)`, focus button 3, show Biography); `roster_entry_init(g_cur_roster,
slot + table78[slot])`; `g_recruit_se` = its SE; campaign+0x14a/0x14b = SE+0x63/0x64; nickname = "";
redraw 2; `voice_play`. Loop body additionally calls `voice_tick(g_frame_ticks)`.
Leave: `recruit_leave` (free voices and portraits), free the SE, campaign+4 = SE id,
`roster_new(id)`, `cmp_save(8)`, `roster_free()`, `cmp_load(8)`.

`recruit_handle_input` keys (after the common part): Esc → done, result 1. '1' → campaign+2 = 0,
year = (year+1) % 4, slot = year*4, roster id = slot + table78[slot], `g_mission_no = year*20`, reload
background and voices (voice_stop/free/load/play), reload SE, campaign+0x14a = SE+0x63. 'n'/'p' → i =
slot%4 ± 1 clamped to 0..3, slot = i + year*4, reload SE, campaign+0x14a = SE+0x63 (0x14b is not updated).
'r'/'b' → toggle page. 'e'/'w' (undocumented) → if weapon ≥ 1 for 'e': campaign+0x14a =
`loadout_next_weapon(w, year, 0 = down / 1 = up, class 1)`. 's' → `ui_dialog_prompt("Enter Nickname:",
0, 0, 64, 148, 14, 16, 0, 0)`; if the first byte is 1 (cancelled) stay, else copy it to campaign+0x0e,
done, result 2. Alt-X → done, result 0. Every handled key sets `g_redraw_frames = 2`.

Render (`recruit_draw_contents`): title tab " UDT/SEAL Training School  " at (0x58, 4); the Biography
(`ui_draw_bio_panel`) or Rating (`ui_draw_rating_panel`) panel for `g_recruit_se` at (0x16, 0x7f); year
label = str_utoa(1966+year); buttons.

## 6. Information panels

All panels: `ui_draw_text_panel("", x-8, y, 292, 68)` (bevelled frame), title in propbold at (x, y+2)
colours (0, 0x0f); body in prop unless noted. "Name line" = propbold at (x, y+0x1b): first name + "  " +
("'" + nickname + "'  " if campaign+0x0e is not empty) + last name; colours shadow 0x0f, text 0x1a
(Biography/Rating) or 0x18 (Campaign panel) when the roster entry is KIA (byte +9 bit 0x40) or the
campaign is over (campaign+2 == 0xff), else 0.

Rank line at (x, y+0x11), colours (0x10, 0) on the Biography, (0x0f, 0) elsewhere:
* Biography (uses SE+0x61 rank r, SE+0x5e rating t): r < 5 → text = rating name[t], plus "  " when t != 0;
  r ≥ 5 → "" ; then if t == 0 and r < 5 → text = "Seaman" (replaces); then if r > 1 append rank name[r].
* Rating panel: same rules. Campaign panel uses the roster rank (entry+3) and appends "  " after "Seaman".
* After the rank line: "SEAL Team 1" at x + width + 8, the digit becomes '2' when (SE+0x5d & 1)
  (Biography/Rating) or when ((|slot| / 4) & 1) (Campaign, slot = portrait slot, i.e. odd recruit year).

`ui_draw_bio_panel(se, x, y)` (81f1), title "Biography": portrait frame `ui_draw_text_panel(NULL, x+0xf4,
y+3, 0x24, 0x21)` and portrait `spr_get_frame_image(port, campaign_year>>1, g_recruit_slot % 8)` at
(x+0xf2, y+2); (x, y+0x25) "<SE+0x5f + campaign year> Years Old"; (x+0xa8, same y) "<ft> ft ", (x+0xbc)
"<in> in", (x+0xd0) "<lbs> lbs"; (x, y+0x2f) home town; (x+0xa8, y+0x2f) "BUD/S Class <class>";
(x, y+0x39) "Best Weapon:", (x+0x3c) "<short> <long>" of campaign+0x14a; (x+0xa8, y+0x39) "Camouflage:",
(x+0xdc) camouflage name. Colours (0x10, 0).

`ui_draw_rating_panel(se, x, y)` (889d), title "Rating": rank line and name line as above; "  STR: " +
Green/Novice/Veteran/Elite with index min(3, (roster_skill_rating(g_cur_roster) - 50) / 10) (C division,
not clamped below), right-aligned so it ends at x+0x116, y+0x1b, name-line colours. Nine skill bars from
SE+0x74, skipping Mortar (2) and Size (8): Rifle, Pistol, Shoulder, Automatic, Throw, Observe, Radio,
Strength, Agility. Bar j: column c = j/3, row r = j%3, bar at (x + 99c, y + 0x25 + 10r), label at bar
x + 0x2a (prop, colours 0x0f/0). Length L = clamp(v - 50, 1, 40) (exactly: v' = max(v, 50),
L = max(1, 40 - max(0, 90 - v'))); draw `gfx_fill_rect(x+1, y+1, L, 4, 0)` then `gfx_fill_rect(x, y, L, 4,
0x0f)`.

`ui_draw_campaign_panel(roster, x, y)` (9c20), title "Campaign": rank/name lines; if the campaign is not
over: right-aligned at x+0x11b, y+0x11: "<index+1> <Month>  <1966+year>" (month from the flow entry);
(x, y+0x25) "Status:" and at x+0x38 Active / Retired (campaign over) / KIA (roster KIA); (x+0x5e) "Tours:"
and at x+0x92 min(missions/6, 4); (x, y+0x2f) "Missions:" / value at x+0x38; if missions > 0 (x+0x5e)
"Victories:" / wins at x+0x92; (x, y+0x39) "Total Score:" / campaign word +6 at x+0x38; if missions > 0
(x+0x5e) "Best Score:" / word +0xa at x+0x92; ribbons at (x+0xac, y+0x1d).

`ui_draw_ribbons(x, y)` (9998): nothing if the point man has 0 missions. For each history entry j <
missions and while fewer than 16 are drawn: bits = awards; for k = 0..6 (bits 7..15 — Medal of Honor and
citations — are never drawn): if bit k: img = mdl(0, k%8); draw at (cx, cy); cx += w+1; every 4th ribbon
cx = x, cy += h+2.

## 7. Campaign screen — `cmpscr_screen(campaign)` (365e:9aa6)

Only runs when `g_game_mode == 1`; otherwise returns 2 immediately. Enter: `ui_screen_transition(7, 1)`;
`cmpscr_enter` (pic_load(9) load.pic; sets bos, bin, bis, bil (1 set each), mdl (2 sets), prt2 (2 sets);
focus Next (51d9:0120, g_ui_focus = 18); `msg_queue_reset`; page 0); redraw 4; music 0x0e;
`g_from_mission` ? `cmp_load_autosave` : `cmp_load(campaign+0)`; `g_cur_roster = roster_find(campaign+4)`;
`g_pm_se` = its SE; `g_recruit_slot = id - table140[id]`; `cmpscr_msg_awards`; if campaign over →
`cmpscr_msg_campaign_over` else if `g_from_mission` → `cmpscr_msg_new_members`. Music sequence 1 when the
point man is KIA, else 0. Leave: `cmp_save_autosave`, `roster_free`, stop music, `cmpscr_leave`,
`cmp_load_autosave`.

Messages (`msg_queue_add(text, kind, duration, style, sticky)`):
* `cmpscr_msg_awards`: n = missions; if n: for each set bit i (0..15) of history awards → ("You've been
  awarded a <medal>.", 1, 0x400), if i < 8 (medal picture, kind 4, style i), ("", 1, 0x100), ("", 1, 0);
  then if the history rank > 1 → ("You've received a promotion to <rank>.", 1, 0x400).
* `cmpscr_msg_campaign_over`: ("Your Campaign has ended.", 1, 0x800), ("Load a Campaign, or press Next for
  the Main Menu.", 1, 0).
* `cmpscr_msg_new_members`: repeat `g_seal_kia` times: i = `roster_activate_recruit()` (index 0..23 inside
  the pool, -1 none); if `roster_find(i)` (note: the index is used as an SE id without the pool base)
  → ("Welcome your new team member <last name>.", 1, 0x300), ("", 1, 0).

Keys (`cmpscr_handle_input`, result starts at 2): Esc → result 1, leave. 'n' → leave (result 1 if the
campaign is over). 'm' → page = (page+1) % 3. 'c' → page 0. 'v' with focus 1..8 (slot = focus-1): if the slot
is unused or `ui_confirm_replace_campaign` agrees: campaign+0 = slot; `ui_dialog_prompt("Enter Campaign
Name:", 0, 0, 32, 60, 25, 16, 0, 1)`; unless cancelled copy the name to 5178:006c+26*slot, `cmp_save(slot)`,
`cmp_set_last_slot(slot)`. 'l' with focus 9..16 (slot = focus-9), slot used and `ui_confirm_load_campaign`:
`roster_free`, `cmp_load(slot)`, campaign+0 = slot, reselect point man/SE/portrait slot,
`cmpscr_msg_awards`, page 0. Alt-E (0x1200) with focus 1..8 → `cmp_delete_slot`. Alt-X → result 0, leave.

Render: bottom bar `gfx_fill_rect(0, 176, 320, 24, 0)`; dog tags (below); title tab " SEAL Campaign " at
(0x78, 4); page 0 `ui_draw_campaign_panel(g_cur_roster, 0x16, 0x56)`, 1 bio, 2 rating (for `g_pm_se`);
buttons; bar again; `msg_draw`. Dog tags `cmpscr_draw_dogtags(list, draw_used)` for buttons 1..16
(while key is 'v' or 'l'): x = 0x19 + 0x23*((i-1)%8), y = 8, rotation (i-1)%8, frame set 0. Focused (focus
== i and pressed/releasing) or flag 0x10: save tags → bis if the slot is used else bin; load tags → bil if
used. Otherwise, when draw_used, save tags of used slots → bos.

## 8. Bull Session — `bull_screen()` (365e:abb9)

Enter: clocks reset, `tod_set(12, 0)`, `msg_queue_reset`, `g_bull_variant = point man missions % 3`,
`g_bull_scroll_y = -60 * variant`, `ui_screen_transition(9, 1)`, `bull_enter` (set "bull" 3 sets, draw
background, focus Next (51f1:0020), `g_mission_text = msn_load_text(NULL, year*20+index)`, `g_seal_chatter
= msn_load_seal_bio_text(id of team slot 5178:0018)`), timers, `bull_start_talk`, music 9.
Frame: every 0x80 ticks `g_bull_anim_step++`; `msg_queue_tick`; every 0x500 ticks `bull_next_line`; if no
conversation is running (`g_bull_talking == 0`) for 0x1e00 ticks → `bull_start_talk`. Leave (`bull_leave`):
`g_bull_chatter_line++`, free texts and sprites.

`bull_start_talk`: `mouse_set_pos(105, 105)` (re-centres the relative mouse used by `input_get_motion`),
redraw, line timer = now - 0x500, `g_bull_line = 0`, talking = 1.
`bull_next_line` (k = `g_bull_line` before the step): text = k 0..4 → mission text line 9+k; k 5 →
"Okay men, let's head out."; k 6 → chatter line (`g_bull_chatter_line % 6`) if loaded, else mission line
15; k ≥ 7 → mission line 9. If k < 7: k++ and redraw. Empty lines are skipped (loop) while k < 7. If the
text is non-empty and k (after step) ≤ 7: speaker slot p = (k + (k == 7)) & 1 → team slot 5178:0018 + 12p;
`msg_say(k == 6 ? "OIC" : SE last name, text)`. Then if k ≥ 7: k 7 → 8; when `msg_queue_count() <= 1`
→ k = 0, talking = 0. (So the conversation alternates Corpsman/OIC, the OIC says the wrap-up line, the OIC
slot SEAL adds his personal chatter, and mission line 9 is repeated once at the end.)

Keys: Enter or 'n' → leave, result 2; Esc → result 1; Space → `bull_next_line`, reset the line timer,
`msg_queue_skip`; 'b' → `bull_start_talk` if no talk is running; Alt-X → result 0.

Render: `bull_draw_background` (clear to 0; image bull(1,2) drawn at (40, scroll_y) with the clip rect
(0, 0, image width, 200); if scroll_y < 0 image bull(1,3) at (40, scroll_y+200)); `bull_draw_seals` (clip
full; for k = variant .. variant+3: img = bull(k>>1, anim[k][g_bull_anim_step % 12] + (k odd ? 4 : 0)),
drawn at (pos[k].x + 40, pos[k].y + scroll_y)); bottom bar; title " SEAL Bull Session " at (0x62, 4);
messages; buttons. Sprite sizes pass through `math_mul_8_8(size, 0x100)` (identity).

## 9. Insertion and extraction cut-scenes

`cut_insertion_screen()` (d285): clocks reset; `camp_load_scene(MCI area)`; `tod_set(MCI start hour,
minute)`; `tod_add(-1, -random(60))`; `camp_place_at_insertion`; `cut_init_viewport` (viewport 0xd88a,
screen rect y 0x18, 320x139, `view_init_camera(0xd88a, -1000, 0, 30, 0, 0, 0x18, 0x140, 0x8b)`);
`cut_init_camera` (scene start = now, `view_set_mode(5)`, focus 5207:0000, 0xd842 = 0x690, zoom
0xd846 = 0x180, 0x530/0x532 = 0); `ui_screen_transition(tod_palette_index(1), 1)`;
`view_update_camera(g_view_mode)`; `mis_load_sprites`; `msg_queue_reset`; `cut_queue_caption`; music 10.
Frame: `snd_music_start_once(0,0)`, `cut_insertion_timeout` (ends with `pal_request_fade(0)`),
`view_update_camera(g_view_mode)`, `cut_render`, `gfx_present`, `clk_update`, `tod_update`,
`spr_advance_anim_clocks`, input + `cut_handle_input`, `evt_mission_tick` (2dbd:3f4c), `msg_queue_tick`.
Leave: cursor wait, render, present, stop/release music, `mis_free_sprites`, `mis_free_world`,
`msg_queue_clear`, `cursor_reset`.

`cut_extraction_screen()` (d659): `camp_load_scene(area)`, `camp_place_after_extraction`,
`tod_add(0, random(30)+30)`, viewport/camera as above, `view_set_mode(6)`, caption, music 12, music
sequence 2 if `g_seal_kia > 0`, else 0 when the mission was won (`cmp_mission_won(campaign, score)`), else 1.
Uses `cut_extraction_timeout`.

Timeouts (t = now - start): insertion leaves when t > 0x1dff; when the music has finished and t < 0x1c00
the start is moved so that t = 0x1c00 (leave 0x200 ticks later). Extraction: same music rule, leave when
t > 0x13ff (i.e. immediately once the music is done); after t > 0x1c00 a finished sequence is stopped.

`cut_queue_caption`: ("<HH><MM> Hours       <index+1> <Month> <1966+year>", 1, 0x1400) with HH, MM =
`str_itoa_pad(g_clock_hour/min, 2, '0')` and month from the flow entry, then ("SEAL Team Camp, " +
location, 1, 0) where location = ((char**)0x2566)[g_world_index] (DS:ed16; the camp worlds 28..31 =
0x1c + g_area_camp_frame[area] give "Nha Be, Rung Sat Special Zone", "My Tho, Mekong Delta", "Vinh Long, Mekong
Delta", "Bin Thuy, Bassac River").
`cut_handle_input`: pointer dx ≠ 0 → `math_angle_add` on the camera heading; dy < 0 → zoom -= 6 (min
0x60), dy > 0 → zoom += 6 (max 900); Left/Right arrows (0x4b00/0x4d00) → angle add; Enter/Esc/'n' →
leave (2); Alt-X → 0. Render (`cut_render`): clear (full redraw) or cursor, bottom bar
`gfx_fill_rect(0,176,320,24,0)`, messages in propbold, clip to the viewport, `tod_draw_sky_ground`,
`r3d_render_view(...)`.

## 10. Mission Debriefing — `dbrf_screen(load_world)` (365e:b511, always called with 1)

Enter: `g_dbrf_score = max(0, stats.score)`; state 0; `ui_screen_transition(0,1)`; `g_mci_ptr =
msn_load()` (NULL → return with an uninitialised result); clocks; `mis_load_world(area)` when the argument is set; `tod_set(start)`,
`tod_add(g_mission_minutes/60, %60)`; `msg_queue_reset`; `brfcam_reset(6000)`; `dbrf_init_viewports`
(`view_init_camera` for 0xd84e: (-1000, 15, 0, 0, 0x112, 0, 0x2e, 200); 0xd86e: (0, 0, 1000, -90, 0x74,
0x1c, 0x80, 0x70) = the "debriefing screen" panel 0x80x0x70 at (0x74,0x1c); 0xd88a as in 9; current =
0xd84e); `dbrf_init_scene` (`view_set_eye_height`, `view_enter_map`, `dbrf_init`, "camp" set, 0xd8b8 =
0x690, 0xd8b4 = 0xc0, 0xd8b6 = 0); `view_update_camera(1)`; remember the point man's world position and
move him to (x 0xfff44800, y 0x0004b000); queue ("Check out the report on the mission", 1, 0x400) and
("before the debriefing begins.", 1, 0); `brfcam_queue_push(&old_position, 6000, 0x400, 0, 1)`; redraw 4;
music 0x0d; music sequence m = 2 if the point man was killed (campaign+0x33+12n bit 0x40), else 0 won /
1 lost.
`dbrf_init`: `dbrf_enter_main` (debrf.pic, palette 0, label "Next"), state 0, group 0 +0x2c (u32) =
0x0003e800, focus Next (5200:0050), mission text; if the flow entry has a historic report:
`g_dbrf_has_historic = 1`, set bit 0 of the Historic clipboard flags (5200:003b), load "hYmNN.s" and
cliph.RLE; else clear both.

Frame: `snd_music_start_once(m)`, `pal_request_fade(0)`, render, `gfx_present`, `clk_update`,
`tod_update`, `msg_queue_tick`; talking → every 0x800 ticks `dbrf_next_line`; idle (state 0, not demo) for
0x4000 ticks → `dbrf_start_talk`; input.
Pointer movement is only tracked in state 0; a released button fires its key in state 0 and closes the
report in states 2/3. Keys: Enter presses the focused button (in state 0 only if one is focused and its key
is not Enter); Esc: in a report → close it, while talking → ignored, idle → leave; Space while talking → `dbrf_next_line`, reset timer, `msg_queue_skip`, `brfcam_queue_skip`;
'd' (idle, not demo) → `dbrf_start_talk`; 'p' (idle) → state 2 + `dbrf_enter_report`; 'h' (idle, report
exists) → state 3 + `dbrf_enter_report`; 'e' in a report → close; 'n' → leave; Alt-X → result 0. Closing a
report: state 0, `g_dbrf_report_closed = 1`, `dbrf_enter_main`, redraw 4. Result is 1 unless Alt-X.
`dbrf_enter_report`: pic_load(3) pwo.pic, palette 10, cursor reset, focus 5 (Next), label "Exit".

Talk: `dbrf_start_talk` = `mouse_set_pos(105,105)`, redraw, line timer = now - 0x800, line 0, state 1.
`dbrf_next_line` (line l): if l < 6: text = mission line 14+l (won) or 19+l (lost); if l < 5 and the
text is non-empty → `msg_say("OIC", text)`, else l = 5 and `dbrf_say_casualties`; l++. Else (l ≥ 6): 6 → 7;
when `msg_queue_count() < 2` → l = 0, state 0, redraw.
`dbrf_say_casualties`: over team slots 0x0c..0x2f: KIA roster entries → "<last> is dead." joined with
"  "; if any: say "We had some casualties on that mission." and the list (speaker "OIC"). Then entries with
flag 0x02 → "<last> is in the hospital." list; if any: say the casualties sentence only if nobody died, then
the list.

Render (`dbrf_render`): states 0/1: bottom bar (0,175,320,25,0); panel `ui_draw_text_panel(NULL, 0x75,
0x1c, 0x7e, 0x70)`; camp backdrop camp(0, g_area_camp_frame[area]+4) right-aligned at (320-w, 0); if
historic: cliph at (0x2a, 0x66); `view_clear_map_ground` when talking else `view_clear_color12`. State 2 →
Post-Mission Report, 3 → Historic Report; buttons. For states 0/1 in map view mode (g_view_mode == 1, the
normal case): bottom bar; when talking `r3d_render_view` of the map, clip to the message rect,
`map_draw_routes` when talking, clip full, `dbrf_draw_overlay` (when talking: 4x6 "<(word 0xd873)/3>" at
y 0x84, x 0x9c and "Meters" at x 0xb4, colour 0x0f; title tab " Mission Debriefing  " at (0x6c,4);
`msg_draw`; `brfcam_update`). Otherwise the viewport is cleared with `tod_draw_sky_ground`.

### 10.1 Post-Mission Report (`dbrf_draw_post_mission_report`, b8ce)
Title tab " Post-Mission Report " at (0x5a, 4). Text in the 4x6 font, colour 0x1e (g_text_flags 0),
left column x 0x24, right column x 0xc4. Rows (y) — every string literal below is exact:

| y | left (x 0x24) | right (x 0xc4) |
|---|---|---|
| 0x18 | mission area name (DS:ecb9) | |
| 0x1e | "SEAL Team " + ("One" if campaign+4 < 25 else "Two") | "Insertion:" |
| 0x24 | "Date: " + utoa(index+1) + " " + first 3 chars of the month name + " " + utoa(1966+year) | " Time: " + pad2(MCI hour) + pad2(MCI minute) |
| 0x2a | "Other Units: " + other_unit(MCI+0x72) [+ ", " + other_unit(MCI+0x74) if non-zero] | " Method: " + insertion_craft(MCI+6) |
| 0x30 | "Tasks: " + objective name(MCI+0x76) [+ ", " + name(MCI+0xd8) if non-zero] [+ ", " + name(MCI+0x13a) if non-zero] | "Extraction:" |
| 0x36 | "Terrain: " + g_terrain_names[class] | " Time: " + pad2(g_clock_hour) + pad2(g_clock_min) |
| 0x3c | "Weather: " + g_weather_names[class] | " Method: " + g_extraction_names[max(0, g_extraction_type)] |
| 0x42 | "Tide: n/a" | |
| 0x48 | "Mission Narrative: " + "The primary " + name(MCI+0x76) + " objective" | |
| 0x4e | " was " + ("not " if !g_mission_success) + "accomplished.  " + "The mission lasted " + utoa(g_mission_minutes) + " minutes." | |

class = g_area_map_frame[MCI area]; other_unit(v) = *(char**)(0x25cc + (v & ~1)), insertion_craft(v)
= *(char**)(0x25c0 + (v & ~1)); pad2 = `str_itoa_pad(v, 2, '0')`. Then y = 0x54 and each further line
advances y by 6:

1. if g_secondary_success: " The secondary " + name(MCI+0xd8) + " objective was accomplished. ";
   if g_tertiary_success: " The tertiary " + name(MCI+0x13a) + " objective was accomplished.".
2. buf = "Results: "; if enemy KIA: += utoa(kia) + " enemy KIA"; if enemy captured == 0: += (kia ? "." :
   "n/a") else += (kia ? ", " : "") + utoa(cap) + " enemy captured.". The line is printed (and buf reset to
   "") only if kia or cap or (no weapons and no documents captured).
3. (continuing buf, so "Results: n/a" may prefix it) if weapons: += utoa(w) + " weapon(s) captured"; if
   documents: += (w ? ", " : "") + utoa(d) + " document(s) captured." else if w: += "."; printed if buf
   is non-empty.
4. "Friendly Casualties: " + (KIA ? utoa(KIA) + " SEAL(s) KIA" : "") + (WIA ? (KIA ? ", " : "") + utoa(WIA)
   + " SEAL(s) WIA." : (KIA ? "." : "None.")) — printed without advancing y first.
5. y += 6: "Remarks: " + "Rounds Fired " + utoa(S.303a) + ", " + "Rounds Hit " + utoa(S.303c) + ".".
6. y += 6: " Grenades Thrown " + utoa(S.303e) + ", " + "Grenades Hit " + utoa(S.3040) + ".".
7. y += 6, memo.fnt, `font_draw_text_emboss` colours (0x12, 0x0f, 0x1c): "Score:" at (0x24, y) and
   `str_itoa_pad(g_dbrf_score, 4, …)` at (0x4c, y).

### 10.2 Historic Report (`dbrf_draw_historic_report`, c952)
Only when a report exists: title tab " Historic Report " at (0x6e, 4); colour 0x1e; the 28 lines of
hYmNN.s, each cut to 58 characters, at x 0x28, y = 0x18 + 6*i.

### 10.3 After the loop
Cursor wait, render, stop music, clear messages, `dbrf_free`, `mis_free_world`, `msn_free_mtm`. If
g_game_mode ≤ 2 (campaign): `cmp_record_mission(campaign, g_dbrf_score)`, `loadout_release_team`; if the
result is non-zero: `cmp_save_autosave`, then speech kind k: 0 if the point man was killed; else 5 if
`roster_count_kia() > 10`; else 4 if the campaign is over; else nothing unless the new mission index is 0
(new year) → k = campaign year; result = `speech_screen(k)`.

## 11. Speech screen — `speech_screen(kind)` (365e:d94a)

`g_speech_kind = kind`; clocks; `speech_enter` (pic_load(14+kind) cmK.pic, focus 520a:0010, text "c<K>.s");
`msg_queue_reset`; `speech_start` (`mouse_set_pos(105,105)`, redraw, line 0, `msg_queue_clear`); unless demo: music 0x0d,
`ui_screen_transition(11 + kind, 1)`. In demo mode (6) the loop is skipped. Frame: music sequence
`g_speech_music[kind]`, fade, render (bars (0,176,320,24,0), `msg_draw`, buttons), every 0x800 ticks
`speech_next_line`, input (Space → next line + `msg_queue_skip`; 'n'/Esc → leave; Enter only when
g_game_mode > 2 presses the button; Alt-X → 0). `speech_next_line`: skip empty lines up to line 11; say line
l with speaker "Chaplain" (kind 0) or "Commander"; after line 10 wait until `msg_queue_count() < 2` and
restart at 0.

## 12. Mission scoring — `score_mission` (365e:deb0)

Arguments from 19ac:00b6 (all 16-bit): out rounds fired, out rounds hit, prev_score (history entry
n-1, low word of +2), rank of the point man, (unused), total campaign score (low word), year, mission
index, year KIA count (always 0 from the caller), two unused values, out &g_mission_success,
&g_secondary_success, &g_tertiary_success. S = mission statistics (section 4.3).

1. `S.team_size = count(group 0)`; *fired = S.303a, *hit = S.303c.
2. score = `score_survival_points(&alive)` + 5*hit, where survival = (point man alive ? 200 : 0) + (alive ==
   S.team_size ? 50 : 100*(alive - S.team_size)) + (no dead in groups of type 1..3 ? 35 : 0).
3. If hit ≠ 0: p = ((100*hit) mod 65536) / fired (unsigned): p == 100 → +300; 80..99 → +200; 60..79 →
   +175; 45..59 → +150; 33..44 → +100.
4. If grenades hit g ≠ 0: q = g / thrown (unsigned): q ≥ 10 → +300; 7..9 → +200; 5..6 → +150; 3..4 → +100.
5. + 20*dead VC + 45*dead NVA - 90*dead civilians.
6. Objectives: o1, o2, o3 = `score_eval_objective` of the three records; store them in the three flags;
   score += 150*(2*o1 + o2) + 100*o3; if o1 and o2: +500, and if also o3: +250 more.
7. For each objective of type 6 (Snatch): `score_snatch_casualties` returns -penalty*dead targets
   (penalties 200/100/50) and the caller *subtracts* it (net bonus — reproduce as is).
8. + min(200, minutes * (primary type == 1 ? 35 : 20)) with minutes = `tod_mission_minutes()`;
   + 100 * destroyed world objects (`wld_count_destroyed`); + 100 * captured VC/NVA; + 250 * S.3042.
9. If the team is not completely aboard the extraction craft (`ent_team_all_extracted` false) or the score
   is negative → 0. S.score = score.
10. Promotion: if rank < 11 and promo[rank] ≤ score + total (unsigned) → `roster_set_rank(pm, rank+1)`
    (rank 1 becomes 2).
11. If score ≥ 2500 (unsigned): for each of the first `alive` team units: random(100) < 30 → promote by one.
12. Purple Heart: every team unit with `med_wound_level(status) != 0` gets award bit 0x0001.
13. Medal of Honor: every MoH record with year == year, mission == index and min ≤ score → point man gets
    0x0080, medal flag set.
14. If no medal yet, try in order (`score_try_medal`: score ≥ a, prev_score ≥ b, total ≥ c, all signed;
    point man gets m1, then each of the first team_size units gets m2 with random(100) < 30):
    (3600, 3200, 10200, Navy Cross 0x40, Silver Star 0x20); (3100, 2500, 0, Silver Star 0x20, Bronze Star
    0x10); (2850, 2200, 0, Bronze Star 0x10, Navy Achievement 0x04); (2400, 0, 0, Navy Achievement 0x04,
    Navy Commendation 0x08); (1850, 0, 0, Navy Commendation 0x08, none). The first that succeeds stops the
    chain. (The manual quotes lower thresholds; the code values above are authoritative.)
15. Unit citations when the mission index is 18 or 19: year-KIA 1 or 2 → Naval Unit Citation 0x100<<year;
    year-KIA 0 → Presidential Unit Citation 0x1000<<year (the caller always passes 0).
16. Skill training when score ≠ 0 and the primary objective succeeded: for each of the team_size units and
    each of the 8 roster skill bytes +0x0a..+0x11: `score_train_skill` (r = random(100); if 90 - skill > r:
    skill += random(3)+1, capped at 90).

`roster_add_awards(unit, bits)` ORs into roster+4 and, for the point man, *assigns* the history awards word,
so only the last award given to the point man in steps 12–15 is recorded in the history (and shown on the
ribbons/messages). `score_eval_objective(type, …, group, arg)`: 1 Patrol → `wld_object_flag0_alt(arg)`;
2 Ambush → more than 65 % of the target group's units dead or captured (100*n/total > 65); 3 Demolition →
`wld_object_destroyed(arg)`; 4 Observe → `wld_objective_done(arg)`; 5 Rescue, 6 Snatch →
`ent_group_has_secured_member(group)`; 7 Recover → `wld_object_flag0(arg)`; anything else 0.
`score_reset_stats` (dbe6, called by the mission 1000:01de) clears S.3042, 303c, 303a, 3040, 303e, 3044.

## 13. Difficulty screen — `diff_screen()` (365e:e5bd)

For each setting s: option button list[s][value[s]] gets flags 0x12. `diff_enter` (pic_load(0) mapscr.pic,
focus 8 = Save). Loop until 53ba:3048: render (title " Difficulty  " at (0x78,4), buttons 52c0:0040 with
labels 0x4e6a), input. Keys a e i p r t w m → `diff_choose_option(s, key)` with s = 0,1,2,3,4,5,6,7; 's' →
save st1.dfr and leave; 'q' → leave; Alt-X → result 0. `diff_choose_option`: buttons 0..9 flags 2,
button s flags 1, its options flags 1 and the current value 0x11; loop with `diff_option_handle_input`
until 53ba:304a; then buttons 0..9 flags 1, options 2, current 0x12. Option keys per setting: Ammo u/r,
Enemy Wounds d/h/r, Intelligence m/d/r, Player Wounds n/d/r, Reload i/t, Team Size d/r/e, Weapons u/r,
Map f/r → value index in that order; pressing the setting's own key again closes without change.

## 14. Open questions

* 0x3042 in the statistics block (+250 each) — which event increments it (not in this half).
* Font header words +0x10, +0x12, +0x18 are not used by this code (probably line spacing / ascent).
* The purpose of group 0 +0x2c = 0x0003e800 set by `dbrf_init`, and of the dword pairs 0xef16/0xef1a.
* `cf50`'s music handling makes the extraction scene end as soon as the music sequence is done; verify
  against the game whether sequence 0/1/2 are long enough.
* Campaign portraits on the campaign screen use the *current* campaign year for the prt2 frame set
  (f = year>>1), not the recruit's starting year — confirm visually.
* 0xd872/0xd873 ("Meters" readout during the debrief) is shared with the briefing camera code of 365e_a.
