# Canonical data structures

This is the single reference for the record layouts of `st.exe`. It merges the
per-module notes (`seg_1000.md`, `seg_19ac.md`, `seg_2255.md`, `seg_2dbd*.md`,
`seg_365e_a.md`, `seg_365e_b.md`, `seg_libs.md`), which each saw the same
records from a different angle and sometimes disagreed. Every disagreement was
settled from the disassembly (`re/export/by_seg/*.asm`), the data tables in
`st.exe` or the shipped data files; the decisions are listed in
[Resolved disagreements](#resolved-disagreements). The C++ definitions are in
`src/game/types.h` (namespace `st::game`), one struct per record below, with
the same field names.

Conventions: offsets are hexadecimal byte offsets inside the record; `far*` is a
4-byte far pointer (typed pointer in the port), `near*` a 2-byte DGROUP offset.
Time is in ticks of 1/256 s (`g_time`, DS:ECA6). `Angle8` = 1/8 degree
(0..2879); "deg" = whole degrees. `Vec3` = three `s32` 24.8 fixed-point
coordinates `x, y, z`, **y = altitude** (map units = value >> 8). Function names
are the canonical names of `tools/re/symbols/seg_*.tsv`, globals those of
`merged_globals.tsv`. "Team" and "group" are the same record (UI and the
2dbd/1000 code say team, 365e/19ac/4a37 say group; the table is `g_groups`).

## Index

| Record | Size | Instances | `types.h` |
|---|---|---|---|
| 3D object ("obj3d", body, instance) | 0x18 (0x12 static) | object arena `g_obj_seg` (DS:F450) | `Obj3D` |
| Model table entry | 6 | far 514B:0000, 97 + terminator | `ModelTableEntry` |
| Model descriptor | 0x4A | DGROUP (0x5D1C..0xCAE4) | `ModelDesc` |
| World / structure / terrain object | 0x0E | `g_world_objects` (DS:26F4) | `WorldObject` |
| Unit | 0x34 | members of teams | `Unit` |
| Status (personnel/body block) | 0x1C | unit+0x06 | `Status` |
| Mover (movement block) | 0x48 | unit+0x0A | `Mover` |
| Anim | 0x26 | unit+0x0E | `Anim` |
| Brain (AI mind) | 0xD0 | unit+0x12 | `Brain` |
| Command | 0x14 | brain, team AI, script steps | `Command` |
| Target record / contact slot / search record | 0x1E | 5149:0000, 53BA:0000[9], brain+0x74[3] | `TargetRec` |
| Team (group) | 0x34 | `g_groups` (DS:12E2) | `Team` |
| Team AI record | 0x8E | team+0x28 | `TeamAi` |
| Waypoint node | 0x14 | TeamAi lists | `WaypointNode` |
| Script step | 0x1A | far 53BA:268C (3 heads) | `ScriptStep` |
| Weapon table entry | 0x22 | DS:48FE, 34 entries | `WeaponDef` |
| Item (tool) table entry | 6 | far 52E3:0000, 12 entries | `ItemDef` |
| Weapon slot node | 0x12 | loadout lists | `WeaponNode` |
| Item node | 8 | unit+0x1A lists | `ItemNode` |
| Weapon list header | 0x0C | unit+0x16 | `Loadout` |
| Projectile / ordnance | 0x50 | `g_fx_pool` (DS:2670), 32 | `Projectile` |
| Flight record | 0x48 | projectile+0x06 | `FlightRec` |
| Shot (combat) record | 0x3C | far 53BA:28BA, 32 | `ShotRec` |
| Victim entry | 8 | stack array of 16 | `ShotVictim` |
| Wound table entry | 4 | far 525F, 5 x 17 | `WoundTableEntry` |
| Noise event | 0x18 | far 53BA:26DA, 20 | `NoiseEvent` |
| Camera | 0x1C | DS:D84E, D86E, D88A | `Camera` |
| Message entry | 0x52 | `g_msg_queue` (DS:3716), 8 | `MsgEntry` |
| Briefing camera waypoint | 0x10 | far 5170:0000, 8 | `BrfCamWaypoint` |
| Button | 0x10 | DS:0BF4 (41), far 365e lists | `Button` |
| Sound channel | 0x2A | far 53BA:258C, 6 | `SoundChannel` |
| Sound-effect descriptor | 0x16 | far 520D:000C + id*0x16 | `SfxDesc` |
| Sprite frame | 0x50 | far 53BA sprite sets | `SpriteFrame` |
| RLX record | 12 (file 18) | `.rlx` files | `RlxRecord` |
| Medal of Honor record | 6 | far 52BB:001A, 9 | `MohRecord` |
| MCI mission header / objective | 0x176 / 0x62 | `g_mci` (DS:ED32) | `MciHeader`, `MciObjective` |
| MTM team record | 0x66 | `g_mission_groups` (DS:39CE) | `MtmTeam` |
| SE personnel record | 0x90 | `.se` files, roster, NPC cache DS:3C6A | `SeRecord` |
| Campaign header / history | 0x14C / 0x0C | `g_campaign_state` (DS:3B0E) | `CampaignHeader`, `CampaignHistory` |
| Roster entry | 0x18 | `g_roster` (DS:3A6A), max 40 | `RosterEntry` |
| Campaign flow entry | 0x0E | far 5275, 4 x 20 | `FlowEntry` |
| Team loadout record | 0x0C | far 5178:0000, 4 + 0xFF | `LoadoutRecord` |
| Difficulty options (st1.dfr) | 0x10 | far 5178:0148 | `DifficultyOptions` |
| s.cnf | 0xE8 | far 5178:0060 | `ConfigFile` |
| .W world record / .WD | 0x12 / 0x40 | worlds.lib | `WorldFileRecord`, `WorldDesc` |
| .S text | 80-byte lines; sealNN.s 84 | str.lib, main4.lib | `parseTextLines`, `SealChatterLine` |

### Far data blocks

The decompiler hides `ES` loads from the segment slots DS:CD5C..CDF8; each slot
holds one of these far segments (checked in the DGROUP image).

| Segment | Content |
|---|---|
| 5149 | +0000 player target record (`TargetRec`, also "current contact" for "Enemy sighted."); +1E6E count of the group segment list |
| 514B | model table |
| 5170 | briefing camera waypoint ring (8 x 16) |
| 5178 | +0000 team loadout (4 x 12, 0xFF at +0030), +0060 s.cnf image (0xE8), +0148 difficulty (16); `cfg_save_s_rst` writes +0000..0054 as s.rst |
| 520D | sound-effect table: +0000 listener position, +000C descriptors |
| 525B | +0000 s16[4] automatic first-aid patient per SEAL slot |
| 525C | s16 noise level by `NoiseType` (walk 120, run 285, wade 240, boat 900, aircraft 1350, helicopter 900, cough/sneeze 300, explosion 2700) |
| 525F | wound tables: bullet bands 0/1/2 at +0000/+0044/+0088, blast bands 0/1 at +00CC/+0110 |
| 5275 | campaign flow, years at +0000/+0118/+0230/+0348 |
| 52BB | +0002 promotion thresholds u16[10], +001A Medal of Honor records |
| 52E3 | item/tool table |
| 53BA | +0000 search log (9 x 0x1E); +010E.. sprite sets (fxmu, soldiers +015E, headgear +137E, hnds +1E70, mdl +2140, exsm/impc/exlg/fxsm ...); +2230..226B briefing camera targets (5 x Vec3); +258C sound channels (6); +2688 EMS byte count; +268C script heads (3 far); +26DA noise events (20); +28BA shot records (32); +303A rounds fired, +303C rounds hit, +303E grenades thrown, +3040 grenade hits, +3042 bonus counter, +3044 mission score, +3046 team size, +3048/+304A difficulty-screen flags |

## World objects and models

### Obj3D — 3D object (0x18 bytes; 0x12 when `flags & 2`)

Built by `world_add_object` (2255:0234) in the object arena. A unit's `body`,
a world object's instance, projectiles and effects are all Obj3D.

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 2 | near* | model | model descriptor; swapped for effects / destroyed shapes | world_add_object; prj_start_burst, dmg_apply, veg_* (w); r3d_* (r) |
| 02 | 2 | u16 | flags | `obj3d_flag`: 0x0001 enabled (drawn), 0x0002 static, 0x0004 sky-hide, 0x0008 in view list, 0x0010 projection cache, 0x0020 group, 0x0040/0x0080 cull/clip skip, 0x0100 LOS tree B, 0x0200 never precise, 0x0400 current auto-target, 0x0800 linked record follows, 0x1000 LOS tree A, 0x2000 free heading (LOS) / vegetation "kept" marker, 0x4000 static grid (creation argument) | fx_* and veg_* set/clear 0x0001/0x2000; tgt_acquire 0x0400; renderer 0x08..0x80; los_* 0x0100/0x1000/0x2000/0x0800 |
| 04 | 2 | near* | next | arena offset of the next object in its list | world_add_object, obj_arena_* |
| 06 | 12 | Vec3 | pos | x, altitude, z | geo_distance/geo_bearing (r); pos_move_polar, evt_update_unit_movement, evt_update_support_craft, prj/fx code (w) |
| 12 | 2 | Angle8 | heading | a0 | unit_turn, view_update_camera, spr_draw_billboard_cb, craft update |
| 14 | 2 | Angle8 | pitch | a1; the ripple, rippleb, tunnel, lssc, mike2 and pit model hooks read it as an animation frame (dmg_apply and wld_hide_highlight set it to 1) | evt_update_support_craft (boat/helo pitch), model hooks |
| 16 | 2 | Angle8 | roll | a2 (bank) | evt_update_support_craft (helo/aircraft bank), ent_spawn_support_craft (OV-10 0xAF0) |

### ModelTableEntry — far 514B:0000 (6 bytes, 97 entries + zero descriptor)

| Off | Size | Type | Name | Meaning | Read by |
|---|---|---|---|---|---|
| 00 | 2 | near* | desc | model descriptor | mdl_load_all, wld_load, veg_* |
| 02 | 2 | u16 | kind | `TerrainKind` given to world objects of this type | wld_load |
| 04 | 2 | u16 | object_flags | flags for `world_add_object` (0x7101 structures, 0x6101 flat terrain/water, 0x3101 ground cover, 0x0101 dynamic, 0x2001 effects) | wld_load |

Kinds by entry (index: name): 0 bunker, 1 hooch, 2 bldgston, 16 well, 28 tower,
32 cache, 33 church, 34 pagoda, 37 shelter = 0 Structure; 7 reeds, 9 tree4,
17 bush, 39 grasses, 40 log, 42 pineaple, 44 bamboo2, 49 flag = 3 Vegetation;
3 paddy, 5 mountain, 11 beach, 12 canal = 4 Terrain; 6 flat, 15 path, 29 bridgef,
31 cemetery = 5 Clearing; 8 palm, 10 jungle, 45 banana2, 46 tree2 = 6 Tree;
4 river, 19 rivers = 7 Water; 21 stream, 24 streams, 25 brook, 26 brooks =
8 Shallow; 18 river2, 20 riverl, 22 sea, 23 bay, 27 riverh = 9 DeepWater;
14 trip = 10 TripWire; 13 pit, 30 stakes = 11 PitTrap; 35 dock = 14 BoatPad;
36 pad = 15 HeloPad; 38 oxcart, 43 rock, 48 boxes = 16 Prop; 41 pima, 47 nipa =
17 Brush; 50 tunnel = 18 Tunnel; 51..96 (human, craft, bullets, grenades,
shadows, ripples, birds, sampans, muzzle, explosions, damage shapes, arc,
crosshc) = 19 Dynamic.

### ModelDesc — model descriptor (DGROUP, 0x4A bytes used)

| Off | Size | Type | Name | Meaning | Read by |
|---|---|---|---|---|---|
| 00 | 1 | u8 | layer | < 0x80 flat layer (2 ground, 3 pad, 4 shadow, 9 sea/bay, 0x0A river, 0x0C dock, 0x10 ripples/arc), >= 0x80 depth sorted | r3d_draw_order_cmp |
| 01 | 1 | u8 | unk_01 | 0 | — |
| 02 | 2 | s16 | radius | model units | mdl_aspect_radii, r3d_obj_visible, shot_blast_victims (via obj_height_or_default) |
| 04 | 2 | s16 | radius_x | aspect-corrected (computed) | r3d |
| 06 | 2 | s16 | radius_y | aspect-corrected (computed) | r3d |
| 08 | 4 | s32 | radius_world | radius << (8 + shift) | r3d, los_* (quadtree margin), teams_ai "structure radius" |
| 0C | 1 | s8 | scale_shift | 1 model unit = 2^shift game units | r3d |
| 0D | 1 | u8 | unk_0d | 0x1F on houses/towers; never read | — |
| 0E | 6 | u16[3] | lod_distance | LOD thresholds | r3d |
| 14 | 6 | near*[3] | lod_model | LOD models (only [0] used) | r3d, mdl_* |
| 1A | 2 | s16 | size_class | world-box cull class | r3d |
| 1C | 4 | far* | lod_callback | segment 0 = none | r3d |
| 20 | 2 | u16 | unk_20 | | — |
| 22 | 2 | near* | pnt_name | `.pnt` base name; also the HUD target name | mdl_load_pnt, hud_draw_target_info |
| 24 | 4 | far* | swap_copy | always 0 | mdl_swap_* |
| 28 | 2 | u16 | swap_reloc | | mdl_swap_* |
| 2A | 2 | u16 | swap_size | | mdl_swap_* |
| 2C | 2 | u16 | default_height | initial altitude of new objects | world_add_object |
| 2E | 2 | u16 | game_flags | 0x200, 0x204, 0x210, 0x214, 0x304, 0x1110 ... | not read by the renderer |
| 30 | 24 | s32[6] | box_min_x .. box_max_z | collision box | los_test_object |
| 48 | 2 | near* | heightmap | optional heightmap descriptor (+0 far bytes, +4 width, +6 depth, +8/+0A offsets, +0C cell shift, +0E height shift) | los_test_object |

### WorldObject — world/structure/terrain object (0x0E bytes)

One per `.w` record (max 524) plus 126 spare ground-cover objects
(`g_spare_objects`, DS:31BA); far pointer table `g_world_objects` (DS:26F4,
NULL terminated), count `g_world_object_count` (DS:ECB6). Objective
"structures" (`MciObjective::target_structure`) index this table.

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 2 | near* | model | shape; replaced by `wld_destroyed_shape` / 0x7A40 on destruction | wld_load, dmg_apply, veg_place_ground_cover |
| 02 | 4 | far* | body | Obj3D | all world queries |
| 06 | 2 | u16 | kind | `TerrainKind` | wld_probe_kind, wld_probe_solid, evt_update_unit_movement, tgt_acquire, wld_open_hut_ahead, grp_set_craft_status, dmg_apply |
| 08 | 2 | u16 | flags | from the .w record; bit 0 = objective completed | msn_check_objective (w), wld_object_flag0 (r), score_eval_objective |
| 0A | 2 | s16 | hit_points | < 1 = destroyed (kind 0x10 forced 0x32, kind 3 0x19) | dmg_apply, wld_object_destroyed, wld_count_destroyed, msn_check_objective |
| 0C | 1 | u8 | cover | LOS cover 0..100 (kinds 4,5,7,8,9,0xB,0x12 -> 0; types 0x28/0x2B 99; 0x20 100) | wld_los_cover, tgt_acquire (100 = targetable), wld_open_hut_ahead (sets 100), dmg_apply (0x19) |
| 0D | 1 | u8 | height | >= 0x7F solid (kinds 4,5,7,8,9,0xB,0x12 -> 0x1F; kind 6 -> 0x7F) | wld_probe_solid, dmg_apply (0x1F) |

## Units

### Unit (0x34 bytes)

`ent_alloc(flags)` (4511:019E) allocates the record and the components of the
flag bits; `ent_group_add_unit` (365e:0459) fills it.

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 2 | near* | model | model descriptor: 0x862A human; class 5 boat 0x897E (LSSC, se 0) / 0x8DB0 (Mike); class 6 0x726A (Cobra, se 0) / 0xC5B2 (UH-1); class 7 0x992C (OV-10) | unit_set_class_word (19ac:612A) |
| 02 | 4 | far* | body | Obj3D | everywhere |
| 06 | 4 | far* | status | `Status`, flag bit 0x01 | see Status |
| 0A | 4 | far* | mover | `Mover`, flag 0x02 | see Mover |
| 0E | 4 | far* | anim | `Anim`, flag 0x04 | see Anim |
| 12 | 4 | far* | brain | `Brain`, flag 0x08 (the Point Man has none: flags 0x17) | see Brain |
| 16 | 4 | far* | loadout | weapon list header | ent_unit_add_weapon, wpn_*, shot_fire, evt_execute_ai_commands |
| 1A | 4 | far* | items | item list head | ent_unit_add_item, item_use_tool, item_cycle_tool, inv_count_items, med_treat |
| 1E | 4 | far* | team | owning team | ent_group_add_unit, ent_group_split/merge, most 2dbd code |
| 22 | 2 | u16 | unk_22 | never written | — |
| 24 | 4 | far* | attached_fx | effect object riding with the unit: shadow/wake (`fx_unit_ground_fx`, pool DS:2F2C) or the team's sampan (`fx_team_marker`, pool DS:2F94) | those two functions |
| 28 | 4 | far* | roster | roster entry (SEALs), NULL for NPCs/craft | ent_unit_init_personnel, roster_add_awards, roster_set_rank, roster_record_casualty, score_* |
| 2C | 4 | far* | se | SE record (last name +0x0C, camouflage +0x62) | messages ("<role> <name>"), spr_draw_soldier_frame, map_draw_info_panel |
| 30 | 4 | far* | buddy | linked unit, both ways (casualty/helper, prisoner/escort) | evt_assign_helper, evt_unit_killed/wounded, evt_follow_buddy, evt_take_prisoner_phk, evt_check_prisoner_escape |

### Status — personnel/body block (0x1C bytes)

A copy of SE+0x74..+0x8F made by `ent_unit_init_personnel` (skills replaced by
the roster skills for SEALs); `unit_init_body` (19ac:8617) sets defaults for
craft (size 200, strength 50, ...) and clears the wound fields.

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 8 | u8[8] | skill | Rifle, Pistol, Mortar, Shoulder, Automatic, Throw, Observe, Radio (0..90) | shot_shooter_skill (19ac:7C8F), shot_scatter (+5), shot_throw_bonus, ai_sight_range_leader_mod (+6) |
| 08 | 1 | u8 | size | load capacity, sprite scale `(size/20 - 3)*scale/16`, AI visibility (> 75 +60, > 90 +300), shooter hit bonus (>= 60 +30, >= 30 +10) | unit_load_level, spr_draw_billboard_cb, ai_sight_range_target_mod, shot_aimed_hit_roll, noise_from_team |
| 09 | 1 | u8 | strength | capacity = size/2 + strength | unit_load_level, evt_calc_move_speed |
| 0A | 1 | u8 | agility | noise level −5*(agility/30) | noise_from_team |
| 0B | 1 | u8 | intelligence | | — (AI jam clearing uses +0x0C) |
| 0C | 1 | u8 | experience | Time In Service; +2 per mission flown (cap 90); AI sight range, jam clearing, noise | unit_add_experience (19ac:85FA), ai_group_scan, ai_group_combat, noise_from_team |
| 0D | 1 | u8 | unk_0d | | — |
| 0E | 2 | u16 | hit_mask | wound location bits (below); 0x4000 dead (byte +0F bit 0x40) | dmg_apply (w), med_*, stat_count_dead, everything that tests "alive" |
| 10 | 2 | u16 | unk_10 | cleared at spawn | ent_unit_init_personnel |
| 12 | 1 | u8 | light_wounds | | dmg_apply, med_treat, evt_update_unit_fatigue |
| 13 | 1 | u8 | heavy_wounds | non-zero = seriously wounded | dmg_apply, med_treat, evt_assign_helper |
| 14 | 1 | u8 | bleeding | 1 = bleeding / dying (set when heavy_wounds reaches 2) | dmg_apply, med_treat, med_bleed_tick, med_wound_level |
| 15 | 1 | u8 | unk_15 | | — |
| 16 | 2 | s16 | bleed_time | rand(0x2C00)+0x2C00 ticks | dmg_apply, med_bleed_tick |
| 18 | 2 | u16 | load | carried load, 1/10 lb | loadout_compute_weight (on SE+0x8C), unit_load_level, unit_add_load |
| 1A | 2 | s16 | unit_class | `UnitClass`; >= 5 makes evt_unit_wounded/evt_unit_dive do nothing (craft, but also civilians 8 / friendlies 9) | unit_init_body (w), evt_unit_wounded, evt_unit_dive, map_draw_info_panel |

Hit bits: light 0x0001, 0x0002, 0x0010, 0x0020, 0x0100 (light_wounds += 1);
heavy 0x0004, 0x0008, 0x0040, 0x0080, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000
(heavy_wounds += 1); legs 0x0010/0x0020 light, 0x0040/0x0080 heavy
(`med_leg_level`); arms 0x0800/0x1000 (`med_arm_count`); pairs {0x0001, 0x0004}
and {0x0002, 0x0008} (`med_head_count`); 0x4000 killed; 0xFFFF in the tables =
miss.

### Mover — movement block (0x48 bytes)

Initialised by `evt_init_mover` (2dbd:261E). Speeds in map units per 256 ticks,
headings in whole degrees. Craft re-use +0x25 as altitude mode.

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 2 | s16 | speed | current (negative backwards) | evt_approach_value/zero, evt_update_unit_movement, evt_update_support_craft, map_draw_info_panel |
| 02 | 2 | s16 | target_speed | | evt_set_move_mode, evt_update_support_craft |
| 04 | 2 | s16 | base_speed | soldiers 0x18, boat 0x40, helo 0x80, OV-10 0x2D0 | evt_set_move_mode, evt_calc_move_speed |
| 06 | 2 | s16 | unk_06 | −12 soldiers, −0x20 boat/helo, 0x18 OV-10 | evt_init_mover only |
| 08 | 2 | s16 | accel | | evt_update_* |
| 0A | 2 | s16 | decel | rate when the target is 0 (evt_approach_zero) | evt_update_* |
| 0C | 2 | s16 | heading | deg; body heading = heading*8 | unit_turn, evt_update_*, spr_* |
| 0E | 2 | s16 | desired_heading | deg | evt_execute_ai_commands (Heading), evt_update_* |
| 10 | 2 | s16 | unk_10 | always 0x168 | evt_init_mover |
| 12 | 2 | s16 | unk_12 | always 0 | evt_init_mover |
| 14 | 2 | s16 | turn_rate | soldiers 0x70 >> posture | evt_update_unit_movement |
| 16 | 2 | s16 | unk_16 | | — |
| 18 | 2 | s16 | height | current height; Point Man = eye height (camera); negative = sunk (sprite depth) | view_set_eye_height, spr_get_sink_depth, craft altitude |
| 1A | 2 | s16 | target_height | eye height {15,9,3} (DS:0880) or wading height (DS:0878: 7 −9, 8 −3, 9 −15); craft target altitude | evt_set_posture, evt_update_unit_movement, evt_set_altitude_level |
| 1C | 2 | s16 | height_high | 15 / craft high | evt_set_altitude_level |
| 1E | 2 | s16 | height_low | 3 / craft low | evt_set_altitude_level, grp_set_craft_status |
| 20 | 2 | s16 | climb_rate | | evt_update_* |
| 22 | 2 | s16 | descent_rate | | evt_update_* |
| 24 | 1 | u8 | move_mode | `MoveMode` | evt_set_move_mode, noise_from_team, spr_draw_billboard_cb, shot_fire |
| 25 | 1 | u8 | posture | `Posture` (craft: `AltitudeMode`) | evt_set_posture, evt_set_altitude_level, shot_fire |
| 26 | 1 | u8 | flags | 0x01 blocked, 0x02 slow terrain (never set), 0x04 in water (craft: on water near the destination), 0x08 deep water, 0x10 searched, 0x20 secured (prisoner/rescued), 0x40 aboard the extraction craft, 0x80 escort/helper | see [disagreement 7](#resolved-disagreements) |
| 27 | 1 | u8 | flags2 | 0x01 fetch buddy, 0x02 enemy in a sampan, 0x08 objective done (target team leader) | evt_follow_buddy, ent_mark_enemies_on_structures, evt_update_unit_movement, msn_check_objective |
| 28 | 12 | Vec3 | destination | | 2dbd team/craft code, map_screen_keys, ent_order_extraction |
| 34 | 2 | s16 | impact_bearing | bearing of the last obstacle / incoming round; wounded units turn to it | evt_update_unit_movement, evt_update_ordnance (w), evt_unit_bounce_off_obstacle, evt_unit_wounded (r) |
| 36 | 2 | s16 | aim_heading | facing while firing | prj_fire (w), spr_draw_billboard_cb, spr_draw_soldier_frame |
| 38 | 2 | s16 | fatigue | 0..8 | evt_update_unit_fatigue, evt_calc_move_speed |
| 3A | 2 | s16 | winded | 0..8; 4 on the Point Man at insertion | evt_update_unit_fatigue, evt_calc_move_speed, mis_start_insertion |
| 3C | 4 | s32 | fired_until | g_time + 0x100 on firing | prj_fire, evt_calc_move_speed, spr_draw_billboard_cb |
| 40 | 4 | s32 | throw_until | g_time + 0x200 on throwing | prj_fire, evt_calc_move_speed |
| 44 | 4 | s32 | posture_until | soldiers: g_time + 0x200 on a posture change; craft: radio busy until | evt_set_posture, evt_calc_move_speed; radio_call |

### Anim — animation block (0x26 bytes, segment 348e)

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 1 | u8 | zero_00 | 0 | spr_init_unit_anim |
| 01 | 1 | u8 | kind | colour-remap scheme (>= 3 remapped) | spr_draw_billboard_cb |
| 02 | 4 | u8[4] | unk_02 | | — |
| 06 | 2 | s16 | posture | 0..3 | spr_set_anim |
| 08 | 2 | s16 | posture_copy | written with +06 | spr_set_anim |
| 0A | 2 | s16 | move_mode | mirror of the move mode (0 on stop/casualty) | evt_stop_unit, evt_unit_killed/wounded/suppressed, evt_execute_ai_commands (w); no reader found |
| 0C | 4 | s32 | created | g_time at init | spr_init_unit_anim |
| 10 | 4 | far* | sets | 53BA:015E soldier sprite sets | spr_init_unit_anim |
| 14 | 4 | u8[4] | unk_14 | | — |
| 18 | 2 | s16 | state | `AnimState` | spr_set_anim, spr_update_anim, spr_draw_billboard_cb |
| 1A | 2 | s16 | return_state | state after the timed animation (3 = dead: evt_unit_killed ignores repeats) | same, evt_unit_killed |
| 1C | 4 | s32 | start | clock at the start | spr_set_anim, spr_update_anim |
| 20 | 2 | s16 | duration | | spr_set_anim |
| 22 | 4 | s32 | clock | + g_frame_dt per frame (foot teams); starts random(256) | spr_advance_anim_clocks |

## AI

### Command (0x14 bytes)

| Off | Size | Type | Name | Meaning |
|---|---|---|---|---|
| 00 | 1 | u8 | arg | MoveMode / Posture / fire mask (bit0 primary, bit1 secondary) |
| 01 | 1 | u8 | type | `CommandType`: 1 move mode, 2 heading, 3 posture, 4 fire, 5 reload, 8 look, 9 surrender |
| 02 | 1 | u8 | flag | |
| 03 | 1 | u8 | unk_03 | |
| 04 | 12 | Vec3 | pos | fire/look position |
| 10 | 1 | u8 | ai_issued | 1 for AI turn commands |
| 11 | 1 | u8 | unk_11 | |
| 12 | 2 | s16 | heading | deg (type 2) |

Written by `ai_mind_push_command` (4a37:0D65), `ai_group_broadcast_command`,
`ai_group_run_scripts`; executed by `evt_execute_ai_commands` (2dbd:6653) via
`ai_member_current_command`.

### TargetRec — target record / contact slot / search record (0x1E bytes)

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 1 | u8 | in_use | contact slot used | ai_mind_store_contact, ai_mind_expire_contacts |
| 01 | 1 | u8 | unk_01 | | — |
| 02 | 12 | Vec3 | pos | aim point / last seen position | tgt_acquire, field_view_keys (point 150 ahead), ai_mind_store_contact, evt_find_nearest_searchable |
| 0E | 4 | far* | target | `Unit*` (kind 0) or `WorldObject*` (kind 1) | tgt_acquire, hud_draw_target_info, ent_team_enemy_sighted, evt_execute_ai_commands |
| 12 | 2 | s16 | kind | `TargetKind`: −1 none, 0 unit, 1 structure, 3 aim point | tgt_acquire, field_view_keys, ai_mind_store_contact |
| 14 | 4 | far* | target_pos | live position of the target (contacts) | ai_mind_store_contact, ai_mind_expire_contacts, ai_mind_borrow_contact |
| 18 | 2 | s16 | range | map units | tgt_acquire, hud_draw_target_info, shot_fire argument |
| 1A | 1 | u8 | score | contact priority (`ai_target_priority`; borrowed 25, synthetic 50) | ai_group_scan, ai_mind_best_contact(_score) |
| 1B | 1 | u8 | sighted | real sighting | ai_mind_store_contact |
| 1C | 2 | s16 | cover | LOS cover toward the target | tgt_acquire (wld_los_cover), hud_draw_target_info, evt_execute_ai_commands -> shot_fire |

### Brain — AI mind (0xD0 bytes)

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 0x64 | Command[5] | commands | own ring | ai_mind_push_command, ai_member_current_command |
| 64 | 1 | u8 | cmd_index | next write; newest = (idx+4)%5 | same |
| 65 | 1 | u8 | cmd_pending | | ai_group_commands_consumed |
| 66 | 1 | u8 | surrendered | hands-up pose (1 for friendly NPCs) | evt_execute_ai_commands (9), evt_check_prisoner_escape, ent_spawn_mtm_team, spr_draw_billboard_cb |
| 67 | 1 | u8 | unk_67 | | — |
| 68 | 4 | far* | team | back pointer | ai_mind_attach, ent_group_split |
| 6C | 1 | u8 | flags | 0x01 alerted, 0x04 engaging, 0x08 contact/fired, 0x10 suppressed, 0x20 fleeing, 0x40 script running, 0x80 active | 4a37, dmg_apply, tgt_acquire (0x80), map_draw_info_panel |
| 6D | 1 | u8 | unk_6d | | — |
| 6E | 2 | s16 | script_delay | | ai_member_start_script, ai_group_run_scripts |
| 70 | 4 | far* | script | current `ScriptStep` | same |
| 74 | 0x5A | TargetRec[3] | contacts | | ai_mind_* |
| CE | 2 | s16 | suppress_time | set 0x500..0x1400+rand by dmg_apply | ai_group_tick_suppression |

### TeamAi — group AI record (0x8E bytes, `ent2_alloc` bit 3)

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 12 | Vec3 | destination | | ai_group_set_destination, ai_group_next_destination, ai_group_at_destination |
| 0C | 4 | s32 | deploy_timer | reserve countdown (delay code 1..9 -> rand ranges << 8) | ai_group_orders_from_mission, ai_group_reinforcement_tick |
| 10 | 4 | s32 | think_timer | reaction delay; initial `slot*0x180 + 0x100` in the low word | ai_update |
| 14 | 4 | far* | script | group script step (unreferenced code only) | ai_group_start_script |
| 18 | 0x64 | Command[5] | commands | team ring (never written by live code) | ai_member_current_command |
| 7C | 1 | u8 | cmd_index | | |
| 7D | 1 | u8 | cmd_pending | | ai_group_commands_consumed |
| 7E | 1 | u8 | script_active | | ai_group_run_scripts |
| 7F | 1 | u8 | blocked | | ai_group_check_blocked (unreferenced), ai_group_alert |
| 80 | 1 | u8 | unk_80 | cleared | ai_group_orders_reset |
| 81 | 1 | u8 | look_counter | `6 − slot` | ai_update |
| 82 | 1 | u8 | order_type | `AiOrderType` 0 none, 1 patrol, 2 hold, 3 reserve | ai_group_orders_from_mission, ai_group_has_move_orders, map_draw_info_panel |
| 83 | 1 | u8 | behaviour | 0x10 hunt, 0x20 guard, 0x30 (ignores passive NPC noise) | ai_update, noise_hear, map_draw_info_panel |
| 84 | 4 | far* | cur_waypoint | | ai_group_next_destination |
| 88 | 4 | far* | waypoints | list head | ent_add_waypoint, ent_clear_waypoints |
| 8C | 1 | s8 | patrol_dir | 1 forward, 0 backward | ai_group_next_destination |
| 8D | 1 | u8 | unk_8d | | — |

`WaypointNode` (0x14): +00 Vec3 pos, +0C far prev, +10 far next.
`ScriptStep` (0x1A): +00 Command, +14 s16 delay, +16 far next.

## Teams

### Team — group (0x34 bytes, `ent2_alloc`)

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 0x20 | far*[8] | members | NULL terminated, [0] leader | ent_group_add_unit, ent_group_split/merge, ent_group_replace_leader |
| 20 | 2 | s16 | formation | `Formation` 0 Column, 1 In-Line, 2 Diamond, 3 Vee Wedge (MTM +0x20 for enemies) | cmd_order_keys, evt_team_formation_update, evt_team_snap_formation, evt_member_watch_heading |
| 22 | 2 | s16 | fire_order | `FireOrder` 0 field, 1 at target, 2 at will, 3 cease, 4 cover, 5 demolish, 6 snipe; created 2 | cmd_order_keys, map_screen_keys, evt_unit_killed (2 when the Point Man dies), evt_execute_ai_commands, ai_target_priority, ent_group_is_passive_npc |
| 24 | 2 | s16 | order | foot teams `TeamOrder` (−1 none, 0 halt, 1 ASAP, 2 stealth, 4 search); craft `CraftOrder` (0 parked, 2 extract, 3 emergency, 4 loiter, 5 attack, 6 cease attack) | cmd_order_keys, evt_search_begin, map_screen_keys, ent_order_extraction, evt_update_support_craft, grp_set_craft_status |
| 26 | 2 | s16 | type | `TeamType` | everywhere |
| 28 | 4 | far* | ai | `TeamAi` | 4a37 |
| 2C | 4 | s32 | map_height | map zoom: 256000 SEAL / 512000 others, −1 = derive | ent_group_create, map_screen_keys, map_select_team_height, dbrf_init |
| 30 | 2 | s16 | view_distance | orbit camera distance (0x60..900) | ent_group_create (0x60), field_view_keys, view_update_camera |
| 32 | 2 | s16 | view_heading | orbit camera angle, −1 = recompute | ent_group_create, view_init_orbit_angle, evt_unit_bounce_off_obstacle |

Team indices: `g_seal_team` (EC88, 0), `g_support_team` (EC8C, insertion craft),
`g_craft_team` (EC8D, extraction craft), `g_emergency_group` (EC8E),
`g_first_enemy_group` (EC8F, first MTM team), `g_fire_support_group` (EC90),
`g_break_contact_group` (EC91), `g_extract_craft_team` (EC92),
`g_extra_teams` (EC9C, split SEAL teams 0..2), `g_team_count` (EC87).

## Weapons and items

### WeaponDef — weapon table entry (DS:48FE, 34 x 0x22)

| Off | Size | Type | Name | Meaning (M16A2 value) | Read by |
|---|---|---|---|---|---|
| 00 | 2 | near* | short_name | "M16A2" | HUD, map, briefing |
| 02 | 2 | near* | long_name | "M16" | HUD, briefing |
| 04 | 2 | s16 | wclass | `WeaponClass` (3) | prj_fire, shot_shooter_skill, evt_projectile_init_motion, evt_start_reload |
| 06 | 2 | u16 | availability | year bits 0..3, enemy 0x10/0x20 (0x0F) | loadout_next_weapon |
| 08 | 2 | s16 | range_short | 180 | shot_range_bands, wpn_range_band |
| 0A | 2 | s16 | range_medium | 480 | same |
| 0C | 2 | s16 | range_max | 2400 | wpn_in_range, wpn_best_range, ai_member_weapons_in_range |
| 0E | 2 | s16 | magazine | 30 | wpn_reload, ent_weapon_init |
| 10 | 2 | u16 | fire_modes | 7 | wpn_next_rof, wpn_ai_pick_rof, prj_fire |
| 12 | 2 | s16 | blast_radius | 0 | shot_blast_victims, shot_apply_damage |
| 14 | 2 | s16 | structure_damage | 1 | shot_apply_damage |
| 16 | 2 | s16 | noise | 1500 (level = noise*4) | noise_from_weapon |
| 18 | 2 | s16 | weight | 76 (1/10 lb) | loadout_compute_weight |
| 1A | 2 | s16 | reload_weight | 11 | loadout_compute_weight |
| 1C | 2 | s16 | jam | 3 | shot_fire |
| 1E | 2 | s16 | reload_ticks | 0x400 | wpn_set_reload_timer |
| 20 | 2 | s16 | default_reloads | 8 | loadout_build_member, loadout_edit_field |

Entries: 0 M3A1, 1 M16A2, 2 CAR15, 3 M76, 4 M39, 5 M63 Stoner, 6 M60, 7 M37,
8 M45, 9 M79, 10 M203, 11 M72 LAAW, 12 DEMO, 13 M26 (not selectable), 14 M26,
15 M15 WP, 16 M18, 17 M7, 18 56SKS, 19 AK47, 20 SVD, 21 K50, 22 T.M33, 23 T.42,
24 RDG33, 25 T.M32, 26 SG1, 27 RPG, 28 T.31 mortar, 29 Minigun, 30 Rocket,
31 Mk3A2 stun, 32 Mk18, 33 Mk I illumination.

### ItemDef — item/tool table (far 52E3:0000, 12 x 6)

+00 near short name, +02 near long name, +04 s16 weight (1/10 lb):
0 PRC25 Radio 45, 1 Med Medical Kit 15, 2 PHK Prisoner Handling Kit 8,
3 Flare 10, 4 Nite Night Vision Scope 30, 5 Docs Documents 3, 6 Binoc
Binoculars 20, 7 $$$ Bag of Cash 100, 8 Ppgda Propaganda leaflets 3,
9 SrvKt Survival kit 50, 10 Rice 200, 11 BbyKt Booby trap kit 75.

### WeaponNode (0x12), ItemNode (8), Loadout header (0x0C)

| Record | Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|---|
| WeaponNode | 00 | 1 | u8 | type | `WeaponId` | ent_weapon_init (365e:000C) |
| | 01 | 1 | u8 | unk_01 | | |
| | 02 | 2 | s16 | rounds | loaded rounds | wpn_reload, wpn_spend_rounds, field_view_keys |
| | 04 | 2 | s16 | reloads | spare magazines (8 when −1) | wpn_reload, evt_start_reload, map_draw_info_panel |
| | 06 | 2 | s16 | m203_count | M203 grenades fired | field_view_keys, hud_draw_text_lines |
| | 08 | 1 | u8 | jammed | next shot is a dud | shot_fire, prj_fire, ai_group_combat |
| | 09 | 1 | u8 | unk_09 | | |
| | 0A | 2 | s16 | unk_0a | init 3 | ent_weapon_init |
| | 0C | 1 | u8 | fire_mode | selected bit (first of Semi, Single, Full, Grenade) | wpn_next_rof, shot_fire |
| | 0D | 1 | u8 | unk_0d | | |
| | 0E | 4 | far* | next | | |
| ItemNode | 00 | 1 | u8 | type | `ItemType` | ent_item_init |
| | 01 | 1 | u8 | unk_01 | | |
| | 02 | 2 | s16 | quantity | | inv_count_items, inv_use_items |
| | 04 | 4 | far* | next | | item_cycle_tool |
| Loadout | 00 | 4 | far* | list | first node | ent_unit_add_weapon |
| | 04 | 4 | far* | primary | current weapon | wpn_select_next_weapon |
| | 08 | 4 | far* | secondary | current grenade / second weapon | wpn_select_next_grenade |

## Ordnance and combat

### Projectile (0x50 bytes; 32 in `g_fx_pool`, DS:2670)

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 2 | near* | base_model | restored after effects (0x832E 40 mm) | fx_create_pools, prj_start_burst |
| 02 | 4 | far* | body | Obj3D | |
| 06 | 4 | far* | flight | `FlightRec` | evt_projectile_init_motion, evt_update_ordnance |
| 0A | 2 | s16 | wclass | ordnance class | prj_fire |
| 0C | 2 | s16 | weapon | WeaponId | prj_fire |
| 0E | 2 | u16 | fire_modes | weapon table +0x10 (satchel 8) | prj_fire, evt_update_ordnance |
| 10 | 4 | s32 | launch_time | | prj_fire |
| 14 | 4 | s32 | muzzle_phase | 0x40 / buckshot 0x20 / classes 0xF,0x10,0x14 0x180; 0 for direct shots | prj_fire, evt_update_ordnance |
| 18 | 4 | s32 | timer_base | = launch time | prj_fire |
| 1C | 4 | s32 | impact_time | | evt_update_ordnance, prj_start_burst |
| 20 | 4 | s32 | lifetime | | prj_fire, evt_calc_launch_velocity |
| 24 | 4 | s32 | detonate_time | | prj_fire, prj_start_burst |
| 28 | 1 | u8 | hit | 1 unit, 2 obstacle, 4 landed; 0x10..0x80 puff kind | evt_update_ordnance, shot_aimed_hit_roll, unit_projectile_idle |
| 29 | 1 | u8 | state | 1 resolved, 2 expired, 0x10 dud, 0x80 in use | prj_fire, evt_update_ordnance, unit_projectile_idle/stop |
| 2A | 4 | far* | target | intended target unit | prj_fire |
| 2E | 4 | far* | last_hit | last collision (unit or world object) | evt_update_ordnance, shot_aimed_hit_roll |
| 32 | 4 | far* | owner | shooter | |
| 36 | 4 | far* | weapon_node | weapon slot fired | evt_projectile_init_motion |
| 3A | 6 | u8[6] | unk_3a | | — |
| 40 | 16 | far*[4] | fx_muzzle, fx_explosion, fx_cloud, fx_impact | effect objects | prj_find_by_effect, spr_draw_scenery_billboard |

### FlightRec — projectile motion (0x48 bytes, Mover layout)

Allocated by `fx_alloc` bit 1 with the Mover size; `evt_projectile_init_motion`
(2dbd:41A4) fills it.

| Off | Size | Name | Meaning |
|---|---|---|---|
| 00 / 02 | 2 / 2 | speed / target_speed | horizontal speed by class: bullets 0x168, M79 0x168, M203 0x1E0, thrown 0x5A, smoke 0x60, gas 0x48, LAAW/RPG 0x258, rocket 0x21C, mortar 0x3C |
| 04 | 2 | max_speed | 0x3E7 (bullets) |
| 08 | 2 | accel | 0x20 |
| 10 | 2 | unk_10 | 0x168 |
| 14 | 2 | turn_rate | 0x20 |
| 18 | 2 | vertical_speed | from evt_calc_launch_velocity (initial 9) |
| 1A | 2 | unk_1a | 9 |
| 1C | 2 | unk_1c | class constant −0x10, −0x1E, −0x24, −0x30, 0x96, 0xB4, 0xD2, 0xF0 (looks like a launch pitch in 1/8 deg) |
| 1E | 2 | launch_height | 3 thrown/smoke/gas, 6 rocket |
| 20 | 2 | bounces | 1 = bounces |
| 22 | 2 | gravity | 1 = falls |

### ShotRec — shot (combat) record (0x3C bytes, far 53BA:28BA x 32)

| Off | Size | Type | Name | Meaning | Written by / read by |
|---|---|---|---|---|---|
| 00 | 1 | u8 | state | 0 in flight, 1 resolved/free | shot_fire / shot_update_all, ai_reset_shot_slots |
| 01 | 1 | u8 | unk_01 | | |
| 02 | 4 | far* | shooter | | shot_fire / hit roll, damage |
| 06 | 12 | Vec3 | shooter_pos | | |
| 12 | 2 | s16 | shooter_move | MoveMode (1 −5, 2 −10) | shot_aimed_hit_roll |
| 14 | 2 | s16 | shooter_posture | (prone +10, crouch +5) | |
| 16 | 2 | s16 | target_kind | TargetKind | |
| 18 | 4 | far* | target | | |
| 1C | 2 | s16 | target_move | (1 −2, 2 −5) | |
| 1E | 2 | s16 | target_posture | (prone −10, crouch +5) | |
| 20 | 1 | u8 | unk_20 | 0 | shot_fire |
| 21 | 1 | u8 | unk_21 | | |
| 22 | 2 | s16 | weapon | WeaponId | |
| 24 | 4 | s32 | range | | shot_range_bands |
| 28 | 1 | u8 | fire_mode | bit used (single −5, full +15) | |
| 29 | 1 | u8 | rounds | | shot_apply_damage |
| 2A | 2 | s16 | cover | cover toward the target (>= 75 −30, >= 50 −10, >= 25 −5); 0 for the player | shot_aimed_hit_roll |
| 2C | 12 | Vec3 | target_pos | | |
| 38 | 4 | far* | projectile | trajectory from prj_fire | unit_projectile_idle / unit_projectile_stop (called with a ShotRec), shot_aimed_hit_roll |

`ShotVictim` (8 bytes, 16 on the stack, `shot_clear_victims` 19ac:8524): +00
far unit or world object, +04 s16 band 0..2, +06 u8 kind (0 person,
1 structure, 2 craft, 3 empty).

`WoundTableEntry` (4 bytes): +00 u8 lo, +01 u8 hi, +02 u16 hit bits (0xFFFF
miss, 0 none, 0x4000 killed); 17 entries per table (the last all zero). The
contents must be read from the executable (the scan runs into the following
table; a roll of 0 ends on the zero entry at 525F:0154).

### NoiseEvent (0x18 bytes, far 53BA:26DA x 20)

| Off | Size | Type | Name | Meaning | Read / written by |
|---|---|---|---|---|---|
| 00 | 2 | s16 | type | `NoiseType` | noise_alloc, noise_hear |
| 02 | 2 | s16 | level | 525C:type*2 minus modifiers | noise_from_team/weapon/explosion |
| 04 | 2 | s16 | heard | first noise_update sets it, second frees the slot | noise_update |
| 06 | 4 | far* | source | source team | noise_hear |
| 0A | 12 | Vec3 | pos | | noise_hear |
| 16 | 2 | s16 | free | | noise_reset, noise_alloc |

## Presentation and UI

### Camera (0x1C bytes)

| Off | Size | Type | Name | Meaning |
|---|---|---|---|---|
| 00 | 12 | Vec3 | pos | x, altitude, z (`view_init_camera` stores args << 8) |
| 0C | 2 | Angle8 | yaw | |
| 0E | 2 | Angle8 | pitch | map camera −90 deg |
| 10 | 2 | Angle8 | roll | |
| 12 | 8 | s16[4] | rect_x/y/w/h | viewport: main (0,8,320,171), map (8,10,204,155), small (0,24,320,139) |
| 1A | 2 | s16 | zoom | projection zoom shift, 8 |

Written by `view_init_camera` (1000:21F0), `view_update_camera` (1000:2544),
`brfcam_update`, `map_screen_keys` (map height = `g_cam_map`+4); read by
`r3d_render_view` (heading, pitch, roll, rect, zoom in that order).

### MsgEntry (0x52 bytes, `g_msg_queue` DS:3716 x 8)

| Off | Size | Type | Name | Meaning |
|---|---|---|---|---|
| 00 | 2 | s16 | style | −1 free, 0/1 dialog lines, 2 HUD line, 3 hand-signal icon line, 6 special text renderer |
| 02 | 2 | s16 | sticky | the last sticky entry cannot be removed |
| 04 | 2 | s16 | duration | 0 = shown with the previous entry |
| 06 | 4 | s32 | expiry | −1 not started |
| 0A | 2 | s16 | kind | 0..2 dialog font DS:EEF4[kind], 3 HUD overlay, 4 medal picture (frame `style % 8` of the mdl set 53BA:2140) |
| 0C | 4 | far* | text_ptr | original text |
| 10 | 64 | char[64] | text | copy |
| 50 | 2 | s16 | icon | hand-signal number |

`msg_queue_add/remove/tick/draw` (1000:7DF2..8339).

### BrfCamWaypoint (16 bytes, far 5170:0000 x 8)

+00 s16 flag (−1 empty), +02 s16 (always 1), +04 s16 duration, +06 s32 end time
(−1 not started), +0A s16 camera height, +0C far target position.
`brfcam_queue_push/remove/tick`, `brfcam_update` (365e:1F28..20D2).

### Button (16 bytes)

| Off | Size | Type | Name | Meaning |
|---|---|---|---|---|
| 00..06 | 8 | s16[4] | x, y, w, h | hit box uses h − 2 |
| 08 | 2 | u16 | key | key injected (ASCII or scan << 8); 0 terminates front-end lists |
| 0A | 1 | s8 | underline | underlined letter index, −1 none |
| 0B | 1 | u8 | flags | 0x01 clickable, 0x02 disabled, 0x04 label not drawn, 0x10 highlighted, 0x20 extra frame, 0x40 invisible hot spot |
| 0C | 4 | far* | label | map buttons (DS:0BF4, bound by `map_init`); 0 in front-end lists, which use parallel near label arrays |

### Sound (segment 4592)

`SoundChannel` (0x2A, far 53BA:258C x 6): +00 s16 effect id, +02 u16 flags (bit0
active, bit1 from descriptor, bit3 unpositioned, bit4 digital voice), +04 s16 FM
parameter, +06 s32 start, +0A s32 lifetime, +0E Vec3 position, +1A unknown,
+1C s16 FM sequence (−1), +1E far FM state table, +22 u8[2] FM controllers,
+24 far attached unit (its body position is followed), +28 s16 digital chunk
counter.

`SfxDesc` (0x16, far 520D:000C + id*0x16, ids 1..53): +00 u16 audible distance,
+02 u16 flags (bit1 positional), +04 u8 FM version, +05 u8 digital version,
+06 far sample, +0A u16 FM sequence (id + 10), +0C far XMI, +10 s16 EMS handle,
+12 u16 priority, +14 u16 sample size. The static fields come from st.exe.

### Sprites (segment 348e)

`SpriteFrame` (0x50): +00 far image[8] per rotation, +20 s16 EMS handle[8]
(−1 none), +30 far RLX record[8]. `RlxRecord` (first 12 of 18 file bytes):
+00 0, +02 anchor x, +04 anchor y, +06 u8 headgear frame, +07 u8 layer
(> 0x80 headgear behind the body), +08 s16 head x, +0A s16 head y.

`MohRecord` (far 52BB:001A x 9): +00 u16 year, +02 u16 mission index, +04 u16
minimum score.

## Files and campaign data

All little endian. `types.h` declares `parse*` / `serialize*` functions that
read and write exactly these layouts.

### MCI mission header `cYmNN.mci` (0x176 bytes; msns.lib) — `msn_load` (365e:237E)

Verified on all 80 missions (size, MTM count = `.mtm` record count).

| Off | Size | Type | Name | Meaning |
|---|---|---|---|---|
| 000 | 2 | u16 | start_hour | tod_set |
| 002 | 2 | u16 | start_minute | |
| 004 | 2 | u16 | world | world index (names DS:1CD0) |
| 006 | 2 | u16 | insertion_method | `InsertionMethod`: 1 LSSC (model lssc), 2 LSSC (model mike2), 4 PBR (model mike2), 8 UH-1 (names DS:25C0[v & ~1]) |
| 008 | 2 | u16 | extraction_method | same |
| 00A | 12 | Vec3 | insertion | |
| 016 | 12 | Vec3 | extraction | |
| 022 | 40 | char | insertion_desc | not used by the code |
| 04A | 40 | char | extraction_desc | not used by the code |
| 072 | 2 | u16 | fire_support | `SupportUnit`: 0 none, 1 OV-10 pair, 2/4 boat pair, 8 helicopter pair (names DS:25CC[v & ~1]) |
| 074 | 2 | u16 | break_contact | same codes; spawned only when fewer than 3 craft exist |
| 076 | 0x62 | MciObjective | objective[0] | primary |
| 0D8 | 0x62 | MciObjective | objective[1] | secondary |
| 13A | 0x3A | MciObjective | objective[2] | tertiary; only +00..+39 exist (no route text) |
| 174 | 2 | u16 | mtm_count | |

`MciObjective` (0x62): +00 u16 kind (`ObjectiveKind`), +02 Vec3 position, +0E
s16 target team (MTM index, −1), +10 s16 target structure (`g_world_objects`
index, −1), +12 char[40] description, +3A char[40] route text. Read by
`msn_check_objective` (365e:251D), `score_eval_objective`, the briefing and
map screens, `loadout_build_member`, `wld_find_mission_point`.

### MTM team record `cYmNN.mtm` (0x66 bytes, n <= 15)

| Off | Size | Type | Name | Meaning | Read by |
|---|---|---|---|---|---|
| 00 | 2 | u16 | kind | `MtmKind` 0 civilian, 1 VC, 2 NVA, 3 friendly | ent_spawn_mtm_team |
| 02 | 12 | Vec3 | start | | ent_spawn_mtm_team |
| 0E | 2 | u16 | member_count | 1..5; reduced in place by Team Size | ent_spawn_mtm_team |
| 10 | 16 | u16[8] | members | vcNN numbers 1..21 (only `count` used) | ent_spawn_mtm_team |
| 20 | 2 | u16 | formation | team formation (0..2 in data) | ent_group_create |
| 22 | 1 | u8 | order_type | 1 patrol, 2 hold, 3 reserve (members spawned hidden, crouched/prone) | ai_group_orders_from_mission, ent_spawn_mtm_team |
| 23 | 1 | u8 | behaviour | 0x10 / 0x20 / 0x30 | ai_group_orders_from_mission |
| 24 | 2 | u16 | deploy_delay | reserve delay code 1..9 | ai_group_orders_from_mission |
| 26 | 2 | u16 | unk_26 | 0/1 (12 records), no reader | — |
| 28 | 2 | u16 | heading | deg, member 0 | ent_spawn_mtm_team |
| 2A | 60 | Vec3[5] | waypoints | patrol (y ignored, (0,0) unused) | ai_group_orders_from_mission |

### SE personnel record `*.SE` (0x90 bytes; se.lib)

`roster_load_seal_se` (365e:384F), `npc_se_load` (365e:391E); 48 SEALs, 21 VC,
CIV01, FRND01..09 all decode to 144 bytes.

| Off | Size | Type | Name | Meaning | Read by |
|---|---|---|---|---|---|
| 00 | 12 | char | first_name | | name lines |
| 0C | 20 | char | last_name | speaker name | messages, dialogues |
| 20 | 26 | char | nickname | NPC: descriptive tag | marching order |
| 3A | 32 | char | birthplace | "Lexington, KY\n" | bio panel |
| 5A | 1 | u8 | height_ft | | bio panel |
| 5B | 1 | u8 | height_in | | |
| 5C | 1 | u8 | weight_lb | | |
| 5D | 1 | u8 | buds_class | BUD/S class index; bit 0 -> "SEAL Team 2" | bio panel |
| 5E | 1 | u8 | rating | 0..12 | rank lines |
| 5F | 1 | u8 | age | + campaign year | bio panel |
| 60 | 1 | u8 | unk_60 | never read (SEALs 0..12, NPCs 8) | — |
| 61 | 1 | u8 | rank | 0..11 | rank lines, roster_set_rank (w), cmp_load (w) |
| 62 | 1 | u8 | camouflage | DS:2536 names; SEAL headgear; NPC cache: index + 1 | ui_draw_bio_panel, spr_draw_soldier_frame, loadout_build_member |
| 63 | 8 | s8[8] | weapons | −1 terminated; [0] best, [1] second, [2]/[3] slots C/D | recruit_screen, loadout_build_member, ent_spawn_mtm_team |
| 6B | 8 | s8[8] | items | −1 terminated | ent_spawn_mtm_team |
| 73 | 1 | u8 | unk_73 | 0 | — |
| 74 | 8 | u8[8] | skill | Rifle..Radio | roster_entry_init, rating panel |
| 7C | 1 | u8 | size | | |
| 7D | 1 | u8 | strength | | |
| 7E | 1 | u8 | agility | | |
| 7F | 1 | u8 | intelligence | | |
| 80 | 1 | u8 | experience | Time In Service | |
| 81 | 11 | u8 | unk_81 | zero in files (image of Status+0D..17) | |
| 8C | 2 | u16 | load | carried load, 1/10 lb | loadout_compute_weight |
| 8E | 2 | u16 | unk_8e | Status+1A image | |

### Campaign save `cN.cmp` (N = 0..8; 8 = autosave) — `cmp_load` (365e:3A55), `cmp_save` (365e:3BC4)

`0x14C + n*0x18` bytes (n = (size − 0x14C) / 0x18, max 40). All nine shipped
files (908 bytes, 24 entries) parse with this layout.

`CampaignHeader` (0x14C, `g_campaign_state` DS:3B0E):

| Off | Size | Type | Name | Meaning |
|---|---|---|---|---|
| 000 | 1 | s8 | slot | replaced by the slot number on load and save; −1 practice |
| 001 | 1 | u8 | year | 0..3 |
| 002 | 1 | u8 | mission | 0..19, 0xFF campaign over |
| 003 | 1 | u8 | last_won | init 1 |
| 004 | 2 | s16 | point_man | SE number |
| 006 | 4 | s32 | total_score | |
| 00A | 4 | s32 | best_score | |
| 00E | 28 | char | nickname | typed on the recruit screen |
| 02A | 0x120 | CampaignHistory[24] | history | |
| 14A | 1 | s8 | pm_weapon_a | |
| 14B | 1 | s8 | pm_weapon_b | |

`CampaignHistory` (12): +0 u16 mission (year*20 + index), +2 s32 score, +6 u16
awards (last write wins), +8 u16 point man hit bits, +A u8 new rank, +B u8 won.
Writers: `cmp_record_mission` (365e:3FD0), `roster_add_awards`,
`roster_set_rank`, `roster_record_casualty`. Readers use two bases:
`cmpscr_msg_awards` reads awards at campaign+0x24+12n and rank at +0x28+12n
with n = missions after the increment (= entry n−1); `dbrf_screen` tests
campaign+0x33+12n (entry n, byte +9) before the increment.

`RosterEntry` (0x18): +00 u8 SE id, +01 u8 missions, +02 u8 wins, +03 u8 rank,
+04 u16 awards, +06 u16 unknown, +08 u16 accumulated hit bits (0x4000 KIA),
+0A u8[8] skills, +12 u8 flags (0x01 in team, 0x02 wounded, 0x04 reserve
recruit, 0x08 killed), +13 unknown, +14 far SE pointer (run time; garbage in the
file — the port keeps the raw value for byte-exact re-saves).

### Campaign flow entry (14 bytes, far 5275)

+0 u16 stage 0..5, +2 u16 month 1..12, +4 u16 day (unused), +6 u16 HHMM
(unused), +8 u16 next if lost, +A u16 next if won (0 = end of the year),
+C u16 flags (bit 0 historic report exists, bit 1 unknown).
`cmp_record_mission`, intel/briefing/debriefing screens.

### Team loadout record (12 bytes, far 5178:0000)

Point Man +00, Officer-in-Charge +0C, Corpsman +18, Rear Security +24, 0xFF at
+30. +0 s8 SE id (−1 ends), +1 u8 camouflage (SE+0x62), +2 s8[4] weapons A..D,
+6 u8[4] reloads, +A s8[2] tools E/F. `loadout_build_team` (365e:45A8),
`loadout_build_member`, `loadout_edit_field`, `ent_spawn_seal_team`,
`camp_build_teams`, `bull_next_line`, `dbrf_say_casualties`.

### Difficulty options `st1.dfr` (16 bytes = far 5178:0148)

| Off | Name | Values (shipped) |
|---|---|---|
| 0 (0148) | ammo | 0 Unlimited, 1 Real (1) |
| 2 (014A) | enemy_wounds | 0 Death, 1 Heavy, 2 Real (2) |
| 4 (014C) | intelligence | 0 Minimal, 1 Decreased, 2 Real (2) |
| 6 (014E) | player_wounds | 0 None, 1 Decreased, 2 Real (2) |
| 8 (0150) | reload_time | 0 Instant, 1 Timed (1) |
| A (0152) | team_size | 0 Decreased, 1 Real, 2 Enhanced (1) |
| C (0154) | weapons | 0 Unlimited, 1 Real (1) |
| E (0156) | map | 0 Freeze, 1 Real (1) |

`cfg_load_difficulty` (365e:48F1), `diff_handle_input` (365e:E7EC).

### `s.cnf` (0xE8 bytes = far 5178:0060..0147)

+00 u8[3] unknown, +03 s8 last slot (−1 none), +04 u8[8] slot used, +0C
char[8][26] campaign names, +DC u8[12] unknown. `cfg_load_s_cnf` (365e:4897),
`cfg_save_s_cnf` (365e:487B).

### World files (worlds.lib) — `wld_load` (1000:3EFD)

`.w`: 18-byte records (max 524): +00 u8 heading code (deg = 2h, +1 if 2h mod 15
!= 0), +01 unknown, +02 s32 x, +06 s32 z (map units), +0A u8 model table index,
+0B unknown, +0C u16 flags, +0E u16 hit points, +10 u8 cover, +11 u8 height.
`.wd`: 64 bytes, +00 u8 (0), +01 area name (NUL terminated, stale bytes follow),
loaded to DS:ECB8 (`g_area_name` = DS:ECB9).

### Text files `.S`

Fixed 80-byte NUL-padded lines: `cYmNN.s` 24 lines (0–4 intel, 5–6 comment after
a win, 7–8 after a loss, 9–13 bull session, 14–18 debriefing success, 19–23
failure), `hYmNN.s` 28 lines, `c.s` credits, `c0..c5.s` speeches.
`sealNN.s`: 6 records of 84 bytes = 80-byte line + u8 year_from, tier_from,
year_to, tier_to (0xFF = always). `msn_load_text` (365e:227C),
`msn_load_seal_bio_text` (365e:2319).

## Enumerations (summary)

The complete lists with comments are in `types.h`.

| Enum | Values |
|---|---|
| `GameModeCode` (DS:D7E4) | 1 campaign, 2 menu/new campaign, 3 practice, 6 command-line mission |
| `ViewMode` (DS:D844) | 0 first person, 1 map, 2 chase, 3/4/0xF/0x10 support 1–4, 5/6 camp cut-scenes, 7 insertion, 8 extraction, 0xC re-insertion map, 0xD target, 0xE enemy, 0x11/0x12 split A/B |
| `TeamType` | 0 SEAL, 1 boat, 2 helicopter, 3 aircraft, 4 VC, 5 NVA, 6 civilian, 7 friendly |
| `Formation` | 0 Column, 1 In-Line, 2 Diamond, 3 Vee Wedge |
| `FireOrder` | 0 field of fire, 1 at target, 2 at will, 3 cease, 4 cover, 5 demolish, 6 snipe |
| `TeamOrder` / `CraftOrder` | −1 none, 0 halt, 1 ASAP, 2 stealth, 4 search / 0 parked, 2 extract, 3 emergency, 4 loiter, 5 attack, 6 cease attack |
| `MoveMode` / `Posture` / `AltitudeMode` | stop, slow, run, back / upright, crouch, prone, (dead) / high, medium, low |
| `UnitClass` | 0 SEAL, 3 VC, 4 NVA, 5 boat, 6 helicopter, 7 aircraft, 8 civilian, 9 friendly |
| `ObjectiveKind` | 0 none, 1 patrol, 2 ambush, 3 demolition, 4 observe, 5 rescue, 6 snatch, 7 recover |
| `WeaponClass` | 1 rifle, 2 sniper, 3 assault rifle, 4 grenade launcher, 5 M203, 6 SMG, 7 light MG, 8 MG, 9 shotgun, 10 explosive, 12 smoke, 13 WP/stun, 14 tear gas, 15 rocket launcher, 16 rocket, 17 minigun, 18 silenced pistol, 19 Swedish K, 20 mortar |
| `TerrainKind` | see the model table above |
| `AnimState`, `CommandType`, `AiOrderType`, `NoiseType`, `HandSignal`, `ItemType`, `WeaponId`, `MtmKind`, `InsertionMethod`, `SupportUnit`, `TargetKind` | see `types.h` |

## Resolved disagreements

1. **Formation codes 2/3.** `seg_2dbd_movement.md` read 2 = Vee, 3 = Diamond;
   `seg_19ac.md` and `seg_2dbd_teams_ai.md` 2 = Diamond, 3 = Vee Wedge.
   Evidence: `cmd_order_keys` writes team+0x20 = 2 together with hand signal
   0x0B and +0x20 = 3 with signal 0x0A (19ac:1496/149C and 14E3/14E9); the
   signal names at DS:1CB0 are index 10 "VeeWedge", 11 "Diamond"; the map
   buttons 19..22 (CoLumn, In Line, Diamond, VeeWedge) are highlighted by
   +0x20; the slot table DS:07DC (slot-major, one (x,z) pair per formation)
   gives formation 2 the offsets (8,−8), (−8,−8), (0,−16), (0,−24) — a diamond
   around the leader — and formation 3 (8,−8), (−8,−16), (16,−24), (−16,−32) —
   a wedge. **Decision: 2 = Diamond, 3 = Vee Wedge.**
2. **Team +0x22.** `seg_2dbd.md`: "result (2 = Point Man dead)"; `seg_libs.md`
   "mode"; `seg_365e_a.md` "parameter 2"; `seg_19ac.md` fire order. The only
   write in `evt_unit_killed` (2dbd:0913) stores 2 there after the Point Man's
   death, next to +0x24 = 0; every other writer is an order key (0..6) and
   `evt_execute_ai_commands` / `ai_target_priority` test it as the fire order.
   **Decision: fire order; the Point Man's death sets team 0 to Halt + Fire at
   Will.**
3. **Team +0x24 for craft.** `seg_365e_a.md` read 3 = "extraction run ordered",
   2 = "in progress"; `seg_2dbd_craft_ordnance.md` 2 = extraction, 3 = emergency.
   The map key 'e' stores 2, 'y' (emergency group) stores 3,
   `ent_order_extraction` (automatic emergency extraction) stores 3, and
   `ent_any_craft_moving` accepts both. **Decision: 2 = Extract, 3 = Emergency
   extraction.** `seg_libs.md` "subtype" is the same field.
4. **Team +0x2C.** "range" (365e_a) vs map height (19ac). 0x3E800 = 256000 is
   the SEAL map zoom set by `map_init`, 0x7D000 the default of other teams;
   `map_screen_keys` stores the zoom there and `dbrf_init` resets it.
   **Decision: map zoom height.**
5. **Unit record.** +0x06 was "personnel/body" (19ac), STATUS (2dbd), "stats
   block" (365e_a) — one record, named `Status`. +0x2C was "roster record
   (+0xC name, +0x62 number)" in 19ac: the roster entry is +0x28
   (`ent_unit_init_personnel`), +0x2C is the SE record (+0x0C last name,
   +0x62 camouflage). `seg_1000.md` listed "+0x24 marker" and "+0x38
   projectile": +0x24 holds the object of `fx_unit_ground_fx` or
   `fx_team_marker` (`attached_fx`); +0x38 cannot exist in a 0x34-byte record
   — `unit_projectile_idle/stop` (1000:575A/5790) are only called from
   `shot_update_all` with a **shot record**, whose +0x38 is the projectile.
   +0x00 is the model descriptor chosen by `unit_set_class_word`.
6. **Status +0x08 / +0x0C.** 19ac: +0x08 "experience used by hit rolls",
   +0x0C "morale/experience"; 2dbd: +0x08/+0x09 physical attributes. The
   status block is SE+0x74.., whose attribute 8 is named "Size" (DS:24EC);
   `unit_load_level` uses size/2 + strength; the sprite scale uses it; AI
   sight modifiers treat large targets as more visible. `shot_aimed_hit_roll`
   (19ac:7D31) indeed adds +30/+10 for the shooter's byte +0x08 ≥ 60/30.
   **Decision: +0x08 = size (hit-roll use kept as a quirk); +0x0C =
   experience / Time In Service** (`unit_add_experience` +2 per mission, cap 90).
7. **Mover flags +0x26/+0x27.** 2dbd: 0x08 wading, 0x04 grass/brush, 0x02
   other slow terrain, 0x40 "suppresses music changes"; teams_ai: 0x01 detour,
   0x04 in water, 0x08 deep water; 365e_a: +0x27 0x02 "on a structure tile".
   `evt_update_unit_movement` sets 0x04 for terrain kinds 7/8/9 and also 0x08
   for kind 9 (2dbd:1EFA/1F0D), clears 0x0C and sets 0x01 on an obstacle
   (1F7D/1F82), and clears the low nibble when no terrain is under the unit
   (242B); 0x02 is tested by `evt_calc_move_speed` but never set. 0x40 is set
   by `evt_update_extraction_pickup` when boarding (2DBD:34C9) and tested by
   `ent_team_all_extracted`; the music test in `evt_unit_killed` is "not
   aboard". `ent_mark_enemies_on_structures` sets +0x27 bit 0x02 on VC/NVA
   standing on kinds 7/8/9 — water, not structures — and the movement code then
   attaches the sampan object (`fx_team_marker`). **Decision: table in
   Mover above.**
8. **Mover +0x1A.** "eye-height offset (Point Man)" (2dbd) vs target height.
   `evt_set_posture` writes {15,9,3} there for the Point Man,
   `evt_update_unit_movement` writes the wading height for everybody, and the
   height +0x18 approaches +0x1A at +0x20/+0x22; the camera uses +0x18.
   **Decision: target height.** +0x44 is the posture-change timer for soldiers
   (2dbd) and the radio busy time for craft (`radio_call`): both uses kept.
9. **Obj3D angles.** 2255 calls them a0/a1/a2; craft_ordnance
   heading/pitch/roll. `evt_update_support_craft` writes the boat pitch to
   +0x14 and the helicopter/aircraft bank to +0x16; the renderer passes them to
   the same matrix code as the camera's heading/pitch/roll. **Decision:
   heading, pitch, roll** (+0x14 doubles as animation frame for hooked models).
10. **Camera +0x0C/+0x0E.** 19ac: "+0xE heading·8"; seg_1000: +0x0C yaw,
    +0x0E pitch. `view_init_camera` stores `pitch << 3` at +0x0E and zeroes
    +0x0C/+0x10; `r3d_render_view` takes heading, pitch, roll from +0x0C,
    +0x0E, +0x10. **Decision: +0x0C yaw, +0x0E pitch, +0x10 roll.**
11. **SE record size 0x80 vs 0x90.** All 79 `.se` entries decode to 144 bytes,
    the loaders keep the whole entry, and `ent_unit_init_personnel` copies
    SE+0x74..+0x8F (28 bytes). **Decision: 0x90.**
12. **SE +0x0C last name** char[20] (365e_a) vs char[14] (365e_b): the field
    runs to the nickname at +0x20. **Decision: char[20]** (14 is only the
    recruit-screen display limit of the nickname).
13. **SE camouflage +0x60 (365e_a) vs +0x62 (365e_b).** `ui_draw_bio_panel`
    reads SE+0x62 and indexes the camouflage names DS:2536 (365e:8884); no code
    reads +0x60. 365e_a's "+0x62 copied to loadout byte 1" and 2dbd's "+0x62
    headgear style" are the same byte. **Decision: +0x62 camouflage, +0x60
    unknown.**
14. **Loadout table base.** 365e_a: records at 5178:0000, 0xFF at +0x30;
    365e_b: Point Man at +0x0C .. Rear at +0x30. `loadout_build_team` writes
    the terminator at 12*4 = 0x30 (365e:45EA); `bull_next_line` reads 0x18 +
    12p; `dbrf_say_casualties` walks 0x0C..0x2F. **Decision: 365e_a**; so the
    bull session alternates the Corpsman and Rear Security, and the debriefing
    casualty list covers OIC, Corpsman and Rear Security (not the Point Man).
15. **Weapon table.** 19ac: 26 entries; 365e_a/1000: 34. Dump of DS:48FE: 34
    valid entries (0..33, ending with Mk I illumination). teams_ai and seg_libs
    used the base DS:4902, so their offsets are +4 short (class +0x04, magazine
    +0x0E, fire modes +0x10, max range +0x0C, jam +0x1C, reload ticks +0x1E).
    Field meanings confirmed by the values (M16A2: 180/480/2400, 30 rounds,
    modes 7, 7.6 lb, 1.1 lb per magazine, jam 3, 0x400 ticks, 8 reloads).
16. **Item/tool table.** 5 (19ac), 6 (365e_a) or 10 (1000) entries: far 52E3
    holds 12 (… 10 Rice, 11 Booby trap kit; NPC SE item lists use 8 and 11).
17. **Weapon list header +0x08.** "tool" (seg_1000 §17) vs second
    weapon/grenade: it points to a weapon node (the HUD shows rounds +
    reloads; `wpn_select_next_grenade` moves it). **Decision: secondary
    weapon.**
18. **Target record vs contact slot.** Documented separately (seg_1000,
    teams_ai, seg_libs) but `tgt_acquire` and `ai_mind_store_contact` use the
    same 0x1E-byte layout. +0x12 "kind" vs "keep flag": the AI stores the same
    kind (synthetic contacts 0). +0x14 "target object": `ai_mind_expire_contacts`
    copies a position through it — a pointer to the live position. +0x1C
    "aim spread" (teams_ai) vs "obstruction" (seg_libs): `tgt_acquire` stores
    `wld_los_cover` there and `evt_execute_ai_commands` passes it as
    `shot_fire`'s fourth argument (shot +0x2A, the 19ac "caller value"), which
    lowers the hit chance. **Decision: one `TargetRec`; +0x1C = LOS cover.**
19. **Brain flags.** 0x10 "ignore team commands" (teams_ai) vs "suppressed"
    (seg_libs, 1000): set by `dmg_apply` with the timer +0xCE, cleared by
    `ai_group_tick_suppression`; `ai_member_current_command` skips team
    commands while it is set. 0x40 "excluded" vs "script": set by
    `ai_member_start_script`, cleared when the script ends. **Decision:
    suppressed, script running.**
20. **MTM +0x22.** "alertness class 1..3" (365e_a) vs order type (seg_libs).
    `ai_group_orders_from_mission` copies it to TeamAi+0x82 (1 builds the patrol
    waypoints, 3 draws the deployment delay); `ent_spawn_mtm_team`'s special
    case for 3 (crouched/prone and hidden via `unit_hide_chain`) is the reserve
    group waiting for deployment. Data: 281 x 1, 214 x 2, 166 x 3. **Decision:
    order type (1 patrol, 2 hold, 3 reserve).**
21. **Projectile +0x06 / +0x14.** The "flight record" (+0 speed, +0x18
    downward speed, +0x20 bounces, +0x22 gravity) is a Mover-sized block
    (`fx_alloc` bit 1, 0x48 bytes) filled by `evt_projectile_init_motion` in the
    Mover pattern; kept as its own struct `FlightRec`. +0x14 "speed" (1000) vs
    "muzzle/hand phase" (craft_ordnance): same values; the ordnance update uses
    it as the duration of the muzzle phase. **Decision: `muzzle_phase`.**
22. **World object kinds 0x0A/0x0B.** seg_1000 and the symbol
    `wld_open_hut_ahead` say huts/bunkers; the movement notes say trip wire /
    pit. The model table names are `trip` (kind 10) and `pit`, `stakes`
    (kind 11); huts/bunkers are kind 0. `wld_open_hut_ahead` (1000:6163) is the
    'x' "expose trap" action: it sets the cover of a trap within 10° and 0x79
    units to 100 so `tgt_acquire` (which only takes kinds 0x0A/0x0B within
    0x79) can target it. Kind 5 ("invisible blockers") are clearings, paths,
    bridges and cemeteries (no ground cover is placed there); 3 = vegetation,
    6 = trees, 0x12 = tunnel. **Decision: `TerrainKind` from the model table.**
23. **Message kind 4.** "hand-signal picture" (seg_1000) — it draws a frame of
    the sprite set at 53BA:2140, which is the medal ribbon set `mdl`
    (365e_b); `cmpscr_msg_awards` queues kind 4 with style = award bit.
    **Decision: medal picture.**
24. **Model descriptor +0x0C** "scale shift" (2255) vs "flags" (seg_libs): the
    low byte is the signed scale shift (0xFF = −1 on reeds, tower); the high
    byte (0x1F on houses) is never read. The LOS "flags & 8" test is on the
    record linked by object flag 0x0800, not on the descriptor. seg_libs'
    collision box (+0x30..+0x47) and heightmap pointer (+0x48) extend the
    2255 table.
25. **Status +0x1A** "(>= 5 wound handler ignored)" (2dbd) vs "class" (19ac):
    `unit_init_body` stores the unit class there. **Decision: unit class**
    (civilians 8 and friendlies 9 are therefore immune to wound effects too).
26. **Group record size and `ent2_alloc`.** 365e_a listed fields up to +0x32;
    seg_libs: 0x34-byte block with the 0x8E-byte AI record at +0x28 — consistent;
    the AI record layout comes from `ai_group_orders_reset` /
    `ai_group_orders_from_mission`.

## Remaining unknowns

* Unit +0x22 (never written).
* Status +0x0D, +0x10 (cleared at spawn), +0x15; the body locations of the
  individual hit bits beyond legs (0x10..0x80) and arms (0x800/0x1000).
* Mover +0x06 (−12/−0x20/0x18, maybe a minimum speed), +0x10 (0x168), +0x12,
  +0x16; mover flag 0x02 is tested but never set.
* Anim +0x02..+0x05, +0x14..+0x17; no reader of +0x0A found.
* Command +0x02/+0x03, +0x11; Brain +0x67, +0x6D; TeamAi +0x80, +0x8D and the
  high word of the think timer; the meaning of behaviour 0x30.
* WeaponNode +0x09, +0x0A (init 3), +0x0D.
* Projectile +0x3A..+0x3F; FlightRec +0x06, +0x0A, +0x0E, +0x12, +0x16, +0x1A
  (9) and +0x1C (probably a launch pitch).
* ShotRec +0x20 (always 0), +0x21.
* ModelDesc +0x01, +0x0D, +0x20, +0x2E (game flags 0x200/0x204/0x210/0x214/
  0x304/0x1110 — not read by the renderer; readers elsewhere not yet found).
* SE +0x60 (never read), +0x73; MTM +0x26 (no reader).
* RosterEntry +0x06, +0x13; s.cnf +0x00..0x02 and +0xDC..0xE7; `s.rst` has no
  reader; flow-table flag bit 1.
* `.w` bytes +0x01 and +0x0B; `.wd` byte +0x00 and the text after the area
  name.
* Craft orders 0 and 1 in mission code (treated like 2 and 3 by the craft
  update; 0 is only used by the camp scenes).
* Message style 6 (special text renderer 365e:93BC) — which callers use it.
* 5149:1E6E (group segment list count) and the rest of the 5149 block.
