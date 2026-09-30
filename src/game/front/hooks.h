// Connection points between the front-end screens and the 3D world /
// renderer (src/render, src/game/mission). The 2D parts of every screen are
// ported; wherever the original builds a world or draws a 3D view the screen
// calls one of these hooks. Until they are connected they only keep the
// area black (the 2D frame around it is drawn by the screen).
#pragma once

#include "core/common.h"
#include "game/types.h"

namespace st::game::front::hooks {

// A screen-rectangle viewport and its camera (view_init_camera 1000:21F0;
// pos in 24.8, x/altitude/z; pitch in 1/8 degree).
struct View {
    Vec3 pos{};
    s16 yaw = 0, pitch = 0, roll = 0;
    s16 x = 0, y = 0, w = 0, h = 0;
};

// ---- Mission briefing (brf_screen 365e:54BD)
// mis_load_world(area) + msn_build_world, map markers (19ac:2B60), eye
// height / map view (brf_init_scene 365e:6DC4); the SEAL leader is parked
// at x -3000 during the briefing.
void briefingWorldEnter();
// mis_free_world (1000:45A6).
void briefingWorldLeave();
// Top-down fly-over in the briefing window while the briefing runs:
// view_clear_map_ground (1000:1C8A), r3d_render_view (2255:3090) and
// map_draw_routes (19ac:30BA) (365e:6E0C). Clip is set to the viewport.
void drawBriefingView(const View& view);

// ---- Debriefing (dbrf_screen 365e:B511)
// mis_load_world(area) (1000:4580) and the world part of dbrf_init_scene
// (365e:CC6E): view_set_eye_height(team 0 leader), view_enter_map (the map
// camera takes the main camera's x/z: write them to mapView.pos.x/z;
// map_select_team_height), team 0 +0x2C = 0x0003E800; then (365e:B5CD) the
// Point Man's world position goes to leaderPos (the camera's first target)
// and he is parked at x 0xFFF44800, z 0x0004B000.
void debriefWorldEnter(int area, View& mapView, Vec3& leaderPos);
void debriefWorldLeave();   // mis_free_world (1000:45A6)
// Map fly-over while the OIC talks (365e:CCA0): view_clear_map_ground
// (1000:1C8A), r3d_render_view and map_draw_routes (19ac:30BA). Clip is set
// to the viewport.
void drawDebriefView(const View& view);

// ---- SEAL camp cut-scenes (365e:D285 / D659)
// camp_load_scene(area) (19ac:64D4): loads the camp world of the area
// (world 0x1C + g_area_camp_frame[area]).
void campSceneLoad(int area);
// camp_place_at_insertion (19ac:6945) or camp_place_after_extraction
// (19ac:6B07), view_set_mode(5 / 6) and mis_load_sprites. Called after the
// time-of-day random draw of the insertion scene, as in the original.
void campScenePlace(bool extraction);
void campSceneLeave();      // mis_free_sprites, mis_free_world
// One frame of the orbiting camp camera (view_update_camera, spr_advance_anim_clocks,
// evt_mission_tick) - world update part.
void campSceneTick();
// Sky/ground (1000:1AFC) and r3d_render_view for the camp viewport; the
// camera orbits at `heading` (1/8 degree) and `zoom`.
void drawCampView(const View& view, int heading, int zoom);

} // namespace st::game::front::hooks
