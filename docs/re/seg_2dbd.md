# Segments 2dbd and 348e — mission events, casualties, soldier sprites

Status: 2dbd:0006–13c1, 161e, 168b, 3f4c and small helpers are fully analysed; 348e is fully analysed;
the sprite file formats are verified against all shipped files. The large 2dbd functions marked
PROVISIONAL in `tools/re/symbols/seg_2dbd.tsv` (1b05, 1c04, 16e6–18b2, 261e, 297d, 3232, 352c, 3684, 3d52,
41a4, 4088, 4429, 4c74, 4eca–5327, 54f8, 58c1, 5a91, 651d, 6653) are named from strings, callers and a first
read only; their exact rules still have to be written up here.

Conventions: times are in ticks of the 256 Hz mission clock `g_time` (DS:eca6, int32); `random(n)` is
2255:78d6 = `(rand16() & 0x7fff) % n` (additive lagged-Fibonacci generator, see seg 2255). Headings are
either whole degrees (0..359) or 1/8 degree (0..2879); both occur and are noted.

## 1. Module purposes

* **2dbd** – in-mission event logic for people: movement mode / posture / speed rules, casualty
  handling (killed, bled out, wounded, suppressed), helper (buddy) assignment, prisoner escape,
  emergency extraction, reload callouts, "Dive Quickly!", terrain/booby-trap events, searches,
  prisoners, medical aid, per-team updates, ordnance and support-fire updates. Entry point per frame:
  `evt_mission_tick` (2dbd:3f4c), called from the mission loop 1000:01de and 365e:d285.
* **348e** – soldier sprites: animation state machine, loading of soldier/headgear/effect sprite sets
  (.RLE/.RLX from the `.lib` archives, optionally kept in EMS), and the draw callback of the billboard 3-D
  models (human, bush, jungle, exgr, muzzle). The strings "View Buffer Too Small"/"Object List Too Small"
  (DS:063d/0653) are not used here; the summary heuristic matched the constant 0x640.

## 2. Data structures (fields used by these segments)

A unit is a 0x34-byte entity (ent_alloc 4511:019e) with optional components:

| off | type | meaning |
|---|---|---|
| +0x02 | far* | world object: +0x06 position = 3 x int32 (x @+0, altitude @+4, y @+8, 1/256 units), +0x12 heading (1/8 deg) |
| +0x06 | far* | STATUS (0x1c bytes): +0x08,+0x09 u8 physical attributes; +0x0e u16 hit-location mask, bit 0x4000 = dead (byte +0x0f bit 0x40); +0x12 u8 light-wound count; +0x13 u8 heavy-wound count (non-zero = "heavily wounded"); +0x14 u8 bleeding flag; +0x16 i16 bleed time left; +0x1a i16 (>=5: wound handler ignored) |
| +0x0a | far* | MOVER (0x48 bytes), see below |
| +0x0e | far* | ANIM (0x26 bytes), see 4.2 |
| +0x12 | far* | AI (0xd0 bytes): +0x66 u8 surrendered (drawn with hands up), +0x68 far* group, +0x6c flags |
| +0x16 | far* | weapon list header: +4 far* current weapon node (+0 u8 weapon kind, +4 u16 reloads left; next node at +0x0e) |
| +0x1a | far* | item list (node: u8 type, +2 i16 quantity, +4 far* next); type 2 = Prisoner Handling Kit |
| +0x1e | far* | owning TEAM |
| +0x2c | far* | personnel record: +0x0c name, +0x62 u8 headgear style, +0x74 stats (+8,+9 attributes, +0x18 load*10) |
| +0x30 | far* | buddy link (casualty <-> helper, prisoner <-> escort) |

