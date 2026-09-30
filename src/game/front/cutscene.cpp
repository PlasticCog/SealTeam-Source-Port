// Insertion and extraction cut-scenes at the SEAL Team camp: the orbiting
// camp camera with a two-line caption (365e:CEA8..D7D6,
// docs/re/seg_365e_b.md 9). The 3D camp itself is drawn by the hooks in
// hooks.h (campSceneLoad / campScenePlace / campSceneTick / drawCampView).
#include "game/front/front.h"

#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/sound.h"
#include "engine/ticker.h"
#include "engine/rng.h"
#include "game/campaign.h"
#include "game/front/common.h"
#include "game/front/hooks.h"
#include "game/front/panels.h"
#include "game/globals.h"
#include "game/screens.h"
#include "game/title.h"
#include "game/ui.h"
#include "gfx/gfx.h"

namespace st::game::front {

namespace {

constexpr u16 kButtonSeg = 0x5207;   // one button, no labels drawn
constexpr u16 kHours = 0x4580;       // " Hours       "
constexpr u16 kSpace1 = 0x458e;      // " "
constexpr u16 kSpace2 = 0x4590;      // " "
constexpr u16 kCampPrefix = 0x4592;  // "SEAL Team Camp, "
constexpr int kInsertionMusic = 10;
constexpr int kExtractionMusic = 12;
constexpr int kCampWorldBase = 0x1c;  // camp worlds 28..31 (g_world_index DS:ED16)
constexpr int kAngleFull = 0xb40;     // 360 degrees in 1/8 degree
constexpr s32 kZoomMin = 0x60, kZoomMax = 0x384;

ButtonList g_buttons;
hooks::View g_view;     // viewport 0xD88A
int g_angle = 0;        // g_orbit_angle (DS:D842)
s32 g_zoom = 0;         // DS:D846
s32 g_start = 0;        // scene start (DS:EF2C)
bool g_done = false;    // DS:EF2A

// snd_music_done (4592:1B86) is true only when the started sequence has
// finished (AIL status SEQ_DONE); engine::Sound::done() is also true before
// anything was started (the music starts only once the fade-in is over), so
// the cut-scene tracks the start itself.
bool g_musicStarted = false;

void musicStartOnce(int sequence) {
    auto& snd = engine::sound();
    if (!g_musicStarted && engine::paletteFade().level() == 0 && snd.musicEnabled()) g_musicStarted = true;
    snd.startOnce(sequence);
}

bool musicDone() { return g_musicStarted && engine::sound().done(); }

// math_angle_add (2255:621E).
void angleAdd(int delta) {
    int a = g_angle + delta;
    while (a < 0) a += kAngleFull;
    while (a >= kAngleFull) a -= kAngleFull;
    g_angle = a;
}

// cut_init_viewport (365e:D54A): view_init_camera(0xD88A, -1000, 0, 30, 0,
// 0, 0x18, 0x140, 0x8B).
void initViewport() {
    g_view = hooks::View{};
    g_view.pos = Vec3{-1000 * 256, 30 << 8, 0};
    g_view.x = 0;
    g_view.y = 0x18;
    g_view.w = 0x140;
    g_view.h = 0x8b;
}

// cut_init_camera (365e:D576) incl. cut_focus_button (365e:CEA9); the view
// mode (5 insertion, 6 extraction) belongs to campScenePlace.
void initCamera() {
    ui().focus = 0;
    uiCursorToButton(g_buttons[0]);
    g_angle = 0x690;
    g_zoom = 0x180;
    present();
}

// cut_queue_caption (365e:D010).
void queueCaption() {
    const CampaignHeader& h = campaign::header();
    const FlowEntry& f = campaign::flow(h.year, h.mission);
    std::string month = tableString(strtab::kMonths, f.month);
    if (month.size() > 3) month.resize(3);  // buf[3] = 0
    std::string line = padNumber(tod::hour(), 2) + padNumber(tod::minute(), 2) + exe().dgString(kHours) +
                       std::to_string(unsigned(h.mission) + 1) + exe().dgString(kSpace1) + month +
                       exe().dgString(kSpace2) + std::to_string(unsigned(h.year) + 1966);
    msg::add(line, 1, 0x1400, 0, 0);
    const int world = campaign::mci().world;
    msg::add(exe().dgString(kCampPrefix) + tableString(strtab::kLocations, kCampWorldBase + areaCampFrame(world)), 1, 0,
             0, 0);
}

// cut_insertion_timeout (365e:CEC2): leave 0x1E00 ticks after the start;
// once the music is over at most 0x200 more.
void insertionTimeout() {
    if (musicDone() && now() - g_start < 0x1c00) g_start = now() - 0x1c00;
    if (now() - g_start >= 0x1e00) {
        g_start = now();
        if (g_start != 0) g_done = true;
    }
    engine::paletteFade().request(0, engine::ticker().frameDt());
}

// cut_extraction_timeout (365e:CF50). The fade-out branch can not be reached
// (the start is reset whenever t >= 0x1400); kept as in the original.
void extractionTimeout() {
    if (musicDone() && now() - g_start < 0x1c00) g_start = now() - 0x1c00;
    if (now() - g_start >= 0x1400) {
        g_start = now();
        if (g_start != 0) g_done = true;
    }
    if (now() - g_start > 0x1c00) {
        engine::paletteFade().request(0x700, engine::ticker().frameDt());
        if (!musicDone()) engine::sound().stop();
    } else {
        engine::paletteFade().request(0, engine::ticker().frameDt());
    }
}

// cut_render (365e:D5B2): no cursor and no buttons are drawn.
void render() {
    Gfx& gx = gfx();
    UiState& u = ui();
    if (u.redrawFrames != 0) {
        gx.clear(0);
        --u.redrawFrames;
    } else {
        engine::ticker().frameLimitWait();
    }
    gx.clipFull();
    gx.fillRect(0, 176, 320, 24, u16(0xff00));
    msg::draw();  // font_select(propbold) + msg_draw; kind 1 draws in propbold
    gx.setClip(g_view.x, g_view.y, g_view.w, g_view.h);
    hooks::drawCampView(g_view, g_angle, int(g_zoom));
    gx.clipFull();
}

// cut_handle_input (365e:D3EF).
int handleInput(int key, int dx, int dy) {
    UiState& u = ui();
    int result = 2;
    if (dx != 0 || dy != 0) uiPointerUpdate(dx, dy, g_buttons);
    if (dx != 0) {
        key = 0;
        angleAdd((dx > 0 ? -10 : 10) * 8);
    }
    if (dy != 0) {
        key = 0;
        if (dy < 0) g_zoom = std::max<s32>(g_zoom - 6, kZoomMin);
        else g_zoom = std::min<s32>(g_zoom + 6, kZoomMax);
    }
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0 && u.focus != -1) handleInput(g_buttons[size_t(u.focus)].key, 0, 0);
    if (inputToggleKeys(key)) return 2;
    switch (key) {
    case engine::key::Enter:
        if (u.focus != -1 && g_buttons[size_t(u.focus)].key != engine::key::Enter) u.pressed = true;
        g_done = true;
        break;
    case engine::key::Esc:
    case 'n':
        g_done = true;
        break;
    case engine::key::AltX:
        result = 0;
        g_done = true;
        break;
    case engine::key::Left:
        angleAdd(0x50);
        break;
    case engine::key::Right:
        angleAdd(-0x50);
        break;
    default:
        break;
    }
    return result;
}

int run(bool extraction) {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    auto& snd = engine::sound();
    if (g_buttons.empty()) g_buttons = loadButtonList(kButtonSeg, 0);
    clock.resetClock();
    const MciHeader& m = campaign::mci();
    hooks::campSceneLoad(m.world);
    if (!extraction) {
        tod::set(m.start_hour, m.start_minute);
        tod::add(-1, -int(engine::rng().range(60)));
        hooks::campScenePlace(false);
    } else {
        // The original continues the mission's own clock (g_tod_hour/minute as
        // the mission left them); the front end keeps a separate clock, so the
        // mission end time is rebuilt from the MCI start + the elapsed mission
        // minutes, as the debriefing does (365e:B511).
        const int minutes = campaign::result().minutes;
        tod::set(m.start_hour, m.start_minute);
        tod::add(minutes / 60, minutes % 60);
        hooks::campScenePlace(true);
        tod::add(0, int(engine::rng().range(30)) + 30);
    }
    initViewport();
    initCamera();
    screenTransition(tod::paletteIndex(), true);
    hooks::campSceneTick();  // view_update_camera(g_view_mode)
    msg::reset();
    g_done = false;
    int result = 1;
    ui().redrawFrames = 2;
    clock.resetClock();
    in.resetRepeatTimers();
    in.flushKeyboard();
    in.setMode(engine::InputMode::Menu);
    g_start = now();
    queueCaption();
    int sequence = 0;
    g_musicStarted = false;
    if (!extraction) {
        snd.loadMusic(kInsertionMusic, 0);
    } else {
        snd.loadMusic(kExtractionMusic, 0);
        sequence = campaign::result().sealKia > 0 ? 2 : (campaign::missionWon(campaign::stats().score) ? 0 : 1);
    }
    while (!g_done) {
        musicStartOnce(sequence);
        if (extraction) extractionTimeout();
        else insertionTimeout();
        render();
        present();
        clock.updateGameTime();
        tod::update();
        // view_update_camera, spr_advance_anim_clocks, evt_mission_tick
        hooks::campSceneTick();
        const int key = getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        result = handleInput(key, dx, dy);
        msg::tick();
    }
    cursorSetWait(true);
    render();
    present();
    snd.stop();
    snd.unload();
    hooks::campSceneLeave();
    msg::clear();
    cursorReset();
    return result;
}

} // namespace

// cut_insertion_screen (365e:D285).
int insertionScreen() { return run(false); }

// cut_extraction_screen (365e:D659).
int extractionScreen() { return run(true); }

} // namespace st::game::front
