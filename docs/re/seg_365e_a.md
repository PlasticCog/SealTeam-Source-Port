# Code segment 365e, offsets 0000-7fff

Segment `365e` is the largest code segment of `st.exe`. Its lower half (this
document; the upper half 8000-ffff is documented separately) contains several
logically distinct groups of functions. By address range:

| Range | Purpose |
|---|---|
| 000c-11a8 | **Mission world construction**: groups, units, weapon/item lists, SEAL team, craft, enemy teams (MTM) |
| 129a | `msn_build_world` - the top level spawner |
| 1544-1e8d | Group management at run time (split/merge team, extraction order, leader replacement) and end-of-mission statistics |
| 1f28-225b | Camera waypoint queue used by the 3D **mission-briefing fly-over** |
| 227c-29c5 | Mission file loading (`cYmNN.mci/.mtm/.s`), objective evaluation |
| 2af6-3712 | Generic UI: dialog/prompt box, bevelled panels, title tabs, button lists, cursor and hit testing, screen transitions |
| 3760-4b57, 4c1d | **Campaign / personnel**: s.cnf, cN.cmp save/load, roster, point-man history, mission recording and campaign progression, default team and loadout |
| 4643-4918 | Y/N prompts, s.cnf / st1.dfr / s.rst I/O |
| 4c7a-6e0c | **Mission Briefing screen** (3D briefing, Patrol Order and Marching Order clipboards) |
| 6fe6-71d3 | **Main menu** |
| 7290-7b3e | **Intel Briefing / Practice Mission** screen |
| 7e14-7f74 | Two-part voice sample player (`newNa.voc`/`newNb.voc`) used by the recruit screen in the upper half |

Scoring, medal/citation awards, promotions, skill improvement, the debriefing,
campaign screen (save/load dogtags), recruit selection and the difficulty
screen are **not** in 0000-7fff; they live in 365e:8000+ (for example
`365e:b511` post-mission processing, `365e:dcea`/`365e:deb0` award checks,
`365e:dd6f` skill gain, `365e:8095` recruit screen, `365e:9aa6` campaign
screen, `365e:e5bd` difficulty screen). This half only provides the helpers
they call (`cmp_record_mission`, `roster_add_awards`, `roster_set_rank`,
`roster_record_casualty`, see section 8).

Notation: `S:O` addresses are Ghidra addresses; `DS:xxxx` is DGROUP; far data
segments are written `5178:0060`. The decompiler hides `ES` loads from the
DGROUP slots `0xcd5c..0xcdf8` (each holds the segment of one far variable);
statements like `*(char*)0x63 = ...` in the decompiled C really address a far
segment. The ones used here:

| Slot | Segment | Content |
|---|---|---|
| cd80, cd82, cd94, cd9c | 5178 | loadout table (+0000), s.cnf image (+0060), difficulty (+0148) |
| cd84 | 5149 | current contact record (enemy sighting) |
| cd90 | 5170 | briefing camera waypoint ring (8 x 16 bytes) |
| cd98 | 518e | briefing / marching-order button lists |
| cd9e | 53ba | briefing camera target positions (+2230..+226b) |
| cda0 | 52e3 | tool table |
| cda2 | 51b3 | main-menu button list |
| cda4 | 51b7 | intel buttons (+0000) and mission map positions (+0050) |

All world coordinates are 32-bit **24.8 fixed point** (x, y=altitude, z);
spawn functions receive `pos >> 8` (whole units). Bearings from `1000:3300`
are degrees 0..359; object headings are stored as `degrees << 3`. Random
numbers come from `2255:78d6` = `rand() % n` with `n` passed in AX.

---------------------------------------------------------------------------

## 1. File formats

All files are loaded through the library loader `4edb:0154/013a` (loose file
first, then the EALIB archives) or written with `4eaf:012f`. Multi-byte values
are little endian.

### 1.1 Mission header `cYmNN.mci` (msns.lib, exactly 0x176 = 374 bytes)

Y = year 1..4 (1966..1969), NN = mission 01..20 in that year. Mission number
`m = (Y-1)*20 + (NN-1)` (0..79) is `g_mission_no` (DS:eebc). The file is read
into DS:ed32 (`g_mci`, pointer `g_mci_ptr` DS:eeb8); a size other than 0x176
is a fatal error.

| Off | Type | Meaning (verified on all 80 files) |
|---|---|---|
| 00 | u16 | mission start hour (0..23), becomes the mission clock (1000:1964) |
| 02 | u16 | mission start minute |
| 04 | u16 | area / world index (0..26, names at DS:1cd0 'mekong1'...'bassac4'); selects terrain, 'map' and 'camp' sprite frames |
| 06 | u16 | insertion method: 1, 2, 4 = boat, 8 = helicopter |
| 08 | u16 | extraction method (same codes) |
| 0a | i32[3] | insertion point x, y, z |
| 16 | i32[3] | extraction point x, y, z |
| 22 | char[40] | insertion point description (not used in 0000-7fff) |
| 4a | char[40] | extraction point description |
| 72 | u16 | fire-support unit: 0 none, 1 OV-10 pair, 2/4 boat pair, 8 helicopter pair |
| 74 | u16 | break-contact unit (same codes, 0 none) |
| 76 | OBJ | objective 1 (primary) |
| d8 | OBJ | objective 2 (secondary) |
| 13a | OBJ | objective 3 ("extra"); only its first 0x3a bytes exist - its location text overlaps the next field |
| 174 | u16 | number of MTM team records to spawn (equals the .mtm record count in all 80 missions) |

Objective record OBJ (0x62 bytes):

| Off | Type | Meaning |
|---|---|---|
| 00 | u16 | type: 0 none, 1 Patrol, 2 Ambush, 3 Demolition, 4 Observe, 5 Rescue, 6 Snatch, 7 Recover |
| 02 | i32[3] | objective position (camera target, bearings for craft placement) |
| 0e | i16 | target team: MTM record index (group `g_first_mtm_group + n`), -1 none (Ambush/Rescue/Snatch) |
| 10 | i16 | target structure: index into `g_structures` (DS:26f4), -1 none (Patrol/Demolition/Observe/Recover) |
| 12 | char[40] | target description ("VC patrol near the clearing.") |
| 3a | char[40] | route text used as "You will move <text>" |

### 1.2 Mission teams `cYmNN.mtm` (msns.lib, n x 0x66 bytes, n <= 15)

Loaded (allocated) to `g_mtm` (DS:39ce), `g_mtm_count = size / 0x66`; more
than 15 records is a fatal error. Record (field ranges checked over all 80
files):

| Off | Type | Meaning |
|---|---|---|
| 00 | u16 | team kind: 0 civilian (group type 6, unit class 8), 1 VC (4, class 3), 2 NVA (5, class 4), 3 friendly (7, class 9) |
| 02 | i32[3] | start position x, y, z |
| 0e | u16 | member count (1..5 in the data) - **modified in place** by the team-size difficulty |
| 10 | u16[8] | member personnel numbers (1-based vcNN index, clamped to 1..21; 0 therefore means vc01). Only the first `count` are used - about 40 records list more ids than their count |
| 20 | u16 | group parameter copied to group+0x20 (0..2; formation) |
| 22 | u8 | alertness class 1..3 (3 = starts crouched/prone, see 5.4) |
| 23 | u8 | 0x10/0x20/0x30 - consumed by the AI (4a37:0425) |
| 24 | u16 | 0..7 - AI parameter (4a37) |
| 26 | u16 | 0/1 - AI parameter (4a37) |
| 28 | u16 | initial heading of the first member, degrees |
| 2a | i32[3] x5 | five patrol waypoints (x, y, z); unused ones are zero (read by the AI) |

### 1.3 Personnel records `*.SE` (se.lib, 0x90 = 144 bytes)

Files: `seal01..48.se` (SEALs), `vc01..21.se`, `civ01.se`, `frnd01..09.se`.

