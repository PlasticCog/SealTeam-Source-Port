# Front end, campaign model and main loop

Everything outside a mission: the campaign data model and save files, the
top-level game loop, and every screen between the main menu and the mission
(recruiting, campaign, intelligence, briefing, bull session, camp
cut-scenes, debriefing, speeches, difficulty options). The original code is
segment 365e (docs/re/seg_365e_a.md, seg_365e_b.md) plus the main loop
19ac:016D (docs/re/seg_19ac.md 2.2) and the UI/message helpers of 265e/1000.

All game text, tables and layouts are read from st.exe at run time
(`exe().dgString(off)`, `tableString(table, i)`, `loadButtonList(seg, labels)`);
nothing is embedded. The only random numbers drawn by the front end go
through `engine::rng()` at the places the original calls `rng_range`.

## Files

| File | Original | Contents |
|---|---|---|
| `game/campaign.h/.cpp` | 365e:1ACC..4C1D, DEB0, 19ac:00B6 | campaign header, roster + SE records, loadout, weapon/tool/flow/promotion/MoH tables, cN.cmp load/save, msn_load, statistics, scoring and awards |
| `game/config.h/.cpp` | 365e:3760..3D2F, 1000 cfg | s.cnf slot directory, st1.dfr options, save-directory redirection |
| `game/main_loop.h/.cpp` | 19ac:016D | `mainLoop()`: title, main menu, practice / campaign / demo step tables |
| `game/ui.h/.cpp` (extended) | 265e:2EBA.., 1000:7B06.. | text panels, dialog prompt, confirm dialogs, arrow-key menu navigation |
| `game/menu.cpp`, `game/title.cpp` | 19ac | main menu (F10 opens the difficulty screen) and title |
| `front/common.h/.cpp` | 1000:7B06..840B, 365e:94DE/9505, 7E14.. | sprite sets, message queue, 4x6 text, emboss, time of day, recruit voice, scripted keys |
| `front/panels.h/.cpp` | 365e:81F1, 889D, 9998, 9C20 | Biography / Rating / Campaign panels, ribbons, string tables |
| `front/brfcam.h/.cpp` | 365e:1F28..225B | map camera waypoint ring (briefing and debriefing) |
| `front/hooks.h/.cpp` | see below | connection points to the 3D world / renderer (placeholders) |
| `front/difficulty.cpp` | 365e:E5BD | Difficulty options (F10) |
| `front/recruit.cpp` | 365e:8095 | UDT/SEAL Training School (new campaign) |
| `front/campaign_screen.cpp` | 365e:9AA6 | Campaign screen: dog tags, save/load/erase slots, panels |
| `front/intel.cpp` | 365e:74E8 | Intel Briefing (campaign) / practice mission selection |
| `front/briefing.cpp` | 365e:4C7A..6FE6 | Mission Briefing, Patrol Order, Marching Order + loadout editor |
| `front/bull.cpp` | 365e:A862..AFDC | SEAL Bull Session |
| `front/cutscene.cpp` | 365e:CEA8..D7D6 | insertion / extraction camp cut-scenes |
| `front/debrief.cpp` | 365e:AFDC..CEA8, 1000:2D83 | Mission Debriefing, Post-Mission Report, Historic Report, recording, "One Moment Please ..." |
| `front/speech.cpp` | 365e:D7D6..DBE6 | Chaplain / Commander speeches |
| `front/devscreens.cpp` | — | developer commands (below) |

## Campaign API (for the mission code)

`namespace st::game::campaign` (campaign.h has the full list):

* team: `loadout()` (4 `LoadoutRecord`s, Point Man first, list ends at
  `se_id == -1`), `rosterFind(se_id)` → `RosterEntry` (`->se` = SE record),
  `header()` (year, mission, point man, history);
* options: `difficulty()` (st1.dfr, config.h);
* mission data: `missionLoad()` (msn_load: `mci()`, `mtm()` for
  `g().missionNo`), `loadMissionText`, `loadTextLines`;
* results: `stats()` (far 53BA:303A block), `scoreResetStats()`, `result()`
  (DGROUP results read by the debriefing), `scoreMission` /
  `awardEvaluateMission(ScoreInput)`, `rosterRecordCasualty/AddAwards/SetRank`;
* flow: `missionWon(score)`, `recordMission(score)`, `saveAutosave()`.

The mission agent's `campaign_link.cpp` already uses this API.

Save files: `load/save(slot)` are byte-exact; `--cmp-roundtrip DIR` re-saves
every shipped c0..c8.cmp, s.cnf and st1.dfr and compares the bytes (all
identical). Writes go to the save directory (`--save-dir DIR` or
`SEALTEAM_SAVE_DIR`), reads check it first and fall back to the game folder.

## Main loop

`mainLoop()` follows the jump table at 19ac:0309 exactly (steps and the
"back" / "quit" results of each screen are listed at the top of
main_loop.cpp). Verified end to end with the mission stub: title → menu →
practice (intel → briefing → mission → debriefing → menu) and new campaign
(recruit → campaign → intel → briefing → bull session → insertion → mission
→ extraction → debriefing → campaign → intel of the next mission). Each step
logs `main loop: ... step N`. The lead switches game.cpp to `mainLoop()`.

## Screens

All screens: layouts, colours, fonts, timings (256 Hz ticks), keys, pointer,
buttons, music sequences and voice samples as in the original. Screenshots
(1x, 320x200) were compared with the original layout for each item marked
verified.

