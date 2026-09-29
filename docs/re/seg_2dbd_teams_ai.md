# Segment 2dbd (part 2) — team orders, search, prisoners, formations, AI commands

Companion to `seg_2dbd.md`; covers the functions that document marks as
provisional. Distances use `1000:321f` (world units, 1 unit = 0x100 in the
int32 positions). Body headings (`body+0x12`) are in 1/8°, mover headings
(`mover+0x0e`, `+0x36`) in whole degrees. Every status message goes through
`1000:7d4b(msg, ticks)`, which also turns time compression off. Message texts
are referenced by DGROUP offset; the port reads them from `st.exe`.

## Enumerations

* **Team type** (`team+0x26`, names at DS:1c7c): 0 SEAL Team, 1 Boat Support
  Unit, 2 Seawolf helicopter, 3 Bronco (Black Pony), 4 Viet Cong, 5 NV Army,
  6 Civilian, 7 Friendly.
* **Team formation** (`team+0x20`): 0 Column, 1 In-Line, 2 Diamond, 3 Vee Wedge.
* **Fire / special order** (`team+0x22`): 0 Field of Fire, 1 At Target,
  2 At Will, 3 Cease Fire, 4 Cover Fire, 5 Demolish, 6 Snipe.
* **Movement order** (`team+0x24`), SEAL teams: -1 none, 0 Halt, 1 ASAP
  (rally point), 2 Stealth, 4 Search. Support craft: 2/3 extraction under way,
  4 Loiter, 5 Attack, 6 Cease attack / out of ammo.
* **Move mode** (`mover+0x24`): 0 Stop, 1 Slow, 2 Run, 3 Back (names DS:1d9c).
* **Objective kind** (DS:1d72): 0 none, 1 Patrol, 2 Ambush, 3 Demolition,
  4 Observe, 5 Rescue, 6 Snatch, 7 Recover.
* **Item types** (table DS:48fe, 0x22-byte records; `type*0x22+0x4902` gives
  +0 weapon class, +0xa magazine size, +0xc flags (0x10 thrown), +0x18 jam
  value). Classes by type: 0→6, 1,2→3, 3→6, 4 (silenced pistol)→0x12, 5→7,
  6→8, 7 (shotgun)→9, 8→0x13, 9 (M79)→4, 0xa (M203)→5, 0xb (LAAW)→0xf,
  0xc (satchel)→0xa, 0xd/0xe frag→0xa, 0xf WP→0xd, 0x10 smoke→0xc,
  0x11 tear gas→0xe, 0x12/0x13 (SKS, AK47)→1, 0x14 (sniper rifle)→2, 0x15→7,
  0x16–0x19 enemy frags→0xa, 0x1a smoke→0xc, 0x1b RPG→0xf, 0x1c mortar→0x14,
  0x1d minigun→0x11, 0x1e rocket→0x10, 0x1f stun→0xd, 0x20 mounted grenade
  launcher→4, 0x21 illumination→0xc.

## Structures (new fields)

* **Team**: `+0x28` far pointer to the team AI record (`+0x18` five 0x14-byte
  commands, `+0x7c` ring head, `+0x7d` pending flag).
* **Unit**: `+0x16` loadout (`+0` item list head, `+4` primary, `+8`
  secondary), `+0x30` buddy (linked both ways), `+0x12` brain.
* **Brain**: `+0x00` five 0x14-byte commands, `+0x64` head, `+0x65` pending,
  `+0x66` surrendered / hands-up display, `+0x68` far team pointer, `+0x6c`
  flags (0x08 contact, 0x10 ignore team commands, 0x20 panic, 0x40 excluded),
  `+0x74` three 0x1e-byte contact slots (`+0` active, `+2` position,
  `+0xe` far target, `+0x18` distance, `+0x1a` priority, `+0x1c` aim spread).
* **Command entry**: `+0` argument, `+1` type, `+4` position (12 bytes),
  `+0x12` heading.
* **Item**: `+0` type, `+2` rounds loaded, `+4` reloads, `+8` jam, `+0xc` fire
  mode (1 single, 2 burst of 3, 4 auto of 20, 8 grenade), `+0xe` far next.
* **Mover**: `+0x18` height, `+0x1c` cruise height, `+0x26` flags (0x01 detour
  active, 0x04 in water, 0x08 deep water, 0x10 searched, 0x20 prisoner /
  rescued, 0x40 aboard extraction craft, 0x80 escort), `+0x27` bit 0 "fetch
  buddy", `+0x28` destination, `+0x36` facing, `+0x3c` int32 facing-override
  expiry.
