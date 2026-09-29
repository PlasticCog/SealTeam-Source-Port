# Segment 19ac — game flow, input, command keys, map / control panel, cursor, weapons and shot resolution

Ghidra segment `19ac` (code `19ac:0007`–`19ac:8a98`, file offset `0xd7b0 + off`) is not one
source module: the linker packed several small Microsoft C modules into one code segment.
By address range they are:

| Range | Contents |
|-------|----------|
| `0008`–`0667` | award bookkeeping after a mission, **top-level game loop**, command line, EA logo + title/credits |
| `0934`–`0cc3`… `1685` | tool use, global toggles (Alt-S/D/M), rate of fire, **mission command keys** (`cmd_order_keys`) |
| `1685`–`2574` | target clear, weapon cycling, turning, pointer motion, **field-view key handler** |
| `2574`–`2b60` | **input layer** (keyboard, mouse, joystick unified into key codes + motion) |
| `2b60`–`36e0` | map markers, cursor bitmaps/masks, map coordinate transforms, route/marker drawing |
| `36e0`–`5f6a` | cursor draw/erase, **map-screen control panel** (buttons, team list, orders, info panel), map key handler, insertion/re-insertion handler |
| `5f6a`–`6d43` | string helpers, unit/group helpers, SEAL camp scene set-up |
| `6cca`–`89c3` | weapon inventory helpers, **shot records, hit rolls, damage**, wounds / bleeding |

All function names used below are the ones in `tools/re/symbols/seg_19ac.tsv`; functions of
other segments are referred to by address (and by the names other analyses gave them where known).

## 1. Conventions used by this code

* **Clock.** `g_timer` (DS:ECA6, u32) is the frame-latched copy of a counter incremented by a
  256 Hz AIL timer callback (4a1c:0028), so **1 tick = 1/256 s**. All delays below are in ticks
  (`0x50` = 0.3125 s, `0x100` = 1 s, `0x200` = 2 s, `0x400` = 4 s, `0x500` = 5 s, `0x5400` = 84 s).
  The recurring "elapsed" test is: `now - last >= N` (32-bit, signed high word), and on success
  `last = now`.
* **World positions** are three int32 (x, y = altitude, z), 24.8 fixed point (value >> 8 = units).
  A 12-byte position is copied everywhere with a 6-word move.
* **Headings** are 1/8 degree, 0..2879 (`0xB40`), wrapped by 2255:621E (BX = address of the
  angle, AX = delta). The mover record keeps `heading >> 3` in degrees.
* **Random**: `random(n)` = 2255:78D6 with n in AX = `(rng() & 0x7FFF) % n`.
* **Status messages**: 1000:7D4B(text, priority) prints a line in the message area; priorities
  used here are 0x80, 0x100, 0x200 and 0x7800 (sticky). 1000:7D6F(unit, n) shows a hand signal
  "`<speaker>: <signal n>`" (signal names at DS:1CB0: 0 Halt, 1 Danger, 2 Enemy, 3 Trap,
  4 Under Fire, 5 Objective, 6 Search, 7 Secure, 8 Column, 9 In-Line, 10 VeeWedge, 11 Diamond,
  12 Cease, 13 At Will, 14 Field, 15 Target). 1000:672A(text, group) sends a radio order to a
  group (needed for friendly groups and for anything beyond 600 units; returns 0 and prints
  'Radio damaged.' when the radio is out, other failures are reported by the caller).
* Far data blocks addressed through segment variables in DGROUP (all offsets below are inside
  those far segments, not DGROUP):
  * **Option block, seg 5178** (via DS:CD64/CD7C/CDD6/CDE2): the Difficulty screen settings.
    Used here: `+0x148` Ammo (1 = Real: reloads consume spare magazines, else unlimited),
    `+0x14A` Enemy Wounds (0 Death, 1 Heavy, 2 Real), `+0x14E` Player Wounds (0 None,
    1 Decreased, 2 Real), `+0x156` Map (0 Freeze = clock paused while the map is up, non-zero Real).
  * **Target block, seg 5149** (DS:CD66/CD7A/CD86/CDDE): `+0x02` 12-byte aim point, `+0x0E` far
    pointer of the point man's current target unit, `+0x12` target state (−1 = none),
    `+0x18` target distance; `+0x1E6E` count of the group-segment list.
  * **Shot block, seg 53BA** (DS:CDD8/CDDA/CDDC): 32 shot records of 0x3C bytes at `+0x28BA`;
    statistics `+0x303A` rounds fired, `+0x303C` rounds that hit, `+0x303E` grenades/explosives
    fired, `+0x3040` explosive hits (only shots from SEAL groups are counted).
  * **Damage tables, seg 525F** (DS:CDE0), see §12.

## 2. Game flow

### 2.1 Game modes (`g_game_mode`, DS:D7E4)

| Value | Meaning | Set by |
|-------|---------|--------|
| 1 | Continue a campaign | main menu 'c' (ignored while the menu marks the option unavailable, bit 1 of byte 51b7:000B) and after the first mission of a new campaign |
| 2 | Main menu / start a new campaign | default; main menu 's'; screens returning "back" |
| 3 | Practice mission | main menu 'p' |
| 6 | Demo: play the mission given on the command line, then exit | `main_parse_cmdline` |

### 2.2 `main_game_loop` (19ac:016D)

Called once from the program entry (1000:0000) after `main_parse_cmdline`. Local state:
`quit` (byte), `step` (int, starts 0). Screen functions return 0 = quit the game,
1 = back, anything else = next.

```
cmp_state_reset_for_mode()                       ; 365e:37be
loop:
  if quit: return
  if g_show_title:
      if mode != 6 and title_screen() == 0: return 0
      g_show_title = 0
  if mode == 2:
      if menu_run() == 0: return 0              ; 365e:71d3 may change the mode
      cmp_state_reset_for_mode()
  if mode == 3: run PRACTICE sequence (below) until it sets 'inner done'
  else:          run CAMPAIGN/DEMO sequence (below)
```

**Practice sequence (mode 3)** — `step`:

| step | action | result handling |
|------|--------|-----------------|
| 0 | `intel_screen(g_campaign)` (365e:74e8, practice calendar) | 0 quit; 1 → mode = 2 (menu), step 0, leave; else step 1 |
| 1 | pick point man `g_campaign+4 = random(8) + (year odd ? 0x22 : 0x0A)`; `roster_new(year odd ? 0x19 : 0)`; `loadout_build_team()`; `cmp_save(8)`; `brf_screen(0)` | 0 quit; 1 → step 0 after `roster_free()`; else step 2 |
| 2 | `ui_screen_transition(0,1)`; "One Moment Please ..." (1000:2d83); **mission** `1000:01de()` | 1 (Alt-X quit) → quit; else `1000:0790()` (unload), step 3 |
| 3 | `365e:b511(1)` (end-of-mission screen) | 0 quit; else `roster_free()`, mode = 2, step 0, leave |
| >3 | quit | |

**Campaign / demo sequence (modes 1, 2, 6)** — on entry `step` 0 is replaced by 1 unless mode = 2:

| step | modes 1/2 | mode 6 (demo) |
|------|-----------|---------------|
| 0 | only mode 2: new campaign screen 365e:8095; 0 quit; 1 → back to menu (mode 2, `roster_free`); else step 1 | — |
| 1 | campaign load/continue screen 365e:9aa6; 0 quit; 1 → menu (mode 2, `roster_free`, `g_campaign_saved` = 0); else step 2 | skipped |
| 2 | `intel_screen(g_campaign)`; 0 quit; 1 → step 0 (`roster_free`, stay in this mode); else step 3 | `g_mci_ptr = msn_load()`, `msn_free_mtm()`, step 3 |
| 3 | `loadout_build_team()`, `cmp_save_autosave()`, `brf_screen(0)`; 0 quit; 1 → step 2 (`loadout_release_team`); else step 4 | only the first two calls |
| 4 | marching order 365e:abb9(0); 0 quit; 1 → step 3; else step 5 | skipped |
| 5 | SEAL camp scene 365e:d285 (3D walk-around, uses §10); 0 quit; 1 → step 4; else step 6 | skipped |
| 6 | `ui_screen_transition(0,1)`, "One Moment Please ...", **mission** `1000:01de()`; 1 → quit; else `1000:0790()`, step 7 | same |
| 7 | debrief 365e:d659; 0 quit; else step 8 | step 8 |
| 8 | 365e:b511(1); 0 quit; if mode = 2 then mode = 1; `g_campaign_saved` = 1; `roster_free`; step 0 | quit (demo ends) |
| >8 | quit | quit |

### 2.3 Command line — `main_parse_cmdline` (19ac:04BE)

`g_mission_no` starts at −1. Arguments are processed **from the last to the first**; each
character is lowered with `tolower`:

* `t` — `g_show_title = 0`.
* `d` — `g_digital_allowed` (DS:45CC) = 0.
* `?` or `h` — print usage (1959:04aa) and `exit(0)`.
* digit at character position 0 of an argument: `g_mission_no = digit*10`; at position 1:
  `g_mission_no += digit`; later digits ignored.

