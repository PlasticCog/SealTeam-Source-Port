#include "game/menu.h"

#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/sound.h"
#include "engine/ticker.h"
#include "game/config.h"
#include "game/front/front.h"
#include "game/front/common.h"
#include "game/globals.h"
#include "game/screens.h"
#include "game/title.h"
#include "game/ui.h"
#include "gfx/gfx.h"

namespace st::game {

namespace {

constexpr u16 kMenuButtonSeg = 0x51b3;   // 3 button records
constexpr u16 kMenuLabelTable = 0x2464;  // near pointers to the labels
constexpr u16 kMenuTitle = 0x3f02;       // " Main Menu  "
constexpr int kTitleX = 124, kTitleY = 4;
constexpr int kContinue = 0, kPractice = 2;

ButtonList g_buttons;
bool g_done = false;

void menuEnter() {
    picLoad(PicMainMenu);
    // Continue is only available when a campaign slot was used before.
    if (slotConfig().lastSlot == -1) g_buttons[kContinue].flags |= btn::Disabled;
    else g_buttons[kContinue].flags &= u8(~btn::Disabled);
    ui().focus = kPractice;
    uiCursorToButton(g_buttons[kPractice]);
}

void menuRender() {
    Gfx& gx = gfx();
    UiState& s = ui();
    if (s.redrawFrames == 0) {
        engine::ticker().frameLimitWait();
        cursorErase();
    } else {
        --s.redrawFrames;
        picBlitToScreen();
        uiDrawButtons(g_buttons);
    }
    gx.clipFull();
    uiDrawTitleTab(exe().dgString(kMenuTitle), kTitleX, kTitleY);
    engine::ticker().frameLimitWait();
    cursorDraw();
}

int menuHandleInput(int key, int dx, int dy) {
    UiState& s = ui();
    int result = 1;
    if ((dx != 0 || dy != 0) && uiPointerUpdate(dx, dy, g_buttons)) s.redrawFrames = 2;
    uiButtonRelease(key);
    if (s.releaseCount != 0 && --s.releaseCount == 0 && s.focus != -1) {
        menuHandleInput(g_buttons[size_t(s.focus)].key, 0, 0);
        s.redrawFrames = 2;
    }
    if (inputToggleKeys(key) || uiMenuArrowKeys(key, g_buttons)) return 1;
    Globals& gs = g();
    switch (key) {
    case engine::key::Enter:
        if (s.focus == -1 || !uiButtonPress(g_buttons[size_t(s.focus)].key)) return 1;
        s.redrawFrames = 2;
        return result;
    case 'c':
        if (g_buttons[kContinue].flags & btn::Disabled) return 1;
        gs.gameMode = GameMode::Campaign;
        g_done = true;
        return 1;
    case 'p':
        gs.gameMode = GameMode::Practice;
        break;
    case 's':
        gs.gameMode = GameMode::Menu;
        break;
    case engine::key::AltX:
        result = 0;
        break;
    case engine::key::F10:
        front::difficultyScreen();  // its result is ignored here
        menuEnter();
        s.redrawFrames = 2;
        return result;
    default:
        return 1;
    }
    g_done = true;
    return result;
}

} // namespace

int menuRun() {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    if (g_buttons.empty()) g_buttons = loadButtonList(kMenuButtonSeg, kMenuLabelTable);
    screenTransition(PalMenu, true);
    menuEnter();
    ui().redrawFrames = 2;
    g_done = false;
    in.resetRepeatTimers();
    in.flushKeyboard();
    in.setMode(engine::InputMode::Menu);
    engine::sound().loadMusic(5, 0);
    int result = 1;
    while (!g_done) {
        engine::sound().startOnce(0);
        engine::paletteFade().request(0, clock.frameDt());
        menuRender();
        present();
        clock.updateGameTime();
        const int key = front::getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        result = menuHandleInput(key, dx, dy);
    }
    cursorSetWait(true);
    menuRender();
    present();
    cursorReset();
    engine::sound().unload();
    return result;
}

} // namespace st::game