* **Target record** (30 bytes; player reticle at `5149:0000`, search log
  `53ba:0000[9]`): `+2` position, `+0xe` far object, `+0x12` kind (0 unit,
  1 structure, -1 none, 3 aim point 150 ahead), `+0x18` range.
* **Structure**: `+0` near type definition (`+8` int32 radius, 8.8), `+2` body,
  `+0xa` hit points (< 1 destroyed).
* **Projectile** (pool of 32 at DS:2670): `+6` motion record, `+0xa` class,
  `+0xc` item type, `+0xe` item flags, `+0x20` int32 flight/fuse time,
  `+0x32` shooter, `+0x36` item.

## Search and prisoners

**`evt_search_begin(team)` 4e56.** Fails (returns 0 → "Another search in
progress.") if a search team is set (`g_search_team` DS:0ae6) or the team's
order is already 4. Otherwise clears the per-search counters (ec5e, ec66,
ec68, ec6a), sets order 4, primes search record 0 with the nearest searchable
body, stores the team and returns 1. Its only caller (the `s` key, 19ac:0cc3)
always passes team 0; before that it refuses when fewer than 2 members are
alive and reports "Nothing to search." when `5327` returns 300.

**`evt_find_nearest_searchable(U, out)` 5327.** Best distance starts at 300.
Scans all teams except U's own, including a team when it is hostile to U's
side (types 4/5 vs 0–3/7 and vice versa) or is Friendly (7); civilians are
never searched. Picks the unsearched member (`flags & 0x10` clear) with the
strictly smallest distance. When something was found and `out` is set, fills
search record 0 (position, unit, kind 0, range) and `*out`. Returns the
distance (300 = nothing).

**`evt_search_member_step(T, M, L, retarget, idx)` 58c1.** On a retarget tick
it re-runs 5327 from the leader; at 300 it reports 'Security: "Search
complete."' (0x200 ticks), gives hand signal 7, clears the order and calls
the end report. Otherwise member M heads to the target with offset
`s = idx odd ? idx-1 : -idx` (0, 0, -2, +2 for idx 0–3):
`x = tx + 3s·256 + (random(2)-1)·3·256`, `y = ty + 0x300 + (random(2)-1)·3·256`.
On other ticks, a member within 0x0f of the target crouches (if standing) and
calls `evt_search_body`.

**`evt_search_body()` 4fcc.** Does nothing when 8 bodies were already logged,
the record is not a unit, or the unit was searched. Marks it searched. A live
unit is also marked captured/rescued and gets an escort picked by
`evt_assign_helper` (slots 2, 1, 3, 0); enemy (4/5) captives count as
prisoners only when an escort exists and show the hands-up pose, friendlies
clear it. A seriously wounded or bleeding live unit with an escort receives
medical aid (`19ac:86b3`), announcing "Applying Medical aid." (0x300). Any
enemy body adds 2 weapons and 1 document. The record is appended to the log.

**`evt_search_end_report()` 516b.** Adds the search's weapons, documents and
prisoners to the mission totals (ec60, ec62, ec64). If the searching team is
the player's, shows the three quoted totals ("Prisoners Captured", "Weapons
Confiscated", "Documents Confiscated", 0x200 ticks each, unsigned decimal).
Clears the search team.

**Medical aid `19ac:86b3(medic, patient)`.** Needs a med kit (item type 1).
Bleeding is cleared; serious wounds ≥ 2 drop by 2, a single serious wound is
cleared and one light wound removed; if nothing else was treated, light wounds
drop by 2 (or clear the last one). Any treatment uses one kit.

**`evt_take_prisoner_phk()` 4eca.** Prisoner Handling Kit used on the reticle
target: only for a unit target of a VC/NVA team at range < 0x78 while the
player has no buddy. Then the target becomes a prisoner (flags |= 0x30,
hands-up, Slow), player and prisoner are linked, the player gets the escort
flag, the mission prisoner count increases and "Taking a prisoner." is shown
(0x400); otherwise "Prisoner not allowed." (0x400). Life, prior capture and
kit count are not checked.

**`evt_follow_buddy(A, B)` 54f8.** Keeps a linked unit B beside its escort or
carrier A; returns non-zero when B was handled. A dead prisoner/friendly
breaks the link with "The prisoner is dead." / "The rescued friendly is
dead." (0x400). Link kinds and offsets (table DS:0868, ×3, rotated by A's
heading): 0 enemy prisoner (2, 3), 1 friendly (-2, 0), 2 seriously wounded
(-2, 0), 3 dead (-1, 0), 4 fetch-buddy (2, 0). The slot becomes B's
destination; B normally takes A's heading, mode, speed and posture and is
placed exactly on the slot. Exceptions: a prisoner more than 12 units away
walks (when A is the player) and turns toward the slot beyond 6 units;
wounded or dead buddies further than 0x18 away (or while searching) are left
where they are, and a dead buddy sets A's fetch bit so the carrier walks back;
carrying a dead buddy makes the carrier crouch.

