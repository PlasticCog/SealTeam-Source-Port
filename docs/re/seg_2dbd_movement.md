# Segment 2dbd (part 3) — movement, terrain, fatigue, booby traps

Companion to `seg_2dbd.md` and `seg_2dbd_teams_ai.md`. Conventions:
`mover = unit+0x0a`, `body = unit+0x02`, `pos = body+6`, `status = unit+0x06`,
`anim = unit+0x0e`; `now` = game time (DS:eca6, int32), `dt` = ticks since the
previous frame (DS:ecaa). `move_polar(dist, pitch8, head8, pos)` is
`4e87:0006` (moves only when dist > 0); headings with suffix 8 are in 1/8°
(0..0xb3f) and `norm8` (`2255:6205`) wraps into that range. Sound effect ids
are the NN of `sfxNN.voc`, played via `4592:0135(id, priority, pos, flags,
owner)`. Status messages use `1000:7d4b(msg, ticks)`.

Note: several string comments in the Ghidra listing for this segment are
false matches on immediate values (0x168, 0x1e0, 0x200, 0x2d0, 0x300, 0x600,
0x4b0 are plain numbers).

## Rate helpers

* **`evt_approach_value(pair, rate)` 2503** moves `pair[0]` toward `pair[1]`:
  `step = max(1, (rate·dt) >> 8)` (1 when the product is below 0x100); snaps
  when the step reaches the difference.
* **`evt_approach_angle(pair, rate)` 2580** does the same for degrees 0..359
  along the shorter arc (temporarily shifting the target by ±360 when the
  difference is 180 or more, then re-wrapping the value).
* **`evt_approach_zero(pair)` 2603** sets `pair[1] = 0` and approaches with
  the rate stored at `pair[5]` — for the mover this decelerates the speed at
  `mover+0x0a`, for `&mover+0x18` it drops the height at `mover+0x22`.
* **`evt_is_positive_turn(a8, b8)` 2933** compares in whole degrees with
  wrap-around; 1 when the shorter turn from b to a is non-negative. Used for
  craft banking (297d).

## Unit setup

**`evt_init_mover(unit, kind)` 261e** (from unit creation, 365e:0459).
Soldiers (kind ≤ 4 or ≥ 8): `+06=-12, +08=0x20, +0a=0x40, +10=0x168, +12=0,
+14=0x70, +18=+1a=+1c=15 (DS:0880), +1e=3 (DS:0884), +04=0x18, +20=0x18,
+22=0x48`; speed and heading untouched.
Vehicles:

| Field | 5 (boat) | 6 | 7 |
|------|---------|---|---|
| +00 / +02 speed | 0 | 0 | 0x2d0 |
| +04 base speed | 0x40 | 0x80 | 0x2d0 (store skipped) |
| +06 | -0x20 | -0x20 | 0x18 |
| +08 | 0x20 | 0x1e | 0x20 |
| +0a | 0x20 | 0x80 | 0x80 |
| +10 | 0x168 | 0x168 | 0x168 |
| +14 | 0x30 | 0x30 | 0x20 |
| +18 | 3 | 0xf0 | 0x1c2 |
| +1a | 0 | 0x10e | 0x1e0 |
| +1c | 3 | 0x10e | 0x1e0 |
| +1e | 0 | 9 | 0x78 |
| +20 | 4 | 0x18 | 0x40 |
| +22 | 8 | 0x80 | 0x80 |

`+0c, +0e, +12` are zeroed for vehicles and the destination is set to the
current position. For kinds 5–7 the body z becomes `mover+0x18 << 8`. Finally
`+0x38..+0x47` are zeroed. (Original bug: with a NULL unit/mover it still
writes the tail; the port returns early.)

**`evt_set_altitude_level(unit, level)` 2841** stores the level in
`mover+0x25` and sets the target height: level 0 → `+1c`, level 1 →
`(+1c >> 1) + +1e`, otherwise `+1e`.

**`evt_set_dest_to_terrain_obj(unit, target)` 287f**: with no target, picks
the nearest terrain object of type 7 (or 9) within 24000 (`1000:3677`);
copies the target's position to the destination and returns it. Boats are
placed on the nearest water this way.

## Diving

**`evt_unit_dive(unit, heading)` 16e6**: not when `status+0x1a ≥ 5`. Sets Run,
gait 2, heading (mover and body), goes prone (animation 2), current speed =
2 × base speed, height = high height, mode byte 0, then jumps 0x2d00 forward.

**`evt_team_dive_quickly(team)` 17c8** ("Dive Quickly!" key, rate limited by
DS:d80e + 0x400): the living leader dives along its heading and "Dive
Quickly!" is shown (0x300); every other living member dives along the bearing
from the leader's new position to itself, so the team scatters outward.

## Fatigue

**`evt_update_unit_fatigue(unit)` 18b2** every 0x500 ticks. F = fatigue
(`mover+0x38`), W = winded (`+0x3a`), both clamped to 0..8.
* Run: F += 1, +2 more with light wounds, +2 more with serious wounds (which
  also force the posture one step lower); W += 2.
