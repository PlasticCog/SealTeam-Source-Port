// Mission input of segment 19ac: input_toggle_keys (0B2A), cmd_order_keys
// (0CC3, the loop's part: Enter/Esc, Alt-I/P/X, F1..F10 - the order letters
// and Alt-T/U are the simulation's playerOrderKey), field_pointer_motion
// (1816) and field_view_keys (1A79). docs/re/seg_19ac.md 5-6.
#include "game/mission/loop.h"

#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/sound.h"
#include "game/globals.h"
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/msg.h"
#include "game/mission/sfx.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/ui.h"
#include "render/veg.h"

namespace st::game::mission {
namespace loop {

namespace {

constexpr u16 kMsgSfxOff = 0x02C2;     // "Sound Effects Off"
constexpr u16 kMsgSfxOn = 0x02D4;      // "Sound Effects On"
constexpr u16 kMsgMusicOff = 0x02E5;   // "Music Off"
constexpr u16 kMsgMusicOn = 0x02EF;    // "Music On "
constexpr u16 kMsgPaused = 0x04D9;     // "Game Paused. Press Enter key to Continue."

constexpr int kKeyTab = 0x09;
constexpr int kKeyAltN = 0x3100;
constexpr int kKeyAltT = 0x1400;
constexpr int kKeyAltU = 0x1600;
constexpr int kKeyAltI = 0x1700;
constexpr int kKeyAltP = 0x1900;
constexpr int kKeyF1 = 0x3B00, kKeyF2 = 0x3C00, kKeyF3 = 0x3D00, kKeyF8 = 0x4200, kKeyF9 = 0x4300;
constexpr int kKeyHome = 0x4700, kKeyPgUp = 0x4900, kKeyCenter = 0x4C00, kKeyEnd = 0x4F00, kKeyPgDn = 0x5100;
constexpr int kKeyCtrlLeft = 0x7300, kKeyCtrlRight = 0x7400, kKeyCtrlPgDn = 0x7600, kKeyCtrlPgUp = 0x8400;

// g_view_heading += d (math_angle_add 2255:621E on DS:CEAC).
void viewHeadingAdd(int d) {
    LoopState& L = ls();
    L.viewHeading = s16(angleWrap(L.viewHeading + d));
}

// g_view_distance -/+ 6 within 0x60..900 (32-bit value).
void zoomIn() {
    LoopState& L = ls();
    L.viewDistance = wrapSub(L.viewDistance, 6);
    if (L.viewDistance < 0x60) L.viewDistance = 0x60;
}
void zoomOut() {
    LoopState& L = ls();
    L.viewDistance = wrapAdd(L.viewDistance, 6);
    if (L.viewDistance > 900) L.viewDistance = 900;
}

// Open the map from a field view (space / 'm').
void openMap() {
    if (ms().opt.map == 0) clkSave();
    sfxStopAll();
    viewEnterMap();
}

// field_pointer_motion (19ac:1816): returns the key (0 when consumed).
int fieldPointerMotion(int key, int dx, int dy) {
    LoopState& L = ls();
    MissionState& S = ms();
    Unit* pm = pointMan();
    const int mode = S.viewMode;
    if (Team* vt = team(L.viewTeam)) {
        L.viewDistance = s32(vt->view_distance);
        L.viewHeading = vt->view_heading;
    }
    if (dx != 0) {
        if ((mode == 2 && key == engine::key::Space) || mode > 2) {
            key = 0;
            viewHeadingAdd(dx > 0 ? -80 : 80);
        } else if (mode != 0 || !S.grenadeAiming) {
            unitTurn(pm, -dx);
        } else {
            playerAdjustGrenadeAim(dx < 0 ? -1 : 1, 0, true);
        }
    }
    if (dy != 0) {
        if (mode < 2) {
            if (mode == 0 && key == engine::key::Space) {
                key = 0;
                if (!pm) return 0;
                const int p = u8(pm->mover->posture);
                if (dy < 0) {
                    if (p > 1) return 0;
                    playerSetPosture(p + 1, true);
                } else {
                    if (p == 0) return 0;
                    playerSetPosture(p - 1, true);
                }
            } else if (mode == 0 && S.grenadeAiming) {
                playerAdjustGrenadeAim(0, dy < 0 ? 1 : -1, true);
            }
        } else if (key == engine::key::Space || mode > 2) {
            key = 0;
            if (dy < 0) zoomIn();
            else zoomOut();
        }
    }
    return key;
}

} // namespace

// ---------------------------------------------------------------------------
// input_toggle_keys (19ac:0B2A)
// ---------------------------------------------------------------------------

bool inputToggleKeys(int key) {
    Globals& gs = g();
    MissionState& S = ms();
    switch (key) {
    case engine::key::AltS:
        gs.sfxOn = !gs.sfxOn;
        engine::sound().setSfxEnabled(gs.sfxOn);
        if (gs.sfxOn) {
            msgShowDs(kMsgSfxOn, 0x80);
        } else {
            sfxStopAll();
            engine::sound().resetChannels();
            msgShowDs(kMsgSfxOff, 0x80);
        }
        return true;
    case engine::key::AltD:
        gs.detailMsgTime = wrapAdd(S.time, 0x100);
        gs.detailLevel = (gs.detailLevel + 1) % 6;
        S.detailLevel = gs.detailLevel;
        if (gs.detailLevel == 0) {
            viewVegReset();
        } else if (gs.detailLevel > 4) {
            render::vegForgetCoverPosition();  // DS:ED18..ED1E = 0
            viewVegUpdate(true);
        }
        return true;
    case engine::key::AltM:
        if (!engine::sound().musicEnabled()) {
            engine::sound().setMusicEnabled(true);
            gs.musicOn = true;
            msgShowDs(kMsgMusicOn, 0x80);
            engine::sound().play(0);
        } else {
            msgShowDs(kMsgMusicOff, 0x80);
            engine::sound().stop();
            engine::sound().setMusicEnabled(false);
            gs.musicOn = false;
        }
        return true;
    default:
        return false;
    }
}

// ui_resume_after_overlay (19ac:0CA7)
void uiResumeAfterOverlay() {
    cursorReset();
    if (ms().opt.map == 0) {
        clkRestore();
        engine::input().resetRepeatTimers();
    }
}

// ---------------------------------------------------------------------------
// cmd_order_keys (19ac:0CC3)
// ---------------------------------------------------------------------------

bool cmdOrderKeys(int key) {
    LoopState& L = ls();
    MissionState& S = ms();
    if (inputToggleKeys(key)) return true;
    switch (key) {
    case engine::key::Enter:
        if (S.viewMode == 7) misInsertionClear();
        return false;
    case engine::key::Esc: {
        if (S.viewMode == 7) {
            misInsertionClear();
            return true;
        }
        sfxStopAll();
        clkSave();
        engine::paletteFade().suspend();
        if (S.viewMode != 1) cursorReset();
        if (uiConfirmEndMission()) {
            entTeamRejoin();
            S.misDone = true;  // g_mission_aborted (the same byte as g_mis_done)
        }
        engine::paletteFade().resume();
        clkRestore();
        L.fullRedraw = 2;
        return true;
    }
    case kKeyAltI:
        L.teamInfoNames = !L.teamInfoNames;
        return true;
    case kKeyAltP: {
        clkSave();
        std::string buf;
        uiDialogPrompt(dsText(kMsgPaused), buf, 0x4C, 0x5C, 0, false, false);
        clkRestore();
        L.skipPresent = true;
        L.fullRedraw = 2;
        return true;
    }
    case engine::key::AltX:
        if (S.viewMode != 1) cursorReset();
        S.quitGame = uiConfirmExitDos();
        return true;
    case kKeyF1:
        if (S.extracting) return true;
        viewSetFirstPerson();
        uiResumeAfterOverlay();
        return true;
    case kKeyF2:
        if (S.extracting) return true;
        viewSetChase();
        uiResumeAfterOverlay();
        return true;
    case kKeyF9:
        if (S.extracting) return true;
        viewSetTargetCamera();
        uiResumeAfterOverlay();
        return true;
    case engine::key::F10:
        if (S.extracting) return true;
        if (S.gameMode == GameMode::Practice || S.gameMode == GameMode::Demo) {
            if (S.viewMode == 0x0E) viewNextEnemy();
            viewSetMode(0x0E);
        }
        uiResumeAfterOverlay();
        return true;
    default:
        break;
    }
    if (key >= kKeyF3 && key <= kKeyF8) {
        viewSetTeamCamera((key - kKeyF3) >> 8);
        uiResumeAfterOverlay();
        return true;
    }
    return playerOrderKey(key);
}

// ---------------------------------------------------------------------------
// field_view_keys (19ac:1A79)
// ---------------------------------------------------------------------------

void fieldViewKeys(int key, int dx, int dy) {
    LoopState& L = ls();
    MissionState& S = ms();
    Unit* pm = pointMan();
    if (!pm) return;
    playerViewPrologue(dx);
    key = fieldPointerMotion(key, dx, dy);
    const int mode = S.viewMode;
    const bool fieldView = mode < 3;  // views 0..2: point man, (map), team
    switch (key) {
    case kKeyTab:
        tgtAcquire(pm, S.playerTarget, S.playerTarget.kind != TargetKind::None);
        break;
    case engine::key::Enter:
        playerFire();
        break;
    case engine::key::Space:
        if (!S.extracting && mode == 0) openMap();
        break;
    case 'm':
        if (!S.extracting) openMap();
        break;
    case '+':
    case '=':
        if (u8(pm->mover->posture) != 0 && unitAlive(pm)) playerSetPosture(u8(pm->mover->posture) - 1);
        break;
    case '-':
        if (u8(pm->mover->posture) <= 1 && unitAlive(pm)) playerSetPosture(u8(pm->mover->posture) + 1);
        break;
    case '1':
        if (unitAlive(pm)) playerSetPosture(2);
        break;
    case '2':
        if (unitAlive(pm)) playerSetPosture(1);
        break;
    case '3':
        if (unitAlive(pm)) playerSetPosture(0);
        break;
    case '[':
        itemUseTool(pm);
        S.toolInfoTime = wrapAdd(S.time, 0x200);
        break;
    case ']':
        itemCycleTool(pm);
        S.toolInfoTime = wrapAdd(S.time, 0x200);
        break;
    case 'g':
        playerThrowGrenade();
        break;
    case 'n':
        wpnSelectNextWeapon(pm);
        tgtAcquire(pm, S.playerTarget, false);
        S.weaponInfoTime = wrapAdd(S.time, 0x200);
        break;
    case kKeyAltN:
        wpnSelectNextGrenade(pm);
        tgtAcquire(pm, S.playerTarget, false);
        S.grenadeInfoTime = wrapAdd(S.time, 0x200);
        break;
    case 'x':
        if (wldOpenHutAhead(pm)) msgHandSignal(pm, 3);
        break;
    case kKeyHome:
        if (fieldView) evtPlayerSpeedStep(0xC00);
        [[fallthrough]];
    case engine::key::Left:
        if (!fieldView) viewHeadingAdd(80);
        else if (!S.grenadeAiming) unitTurn(pm, 3);
        else playerAdjustGrenadeAim(-1, 0);
        break;
    case kKeyPgUp:
        if (fieldView) evtPlayerSpeedStep(0xC00);
        [[fallthrough]];
    case engine::key::Right:
        if (!fieldView) viewHeadingAdd(-80);
        else if (!S.grenadeAiming) unitTurn(pm, -3);
        else playerAdjustGrenadeAim(1, 0);
        break;
    case engine::key::Up:
        if (!fieldView) zoomIn();
        else if (S.grenadeAiming) playerAdjustGrenadeAim(0, 1);
        else evtPlayerSpeedStep(0xC00);
        break;
    case kKeyPgDn:
        if (fieldView) unitTurn(pm, -3);
        [[fallthrough]];
    case engine::key::Down:
        if (!fieldView) zoomOut();
        else if (S.grenadeAiming) playerAdjustGrenadeAim(0, -1);
        else evtPlayerSpeedStep(-0xC00);
        break;
    case kKeyEnd:
        if (fieldView) {
            unitTurn(pm, 3);
            evtPlayerSpeedStep(-0xC00);
        }
        break;
    case kKeyCenter:
        if (fieldView) evtPlayerSpeedStep(0);
        break;
    case kKeyCtrlLeft:
        if (mode > 1) viewHeadingAdd(80);
        else unitTurn(pm, 15);
        break;
    case kKeyCtrlRight:
        if (mode > 1) viewHeadingAdd(-80);
        else unitTurn(pm, -15);
        break;
    case kKeyCtrlPgDn:
        if (mode >= 2) zoomOut();
        break;
    case kKeyCtrlPgUp:
        if (mode >= 2) zoomIn();
        break;
    default:
        break;
    }
    if (Team* vt = team(L.viewTeam)) {
        vt->view_distance = s16(L.viewDistance);
        vt->view_heading = L.viewHeading;
    }
}

} // namespace loop
} // namespace st::game::mission
