# The mission loop, cameras, HUD and map screen

`src/game/mission/loop*.cpp`, `view.cpp`, `hud.cpp`, `map.cpp` and
`src/game/front/hooks.cpp` port the part of the original that runs while a
mission is played and was not covered by the simulation (`docs/mission.md`)
or the renderer (`docs/render.md`): `mis_run` (1000:01DE), the cameras and
view modes, the frame render driver, the HUD, the message drawing, the map
screen / control panel, the mission key handlers, time compression and the
insertion / re-insertion presentation. The same code draws the 3D views of
the front end (briefing and debriefing fly-overs, the SEAL camp cut-scenes).
Reverse-engineering notes: `docs/re/seg_1000.md` 6-10, `docs/re/seg_19ac.md`
4-9, `docs/re/seg_2dbd.md` 4.

## Files

| File | Original | Contents |
|---|---|---|
| `loop.h` | DGROUP globals of 1000/19ac | `LoopState ls()`: cameras, orbit state, view team, redraw flags, map cursor mode, the clock base (every member names its DS offset); the internal API of the loop modules |
| `loop.cpp` | 1000:01DE mis_run, 0790 mis_cleanup, 2FEA..314B clock | `mission::run()` / `unload()`, hook installation, the frame loop with the time-compression states, `clk_*` |
| `loop_input.cpp` | 19ac:0B2A, 0CA7, 0CC3, 1816, 1A79 | input_toggle_keys, ui_resume_after_overlay, cmd_order_keys (the loop's keys; the order letters and Alt-T/U are `playerOrderKey`), field_pointer_motion, field_view_keys |
| `view.cpp` | 1000:1EB0, 1F92, 21F0..2D01, 6DD5, 6F4D, 348e:0A90 (ordnance part) | cameras (view_init_cameras, view_update_camera, view_set_*, view_enter_map, view_set_eye_height, view_next_enemy), view_render_frame, hud_reset_mission, the renderer hooks (unit lookup, effect painter for muzzle flashes / explosions / clouds / impact puffs, blocker probe), the scenery hook |
| `hud.cpp` | 1000:0900..18EB, 79F7, 80F1, 8339 | compass tape, objective marker, target diamond and names, text lines, clock box, msg_draw / msg_draw_queue for the mission message queue (`mission/msg.h`) |
| `map.cpp` | 19ac:2E69..5F6A | map_init, coordinate transforms, routes, markers, control-panel buttons (table DS:0BF4 and labels DS:1BDE from st.exe), team list, orders menu, info panel and Team Info table, hit test / focus / pointer, map_zoom_keys, map_screen_keys, insert_keys |
| `loop_cmd.cpp` | — | `--play-mission` developer command |
| `front/hooks.cpp` | 365e:6DC4, 6E0C, CC6E, CCA0, B5CD, D576, D5B2, 19ac:64D4 | the 3D hooks of the briefing, debriefing and cut-scenes |

## Frame order

`mission::run()` = mis_run. Set-up: renderer data (models, sky, sprites),
hook installation, `view_init_cameras`, the world-independent part of
`hud_reset_mission`, `clk_reset`, `simInit(missionSetupFromCampaign())`
(everything of the original set-up up to the frame loop, including
`view_set_first_person` / `mis_start_insertion` through the hooks), the rest
of `hud_reset_mission` (eye height, map_init, orbit heading,
`mdl_time_of_day_colors`), `view_next_enemy`, the time-of-day palette,
`clk_reset`, input flush, `pal_set_level(0x100)`.

Every frame follows 1000:01DE exactly (docs/re/seg_1000.md 6.2):

```
switch g_tc_state:
  0: snd_music_start_once(mus_get_mood); view_update_camera; veg_update(0); view_render_frame
  1: hud_draw_clock_box; present
  2: page_copy_full; msg_draw_queue; state = 1; present; page_copy_full
  3: view_update_camera; view_render_frame; msg_draw_queue; state = 2; present; page_copy_full
clock: state 0 -> simFrameClock(g_ticks), state 1 -> simFrameClockCompressed
tod_update + spr_advance_anim_clocks (simFrameAnimate) unless the frozen map is up
input_reset_button2_timers in views >= 2 (not 0xC); input mode map / action
key = input_get_key (scripted keys first), motion; cmd_order_keys
state 0: present (the first four frames clear the screen instead; g_skip_present after Alt-P)
frozen map: map_screen_keys only
else if not g_mis_freeze: state 0: map_screen_keys / insert_keys (+ automatic
     mis_insertion_clear) / field_view_keys; simFrameUpdate (extraction end test,
     periodic world updates, evt_mission_tick, veg_update(1) every 0x900 ticks);
     pal_request_fade with its result unless the view is 0xC
msg_queue_tick
```

Exit: page copy, shake off, menu input mode; Alt-X returns 1 after
`simShutdown()` (the original leaves to DOS without `mis_cleanup`), otherwise
`unload()` = `simShutdown()` (ai_shutdown, snd_unload_sfx_bank, mis_free_world
and the statistics hand-over to the campaign).

### Clock

`g_time` is `ms().time`; the raw 256 Hz counter `g_ticks` is the ticker's
count minus `LoopState::tickBase`, which `clk_reset`, `clk_resync_raw` (time
compression off) and `clk_restore` (Esc dialog, Alt-P, the frozen map) move.
The ticker itself keeps running for the input layer's repeat timers.

## Hook wiring

| Hook | Set by the loop to | Called by |
|---|---|---|
| `SimHooks::viewModeChanging(mode)` (new) | `viewSelectCamera`: `g_cur_camera` (0xC: `view_enter_map`), the orbit distance of modes 7 / 8 / 2 (`mis_start_insertion` 0x264, `mis_start_extraction` 0x120, `mis_insertion_clear` 0xC0) | `sim.cpp setViewMode` before its `veg_update(1)`, so the scenery sees the new camera like `view_set_*` |
| `SimHooks::viewModeChanged(mode)` | `viewModeStored`: `g_full_redraw = 2`, `fx_markers_hide(1)` | after the mode store |
| `SimHooks::clockResync` / `fullRedraw` | `clkResyncRaw` / redraw | `tc_off` |
| campaign hooks | `addCampaignHooks` | `mis_start_extraction` |
| `setVegetationHook` / `setVegetationResetHook` | `viewVegUpdate` (`render::vegUpdate` with the current camera, the Point Man position and `ent_team_all_extracted`) / `viewVegReset` | `simInit`, every 0x900 ticks, `mis_insertion_clear` |
| `setGroundFxCameraHook` | the current camera position | `fx_unit_ground_fx` (rng within 0x4B0) |
| `craftSetViewerPos` | `LoopState::curPos` (a copy of the current camera position) | ambient flyers |
| `setViewHeadingTurnHook` | `g_view_heading` += delta | `unit_turn` |
| `render::setUnitLookup` / `setEffectPainter` / `setAnimUpdate` / `setPointMan` / `setBlockerProbe` | `unitFromBody`, the ordnance painter of view.cpp, `sprUpdateAnim`, the Point Man, `wld_probe_kind(pos, 5)` | billboard callback, scenery placement |
| `render::vegSetPools` | `fxPools().vegetation` / `.trees` (handed over lazily when the pools change) | scenery |
| `render::renderContext()` | filled by `viewUpdateRenderContext` before every render / scenery call: frame ticks, time, hour, view mode, detail level, reinsertion point, reticle target (the Point Man's target body) | renderer, billboards |

`view_set_target_camera` (F9) is done by the loop itself because its
`veg_update(1)` runs after the mode store, unlike `view_set_mode`.

### Front-end hooks (`front/hooks.cpp`)

* Briefing: `briefingWorldEnter` = `simBuildWorld` (mis_load_world +
  msn_build_world + map markers, drawing random numbers as the original) +
  `brf_init_scene` (eye height, `view_enter_map`, orbit defaults, the SEAL
  leader parked at x -3000). `drawBriefingView(view)`: the front end's view
  becomes the map camera; view_clear_map_ground, r3d_render_view,
  map_draw_routes clipped to the window.
* Debriefing: `debriefWorldEnter` additionally runs `view_update_camera(1)`
  (the map camera on the Point Man), hands the position to the front end as
  the first camera target and parks him at (0xFFF44800, 0x0004B000).
* Cut-scenes: `campSceneLoad` = `camp_load_scene`; `campScenePlace` =
  `camp_place_at_insertion` / `_after_extraction` + `view_set_mode(5 / 6)`
  (veg_update(1)) + the sky offsets of `tod_palette_index`;
  `campSceneTick(heading, zoom)` syncs the simulation clock with the front
  end's, runs `view_update_camera(5 / 6)` (the random +-15 degree offset of
  the first two calls, see below), `spr_advance_anim_clocks` and
  `evt_mission_tick`, and returns the camera angle; `drawCampView` renders
  the small camera with the sky gradient.

## Developer command

```
sealteam [--original|--enhanced] --play-mission <1..80> [--keys SPEC] [--save-dir DIR]
         [--shot FILE --shot-after S]
```

Starts a practice-style mission directly (random Point Man of the year's
pool, `roster_new`, `loadout_build_team`, fade, "One Moment Please ...",
`mission::run()`). `--keys` scripts the mission keys (front/common.h now
also knows F1..F9, Tab, ShiftTab, Home, End, PgUp, PgDn, Center, Ctrl arrows,
AltT/U/I/P/D/N, Equals, LBracket, RBracket). Times are wall-clock seconds
since the command start; the mission itself begins about 3 s later
(transition + "One Moment Please ...") and the insertion craft leaves after
0xA00 ticks (10 s) unless Esc / Enter skips it.

## Verification

Screenshots of the Original preset (`re/scratch_loop/*.png`) against the
original running in DOSBox Staging (`re/scratch_loop/dosmis.ps1`, a variant
of `re/scratch_render/dosshot.ps1` that sends scan-code key events between
captures; captures in `re/scratch_loop/cap_m1*/`), mission 1 in demo mode
(`st t 1`):

* Insertion view: banner, "Esc to skip - R to Reinsert", "Insertion"
  message, the LSSC and the four SEALs at the same place and size as the
  original (orbit distance 0x264, altitude 0x1800).
* Chase view after the insertion: objective marker, "Insertion craft
  clear.", weapon / grenade / item lines with the original's format and
  positions; the same composition as the DOSBox capture.
* First-person view: compass tape with labels and ticks, the weapon line
  (`CAR15 Commando (20) Semi`), grenade and item lines match the original's
  layout (`M79 Grenade Launcher (1) Grenade` / `M26 Frag Grenade:5` /
  `Med Medical Kit:1` in the capture).
* Map screen: compared by palette index with the DOSBox capture of the same
  mission at nearly the same time: the whole control panel (buttons, team
  list, Team Info table, orders menu, info panel, date, scale, area name) is
  pixel-identical except the clock digits (03:15:25 vs 03:15:05), the Point
  Man's weapon column (a different random loadout: `M79 5` vs `CAR15 8`)
  and 11 pixels of a short grey line under the EXpand button in the
  original (row 187, x 201..212) whose source was not found. The map area
  differs only in the dynamic objects (the boat has moved, the SEAL dots).
* Time compression (Alt-T): "Time Compression On" line, only the clock box
  (`03:16:13`) is redrawn under the view; the original's capture shows the
  same clock box on the map screen.
* Re-insertion map ('r' during the insertion): the map with the boat
  selected in the team info, "Press R to Reinsert." sticky message, no team
  list / orders menu (only Zoom / EXpand).
* Firing (Enter twice): tracer, rounds 20 -> 14 in the weapon line, the
  Point Man's muzzle flash is not drawn in the first-person view.
* Hand signals: the icon of the `hnds` bank with the name line above and
  the signal name below (the original's map capture with "Enemy" confirms
  the y positions 0x99 - 0x14 / -0x3C / -0x11).
* A full mission to the end: emergency extraction ordered on the map
  (`m`, `y`, `m`), the helicopter picks the team up, the extraction orbit
  (view 8), fade-out, `g_mis_done` after 0x1D00 ticks; `--play-mission`
  reports minutes 1, score 305.
* `--main-loop` practice flow with scripted keys: title -> menu -> intel ->
  briefing -> mission (Esc, Esc, 'y' aborts) -> debriefing; the
  Post-Mission Report shows this mission's data (0 minutes, rounds 0,
  friendly casualties none). The campaign flow (recruit -> campaign ->
  intel -> briefing fly-over -> bull session -> insertion cut-scene ->
  mission -> extraction cut-scene) was run the same way.
