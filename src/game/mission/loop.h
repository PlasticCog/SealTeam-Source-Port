// The in-mission loop: mis_run (1000:01DE) and everything of segments 1000
// and 19ac that draws or reads input while a mission runs. Internal header
// of the loop modules (loop.cpp, loop_input.cpp, view.cpp, hud.cpp, map.cpp);
// the public entry points are mission.h.
//
//   loop.cpp       mis_run / mis_cleanup, the frame loop, the game clock helpers,
//                  the hooks into the simulation and the renderer, the dev command
//   loop_input.cpp cmd_order_keys (19ac:0CC3), input_toggle_keys (0B2A),
//                  field_view_keys (1A79), field_pointer_motion (1816)
//   view.cpp       cameras and view modes (1000:21F0..2D83), view_render_frame
//                  (1000:1F92), the effect sprites of 1000:6DD5 / 6F4D
//   hud.cpp        the HUD of the field views (1000:0900..18EB) and the message
//                  queue drawing (1000:79F7, 80F1, 8339)
//   map.cpp        the map screen / control panel (19ac:2B60..5F6A)
//
// Every global of the original is a member of LoopState (its DS offset in the
// comment); the simulation's globals stay in mission/state.h.
#pragma once

#include "game/types.h"

namespace st::game::mission {
namespace loop {

struct LoopState {
    // ---- Cameras (1000:21F0..2261) ---------------------------------------
    Camera camMain{};               // DS:D84E main 3D camera (Point Man's eyes)
    Camera camMap{};                // DS:D86E map camera; pos.y = g_map_height (DS:D872)
    Camera camSmall{};              // DS:D88A small 3D camera (chase / remote views)
    Camera* cur = nullptr;          // DS:D8AE g_cur_camera
    int viewTeam = 0;               // DS:053E g_view_team (team followed by the camera / listener)
    s32 orbitDist = 0xC0;           // DS:D8B4 g_orbit_dist (insertion / extraction orbit)
    s16 orbitHeading = 0;           // DS:D8B8 g_orbit_heading
    s32 orbitDistCraft = 0xC0;      // DS:D846 g_orbit_dist_craft (camp cut-scene zoom)
    s16 orbitAngle = 0;             // DS:D842 g_orbit_angle (camp cut-scene heading)
    int enemyViewIdx = -1;          // DS:053C g_enemy_view_idx (F10 view)
    const Team* targetCamTeam = nullptr;  // DS:0538 g_target_cam_team (F9 view)
    int prevViewMode = 0;           // DS:EC52 g_prev_view_mode (3D mode to return to from the map)
    s32 viewDistance = 0;           // DS:CEAE g_view_distance (field views, 0x60..900)
    s16 viewHeading = 0;            // DS:CEAC g_view_heading (1/8 degree)
    Vec3 curPos{};                  // copy of cur->pos for craft_set_viewer_pos (a stable address)
    int cutRandomTicks = 0;         // camp cut-scenes: view_update_camera calls that still draw the random offset

    // ---- Frame / redraw state ----------------------------------------------
    int fullRedraw = 0;             // DS:EC56 g_full_redraw (frames still needing a full redraw)
    bool skipPresent = false;       // DS:D7F6 g_skip_present (after the pause dialog)
    bool misFreeze = false;         // DS:D7EC g_mis_freeze (never set by the game)
    int renderResult = 0;           // g_render_result
    int startFrames = 0;            // mis_run local: the first 4 frames are cleared

    // ---- HUD / map switches -----------------------------------------------
    bool compassEnabled = true;     // DS:02BC g_compass_enabled
    bool teamInfoNames = false;     // DS:0F90 g_toggle_alt_i (map Team Info in Name/Rank mode)
    u8 mapCursorMode = 0;           // DS:EC6E: 0 keyboard focus on buttons, 1 free map cursor, 2 pointer over buttons