## Formations and movement

**`evt_team_formation_update(i)` 5a91** runs per team per frame.
Retarget tick A fires every 0x400 ticks for a searching SEAL team, otherwise
every 0x200 ticks for the player's team; idle tick B fires every 0x900 ticks
for the player's team. For each member after the leader (the leader only when
captured, or a searching split team): buddies are handled by 54f8, dead units
are skipped (except helicopter crews), and the destination is the leader
during extraction, the search target when searching (first three members or
when at most two remain), the buddy position when fetching, else the
formation slot. Slots come from the table below, scaled by `3 × scale[type]`
(scale bytes DS:0ade: SEAL 1, boat 8, Seawolf 12, Bronco 16, others 1),
rotated by the leader's heading; members 1 (and 3 while searching) are
positioned from the leader, the others from the previous member.

| Slot | Column | In-Line | Diamond | Vee Wedge |
|------|--------|---------|---------|-----------|
| 1 | (0,-8) | (16,0) | (8,-8) | (8,-8) |
| 2 | (0,-16) | (-8,-2) | (-8,-8) | (-8,-16) |
| 3 | (-1,-24) | (8,-2) | (0,-16) | (16,-24) |
| 4 | (-1,-32) | (-16,-5) | (0,-24) | (-16,-32) |

Movement mode for SEAL members: stop when the leader is stopped and the
member is within 0x54; run when 0x24 or more away (slow instead when the
leader is not standing or the member carries a dead buddy); match the
leader's mode within 0x0c; between 0x0c and 0x24 slow, or run on tick A when
the leader runs. On tick B a non-running member within 0x24 stops. Enemies in
water are teleported to their slots; support craft align with the leader when
nearly facing it. On tick A (player team, mode not Run) members copy the
leader's posture and face outward per formation: In-Line members 1–2 look
ahead and 3 behind; other formations 1 left (-90°), 2 right (+90°), 3 behind.

**`evt_team_snap_formation(i)` 6371** places members instantly at their slots
(offsets always from the leader) at mission start and when teams spawn.

**`evt_player_speed_step(dir)` 651d.** Forward steps Back→Stop, Stop→Slow,
Slow→Run; zero stops; backward steps Run→Slow, Slow→Stop, Stop→Back. A change
shows the mode name (0x80 ticks). Stopping while holding plays the stop
animation for the current posture (0x0b, 0x0e, 0x12); moving cancels any
order, aborting a search with 'Security: "Search aborted."' (0x100) and its
report.

## Split teams and AI commands

**`evt_split_teams_update()` 3684** (every 0x400 ticks, and right after
"Team split.") drives the leaders of split SEAL teams toward their waypoint
(DS:0eb8 for team A, DS:0ec4 for team B). A stopped, unalerted leader runs
(ASAP) or crawls in stealth (Slow, crouched); an alerted one stays put and
goes prone. Within 0xb4 of the waypoint it halts and crouches; while moving
slowly it alternates crouch/prone (prone with 1-in-3 chance). Special orders
within 0x4b0 of the waypoint:
* **Demolish** (mission has a demolition objective): with a satchel charge,
  approaches the target structure; within its radius + 0x5a it places the
  charge ("Performing Demolition.", 0x300) and reloads; with no charge left it
  runs away from the structure.
* **Snipe** (mission has an ambush objective): with the silenced pistol,
  faces and goes prone within 0x4b0 of the target team's leader and fires
  inside 0x168 ("Performing Snipe.", 0x300).

**`evt_announce_medic_assignments()` 3d52** prints, for squad slots 1–3 with
a pending medic assignment (`525b:0000[4]`, set by the AI), the message
`<role> <name>: "Medical aid to <name>."` (0x300) and clears the assignment.

