// Difficulty screen (365e:E554..EB3A, docs/re/seg_365e_b.md 13): F10 from the
// main menu and the mission briefing. Eight settings, each opening a row of
// option buttons; 's' saves st1.dfr.
#include "game/front/front.h"

#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "engine/ticker.h"
#include "game/config.h"
#include "game/front/common.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/gfx.h"

namespace st::game::front {

namespace {

constexpr u16 kOptionTableSeg = 0x52c0;  // u16[8][4] option button indices per setting
constexpr u16 kButtonSeg = 0x52c4;       // = 52c0:0040
constexpr u16 kLabelTable = 0x4e6a;
constexpr u16 kTitle = 0x4ea6;           // " Difficulty  "
constexpr int kTitleX = 0x78, kTitleY = 4;
constexpr int kSettings = 8;
constexpr int kSaveButton = 8;
constexpr int kMainButtons = 10;         // 8 settings + Save + Quit

ButtonList g_buttons;  // loaded once: the flags persist between visits, as in the original
bool g_done = false;        // 53ba:3048
bool g_optionDone = false;  // 53ba:304A

u16& setting(int s) {
    DifficultyOptions& d = difficulty();
    u16* f[kSettings] = {&d.ammo, &d.enemy_wounds, &d.intelligence, &d.player_wounds,
                         &d.reload_time, &d.team_size, &d.weapons, &d.map};
    return *f[s];
}

int optionButton(int s, int i) {
    const u8* p = exe().at(kOptionTableSeg, u16(s * 8 + i * 2));
    return p ? rd16(p) : 0;
}

void setFlags(int index, u8 flags) {
    if (index >= 0 && index < int(g_buttons.size())) g_buttons[size_t(index)].flags = flags;
}

// diff_render (365e:E554): buttons and title every frame.
void render() {
    renderBegin();
    gfx().clipFull();
    uiDrawButtons(g_buttons);
    uiDrawTitleTab(exe().dgString(kTitle), kTitleX, kTitleY);
    engine::ticker().frameLimitWait();
    cursorDraw();
}

// Option keys per setting (jump table at 365e:E982); -1 = unused.
void optionKeys(int s, int k[3]) {
    static const int kKeys[kSettings][3] = {
        {'u', 'r', -1}, {'d', 'h', 'r'}, {'m', 'd', 'r'}, {'n', 'd', 'r'},
        {'i', 't', -1}, {'d', 'r', 'e'}, {'u', 'r', -1}, {'f', 'r', -1},
    };
    for (int i = 0; i < 3; ++i) k[i] = (s >= 0 && s < kSettings) ? kKeys[s][i] : -1;
}

// diff_option_handle_input (365e:E967).
int optionHandleInput(int key, int dx, int dy, int s, int settingKey) {
    UiState& u = ui();
    int result = 1;
    int k[3];
    optionKeys(s, k);
    if (dx != 0 || dy != 0) uiPointerUpdate(dx, dy, g_buttons);
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0 && u.focus != -1)
        optionHandleInput(g_buttons[size_t(u.focus)].key, 0, 0, s, settingKey);
    int value = -2;
    if (key == k[0]) value = 0;
    else if (key == k[1]) value = 1;
    else if (key == k[2]) value = 2;
    if (value != -2) {
        setting(s) = u16(value);
        g_optionDone = true;
    } else if (key == settingKey) {
        g_optionDone = true;
    }
    if (uiMenuArrowKeys(key, g_buttons)) return 1;
    if (key == engine::key::Enter) {
        if (u.focus != -1 && g_buttons[size_t(u.focus)].key != engine::key::Enter) u.pressed = true;
    } else if (key == engine::key::AltX) {
        // Closes the option row only: diff_handle_input ignores this result.
        result = 0;
        g_optionDone = true;
    }
    return result;
}

// diff_choose_option (365e:E662).
void chooseOption(int s, int settingKey) {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    for (int b = 0; b < kMainButtons; ++b) setFlags(b, btn::Disabled);
    setFlags(s, btn::Clickable);
    for (int i = 0; optionButton(s, i) != 0; ++i)
        setFlags(optionButton(s, i), u8(setting(s) == i ? btn::Clickable | btn::Highlighted : btn::Clickable));
    ui().redrawFrames = 2;
    g_optionDone = false;
    while (!g_optionDone) {
        render();
        present();
        clock.updateGameTime();
        const int key = getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        optionHandleInput(key, dx, dy, s, settingKey);
    }
    for (int b = 0; b < kMainButtons; ++b) setFlags(b, btn::Clickable);
    for (int i = 0; optionButton(s, i) != 0; ++i)
        setFlags(optionButton(s, i), u8(setting(s) == i ? btn::Disabled | btn::Highlighted : btn::Disabled));
    ui().redrawFrames = 2;
}

// diff_handle_input (365e:E7EC).
int handleInput(int key, int dx, int dy) {
    UiState& u = ui();
    int result = 1;
    if (dx != 0 || dy != 0) uiPointerUpdate(dx, dy, g_buttons);
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0 && u.focus != -1)
        handleInput(g_buttons[size_t(u.focus)].key, 0, 0);
    if (uiMenuArrowKeys(key, g_buttons)) return 1;
    int s = -1;
    switch (key) {
    case engine::key::Enter:
        if (u.focus == -1 || g_buttons[size_t(u.focus)].key == engine::key::Enter) return 1;
        u.pressed = true;
        return 1;
    case 'a': s = 0; break;
    case 'e': s = 1; break;
    case 'i': s = 2; break;
    case 'm': s = 7; break;
    case 'p': s = 3; break;
    case 'r': s = 4; break;
    case 't': s = 5; break;
    case 'w': s = 6; break;
    case 'q':
        g_done = true;
        return result;
    case 's':
        g_done = true;
        cfgSaveDifficulty();
        return 1;
    case engine::key::AltX:
        g_done = true;
        return 0;
    default:
        return 1;
    }
    chooseOption(s, key);
    return 1;
}

} // namespace

int difficultyScreen() {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    if (g_buttons.empty()) g_buttons = loadButtonList(kButtonSeg, kLabelTable);
    // Current values: highlighted and not clickable.
    for (int s = 0; s < kSettings; ++s) setFlags(optionButton(s, setting(s)), btn::Disabled | btn::Highlighted);
    // diff_enter: mapscr.pic, focus Save. No palette change: the screen shows
    // in the palette of the screen it was called from.
    picLoad(PicMap);
    ui().focus = kSaveButton;
    uiCursorToButton(g_buttons[kSaveButton]);
    ui().redrawFrames = 2;
    g_done = false;
    in.resetRepeatTimers();
    in.flushKeyboard();
    int result = 1;
    while (!g_done) {
        render();
        present();
        clock.updateGameTime();
        const int key = getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        result = handleInput(key, dx, dy);
    }
    cursorReset();
    return result;
}

} // namespace st::game::front