MOVER: +0x00 i16 current speed; +0x02 target speed; +0x04 base speed; +0x0a acceleration; +0x0c heading
(deg); +0x0e desired heading; +0x18 i16 vertical offset (negative = sunk, see 4.4); +0x1a eye-height
offset (Point Man); +0x24 u8 move mode (0 stop, 1 slow, 2 fast/run, 3 backwards); +0x25 u8 posture
(0 upright, 1 crouch, 2 prone); +0x26 u8 flags (0x80 is a helper/escort, 0x40 on the Point Man suppresses
music changes, 0x20 captured prisoner, 0x08 wading in water, 0x04 in grass/brush, 0x02 other slow terrain);
+0x28 12-byte destination; +0x34 i16; +0x36 aim heading (deg); +0x38 fatigue 0..8; +0x3a exhaustion 0..8;
+0x3c int32 "recently fired" until-time (now+0x100 on firing); +0x40 int32 heavy-weapon until-time
(now+0x200); +0x44 int32 posture-change until-time (now+0x200).

TEAM: member far pointers from +0 (NULL-terminated, member 0 = leader; team 0 member 0 = Point Man =
player), +0x20 formation, +0x22 result (2 = Point Man dead), +0x24 order/state (3 extract, 4 search, 6 ...),
+0x26 type (0 SEAL squad, 1/2 support craft, 4/5 enemy/civilian groups that can be captured, 6/7 other).
`g_teams` (DS:12e2) is the NULL-terminated array of team pointers.

## 3. 2dbd rules (analysed functions)

### 3.1 Speed, posture, move mode

`evt_set_move_mode(unit, m)` (03c5): "people" = team type 0, 4 or 5. v = people ?
`evt_calc_move_speed(unit, mover.base)` : mover.base. m=0: target 0; m=2: if people and posture != 0 then
`evt_set_posture(unit,0)` and `spr_set_anim(unit,0)` first, target v; m=1: v>>2 (arithmetic); m=3: -(v>>2).
Store mover.mode = m.

`evt_calc_move_speed(unit, base)` (01a7): penalty p =
  +2 if flag 0x04; +1 if flag 0x08 or 0x02; +2*load_level (19ac:63b9 on personnel+0x74: w = load/10,
  c = attr8/2 + attr9; 3 if w > c, 2 if w > c/2, 1 if w > c/4, else 0); + mover.fatigue;
  if flag 0x80: +1, then +8 if the buddy is dead, else +4 if the buddy is heavily wounded;
  if now < mover+0x3c: +2 and mover.exhaustion += 1 (side effect); if now < +0x40: +2; if now < +0x44: +2;
  posture prone +8, crouch +2.
  bonus = max(0, (status.attr8 + status.attr9 - 0x50) / 10); result = max(0, base + bonus - p).

`evt_set_posture(unit, p)` (0307): if p differs from mover.posture: mover+0x44 = now+0x200 and re-apply
the current move mode (speed recalculated). Store posture. If mode is 2 and p != 0 switch to mode 1. For
the Point Man: if mode 2 and p == 2 eye height 0, else unless flag 0x04 eye height = {15,9,3}[p].

### 3.2 Helper assignment `evt_assign_helper(unit, team)` (004e)

1. NULL unit or team -> NULL.
2. If the unit itself is a helper (flag 0x80): its patient loses it (patient.buddy = NULL), the patient is
   given a new helper recursively (and linked back), unit.buddy = NULL, flag 0x80 cleared.
3. If unit.buddy is set, return it.
4. Try team slots 2, 1, 3, 0 (DS:089c): member exists, not dead, heavy-wound count 0, buddy NULL, not the
   unit. First match gets flag 0x80 and is returned; none -> NULL. Callers set both buddy links.

### 3.3 Casualty handlers

Called from the hit resolver 1000:521c: near miss -> sfx 0x0c + `evt_unit_suppressed`; wounded ->
sfx 0x0b + `evt_unit_wounded`; dead -> `evt_unit_killed`. The bleed tick 19ac:89c3 calls
`evt_unit_bled_to_death` when a bleeding unit's time runs out.

"Role name" = DS:1c8c[slot]; "<role> <name>" uses personnel+0x0c. Under-fire signal: for team-0 members
other than the Point Man, if now > g_under_fire_signal_time (DS:0898) the Point Man shows hand signal 4
"Under Fire" (1000:7d6f). All team-0 casualties then set DS:0898 = now+0xf00 and call
`evt_check_prisoner_escape(unit)`.

