// Public API of the mission simulation for the mission loop / HUD phase.
//
// The original mission loop (1000:01DE mis_run, docs/re/seg_1000.md 6.2)
// interleaves rendering, clock, input and world updates in a fixed order; the
// simulation exposes each step so the loop can call them in that order:
//
//   simInit(setup)                      once (mis_run set-up, up to the loop)
//   per frame:
//     [render: view_update_camera, veg_update(0), view_render_frame, snd_update]
//     simFrameClock(now)                clk_update / clk_update_compressed
//     simFrameAnimate()                 tod_update + spr_advance_anim_clocks
//                                       (skipped on the map with Map = Freeze)
//     [input: cmd_order_keys, field_view_keys / map / insertion keys -> the
//      player actions of combat.h, teams.h, build.h and actions below]
//     simFrameUpdate()                  periodic updates + evt_mission_tick
//                                       (skipped while frozen / time compression rules)
//     simMsgTick()                      msg_queue_tick
//   until ms().misDone or ms().quitGame; then simShutdown().
//
// All state is in ms() (state.h); the HUD reads it directly (targets, timers,
// messages(), teams, units).
#pragma once

#include "game/types.h"

#include <array>
#include <functional>

namespace st::game::mission {

struct MissionSetup {
    int missionNo = 0;                         // 0..79 (g_mission_no)
    int year = 0;                              // campaign year 0..3 (g_campaign+1)
    GameMode gameMode = GameMode::Demo;
    DifficultyOptions options{1, 2, 2, 2, 1, 1, 1, 1};  // st1.dfr
    std::array<LoadoutRecord, 4> loadout{};    // marching order (far 5178:0000)
    std::function<RosterEntry*(int se)> rosterFind;  // campaign roster (SEAL personnel)
    int detailLevel = 5;
    bool handSignals = true;
    bool headless = false;                     // no audio output, no view hooks
};

// Optional callbacks into the presentation layer (all may be empty).
struct SimHooks {
    std::function<void(int mode)> viewModeChanged;   // view_set_mode(7/8), first person/chase switches
    std::function<void()> clockResync;               // clk_resync_raw after time compression
    std::function<void()> fullRedraw;                // g_full_redraw = 2
    std::function<bool(s16 score)> missionWon;       // 365e:3FAE cmp_mission_won (extraction music 7)
    std::function<void()> evaluateMission;           // 19ac:00B6 awd_evaluate_mission (after the casualty tally)
};
void setSimHooks(const SimHooks& hooks);

// mis_run set-up (1000:01DE up to the frame loop): mission files, world,
// teams, AI, timers, insertion. Returns false when the mission data is missing.
bool simInit(const MissionSetup& setup);
// mis_run exit (ai_shutdown) + mis_cleanup (1000:0790).
void simShutdown();

// clk_update: g_time = now (not decreasing), g_frame_ticks = max(1, now - prev).
void simFrameClock(Ticks now);
// clk_update_compressed: g_time += 0x80.
void simFrameClockCompressed();
// tod_update (1000:19EE) and spr_advance_anim_clocks (348e:000E).
void simFrameAnimate();
// The world part of the frame (1000:01DE after the input): extraction end test,
// 0x40 shots, 0x140 AI/music/alerts, 0x400 noise/bleeding/objectives, every
// frame evt_mission_tick, 0x900 scenery hook. Returns the palette fade request
// (0x700 during the last 0x400 ticks of the extraction, else 0; -1 = no
// request this frame, in the re-insertion map view 0xC).
int simFrameUpdate();
void simMsgTick();

// Scenery hook (veg_update(force), 1000:784D, owned by the renderer): called
// by simFrameUpdate every 0x900 ticks with force = 1 and by the insertion
// helpers; it draws random numbers, so the renderer must call it exactly
// where the original does.
void setVegetationHook(std::function<void(bool force)> fn);
// veg_reset (1000:70B3): park and hide the scenery, force regeneration; called
// by simInit only (mis_run set-up, before its veg_update(1)).
void setVegetationResetHook(std::function<void()> fn);

// World set-ups outside the mission loop (camp.cpp): the mission world alone
// for the briefing / debriefing views (mis_load_world + msn_build_world +
// map markers), and the SEAL camp cut-scene world (19ac:64D4 camp_load_scene
// of the mission's area with the camp teams, then 19ac:6945 / 6B07).
bool simBuildWorld(const MissionSetup& setup);
bool campLoadScene(const MissionSetup& setup, int area);
void campBuildTeams();
void campPlaceAtInsertion();
void campPlaceAfterExtraction();

// ---- Mission flow (1000) ------------------------------------------------------
void misStartInsertion();      // 1000:0028
void misInsertionClear();      // 1000:007A (the loop calls it on Enter/Esc or at insertionClearTime)
void misStartExtraction();     // 1000:00B0 (called by the extraction pickup)
int musGetMood();              // 1000:07A1
void musUpdateMood();          // 1000:07DD (every 0x140 ticks, not in time compression)
void misCheckAlerts();         // 1000:0842
void evtMissionTick();         // 2dbd:3F4C
// Time of day (1000:1964..19EE).
void todSet(int hour, int minute);
void todAdd(int hours, int minutes);
void todUpdate();

// ---- Player actions that are not in a module header ------------------------
// Posture keys ('1'/'2'/'3', '+'/'-', mouse): 19ac:1A79 / 1816.
// fromPointer = the mouse/joystick path (19ac:1983), which has no death check.
void playerSetPosture(int posture, bool fromPointer = false);
// Turn keys: unit_turn(point man, d).
void playerTurn(int d);
// cmd_order_keys (19ac:0CC3) order letters for the main team or, when the
// map has another SEAL team selected, the split team: c d f h i j l p q s t v w.
// Returns true when the key was consumed (the original's return value).
bool playerOrderKey(int key);

// Map screen orders (19ac:52A1 map_screen_keys, orders.cpp). The waypoint
// comes from map_screen_to_world(cursor) of the map screen (x/z, 24.8).
void mapSetWaypoint(s32 x, s32 z);
// Support-craft and fire orders on the map: a b u k o e y (and f t w for team
// 0). Returns the map button to show pressed (-1 none).
int mapOrderKey(int key);
// Insertion view 'r' (19ac:5C8E): enter the re-insertion map / confirm it at
// ms().wpSupport (resets the game clock to 0 like the original clk_reset;
// the loop must reset its timer too). Returns false when refused.
void reinsertBegin();
bool reinsertConfirm();

// view_set_mode for the simulation's own view switches (7 insertion,
// 8 extraction, 0/2 after the insertion, 0xC re-insertion): stores
// ms().viewMode and calls SimHooks::viewModeChanged.
void simSetViewMode(int mode);

// ---- Queries for the HUD and the debriefing -----------------------------------
Unit* unitFromBody(const Obj3D* body);  // 19ac:622C grp_from_object_id
bool objectiveComplete(int i);          // flag bit 0 / leader flag 8 of objective i

} // namespace st::game::mission