**`evt_projectile_init_motion(p, range)` 41a4** initialises a projectile's
motion by weapon class (the M203 in rifle mode counts as a rifle): speed,
bounce and gravity flags, launch height and a class-specific constant. Bullets
0x168, M79 0x168, M203 grenade 0x1e0, thrown grenades 0x5a (bouncing, launch
height 3), smoke 0x60, tear gas 0x48, LAAW/RPG 0x258, rocket 0x21c (height 6),
mortar 0x3c; a satchel charge is simply dropped. The vertical launch speed
comes from `4088`, which shortens the range twice by a quarter, derives the
flight time `t = (range << 8) / speed` and the fuse time (2t, or 4t above speed
0x12c).

**`evt_team_is_engaged(i)` 6487** — a team is engaged if it is not ceasing
its attack (order 6), not loitering (order 4, except the boat), and any member
has contact. Used by the support-craft Attack orders ("Hang on, Section hot!").

**`evt_execute_ai_commands()` 6653** runs each frame: every AI unit (not the
player; master switch DS:02ba) executes its newest pending command, taken from
its own queue or, unless it ignores team commands, from the team queue; then
the pending flags are cleared. Command types: 2 heading (enemies/civilians);
1 move mode for group leaders (stopping enemies in contact shout, sfx 0x28,
30% chance; panicking civilians running shout, sfx 0x2b); 4 fire at the
highest-priority contact after the hold-fire time (DS:d7f2) with rules per
side (craft only when in range/airborne, the player's squad not after
extraction, not under Cease Fire, not with satchels, not beyond 0x5a° off
facing); 3 posture; 5 reload; 9 surrender (hands up, enemy shout sfx 0x2a).

## Globals

| Offset | Name | Type | Meaning |
|-------:|------|------|---------|
| 023a | g_extraction_done | u8 | End-of-mission sequence running; squad stops auto-firing |
| 02ba | g_ai_enabled | u8 | AI command execution switch (always 1) |
| 07dc | g_formation_slot_tbl | s16[4][5][2] | Formation offsets (indexed from 0x7c8 / 0x7b4) |
| 0868 | g_follow_offset_tbl | s16[6][2] | Buddy follow offsets |
| 088c | g_team_move_tick_t | s32 | Last 0x200 tick |
| 0890 | g_team_idle_tick_t | s32 | Last 0x900 tick |
| 0ade | g_team_formation_scale | u8[8] | Formation scale per team type |
| 0ae6 | g_search_team | far ptr | Team currently searching |
| 0aea | g_search_tick_t | s32 | Last search retarget tick |
| 0eb8 | g_split_wp_a | s32[3] | Split team A waypoint |
| 0ec4 | g_split_wp_b | s32[3] | Split team B waypoint |
| 26f4 | g_structures | far ptr[] | Structures |
| cd6e | g_seg_medic_target | u16 | Segment of `g_medic_target[4]` (525b) |
| cd70 | g_seg_player_target | u16 | Segment of the reticle target (5149) |
| cd72/cd74 | g_seg_search_recs | u16 | Segment of the search log (53ba) |
| d7f2 | g_hold_fire_until | s32 | Insertion time + 0xa00 |
| ec5e | g_search_rec_count | s16 | Bodies logged this search (≤ 8) |
| ec60/ec62/ec64 | g_mis_weapons / documents / prisoners | s16 | Mission totals |
| ec66/ec68/ec6a | g_search_weapons / documents / prisoners | s16 | Current search totals |
| ec87 | g_team_count | u8 | Entries in g_teams |
| ec88 | g_player_team_idx | u8 | Player's team index |
| ec9c | g_split_team_count | u8 | Split SEAL teams (0–2) |
| ecfb | g_msn_has_demo_obj | u8 | Mission has a demolition objective |
| ecfc | g_msn_has_ambush_obj | u8 | Mission has an ambush objective |

## Open questions / probable original bugs

* The search order is always issued for team 0, even when radioing a split team.
* With more than 8 unsearched bodies in range the search never completes.
* PHK capture checks neither the kit count nor whether the target is alive or
  already captured.
* With unlimited reloads (DS:0148 ≠ 1) a demolition team keeps re-placing charges.
* The snipe only targets the target team's first member.
* `1000:671c` returns "mission number > 0x1b"; what differs from mission 28 on is unknown.