| Screen | Status | Verified screenshots |
|---|---|---|
| Difficulty (F10) | complete, saves st1.dfr with 's' | yes |
| Recruit (Training School) | complete: recruits per year, Bio/Rating pages, voice, nickname prompt | Bio, Rating, nickname prompt |
| Campaign screen | complete: dog tags, awards / campaign over / new members messages, save (name prompt), load, erase, panel pages | c5, c6 (medal message), Bio page |
| Intel Briefing / practice selection | complete: NILO talk, zoom map, markers, calendar | campaign c5, practice |
| Mission Briefing | complete 2D; fly-over is a hook | idle, running (altitude read-out), Patrol Order, Marching Order |
| Loadout editor (Marching Order) | complete | yes |
| Bull Session | complete: animated SEALs, conversation, Corpsman chatter | yes |
| Insertion / extraction cut-scenes | complete 2D (caption, timing, camera input, music); camp view is a hook | both (captions, clock) |
| Mission Debriefing | complete 2D; map fly-over is a hook | idle, talking (read-out), with Historic clipboard |
| Post-Mission Report | complete | yes (sample results) |
| Historic Report | complete | yes (h3m07) |
| Speeches (cm0..cm5) | complete | Chaplain, Commander |
| "One Moment Please ..." | complete | via main loop |

## Hooks for the renderer / mission (front/hooks.h)

Placeholders fill the viewport black; each names its original addresses.
The mission code already has the world set-ups (`mission/sim.h`:
`simBuildWorld`, `campLoadScene`, `campPlaceAtInsertion`,
`campPlaceAfterExtraction`, `evtMissionTick`); the 3D drawing
(`r3d_render_view`, sky/ground, map routes) belongs to the renderer.

| Hook | Original | Called by |
|---|---|---|
| `briefingWorldEnter/Leave()` | 1000:4580 mis_load_world, 365e:129A, 19ac:2B60, 365e:6DC4 / 1000:45A6 | briefing enter / leave |
| `drawBriefingView(view)` | 365e:6E0C (1000:1C8A, 2255:3090, 19ac:30BA) | briefing while running, clip = view |
| `debriefWorldEnter(area, mapView, leaderPos)` | 1000:4580, 365e:CC6E, 365e:B5CD | debriefing enter: set the map camera x/z, return the Point Man's position (camera target) and park him |
| `debriefWorldLeave()` | 1000:45A6 | debriefing leave |
| `drawDebriefView(view)` | 365e:CCA0 (1000:1C8A, 2255:3090, 19ac:30BA) | debriefing while the OIC talks |
| `campSceneLoad(area)` | 19ac:64D4 | cut-scene enter |
| `campScenePlace(extraction)` | 19ac:6945 / 6B07, view_set_mode 5/6, mis_load_sprites | after the time-of-day draw (random number order kept) |
| `campSceneTick()` | view_update_camera, spr_advance_anim_clocks, 2dbd:3F4C | once per cut-scene frame |
| `drawCampView(view, heading, zoom)` | 365e:D5B2 (1000:1AFC, 2255:3090) | cut-scene render; heading in 1/8 degree (0x690 start), zoom 0x60..0x384 |
| `campSceneLeave()` | mis_free_sprites, mis_free_world | cut-scene leave |

The map camera position the hooks receive is moved by `front::brfcam`
exactly like the original (altitude read-out = (y >> 8) / 3 "Meters").

## Deviations

* Intel: "won" for the last mission is taken from the Point Man's history
  (the original reads a stale team-leader pointer).
* Briefing / Patrol Order team types are derived from the MCI
  (`insertionTeamType()` etc.) because the world is not built in the front
  end yet; area names come straight from `<world>.wd`.
* The front end keeps its own message queue (`front::msg`) and time of day
  (`front::tod`); the mission code has its own copies. The extraction
  cut-scene rebuilds the mission-end clock from the MCI start time + the
  mission minutes (the original continues the mission clock).
* Cut-scenes: `engine::Sound::done()` is true before a sequence was started,
  unlike snd_music_done (4592:1B86, true only for AIL SEQ_DONE); the
  cut-scene tracks the start itself. Suggested fix in engine/sound (mission
  agent): report done only after a started sequence finished.
* Debriefing: the Point Man history lookup is guarded for more than 24
  missions (the original reads past the table).
* `rosterAdd` refuses more than 40 entries; scoring guards divisions by zero.
* Saves can be redirected (`--save-dir`, `SEALTEAM_SAVE_DIR`).

Original quirks kept (with comments and addresses): the Rating panel's STR
word from stale roster skills; "SEAL Team Two" only from SE 25 on
(365e:B966); the casualty talk skips the Point Man; "Results: n/a" may prefix
the captured-items line; the unreachable fade-out branch of the extraction
timeout; loadout reload bytes read for empty slots; Enter during a dialog
redraw frame is dropped.

## Open issues

* 3D views (briefing, debriefing, camp cut-scenes) wait for the hooks above.
* The debriefing parks the Point Man only through the hook; without a world
  the camera target is the origin.
* Demo mode (command-line mission) is wired in the main loop but only tested
  with the mission stub.

## Developer commands

```
sealteam --screen NAME [--slot N] [--mission N] [--practice] [--sample]
         [--score N] [--won 0/1] [--kia N] [--kind K] [--save-dir DIR] [--keys SPEC]
   NAME: difficulty recruit campaign intel briefing patrol marching bull
         insertion extraction debrief report historic speech
sealteam --main-loop [--save-dir DIR] [--keys SPEC]
sealteam --cmp-roundtrip DIR
```

`--keys` scripts key presses: comma separated keys (a character, Enter, Esc,
Space, Backspace, Up, Down, Left, Right, F10, AltX, AltE, AltS, AltM, Plus,
Minus, Comma), each optionally `@seconds` since start. `--sample` fills
plausible mission results for the debriefing screens. Always pass
`--save-dir` in tests so the shipped saves are never written.
