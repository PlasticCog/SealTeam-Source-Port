// Speech screen: the Chaplain's funeral speech and the Commander's speeches
// at the end of a year, of the campaign and when relieved of command
// (365e:D7D6..DBE6, docs/re/seg_365e_b.md 11).
#include "game/front/front.h"

#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/sound.h"
#include "engine/ticker.h"
#include "game/campaign.h"
#include "game/front/common.h"
#include "game/globals.h"
#include "game/screens.h"
#include "game/title.h"
#include "game/ui.h"
#include "gfx/gfx.h"

namespace st::game::front {

namespace {

constexpr u16 kButtonSeg = 0x520a;
constexpr u16 kLabelTable = 0x24c8;
constexpr u16 kChaplain = 0x45ac;
constexpr u16 kCommander = 0x45b5;
constexpr u16 kMusicTable = 0x45a6;  // u8[6] music sequence per kind {1,0,0,0,0,2}
constexpr int kNextButton = 1;
constexpr int kMusic = 0x0d;
constexpr int kLines = 11;

ButtonList g_buttons;
std::vector<std::string> g_text;  // c<K>.s
int g_kind = 0;                   // g_speech_kind
int g_line = 0;                   // g_speech_line
s32 g_lineTime = 0;
bool g_done = false;

void render() {
    Gfx& gx = gfx();
    UiState& u = ui();
    if (u.redrawFrames != 0) {
        --u.redrawFrames;
        picBlitToScreen();
        gx.fillRect(0, 176, 320, 24, u16(0xff00));
    } else {
        engine::ticker().frameLimitWait();
        cursorErase();
    }
    gx.fillRect(0, 176, 320, 24, u16(0xff00));
    gx.clipFull();
    msg::draw();
    uiDrawButtons(g_buttons);
    engine::ticker().frameLimitWait();
    cursorDraw();
}

// speech_next_line (365e:D84F).
void nextLine() {
    std::string text;
    for (;;) {
        text = g_line < int(g_text.size()) ? g_text[size_t(g_line)] : std::string();
        if (g_line < kLines) ++g_line;
        if (!text.empty() || g_line >= kLines) break;
    }
    if (!text.empty() && g_line <= kLines) msg::say(exe().dgString(g_kind == 0 ? kChaplain : kCommander), text);
    if (g_line >= kLines) {
        if (g_line == kLines) ++g_line;
        if (msg::count() <= 1) g_line = 0;
    }
}

// speech_start (365e:D91C).
void start() {
    mouseRecenter();
    ui().redrawFrames = 2;
    g_lineTime = now() - 0x800;
    g_line = 0;
    msg::clear();
}

// speech_handle_input (365e:DAE7).
int handleInput(int key, int dx, int dy) {
    UiState& u = ui();
    int result = 2;
    if (dx != 0 || dy != 0) uiPointerUpdate(dx, dy, g_buttons);
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0 && u.focus != -1) handleInput(g_buttons[size_t(u.focus)].key, 0, 0);
    if (inputToggleKeys(key) || uiMenuArrowKeys(key, g_buttons)) return 2;
    switch (key) {
    case engine::key::Enter:
        // In a campaign Enter leaves at once; otherwise it presses the focused button.
        if (int(g().gameMode) <= 2) {
            g_done = true;
        } else if (u.focus != -1 && g_buttons[size_t(u.focus)].key != engine::key::Enter) {
            u.pressed = true;
        }
        break;
    case engine::key::Esc:
    case 'n':
        g_done = true;
        break;
    case engine::key::Space:
        nextLine();
        g_lineTime = now();
        msg::skip();
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

int speechScreen(int kind) {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    auto& snd = engine::sound();
    if (g_buttons.empty()) g_buttons = loadButtonList(kButtonSeg, kLabelTable);
    int result = 1;
    g_kind = kind;
    clock.resetClock();
    // speech_enter: cmK.pic, focus Next, text cK.s
    picLoad(PicCamp0 + kind);
    ui().focus = kNextButton;
    uiCursorToButton(g_buttons[kNextButton]);
    campaign::loadTextLines(std::string("c") + char('0' + kind), g_text);
    g_lineTime = now();
    ui().redrawFrames = 2;
    msg::reset();
    start();
    if (g().gameMode == GameMode::Demo) {
        g_done = true;
    } else {
        in.resetRepeatTimers();
        in.flushKeyboard();
        in.setMode(engine::InputMode::Menu);
        snd.loadMusic(kMusic, 0);
        screenTransition(PalCamp0 + kind, true);
        g_done = false;
    }
    clock.resetClock();
    while (!g_done) {
        snd.startOnce(exe().dgByte(u16(kMusicTable + kind)));
        engine::paletteFade().request(0, clock.frameDt());
        render();
        present();
        clock.updateGameTime();
        msg::tick();
        if (now() - g_lineTime >= 0x800) {
            g_lineTime = now();
            if (g_lineTime != 0) nextLine();
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
        snd.stop();
        snd.unload();
    }
    // speech_leave
    msg::clear();
    cursorReset();
    g_text.clear();
    return result;
}

} // namespace st::game::front