After the loop, if the argument processed last (i.e. `argv[1]`) had exactly one character,
`g_mission_no /= 10` (so a single digit n gives n). Then `g_mission_no -= 1`; if the result is
0..79 the mode becomes 6 (demo of that mission), otherwise `g_mission_no = 0` and mode 2.

### 2.4 Logo and title — `title_show_logo` (05A4), `title_screen` (0667)

Logo: clear, present, reset palette fade, `ui_screen_transition(4,1)`, load picture `ea.pic`,
reset the clock and input timers, then loop (fade service, blit the background rectangle
DS:4F94 and present for the first two frames) until 0x500 ticks have passed. No input.

Title: logo, `ui_screen_transition(3,1)`, picture `title.pic`, clock reset, credits text
`g_mission_text = msn_load_text("c")` (10 pages × 160 bytes: two 80-byte lines), font DS:EEF8,
music `snd_load_music(4)`, wait 0x300 ticks, `snd_music_play(0)`, reset input timers, flush keys.
Per frame: blit the background, clip to (0,150,320,40); if page < 9 draw its two lines centred
(x = 160 − width/2, colour 15, font 365e:9489) at y = 190 − (counter mod 64) and y + 12;
present. `counter` (32-bit, starts 1) increments every 0x10 ticks; when `counter mod 50 == 0`
it restarts at 1 and the page advances (10 pages, wraps; page 9 is blank). Input:
`input_toggle_keys` handles Alt-S/D/M; **Enter** ends with result 1; **Esc**, **Alt-X** or 84 s
(0x5400 ticks) since start end the screen, result 1 except Alt-X which returns 0 (quit game).
On exit the text is freed, music stopped and released.

## 3. Input layer (19ac:2574–2B3B)

### 3.1 Devices and modes

* `input_init` (2574): `g_joy_present = joy_detect() > 0` (4e98, game port 0x201);
  `g_mouse_present = mouse_init(3) > 0` (49d2, INT 33h, mouse used as a virtual stick 0..211,
  centre 105); read the mouse position into `g_mouse_centre_x/y`; `g_input_mode = 1`.
* `input_joystick_calibrate` (25B3): joystick calibration screen (1000:2ed7); Esc there turns the
  joystick off.
* `g_input_mode` (DS:D820): **0 action** (field views; set every mission frame), **1 menu**,
  **2 map** (map screen and re-insertion map).

### 3.2 `input_get_key` (2647) — returns one key code per call

Key codes are BIOS style: ASCII, or `scan << 8` for extended keys (e.g. Up `0x4800`).
Order of tests (the first that fires returns):

1. **Joystick** (if present): read axes into `g_joy_x/y` (4e98:0074, ±128 scale, dead zone 5)
   and buttons (4e98:011d).
   * buttons == 1 (button 1 only): every ≥0x50 ticks (timer D82A) → `0x0D` (Enter).
   * buttons == 2: every ≥0x50 ticks in menu mode, ≥0x100 otherwise (timer D818) → `0x20`.
   * action mode only: every ≥0x60 ticks (timer D814): `g_joy_y > 0x30` → `0x5000` (Down),
     `< −0x30` → `0x4800` (Up).
2. **Mouse** (if present), buttons in DS:467F:
   * left: every ≥0x50 ticks (D82E) → `0x0D`.
   * right: every ≥0x50 (menu) / ≥0x100 (other modes) ticks (D81C) → `0x20`.
   * action mode only: every ≥0x60 ticks (D822): `dy = mouse_y − g_mouse_centre_y`, then
     `mouse_set_pos(mouse_x, 105)`; `dy > 0x56` → Down, `dy < −0x56` → Up.
3. **Keyboard**: if ≥0x50 ticks elapsed since timer D826 (timer then reset) or `g_key_poll_now`
   is set: read one BIOS key (`input_poll_bios_key`), flush the rest, `g_key_poll_now = (key == 0)`.
   Otherwise flush the buffer and return 0 (keys typed inside the throttle window are lost).
   `A`..`Z` are OR-ed with 0x60 (lower case). Outside menu mode a keyboard **space becomes `m`**
   (so only the mouse right button / stick button 2 produce `0x20` in the field and map).

`input_reset_repeat_timers` (25F6) sets all seven timers to now; `input_reset_button2_timers`
(262F) zeroes D818/D81C — the mission loop calls it every frame in views ≥ 2 (except 0xC), so a
held right button yields `0x20` every frame there (used for camera pan/zoom).
`input_flush_keyboard` (263E) drains the BIOS buffer. `input_poll_bios_key` (2B3B): INT 16h
AH=1; none → 0; else AH=0 read; AL≠0 → AL else AX.

### 3.3 `input_get_motion` (29CD) — returns dx (AX), dy (DX)

* Joystick present: action mode: `dx = |jx| > 5 ? clamp(jx,±100) >> 3 : 0`,
  `dy = |jy| > 5 ? clamp(jy,±100) >> 4 : 0`; other modes: `dx = clamp(jx,±100) >> 2`,
  `dy = clamp(jy,±100) >> 2` (0 stays 0). Shifts are arithmetic (round toward −∞).
* If the mouse is present and the joystick produced (0,0): `jx = mouse_x − centre_x`,
  `jy = mouse_y − centre_y`, re-centre the mouse to (105,105); action mode:
  `dx = |jx| > 2 ? clamp(jx,±100) >> 3 : 0`, `dy = jy`; other modes `dx = jx`, `dy = jy`.

`menu_arrow_keys` (0AC1) is used by the 365e menus: Up/Left/Right/Down move the UI pointer by
(0,−5)/(−8,0)/(8,0)/(0,5) through `ui_pointer_update` and return 1.

## 4. Mission frame input flow (context: mission loop 1000:01de)

Every mission frame: `key = input_get_key()`, `(dx,dy) = input_get_motion()`;
if `cmd_order_keys(key)` returns 1 the key is dropped (set 0). Then by `g_view_mode`
(DS:D844):

| view | handler |
|------|---------|
| 1 (map) | `map_screen_keys(key, dx, dy)` |
| 7 (insertion zoom), 0xC (re-insertion map) | `insert_keys(key, dx, dy)` |
| anything else | `field_view_keys(key, dx, dy)` |

With Map = Freeze the map handler runs every frame; all other cases (including the map with
Map = Real) run only while time compression is off (`g_time_compress == 0`) and DS:D7EC is 0.

View modes: 0 point man (F1), 1 map, 2 team (F2), 3/4 support views 1/2 (F3/F4),
7 insertion zoom, 8 extraction, 0xC re-insertion map, 0xD target (F9), 0xE debug view (F10),
0xF/0x10 support views 3/4 (F5/F6), 0x11/0x12 split team A/B (F7/F8).

## 5. Global command keys

### 5.1 `input_toggle_keys` (0B2A) — also active on the title screen

| key | action |
|-----|--------|
| Alt-S `0x1F00` | `g_sfx_on ^= 1`; on: 'Sound Effects On'; off: stop all channels, reset channels, 'Sound Effects Off' (priority 0x80) |
| Alt-D `0x2000` | `g_detail_msg_time = now + 0x100`; `g_detail_level = (level+1) % 6`; level 0 → 1000:70b3 (remove detail objects); level 5 → zero `g_detail_anchor` and 1000:784d(1) (respawn) |
| Alt-M `0x3200` | music off→on: 'Music On ', `snd_music_play(0)`; on→off: 'Music Off', `snd_music_stop()` |

Returns 1 when one of these keys was handled.

### 5.2 `cmd_order_keys` (0CC3)

First calls `input_toggle_keys` (returns 1 if handled). `sel` = `g_groups[g_selected_group]`.
"Split context" means `g_selected_group != g_seal_group && g_view_mode == 1` (an order given on
the map to another group); otherwise orders go to the main SEAL team (group 0, leader = its
first member). In split context an order is ignored when `sel+0x26 != 0` (not a SEAL group)
unless noted. "Abort search" = if the group's movement order (+0x24) is 4: print
'Security: "Search aborted."' (0x100) and call 2dbd:516b.