**Killed** (0684): ignore if anim return-state is already 3. Mode 0, current speed = target speed,
anim+0x0a = 0, `spr_set_anim(3)`, posture 2. random(6)==0 -> scream sfx 0x34 (team type 0 or 7) else 0x26,
lifetime 0x300, at the body, attached to the unit.
- Team 0, Point Man: flush messages, "The Point Man is dead." (0xa00 ticks), "Campaign Ended" (0x5a00),
  team0.order = 0, extraction ordered at his position (365e:1800), 1000:6990, horizontal shake 0x33 ticks,
  palette level 0x100 (black), team0.result = 2.
- Team 0, other: "<role> <name> is dead." (0x200); buddy = evt_assign_helper(unit, team 0), linked both
  ways; if the helper is the Point Man "Pick him up." (0x200); under-fire signal; if the Point Man's mover
  flag 0x40 is clear: stop music, music 3.
- Other team with type != 0: promote a new leader (365e:1a2f); if no member of the group is alive and the
  type is 4 or 5 and the Point Man flag 0x40 is clear: stop music, music 4.
- Always finally `evt_check_emergency_extraction(unit.team)`.

**Bled to death** (0a9d): as Killed but no scream, no music changes, and the Point Man case uses vertical
shake 0x55 instead of horizontal 0x33 (palette still 0x100).

**Wounded** (0d6f): ignore if status+0x1a >= 5. Mode 0, anim+0x0a = 0, desired heading = mover+0x34,
posture 2, anim 2. random(6)==0 -> pain sfx 0x35 (type 0/7) or 0x27, lifetime 0x200; else random(5)==0
and type 4/5 -> sfx 0x29.
- Not team 0: only if team type 0, alive, not a prisoner and heavily wounded: assign a helper from its own
  team (no messages).
- Point Man (alive): "Wham!" (0x100) if no heavy wound, else "The Point Man is bleeding heavily!" (bleeding
  flag) / "The Point Man is heavily wounded." (0x200); horizontal shake 0x2a; palette level -0x100 (red
  flash, fades back through the palette tick).
- Other team-0 member alive: no heavy wound -> "<role> <name> has been hit!"; else "... is bleeding
  heavily!" / "... is heavily wounded!" (0x200), helper from team 0, "He needs help." if the helper is
  the Point Man; then the under-fire signal.

**Suppressed** (12e7): mode 0, anim+0x0a = 0, posture 2, anim 2. Team 0: Point Man -> "Suppressed!"
(0x100) and vertical shake 0x2a; others -> under-fire signal.

**Prisoner escape** (0520): unit has a buddy, unit is team type 0 with flag 0x80, buddy has flag 0x20 and
buddy's team type is 4/5; escape if (unit has no PHK and random(100) < 50) or the unit is dead. Escape:
prisoner.buddy = NULL, clear 0x20, AI surrendered = 0, prisoner mode 2 (runs), unit.buddy = NULL, clear
0x80, "A prisoner has escaped." (0x400), g_prisoners_held--.

**Emergency extraction** (0632, argument = team): team type 0, no craft already moving (365e:1875), living
SEALs over all type-0 teams <= 2 -> extraction at the Point Man's position, 1000:6990, "Extracting!" (0x400).

### 3.4 Weapon handling helpers

`evt_start_reload` (13c1): only if weapon class (DS:4902[kind*0x22]) is not 0x10/0x0f, kind != 0x0c and
reloads left != 0. With an anim component: mode 0, anim+0x0a = 0, posture 1, anim 1 then anim 16 (crouch
reload, 0x300 ticks). Team-0 units: reload sfx 0x0d (0x100); non-Point-Man with reloads <= 2:
`<role> <name>: "One reload remaining."` (reloads == 2) else `"Out of reloads!"` (0x200).
`evt_crouch_to_fire_launcher` (161e): mode 0, crouch, anim 1 then 17. `evt_crouch_to_place_charge` (168b):
mode 0, crouch, anim 1. Both called by the fire routine 1000:488e (kind 0x0c -> 168b, class 0x0f/0x10 ->
161e, weapon-table byte +0x0c == 0x10 (thrown) -> `spr_start_throw_anim`).

