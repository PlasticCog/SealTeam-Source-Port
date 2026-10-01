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
         [--script idle|walk] [--log FILE] [--quiet-snapshots]
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
presentation. Every rule applies to the SEAL team only; enemy units, craft
and NPCs keep the original tables. With the option off nothing changes: the
`--sim-mission` logs of the Original and of the Enhanced preset are
byte-identical to those of the build without the code.

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
