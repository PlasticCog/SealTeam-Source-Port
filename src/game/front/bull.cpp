// "SEAL Bull Session": four animated SEALs and a scripted conversation
// before the mission (365e:A862..AFDC, docs/re/seg_365e_b.md 8).
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

#include <cstring>

namespace st::game::front {

namespace {

constexpr u16 kBullSeg = 0x51f1;   // +0 buttons, +0x40 figure positions, +0x58 animation table
constexpr u16 kLabelTable = 0x24aa;
constexpr u16 kTitle = 0x41f3;     // " SEAL Bull Session  "
constexpr u16 kOic = 0x41ef;       // "OIC"
constexpr int kNextButton = 2;
constexpr int kMusic = 9;
constexpr int kCorpsman = 2;       // loadout record 5178:0018 (then Rear Security 0x24)

ButtonList g_buttons;
SpriteSet g_bull("bull");
std::vector<std::string> g_text;           // cYmNN.s
std::vector<SealChatterLine> g_chatter;    // sealNN.s of the Corpsman
bool g_hasChatter = false;
int g_chatterLine = 0;   // g_bull_chatter_line (DS:41E2), +1 per session
int g_scrollY = 0;       // g_bull_scroll_y
int g_variant = 0;       // g_bull_variant
int g_animStep = 0;      // g_bull_anim_step
s32 g_lineTime = 0, g_animTime = 0;
int g_line = 0;          // g_bull_line
bool g_talking = false;  // g_bull_talking
bool g_done = false;

s16 table(u16 off) {
    const u8* p = exe().at(kBullSeg, off);
    return p ? rds16(p) : 0;
}

std::string line(int i) { return i >= 0 && i < int(g_text.size()) ? g_text[size_t(i)] : std::string(); }

// bull_draw_background (365e:A862): two stacked pictures scrolled by the
// seating variant; the result becomes the background picture.
void drawBackground() {
    Gfx& gx = gfx();
    gx.clipFull();
    gx.clear(0);
    const u8* top = g_bull.frame(1, 2);
    const int w = SpriteSet::width(top);
    gx.setClip(0, 0, w, 200);
    drawSprite(top, 0x28, g_scrollY);
    if (g_scrollY < 0) drawSprite(g_bull.frame(1, 3), 0x28, g_scrollY + 200);
    picGrabScreen();
}

// bull_draw_seals (365e:AE8E).
void drawSeals() {
    picBlitToScreen();
    gfx().clipFull();
    for (int k = g_variant; k < g_variant + 4; ++k) {
        const int frame = table(u16(0x58 + 2 * (k * 12 + g_animStep % 12))) + ((k & 1) ? 4 : 0);
        const u8* img = g_bull.frame(k >> 1, frame);
        drawSprite(img, table(u16(0x40 + 4 * k)) + 0x28, table(u16(0x42 + 4 * k)) + g_scrollY);
    }
}

void render() {
    UiState& u = ui();
    if (u.redrawFrames != 0) {
        --u.redrawFrames;
        drawBackground();
    } else {
        engine::ticker().frameLimitWait();
        cursorErase();
    }
    drawSeals();
    Gfx& gx = gfx();
    gx.clipFull();
    gx.fillRect(0, 176, 320, 24, u16(0xff00));
    fontSelect(FontId::Title);
    uiDrawTitleTab(exe().dgString(kTitle), 0x62, 4);
    msg::draw();
    uiDrawButtons(g_buttons);
    engine::ticker().frameLimitWait();
    cursorDraw();
}

// bull_next_line (365e:AA23): mission lines 9-13 alternate between Rear
// Security and the Corpsman, the OIC wraps up, the Corpsman adds his own
// chatter and repeats line 9.
void nextLine() {
    std::string text;
    for (;;) {
        const int k = g_line;
        if (k <= 4) text = line(9 + k);
        else if (k == 5) text = tableString(strtab::kBullWrapUp, 0);
        else if (k == 6) {
            if (g_hasChatter && !g_chatter.empty()) {
                const SealChatterLine& c = g_chatter[size_t(g_chatterLine % 6) % g_chatter.size()];
                text.assign(c.text, strnlen(c.text, sizeof c.text));
            } else {
                text = line(9 + 6);
            }
        } else {
            text = line(9);
        }
        if (g_line < 7) {
            ++g_line;
            ui().redrawFrames = 2;
        }
        if (!text.empty() || g_line >= 7) break;
    }
    if (!text.empty() && g_line <= 7) {
        const int p = (g_line + (g_line == 7 ? 1 : 0)) & 1;
        const RosterEntry* e = campaign::rosterFind(campaign::loadout()[kCorpsman + p].se_id);
        std::string speaker;
        if (g_line == 6) speaker = exe().dgString(kOic);
        else if (e && e->se) speaker.assign(e->se->last_name, strnlen(e->se->last_name, sizeof e->se->last_name));
        msg::say(speaker, text);
    }
    if (g_line >= 7) {
        if (g_line == 7) ++g_line;
        if (msg::count() <= 1) {
            g_talking = false;
            g_line = 0;
        }
    }
}

// bull_start_talk (365e:AB8B).
void startTalk() {
    mouseRecenter();
    ui().redrawFrames = 2;
    g_lineTime = now() - 0x500;
    g_line = 0;
    g_talking = true;
}

// bull_handle_input (365e:ADD7): the pointer is not used here.
int handleInput(int key) {
    UiState& u = ui();
    int result = 2;
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0 && u.focus != -1) handleInput(g_buttons[size_t(u.focus)].key);
    if (inputToggleKeys(key)) return 2;
    switch (key) {
    case engine::key::Enter:
    case 'n':
        g_done = true;
        break;
    case engine::key::Esc:
        result = 1;
        g_done = true;
        break;
    case engine::key::Space:
        nextLine();
        g_lineTime = now();
        msg::skip();
        break;
    case 'b':
        if (!g_talking) startTalk();
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

int bullScreen() {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    auto& snd = engine::sound();
    CampaignHeader& h = campaign::header();
    if (g_buttons.empty()) g_buttons = loadButtonList(kBullSeg, kLabelTable);
    clock.resetClock();
    g_lineTime = now();
    tod::set(12, 0);
    msg::reset();
    const RosterEntry* pm = campaign::rosterFind(h.point_man);
    g_variant = pm ? pm->missions % 3 : 0;
    g_scrollY = s16(u16(g_variant * 0xffc4));  // variant * -60
    screenTransition(PalB, true);
    // bull_enter
    g_bull.free();
    ui().redrawFrames = 1;
    drawBackground();
    ui().focus = kNextButton;
    uiCursorToButton(g_buttons[kNextButton]);
    campaign::loadMissionText(h.year * 20 + h.mission, g_text);
    g_hasChatter = false;
    if (const RosterEntry* c = campaign::rosterFind(campaign::loadout()[kCorpsman].se_id))
        g_hasChatter = campaign::loadSealChatter(c->se_id, g_chatter);
    g_lineTime = g_animTime = now();
    int result = 1;
    ui().redrawFrames = 2;
    g_line = 0;
    g_talking = false;
    g_done = false;
    startTalk();
    in.resetRepeatTimers();
    in.flushKeyboard();
    in.setMode(engine::InputMode::Menu);
    snd.loadMusic(kMusic, 0);
    while (!g_done) {
        snd.startOnce(h.year);
        engine::paletteFade().request(0, clock.frameDt());
        render();
        present();
        clock.updateGameTime();
        tod::update();
        if (now() - g_animTime >= 0x80) {
            g_animTime = now();
            if (g_animTime != 0) ++g_animStep;
        }
        msg::tick();
        if (now() - g_lineTime >= 0x500) {
            g_lineTime = now();
            if (g_lineTime != 0) nextLine();
        }
        if (!g_talking && now() - g_lineTime >= 0x1e00) {
            g_lineTime = now();
            if (g_lineTime != 0) startTalk();
        }
        const int key = getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        result = handleInput(key);
    }
    cursorSetWait(true);
    render();
    present();
    snd.stop();
    snd.unload();
    // bull_leave
    msg::clear();
    cursorReset();
    ++g_chatterLine;
    g_text.clear();
    g_chatter.clear();
    g_bull.free();
    return result;
}

} // namespace st::game::front