### 3.5 Mission tick `evt_mission_tick` (3f4c)

1. `evt_update_support_fire` (297d).
2. If now - DS:0a6e >= 0x3c00: DS:0a6e = now, `evt_minute_job` (352c). (`evt_trigger_minute_job` 3517 sets
   DS:0a6e = now - 0x3c00.)
3. If now - DS:0a72 >= 0x400: DS:0a72 = now, `evt_update_special_actions` (3684).
4. 3232, 6653, 3d52, 1c04 in that order.
5. `evt_update_team(i)` for every team index.
6. For each of the 32 slots of DS:2670 with flag +0x29 bit 7: `evt_update_ordnance`.
7. For each of the 8 slots of DS:3100 with flag +2 bit 0: `evt_update_area_effect`.
(The "fire" of step 2/3 is skipped only in the degenerate case now == 0.)

### 3.6 Small helpers

`evt_approach_value(pair, rate)` (2503): step = rate*g_frame_ticks>>8 (at least 1); pair[0] moves by step
toward pair[1], snapping when |diff| <= step. `evt_approach_angle` (2580) does the same on degrees going the
short way (adds/subtracts 360 to the target, re-normalises the result). `evt_heading_diff(a,b)` (0006)
and `evt_heading_is_cw(a,b)` (2933) work on 1/8-degree headings converted with >>3. `evt_turn_heading`
(5a25): code 1 -> -90, 2 -> +90 (only when the craft flag +0x20 != 1), 3 -> +180, result mod 360.
`evt_place_team_formation` (6371): member k gets leader position + rotate(leader heading, spacing*3 *
table[form][k]) where table = DS:07dc + formation*4 (two words per member, 10 words per formation) and
spacing = byte DS:0ade[team type].

## 4. Segment 348e — soldier sprites

### 4.1 Sprite-set tables (far segment 53ba, also in g_spr_table_seg)

A set is an array of FRAMEs of 0x50 bytes: +0x00 far ptr[8] conventional image per rotation (NULL when in
EMS), +0x20 int16[8] EMS handle (-1 none), +0x30 far ptr[8] 12-byte RLX record (NULL if none).
`spr_get_frame_image(set,f,r)` returns `ems_map(handle)` when the handle is not -1, else the pointer.

53ba layout: 0x010e fxmu (1 frame, conventional) | 0x015e soldier sets | 0x137e + 0x190*(h-1) headgear
h=1..7 (5 frames, conventional) | 0x1e70 hnds (5) | 0x2000 exsm | 0x2050 impc | 0x20a0 exlg | 0x20f0 fxsm.
Effect sets are loaded by 1000:6a9c.

Soldier sets (offset from 53ba:015e, frames): us +0x000 (8), cs +0x280 (6), ps +0x460 (4), u +0x5a0 (1),
c +0x5f0 (1), p +0x640 (1), uq +0x690 (6), uc +0x870 (2), uf +0x910 (2), ua +0x9b0 (2), uw +0xa50 (2),
up +0xaf0 (2), unused +0xb90 (2), cp +0xc30 (2), cf +0xcd0 (1), ca +0xd20 (2), cr +0xdc0 (2),
cw +0xe60 (2), pi +0xf00 (1), pa +0xf50 (2), d +0xff0 (1), ds +0x1040 (4), dp +0x1180 (2).
Headgear h: 1 band, 2 bere(t), 3 bush hat, 4 flop(py hat), 5 hbnd, 6 coni(cal hat), 7 pith helmet.