| key | main team | split context | returns |
|-----|-----------|---------------|---------|
| Enter `0x0D` | in view 7: skip insertion (1000:007a) | same | 0 |
| Esc `0x1B` | view 7: skip insertion, return 1. Else: stop SFX, pause clock (1000:308a), suspend fade, reset cursor if not on the map, 'End Your Mission? (Y/N)'; yes → `ent_team_rejoin`, `g_mission_aborted = 1`; resume fade and clock; redraw ×2 | same | 1 |
| `c` | if fire order (+0x22) already 3: hand signal 12, else message 'Cease Fire!'; set +0x22 = 3 | radio 'Cease Fire' (only SEAL groups); set +0x22 = 3 | 0 |
| `d` | formation +0x20 = 2 (Diamond), signal 11 | radio 'Demolish Action', +0x22 = 5 | 0 |
| `f` | signal 14, +0x22 = 0 (In Field of Fire) | radio 'Field of Fire' (SEAL groups), +0x22 = 0 | 0 |
| `h` | abort search, +0x24 = 0, 2dbd:651d(0) (speed 0), signal 0 | abort search, +0x24 = 0, stance anim 0 (2dbd:03c5 if alive), radio 'Halt' | 0 |
| `i` | formation 1 (In-Line), signal 9 | radio 'Snipe Action', +0x22 = 6 | 0 |
| `j` | `ent_group_merge(0, last group)`; success → 'Team joined.' (0x200) | non-SEAL `sel` → only deselect; abort search, merge the **last** group into 0; success → radio 'Join' | 0; on success, if the view is ≥ 0x11 (split view) switch to the team view; always `g_selected_group = 0` |
| `l` | formation 0 (Column), signal 8 | abort search, +0x24 = 2 (Stealth), stance anim 1, radio 'Stealth' | 0 |
| `p` | `ent_group_split(0)`; if a new group n > 0: its +0x22 = 2, +0x20 = 0, +0x24 = 0, stance anim 0, 'Team split.' (0x200); on the map select it (n = count−1); 2dbd:3684 | abort search, +0x24 = 1 (ASAP), stance anim 2, radio 'Rallypoint ASAP!' | 0 |
| `q` | dive: if now > `g_dive_ready_time`: 2dbd:17c8(team), `g_dive_ready_time = now + 0x400` | same | 1 |
| `r` | not in views 7/0xC: current weapon slot `rof = wpn_next_rof(slot)`, `g_weapon_info_time = now + 0x100` | same | 0 |
| `s` | signal 6 from the leader; fewer than 2 live members → 'Teammates are unable to search.'; else 2dbd:5327(leader) == 300 → 'Nothing to search.' + signal 7 (Secure) from the team's second member; else start the search 2dbd:4e56(0), failure → 'Another search in progress.' (0x200 messages) | (no live-member check) 5327 on `sel`'s first member == 300 → 'Nothing to search.' + signal 7 from that member; else 4e56(0): success → radio 'Search', failure → 'Another search in progress.' | 0 |
| `t` | signal 15, +0x22 = 1 (Fire at Target) | radio 'At Target', +0x22 = 1 | 0 |
| `v` | formation 3 (VeeWedge), signal 10 | +0x22 = 4, radio 'Cover Fire' | 0 |
| `w` | signal 13, +0x22 = 2 (Fire at Will) | radio 'At Will', +0x22 = 2 | 0 |
| Alt-T `0x1400` | time compression: if off → stop SFX, stop music, `snd_music_play(5)`, 1000:01b0 ('Time Compression On'); if on → 1000:0194 (off), stop music, 'Time Compression Off' | same | 1 |
| Alt-U `0x1600` | toggle `g_auto_target`, 'Auto-Target On/Off' (0x100) | same | 1 |
| Alt-I `0x1700` | toggle `g_teaminfo_names` (map Team Info mode) | same | 1 |
| Alt-P `0x1900` | pause clock, modal dialog 'Game Paused. Press Enter key to Continue.' (365e:2b25 at 0x4C,0x5C), resume clock, `g_skip_present = 1`, redraw ×2 | same | 1 |
| Alt-X `0x2D00` | reset cursor if not on map; `g_quit_to_dos = 'Exit To DOS? (Y/N)'` | same | 1 |
| F1 `0x3B00` | unless `g_input_locked`: point-man view | same | 1 |
| F2 `0x3C00` | unless locked: team view | same | 1 |
| F3..F8 `0x3D00`–`0x4200` | view 1000:2371(0..5): 0–3 support views of groups `g_insertion_group+i` (only if that group's type is 1..3), 4/5 split A/B (only if 1/2 splits exist) | same | 1 |
| F9 `0x4300` | unless locked: target view (only while a target is locked, target state 0) | same | 1 |
| F10 `0x4400` | unless locked, only in modes 3 and 6: debug view 0xE (1000:2d01 first if already there) | same | 1 |
| other keys | — | — | 0 |

After every F-key `ui_resume_after_overlay` runs (cursor reset; with Map = Freeze the paused
clock resumes). Order values on the group record: **+0x20 formation** 0 Column, 1 In-Line,
2 Diamond, 3 VeeWedge; **+0x22 fire order** 0 In Field of Fire, 1 At Target, 2 At Will,
3 Cease Fire, and for split teams the specials 4 Cover, 5 Demolish, 6 Snipe; **+0x24
movement/status** for SEAL groups 0 Halt, 1 ASAP, 2 Stealth, 4 Searching.

## 6. Field views — `field_view_keys` (1A79) and `field_pointer_motion` (1816)

### 6.1 Frame prologue

`pm` = point man (group 0, member 0), `wpn` = its current weapon slot (`pm+0x16 -> +4`).
If `g_auto_target`: when ≥0x100 ticks passed (DS:02B6) or `dx != 0`, re-acquire a target
(1000:6225 with 'keep' = 0). Target data: if target state == −1 then copy `pm`'s position
into the aim point (5149:0002), push it 0x9600 (150 units) ahead along `pm`'s heading
(`pos_move_polar`), target = NULL, **range = 150**, mode = 3; else target = 5149:000E,
range = 5149:0018, mode = 5149:0012.
`g_view_distance`/`g_view_heading` are loaded from `g_groups[g_view_group]+0x30/+0x32` and
written back at the end of the handler.

### 6.2 Pointer motion (`field_pointer_motion`, key passed in, returns key or 0)

dx:
* views 0–2 without right button (key ≠ `0x20` or view 0): in view 0 while grenade aiming,
  `g_grenade_aim_side ±= 6` clamped to ±dist/2 and redraw the box (1000:57be(side, dist));
  otherwise `unit_turn(pm, −dx)`.
* view 2 with right button, or any view > 2: key consumed, `g_view_heading -= sign(dx)*80`
  (10°; right motion turns the view the other way).

dy:
* view > 2, or view 2 with the key still `0x20` (view 2 without it: nothing): key consumed, `g_view_distance ∓= 6` (dy < 0 nearer), clamped 0x60..900.
* view 0 with right button: dy < 0 → posture + 1 (max 2 = prone), dy > 0 → posture − 1
  (min 0); `spr`/unit posture set (2dbd:0307, 348e:00c8) and message 'Upright'/'Crouch '/' Prone '.
* view 0 without right button while aiming: dy < 0 → dist += 6 (max 300), dy > 0 → dist −= 6
  (min 75), side re-clamped to ±dist/2.

### 6.3 Key table

"Views 0–2" = point man, map (never here) and team; "other" = support/target/split views.

| key | action |
|-----|--------|
| Tab `0x09` | next target (1000:6225, keep flag = target held) |
| Enter `0x0D` | unless locked: if aiming → cancel the grenade box (57be(side, dist−3000)), `g_grenade_aiming = 0`. Else **fire** (§6.4) |
| mouse right / stick 2 `0x20` | unless locked, view 0 only: open map (pause clock if Map = Freeze, stop SFX, 1000:22d4) |
| `m` (and keyboard space) | unless locked, any field view: open map as above |
| `+`, `=` | posture − 1 (towards Upright) if > 0 and alive; message |
| `-` | posture + 1 (towards Prone) if < 2 and alive; message |
| `1` / `2` / `3` | posture Prone / Crouch / Upright (alive only), message |
| `[` | `item_use_tool(pm)`, `g_tool_info_time = now + 0x200` |
| `]` | `item_cycle_tool(pm)`, same timer |
| `g` | unless locked: grenade (§6.5) |
| `n` | `wpn_select_next_weapon`, show weapon info, `g_weapon_info_time = now + 0x200` |
| Alt-N `0x3100` | `wpn_select_next_grenade`, show info, `g_grenade_info_time = now + 0x200` |
| `x` | expose trap: if 1000:6163(pm) succeeds, hand signal 3 (Trap) |
| Up `0x4800` | other views: zoom in (distance −6, min 0x60). Views 0–2: aiming → dist + 6 (max 300); else speed up (2dbd:651d(+0xC00)) |
| Down `0x5000` | other: zoom out (distance + 6, max 900). Views 0–2: aiming → dist − 6 (min 60), side re-clamped; else slow down/back (2dbd:651d(−0xC00)) |
| Left `0x4B00` | other: view heading +80. Views 0–2: aiming → side − 6; else `unit_turn(pm, +3)` |
| Right `0x4D00` | other: view heading −80. Views 0–2: aiming → side + 6; else `unit_turn(pm, −3)` |
| Home `0x4700` | views 0–2: speed up first; then as Left (all views) |
| PgUp `0x4900` | views 0–2: speed up first; then as Right (all views) |
| End `0x4F00` | views 0–2 only: `unit_turn(pm,+3)`, then slow down |
| PgDn `0x5100` | views 0–2: `unit_turn(pm,−3)` first; then as Down (all views) |
| keypad 5 `0x4C00` | views 0–2: stop (2dbd:651d(0)) |
| Ctrl-Left `0x7300` | views ≥ 2: view heading +80; else `unit_turn(pm, +15)` |
| Ctrl-Right `0x7400` | views ≥ 2: view heading −80; else `unit_turn(pm, −15)` |
| Ctrl-PgDn `0x7600` | views ≥ 2: zoom out |
| Ctrl-PgUp `0x8400` | views ≥ 2: zoom in |

`unit_turn(unit, d)`: if the unit is alive, heading += d·8 (wrapped), mover heading = heading>>3,
and `g_view_heading` also += d·8.

### 6.4 Firing (Enter)

1. Hide the grenade box, `g_weapon_info_time = now + 0x200`.
2. If `wpn.rounds (+2) != 0`, `now ≥ g_weapon_ready_time` and not (M203 [type 10] with
   `+6 ≥ 3` and rof 8): if `pm` alive, `shot_fire(pm, target, target ? target pos : aim point,
   0, wpn, range, mode)`.
3. If the magazine is now empty, or the weapon is an M203 whose grenade counter `+6 == 3`:
   reload animation (2dbd:13c1), ready time from 1000:18a1, `wpn_reload(wpn)`.
4. Every ≥0x200 ticks (DS:02B2), if group 0's fire order is 1 (At Target), `wpn` is not the
   satchel (type 12) and a target exists: members 1..3 of group 0 that are alive, not holding the
   satchel and have rounds > 0 also `shot_fire` at the target (range = target distance); a member with 0 rounds but spare magazines plays the reload animation and
   `wpn_reload_both`.

### 6.5 Grenades (`g`)

`g_grenade_info_time = now + 0x200`. With no target and not aiming: if the grenade slot has
rounds > 0 and `now ≥ g_grenade_ready_time`, show the box (57be(side, dist)) and set
`g_grenade_aiming = 1`. Otherwise (target held or already aiming), if rounds and ready:
range = target distance, else `g_grenade_aim_dist`; if `!wpn_in_range` → 'Out of Range.';
else if alive: target position = target unit, else box object `g_grenade_box_obj` (+6) when
aiming, else aim point; `shot_fire(pm, target, pos, 0, grenade, range, mode)`, set ready time
(1000:18a1), `wpn_reload(grenade)`. On this second path the box is always hidden and aiming
cleared afterwards (also when out of rounds, not ready or out of range).

### 6.6 Inventory helpers

Weapon slot record: `+0` type, `+2` rounds loaded, `+4` spare magazines, `+6` M203 grenade
counter, `+8` jammed flag, `+0xC` rate-of-fire bit, `+0xE` far next. A unit's inventory header
(`unit+0x16`): `+0` list head, `+4` current weapon, `+8` current grenade.
Rate of fire bits: 1 Single, 2 Semi (3 rounds per trigger event), 4 Full (20 rounds),
8 grenade launcher (M203), 0x10 thrown, 0x20 shotgun (table flags: e.g. M39 pistol 1, AK47 3,
Stoner 6, M16 7, M60 4, M203 0xF). `wpn_next_rof`: `rof<<1` if allowed by table `+0x10`, else
the first allowed of 1,2,4,8 (0 if none). `wpn_select_next_weapon`: step through the list (wrapping) until the item is not
grenade-class (flags 0x10) or is the satchel charge (type 12). `wpn_select_next_grenade`: step
from the grenade slot until a grenade-class non-satchel item or back to the start.
`wpn_reload`: if Ammo = Real, needs `mags > 0` (then `mags--`); rounds = table `+0xE`; `+6 = 0`.
`wpn_ai_pick_rof` (by table flags, r = random(20)): 0x20 → 0x20; > 0x20 or 1 → 1;
3 → r > 16 ? 1 : 2; 4 → 4; 6 → r > 16 ? 4 : 2; 7 → r < 3 ? 4 : r > 16 ? 1 : 2; 8 → 8;
0xF → r < 3 ? 4 : r < 11 ? 2 : r > 16 ? 1 : (+6 > 2 ? 2 : 8); 0x10 → 0x10; others 1.
Only flags 3, 6, 7 and 0xF draw a random number.

## 7. Map screen (control panel)

### 7.1 Entering, cameras, coordinates

1000:22d4 opens the map: remembers the field view (0 or 2) in `g_view_before_map`, selects
the map camera `g_cam_map` (DS:D86E) with the point-man camera's x/z, calls
`map_select_team_height`, sets view 1 and two full redraws. `map_init` (2E69, at mission
start): load 'mapscr' picture (and cursors on first use), cursor mode 0, focus button 2,
group 0 map height 256000 (`0x3E800`), cursor (100,100), bind button labels, Team Info in
Position mode.

Camera structs (built by 1000:21f0, 0x1C bytes): +0 x (i32), +4 height (i32), +8 z (i32),
+0xE heading·8, +0x12..+0x18 viewport x,y,w,h, +0x1A shift. The map camera is a straight-down
camera over the viewport **(8,10,204,155)**, shift 8.

* **Zoom** = camera height `g_map_height` (DS:D872): '+'/'x' **expand** (+256000, only while
  ≤ 0x3E7FFF, max 4096000); '-'/'z' **zoom** (−256000 only while > 256000). The new value is
  stored in the selected group (+0x2C) so each group remembers its own zoom. Without a pointer
  device the focus jumps to the EXpand (0x27) / Zoom (0x28) button and shows it pressed.
  Default heights: SEAL group 256000, other groups 512000; if a group's value is −1,
  `map_select_team_height` uses the point-man distance to that group's leader rounded up to the
  next multiple of 400 (always adds 400 − d%400), shifted left 9, and stores it in the group.
* **Scale text**: `1:` + ((word at D873) >> 2) = height/1024, i.e. 250 per zoom step.
* `map_screen_to_world(p, out)`: `out.x = (p.x − 110) · (h >> 8) + cx`,
  `out.z = (((87 − p.y) · (h >> 8)) >> 8) << 8 + cz` (h = height, int32 arithmetic).
* `map_world_to_screen(pos, out)`: dx = (x − cx) << 8, dz = (z − cz) << 8, projected with the
  3D engine (2255:4328) for depth h << 8; returns whether (sx, sy) is inside button 0's
  rectangle (the map area).

### 7.2 Screen layout (320×200, 4×6 font)

Frame order (1000:1f92, map view): restore background `g_bg_rect`; panel frame
(365e:2eba 9,10,202,155); `map_draw_team_list`; `map_draw_orders_menu`; clear message line
(0,192,320,8, colour 0); 3D render of the map through the map camera; clip to the map viewport;
`map_draw_routes`; `map_draw_markers`; clip to full screen; `map_draw_info_panel`; messages;
sound update; frame limiter when Map = Freeze; `cursor_draw`. When no full redraw is due only
`cursor_erase` runs first.

**Static panel (`map_draw_info_panel`)** — text colours are palette indices (fg; bg = 0xFF
transparent):

| item | position (x,y) | colour |
|------|---------------|--------|
| title `g_title_text` ('SEAL Team  V1.0') | 8,1 | 8 |
| scale box fill | 48,171 w96 h7 | 0x11 |
| '      SCALE        Mtrs' | 48,171 | 8 |
| '1:' + scale | 96,171 | 0 |
| area name `g_area_name` | 8,180 | 2 |
| 'PictoMap USNavy' | 8,171 | 8 |
| objective 1/2/3 marker triangles (half-size 2) | 218, 11/17/23 | 4 / 0xC / 0xE |
| '1st/2nd/3rd Objective:' | 224, 10/16/22 | 8 |
| objective type name (`g_mission_type_names[MCI+0x76/+0xD8/+0x13A]`) | 280, 10/16/22 | 0 |
| date box fill | 220,2 w80 h7 | 0x11 |
| day (mission-in-year + 1) | 272,3 | 0 |
| month (first 3 letters of `g_month_names[flow+2]`) | 284,3 | 0 |
| year (1966 + year index) | 300,3 | 0 |
| clock 'hh:mm:ss', two digits each, at x, x+12, x+24 | 224,3 | 0 |
| Team Info fill | 220,64 w100 h33 | 0x11 |
| header 'TEAM  Pos Spd Hdg Weapon' or 'Rank   Name   Grenade' | 220,66 | 0 |

Objective lines are drawn only when the MCI type field is non-zero. The Name/Rank header is
used when `g_teaminfo_names` is set and the selected group is a SEAL group.

**Team Info rows** (one per member of the selected group, y = 72 + 6·i):

* colour: dead → 7; enemy group (type ≥ 4) by AI flags (`unit+0x12 -> +0x6C`): 0x20 → 2,
  0x10 → 0xA, 4 → 4, 1 → 0xC; friendly: dying (`+6 -> +0x14`) → 0, seriously wounded
  (`+0x13`) → 8, else 1; enemy with flag 0x80 → 1, else 9.
* SEAL group: x 220 role name (`g_seal_role_names[i]`; for a group other than the point man's
  own, i is offset by 4a37:066e(group)) or, in Name/Rank mode, rank prefix + grade suffix at 220
  and the name (`unit+0x2C -> +0xC`) at 248.
* craft group: `g_craft_abbrev[type]` at 220, member number at 236.
* enemy group: state abbreviation (`Surr` if `AI+0x66`, else by `group AI +0x82`: 1 Ptrl,
  3 Guer, otherwise Gard if bit 0x02 is set, else Null) at 220, number (`+0x2C -> +0x62`) at 236, alert
  abbreviation at 244.
* Position mode, member with a mover: speed (mover +0, 3 chars, space padded) at 260; posture
  (`Up/Cr/Pr`, `Dd` if dead) at 248 when personnel `+0x1A < 5`; heading
  `((360 − (obj_heading>>3)) mod 360)` (computed as |(|h/8| − 360)| mod 360) at 276.
* weapon column (colour 0xC when the member's AI flag 8 = engaging, else 0): short name of the
  current weapon (Position) or current grenade (Name/Rank) at 292, its spare-magazine count
  (+4) at 316.

**Team list (`map_draw_team_list`)** — hidden in the re-insertion map unless group 0 selected.
Buttons 2,3,4 (y 34/45/56, x 224, w 92, h 11) show groups 0,1,2 while `g_selected_group < 3`;
otherwise buttons 5,6,7 (same places) show the friendly groups from index 3 on (hostile groups
skipped; drawing stops at the first hostile or the end). Label = button − 1 ('1'..'6'), '.',
group type name, and for split SEAL groups ' b' (second split, last group) or ' a'.
Colours: selected group 1, Viet Cong / NVA groups 0xC, others 9. Craft groups whose
status is not 6 get a small inverted triangle at (218, y+3), colour 0x25 − 6·index (group 1 →
0x1F, 2 → 0x19, 3 → 0x13 — the same shades as their map markers).

**Orders menu (`map_draw_orders_menu`)** — not in the re-insertion map except EXpand/Zoom.
'   TEAM ORDERS' at (232, 98) colour 0. Text colour 0 marks the active order, 8 the others.

* SEAL or enemy group: fire orders 0x0B..0x0E (active = +0x22). If it is the main team:
  movement 0x0F..0x12 (Halt, Search, SPlit, Join; active when +0x24 equals 0..3 in order) and
  formations 0x13..0x16 (active = +0x20). Split team: 0x1F Halt (+0x24 = 0), 0x20 Search (4),
  0x21 AsaP (1), 0x22 Join, 0x23 SteaLth (+0x24 = 2), 0x24 SnIpe (+0x22 = 6),
  0x25 Demolish (5), 0x26 CoVer (4).
* Craft group (type 1..3): only 0x17 (boat), 0x18 (helo) or 0x19 (aircraft) attack, 0x1A Cease
  AttacK, 0x1B Extract (boat/helo), 0x1C LOiter, 0x1D EmergencY (only the emergency group).
  Active: attack when +0x24 = 5, cease 6, extract 2, emergency 3, loiter 4.
* Always: 0x27 EXpand, 0x28 Zoom (colour 8).

### 7.3 Button table (DS:0BF4, 16 bytes per button)

`+0 x, +2 y, +4 w, +6 h, +8 hot key (u16), +0xA underline character index, +0xC far label`
(labels from `g_ui_button_labels`, bound by `map_init`). Values (from the shipped exe):

| # | x,y,w,h | hot key | label |
|---|---------|---------|-------|
| 0 | 8,10,204,155 | Enter | (map area) |
| 1 | 220,1,32,11 | Alt-T | (clock: time compression) |
| 2–4 | 224, 34/45/56, 92, 11 | '1','2','3' | team list page 1 |
| 5–7 | 224, 34/45/56, 92, 11 | '4','5','6' | team list page 2 |
| 8–10 | 224, 34/45/56, 92, 11 | '7','8','9' | (unused third page) |
| 11–14 | 232, 106/116/126/136, 76, 11 | f t w c | In Field of Fire, Fire at Target, Fire at Will, Cease fire |
| 15–18 | 228, 148/158/168/178, 38, 11 | h s p j | Halt, Search, SPlit, Join |
| 19–22 | 276, 148/158/168/178, 40, 11 | l i d v | CoLumn, In Line, Diamond, VeeWedge |
| 23–25 | 224, 106, 96, 11 | b u a | LSSC Boat / UH-1B Helo / OV-10 Aircraft Attack |
| 26 | 224, 116, 96, 11 | k | Cease AttacK |
| 27–29 | 244, 128/138/148, 48, 11 | e o y | Extract, LOiter, EmergencY |
| 30 | 244, 178, 48, 11 | r | Reinsert |
| 31–34 | 228, 148/158/168/178, 38, 11 | h s p j | Halt, Search, AsaP, Join (split) |
| 35–38 | 276, 148/158/168/178, 40, 11 | l i d v | SteaLth, SnIpe, Demolish, CoVer |
| 39 | 180,171,36,11 | x | EXpand |
| 40 | 148,171,28,11 | z | Zoom |

Underline indices (character within the label, which starts with a space): 11:3, 12:8, 13:8,
17:1, 19:2, 23:6, 25:6, 26:0xB, 28:1, 29:8, 33:3, 35:4, 36:2, 38:2, 39:1, button 1:1,
button 0:0xFF, others 0 (index u underlines label character u+1). Button 30 (Reinsert) is never
drawn; buttons 0 and 1 are never drawn either (only hit-tested).

`map_draw_button(n)` (3839): fill (x−2, y−2, w−1, h−1) colour 0x11; unless pressed
(`g_ui_focus == n && g_ui_pressed`) a white (0xF) outline (x−1, y−1, w−2, h−2); grey (7)
outline (x−2, y−2, w−1, h−1); label at (x+p, y+p) (p = 1 when pressed) in `g_text_colour`;
underline: horizontal line colour 8 from x+4·u+4 to x+4·u+7 at y+p+6 (u = underline index).

### 7.4 Pointer, focus and hit testing

`g_cursor_mode`: **0** keyboard focus on buttons, **1** free cursor over the map area,
**2** pointer device hovering buttons.

* `map_hit_test` (4BF0) walks buttons 0..0x28 and returns the first whose rectangle
  (x, y, w, h − 2) contains the cursor (2255:2622), with skips: from 2 to 5 when the selected
  group ≥ 3; 0x17 jumps to 0x27 unless the selected group is a craft; 0x0B jumps to 0x17/0x18/0x19
  (by craft type) for craft groups; 0x1D becomes 0x1E unless it is the emergency group.
* `map_step_focus(dir)` (4D0A) moves `g_ui_focus` by one with wrap (0x28 → 0 forward,
  0 → 0x28 backward) and then applies these substitutions in this order (t = selected group's
  type, "craft" = t in 1..3):
  * forward: 5 → 0x0B; 8 → 0x0B; 0x0B and craft → 0x18 (t 2), 0x19 (t 3) or 0x17; 0x0F and
    t = 0 → 0x1F (for every SEAL group, including the main team — the split-team buttons share
    the rectangles and hot keys of 0x0F..0x16); 0x18 and t = 1 → 0x1A; 0x19 and t = 2 → 0x1A;
    0x1B and t = 3 → 0x1C; 0x1D and not the emergency group → 0x27; 0x1E → 0x27; 0x17 and
    (main team selected or t ≥ 4) → 0x27.
  * backward: 0x26 → 0x16 when the main team (or t ≥ 4) is selected, stays for other SEAL
    groups, → 0x1D for craft; 0x1E and t = 0 → 0x0E; 0x1D and not the emergency group → 0x1C;
    0x1B and t = 3 → 0x1A; 0x19 → 0x18 (t 2) or 0x17 (t 1); 0x18 and t = 3 → 0x0A; 0x17 and
    t = 2 → 0x0A; 0x16 and craft → 4 (selected < 3) or 7; 0x0A → 7 (t = 4) or 4; 4 and t = 4 → 1.
* `map_focus_button(n)` (504E): focus n, cursor = button centre (x + w/2, y + h/2), mode 0,
  `ui_cursor_move(0,0)`.
* `map_move_pointer(dx, dy)` (508C): mode 1: move the cursor (`ui_cursor_move`, clamps x 4..308,
  y 5..186); if it left the map area (hit ≠ 0) → mode 0. Mode 2: move, focus = hit (if any).
  Mode 0: dx > 0 or (dx = 0 and dy > 0) → step +1, dx < 0 or dy < 0 → step −1, then centre on
  the focused button. Whenever the focus is button 0 the mode becomes 1.

### 7.5 `map_screen_keys` (52A1)

```
if dx or dy: mode = (mode == 1) ? 1 : 2; map_move_pointer(dx, dy)
else if map_hit_test() != 0: mode = 0
ui_button_release(key)                          ; 365e:2af6
if g_extra_redraws: g_extra_redraws--, full redraw x2
if map_zoom_keys(key): return                   ; + - x z and arrows (arrows = 508c(0,-5)/(-8,0)/(8,0)/(0,5))
```

Key actions (the key already went through `cmd_order_keys`, which executed any order letter):

| key | action |
|-----|--------|
| Enter | mode 0: if the focused button's hot key ≠ Enter, run `cmd_order_keys(hotkey)` and recurse with (hotkey,0,0); show pressed; redraw. Mode 1: **set a waypoint** at the cursor: selected group 0 → `g_wp_seal`; craft (type 1..3) → `g_wp_support`; SEAL split group → `g_wp_split_b` if it is the last group and 2 splits exist, else `g_wp_split_a`; enemy group (4/5) → `g_wp_seal`; other types nothing. Pressed flag set |
| space `0x20`, `m` | leave the map: cursor reset; if Map = Freeze resume the clock and input timers; 2dbd:3517; return to team view if it was the team view, else point-man view |
| `1`–`6` | select a group: keys 1–3 → group n−1 directly; keys 4–6 → starting at index n−1, skip = (n−4) + (first MTM group ≤ 3 ? 1 : 0), take the max(skip,1)-th friendly group (type ≤ 3) at or after it. Focus button n+1 (for n ≤ 6), load its map height, redraw, cursor reset |
| Tab / Shift-Tab `0x0F00` | next (wrap to 0 when the next entry is missing) / previous (wrap to last) group, repeated until a friendly group; load its height; focus button index+2 when < 6, else 5 + (last split group ? (first MTM group > 3 ? 2 : 1) : (first MTM group > 3 ? 1 : 0)) |
| Alt-T | focus button 1 (clock), pressed |
| `f` `t` `w` | set **group 0's** fire order to 0/1/2 (even when another group is selected — `cmd_order_keys` has already set the selected group's) and, if group 0 is selected, show button 11/12/13 pressed |
| `c` `h` `s` `l` `i` `d` `v` | press buttons 14/15/16/19/20/21/22 when group 0 is selected |
| `p` `j` | press buttons 17/18 and redraw |
| `b` / `u` / `a` | **attack** by a selected boat/helo/aircraft (type 1/2/3): radio 'Boat Attack!' / 'Seawolf Attack!' / 'Black Pony Attack!'. A failed radio call with the radio damaged aborts; with the radio intact: boat 'Engine malfunction!' + voice 0x2E and abort, helo 'Experiencing Turbulence!' + voice 0x2D and abort, aircraft continues. If 2dbd:6487(group) reports the section hot: 'Hang on, Section hot!' + voice 0x2C. Otherwise status 5, focus 0x17/0x18/0x19 pressed, destination (leader mover +0x28) = `g_wp_support`, voice 0x31 |
| `k` | cease attack (friendly craft only, not group 0): radio 'Attack Ceased.'; status 6, focus 0x1A, destination = `g_wp_support` with x + 0x5DC00; voice 0x30 |
| `o` | loiter (friendly craft): radio 'Loitering.'; failure with radio intact → 'Engine malfunction!', voice 0x2E, status 4 anyway; success: status 4, focus 0x1C; boat: temporarily move the boat to the waypoint, snap it (2dbd:287f), take that position as destination and restore; others: destination = `g_wp_support`; voice 0x30 |
| `e` | extract (boat/helo selected), refused with 'Extract already in progress.' when a craft has status 2/3; boat needs water at `g_wp_support` (terrain 7 or 9, 1000:3757) else 'Extraction point must be on water!'; radio 'Boat Extracting.' / 'Helo Extracting.' (failures: 'Engine malfunction!' + 0x2E / 'Rotor damage!' + 0x2F); `g_extracting_group` = it, status 2, focus 0x1B, `g_extract_marker = g_wp_support`, destination = marker, `ent_team_rejoin`, voice 0x32 |
| `y` | emergency extraction by `g_emergency_group`: same busy check; `g_wp_support` = point-man position; boat needs water ('Extraction point must be on the water!'); radio 'Boat Extracting.'/'Helo Extracting.' to the selected group (only a damaged radio aborts); status 3, focus 0x1D, marker and destination as above, 1000:6990, `ent_team_rejoin`, voice 0x31 |

Voices are sound effects played at the point man by `snd_radio_ack(n)` =
`snd_play_sfx(n, 0x200, point-man pos, 1, 0, 0)`.

### 7.6 Map drawing

* `map_init_markers` (2B60, mission start): route nodes (20-byte entries at DS:0ED0):
  insertion MCI+0xA, objective 1 MCI+0x78, objective 2 MCI+0xDA (or the extraction point
  MCI+0x16 when MCI+0xD8 is 0 or −1), extraction MCI+0x16; `g_wp_seal`, `g_wp_split_a`,
  `g_wp_split_b` = objective 1; `g_wp_support` = insertion; `g_extract_marker` = extraction.
* `map_draw_routes` (30BA): dashed lines (line style byte 0x5A, colour 0xF) node0→1→2→3;
  with 2 splits a dashed colour-1 line from the last group's leader to `g_wp_split_b`; with
  ≥ 1 split a dashed colour-9 line from the (last, or second-last with 2 splits) group's leader
  to `g_wp_split_a`.
* `map_draw_markers` (3300), only points inside the map area:
  * objectives (MCI type ≠ 0): triangles, auto size, colours 4, 0xC, 0xE;
  * waypoint crosses (3 px): `g_wp_seal` '+' colour 0xF, `g_wp_support` 'X' colour 4,
    `g_wp_split_a` '+' colour 9 (≥ 1 split), `g_wp_split_b` 'X' colour 1 (2 splits);
  * craft groups (index 1.., types 1..3) with status 4 or 5: inverted triangle at their
    destination, colour 0x1F − 6·(index − 1);
  * then 1000:592d(1) and, for every member of every group whose world object is visible
    (obj+2 bit 0): enemy groups (4/5) — only the current target's unit or members of the
    selected group — dot colour 4; SEAL groups dot colour 1; other types not drawn. Dot =
    filled circle (2255:099a) of radius max(1, ((16000/(h>>8))>>4)·2). Live SEALs are also
    labelled with 1000:587a(unit, running index).
* `map_draw_triangle` (31E1): points (x−s, y+s), (x+s, y+s), (x, y) (±s negated when
  flipped); auto size s = ((16000/(h>>8))>>3)·4, min 2; polygon fill 2255:10fc.
* `map_draw_cross` (327C): flag 0 → lines (x−3,y−3)–(x+3,y+3) and (x−3,y+3)–(x+3,y−3);
  flag ≠ 0 → (x,y−3)–(x,y+3) and (x−3,y)–(x+3,y).

## 8. Insertion and re-insertion — `insert_keys` (5C8E)

* View 7 (insertion zoom): right button + mouse x rotates `g_insert_view_heading` by ∓80.
* View 0xC (re-insertion map): pointer handling exactly as on the map screen; the map zoom keys
  work (`map_zoom_keys`).
* `r`: in view 7 → switch to view 0xC (1000:23fb(0xC)), clear messages, sticky message
  'Press R to Reinsert.' (0x7800), select the insertion craft group. In view 0xC → if the
  insertion craft is a boat and `g_wp_support` is not water: 'Insertion point must be on the
  water!' and sticky 'Set New Insertion point. Press R ...'. Else move the craft leader and the
  point man to `g_wp_support`; boat: snap (2dbd:287f) and 4e87:0006(0xB400, 0, 0, heading, pos);
  helo: mover +0x18 = 6; face the team to the objective (365e:24dd), 2dbd:6371(0), restart the
  insertion (1000:0028), reset clock and input timers, select group 0. Other views: key passes.
* Enter: mode 0 → recurse with the focused button's hot key (unless it is Enter); mode 1 → set
  `g_wp_support` (the new insertion point) from the cursor. Space in mode 1 does the same.
* The mission loop skips the insertion automatically after `D7F2/D7F4` (1000:007a).

## 9. Cursor (19ac:2C97–37E9)

* Files `cursor.pic/.msk`, `wait.pic/.msk` (archive main.lib) and `sight.pic/.msk` (main4.lib).
  `.pic`: PXPK, depth 0x100, 16×14 pixels (stride field 8), pixel value 0xFF = transparent.
  `.msk`: 28 bytes = 14 rows × 2 bytes, big-endian bit order (bit 15 = leftmost pixel),
  1 = draw. The arrow's tip and the wait cursor's top-left are at (0,0) of the picture.
* `cursor_load_all` (2D7F, called at start-up by 1959:0395 and lazily by `map_load_screen`):
  `pxpk_load` each picture into DS:0F4C/0F58/0F64 and `cursor_load_mask` each mask into
  +0xA; create the two save-under bitmaps DS:0F70/0F7A (0x14 × 0x18); `g_cursor_page = 0`,
  both saved x = −1.
* `cursor_load_mask` (2C97): heap top-down mode, `ealib_load_far`; in video mode 6 every mask
  byte's two nibbles are remapped through DS:5076; otherwise the buffer is widened by doubling
  (`2255:106a` into a new block of twice the size) once per bit while `1 << i < g_pixel_scale`;
  then copied into a fresh block whose segment is returned.
* `cursor_draw` (36E0): remember (x,y) for the current page, copy the 20×19 screen area at
  (x−4, y−5) into that page's save bitmap, toggle the page, then draw: wait flag → wait picture
  at (x,y); sight flag → sight picture at (x−4, y−5); else arrow at (x,y); each 16×14 through
  its mask (2255:aa26); if the mask is missing the picture is copied unmasked at (x,y).
* `cursor_erase` (37E9): if the page has a saved area, copy it back to (sx−4, sy−5) and mark
  it empty. Two save slots exist because rendering alternates between two video pages.
* `cursor_set_wait`/`cursor_set_sight` set DS:0F8E/0F8F; `cursor_reset` clears both and the
  saved areas.

## 10. SEAL camp scene helpers (used by 365e:d285)

* `camp_load_scene(area)` (64D4): world load sequence with file id
  `g_area_camp_frame[area] + 0x1C` (1000:3efd, 1000:4291), then `camp_build_teams`.
* `camp_build_teams` (663A–6944, one function): clear the group table (as 365e team init),
  create a SEAL group and 4 members at structure 0's position (x>>8, z>>8 + 30), unit kinds from
  seg 5178 records (12 bytes each), member flags 0x117 for the first and 0x1F for others;
  insertion craft of type MCI+6 at the point man (+300 in z) → `g_insertion_group`; if MCI+8
  differs an extraction craft at (+0x708, +0x708) → `g_extraction_group`; fire support MCI+0x72
  at (+0x708 x, −0x708 z) → `g_fire_support_group`; if fewer than 3 craft and MCI+0x74 a
  break-contact craft at (+0x4B0,+0x4B0); if no emergency group a helo (type 8) at the
  extraction point; `g_fire_support_group` defaults to the emergency group. Every craft gets
  `grp_set_craft_status(group, 4)`.
* `grp_set_craft_status(group, s)` (651B): find the nearest pad structure (helo type 0xF,
  others 0xE) with 1000:3677; when s == 0 place the craft there (boat x + 0x5A00; helo altitude
  from its mover +0x1E, also copied to +0x18/+0x1A) and set its destination to that position;
  helo with s == 4 gets altitude 0x12C00; store s in +0x24.
* `camp_place_at_insertion` (6945): `g_extraction_group` = insertion group; park the craft
  (`grp_set_craft_status(craft, 0)`), point man to the craft's position, craft status 2; then
  the point man is moved to the pad (type 0xF helo / 0xE boat) nearest to him and then to the
  type-5 structure nearest to him, turned to face the craft (1000:3300), pushed 0x3C00 forward
  (`pos_move_polar`); team movement style 1 (2dbd:03c5), 2dbd:6371(g_seal_group).
* `camp_place_after_extraction` (6B07): craft = `g_extracting_group` (if not 0xFF, else
  `g_extraction_group`); park it, craft movement style 0; hide `g_seal_kia` team slots counting
  down from slot 3 (`unit_hide_chain`); point man to the pad nearest to the craft, turned towards
  the nearest type-5 structure and pushed 0x1E00 forward; team movement style 1.

## 11. Group and unit records (as used here)

**GROUP** (`g_groups[i]`, far): `+0x00` up to 8 far pointers to member units (NULL-terminated),
`+0x20` formation, `+0x22` fire order / split special, `+0x24` movement order (SEAL) or craft
status (0 parked, 2 extracting, 3 emergency extraction, 4 loiter, 5 attack, 6 cease attack),
`+0x26` type (0 SEAL, 1 boat, 2 helo, 3 aircraft, 4 Viet Cong, 5 NVA, 6 civilian, 7 friendly),
`+0x28` AI block (+0x82, +0x83 state bytes), `+0x2C` map height (i32, −1 = unset),
`+0x30` view distance, `+0x32` view heading. Groups 0..`g_first_mtm_group`−1 are friendly;
split groups are appended at the end (`g_split_groups` = 0..2).

**UNIT** (far): `+0x02` world object (`+2` flags, bit0 visible; byte `+3` bit 0x04 'locked as target';
`+6` position; `+0x12` heading·8), `+0x06` personnel/body record, `+0x0A` mover
(`+0x0C` heading in degrees, `+0x18` altitude, `+0x24` movement state byte, `+0x25` posture
0 Upright 1 Crouch 2 Prone 3 Dead, `+0x28` destination), `+0x12` AI record (`+0x66`, `+0x68`
group back-pointer, `+0x6C` flags), `+0x16` inventory header, `+0x1A` tool list,
`+0x1E` owning group, `+0x2C` roster record (`+0xC` name, `+0x62` number),
`+0x30` linked (carried) unit.

**Personnel/body record** (`unit+6`): bytes 0..7 skills (`+5` throwing accuracy, `+8`
experience used by hit rolls, `+9` throwing skill), `+0xC` morale/experience (≤ 90),
`+0x0E/+0x0F` wound bits (0x0F bit 0x40 = dead), `+0x12` light wounds, `+0x13` serious
wounds, `+0x14` dying flag, `+0x16` bleed-out timer, `+0x18` load, `+0x1A` class.
`unit_init_body(u, class)` (8617) for classes < 0 or > 4 except 8 sets defaults
(b8=200, b9=50, b10=60, b11=70, b12=1, b2=5, b0=15, b4=15, b1=b3=b5=10, b6=20, b7=30); always
clears +0xE, +0x12..+0x14 and stores the class.

Helpers: `grp_count_alive` (6317), `seal_count_alive` (6375), `unit_index_in_group` (6279),
`grp_index` (62D1), `grp_nearest_enemy_dist(group, pos)` (642C: distance to the closest leader
of a group on the other side, where "other side" pairs enemy types 4/5 with types < 4 or 7;
start value 3000), `unit_load_level` (63B9: L = b8/2 + b9, v = load/10: v > L → 3,
v > L/2 → 2, v > L/4 → 1, else 0), `unit_add_load` (640C), `unit_add_experience` (85FA:
+2n, cap 90), `unit_set_class_word` (612A), `unit_show`/`unit_hide_chain` (618D/61A7),
`grp_seglist_*`/`grp_from_object_id` (61E8/61FA/622C: world object id = 2 + 24·k,
k counted after the structure and misc objects; list DS:25F0 of 64 group segments).

## 12. Shots and damage (19ac:6CCA–89C3)

### 12.1 Weapon table (DS:48FE, 26 entries × 0x22)

`+0` short name, `+2` long name, `+4` category (skill selector), `+8` short range, `+0xA`
medium range, `+0xC` maximum range, `+0xE` magazine size, `+0x10` ROF flags (0x10 = grenade
class), `+0x12` blast radius (0 for bullets), `+0x14` structure damage, `+0x1C` jam value.
Order: M3A1 Greasegun, M16A2, CAR15, M76, M39 Hushpuppy, M63 Stoner, M60, M37 Ithaca, M45
Swedish K, M79, M203, M72 LAAW, DEMO satchel (12), M26 ×2, M15 WP, M18 smoke, M7 tear gas,
56SKS, AK47, SVD, K50, T.M33, T.42, RDG33, T.M32. `wpn_in_range(d, slot)` = d ≤ max range;
`wpn_best_range(unit)` = larger max range of current weapon and grenade.

### 12.2 Shot record (53BA:28BA + 0x3C·i, 32 records)

`+0x00` state (0 in flight, 1 done), `+0x02` shooter, `+0x06` shooter position,
`+0x12` shooter movement state, `+0x14` shooter posture, `+0x16` mode, `+0x18` target unit,
`+0x1C/+0x1E` target movement state/posture (0 when mode ≠ 0), `+0x20` 0, `+0x22` weapon type,
`+0x24` range (i32), `+0x28` rate of fire, `+0x29` rounds, `+0x2A` caller value,
`+0x2C` target position, `+0x38` trajectory/impact object from 1000:488e.

### 12.3 `shot_fire` (7124)

Find a free record (none → return). Jam check: if `random(100) ≤ max(1, table+0x1C >> 1)`:
set slot jammed (+8 = 1), spend 1 round, record only the trajectory (1000:488e with −1) and
the muzzle effect. Otherwise ROF = the slot's for the point man, else `wpn_ai_pick_rof`;
rounds per trigger: 1 (single/shotgun/other), 3 (semi), 20 (full), capped by rounds loaded.
Statistics for SEAL shooters: bullets (no blast, satchel, ROF 8, or M203 in rifle mode) add the
rounds to 'fired', others add 1 to 'explosives fired'. Fill the record, spend rounds
(`wpn_spend_rounds`), compute the trajectory, then the muzzle/sound effect (1000:87bf).

### 12.4 Resolution — `shot_update_all` (8556, every 0x40 ticks from the mission loop)

For every record in state 0 whose projectile arrived (1000:575a): clear the 16-entry victim
list (`far unit, band, kind`; kind 3 = empty, 0 person, 1 structure, 2 craft), collect victims
(`shot_collect_victims`), apply damage if any (result 3) else result 0, spawn the impact effect
(1000:5790), state = 1.

* **Aimed bullet** (`shot_aimed_hit_roll`): chance = shooter skill for the weapon category
  (jump table: categories 0,18 → skill byte 1; 1,2,4,5,9 → byte 0; 3,6,7,8,17,19 → byte 4;
  15 → byte 3; 16 → 5; others 0). Range bands (`shot_range_bands`): short = table+8 + a,
  medium = table+0xA + b, where (a, b) = `shot_throw_bonus` (only for thrown grenades, ROF 0x10:
  thrower skill +9 ≥ 60 → (21, 54), ≥ 30 → (9, 27), else (0, 0)); an M203 in rifle mode uses
  the M16's +0xA/+0xC values instead (no bonus). range < short → band 0; range < medium →
  band 1 and chance −10; else band 2 and chance −20.
  Victim = the obstacle hit on the way (impact object +0x2E when its +0x28 bit 2 is set and it
  is not the target, kind 1) else the target. Modifiers: shooter leg level 2/1 → −10/−5;
  caller value ≥ 75 → −30, 50..74 → −10, 25..49 → −5; ROF single −5, full +15; shooter posture
  prone +10, crouch +5; shooter movement 1/2 → −5/−10; target movement 1/2 → −2/−5; target
  posture prone −10, crouch +5; experience ≥ 60 → +30, ≥ 30 → +10; clamp 0..90; hit if
  `random(100) ≤ chance`.
* **Untargeted bullet** (`shot_line_victims`): scan the members of every group whose leader is
  within the weapon's maximum range; a member (not the shooter) within max range, inside the
  8° cone around the shooter's heading (1000:3c84), with obstruction value < 90 (1000:3d40) and
  not farther than the last accepted one (initially max range + 1) is **appended** (kind 2 for
  craft else 0, band 2) and becomes the new limit; then structures in the cone within the
  limit are appended the same way (kind 1). Returns whether anything was appended.
* **Explosive** (`shot_blast_victims`): radius R = table+0x12; for groups whose leader is
  within 16R, members within R (band 1 beyond R/2 else 0; kind 2 for craft else 0);
  structures with |distance − structure radius (1000:695a)| ≤ R (kind 1, band 0); at most 16.
* **Damage** (`shot_apply_damage`) — per victim entry (16 entries):
  * Bullets (weapon without blast radius, or M203 in rifle mode): person entries (kind 0):
    roll = random(100); point man and Player Wounds = Decreased → −15 (floor 0); enemy (group
    type 4/5) hit by full-auto fire → +40; enemy and Enemy Wounds = Heavy → +15 (cap 100);
    wound = bullet table of the entry's band; point man and Player Wounds = None → 0 (unless
    miss); enemy and Enemy Wounds = Death → 0x4000; if the wound is > 0, the shooter is in a
    SEAL group, rounds > 0 and the victim was alive: 'hits' += random(rounds) + 1;
    apply 1000:521c(victim, wound, 0, kind). Other non-empty entries (structures/craft):
    1000:521c(obj, 0, rounds·2, kind).
  * Explosives: person entries: roll = random(100); point man −15 (Decreased); enemy +25 when
    Enemy Wounds = Heavy; enemy +10; cap 100; weapon category 11 or 12 (smoke) → roll 0,
    13 (WP) or 14 (tear gas) → roll 1; wound = blast table of the band; SEAL shooter, wound > 0,
    ROF ≠ 8 and not the satchel → 'explosive hits' += 1; Player Wounds = None and Enemy
    Wounds = Death as for bullets; apply. Other entries: 1000:521c(obj, 0, table+0x14, kind).
  * Tables (seg 525F, 16 entries of `{lo, hi, wound bits}` each, scanned until
    `lo ≤ roll ≤ hi`; −1 = miss, 0 = no effect, 0x4000 = killed; the scan continues into the
    following tables, so a roll of 0 ends on the all-zero entry at 525F:0154): bullets band 0/1/2
    at 0000/0044/0088, blast band 0/1 at 00CC/0110. Contents (first match wins): bullet band 0:
    1–3 → 0x10, 4–6 → 0x20, 7–9 → 1, 10–12 → 2, 13–15 → 0x100, 16–18 → 0x400, 19–21 → 0x800,
    22–24 → 0x1000, 25–27 → 0x2000, 28–31 → 0x40, 32–35 → 0x80, 36–39 → 4, 40–43 → 8,
    44–50 → 0x200, 51–100 → 0x4000. Band 1: 1–34 miss, 35–85 none, 86..98 one roll each →
    0x20, 1, 2, 0x100, 0x20, 1, 2, 0x100, 0x40, 0x80, 4, 8, 0x200; 99–100 → 0x4000. Band 2:
    1–50 miss, 51–86 none, 87..97 → 1, 2, 0x100, 0x20, 1, 2, 0x100, 0x10, 0x20, 1, 2,
    98 → 0x100, 99–100 → 0x200. Blast band 0: 1–3 none, 4–5 0x10, 6–7 0x20, 8–9 1, 10–11 2, 12–19 0x100,
    20–21 0x400, 22–23 0x800, 24–25 0x1000, 26–27 0x2000, 28–33 0x40, 34–37 0x80, 38–42 4,
    43–46 8, 47–50 0x200, 51–100 0x4000. Blast band 1: 1–15 none, 16–20 0x10, 21–25 0x20,
    26–31 1, 32–37 2, 38–51 0x100, 52 0x400, 53 0x800, 54 0x1000, 55–56 0x2000, 57–58 0x40,
    59–60 0x80, 61–63 4, 64–66 8, 67–70 0x200, 71–100 0x4000. The port should read these
    tables from the executable.
