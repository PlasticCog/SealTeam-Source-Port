# Mission simulation

`src/game/mission/` ports everything that happens inside a mission except
drawing and the mission screen / HUD loop: world construction from the
mission files, units and teams, movement and terrain, casualties and wounds,
the animation state machine, group and soldier AI, AI command execution,
shots and damage, ordnance, noise events, objectives, support craft,
extraction, the sound-effect layer and the statistics the debriefing reads.
The original code lives in segments 2dbd, 348e, 365e (0000-29C5), 4a37,
4ff8, 4592 and parts of 1000 and 19ac; the per-segment notes are in
`docs/re/`.

## Architecture

| File | Original | Contents |
|---|---|---|
| `state.h/.cpp` | DGROUP globals, far 5149/525B/53BA blocks | `MissionState ms()`: every shared global of the mission code (with its DS offset), record pools, `resetMission()` |
| `exedata.h/.cpp` | DS:48FE, 52E3, 525C, 525F, 5327, 52E7 | weapon/item/wound/noise/trig tables read from st.exe; `dsText()` |
| `geo.h/.cpp` | 2255:4481..6368, 4e87:0006, 1000:321F..3C84 | bit-exact fixed-point math: sin/cos tables, atan2, heading/pitch, rotation, polar move, distance/bearing/field of view |
| `world.h/.cpp` | 2255:0234, 1000:3EFD, 1000:4291 | the simulation's view of the renderer's world (`render/world.h`): models, object arena, world objects, effect pools, body -> unit map |
| `los.h/.cpp` | 4ff8 | quadtrees and segment tests (line of sight, collision probes) |
| `wquery.h/.cpp` | 1000:314C..3E85, 6648..695A | ray/probe queries, terrain kind probes, cover sums, nearest objects |
| `msg.h/.cpp` | 1000:7B06..840B, 672A | message queue logic, `msgShow`, hand signals, speech, `radioCall`, time-compression switch |
| `sfx.h/.cpp` + `engine/sound.*` | 4592 | sound-effect channels, priorities, positional volume, digital vs FM playback |
| `entity.h/.cpp` | 4511, 365e:000C..05A3, 19ac:612A..642C | allocation, team/unit construction, weapon/item nodes, NPC personnel, unit/team helpers |
| `build.h/.cpp` | 365e:0693..29C5, 1000:68E2, 19ac:2B60 | mission loading, world construction, run-time team helpers, statistics, objectives |
| `people.h` + `movement.cpp`, `casualty.cpp`, `anim.cpp` | 2dbd parts 1/3, 348e, 1000:5985/5D1D | movement, posture, speed, fatigue, terrain and booby traps, casualty handlers, animation state machine |
| `teams.h/.cpp` | 2dbd part 2 | search, prisoners, buddies, formations, split teams, speed steps, AI command execution |
| `combat.h` + `combat.cpp`, `combat_player.cpp` | 19ac:0934..17A0, 6CCA..89C3, 1000 dmg/tgt/inventory | weapons, shots, hit rolls, damage, wounds, medical aid, targeting, player fire/grenades |
| `ai.h` + `ai.cpp`, `noise.cpp` | 4a37, 1000:841A..892E | group/soldier AI, noise events |
| `craft.h` + `craft.cpp`, `ordnance.cpp` | 2dbd part 4, 1000:45B9..60FA | support craft, extraction pickup, engine sounds, projectiles and ordnance flight, ambient flyers |
| `sim.h/.cpp` | 1000:01DE, 2dbd:3F4C, 1000:0028..0842, 1000:1964..19EE, 19ac:0CC3 | set-up, frame schedule, mission tick, insertion/extraction, music mood, alerts, time of day, order keys |
| `orders.cpp` | 19ac:52A1, 19ac:5C8E | map orders (waypoints, craft attack/cease/loiter/extract/emergency), re-insertion |
| `camp.cpp` | 19ac:64D4..6B07 | world alone for the briefing/debriefing, SEAL camp cut-scene worlds |
| `campaign_link.h/.cpp` | 19ac:00B6, 365e:1D1F | mission set-up from the campaign state, hand-over of the results |
| `modern.h/.cpp` | — | the Enhanced "Modern gameplay" rules that change the simulation, and its one presentation rule, the marked snatch target (below); no-ops while the option is off |
| `sim_cmd.cpp` | — | `--sim-mission` developer command |