* Demo mode (`sealteam 3 t`): the command-line mission starts straight in
  the insertion view; the mission-3 frame matches the DOSBox capture of
  `re/scratch_render/cap_m3` (boat, sky gradient, shoreline, SEALs).
* Briefing fly-over and debriefing map (`--screen briefing --keys b@3`,
  `--screen debrief --sample --keys d@3`): the top-down world with the
  dashed route and the altitude read-out.
* Enhanced preset: the 3D view of the field views and of the map screen at
  render scale 2 with the HUD, markers and panel on top (`re/scratch_loop/
  enh.png`, `mapenh.png`).

Headless simulation runs with large frame deltas (`--sim-mission 1 --dt 60
/ 200`) stay clean; stress runs of four parallel instances of
`--play-mission` were used to find a use-after-free of the mission state
(fixed: `simInit` replaces the `MissionState` object, so the loop takes its
reference only afterwards).

## Deviations

* `hud_reset_mission`'s loop parts run around `simInit` instead of inside
  it: the values that do not need the world (orbit distances 0xC0, enemy
  view index, sky offsets) before, the rest after. `mis_start_insertion`
  inside `simInit` sets the orbit distance 0x264 through the view hook; the
  observable state after the set-up is the original's.
* `mis_load_sprites` / `mis_free_sprites`: the sprite banks stay loaded by
  the renderer (`spritesLoad` at start-up).