* `combat_random_wound` (7F58): roll = random(100); rolls < 16 become 0x15 or 0x17 (odd/even);
  wound from band 1 (525F:0044); point man with Player Wounds = None gets 0.
* `shot_scatter` (7BA8): n = max(1, 19 − skill5/5 + (leg level 2 ? 10 : level 1 ? 5 : 0));
  offsets random(n)·3 in x and z, each negated when random(100) < 50, shifted << 8.
* `ai_target_priority` (746D): p = ((900 − d)/50)·30 + 10 (integer division, cap 80); minus
  the caller value when it is < 95 else p = 0 (floor 0); +20 against craft; −5 for a lone
  target group, else +5·(size−1); + random(20); 127 if the target is the point man's target
  and the shooter's group fires At Target; 0 for SEAL shooters whose group has Cover Fire when
  the target's AI flag 0x10 is set; finally negative → 0 and values above 127 → 126 (so a
  forced 127 survives). The group size is 4a37:066e of the target's group (group 0 when the
  target is the point man).

### 12.5 Wounds and medical

`med_wound_level` (87B8: dead 0, dying 2, else weighted wound-bit count capped at 2),
`med_status` (888C), `med_leg_level` (88C6), `med_arm_count` (893B), `med_head_count` (8971).
`med_treat(medic, patient)` (86B3): needs a usable medical kit (1000:8b64 > 0). Clears the
dying flag if set; independently serious ≥ 2 → −2, serious 1 → 0 plus one light wound removed;
only when neither applied: light ≥ 2 → −2, light 1 → 0. If anything was treated the kit is
consumed (1000:8baa(medic,1,1)); returns the number of treatments (0..2).
`med_bleed_tick(dt)` (89C3, every 0x400 ticks): for each live dying unit, timer −= dt; when the
timer ≤ dt the unit dies (2dbd:0a9d, AI cleanup 4a37:1ed6, dead flag 0x40).

