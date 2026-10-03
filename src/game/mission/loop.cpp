// mis_run (1000:01DE) and mis_cleanup (1000:0790): the in-mission loop of the
// original, docs/re/seg_1000.md 6. The set-up and the world part of every
// frame live in the simulation (sim.h); this file keeps the exact frame order
// of the original around them: render, clock, animation, input, world update,
// palette fade, message tick. Time compression (Alt-T) states 0..3 and their
// partial redraws, the four cleared start frames, the pause and the clock
// helpers (1000:2FEA..314B) are here as well.
#include "game/mission/loop.h"

#include "platform/system.h"

#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/sound.h"
#include "engine/ticker.h"
#include "game/front/common.h"
#include "game/globals.h"
#include "game/mission/campaign_link.h"
#include "game/mission/combat.h"
#include "game/mission/craft.h"
#include "game/mission/entity.h"
#include "game/mission/geo.h"
#include "game/mission/mission.h"
#include "game/mission/people.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/gfx.h"
#include "render/r3d.h"
#include "render/sky.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>

namespace st::game::mission {
namespace loop {

namespace {
bool g_missionLoaded = false;
}

// ---------------------------------------------------------------------------
// Game clock (1000:2FEA..314B). g_time is ms().time; the raw 256 Hz counter
// g_ticks is the ticker's count minus a base that clk_reset / clk_resync_raw /
// clk_restore move (the ticker itself keeps running for the input layer).
// ---------------------------------------------------------------------------

s32 clkRawTicks() { return s32(engine::ticker().ticks()) - ls().tickBase; }

void clkReset() {
    LoopState& L = ls();
    MissionState& S = ms();
    L.tickBase = s32(engine::ticker().ticks());
    S.time = 0;
    S.frameTicks = 1;
    L.savedTime = -1;
    // Original quirk (1000:30C9): g_prev_time is not reset, the next
    // clk_update sees one big (16-bit wrapped) frame.
}

void clkResyncRaw() { ls().tickBase = s32(engine::ticker().ticks()) - ms().time; }

void clkSave() { ls().savedTime = ms().time; }

void clkRestore() {
    LoopState& L = ls();
    if (L.savedTime == -1) return;
    ms().time = L.savedTime;
    L.tickBase = s32(engine::ticker().ticks()) - L.savedTime;
    L.savedTime = -1;
}

// gfx_present (1000:1E87): full clip, flip with the 5-tick latch wait, frame limiter reset.
void presentFrame() { present(); }

// page_copy_full (1000:1EA7)
void pageCopyFull() { gfx().copyPage(gfx().displayPage(), gfx().drawPage()); }

int loopGetKey() { return front::getKey(); }

// ---------------------------------------------------------------------------
// mis_run
// ---------------------------------------------------------------------------

namespace {

void installHooks() {
    LoopState& L = ls();
    SimHooks hooks;
    hooks.viewModeChanging = viewSelectCamera;
    hooks.viewModeChanged = viewModeStored;
    hooks.clockResync = clkResyncRaw;
    hooks.fullRedraw = [] { ls().fullRedraw = 2; };
    addCampaignHooks(hooks);
    setSimHooks(hooks);
    setVegetationHook(viewVegUpdate);
    setVegetationResetHook(viewVegReset);
    setGroundFxCameraHook([] { return ls().cur ? ls().cur->pos : Vec3{}; });
    craftSetViewerPos(&L.curPos);
    setViewHeadingTurnHook([](int d8) { ls().viewHeading = s16(angleWrap(ls().viewHeading + d8)); });
    viewInstallRenderHooks();
}

// Port (dev): with ST_PERFLOG set, every 2 s of wall time one log line with
// the frame time split into render (which includes the frame limiter's
// wait in tod_draw_sky_ground), present and simulation, and the counts that
// grow during a mission. Off (one getenv per mission) otherwise.
struct PerfLog {
    bool on = false;
    double start = 0, frameStart = 0, renderMs = 0, presentMs = 0, simMs = 0, worstMs = 0;
    double animMs = 0, keyMs = 0, updMs = 0;
    int frames = 0;
    double now() const { return double(SDL_GetPerformanceCounter()) * 1000.0 / double(SDL_GetPerformanceFrequency()); }
    void report() {
        const double t = now();
        if (t - start < 2000.0 || frames == 0) return;
        const MissionState& S = ms();
        int prj = 0;
        for (const Projectile* p : S.projectiles)
            if (p && (p->state & prj_state::kInUse)) ++prj;
        int alive = 0, dead = 0;
        for (int ti = 0; ti < kMaxTeams && S.teams[ti]; ++ti)
            for (int mi = 0; mi < 8 && S.teams[ti]->members[mi]; ++mi) (unitAlive(S.teams[ti]->members[mi]) ? alive : dead)++;
        int sfx = 0;
        for (int n = 0; n < engine::kSfxChannels; ++n)
            if (engine::sound().channel(n).flags) ++sfx;
        const auto& rs = render::renderStats();
        logInfo("perf: time %d  %d frames %.1f fps  frame avg %.1f max %.1f ms  render %.1f present %.1f sim %.1f ms  "
                "(anim %.1f key %.1f update %.1f)  drawn %d listed %d  projectiles %d  units %d alive %d dead  sfx %d",
                S.time, frames, frames * 1000.0 / (t - start), (t - start) / frames, worstMs, renderMs / frames,
                presentMs / frames, simMs / frames, animMs / frames, keyMs / frames, updMs / frames, rs.drawn, rs.listed, prj, alive, dead, sfx);
        start = t;
        frames = 0;
        renderMs = presentMs = simMs = worstMs = animMs = keyMs = updMs = 0;
    }
};
PerfLog g_perf;

// One iteration of the frame loop (1000:01DE, 6.2).
void frame() {
    LoopState& L = ls();
    MissionState& S = ms();
    auto& in = engine::input();
    PerfLog& pf = g_perf;
    const double fStart = pf.on ? pf.now() : 0;
    engine::ticker().updateGameTime();  // the ticker's own clock (input repeat timers)
    switch (S.tcState) {
    case 0:
        engine::sound().startOnce(musGetMood());
        viewUpdateCamera(S.viewMode);
        viewVegUpdate(false);
        viewRenderFrame();
        break;
    case 1:
        hudDrawClockBox();
        presentFrame();
        break;
    case 2:
        pageCopyFull();
        msgDrawQueue();
        S.tcState = 1;
        presentFrame();
        pageCopyFull();
        break;
    default:  // 3: one more full frame before the clock box only
        viewUpdateCamera(S.viewMode);
        viewRenderFrame();
        msgDrawQueue();
        S.tcState = 2;
        presentFrame();
        pageCopyFull();
        break;
    }
    const double fRendered = pf.on ? pf.now() : 0;
    if (S.tcState == 0) simFrameClock(clkRawTicks());
    else if (S.tcState == 1) simFrameClockCompressed();
    if (S.viewMode != 1 || S.opt.map != 0) simFrameAnimate();
    const double fAnim = pf.on ? pf.now() : 0;
    if (S.viewMode >= 2 && S.viewMode != 0x0C) in.resetButton2Timers();
    const bool mapView = S.viewMode == 1 || S.viewMode == 0x0C;
    in.setMode(mapView ? engine::InputMode::Map : engine::InputMode::Action);
    int key = loopGetKey();
    const double fKey = pf.on ? pf.now() : 0;
    int dx = 0, dy = 0;
    in.getMotion(dx, dy);
    // Port: the mouse wheel zooms the map and the chase / team cameras.
    if (const int wheel = sys().input().takeWheel()) {
        if (S.viewMode == 1) mapWheelZoom(wheel);
        else if (!mapView && !L.misFreeze && S.tcState == 0) fieldViewWheelZoom(wheel);
    }
    if (cmdOrderKeys(key)) key = 0;
    const double fPresent0 = pf.on ? pf.now() : 0;
    if (S.tcState == 0) {
        if (L.skipPresent) {
            L.skipPresent = false;
        } else if (L.startFrames == 0) {
            presentFrame();
        } else {
            gfx().clipFull();
            gfx().clear(0);
            presentFrame();
            if (--L.startFrames == 0) L.fullRedraw = 2;
        }
    }
    const double fPresented = pf.on ? pf.now() : 0;
    if (S.viewMode == 1 && S.opt.map == 0) {
        // "Map: Freeze": the world stands still while the map is up.
        mapScreenKeys(key, dx, dy);
    } else if (!L.misFreeze) {
        if (S.tcState == 0) {
            if (S.viewMode == 1) {
                mapScreenKeys(key, dx, dy);
            } else if (S.viewMode == 7 || S.viewMode == 0x0C) {
                insertKeys(key, dx, dy);
                if (S.viewMode == 7 && S.insertionClearTime < S.time) misInsertionClear();
            } else {
                fieldViewKeys(key, dx, dy);
            }
        }
        const double u0 = pf.on ? pf.now() : 0;
        const int fade = simFrameUpdate();
        if (pf.on) pf.updMs += pf.now() - u0;
        if (fade >= 0) engine::paletteFade().request(fade, S.frameTicks);
    }
    simMsgTick();
    if (pf.on) {
        const double fEnd = pf.now();
        pf.renderMs += fRendered - fStart;
        pf.presentMs += fPresented - fPresent0;
        pf.animMs += fAnim - fRendered;
        pf.keyMs += fKey - fAnim;
        pf.simMs += (fPresent0 - fRendered) + (fEnd - fPresented);
        pf.worstMs = std::max(pf.worstMs, fEnd - fStart);
        ++pf.frames;
        pf.report();
    }
}

} // namespace

} // namespace loop

// ---------------------------------------------------------------------------
// Public entry points (mission.h)
// ---------------------------------------------------------------------------

int run() {
    using namespace loop;
    LoopState& L = ls();
    if (!viewEnsureRenderer()) fatal("mission: renderer data missing");
    installHooks();
    logInfo("mission loop: start (t=%.1fs)", sys().timer().seconds());
    front::rebaseKeyScript("mission");
    // Loop state of mis_run's set-up (the simulation clears its own flags).
    L.fullRedraw = 0;
    L.skipPresent = false;
    L.misFreeze = false;
    L.startFrames = 4;
    L.savedTime = -1;
    L.teamInfoNames = false;
    L.mapCursorMode = 0;
    L.targetCamTeam = nullptr;
    L.prevViewMode = 0;
    L.viewTeam = 0;
    viewInitCameras();
    // hud_reset_mission (1000:1EB0), the values that do not need the world.
    L.orbitDist = 0xC0;
    L.orbitDistCraft = 0xC0;
    L.enemyViewIdx = -1;
    render::skyState().skyOfs = 0;
    render::skyState().groundOfs = 0;
    clkReset();
    if (!simInit(missionSetupFromCampaign())) return 0;  // mission data missing (msn_load NULL)
    // simInit replaces the MissionState object (resetMission): take the reference only now.
    MissionState& S = ms();
    g_missionLoaded = true;
    // The rest of hud_reset_mission, view_next_enemy, the time-of-day palette
    // and the sprite banks (mis_load_sprites: loaded by viewEnsureRenderer).
    hudResetMission();
    viewNextEnemy();
    const int pal = render::todPaletteIndex(S.todHour, S.todMinute);
    palLoad(pal);
    palApply();
    clkReset();  // clk_reset before the periodic timestamps (all 0 in simInit)
    engine::input().resetRepeatTimers();
    engine::input().flushKeyboard();
    engine::paletteFade().setLevel(0x100);  // pal_set_level(0x100): the frame loop fades in
    // The key reference (Ctrl+H) is handled like the pause dialog (Alt-P).
    uiSetOverlayHooks({clkSave, [] {
                           clkRestore();
                           ls().skipPresent = true;
                           ls().fullRedraw = 2;
                       }});
    // One present per page flip, like the CRTC showing a new display start
    // (gfx_present 1000:1E87). Without it every event pump in the frame (the
    // key poll, the frame limiter's wait) composed and presented the frame
    // again: with presents blocking on the display refresh, the mission ran
    // at half speed and the 256 Hz service that drives the music and effects
    // (delivered by the same pump) stalled - lag and late sound that grew
    // with the amount of drawing. The flag was meant to be set here since
    // the frame-pacing fix of 0.1 and never was; mis_run's exit clears it.
    sys().video().setExplicitPresent(true);
    g_perf = PerfLog{};
    g_perf.on = std::getenv("ST_PERFLOG") != nullptr;
    if (g_perf.on) g_perf.start = g_perf.now();
    while (!S.misDone && !S.quitGame) frame();
    uiSetOverlayHooks({});
    logInfo("mission loop: exit at %d ticks (t=%.1fs), done %d quit %d view %d", S.time, sys().timer().seconds(), S.misDone ? 1 : 0, S.quitGame ? 1 : 0, S.viewMode);
    // mis_run exit (1000:06F9): page copy, shake off, menu input mode, sprites, ai_shutdown.
    sys().video().setExplicitPresent(false);
    pageCopyFull();
    engine::ticker().shakeStop();
    engine::input().setMode(engine::InputMode::Menu);
    // 1000:076A-0787 leaves the palette fade level as it is (the front end's next
    // screen transition sets it), so no pal_set_level here.
    if (S.quitGame) {
        // Alt-X: the original exits to DOS without mis_cleanup.
        unload();
        return 1;
    }
    return 0;
}

void unload() {
    if (!loop::g_missionLoaded) return;
    loop::g_missionLoaded = false;
    simShutdown();  // ai_shutdown, snd_unload_sfx_bank, mis_free_world (+ the statistics hand-over)
}

} // namespace st::game::mission