* Walk/back: F += 1 with light or serious wounds (serious also force the
  posture down); SEALs recover F by 1; W −1.
* Stopped: F −1, W −2.
* Player only, when W changed or is 0: W < 2 → sfx 32 (priority 0x400) or
  sfx 30 (0x300) at random; W < 4 → sfx 31 (0x600).

## Obstacles

**`evt_unit_bounce_off_obstacle(unit)` 1b05** restores x/y from the last
collision contact (DS:ecfe/ed06), pushes the unit back 0x400 away from the
obstacle bearing (`mover+0x34`), and if facing within 100° of the obstacle,
turns it 100° to one side (the other side if still within 100°). For the
player the team camera orbit angle (`team+0x32`) is moved behind the new
heading.

## Per-frame movement `evt_update_unit_movement()` 1c04

A 0x500-tick timer (DS:0894) drives fatigue and mode refreshes.

**Pass 1 — every unit except the player** (teams of type 0 or ≥ 4):
shadow/wake sprite on/off (`1000:5d1d`; needs detail level ≥ 1, DS:02be);
speed approach (to zero at `+0a`, else at `+08`); height approach (`+20`);
turning at `+14 >> posture` for living or carried units; body heading =
mover heading × 8; advance by `speed·dt` (backwards when negative). For living
units: fatigue on the tick, then the terrain object under the unit
(`1000:3e85`, solid when height ≥ 0x7f or a special type):
* water (7, 8, 9) sets in-water (and deep for 9) and the wading height from
  DS:0878 (−9 / −3 / −15); an enemy group leader in group-boat mode spawns
  the group boat;
* NPCs ignore types 5, 0xe, 0xf, 0x11 and never trigger traps — anything
  else blocks and bounces them (1b05).
Leaving all terrain clears the flags. On the tick (or a change) the move mode
is re-applied and the height reset when out of water.

**Pass 2 — the player**: speed as above; height approach (drop to zero at
`+22`, and when zero reset the target to 3); camera eye height (`1000:2462`);
movement; fatigue on the tick; then terrain:
* water: "On Water" (0x200) on entering, flags and wading height as above;
* pit (0xb): "Boobytrap pit!" (0x300), random pit wound (`19ac:7f58`),
  player moved into the pit at height −3, pit spent, target speed 0;
* trip-wire (0xa) unless prone: "Boobytrap grenade tripped!" (0x100), trap
  grenade fired at the player (`1000:5114`), wire disarmed, player stopped;
  a prone player treats it as an obstacle;
* brush (0x11) while moving: rustle sfx 26 at most every 0x200 ticks;
* anything else blocks: at most every 0x300 ticks shows "Watch out for that
  tree." (type 6), "You can't go in there." (type 0) or "Collision" and plays
  sfx 11; always bounces.
Afterwards the move mode and posture/animation are refreshed on the tick.
The player's heading only turns here on camp maps (mission index > 27) or
when blocked; otherwise the control code turns the player.

## Formation watch heading

**`evt_member_watch_heading(hdg, slot, team)` 5a25** — only for fire orders
0, 2 and 3. In-Line: slots 1–2 keep the heading, slot 3 turns 180°. Other
formations: slot 1 −90°, slot 2 +90°, slot 3 +180°. Uses C `%` 360 (can be
negative).

## Globals

| Offset | Name | Type | Meaning |
|-------:|------|------|---------|
| 02be | g_detail_level | u8 | Detail 0..5 (default 5); shadows need ≥ 1 |
| 0878 | g_terrain_height_tbl | s16[] | Wading height by terrain type: [7] −9, [8] −3, [9] −15 |
| 0880 | g_posture_eye_height | s16[3] | {15, 9, 3} |
| 0888 | — | s16 | −3, height after falling into a pit |
| 0894 | g_fatigue_tick_time | s32 | Last 0x500 tick |
| 26f4 | g_terrain_objs | far ptr[] | Terrain features (same list as the structures) |
| d8b0 | g_player_terrain_fx_time | s32 | Throttle for collision messages and brush sound |
| ecaa | g_frame_ticks | s16 | Ticks since the previous frame |
| ecfe | g_coll_contact_pos | s32[3] | Contact point of the last collision sweep |

Terrain object: `+2` body, `+6` type, `+0x0d` height (≥ 0x7f solid). Types:
0 no-go, 5/0xe/0xf ignored by NPCs, 6 tree, 7 water, 8 shallow, 9 deep,
0xa trip-wire, 0xb pit, 0x11 brush.

## Open questions

* Formation codes: this analysis reads `team+0x20` as 0 Column, 1 In-Line,
  2 Vee, 3 Diamond, while `seg_2dbd_teams_ai.md` has 2 Diamond, 3 Vee Wedge.
  Resolve from the order-key handler (19ac) and the formation table values.
* sfx 30/31/32 (short breath sounds?) play while winded is *low*; confirm.
* Whether the collision contact point is the touch point or the last free position.
* Meaning of mover `+06`, `+10`, `+12`.