Records are the canonical structs of `game/types.h`. They are allocated from
`MissionState` pools with stable addresses and released all at once by
`resetMission()` (the original freed merged teams and waypoint lists early;
nothing references them afterwards, so this is equivalent).

Object ownership: every 3D object (unit bodies, world objects, projectiles,
effects) lives in the renderer's arena (`render::worldAddObject`);
`mission/world.cpp` adds the lists the LOS module needs and the maps
body -> world object / unit (the original's `grp_from_object_id` arithmetic).

Random numbers come only from `engine::rng()` in the original call order.
Everything that draws random numbers outside the simulation (the renderer's
scenery `veg_update`, the grass tufts of the soldier sprites, the camera of
views 5/6) must be called by the loop exactly where the original does; the
simulation offers `setVegetationHook` for the calls it makes itself.

## API for the mission loop / HUD phase

`sim.h` exposes the steps of `mis_run` (1000:01DE) so the loop can keep the
original order:

```
setSimHooks(hooks);                  // view mode changes, clock resync, full redraw,
                                     // campaign: addCampaignHooks(hooks)
simInit(missionSetupFromCampaign()); // mis_run set-up up to the frame loop
per frame:
    render: view_update_camera, veg_update(0), view_render_frame,
            engine::sound().setListener(pos); engine::sound().updateSfx()
    simFrameClock(ticker time)       // or simFrameClockCompressed() in time compression
    simFrameAnimate()                // tod_update + spr_advance_anim_clocks (not on the frozen map)
    input: cmd_order_keys -> playerOrderKey(key), field views -> playerViewPrologue(dx),
           playerFire(), playerThrowGrenade(), playerSetPosture(), playerTurn(),
           evtPlayerSpeedStep(), itemUseTool(), itemCycleTool(), wpnSelectNextWeapon(),
           wpnSelectNextGrenade(), wldOpenHutAhead(), tgtAcquire(), map orders ...
           insertion view 7: misInsertionClear() on Enter/Esc or after insertionClearTime
    fade = simFrameUpdate()          // periodic updates + evt_mission_tick
    if (fade >= 0) paletteFade().request(fade, dt)
    simMsgTick()
until ms().misDone || ms().quitGame
simShutdown()
```

HUD and debriefing read `ms()` directly: `playerTarget` (5149 target record),
weapon/grenade/tool timers, `demoExplodeTime`, `messages()` (queue to draw),
teams and units, `stats`, `enemyKia/sealKia/sealWia/missionMinutes`,
`objectiveComplete(i)`, `unitFromBody(obj)` for the sprite callback.

The loop must also provide (each of them feeds the original's RNG order or a
game rule, so it must be called where the original does):

* `setVegetationHook(fn)`: `veg_update(force)` of the renderer's scenery;
  `setVegetationResetHook(fn)`: `veg_reset` (run once by `simInit`). The
  simulation calls `veg_update(1)` before every view-mode store, exactly
  like view_set_mode / view_set_first_person / view_set_chase.
* `setGroundFxCameraHook(fn)` (people.h): the current camera position
  (`g_cur_camera`, DS:D8AE); `fx_unit_ground_fx` draws `rng(8)` / `rng(15)`
  for every unit within 0x4B0 of it. Default: the Point Man.
* `craftSetViewerPos(&camera.pos)` (craft.h): the ambient flyers disappear by
  distance from the camera. Default: the Point Man.
* `view_set_eye_height` (1000:2462) after the movement update: the
  simulation only writes the Point Man's mover height fields.
* `engine::sound().setListener()` / `updateSfx()` once per rendered frame.
* The renderer spawns the distant trees / ambient flyers into
  `fxPools().trees` (1000:770B); the simulation only updates enabled ones.
* `playerViewPrologue(dx)` every frame in the field views, before the key;
  then Enter `playerFire()`, 'g' `playerThrowGrenade()`, pointer/arrows while
  `ms().grenadeAiming` `playerAdjustGrenadeAim()` else `unitTurn()`, '['/']'
  `itemUseTool`/`itemCycleTool` (+ toolInfoTime), 'n' / Alt-N weapon and
  grenade selection followed by `tgtAcquire(pm, playerTarget, false)`, Tab
  `tgtAcquire(pm, rec, kind != None)`, 'x' `wldOpenHutAhead` + hand signal 3.
* `setViewHeadingTurnHook(fn)` (combat.h): `unit_turn` also turns the view
  heading `g_view_heading` (DS:CEAC), which belongs to the loop.
* After `reinsertConfirm()` the game clock restarts at 0 (clk_reset).

`engine/sound.h` now has the effect layer: `loadSfxBank`, `playSfx`,
`updateSfx` (snd_update, once per rendered frame), `setListener`
(snd_set_listener_pos, from the camera update), `stopAllSfx`,
`setSfxEnabled` (Alt-S). The music device, music/effects volume and the
digital-effects switch of the port settings are honoured there
(SAMPLE.AD for AdLib/OPL2, SAMPLE.OPL for SB Pro 2/OPL3).

## Test entry point

```
sealteam --sim-mission <1..80> [--ticks N] [--seed-skip K] [--dt D]
         [--script idle|walk|hold|attack] [--craft b|u|a] [--summary]
         [--log FILE] [--quiet-snapshots]
```

Builds the mission headlessly (no audio output) with a test team (SE 1, 10,
8, 2 with a simplified marching-order loadout) and runs the simulation for N
ticks at D ticks per frame (default 5 = the 51.2 fps frame limiter). The
`walk` script turns the Point Man toward the first objective and runs. The
log lists the spawned teams and then every observed change: messages, hand
signals, sound effects, team orders, AI flags, visibility, postures, wounds,
deaths, shots and their resolution, objectives, and the end of the mission.
`--seed-skip K` advances the game RNG K steps first. Two runs with the same
arguments produce identical logs.

Two scripts exercise the combat rules for longer: `hold` runs the Point Man
at the first enemy (VC / NVA) team of the mission table whose leader is
visible, stops about 250 units from it and holds there facing it, so that
the squad and the enemy exchange fire for the rest of the run; `attack` does
the same and, once the insertion is over (about tick 2500), gives the map's
Attack order of the first support craft (`--craft b` boat, the default, `u`
helicopter, `a` aircraft; `mapOrderKey`) at the position of the nearest enemy
team, logging the order and its result. With these two scripts (or
`--summary` with any script) the log gains a `hit:` line for every wound a
shot inflicts, naming the shooter and his team type, and a three-line
`summary:` at the end: enemy shots by weapon class (bullets, thrown, other
explosives), support-craft shots and those held back near friendlies, SEAL
wounds and deaths by the shooter's team type, enemy grenade throws held by
the discipline rule, and the number of unit bounces off obstacles and of
detours started. The `walk` and `idle` logs are unchanged by these
additions, so they remain comparable across builds.

## Deviations

* The sound effect channels keep the original bookkeeping (6 channels, one
  digital voice, lifetimes); samples are played whole through `st::audio`
  instead of being streamed in 3600-byte chunks (only the first VOC block was
  streamed by AIL; all shipped effects are single-block).
* `resetMission()` frees everything at once (see above).
* Where the original reads through NULL or uninitialised memory, the port
  guards the access and keeps the documented effect where it is known
  (marked `Original bug` in the code): NULL units/movers/brains/loadouts,
  the Point Man's missing brain in `ai_group_has_move_orders` (read as flags
  0; the original reads the interrupt vector table), craft without an
  animation block in prj_fire (muzzle posture entry 2 used), the wake heading
  of fx_unit_ground_fx (1000:5EEC, read with the wrong segment: the port uses
  the object heading + 180 degrees; visual only), contact slots without a
  target, out-of-range objective indices, the medic patient array (only
  slots 0..3 written).