    // ---- Clock (1000:2FEA..314B) -----------------------------------------
    // The raw 256 Hz counter g_ticks (DS:ECB0) is the ticker's tick count
    // minus tickBase; clk_reset / clk_resync_raw / clk_restore move the base.
    s32 tickBase = 0;
    Ticks savedTime = -1;           // DS:EC9E g_saved_time (clk_save / clk_restore)
};

LoopState& ls();

// ---- loop.cpp ------------------------------------------------------------------
s32 clkRawTicks();       // g_ticks
void clkReset();         // 1000:30C9 clk_reset (g_time, g_ticks, saved time; frame ticks 1)
void clkResyncRaw();     // 1000:307B clk_resync_raw (g_ticks = g_time)
void clkSave();          // 1000:308A clk_save
void clkRestore();       // 1000:3099 clk_restore
void presentFrame();     // 1000:1E87 gfx_present
void pageCopyFull();     // 1000:1EA7 page_copy_full (displayed page -> draw page)
// The key script of the developer commands is consulted first (front/common.h).
int loopGetKey();

// ---- view.cpp ------------------------------------------------------------------
void viewInitCameras();                 // 1000:2261
void viewSelectCamera(int mode);        // the g_cur_camera part of view_set_* (before veg_update)
void viewModeStored(int mode);          // the g_full_redraw / fx_markers_hide part (after the store)
void viewEnterMap();                    // 1000:22D4
void viewSetFirstPerson();              // 1000:2321
void viewSetChase();                    // 1000:2349
bool viewSetTeamCamera(int n);          // 1000:2371 (F3..F8: n = 0..5)
void viewSetMode(int mode);             // 1000:23FB
void viewSetTargetCamera();             // 1000:2432 (F9)
void viewSetEyeHeight(const Unit* u);   // 1000:2462
void viewInitOrbitAngle(Team* t);       // 1000:24F0
void viewUpdateCamera(int mode);        // 1000:2544
void viewNextEnemy();                   // 1000:2D01
void viewRenderFrame();                 // 1000:1F92
void hudResetMission();                 // 1000:1EB0 (the view part; the simulation resets its timers)
// The renderer hooks the loop installs (billboard unit lookup, effect
// painter, animation update, blocker probe) and the scenery pools.
void viewInstallRenderHooks();
// Model, sky and sprite data of the renderer (idempotent).
bool viewEnsureRenderer();
void viewUpdateRenderContext();         // fill render::renderContext() from the mission state
void viewVegUpdate(bool force);         // veg_update(force) with the current camera
void viewVegReset();                    // veg_reset

// ---- hud.cpp -------------------------------------------------------------------
void hudDrawCompass();                  // 1000:0900
void hudDrawObjectiveMarker();          // 1000:0A2C
void hudDrawTargetInfo();               // 1000:0D44
void hudDrawTextLines();                // 1000:10C2
void hudDrawClockBox();                 // 1000:18EB
void msgDrawQueue();                    // 1000:8339 (mission message queue)
void msgDraw();                         // 1000:80F1

// ---- map.cpp -------------------------------------------------------------------
void mapInit();                                       // 19ac:2E69
void mapSelectTeamHeight();                           // 19ac:2FE8
void mapScreenToWorld(int sx, int sy, s32& x, s32& z);// 19ac:2EC7
bool mapWorldToScreen(const Vec3& pos, int& sx, int& sy);  // 19ac:2F47
void mapDrawRoutes();                                 // 19ac:30BA
void mapDrawMarkers();                                // 19ac:3300
void mapDrawTeamList();                               // 19ac:39B7
void mapDrawOrdersMenu();                             // 19ac:3E76
void mapDrawClock(int x, int y);                      // 19ac:417F
void mapDrawInfoPanel();                              // 19ac:421E
void mapScreenKeys(int key, int dx, int dy);          // 19ac:52A1
void insertKeys(int key, int dx, int dy);             // 19ac:5C8E
const s16* mapButtonRect(int n);                      // x, y, w, h of button n (DS:0BF4)

// ---- loop_input.cpp -------------------------------------------------------------
bool inputToggleKeys(int key);                        // 19ac:0B2A
bool cmdOrderKeys(int key);                           // 19ac:0CC3 (true = key consumed)
void fieldViewKeys(int key, int dx, int dy);          // 19ac:1A79
void uiResumeAfterOverlay();                          // 19ac:0CA7

} // namespace loop
} // namespace st::game::mission
