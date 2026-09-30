// Intel Briefing / Practice Mission screen (365e:7290..7B3E,
// docs/re/seg_365e_a.md 9.4): zooming area map with mission markers,
// calendar, and the NILO's intel lines in the text bar.
#include "game/front/front.h"

#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/sound.h"
#include "engine/ticker.h"
#include "game/campaign.h"
#include "game/front/common.h"
#include "game/front/panels.h"
#include "game/globals.h"
#include "game/screens.h"
#include "game/title.h"
#include "game/ui.h"
#include "gfx/gfx.h"

namespace st::game::front {

namespace {

constexpr u16 kButtonSeg = 0x51b7;       // +0 buttons, +0x50 mission map positions (80 x {x, y})
constexpr u16 kLabelTable = 0x249e;
constexpr u16 kSpeaker = 0x3f1a;         // "NILO"
constexpr u16 kPracticeHint = 0x3f1f;    // "Change Date or Year for different missions."
constexpr u16 kTitlePractice = 0x3f4b;   // " Practice Mission  "
constexpr u16 kTitleIntel = 0x3f5f;      // " Intel Briefing  "
constexpr u16 kPanelText = 0x3f71;       // " "
constexpr u16 kMissionLabel = 0x3f73;    // "Mission:"
constexpr int kNextButton = 3;
constexpr int kMusic = 7;

ButtonList g_buttons;
SpriteSet g_map("map");
std::vector<std::string> g_text;  // cYmNN.s
int g_mapIndex = 0;               // g_intel_map_index
int g_zoom = 0;                   // g_intel_zoom (0..0x100)
int g_zoomHold = 0;               // g_intel_zoom_hold
s32 g_textTime = 0;               // g_intel_text_time
int g_textLine = 0;               // g_intel_text_line
bool g_textOn = false;            // g_intel_text_on
bool g_done = false;
s8 g_practiceMission = -1, g_practiceYear = -1;  // DS:3F18 / 3F19

bool practice() { return g().gameMode == GameMode::Practice; }

std::string line(int i) { return i >= 0 && i < int(g_text.size()) ? g_text[size_t(i)] : std::string(); }

// intel_next_text_line (365e:7375). In a campaign lines 5-6 (previous
// mission won) or 7-8 (lost) come first, then the briefing lines 0-4. The
// original reads "won" through the leader of the previous mission's team;
// the port uses the point man's last history entry (see docs/front.md).
void nextTextLine() {
    const CampaignHeader& h = campaign::header();
    bool won = false;
    if (practice()) {
        if (g_textLine < 2) g_textLine = 2;
    } else if (const RosterEntry* pm = campaign::rosterFind(h.point_man)) {
        int i = pm->missions - 1;
        if (i < 0) i = 0;
        won = h.history[std::min(i, 23)].won != 0;
    }
    std::string text;
    for (;;) {
        const int s = g_textLine;
        if (s <= 1) text = line((won ? 5 : 7) + s);
        else if (s <= 6) text = line(s - 2);
        else text = line(0);
        if (g_textLine < 7) ++g_textLine;
        if (!text.empty() || g_textLine >= 7) break;
    }
    if (!text.empty() && g_textLine <= 7) msg::say(exe().dgString(kSpeaker), text);
    if (g_textLine >= 7) {
        if (g_textLine == 7) ++g_textLine;
        if (msg::count() <= 1) {
            g_textOn = false;
            g_textLine = 0;
        }
    }
}

void textTick() {
    if (now() - g_textTime >= 0x800) {
        g_textTime = now();
        if (g_textTime != 0) nextTextLine();
    }
}

// intel_text_restart (365e:74B5).
void textRestart() {
    mouseRecenter();
    ui().redrawFrames = 2;
    g_textTime = now() - 0x800;
    g_textLine = 0;
    g_textOn = true;
    msg::clear();
}

void loadMission() {
    CampaignHeader& h = campaign::header();
    g().missionNo = h.year * 20 + h.mission;
    campaign::missionLoad();
    campaign::missionFreeMtm();
}

// intel_draw_mission_markers (365e:7873): the other missions of the year
// (practice only) and the blinking current one.
void drawMarkers(int x0, int y0, bool all) {
    Gfx& gx = gfx();
    const CampaignHeader& h = campaign::header();
    const int cur = h.year * 20 + h.mission;
    auto pos = [](int i, int& x, int& y) {
        const u8* p = exe().at(kButtonSeg, u16(0x50 + 4 * std::min(i, 0x4f)));
        x = p ? rds16(p) : 0;
        y = p ? rds16(p + 2) : 0;
    };
    int px, py;
    if (all) {
        for (int i = h.year * 20; i < (h.year + 1) * 20; ++i) {
            if (i == cur) continue;
            pos(i, px, py);
            gx.rect(x0 + px - 2, y0 + py - 1, 4, 3, u16(0xff02));
            gx.rect(x0 + px - 3, y0 + py - 2, 4, 3, u16(0xff0a));
        }
    }
    pos(cur, px, py);
    gx.rect(x0 + px - 3, y0 + py - 2, 6, 4, u16(0xff04));
    gx.rect(x0 + px - 4, y0 + py - 3, 6, 4, u16((now() & 0x40) ? 0xff0c : 0xff0f));
}

// intel_draw_calendar (365e:76C8).
void drawCalendar() {
    const CampaignHeader& h = campaign::header();
    fontSelect(FontId::Title);
    const FlowEntry& f = campaign::flow(h.year, h.mission);
    if (practice()) uiDrawTitleTab(exe().dgString(kTitlePractice), 0x64, 1);
    else uiDrawTitleTab(exe().dgString(kTitleIntel), 0x68, 1);
    std::string month = tableString(strtab::kMonths, f.month);
    if (month.size() > 3) month.resize(3);
    drawTextShadow(0x110, 0x7a, month, 0x11, 4);
    drawTextShadow(0x118, 0x8a, std::to_string(h.mission + 1), 0x11, 4);
    const std::string year = std::to_string(1966 + h.year);
    g_buttons[0].label = year;
    drawTextShadow(0x110, 0x9a, year, 0x0f, 0);
    uiDrawTextPanel(exe().dgString(kPanelText).c_str(), 0xf4, 0x2c, 0x40, 0x20);
    fontSelect(FontId::Title);
    drawTextShadow(0xfc, 0x30, exe().dgString(kMissionLabel), 0, 0x0f);
    if (g_zoom != 0)
        drawTextShadow(0xfc, 0x40, tableString(strtab::kObjectiveNames, int(campaign::mci().objective[0].kind)), 0x0f, 0);
    msg::draw();
}

// intel_render (365e:798A).
void render() {
    Gfx& gx = gfx();
    UiState& u = ui();
    if (u.redrawFrames != 0) {
        --u.redrawFrames;
        picBlitToScreen();
        gx.fillRect(0, 176, 320, 24, u16(0xff00));
        const u8* corner = g_map.frame(0, 4);
        drawSprite(corner, 0x138 - SpriteSet::width(corner), 0xaa - SpriteSet::height(corner));
    } else {
        engine::ticker().frameLimitWait();
        cursorErase();
    }
    gx.fillRect(0, 176, 320, 24, u16(0xff00));
    gx.clipFull();
    // Zoom the area map in, then keep it on both pages for two frames.
    if (g_zoomHold == 0 && g_zoom < 0x100) g_zoomHold = -1;
    g_zoom = std::min(g_zoom + 2 * engine::ticker().frameDt(), 0x100);
    if (g_zoomHold == -1 && g_zoom == 0x100) g_zoomHold = 2;
    const u8* map = g_map.frame(0, g_mapIndex);
    const int w = (SpriteSet::width(map) * g_zoom) >> 8;
    const int h = (SpriteSet::height(map) * g_zoom) >> 8;
    if (g_zoomHold > 0 || g_zoom < 0x100) {
        if (map) gx.spriteCentered(map, 0x78, 0x60, w, h);
        if (g_zoomHold > 0) --g_zoomHold;
    }
    if (g_zoomHold == 0 && g_zoom == 0x100) drawMarkers(0x78 - w / 2, 0x60 - h / 2, practice());
    drawCalendar();
    uiDrawButtons(g_buttons);
    engine::ticker().frameLimitWait();
    cursorDraw();
}

int handleInput(int key, int dx, int dy);

// Year ('1'/'2') and date ('d'/'f') changes of the practice calendar.
void changeMission(int key) {
    CampaignHeader& h = campaign::header();
    cursorSetWait(true);
    render();
    present();
    cursorReset();
    uiPointerUpdate(0, 0, g_buttons);
    if (key == '1' || key == '2') {
        if (key == '1') h.year = u8((h.year + 1) % 4);
        else h.year = u8(h.year == 0 ? 3 : h.year - 1);
        g_practiceYear = s8(h.year);
        h.mission = 0;
        g_practiceMission = 0;
    } else {
        if (key == 'd') h.mission = u8((h.mission + 1) % 20);
        else h.mission = u8(h.mission == 0 ? 19 : h.mission - 1);
        g_practiceMission = s8(h.mission);
        g_practiceYear = s8(h.year);
    }
    loadMission();
    campaign::loadMissionText(h.year * 20 + h.mission, g_text);
    msg::clear();
    textRestart();
    ui().redrawFrames = 2;
    g_zoom = 0;
    g_mapIndex = areaRegion(campaign::mci().world);
}

// intel_handle_input (365e:7B3E).
int handleInput(int key, int dx, int dy) {
    UiState& u = ui();
    int result = 2;
    if (dx != 0 || dy != 0) uiPointerUpdate(dx, dy, g_buttons);
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0 && u.focus != -1)
        handleInput(g_buttons[size_t(u.focus)].key, 0, 0);
    if (inputToggleKeys(key) || uiMenuArrowKeys(key, g_buttons)) return 2;
    switch (key) {
    case engine::key::Enter:
        if (u.focus != -1 && g_buttons[size_t(u.focus)].key != engine::key::Enter) u.pressed = true;
        break;
    case engine::key::Esc:
        result = 1;
        g_done = true;
        break;
    case engine::key::Space:
        if (u.focus == 0) handleInput('2', 0, 0);
        else if (u.focus == 1) handleInput('f', 0, 0);
        else {
            nextTextLine();
            g_textTime = now();
            msg::skip();
        }
        break;
    case '1':
    case '2':
    case 'd':
    case 'f':
        if (practice()) changeMission(key);
        break;
    case 'n':
        g_done = true;
        break;
    case engine::key::AltX:
        result = 0;
        g_done = true;
        break;
    default:
        break;
    }
    return result;
}

} // namespace