| Off | Type | Meaning |
|---|---|---|
| 00 | char[12] | first name |
| 0c | char[20] | last name |
| 20 | char[26] | nickname (NPCs: descriptive tag, e.g. "Avg-Rifleman-66") |
| 3a | char[32] | birthplace (terminated by '\n' + NUL) |
| 5a | u8 | height, feet |
| 5b | u8 | height, inches |
| 5c | u8 | weight, lbs |
| 5d | u8 | biography number, probably the BUD/S class (0xff for some SEALs) |
| 5e | u8 | Navy rating / billet 0..12 (abbreviations DS:1bc4: '' ELM BM TM SG EM MM GM AO DV HC RM 2IC; long names DS:1baa: '', Electrician's Mate .. Radioman, Second-in-Command). Entry 12's strings are stored without terminator and run into the following 'OIC' / 'Officer-in-Charge', so the original prints '2ICOIC' |
| 5f | u8 | age |
| 60 | u8 | camouflage (DS:2536: Tiger Stripe, Leopard Spot, Jungle Leaf, Blue Jeans, Black Pajamas, Tiger Stripe 2) |
| 61 | u8 | rank 0..11 (DS:2506 / abbreviations DS:251e; index 1 is an empty placeholder) |
| 62 | u8 | copied to loadout byte 1; for cached NPC records overwritten at run time with NPC index+1 |
| 63 | i8[8] | weapons (weapon-table ids, 0xff terminated); SEALs: default slots A,B,C,D = [0..3] |
| 6b | i8[8] | tools/items (0xff terminated) |
| 73 | u8 | 0 |
| 74 | u8[13] | attributes: Rifle, Pistol, Mortar, Shoulder, Automatic, Throw, Observe, Radio, Size, Strength, Agility, Intelligence, Time In Service (names DS:24ec) |
| 81 | u8[15] | zero in the files; the 28 bytes 0x74..0x8f are the unit "stats block" at run time: +0x8c (word) = carried load in 1/10 lb |

### 1.4 Text files `*.S` (str.lib)

Fixed-width records, NUL padded:

* `cYmNN.s` - 24 lines x 80 bytes (1920 bytes). Lines 0-4 intel briefing;
  5-6 comment shown when the previous mission was won; 7-8 comment when it
  was lost; 9-13 bull session; 14-18 debriefing after success; 19-23
  debriefing after failure. Loaded by `msn_load_text` into `g_mission_text`
  (DS:ed26).
* `sealNN.s` - 6 records x 84 bytes: char[80] line + 4 bytes date window
  (year_from, tier_from, year_to, tier_to; tier = flow-table stage 0..5; all
  0xff = always). Bull-session lines per SEAL, used by the upper half;
  `365e:2319` can load one but has no caller.
* `HyMNN.s` (28 x 80, historical report), `RyMNN.s` (16 x 80), `C.S`
  (credits, 28 x 80) are handled elsewhere.

### 1.5 Campaign save `cN.cmp` (N = 0..8; 8 = automatic save of the running game)

`0x14c + n*0x18` bytes (908 bytes for the usual 24 roster entries).

| Off | Size | Content |
|---|---|---|
| 000 | 0x14c | campaign header (section 2.1) - byte 0 is replaced by the slot number on load and save |
| 14c | n x 0x18 | roster entries (section 2.2); the far pointer at +0x14 is garbage in the file |

`cmp_load` (365e:3a55): read, copy header to DS:3b0e, set header[0]=slot,
then for each 24-byte record (count = (size-0x14c)/0x18, max 40) allocate an
entry, copy it, load `sealNN.se` for entry[0] and write entry[3] (rank) into
SE+0x61. Returns 1, or 0 when the file is missing or slot is not 0..8.
`cmp_save` (365e:3bc4): header[0]=slot; buffer = header + all entries; write;
if slot < 8 set `slot_used[slot]=1` (s.cnf is written by the callers).
The shipped `c0..c8.cmp` are developer leftovers and parse correctly with this
layout (e.g. c5.cmp: year 1, 10025 points, 6 history entries).

### 1.6 `s.cnf` (232 = 0xe8 bytes, image of 5178:0060..0147)

| Off (file) | Far addr | Content |
|---|---|---|
| 00..02 | 5178:0060 | unused |
| 03 | 5178:0063 | last used campaign slot (-1 none); "Continue Campaign" is disabled when -1 |
| 04..0b | 5178:0064 | slot_used[8] |
| 0c | 5178:006c | char name[8][26] campaign names (shown in "Load Campaign "<name>" ?") |
| dc..e7 | 5178:013c | unused |

Written by `cfg_save_s_cnf` whenever the last slot changes or a slot is
deleted; read by `cfg_load_s_cnf` (missing file => last slot -1). The shipped
file has all slots unused and stale names.

### 1.7 `st1.dfr` (16 bytes, image of 5178:0148)

Eight u16 difficulty settings (the Difficulty screen in the upper half edits
them): 0148 Ammo (0 Unlimited, 1 Real), 014a Enemy Wounds (0 Death, 1 Heavy,
2 Real), 014c Intelligence (0 Minimal, 1 Decreased, 2 Real), 014e Player
Wounds (0 None, 1 Decreased, 2 Real), 0150 Reload Time (0 Instant, 1 Timed),
0152 Team Size (0 Decreased, 1 Real, 2 Enhanced), 0154 Weapons (0 Unlimited,
1 Real), 0156 Map (0 Freeze, 1 Real). The shipped file is `1,2,2,2,1,1,1,1`
(everything "Real").

### 1.8 `s.rst` (0x55 bytes)

`cfg_save_s_rst` (365e:4918, no caller) writes 5178:0000..0054 (the team
loadout table and following bytes). No reader was found.

### 1.9 Other resources referenced here

* Palettes: `1959:01d5(n)` loads `<name>.pal` with n indexing DS:0102
  (0 pal2, 1 paln, 2 pals, 3 st, 4 se, 5 sm, 6 sn, 7 sl, 8 si, 9 sb, 10 so,
  11-16 c0..c5, 18-21 remap2..5); `1959:0136(n)` applies it. Used here: 0 pal2
  (3D briefing), 5 sm (main menu), 8 si (intel), 10 so (clipboards).
* Full-screen pictures `1000:1ccb(n)`: 0 mapscr, 1 title, 2 brf, 3 pwo, 4
  mainm, 5 intel, 7 debrf, 9 load, 10 new, 11-13 new2..4, 14-19 cm0..cm5, 20
  ea (`.pic`). Used here: 2 brf, 3 pwo, 4 mainm, 5 intel.
* RLE sprite sets via `348e:15f0(name, ...)`: `camp` (briefing scene backdrop,
  frame = DS:3178[area]) and `map` (intel area map, frame = region
  DS:3140[area]; frame 4 is drawn in the bottom-right corner).
* Voice: `newNa.voc`, `newNb.voc` with N = year+1 (new.lib).

---------------------------------------------------------------------------

## 2. Campaign data structures

### 2.1 Campaign header (DS:3b0e, 0x14c bytes; `g_campaign` DS:eec2 points to it)

| Off | Type | Meaning |
|---|---|---|
| 00 | i8 | save slot (0..8, -1/0xff = none/practice) |
| 01 | u8 | year 0..3 (1966..1969) |
| 02 | u8 | mission index in year 0..19; 0xff = campaign over |
| 03 | u8 | result of the last mission (1 won) - initialised to 1 |
| 04 | i16 | point man SE number (0..47) |
| 06 | i32 | total score |
| 0a | i32 | best single-mission score |
| 0e | char[28] | point man nickname typed at campaign start (empty = use SE nickname) |
| 2a | HIST[24] | point-man mission history, indexed by the point man's mission count |
| 14a | i8 | point man's remembered weapon for slot A |
| 14b | i8 | point man's remembered weapon for slot B |

HIST entry (12 bytes, at 0x2a + i*12):

| Off | Type | Meaning |
|---|---|---|
| 0 | u16 | mission number (year*20 + index) |
| 2 | i32 | mission score |
| 6 | u16 | award bits earned this mission (`roster_add_awards`) |
| 8 | u16 | casualty bits of the point man (`roster_record_casualty`) |
| a | u8 | new rank if promoted (`roster_set_rank`) |
| b | u8 | won flag (`cmp_mission_won`) |

Award bit n corresponds to name DS:2542[n]: 0 Purple Heart, 1 (unused), 2 Navy
Achievement Medal, 3 Navy Commendation Medal, 4 Bronze Star, 5 Silver Star, 6
Navy Cross, 7 Medal of Honor, 8+year Naval Unit Citation, 12+year
Presidential Unit Citation.

`cmp_state_init` (365e:378b): +0 = slot, +1 = +2 = 0, +3 = 1, +4 = point man
argument, +6 = 0 (dword), +0e = 0. `cmp_state_reset_for_mode` (365e:37be):
modes 2 and 3 -> slot -1, point man 0; mode 1 -> slot = last slot from s.cnf
(8 becomes 0); mode 6 -> slot -1, then year = g_mission_no / 20 and mission =
g_mission_no % 20.

### 2.2 Roster entry (0x18 bytes; up to 40 far pointers at DS:3a6a, null terminated)

| Off | Type | Meaning |
|---|---|---|
| 00 | u8 | SE number (0..47 -> sealNN.se, NN = n+1) |
| 01 | u8 | missions flown (also the HIST index for the point man) |
| 02 | u8 | missions won |
| 03 | u8 | rank (mirrors SE+0x61) |
| 04 | u16 | accumulated award bits |
| 06 | u16 | (unused here) |
| 08 | u16 | accumulated casualty bits (0x4000 = killed in action) |
| 0a | u8[8] | weapon skills (Rifle..Radio), copied from SE+0x74..0x7b, improved in the upper half |
| 12 | u8 | flags: 0x01 in the current team, 0x02 wounded last mission, 0x04 reserve recruit not yet available, 0x08 killed |
| 13 | u8 | - |
| 14 | SE far * | loaded sealNN.se record (run time only) |

`roster_new(pm)` (365e:3d99): clear the table, header[0] = -1; if pm >= 0,
base = 0 if pm < 24 else 24; add entries for SE numbers base..base+23 and set
flag 0x04 on the last six (base+18..base+23). `roster_entry_init` zeroes the
entry, stores the id, loads the SE file and copies SE+0x74..0x7b to +0a and
SE+0x61 to +03. `roster_activate_recruit` (no caller here) clears the flags of
the first entry with 0x04 in the current pool and returns its index.
`roster_skill_rating` = (sum of the 8 skill bytes) >> 3.
`roster_count_kia` counts entries with casualty bit 0x4000.

### 2.3 Team loadout table (5178:0000, 4 records of 12 bytes, 0xff at 5178:0030)

| Off | Meaning |
|---|---|
| 0 | SE number of the team member (0xff terminates the list) |
| 1 | SE+0x62 |
| 2..5 | weapon ids for slots A, B, C, D (-1 empty) |
| 6..9 | reloads (magazines) for slots A..D |
| a, b | tool ids for slots E, F (-1 empty) |

Member order = marching order: 0 Point Man (player), 1 Officer-in-Charge, 2
Corpsman, 3 Rear Security (names DS:1c8c).

### 2.4 Weapon table (DS:48fe, 34 entries of 0x22 bytes) - fields used here

| Off | Meaning |
|---|---|
| 00 | short name (e.g. "M16A2") |
| 02 | long name ("M16") |
| 06 | availability mask: bit y (0..3) = SEAL-selectable in year y (1966+y); bits 4,5 = enemy weapons |
| 0e | magazine capacity (rounds per reload) |
| 10 | fire modes: 0x01 Single, 0x02 Semi, 0x04 Full, 0x08 Grenade launcher, 0x10 Throw, 0x20 Buckshot |
| 18 | weapon weight, 1/10 lb |
| 1a | weight of one reload, 1/10 lb (0 = use +18) |
| 20 | default / maximum number of reloads |

Weapon ids used by name in this half: 4 M39, 6 M60, 9 M79, 12 DEMO, 14 M26,
16 M18 smoke, 19 AK47, 26 SG1, 29 Minigun, 30 Rocket, 32 Mk18.

### 2.5 Tool table (52e3:0000, 6 entries of 6 bytes: short name, long name, weight 1/10 lb)

0 PRC25 Radio (45), 1 Med - Medical Kit (15), 2 PHK - Prisoner Handling Kit
(8), 3 Flare (10), 4 Nite - Night Vision Scope (30), 5 Docs - Documents (3).
The Marching Order editor only offers -1 (empty) and tools 0..2.

### 2.6 Campaign flow table (far segment 5275, file offset 0x46440)

`g_campaign_flow` (DS:3c5a) holds four far pointers (5275:0000, :0118, :0230,
:0348), one per year, each to 20 entries of 14 bytes (7 x u16):

| Off | Meaning |
|---|---|
| 0 | stage 0..5 in the year (missions 0 / 1-2 / 3-6 / 7-10 / 11-14 / 15-19) |
| 2 | month index (1..12, DS:1b90) shown on the intel calendar and Patrol Order |
| 4 | day of month (not used in this half; the screens here show index+1 instead) |
| 6 | time as HHMM (not used in this half) |
| 8 | next mission index if this mission is lost |
| a | next mission index if it is won |
| c | bit 0: a historical report `HyMNN.s` exists; bit 1 set only on year 1 mission 2 (meaning unknown) |

The graph is a binary tree by stage: a win moves to the harder branch; stage-5
missions have next = 0, which makes `cmp_record_mission` advance to the next
year (or end the campaign after 1969). Six missions are flown per year.

---------------------------------------------------------------------------

## 3. Mission world run-time structures (as far as this half uses them)

### 3.1 Group (allocated by `4511:03d0`)

`g_groups` (DS:12e2) is a null-terminated array of far pointers;
`g_group_count` (DS:ec87). A group starts with a null-terminated array of up
to 8 unit far pointers ([0] is the leader), followed by:

| Off | Meaning |
|---|---|
| 20 | parameter 1 (SEAL 0, insertion craft/air/helo 3, support boats 0, MTM +0x20) - formation |
| 22 | parameter 2 (2 for all groups created here) |
| 24 | status / order: 4 craft idle, 3 extraction run ordered, 2 in progress (see 1875), -1 SEAL team, 0 MTM teams |
| 26 | group type: 0 SEAL, 1 boat, 2 helicopter, 3 aircraft, 4 VC, 5 NVA, 6 civilian, 7 friendly |
| 28 | far pointer to the group AI block (4a37) |
| 2c | i32 range: 0x3e800 for SEALs, 0x7d000 for all others |
| 30 | u16 0x60, +32 u16 0xffff |

Group-index globals filled while spawning: `g_seal_group` (ec88, always 0),
`g_boat_group` ec89, `g_helo_group` ec8a, `g_air_group` ec8b,
`g_insertion_group` ec8c, `g_extraction_group` ec8d, `g_emergency_group`
ec8e, `g_first_mtm_group` ec8f, `g_fire_support_group` ec90,
`g_break_contact_group` ec91, `g_active_extraction_group` ec92.

### 3.2 Unit (allocated by `4511:019e(flags)`, 0x34 bytes)

| Off | Meaning |
|---|---|
| 00 | model id (set by 19ac:612a from the unit class) |
| 02 | far pointer to the world object (2255:0234): +06 position (3 x i32), +12 heading (deg<<3) |
| 06 | stats block (flag 1, 0x1c bytes): +00..0c copy of SE+0x74.. (skills overridden by roster skills), +0e u16 casualty bits (0x4000 = KIA), +14 u8, +18 u16 carried load |
| 0a | movement block (flag 2, 0x48 bytes): +0c/+0e heading, +18, +25, +26 flags (0x10 searched/captured, 0x20 secured by the team, 0x40 aboard extraction craft), +27 flags (0x02 on a structure tile, 0x08 objective satisfied), +28 destination (3 x i32) |
| 0e | animation block (flag 4, 0x26 bytes, 348e) |
| 12 | AI block (flag 8, 0xd0 bytes): +66, +68 group pointer, +6c |
| 16 | weapon list header (12 bytes): +0 first node, +4 current node, +8 second weapon |
| 1a | item list head |
| 1e | owning group |
| 28 | roster entry (SEALs) |
| 2c | SE record |
| 30 | u32, zeroed |

Unit classes (argument of 19ac:612a): 0 SEAL, 3 VC, 4 NVA, 5 boat, 6
helicopter, 7 aircraft, 8 civilian, 9 friendly.

Weapon node (0x12 bytes, `ent_weapon_init`): +0 id, +2 rounds in magazine =
table+0e, +4 reloads (8 when -1 is passed), +8 = 0, +a = 3, +c fire mode:
table+10; if its low nibble is non-zero choose the first present of Semi (2),
Single (1), Full (4), Grenade (8); +e next node (far).
Item node (8 bytes): +0 id, +2 quantity (8 when -1), +4 next node.

---------------------------------------------------------------------------

## 4. Mission loading

`msn_load` (365e:237e): copy the template "cNmNN" into DS:eea8, set char 1 =
'1' + m/20, chars 3,4 = two digits of (m%20)+1; load `<name>.mci` into DS:ed32
(must be 0x176 bytes), load `<name>.mtm` (allocated) into `g_mtm`, set
`g_mtm_count`; any error -> fatal "Not enough memory." exit (4eac:001c).
Returns DS:ed32.

`msn_load_text(name, m)` (365e:227c): name NULL -> build "cYmNN" as above
from m; append ".s"; return the loaded buffer.

## 5. World construction (`msn_build_world`, 365e:129a)

Called from 1000:4580 when a mission (or the briefing scene) is set up.

1. Clear the group table (ec86 = ec87 = 0, ec8f = 0xff), 19ac:61e8,
   ec88 = 0.
2. `ent_spawn_seal_team` -> group 0.
3. All craft role globals = 0xff, `g_split_groups` = 0.
4. `ent_spawn_insertion_craft(MCI.ins_method, ins.x>>8, ins.z>>8)`;
   ec8c = last group. craft = 1.
5. If extraction method == insertion method: if the extraction group is a
   boat group, copy the extraction point into its leader's movement
   destination. Otherwise spawn a second craft with
   `ent_spawn_insertion_craft(MCI.ext_method, ext.x>>8, ext.z>>8)`; craft = 2.
6. If MCI+72: `ent_spawn_support_craft(MCI+72, ins.x>>8, ins.z>>8)`,
   ec90 = last group, craft++.
7. If craft < 3 and MCI+74: `ent_spawn_support_craft(MCI+74,
   (ins.x>>8)+1200, (ins.z>>8)+1200)`, ec91 = last group.
8. If no emergency helicopter exists (ec8e == 0xff):
   `ent_spawn_support_craft(8, ext.x>>8, ext.z>>8)`.
9. If ec90 == 0xff: ec90 = ec8e.
10. Clear the NPC SE cache, ec8f = group count; spawn one team per MTM
    record 0..MCI+174-1 (`ent_spawn_mtm_team`).
11. `ent_spawn_hidden_grenadier`; ec93 = type of the extraction group.

(2dbd:6371(group index) is called after each group is created; its meaning is
outside this segment.)

### 5.1 `ent_group_create(type, p20, p22, p24, flags)` (365e:05a3)

Append a new group, increment ec87, store the four parameters, set +2c to
0x3e800 (type 0) or 0x7d000 (others), +30 = 0x60, +32 = 0xffff. If flags bit
3: AI setup - 4a37:0372(group) when ec8f == 0xff, else
4a37:0425(group, &g_mtm[index - ec8f], index, ec8f).

### 5.2 `ent_group_add_unit(group, flags, class, se, x, z)` (365e:0459)

Allocate the unit with components per flags (1 stats 0x1c bytes, 2 movement
0x48, 4 animation 0x26, 8 AI 0xd0; bit 0x10, always set by the callers, is
not used by the allocator), append it to the group, set
unit+1e = group, 19ac:612a(unit, class, se) (model), vehicles (class 5/6) get
se = -1 afterwards, create the world object at (x, 0, z) with 2255:0234 and
register it (19ac:61fa), then attach: flag 2 -> 2dbd:261e, flag 4 ->
348e:158c, flag 1 -> `ent_unit_init_personnel`, flag 8 -> 4a37:061c.

`ent_unit_init_personnel(unit, class, se)`: 19ac:8617 first. se == -1 ->
nothing. class < 3 (SEAL): roster = `roster_find(se)`; unit+28 = roster,
unit+2c = roster SE; copy SE+0x74..0x8f (28 bytes) to the stats block, then
overwrite its first 8 bytes with roster skills +0a..+11; stats+14 = 0;
19ac:85fa(stats, roster missions). Otherwise class 8 forces se = 21 and class
9 forces se = 22; SE = `npc_se_load(se)`; unit+28 = 0, unit+2c = SE; copy
the 28 bytes; SE+0x62 = se+1; stats+14 = 0. Finally the stats fields +0e,
+10 (words), +12, +13, +14 (bytes) and +18 (word) are cleared.

`npc_se_load(i)`: 0..20 -> vc(i+1), 21 -> civ01, 22..30 -> frnd(i-21), >=31 ->
vc01 (cached as entry 0); records are cached in DS:3c6a[31].

### 5.3 `ent_spawn_seal_team` (365e:0d8b)

Group(type 0, p20 0, p22 2, p24 -1, flags 8). For each of the 4 loadout
records r (index k): unit = add_unit(flags k==0 ? 0x17 : 0x1f (the player gets
no AI), class 0, se r[0], insertion point); for slots 0..3 with r[2+i] != -1:
`ent_unit_add_weapon(unit, r[2+i], r[6+i])`; for tools 0..1 with r[10+i] !=
-1: `ent_unit_add_item(unit, r[10+i], 1)`.

### 5.4 `ent_spawn_mtm_team(rec)` (365e:0ea6)

1. kind -> (group type, unit class): 0 -> (6, 8), 2 -> (5, 4), 3 -> (7, 9),
   otherwise (1) -> (4, 3).
2. Group(type, rec+20, 2, 0, flags 8).
3. Team size (5178:0152): if the setting is 0 (Decreased) count =
   max(count-1, 1); then **always** count = max(count-1, 1). (So Real and
   Enhanced both remove one member; Decreased removes two. The record is
   changed in place.)
4. For each member i < count: se = clamp(rec.member[i], 1, 21) - 1;
   unit = add_unit(flags 0x1f, class, se, rec.x>>8, rec.z>>8);
   first SE weapon with reloads `g_enemy_reloads_by_year[year]` (5,6,7,8);
   further SE weapons (until 0xff, max 8) with half that; if the SE had only
   the first weapon add weapon 26 (SG1) with 8 reloads (-1 -> 8); SE items
   (until 0xff) with quantity 1, none -> item 5 (Documents).
   Member 0: world heading = rec+28 << 3.
   Stance (2dbd:0307): alertness 3 -> rand(2)+1, then 348e:00c8(unit,
   move+25) and 19ac:61a7(unit); else rand(2) and 348e:00c8.
   Friendly teams (class 9): stance 0, 348e:00c8, AI+66 = 1.
   (Stance values appear to be 0 upright, 1 crouch, 2 prone.)

### 5.5 Insertion / extraction craft (`ent_spawn_insertion_craft(method, x, z)`, 365e:0aa5)

Weapon reloads come from `g_craft_reloads_by_year[year]` (7,6,5,4).

* method 8 (helicopter): group(type 2, 3, 2, 4, 8); unit class 6 (se -1) at
  (x, z), flags 0x1b. If this is the insertion craft (MCI.ins_method == 8):
  world y = 0x600 (hovering), move+18 = 6, and its destination x and z are
  each moved 2700 units (0xa8c<<8) *away* from the primary objective (+2700
  when destination >= objective coordinate, else -2700); otherwise
  `ent_unit_push_back_from_objective(unit, 2700)`. Weapons 6 (M60) and 32
  (Mk18). ec8a = ec8d = ec8e = this group.
* methods 1, 2, 4 (boat): group(type 1, 3, 2, 4, 8); unit class 5 (model
  from se: method 1 uses se 0 = first boat model, others se -1 = second
  model) at (x + (method == MCI.ins_method ? 0 : 1350), z); 2dbd:287f; if
  this is not the insertion craft, the world position is set to its movement
  destination. Heading = bearing(position -> destination), stored in move+0c,
  +0e and world+12 (<<3); the position is then advanced 0xb400 (180 units)
  along that heading (4e87:0006(dist, 0, angle, pos) moves a position by dist
  in direction angle); weapons 6 (M60) and 9 (M79). ec8d = ec89 = this
  group.

### 5.6 Support craft (`ent_spawn_support_craft(kind, x, z)`, 365e:071c)

* kind 1 (OV-10): group(type 3, 3, 2, 4, 8); two class-7 units (se -1) at
  (x, z) and (x+600, z); unit 1 gets weapon 30 (Rocket) twice, unit 2 gets 30
  and 6 (M60); each gets world+16 = 0xaf0 and is pushed back 6750 units from
  the objective. ec8b = group.
* kind 8 (UH-1): group(type 2, 3, 2, 4, 8); unit 1 (class 6, se -1) at (x, z)
  with weapons 6 (M60) and 30 (Rocket); unit 2 (class 6, se 0 = other model)
  at (x+600, z) with 6 and 6; each pushed back 2700 units. ec8a = ec8e =
  group.
* kind 0, 2, 4 (boats): group(type 1, 0, 2, 4, 8); boat 1 (class 5, se -1) at
  (x+1350, z) with weapons 29 (Minigun) and 6 (M60); boat 2 (class 5, se -1)
  at (x+1950, z) with weapons 6 and 32 (Mk18); each world position is set to
  its movement destination after 2dbd:287f. ec89 = group.

(The first weapon of several of these units is passed through arguments left
on the stack by the preceding call; the decompiler hides this, the list above
is taken from the disassembly.)

`ent_unit_push_back_from_objective(unit, d)` (365e:0693): a = bearing(unit ->
objective 1) + 180; move the world position d units in direction a
(4e87:0006 with d<<8) and copy it into the movement destination.

### 5.7 Proxy grenadier (365e:11a8)

A hidden VC group (type 4, 0, 2, 0, flags 8, no MTM data), AI block +82 = 3,
+83 = 0x20, +0c = 0x70800; one VC (class 3, se 0) at (35, 35) with AK47 and
M26 (8 reloads each), prone, AI+6c = 0. Stored in `g_proxy_grenadier`
(DS:134a); 1000:5114 moves it next to a target and fires the grenade (used for
scripted explosions).

### 5.8 Teardown (`ent_free_world`, 365e:18c2)

Free the NPC SE cache; for groups from last to first: 4511:0480(group), for
members from last to first free the item list, the weapon nodes and header,
the unit (4511:00a4); free the group (4511:037d); clear the table slot.

## 6. Run-time group helpers and statistics

* `ent_group_split(g)`: only for type-0 groups with >= 2 members. New group
  with the same type/p20/p22/p24 (flags 8, created with ec8f forced to 0xff);
  members from index ceil(n/2) to n-1 move to it (unit+1e and AI+68 updated);
  `g_split_groups`++; returns the new index (or -1).
* `ent_group_merge(a, b)`: both type 0 -> append b's members to a, free b,
  clear its table slot, ec87--, g_split_groups--. Returns a or -1.
* `ent_team_rejoin`: if the selected group (ec86) **is** a SEAL group select
  0 (corrected: at 365e:17BF the JNZ skips the reset for non-SEAL groups; the
  first version of this note had the test inverted); if g_split_groups is 2
  or 1 merge the last group into group 0 (up to two times).
* `ent_order_extraction(pos)`: group = ec8e if present else ec8d; ec92 =
  group; group+24 = 3; copy pos to `g_extraction_point` (DS:0ea0) and to the
  leader's movement destination; `ent_team_rejoin`.
* `ent_any_craft_moving`: any boat/helo group with status 2 or 3.
* `ent_group_replace_leader(group, unit)`: if unit is member 0, swap it with
  the first member that is not KIA and has no movement flag 0x20.
* `ent_team_enemy_sighted` (used for the "Enemy sighted." message): true if
  the contact record 5149:+12 is 0 and its unit (5149:+0e) belongs to a VC/NVA
  group, or if any non-leader member of group 0 reports a sighting
  (4a37:289e on its AI block).
* `ent_mark_enemies_on_structures`: VC/NVA units whose tile object
  (1000:3e85) has kind 7, 8 or 9 get movement flag 0x02.
* Counters: dead units of a type (`stat_count_dead`, stats+0f bit 0x40),
  wounded (`stat_count_wounded`, 19ac:87b8 wound score > 0),
  `stat_count_captured_enemies` (VC/NVA with movement+26 bit 0x10),
  `stat_no_craft_lost` (no dead unit in groups of type 1..3).
* `msn_tally_casualties` (called at mission end by 19ac:00b6): merge all
  split groups; if the leader has movement flag 0x40 call 2dbd:31ec(group 0,
  0x40); clear roster bit 0x02; for every member of group 0
  `roster_record_casualty(unit, stats+0e)`; `g_enemy_kia` = dead VC + dead
  NVA; `g_seal_kia`; `g_seal_wia`; `g_mission_minutes` (DS:135e) = elapsed
  minutes = (clock_hour - MCI.hour)*60 - MCI.minute + clock_min + 1;
  `g_extraction_type` = type of group ec92 (4 when none).

## 7. Objectives (`msn_check_objective(obj)`, 365e:251d; all three via 365e:2853)

```
target = obj.structure >= 0 ? g_structures[obj.structure] : none
team   = obj.team >= 0 ? g_groups[ec8f + obj.team] : none
leader = g_groups[0][0]
d = 3000
if type in {1,3,4,7} and target and !(target.flags & 1): d = dist(leader, target)
if type in {2,5,6}   and team and !(team[0].move.flags27 & 8): d = dist(leader, team[0])
if d >= 3000: return 0
switch type:
 1 Patrol:     d < 450 and team not extracted     -> target.flags |= 1  "Patrol completed."
 2 Ambush:     d < 2400 and (alive(team) == 0 or team has a secured member)
                                                  -> team[0].flags27 |= 8 "Ambush completed."
 3 Demolition: d < 2400 and target.hp (+0a) <= 0   -> target.flags |= 1  "Demolition completed."
 4 Observe:    d < 1200 and ( (target+06 == 0 and target.hp <= 0)
                              or (target+06 != 0 and dead VC + dead NVA > 4) )
                                                  -> target.flags |= 1  "Observe completed."
 5 Rescue:     team has a secured member          -> team[0].flags27 |= 8 "Rescue completed."
 6 Snatch:     same as Rescue                     -> "Snatch completed."
 7 Recover:    d < 240 and team not extracted     -> target.flags |= 1
               "Cache marked.  Recover completed." and 1000:6990 (support pickup)
if completed and the team is not all aboard: 1000:7d6f(leader, 5) and
   4592:0135(sound 0x33, 0x200, leader pos, 1, leader)
return completed
```

Distances come from 1000:321f (world units). `alive(group)` is 19ac:6317
(members without KIA bit). "team has a secured member" is
`ent_group_has_secured_member` (movement flag 0x20). "team not extracted" is
`!ent_team_all_extracted` (every SEAL has movement flag 0x40). Messages use
colour/priority 0x400 (1000:7d4b). Note: the manual's "65 % of the ambush
target" is not what this code checks.

Related queries: `msn_is_objective_structure(s)` - s is the target structure
of an objective whose type is > 2; `msn_is_objective_team(g)` - g equals
g_groups[ec8f + obj.team] for any objective (team -1 compares with the group
just before the MTM teams); `msn_objective_demo_weapon(obj)` - 12 if
Demolition else -1; `msn_find_objective_target(type, pos)`: for each objective
of that type (2 and 6 use the team, 3 the structure; a team must have living
members, a structure must still be standing (1000:6648 == 0)) take its index;
with pos != 0 only candidates closer than 24000 count and the last such one
wins; with pos == 0 the last existing one is returned; -1 if none.

## 8. Campaign progression and personnel helpers

### 8.1 Recording a mission (`cmp_record_mission(campaign, score)`, 365e:3fd0)

Called from the upper half after the debriefing.

```
entry  = flow[year][mission]
pm     = roster_find(campaign.point_man)
campaign.total += score                         (score is sign-extended)
h = pm.missions++                               (history index)
hist[h].mission = year*20 + mission; hist[h].score = score
hist[h].won = won = (g_mission_success != 0 && score > 0)
if score > campaign.best: campaign.best = score
if won: campaign.mission = entry.next_win; pm.wins++
else:   campaign.mission = entry.next_loss
if campaign.mission == 0:
    if year == 3: campaign.mission = 0xff        (campaign complete)
    else: year++
if pm.flags & 0x08 (point man killed): campaign.mission = 0xff
if roster_count_kia() > 10: campaign.mission = 0xff
campaign.last_won = won
```

The history array has room for 24 entries (4 years x 6 missions).

### 8.2 Award / rank / casualty recording

* `roster_add_awards(unit, bits)`: roster+04 |= bits; if unit is the point
  man (g_groups[0][0]) hist[pm.missions].awards = bits.
* `roster_set_rank(unit, r)`: ignored unless 0 <= r <= 11; r == 1 becomes
  2; SE+0x61 = roster+03 = r; point man: hist[pm.missions].rank = r.
* `roster_record_casualty(unit, bits)`: roster+08 |= bits; if bits & 0x4000
  roster+12 |= 0x08 (the code also ORs 1 << (year+8) into this byte, which is
  always 0); else if bits != 0 roster+12 |= 0x02; point man:
  hist[pm.missions].casualties = bits.

### 8.3 Default team (`loadout_build_team`, 365e:45a8)

Member 0 = point man (campaign+04); members 1..3 =
`roster_pick_default_member(k)`; then `loadout_build_member(k, se)` for each;
terminator 0xff at 5178:0030.

`roster_pick_default_member(k)` (365e:4124): base = 0 if point man < 24 else
24; start = base+9 for the OIC, base+7 for the Corpsman, base + rand(3) for
Rear Security. Candidate c is accepted when c != point man and (c has no
roster entry, or its flags byte is 0 and (k != 1 or rank > 7)). Otherwise try
the next index cyclically inside base..base+23; after 24 failed steps any
index different from the point man is returned.

`loadout_build_member(k, se)` (365e:422f):

```
e = roster_find(se) (created if missing); e.flags |= 1
r = loadout[k]; r[0] = se; r[1] = SE+0x62
r[2] = SE+0x63; if k > 0: r[2] = fix_for_year(r[2]); r[6] = W[r[2]].reloads
r[3] = fix_for_year(SE+0x64);                          r[7] = W[r[3]].reloads
r[10] = r[11] = 1                                      (two Medical Kits)
r[4] = SE+0x65; if k > 0: r[4] = fix_for_year(r[4]);   r[8] = W[r[4]].reloads
r[5] = SE+0x66; if k > 0: r[5] = fix_for_year(r[5]);   r[9] = W[r[5]].reloads
k == 0 (point man):
    in campaign modes (not 3 practice / 6 demo): r[2], r[3] = campaign+14a, +14b
    d = DEMO if any objective is a Demolition (first one found in order 1,2,3)
        else SE+0x66
    r[5] = d; r[9] = W[d].reloads
    if a Snatch objective exists: r[10] = 2 (PHK)
    r[11] = 0 (PRC25 radio)
k == 1 (OIC): r[3] = 16 (M18 smoke), r[7] = W[16].reloads
    if a Demolition objective exists: r[4] = 12 (DEMO), r[8] = W[12].reloads
    r[5] = 4 (M39), r[9] = W[4].reloads
k >= 2: if Demolition: r[5] = DEMO, r[9] = W[12].reloads
        if Ambush:     r[4] = 4 (M39), r[8] = W[4].reloads
        k == 2 (Corpsman): r[10] = 2 (PHK)
```

`fix_for_year(w)` (`loadout_fix_weapon_for_year`, 365e:41e0) returns -1
unchanged, otherwise `loadout_next_weapon(loadout_next_weapon(w, year, down,
any), year, up, any)`; the net effect is: the smallest weapon id >= w
available in the year, else the largest available id < w, else -1.

`loadout_next_weapon(w, year, up, cls)` (365e:4b57): mask = bits 0..year set.
The first candidate is w+1 (up, if w <= 27) or w-1 (down, if w >= 0); when w
is already at the end (w > 27 going up, w < 0 going down) w itself is the
first candidate. Candidates step by +-1 while inside 0..28; the first id with
(W[id].mask & mask) != 0 that matches the class is returned: cls 1 = mode
!= 0x10 (not a thrown item) or DEMO; cls 2 = mode == 0x10 and not DEMO; cls
>= 3 = any. When the search leaves 0..28 the original w is returned, except
that leaving at -1 returns -1 (so stepping down past the first weapon gives an
empty slot).

`loadout_release_team` clears roster flag 0x01 of each member and sets
r[0] = r[2] = -1. `brf_leave` stores the point man's r[2], r[3] into
campaign+14a/14b.

### 8.4 Load (weight) and strength display

`loadout_compute_weight(stats, k)` (365e:5cee) adds (19ac:640c, clamped >= 0)
for every non-empty weapon slot: W.weight (+18) and reloads x (W+1a != 0 ?
W+1a : W+18); plus the weight of tool slots E and F. The total is stats+18
(= SE+0x8c), in 1/10 lb. 19ac:63b9 classifies it: lbs = load/10, cap = size/2
+ strength (SE+0x7c, +0x7d); lbs > cap -> Extreme, > cap/2 -> Heavy, > cap/4
-> Normal, else Light (DS:256a). The "STR:" word is
DS:25e4[min((rating - 50) / 10, 3)] (Green, Novice, Veteran, Elite) with
rating = `roster_skill_rating`.

### 8.5 Save slots and config

`cmp_slot_used(i)`, `cmp_last_slot()`, `cmp_set_last_slot(i)` (writes s.cnf),
`cmp_delete_slot(i)`: clear used flag; if it was the last slot, last slot =
highest remaining used slot (or -1); write s.cnf. `cmp_load_autosave` /
`cmp_save_autosave` use slot 8 but keep header[0]. `cfg_load_difficulty`
reads st1.dfr into 5178:0148 (16 bytes).

---------------------------------------------------------------------------

## 9. Screens

### 9.1 Game modes and the main loop (19ac:016d, for context)

`g_game_mode` (DS:d7e4): 2 main menu / new campaign, 1 continue campaign, 3
practice, 6 demo (plays a mission with no screens). In practice mode the loop
runs: `intel_screen` -> random point man (odd year index: SE 34+rand(8) with
pool base 25 -> SEAL25..48; even: 10+rand(8), pool 0), `roster_new`,
`loadout_build_team`, `cmp_save(8)`, `brf_screen` -> mission -> upper-half
debriefing -> back to the menu. The campaign loop (modes 1/2) runs: 0
recruit screen 365e:8095 (mode 2 only), 1 campaign screen 365e:9aa6, 2
`intel_screen`, 3 `loadout_build_team` + `cmp_save_autosave` + `brf_screen`
(returns 1 -> `loadout_release_team` and go back one step), 4 365e:abb9 (bull
session), 5 365e:d285, 6 the mission (1000:01de), 7 365e:d659, 8 365e:b511
(after which mode 2 becomes 1 and the loop restarts at step 0). Mode 6 skips
all screens. Screen functions return 0 = quit to DOS (Alt-X), 1 = back
(Esc), 2 = next.

### 9.2 UI toolkit

Button list: array of 16-byte records terminated by key == 0:

| Off | Meaning |
|---|---|
| 0, 2, 4, 6 | x, y, width, height |
| 8 | key code sent when activated (ASCII, or scan<<8) |
| a | index of the underlined hot-key letter (-1 none) |
| b | flags: 0x01 clickable, 0x02 disabled (hatched, not clickable), 0x04 label not drawn by 36c3, 0x10 always drawn highlighted, 0x20 extra outer frame, 0x40 invisible hot-spot |

Labels are a parallel array of near string pointers. `ui_draw_buttons`
centres the label horizontally ((w - textwidth)/2, never negative) in font
DS:eef8; normal buttons: frame 0x18, inner 0x16, face 0x14 (2255:25ce), dark
edges 0x12/0x10, label in colour 0x00 over a colour-0x0F shadow (black text,
light shadow: `font_draw_text_shadow(.., 0x0f, 0x00)`, verified from the pushes
at 365e:344c/35a0); focused (and pressed or
releasing) or flag 0x10: face 0x16, frame 0x18, edges 0x12/0x10 moved one
pixel; disabled: hatch pattern 0x5a14 over the face. When +0a != -1 a
hot-key underline is drawn under the first letter of the label (from label x
+ 2 over the width of that glyph): colour 0x0f at y+12 and 0x00 at y+11 for
focused buttons, one pixel lower otherwise (all lists here use index 0).
Colours 0xffNN are UI palette index NN.

Cursor: `g_cursor_x/y` (DS:ec70/ec6c) clamped to x 4..308, y 5..186.
`ui_pointer_update(dx, dy, list)` moves it, hit-tests (a button's hit box is
its rectangle with height-2; disabled or non-clickable entries are skipped;
while a release countdown is running the focus is kept) and stores the result
in `g_ui_focus` (DS:eebe); it returns 1 when a pressed button loses the
cursor. Enter on a focused button sets `g_ui_pressed`; the next non-Enter
input releases it and starts a 3-frame countdown (`g_ui_release_count`), after
which the screen's input handler is called again with that button's key.
`ui_cursor_to_button` warps to (x + w/2, y + h - 4).

`ui_draw_text_panel(str, x, y, w, h)`: fill (x-4, y-3, w+8, h+6) colour 0x18,
(x-3, y-2, w+6, h+4) colour 0x16, shadow lines 0x12/0x10 along the bottom and
right, face (x-2, y-1, w+4, h+2) colour 0x14 with the text (white/black) when
str is not NULL.

`ui_draw_title_tab(str, x, y)`: w = text width; four horizontal rules from
x-48 to x+w+48 at y+6 (0x16), y+7 (0x14), y+7 (0x12, one pixel shorter), y+8
(0x10); tab (x, y, w, 16) colour 0x14, (x+1, y+1, w-2, 14) 0x16, face (x+2,
y+2, w-4, 12) 0x14, edges 0x12/0x10, caption white on black. Titles: ' Main
Menu  ' (124, 4), ' Mission Briefing  ' (120, 4), ' Patrol Order  ' (120, 4),
' Marching Order  ' (112, 4), ' Intel Briefing  ' (104, 1), ' Practice
Mission  ' (100, 1).

`ui_dialog_prompt(buf, 0, 0, x, y, maxlen, h, yes_no, flag)` (365e:2b25):
resets the clip window to the full 320x200 screen (2255:0fd0), then draws the
prompt `buf` in a text panel at (x, y) with font DS:eef4 (redrawn on the
first two frames, into both pages when text entry is active). Y/N mode waits for 'y'/'n' (Esc = 'n') and stores it in buf[0].
Text mode (maxlen != 0, box width (maxlen+1)*6) accepts letters/digits,
space, '.', ''' and '-' up to maxlen/6-3 characters, Backspace deletes, a
'_' cursor blinks with timer bit 6, Enter finishes, Esc returns buf[0] = 1.
Prompts: 'Load Campaign "<name>" ?  (Y/N)' and 'Replace Campaign ...' at x =
184 - width/2, y = 60; 'End Your Mission?  (Y/N)' at x = 160 - width/2, y =
64; 'Exit To DOS?  (Y/N)' at x = 154 - width/2, y = 64.

`ui_screen_transition(pal, wait)`: if wait, spin for 0x180 timer ticks while
fading the music (49f8:0138(0x700)); clear the screen (2255:0735) and flip
once (twice when wait); load palette `pal`; apply it (with 49f8:0124(0x100)
around it when wait).

### 9.3 Main menu (`menu_run`, 365e:71d3)

Transition to palette 5 (sm), `menu_enter`: mainm.pic; if no last campaign
slot the Continue button gets flag 0x02 (disabled); focus button 2 and warp
the cursor to it. Music 4592:1384(5). Each frame: render (full redraw while
`g_redraw_frames` > 0: background, buttons 51b3 with labels DS:2464, title),
read key and mouse, `menu_handle_input`.

| # | Rect (x, y, w, h) | Key | Label | Action |
|---|---|---|---|---|
| 0 | 186,104,112,16 | c | Continue Campaign | mode 1 (only if enabled) |
| 1 | 186, 80,112,16 | s | Start Campaign | mode 2 |
| 2 | 186,128,112,16 | p | Practice Mission | mode 3 |

F10 opens the difficulty screen (365e:e5bd) and re-enters the menu; Alt-X
returns 0 (quit). The menu loop ends when a mode was chosen.

### 9.4 Intel Briefing / Practice Mission (`intel_screen(campaign)`, 365e:74e8)

* Practice: if a previous practice pick exists (DS:3f18 != -1) restore
  mission/year from DS:3f18/3f19.
* A branch "campaign mission == 0xff -> switch to mode 6, return 0" exists
  but is unreachable: the byte is zero-extended and then compared with -1
  (`inc ax; je`), so it never matches. A port should not rely on it.
* g_mission_no = year*20 + mission, `msn_load` (MTM freed again at
  once), `intel_enter` (intel.pic, 'map' sprite set, focus Next, year/date
  buttons clickable only in practice, load cYmNN.s). Message "Change Date or
  Year for different missions." in practice, else start the intel text. Music
  4592:1384(7); palette 8 (si).
* Buttons (51b7:0000, labels DS:249e):

| # | Rect | Key | Label |
|---|---|---|---|
| 0 | 264,150,40,16 | 1 | year text (practice only) |
| 1 | 264,118,40,24 | d | 'Date:' (practice only) |
| 2 | 0,176,320,24 | n | text bar (invisible) |
| 3 | 276,4,40,16 | n | Next |

* Keys: '1'/'2' year +1/-1 (wrap 0..3, mission reset to 0), 'd'/'f' mission
  +1/-1 (wrap 0..19) - practice only, each reloads the mission and text and
  restarts the text; Space on button 0 = '2', on button 1 = 'f', otherwise next
  text line; n/Enter on Next -> return 2; Esc -> 1; Alt-X -> 0.
* Text sequencer (`intel_next_text_line`, every 0x800 ticks, restart after
  0x3000 ticks idle): state `s` (DS:eeee) 0..1 shows line (won ? 5 : 7) + s,
  2..6 shows line s-2, anything else line 0; after showing, s++ (max 7) and
  empty lines are skipped. Practice mode starts at s = 2 (lines 0-4 only).
  "won" is read from the history entry of the SEAL-team leader unit that is
  still in `g_groups[0][0]` from the previous mission (entry index missions-1,
  clamped at 0) - effectively the result of the previous campaign mission;
  a port should use campaign.hist[pm.missions-1].won. Once s reaches 7 it
  becomes 8 and, when fewer than 2 messages are queued, the sequencer stops
  (DS:eeef = 0) until restarted.
* Rendering (`intel_render`): full redraw -> intel.pic, text bar (0,176,320,24)
  colour 0, 'map' frame 4 at (312 - w, 170 - h). Every frame: the area map
  frame (region DS:3140[area]) is zoomed in centred on (120, 96) (zoom += 2*frame_ticks
  up to 0x100, then held 2 frames); then mission markers: every other mission
  of the year (practice only) as a 4x3 dot colour 0x02 with a 0x0a
  highlight, the current mission as a 6x4 marker colour 0x04 with a
  blinking 0x0c/0x0f top (timer bit 6). Mission map positions are 80 (x, y)
  pairs at 51b7:0050 (offset from the map origin). Calendar: month (3
  letters) at (272,122) and day (= mission index + 1) at (280,138) red with
  0x11 shadow, year (1966+) at (272,154) black on white; panel (244,44,64,32);
  "Mission:" at (252,48) and the primary objective type name at (252,64)
  once the zoom has finished.

### 9.5 Mission Briefing (`brf_screen`, 365e:54bd)

Setup: `msn_load`, 1000:4580(area) (builds the world through
`msn_build_world`), mission clock 10:20, 19ac:2b60, camera height 6000, copy
camera targets to 53ba: +2230 objective 1, +223c objective 2, +2248 objective
3, +2254 insertion point, +2260 extraction point; palette 0;
`brf_init_viewports` (1000:21f0(desc, x, z, y, pitch, sx, sy, sw, sh) stores
x<<8, y<<8, z<<8 at +0/+4/+8, pitch<<3 at +0e and the screen rectangle at
+12..+18): d84e (x -1000, z 15, y 0, pitch 0; screen 0,0,46,200), d86e (x 0,
z 0, y 1000, pitch -90 = looking straight down; screen 76,28,128,112 = the
briefing window), d88a (x -1000, z 0, y 30, pitch 0; screen 0,24,320,139);
`brf_init_scene`; the SEAL leader's world object is parked at x = -3000
(outside the map) during the briefing; first camera waypoint -> insertion
point (height 6000, 0x400 ticks); message "Look at the Patrol and Marching
Orders for more detail."; music 8.

Sub-modes (`g_brf_mode`): 0 3D view idle, 1 briefing running, 2 Patrol Order,
3 Marching Order. The briefing starts automatically after about 0x600 ticks
the first time (the idle timer is preset to now-0x1800 and fires at 0x1e00).

Buttons (518e:0000, labels DS:24b0):

| # | Rect | Key | Use |
|---|---|---|---|
| 0 | 0,176,320,24 | n | text bar (invisible, disabled) |
| 1 | 76,28,128,112 | b | briefing window - start briefing |
| 2 | 232,31,48,56 | p | Patrol Order clipboard |
| 3 | 232,103,48,56 | m | Marching Order clipboard |
| 4 | 0,0,46,200 | n | left strip (invisible) |
| 5 | 276,4,40,16 | n | Next (label/key become 'Exit'/'e' on the clipboards) |

`brf_handle_input` (modes 0-2; mode 3 uses `brf_marching_input`):

* 'b' (mode 0): `brf_start` - sound 49d2:00f6(0x69,0x69), clear messages and
  camera queue, step 0, mode 1, first step immediately.
* Space (mode 1): next step at once, drop the current camera waypoint
  (`brfcam_queue_skip`) and the current message (1000:7f71).
* 'p' (mode 0): mode 2 via `brf_enter_clipboard` (palette 10 'so', pwo.pic,
  Next becomes 'Exit'/'e'); 'm' (mode 0): mode 3, same, focus the Exit
  hot spot (index 28).
* 'e' or Esc in mode 2: back to mode 0 (`brf_enter_main`: palette 0,
  brf.pic, label 'Next'/'n', cursor on Next; the idle timer restarts).
* 'n' (mode 0): leave with 2 (next); Esc (mode 0): leave with 1 (back);
  Alt-X: leave with 0; F10 (mode 0): difficulty screen (365e:e5bd), then
  `brf_reset_view`.
* Enter presses the focused button (see 9.2); mouse movement moves the cursor
  over list 518e:0000.

Briefing steps (`brf_step`, `g_brf_step` 0..10, one per 0x600 ticks or Space;
each shows message(s) in the text bar and queues a camera waypoint = (target,
camera height, 0x500 ticks)):

| Step | Message | Camera target, height |
|---|---|---|
| 0 | "Welcome to the mission briefing." | insertion, 7200 |
| 1 | "The mission is located in the <area name>." | insertion, 3000 |
| 2 | "The primary objective is to <verb[obj1]>" + obj1 description (if no objective 2, skip steps 3-4) | objective 1, 1800 |
| 3 | "The secondary objective is to <verb[obj2]>" + description (if no objective 3, skip 4) | objective 2, 2100 |
| 4 | "An extra objective is to <verb[obj3]>" + description | objective 3, 2550 |
| 5 | "Your insertion craft will be a <group type name>." | insertion, 3150 |
| 6 | "You will move <obj1 route text>" (skip step 7 if no objective 2) | objective 1, 1950 |
| 7 | "You will then move <obj2 route text>" | objective 2, 1800 |
| 8 | "A <extraction group name>" + "will meet you at the extraction site." | extraction, 1950 |
| 9 | "Your support craft will be a <fire-support group name>." | insertion, 4500 |
| 10 | "Enemy patrol activity is <word[min(team count, 8)]>" | insertion, 7200 |

After step 10 the briefing ends (mode 0) once at most one message and at most
two camera waypoints remain.

Camera (`brfcam_*`): a top-down camera (the d86e descriptor) flies over the
map. Ring of 8 slots at 5170:0000, 16 bytes each: +0 flag (-1 empty), +2
(always 1), +4 duration (ticks), +6 end time (-1 = not started), +0a camera
height, +0c far pointer to the target position. `brfcam_queue_push(target,
height, duration, 0, 1)` fails when 8 are queued; the first waypoint queued
into an empty ring sets the height immediately. Per frame
`brfcam_queue_tick` sets end = now + duration for a fresh head slot and
removes it once the timer passes end (`brfcam_queue_remove` keeps the last
remaining slot); `brfcam_update` moves the camera toward the head slot: for x
and z, if |cam - target| < 0x3c00 snap, else cam += ((target - cam)/2 *
frame_ticks) >> 8; height: target = h << 8; snap when closer than 0x3c00,
else +-600 * frame_ticks. The read-out during the briefing is (height >> 8)/3
followed by "Meters".

Patrol Order clipboard (`brf_draw_patrol_order`, font DS:eefc, text colour
0x1c on shadow 0x0f): area name (36, 20); day (mission index + 1) (162, 34),
month (3 letters) (178, 34), year (206, 34); "Time:" (36, 34) with HH at
(98, 34), ':' and MM at (114, 34) (mission clock); "First Objective:" (36,
48) and the objective type name at (162, 48); second and third objective
lines follow every 14 pixels when present; then "Insertion Team:",
"Extraction Team:", "Fire Support Unit:", "Break Contact Unit:" (only when
MCI+74 and the group exist) with the group type names (DS:1c7c) of ec8c,
ec8d, ec90, ec91; then "Weather:" DS:25b6[region] ('Hot', 'Warm, Humid',
'Hot, Humid', 'Clear'), "Tide:" 'N/A', "Terrain:" DS:25ac[region] ('Swamp
Jungle', 'Coastal Delta', 'Delta Swamp', 'Farmland') and "Enemy strength is "
+ DS:2572[min(team count, 8)] (value at x 156). Every row is 14 pixels below
the previous one, labels at x 36, values at x 162. region = DS:3140[area]
(0 Rung Sat for areas 6-11 and 18-22, 1 Mekong/My Tho for 0-5, 2 Vinh Long
for 12-17 and 27, 3 Bassac for 23-26).

Marching Order clipboard (`brf_draw_marching_order`): member k uses base y =
31 + 44k:

* name line at (40, y-13) colour 4 when that record is being edited else
  0x1c (shadow 0x0f): [rating abbreviation if rank < 8] + ["SM" if rating 0
  and rank < 5] + [rank abbreviation if rank >= 2] + "  " + first name + [
  " '" + nickname + "'" - the point man uses campaign+0e and only shows it
  when not empty, others use SE+0x20] + " " + last name;
* role name (DS:1c8c) at (40, y), "STR:" + rating word at (116, y), "Load:" +
  lbs + " lbs" + " " + load class (when load > 0) at (164, y);
* weapon slots A (40, y+8), B (164, y+8), C (40, y+16), D (164, y+16): short
  name in colour 0, then "     " + " " + long name + " (" + capacity x
  reloads + ")" in colour 8, red (4) while edited; "Empty Weapon Slot" when
  empty;
* tool slots E (40, y+24), F (164, y+24): short name, "     " + " " + long
  name, or "Empty Tool Slot".

Marching Order hot spots (518e:0070, 7 per member; index = member*7 + field;
field 0 name line (40, y-13, 248, 14; not clickable for the point man),
1..4 weapon slots A..D (124 x 8 at the positions above), 5..6 tools E/F);
index 28 is the Exit button (276,4,40,16, key 'e'). `brf_marching_input`:
arrow keys move the cursor 8 pixels (left/right) or 5 (up/down); Enter,
Space, '+', '-' on a field are applied by `loadout_edit_field`:

* name (members 1..3): Enter = next / Space = previous free roster entry in
  the current pool (`roster_cycle_member`: entries with flags == 0, never the
  point man; the old member's in-team flag is cleared first; the search stops
  at the pool bounds base..base+23 without wrapping and keeps the current
  member when nothing is found) and the member's loadout is rebuilt
  (`loadout_build_member`);
* weapon slot s (1..4): Enter/Space = next/previous weapon allowed this year
  for the slot class (A guns and DEMO, B throwables except DEMO, C/D any); the
  reloads are reset to the weapon's default; '+'/'-' change reloads within 0
  .. default;
* tool slot: Enter = +1 (max 2), Space = -1 (min -1).

`brf_render`: full redraws draw brf.pic, text bar, the frame around the
3D window (text panel 77,28,126,112), the 'camp' frame for the area, the
clipboard (modes 2/3) and the buttons; in modes 0/1 the 3D scene is drawn in
the briefing viewport, with the altitude read-out (value at x 116, y 132 and
"Meters" at x 140, y 132, colour 0x0f, only while the briefing runs) and the
title tab.

On exit `brf_leave` stores the point man's A/B weapons in the campaign and
unloads 'camp'; the world is freed (1000:45a6) and the MTM table released.

### 9.6 Voice samples (365e:7e14-7f74)

Only when digital sound is present (DS:45c3): `voice_load(year)` loads
`new<year+1>a.voc` and `...b.voc`; `voice_play` starts A (if DS:45c0);
`voice_tick(dt)` accumulates time and starts B when the driver reports A
finished (status 3); `voice_stop` stops output.

---------------------------------------------------------------------------

## 10. Open questions

* MCI +22/+4a (insertion/extraction descriptions) and the flow-table fields
  +4 (day) and +6 (HHMM) are not used in this half; the screens here show
  "mission index + 1" as the day.
* Flow-table +0c bit 1 (set only for year 1 mission 2) - meaning unknown.
* Team Size "Enhanced" behaves like "Real" in `ent_spawn_mtm_team` (both drop
  one member); reinforcement logic may exist elsewhere.
* All friendly NPCs use FRND01 and all civilians CIV01 regardless of the MTM
  member numbers; FRND02..09 are not referenced by this half.
* SE+0x62 meaning for SEALs (copied into loadout byte 1, not used here).
* Group +20/+22 semantics (formation / speed) are inferred from the order
  menus; MTM +23..+27 and the waypoint block are consumed by 4a37.
* s.cnf bytes 0..2 and 0xdc..0xe7, and `s.rst`, have no reader in this half.
* `365e:2319` (sealNN.s), `365e:36c3`, `365e:4c1d` and `365e:4918` have no
  direct caller in the disassembly (possibly dead code or called through
  pointers).