* `Anim::sets` (sprite set pointer) is left NULL: sprite sets belong to the
  renderer.
* The four unreferenced 4a37 functions (0248, 110E, 1242, 1435) are not
  ported.

### Modern gameplay

The Enhanced option "Modern gameplay" (`settings().effectiveModernGameplay()`,
cfg `modern_gameplay`, Setup "Modern gameplay"; always off with the Original
preset) is the one Enhanced option that changes gameplay rules, not the
presentation. The loadout rules apply to the SEAL team only; the three
simulation rules (support craft, obstacle detours, enemy grenades) change
what the AI of craft, squad mates and enemy soldiers does, as described
below, and one presentation rule under the same option marks the snatch
target on screen; all other tables and code paths stay the original's. With the option
off nothing changes: the `--sim-mission` logs of the Original and of the
Enhanced preset are byte-identical to those of the build without the code,
and none of the new code touches `engine::rng()` in either state (every
choice is made from the unit's own state), so enabling the option changes
only what it is meant to change.

* **CAR-15 Commando ammunition like the M16's.** The weapon table (DS:48FE)
  gives both rifles 8 magazines, but the Commando's hold 20 rounds against
  the M16's 30 (160 against 240 rounds in the briefing's count). A SEAL's
  Commando now gets as many 20-round magazines as the M16's rounds fill,
  8 x 30 / 20 = 12 (240 rounds), computed from the table by
  `campaign::sealWeaponReloads` (campaign.cpp). That function replaces
  `weaponReloadsByte` wherever a SEAL loadout slot gets its default or its
  maximum: `loadoutBuildMember`, the briefing's loadout editor (the default
  after a weapon change and the cap of '+') and the `--sim-mission` test
  team. `ent_spawn_seal_team` is unchanged: it copies the record. The load is
  still computed from the magazines (`loadoutComputeWeight`, 1.1 lb each), so
  the four extra magazines add 4.4 lb, as in reality.