Loading (`spr_load_soldier_sprites` 19b0): g_blit_remap_on = 0, ems_init, g_ec82 = 53ba:015e, sets loaded
with `spr_load_set_rlx(set, nframes, name, digitpos)` (digit position = strlen(name)+3) in the order
u, c, p, us, uq, cs, ps, cp, uf, ca, pi, cf, ua, uc, up, pa, cr, cw, uw, d, ds, dp; then the headgear
names from DS:1d4e (band, bere, bush, flop, hbnd, coni, pith) with `spr_load_set(..., 5 frames, pos 7,
no EMS)`. Per image: name = `<set>f1r1`, frame digit at pos-2 = '1'+f, rotation digit at pos = '1'+r,
extension set with 19ac:607a (replaces from the first '.'). `.rle` is loaded to a far block
(ealib_load_far); rlx variant always, plain variant only if its flag is set: with `heap_set_alloc_top`
around the load, `h = ems_alloc(size)`, if `ems_map(h)` succeeds copy the image to EMS and free the block.
The `.rlx` file is loaded, its first 12 bytes copied into a new 12-byte far block, the file buffer freed.
`spr_free_set` walks frames and rotations backwards freeing RLX blocks and conventional images (or EMS
blocks when its flag is set). `spr_free_headgear_sprites` frees the 7 headgear sets.

### 4.2 Animation state machine

ANIM component: +0x00 u8 0; +0x01 u8 kind (colour scheme, 4.4); +0x06/+0x08 posture (0,1,2,3=dead);
+0x0a i16; +0x0c int32 creation time; +0x10 far* sprite sets; +0x18 state; +0x1a return state;
+0x1c int32 start time; +0x20 duration; +0x22 int32 clock. `spr_init_unit_anim(unit, kind)` (158c):
sets = 53ba:015e, +0x0c = now, +0 = 0, +1 = kind, posture/state/return = 0, clock = random(256).
`spr_advance_anim_clocks` (000e): every frame clock += g_frame_ticks for members of teams of type 0 or >= 4.

`spr_set_anim(unit, s)` (00c8), nothing if no anim component. start := clock where noted:

| s | condition | state, duration, return |
|---|---|---|
| 0 | posture 1 / 2 / else | 5, 0x80 / 8, 0x80 / 0; return 0, start |
| 1 | posture 0 / 2 / else | 4, 0x80 / 8, 0x80 / 1; return 1, start |
| 2 | posture 0 / 1 / else | 6, 0x100 / 7, 0x80 / 2; return 2, start |
| 3 | posture 0 / else | 9, 0x200 / 13, 0x100; return 3, start |
| 9 | posture 0 only | 9, 0x100, return 3, start |
| 11,12 | posture 0 only | s, 0x100, return 0, start |
| 13 | posture 1 only | 13, 0x100, return 3, start |
| 14,15 | posture 1 only | s, 0x100, return 1, start |
| 16,17 | posture 1 only | s, 0x300, return 1, start |
| 18,19 | posture 2 only | s, 0x100, return 2, start |
| other | – | state = return = s (no timing) |

Afterwards, if s <= 3, anim posture (+6 and +8) = s. `spr_start_throw_anim` (0085): posture 0/1/2 ->
s = 12/15/19.

`spr_update_anim` (0302): if state == return -> 0. Else if clock - start < duration -> 1. Else
start = clock; if state == 8 and posture == 0 (prone to upright): posture = 1, `spr_set_anim(0)`
(continues with crouch-to-upright), return 1; otherwise state = return, return 0.

### 4.3 Frame selection (`spr_draw_billboard_cb` 0c52, args item, screen x, screen y)

