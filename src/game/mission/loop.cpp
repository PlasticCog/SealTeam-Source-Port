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
#include "game/mission/geo.h"
#include "game/mission/mission.h"
#include "game/mission/people.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/gfx.h"
#include "render/sky.h"

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

// One iteration of the frame loop (1000:01DE, 6.2).
void frame() {
    LoopState& L = ls();
    MissionState& S = ms();
    auto& in = engine::input();
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
    if (S.tcState == 0) simFrameClock(clkRawTicks());
    else if (S.tcState == 1) simFrameClockCompressed();
    if (S.viewMode != 1 || S.opt.map != 0) simFrameAnimate();
    if (S.viewMode >= 2 && S.viewMode != 0x0C) in.resetButton2Timers();
    const bool mapView = S.viewMode == 1 || S.viewMode == 0x0C;
    in.setMode(mapView ? engine::InputMode::Map : engine::InputMode::Action);
    int key = loopGetKey();
    int dx = 0, dy = 0;
    in.getMotion(dx, dy);
    // Port: the mouse wheel zooms the map and the chase / team cameras.
    if (const int wheel = sys().input().takeWheel()) {
        if (S.viewMode == 1) mapWheelZoom(wheel);
        else if (!mapView && !L.misFreeze && S.tcState == 0) fieldViewWheelZoom(wheel);
    }
    if (cmdOrderKeys(key)) key = 0;
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
        const int fade = simFrameUpdate();
        if (fade >= 0) engine::paletteFade().request(fade, S.frameTicks);
    }
    simMsgTick();
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