* **M79 grenadier carries a full vest of 40 mm.** The table limits the M79
  to 5 rounds; the option gives 20 (the same function; +15 x 1.3 lb of load).
  The M203 is not changed: it is not SEAL-selectable (availability 0) and its
  grenades come three with every rifle magazine (`m203_count`), not from a
  separate round count.
* The map's Team Info table (`map.cpp`) has room for one digit of magazines
  at x 316; with the option on a two-digit count is drawn right-aligned to
  the same edge (x 312) instead of being clipped.

Verification: `--original --sim-mission 1 --ticks 30000 --script walk` and
the same with `--enhanced` and `modern_gameplay = 0` give logs identical to
the build without the option, with `--original --view-world 3`
pixel-identical; with the option on the only difference in the mission-1 log
is the spawn line, `M79(1+20)` instead of `M79(1+5)` (the test team has no
Commando); `--play-mission` logs the team's loadout (`CAR15 x12` with the
option, `x8` without), the first-person weapon line and the map's Team Info
show the magazines, and the Setup page fits eight rows per column with the
new entry.

The three simulation rules live in `modern.h/.cpp`; their per-unit state is
the side table `ms().portUnits` (released with the mission) and the
`--sim-mission` summary reads the counters `ms().portStats`. The bounce
counter is kept with the option off too (statistics only).