## 13. Tools and misc

* Tools (seg 52E3, 6-byte entries `+0` short name, `+2` long name, `+4` weight): 0 Radio,
  1 Medical Kit, 2 Prisoner Handling Kit, 3 Flare, 4 Night Vision Scope. A unit's tool list is
  linked through `+4` (far). `item_use_tool` (0934): Radio → only 'Radio damaged.' when broken;
  Medical Kit → `med_treat(point man, point man)`; PHK → 2dbd:4eca; message 'Using the ' +
  long name + '.' (0x100). `item_cycle_tool` (0A3A) moves the head to the tail.
* Strings: `str_utoa` (5F6A), `str_itoa_pad(v, buf, w, signed, zero)` (5F83; negative with
  w = 0 gives ''), `str_set_ext` (607A), `str_fstrcat` (60CF).
* Awards: `awd_evaluate_mission` (00B6) runs after extraction (1000:00b0): tally casualties,
  then 365e:deb0 with the last point-man history record (`g_campaign+0x2A + 12·i`, i = roster
  missions − 1), `awd_sum_block_scores()` (sum of the int32 scores `+0x2C` of the point man's
  records in his current block of 20), year/mission, 0, (mission−1)/20, (mission−1)%20 and
  pointers to DS:EEC6..EEC8; outputs also DS:D7E6/D7E8.

