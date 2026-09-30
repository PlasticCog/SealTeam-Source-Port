// The 3D hooks of the front-end screens (hooks.h), connected to the mission
// world builders (mission/sim.h: simBuildWorld, campLoadScene, campPlace*)
// and the loop's cameras and map drawing (mission/loop.h).
#include "game/front/hooks.h"

#include "engine/ticker.h"
#include "game/front/common.h"
#include "game/globals.h"
#include "game/mission/campaign_link.h"
#include "game/mission/entity.h"
#include "game/mission/loop.h"
#include "game/mission/people.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/mission/world.h"
#include "gfx/gfx.h"
#include "render/r3d.h"
#include "render/sky.h"

namespace st::game::front::hooks {

namespace {

using namespace st::game::mission;

Camera toCamera(const View& v) {
    Camera c{};
    c.pos = v.pos;
    c.yaw = v.yaw;
    c.pitch = v.pitch;
    c.roll = v.roll;
    c.rect_x = v.x;
    c.rect_y = v.y;
    c.rect_w = v.w;
    c.rect_h = v.h;
    c.zoom = 8;
    return c;
}

bool g_worldLoaded = false;

bool buildWorld(bool camp, int area) {
    if (!loop::viewEnsureRenderer()) return false;
    loop::viewInstallRenderHooks();
    const MissionSetup setup = missionSetupFromCampaign();
    const bool ok = camp ? campLoadScene(setup, area) : simBuildWorld(setup);
    g_worldLoaded = ok;
    if (ok) loop::viewInitCameras();
    return ok;
}

void freeWorld() {
    if (!g_worldLoaded) return;
    g_worldLoaded = false;
    worldFree();  // mis_free_world (1000:45A6)
}

// The top-down map view of the briefing / debriefing (365e:6E0C / CCA0, the
// 3D part): view_clear_map_ground, r3d_render_view with the map camera,
// map_draw_routes clipped to the map rectangle.
void drawMapView(const View& view) {
    if (!g_worldLoaded) {
        gfx().fillRect(view.x, view.y, view.w, view.h, u16(Gfx::kSolid | 0x00));
        return;
    }
    loop::LoopState& L = loop::ls();
    L.camMap = toCamera(view);
    L.cur = &L.camMap;
    Gfx& gx = gfx();
    gx.setClip(view.x, view.y, view.w, view.h);
    loop::viewUpdateRenderContext();
    render::viewClearMapGround(L.camMap);
    render::renderView(L.camMap, true);
    gx.setClip(view.x, view.y, view.w, view.h);
    loop::mapDrawRoutes();
    gx.clipFull();
}

} // namespace

// ---- Mission briefing ----------------------------------------------------------

void briefingWorldEnter() {
    if (!buildWorld(false, 0)) return;
    // brf_init_scene (365e:6DC4): eye height, map view, orbit defaults; the
    // SEAL leader is parked outside the map at x -3000.
    loop::LoopState& L = loop::ls();
    render::skyState().dawnTime = engine::ticker().time();
    Unit* pm = pointMan();
    loop::viewSetEyeHeight(pm);
    loop::viewEnterMap();
    L.orbitHeading = 0x690;
    L.orbitDist = 0xC0;
    render::skyState().skyOfs = 0;
    render::skyState().groundOfs = 0;
    if (pm) pm->body->pos.x = s32(u32(-3000) << 8);
}

void briefingWorldLeave() { freeWorld(); }

void drawBriefingView(const View& view) { drawMapView(view); }

// ---- Debriefing ------------------------------------------------------------------

void debriefWorldEnter(int /*area*/, View& mapView, Vec3& leaderPos) {
    if (!buildWorld(false, 0)) return;
    // dbrf_init_scene (365e:CC6E) + the world part of 365e:B5CD.
    loop::LoopState& L = loop::ls();
    L.camMap = toCamera(mapView);
    Unit* pm = pointMan();
    loop::viewSetEyeHeight(pm);
    loop::viewEnterMap();
    if (Team* t0 = team(0)) t0->map_height = 0x3E800;
    L.orbitHeading = 0x690;
    L.orbitDist = 0xC0;
    loop::viewUpdateCamera(1);
    mapView.pos.x = L.camMap.pos.x;
    mapView.pos.z = L.camMap.pos.z;
    if (pm) {
        leaderPos = pm->body->pos;
        pm->body->pos.x = s32(0xFFF44800u);
        pm->body->pos.z = 0x0004B000;
    }
}

void debriefWorldLeave() { freeWorld(); }

void drawDebriefView(const View& view) { drawMapView(view); }

// ---- SEAL camp cut-scenes ----------------------------------------------------------

void campSceneLoad(int area) {
    buildWorld(true, area);
    loop::ls().cutRandomTicks = 0;
}

void campScenePlace(bool extraction) {
    if (!g_worldLoaded) return;
    if (extraction) campPlaceAfterExtraction();
    else campPlaceAtInsertion();
    // cut_init_camera (365e:D576): view_set_mode(5 / 6) - veg_update(1), full
    // redraw; the sky offsets come from tod_palette_index of the screen transition.
    simSetViewMode(extraction ? 6 : 5);
    loop::ls().cutRandomTicks = 2;  // view_update_camera draws its random offset while g_full_redraw == 2
    render::todPaletteIndex(tod::hour(), tod::minute());
}

void campSceneLeave() { freeWorld(); }

int campSceneTick(int heading, int zoom) {
    if (!g_worldLoaded) return heading;
    loop::LoopState& L = loop::ls();
    MissionState& S = ms();
    L.orbitAngle = s16(heading);
    L.orbitDistCraft = zoom;
    // The cut-scene runs on the front end's clock and time of day.
    simFrameClock(engine::ticker().time());
    S.todHour = s8(tod::hour());
    S.todMinute = s8(tod::minute());
    // The original's cut_render decrements g_full_redraw after the first
    // frame, so the random camera offset is drawn twice (before the loop and
    // in the first frame); the front end renders before it ticks, hence the counter.
    if (L.cutRandomTicks > 0) {
        --L.cutRandomTicks;
        L.fullRedraw = 2;
    } else {
        L.fullRedraw = 0;
    }
    loop::viewUpdateCamera(S.viewMode);
    sprAdvanceAnimClocks();
    evtMissionTick();
    return L.orbitAngle;
}

void drawCampView(const View& view, int heading, int zoom) {
    if (!g_worldLoaded) {
        gfx().fillRect(view.x, view.y, view.w, view.h, u16(Gfx::kSolid | 0x00));
        return;
    }
    loop::LoopState& L = loop::ls();
    L.orbitAngle = s16(heading);
    L.orbitDistCraft = zoom;
    Camera& cam = L.camSmall;
    cam.rect_x = view.x;
    cam.rect_y = view.y;
    cam.rect_w = view.w;
    cam.rect_h = view.h;
    L.cur = &cam;
    Gfx& gx = gfx();
    gx.setClip(view.x, view.y, view.w, view.h);
    loop::viewUpdateRenderContext();
    render::todDrawSkyGround(cam, g().detailLevel, ms().time, ms().todHour);
    render::renderView(cam, true);
    gx.clipFull();
}

} // namespace st::game::front::hooks