* **Support craft hold fire near friendlies.** Mechanism (original bug):
  `ai_group_support_fire` (ai.cpp `aiGroupSupportFire`) gives a craft
  member without a real contact a 50 % synthetic contact at the aim point
  (the leader's destination, the map's support waypoint) whose `target` is
  the Point Man, team 0 member 0; `evt_execute_ai_commands` (teams.cpp) then
  fires with target kind 0 and `shot_aimed_hit_roll` (combat.cpp) resolves
  aimed bullets against that target unit wherever he is, so the Point Man
  can be hit from across the map; blast weapons (`shot_blast_victims`:
  Minigun radius 270, rocket 48, M79 and the boat's grenades) hit anything
  within the radius of the scattered aim point, and borrowed contacts keep
  the old `target`, so the Point Man stays the target later. With the option
  on, in the craft branch of `evtExecuteAiCommands` before `shotFire`: a
  target that is a SEAL is replaced by `nullptr` with target kind 3 (aim
  point), so bullets resolve through the line / cone victims
  (`shot_line_victims`) like the player's untargeted shots; and the shot is
  held (`modernCraftHoldsFire`, counter `craftHolds`) when a living member
  of a SEAL or Friendly team who is not aboard the craft lies within
  blast radius + 60 units of the target or within 90 units of the line of
  fire craft -> target (perpendicular distance from `geoDistance`,
  `geoBearing` and the 1.14 sine table; units behind the craft or beyond the
  target are not on the line). Verification (`--script attack`, 60000 ticks,
  the boat ordered to attack the nearest VC team at about tick 2500, option
  off = `--original`, on = `--enhanced` with `modern_gameplay = 1`): mission
  1, off: 4 boat shots, the boat wounds and kills one SEAL; on: 11 boat
  shots, no SEAL hit by the boat (the squad's 1 KIA is by the VC). Mission 5,
  off: 5 boat shots, 2 SEAL wounds and 1 KIA by the boat; on: 22 boat
  shots, none. Mission 21 `--script hold` (helicopters only): on, 3 of 8
  craft shots held near friendlies. In every run the shots held or redirected
  are the ones the summary attributes to the craft; enemy shots and SEAL
  casualties by the enemy vary because the runs diverge.
* **Squad mates work around obstacles ("unstick").** Mechanism (original):
  a foot unit whose 6-unit probe (`wld_probe_solid`, a short ray to +x +z)
  hits a solid gets `impact_bearing` (bearing to the object's centre), the
  blocked flag and `evt_unit_bounce_off_obstacle` (position reset to the
  probe's hit point, pushed 4 units back, desired heading = impact +-100
  degrees when within 100 degrees); the flag is cleared only on the 0x500
  fatigue tick when the unit is off the solid; `evt_team_formation_update`
  then steers straight back at the formation slot through the same obstacle,
  `evt_split_teams_update` rewrites a split-team leader's heading toward the
  waypoint / structure regardless of the flag, and a unit whose heading leads
  into the object's outline is reset to the same spot every few frames
  ("pinned") because the 4-unit push does not clear the probe's reach. With
  the option on (`modernNoteBounce`, called after the bounce in
  `moveOtherUnit`, SEAL teams only): three bounces against the same world
  object within 0x500 ticks of each other start a detour, desired heading =
  impact bearing +-90 degrees on the side nearer the unit's destination
  (its formation slot; ties go right); `modernDetourUpdate`, run every frame
  before the unit turns, keeps that heading until the unit is radius + 20
  units from the object (`objRadiusOrDefault`) or 0x300 ticks passed; three
  bounces at the same spot (within 1 unit, with the unit already on the
  detour heading) show the heading leads into the outline and turn it 90
  degrees to the side farther from the impact bearing, restarting the
  timer. While a detour is active the formation update treats the unit like
  a blocked one (no heading rewrite, mode left alone, no facing of the aim
  heading when stopped) and the two heading rewrites of the split-team
  update (toward the waypoint, toward the demolition structure) are
  skipped. `evtUnitBounceOffObstacle` and the flag clearing are unchanged.
  Verification: in the mission-1 walk run with the option on the squad's
  three members hit the same palm (kind 6, "radius" 2, outline about 14
  units) one after the other at (12540,13589); without the turn-away rule
  (first implementation) T0.3 sat there for 10000 ticks bouncing every 7
  frames with 18 detours started, with it the run shows 2 detours (T0.1
  released at radius + 20 after 310 ticks, T0.2 turned south after 125
  ticks and was released 185 ticks later) and the squad moved on. Over
  missions 1-20 (walk, 30000 ticks) and ten hold runs the SEAL squad is
  rarely stuck in these scripts (detours: mission 1 walk 2, missions 8 and
  12 hold 1 each); the large bounce counts of some runs (mission 3 hold
  16425, mission 5 attack 946, mission 9 walk 1039) are enemy or civilian
  units ping-ponging against an obstacle, which this rule leaves alone.
* **Enemy grenade discipline.** Mechanism (original): enemies whose SE
  loadout lists a grenade first have it as primary and `ai_group_combat`
  throws one per AI decision (every 0x140 ticks) at any contact within the
  grenade's maximum range (600-720 units), because the range check of the
  primary is the full range (`wpn_in_range`) and `wpn_select_for_range`
  skips thrown items; and when a rifle runs dry with Ammo = Real,
  `wpn_select_longest` picks the longest-range item including grenades and
  the unit lobs one every 1.25 s until they are gone. The secondary (grenade)
  roll is only 40 % / 65 % and only when the maximum range is at least four
  times the distance (150-180 units). With the option on
  (`modernGrenadeAllowed`, in `aiGroupCombat` after the weapon choice and
  before the fire command is pushed): a thrown item chosen as the primary is
  thrown only when four times the distance is within its maximum range (the
  secondary roll's rule) and 0x400 ticks (4 s) passed since the unit's last
  throw (recorded for every throw, including the secondary roll's); otherwise
  the shot is skipped (counter `grenadeHolds`) and the unit keeps the turn it
  made and its posture. `wpnSelectLongest` skips thrown items
  (`modernSkipThrownForLongest`), so a dry rifleman without magazines falls
  through to the original no-ammo rules: with the secondary also empty he
  flees when more than 210 units from the player, else surrenders; with only
  the primary dry (mask 1) he flees. Verification (`--script hold`, 60000
  ticks, off vs on): mission 21 thrown 3 -> 1 (47 held), mission 23 2 -> 0
  (71 held), mission 24 14 -> 4 (36 held, SEAL KIA by the VC 3 -> 1);
  `--script walk` 30000 ticks: mission 10 thrown 8 -> 0 (107 held, SEAL KIA
  3 -> 0), mission 20 9 -> 0 (33 held), mission 14 3 -> 1, missions 7, 18
  and 19 1 -> 0. Missions 33 and 1-6 hold runs engage no grenade-first unit
  and are identical with the option on and off.
* **The snatch target is marked** (presentation only; nothing here touches
  the simulation or `engine::rng()`). Mechanism (original): a Snatch
  objective (`ObjectiveKind::Snatch`, MCI `target_team`) is complete when a
  member of MTM team `target_team` is secured with the team's leader within
  3000 units of the Point Man (`msn_check_objective`), but nothing tells
  the official, courier or tax collector of the briefing from the other
  enemies of the area: the only hint is the HUD's " Objective" line under
  the name of a targeted member of any objective team. With the option on,
  `modernIsSnatchTarget(u)` (modern.cpp) is true for the leader
  (`members[0]`, the unit the objective's distance test and the mission
  camera refer to) of the target team (VC, NVA or Civilian) of every Snatch
  objective; the targets are looked up once from `ms().mci.objective` on
  the first query after the MTM teams exist and cached in
  `ms().snatchTargets` (released with the mission; the camp scenes have no
  MTM teams, `firstMtmGroup` 0xFF, and never match). Three readers, each the
  original drawing when the function is false:
  * the sprite renderer (`render::setUnitMark`, installed by
    `viewInstallRenderHooks`): `drawSoldierFrame` (sprites.cpp) draws the
    marked unit's headgear - the VC conical hat, the NVA pith helmet -
    through a red remap built from the current base palette like the impact
    remaps (luminance -> red, nearest palette entry, 0 and 255 pinned,
    rebuilt when the palette changes), and `drawMarkBand` draws the body
    frame once more through the same remap with the clip box reduced to a
    band of rows at the bottom edge of the headgear frame (placed as
    `drawHelmet` places it; a fifth of the body down without a headgear
    frame), two sprite pixels scaled with the sprite and never thinner than
    one page pixel (`frameScale()`): the "red scarf" is the body's own
    pixels, so the silhouette is the original's and the marker shows at any
    distance the sprite is drawn. The remap set by the anim kind, if any, is
    restored around the headgear.
  * the map screen (`drawSnatchMark`, map.cpp): a one-pixel light red
    (0x0C) ring three pixels outside the unit's disc and a "!" to its
    right, drawn only after the original has drawn the disc (his team
    selected in the team list or he is the player's target), so the map
    never reveals him earlier than the original does.
  * the HUD target name (`hudDrawTargetInfo`): " (target)" is appended to
    the team-type name when the player's target is the snatch target
    ("Viet Cong (target)", 72 pixels; the line keeps the original's
    position test and is clipped to the view).

  Securing any member of the target team still completes the objective, as
  in the original (mission 74's team has two members, the hamlet chief and
  his escort; the leader is the marked one). Snatch missions: 11, 35, 36,
  44, 51, 53, 59, 67, 70, 74 and 80 (`--sim-mission N --ticks 10 --log`
  lists the objectives, kind 6). Verification (mission 36, "VC procurement
  officer at the market", a one-man VC team 1400 units from the insertion
  point): `--enhanced --window 1920x1080 --play-mission 36 --keys
  "Enter@mission+1,F10@mission+4" --shot ... --shot-after 14` with
  `modern_gameplay = 1` shows the officer through the enemy camera with a
  red conical hat and a red band at the neck, the civilians beside him in
  their own hats; the same run with `modern_gameplay = 0` shows him in the
  tan hat without a band. With `F1`, four `Up` presses and `Tab` at
  mission + 50 s the Point Man reaches the market and acquires him: the HUD
  reads "Viet Cong (target)" over " Objective", and `m` then shows his map
  mark ringed with the "!" beside it.
* **No ground cover inside buildings.** Mechanism (original):
  `veg_place_ground_cover` (1000:7181, render/veg.cpp `placeGroundCover`)
  probes `wld_probe_kind(cell, 5)` at the corner of a 256-unit cell and only
  then places the plant at `cell + (rand(0x80) << 8)` in x and z, so a bush
  can stand inside a hut and, the billboards being painted by object depth,
  is drawn through its wall; as a world object it also gives line-of-sight
  cover (0x19..0x64) to anyone inside. With the option on the final spot is
  probed too (after the original's draws from the RNG, so the sequence is
  unchanged) and a plant on a blocker is kept hidden like one whose cell
  corner is blocked (the end of the function then hides and zeroes it).
  Verified: `--original --sim-mission 1 --ticks 30000 --script walk --log`
  byte-identical with and without the code.

Verification of the identity: `--original --sim-mission N --ticks 60000
--script walk --log` for N = 1, 21, 33 is byte-identical between the build
without this code and with it, and so is `--enhanced` with a cfg
`modern_gameplay = 0` (also identical to the `--original` logs);
`--original --view-world 3 --shot` is bit-identical; `--enhanced --window
1920x1080 --play-mission 1 --keys "Enter@mission+1,F1@mission+2.5" --shot
... --shot-after 8` runs normally with the option on (`CAR15 x12` in the
loadout line). For the marked snatch target: `--original --sim-mission N
--ticks 30000 --script walk --log` for N = 1 and 36 is byte-identical
between the build without the code and with it, and so are the `--original
--view-world 3` and `--view-model 51` (a soldier sprite) shots.

## Corrections to the RE notes found while porting

* Bled to death (2dbd:0A9D) does not update DS:0898 and gives no under-fire
  signal.
* Obstacle bounce (2dbd:1B05) turns to impact + 100 degrees unless the
  heading is more than 100 degrees from it, then impact - 100.
* Pass 2 of the movement plays the brush sound only while moving; a tripped
  wire also sets the blocked flag (which lets the Point Man's heading turn).
* Wounded units outside team 0 get a helper only in split SEAL teams.
* Ambient flyers are reset to altitude 0x300 only when they reach 0 or less.
* prj_deactivate (1000:5068) hides only the muzzle flash and the explosion;
  the effect pointers stay set.
* prj_fire's start altitude replaces the shooter's altitude; thrown items
  start 0x300 to the side (heading + 90 degrees).
* ent_team_rejoin (365e:17B1) resets the map selection to team 0 when the
  selected team is a SEAL team (not when it is not).
* msn_check_objective (365e:251D): Rescue and Snatch also require the target
  team's leader within 3000 units; Observe on a non-structure target counts
  dead VC + NVA > 4 only when the target object's kind is not 0.
* camp_build_teams (19ac:663A) spawns the missing emergency helicopter with
  ent_spawn_insertion_craft(8, ...), and gives the break-contact craft's
  status call to the previously created craft group.
* The world description file is `<world>.w` + `d` (DS:33BA ".w", DS:33BD "d").
* Wound tables: each of the five 525F tables ends with its own zero entry, so
  the scan never runs into the next table.
* shot_aimed_hit_roll's +30/+10 bonus uses the shooter's size (Status +8);
  med_leg_level's ">= 2" test is on the light wounds; shot_line_victims
  measures distances from the aim point.
* los_test_object (4ff8:098E): for heading 2160 with flag 0x2000 the inverse
  rotation uses the hit point's model-space z as angle, not 2160 (so hit
  points are not consistent); los.cpp reproduces it, including the sine
  table reads outside the table for large river models. A hit container
  whose children miss lets the walk continue.

## Verification

* `los.cpp` was compared with the original 4ff8/2255 machine code run in a
  16-bit x86 emulator (re/scratch_mission/los/emu.py, not part of the
  repository): about 40000 random queries and the built trees matched.
* All 80 missions run 30000 ticks with the walk script without errors, and
  two runs give identical logs. Typical mission-1 log: VC patrol sighted at
  about 730 units, return fire, suppression, a reserve team deployed after
  90 s, wounds and bleeding, a kill, a surrender; missions with a dead Point
  Man show the emergency extraction and the "Extraction" end sequence.

## Open issues

* The mission loop / HUD is the next phase: views, cameras, key handlers,
  map screen drawing, `view_set_eye_height`, the scenery hook.
* Review coverage: geo, wquery, msg, entity, build, sim and orders were
  written against the asm but have not had an independent second review.
* The debriefing score (campaign::awardEvaluateMission) is fed through
  campaign_link.cpp; roster casualties are recorded there, not in
  msn_tally_casualties itself.