## 14. Unlisted / mis-split functions

* `19ac:2E1D` `cursor_show_now` and `19ac:6506` `camp_unload_scene` have no direct callers
  (possibly dead code or reached through pointers).
* `19ac:651B` is the real entry of the function Ghidra lists at `6544`; `6690`/`6713` are
  parts of `663A`; `6D44` is part of `6D43`; `744E` is the tail of `7124`. The C export for
  `016D`, `612A` and `7C8F` is garbled by jump tables: jump tables live at `19ac:0309`
  (main loop, 9 entries), `19ac:613D` (unit class, 10 entries) and `19ac:7CB2` (skill
  selector, 20 entries).

## 15. Open questions

* Exact meaning of several AI/state bytes shown in the enemy Team Info rows (`AI+0x82/0x83`,
  the `g_enemy_alert_abbrev` table) and of `unit_set_class_word` values (0x862A etc.).
* 365e:8095, 9aa6, abb9, d285, d659, b511 and deb0 were named from context only (new campaign,
  load, marching order, camp, debrief, end-of-mission, awards).
* The jump in `map_step_focus`/`map_hit_test` for buttons 8–10 (third team-list page) is never
  reached with the shipped data; whether more than six friendly groups can exist is unknown.
* The purpose of the caller value `shot +0x2A` (7124 argument 4, 0 for the player) in hit rolls.