int intelScreen() {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    auto& snd = engine::sound();
    CampaignHeader& h = campaign::header();
    if (g_buttons.empty()) g_buttons = loadButtonList(kButtonSeg, kLabelTable);
    if (practice() && g_practiceMission != -1) {
        h.mission = u8(g_practiceMission);
        h.year = u8(g_practiceYear);
    }
    clock.resetClock();
    // (A "campaign over -> demo mode" branch follows here in the original
    // but can never be taken: the byte is compared with -1 after zero
    // extension.)
    loadMission();
    int result = 2;
    // intel_enter
    picLoad(PicIntel);
    g_map.free();
    g_mapIndex = areaRegion(campaign::mci().world);
    ui().focus = kNextButton;
    uiCursorToButton(g_buttons[kNextButton]);
    for (int b = 0; b < 2; ++b) {
        if (practice()) g_buttons[size_t(b)].flags |= btn::Clickable;
        else g_buttons[size_t(b)].flags &= u8(~btn::Clickable);
    }
    campaign::loadMissionText(h.year * 20 + h.mission, g_text);
    g_textTime = now();
    ui().redrawFrames = 2;
    msg::reset();
    if (practice()) msg::add(exe().dgString(kPracticeHint), 1, 0x300, 0, 0);
    else textRestart();
    snd.loadMusic(kMusic, 0);
    if (g().gameMode == GameMode::Demo) {
        g_done = true;
    } else {
        in.resetRepeatTimers();
        in.flushKeyboard();
        in.setMode(engine::InputMode::Menu);
        screenTransition(PalIntel, true);
        g_done = false;
    }
    g_zoomHold = 0;
    g_zoom = 0;
    clock.resetClock();
    while (!g_done) {
        snd.startOnce(0);
        engine::paletteFade().request(0, clock.frameDt());
        render();
        present();
        clock.updateGameTime();
        msg::tick();
        textTick();
        if (!g_textOn && now() - g_textTime >= 0x3000) {
            g_textTime = now();
            if (g_textTime != 0) textRestart();
        }
        const int key = getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        result = handleInput(key, dx, dy);
    }
    if (g().gameMode != GameMode::Demo) {
        cursorSetWait(true);
        render();
        present();
    }
    snd.stop();
    snd.unload();
    // intel_leave
    msg::clear();
    cursorReset();
    g_text.clear();
    g_map.free();
    g_mapIndex = 0;
    return result;
}

} // namespace st::game::front