1. Return if g_view_mode is 1 or 0x0c. Scale (8.8, 0x100 = 1:1) = projected x of a reference vector
   (0x60 units lateral at the object's depth: object +0x0a << 16 >> object +0x05) minus g_view_center_x,
   clamped to 0x640. Return unless -0x640 <= x <= 0x780, -0x640 <= y <= 0x708 and scale > 1.
2. unit = 19ac:622c(object model record). If unit has no anim component return. Heading (1/8 deg) =
   mover+0x36*8 while mover+0x3c - 0x80 > now (just fired: face the aim), else object heading +0x12.
3. Rotation g_spr_rotation = ((( (180 - heading/8 + bearing(viewer, object)) mod 360) + 22) mod 360) * 8
   / 360, where bearing is 1000:3300 in degrees. Rotation 0 = facing the viewer.
4. If anim kind >= 3: return if scale <= 2; remap on with table far ptr at DS:(0xde + 4*kind) for kind < 8,
   DS:(0x116 - 4*kind) for kind >= 8 (3->remap2, 4->remap3, 5->remap4, 6->remap5, 8->remap5, 9->remap4,
   10->remap3, 11->remap2; tables loaded by 1959:0265).
5. No unit -> `spr_draw_scenery_billboard`. Else scale += (status.attr8/20 - 3) * (scale >> 4).
6. "Surrendered" = AI block present and AI+0x66 != 0. If `spr_update_anim` returns 1 and not surrendered,
   draw the timed animation of state s (e = clock - start, lmod keeps the sign of e; |x| where noted):
   4/5 uc f=|e%0x80|>>6 (5: 1-f), clamp 1; 6 up |e%0x100|>>7; 7/8 cp |e%0x80|>>6 (8: 1-f);
   9 uf |e%0x200|>>8; 10 invisible; 11 ua 0, 12 ua |e%0x100|>>7; 13 cf 0; 14 ca 0, 15 ca |e%0x100|>>7;
   16 cr (|e%0x300|>>8)%2; 17 cw ((e%0x300)/0x110)%2; 18 pa 0, 19 pa |e%0x100|>>7; >19 invisible.
7. Otherwise (c = anim clock):
   - mode 2 and mover posture 0: f = (c%0x100)/0x2b; surrendered -> ds f%4, else uq f.
   - mode 0: state 1 -> surrendered: set d frame 1 (i.e. image DSF1), else c; state 2 -> dp 0 / p;
     state 3 -> pi; other -> d 0 / u.
   - else by anim posture: 1: f = |c%0x300|>>7 (backwards 5-f), ds f%4 / cs f; 2: f = (c%0x300)/0xc0
     (backwards 3-f), dp f%2 / ps f; else f = (c%0x300)/0x60 (backwards 7-f), ds f%4 / (flag 0x08) uw f%2 / us f.
8. `spr_draw_soldier_frame(x, y, scale, set, f, rotation, unit)`; remap off.

### 4.4 Drawing

`spr_sprite_topleft(x, y, w, h, s)`: left = x - w/2, top = y + (5*s>>8) - h.
`spr_draw_scaled(x, y, s, img)`: w = img.w*s>>8, h = img.h*s>>8, blit at spr_sprite_topleft.
`spr_draw_body` (07cc): body anchor (RLX +2,+4) is placed at (x, y + 5*s/256): X = x + s*(w/2 - ax),
Y = y + s*(h - ay); the viewport bottom is temporarily lowered to min(bottom, y + 5*s/256) (ground line),
Y += sh - ((8 - depth)*sh >> 3) with sh = s*h (sinks depth/8 of the height below the ground line).
`spr_draw_helmet` (065c): hat type 0 = none; image = headgear set (53ba:0x11ee + 0x190*type), frame
RLX+6, rotation = head rotation; placed with its bottom centre at body pixel (RLX+8, RLX+10) using the same
anchor, ground clip and sink; for s < 0x100 add (s*head_y>>8)&1 to y.
`spr_draw_soldier_frame` (089f): hat = 2 (beret) for the Point Man; team type 4/5/6 -> 7 (pith) for type 5
else 6 (conical); team type 0 -> v = personnel+0x62 % 6, v 0/1 -> 3 (bush) else v; others none. Head
rotation from `spr_calc_rotation(mover+0x36*8, pos)`, limited to one step from the body rotation
(greater -> (r+1)%8, smaller -> |r-1|%8). Depth = `spr_get_sink_depth` = mover+0x18/-3 if negative, else 0.
If RLX+7 > 0x80 draw helmet then body, else body then helmet; remap off; `spr_draw_grass_tuft`.
`spr_draw_grass_tuft` (0443): only in mode 2, and (flag 0x04 or depth != 0), and not a non-captive of team
type 4/5: w = body width - random(2) (if non-zero), h = grass height*s>>8 - random(4) (if non-zero),
Grass.RLE drawn with remap4 at (x - bodyW/2, y + 5*s/256 - h/2).
`spr_draw_scenery_billboard` (0a90): model 0x878e -> Jungle.RLE (detail >= 4), 0x6dcc -> Bush.RLE
(detail != 0); model owned by an ordnance object (1000:461a, slots +0x40/+0x44/+0x48/+0x4c): muzzle
flash fxmu[rotation] at 2x scale centred (skipped for the Point Man's own weapon in view mode 0, remap5
for kinds 0x0f/0x10/0x14), explosion (1000:6dd5), smoke (1000:6f4d), impact (impcf1r1 at scale/4); model
0x95f0 (muzzle) -> fxmu[(rotation+4)%8] with remap4; model 0x7c02 draws nothing.

## 5. Sprite files

### 5.1 .RLE (little-endian)

```
u16 width, u16 height
u16 key        transparent colour, low byte (0x00FF in all files)
u16 reserved   0x0100 in all files, unused
per row: u16 n, then n bytes: c = byte; if c & 0x80: repeat next byte (c & 0x7f) times
                                          else: copy the next c bytes
```
Verified on all 934 RLE files in re/assets: each row yields exactly `width` pixels, consumes exactly `n`
bytes, nothing follows the last row; counts 0 and 0x80 never occur. The blitter (2255:b0d6, mode-X only)
scales with a DDA per axis: `acc = src/2 + dst; cur = 0; per destination line { while (acc < src) { cur++;
acc += dst } emit(cur); acc -= src }`; with the remap flag set each colour goes through the remap table
before the key test. When rows are clipped at the top the first visible source row is one later than the
formula (visible only when enlarging). Palettes: soldier/effect sprites use the mission palettes
main/PAL2.PAL (PALN/PALS variants); port, mdl, lbtn and bull art use main/ST.PAL (6-bit VGA, x*255/63).

### 5.2 .RLX (18 bytes, first 12 used; soldier sets only)

+0 u16 0; +2 u16 anchor x (ground point); +4 u16 anchor y; +6 u8 headgear frame (0 upright F1, 1 prone F2,
2/3 tilted F3/F4 used by falls, 4 lying F5); +7 u8 layer (>0x80: headgear behind body; only one PI image
has 0xA0); +8 i16 head x; +10 i16 head y; +12..17 zero.

### 5.3 Naming

`<set>F<frame>R<rotation>.RLE/.RLX`, frame 1-based, rotation 1..8 = 45-degree view steps (R1 front,
R5 back). Library = set name. Set meanings: u stand, c crouch, p prone, us walk, uq run, cs crouch-walk,
ps crawl, uc stand<->crouch, up dive to prone, cp crouch<->prone, uf fall when hit (upright), cf fall
(crouch/prone), pi lying dead, ua/ca/pa throw (upright/crouch/prone), cr crouch reload, cw crouch
shoulder-fire, uw wading (weapon over head), d hands up, ds walking with hands up, dp prisoner prone.
Headgear `BAND/BERE/BUSH/FLOP/HBND/CONI/PITH F1..F5` live in u, p, uf, cf, pi libs. Effects: fxmu muzzle
flash (8 directions), fxsm smoke, exsm/exlg explosions, impc impact, fxhand `HNDS F1..F5` hand signals
(R = animation frame), plus Jungle.RLE, Bush.RLE, Grass.RLE, impcf1r1.RLE.

## 6. Open questions

* Exact rules of the PROVISIONAL 2dbd functions (terrain/booby traps 1c04, support fire 297d, ordnance
  4429/4c74, commands 41a4, special actions 3684/6653, search/prisoner/medical 4eca–651d, team update 5a91).
* Mover flags 0x04/0x02 terrain meaning and who sets mover+0x18 (sinking) are inferred, not confirmed.
* Status+0x1a meaning (wound handler ignores values >= 5).