* The input layer's key repeat timers run on the ticker's real time, also
  during time compression (the original compares them with the compressed
  `g_time`).
* Cut-scene camera: the original runs `view_update_camera` before the frame
  and `cut_render` decrements `g_full_redraw`, so the random camera offset is
  drawn twice (before the loop and in frame 1); the front end renders before
  it ticks, so `campSceneTick` counts the two calls itself. Frame 1 is
  rendered with the camera of the pre-loop call (the original's frame 1 uses
  the second one); the random-number order is the same.
* `drawBriefingView` / `drawDebriefView` are called by the front end in the
  full-redraw branch as well as per frame, so full-redraw frames render the
  3D map twice (the original clears the ground only in the first place).
* "Team x: order" HUD line (g_team_msg_ptr DS:0514) is never set by the
  game; not drawn. Kind-4 medal pictures are never queued in a mission.
* `map_draw_button`'s label underline uses the byte 0xFF of buttons 0 and 1
  as 255 (never drawn in the original either).
* Port guards (with comments): NULL teams in the team list / hit test /
  Tab cycling, missing target for the target info, `view_next_enemy` with
  no enemy team, spr_draw_explosion without a projectile.
* Enhanced: `view_clear_map_ground` also fills the high-resolution layer so
  the map view starts from the ground colour; the map markers project with
  the normal page projection (`render::projectPage`).

## Open issues

* The short grey line under the EXpand button seen in one DOSBox map
  capture (row 187, x 201..212, colour 8) is not reproduced; its origin in
  the original was not identified (it may be a remnant of an earlier frame).
* Pixel comparison covered the insertion view, the first-person HUD layout
  and the map screen of mission 1; the target diamond / names, the split
  team lines and markers, the support orders on the map, the enemy view
  (F10) and the hand-signal icons were checked visually / by the code only.
* Joystick input: a game controller stands in for the game-port joystick (docs/controller.md); the calibration screen is not reproduced.
* Screen shake (`ticker().shake*`) is driven by the simulation; not
  verified here.
