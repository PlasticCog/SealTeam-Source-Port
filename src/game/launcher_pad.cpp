#include "game/launcher_pad.h"

#include "core/settings.h"
#include "engine/controller.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/ticker.h"
#include "game/devtools.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/gfx.h"
#include "platform/system.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace st::game {

namespace {

using engine::Binding;
using engine::Controller;
using engine::PadAction;
using engine::PadInput;

u16 colour(u8 index) { return u16(0xff00 | index); }

// Same panel / help line as the start menu (launcher.cpp).
void drawPanel(int x, int y, int w, int h) {
    Gfx& gx = gfx();
    gx.fillRect(x - 4, y - 3, w + 8, h + 6, colour(0x18));
    gx.fillRect(x - 3, y - 2, w + 6, h + 4, colour(0x16));
    gx.fillRect(x - 2, y - 1, w + 4, h + 2, colour(0x14));
}

void drawHelp(const std::string& text) {
    fontSelect(FontId::Dialog);
    drawPanel(20, 170, 280, 12);
    drawTextShadow(160 - textWidth(text) / 2, 172, text, 0x0f, 0x00);
}

Button makeButton(int x, int y, int w, u16 key, const std::string& label) {
    Button b;
    b.x = s16(x);
    b.y = s16(y);
    b.w = s16(w);
    b.h = 16;
    b.key = key;
    b.hotIndex = -1;
    b.flags = btn::Clickable;
    b.label = label;
    return b;
}

// Pages of the action list (two columns of seven).
struct Page {
    const char* title;
    std::vector<PadAction> actions;
};
const Page kPages[] = {
    {"Basics",
     {PadAction::Move, PadAction::Camera, PadAction::Select, PadAction::Fire, PadAction::Cancel, PadAction::Button2,
      PadAction::Grenade, PadAction::NextWeapon, PadAction::PostureDown, PadAction::PostureUp,
      PadAction::NextTarget, PadAction::Map, PadAction::Pause, PadAction::Shift}},
    {"Field",
     {PadAction::Orders, PadAction::CycleTool, PadAction::UseTool, PadAction::NextGrenade, PadAction::RateOfFire,
      PadAction::Up, PadAction::Down, PadAction::Left, PadAction::Right, PadAction::TimeCompression,
      PadAction::AutoTarget, PadAction::TeamInfo, PadAction::ExposeTrap, PadAction::Dive}},
    {"Views",
     {PadAction::ViewPointMan, PadAction::ViewTeam, PadAction::ViewSupport1, PadAction::ViewSupport2,
      PadAction::ViewSupport3, PadAction::ViewSupport4, PadAction::ViewSplitA, PadAction::ViewSplitB,
      PadAction::ViewTarget, PadAction::QuitGame}},
    {"Orders",
     {PadAction::OrderFieldOfFire, PadAction::OrderAtTarget, PadAction::OrderAtWill, PadAction::OrderCeaseFire,
      PadAction::OrderHalt, PadAction::OrderSearch, PadAction::OrderSplit, PadAction::OrderJoin,
      PadAction::OrderColumn, PadAction::OrderInLine, PadAction::OrderDiamond, PadAction::OrderVee}},
    {"Options", {}},
};
constexpr int kPageCount = int(sizeof(kPages) / sizeof(kPages[0]));
constexpr int kOptionsPage = kPageCount - 1;

// Button key codes (outside the BIOS key code space of the keyboard).
constexpr u16 kKeyAction = 0xe000;   // + index on the page
constexpr u16 kKeyPrev = 0xe100, kKeyNext = 0xe101, kKeyDefaults = 0xe102;
constexpr u16 kKeyEnabled = 0xe200, kKeyDeadZone = 0xe201, kKeySensitivity = 0xe202;
constexpr u16 kKeyBack = engine::key::Esc;

constexpr int kColX[2] = {4, 162};
constexpr int kColW = 154;
constexpr int kRowY0 = 22, kRowH = 18, kRows = 7;
constexpr int kBottomY = 150;

// "label: binding", shortened until it fits the button (Title font).
std::string fitLabel(const std::string& label, const std::string& bind, int w) {
    fontSelect(FontId::Title);
    std::string s = label + ": " + bind;
    if (textWidth(s) <= w - 6) return s;
    s = label + ":" + bind;
    std::string l = label;
    while (textWidth(s) > w - 6 && l.size() > 3) {
        l.pop_back();
        s = l + ".:" + bind;
    }
    return s;
}

class ControllerPage {
public:
    bool run(int startPage = 0);

private:
    void rebuild();
    void tick();             // per frame: capture polling, status
    bool handle(int key);    // returns false to leave
    void beginCapture(PadAction a);
    void endCapture();
    void save();
    std::string status() const;

    Controller& pad_ = engine::controller();
    ButtonList list_;
    int page_ = 0;
    std::string help_;
    bool capturing_ = false;
    PadAction captureAction_ = PadAction::Move;
    bool quit_ = false;
};

void ControllerPage::rebuild() {
    Settings& st = settings();
    list_.clear();
    const Page& p = kPages[page_];
    if (page_ == kOptionsPage) {
        char buf[64];
        list_.push_back(makeButton(kColX[0], kRowY0, kColW, kKeyEnabled,
                                   std::string("Controller: ") + (st.padEnabled ? "On" : "Off")));
        std::snprintf(buf, sizeof buf, "Dead zone: %d%%", st.padDeadZonePct);
        list_.push_back(makeButton(kColX[0], kRowY0 + kRowH, kColW, kKeyDeadZone, buf));
        std::snprintf(buf, sizeof buf, "Stick speed: %d%%", st.padSensitivityPct);
        list_.push_back(makeButton(kColX[0], kRowY0 + 2 * kRowH, kColW, kKeySensitivity, buf));
    } else {
        for (size_t i = 0; i < p.actions.size(); ++i) {
            const PadAction a = p.actions[i];
            const int col = int(i) / kRows, row = int(i) % kRows;
            const std::string bind = pad_.bindingText(pad_.binding(a));
            list_.push_back(makeButton(kColX[col], kRowY0 + row * kRowH, kColW, u16(kKeyAction + i),
                                       fitLabel(Controller::info(a).label, bind, kColW)));
        }
    }
    list_.push_back(makeButton(4, kBottomY, 44, kKeyPrev, "< Prev"));
    list_.push_back(makeButton(52, kBottomY, 44, kKeyNext, "Next >"));
    list_.push_back(makeButton(104, kBottomY, 100, kKeyDefaults, "Xbox defaults"));
    list_.push_back(makeButton(260, kBottomY, 56, kKeyBack, "Back"));
}

std::string ControllerPage::status() const {
    const Gamepad& g = sys().gamepad();
    if (!settings().padEnabled) return "Controller support is switched off.";
    if (!g.present()) return "No controller connected (plug one in any time).";
    return "Connected: " + g.name();
}

void ControllerPage::save() {
    pad_.storeToSettings();
    saveSettings();
}

void ControllerPage::beginCapture(PadAction a) {
    if (!sys().gamepad().present()) {
        help_ = "Connect a controller to change bindings.";
        return;
    }
    capturing_ = true;
    captureAction_ = a;
    pad_.beginCapture();
    const auto flags = Controller::info(a).flags;
    if (flags & engine::pad::Motion) help_ = std::string("Move a stick for '") + Controller::info(a).label + "'. Esc cancels.";
    else if (flags & engine::pad::Modifier) help_ = std::string("Press the button to hold for '") + Controller::info(a).label + "'. Esc cancels, Del clears.";
    else help_ = std::string("Press a button for '") + Controller::info(a).label + "' (hold Shift/Orders for a chord). Esc cancels, Del clears.";
}

void ControllerPage::endCapture() {
    capturing_ = false;
    pad_.endCapture();
}

void ControllerPage::tick() {
    if (!capturing_) return;
    Binding b;
    if (!pad_.captured(b)) return;
    const auto flags = Controller::info(captureAction_).flags;
    if (flags & engine::pad::Motion) {
        switch (b.input) {
        case PadInput::LxMinus: case PadInput::LxPlus: case PadInput::LyMinus: case PadInput::LyPlus:
            b = Binding{PadInput::LeftStick, engine::Layer::Base};
            break;
        case PadInput::RxMinus: case PadInput::RxPlus: case PadInput::RyMinus: case PadInput::RyPlus:
            b = Binding{PadInput::RightStick, engine::Layer::Base};
            break;
        default:
            help_ = "That is not a stick. Move the left or right stick.";
            return;
        }
    } else if (flags & engine::pad::Modifier) {
        b.layer = engine::Layer::Base;
    }
    pad_.setBinding(captureAction_, b);
    endCapture();
    save();
    rebuild();
    help_ = std::string(Controller::info(captureAction_).label) + " = " + pad_.bindingText(b);
}

bool ControllerPage::handle(int key) {
    Settings& st = settings();
    if (capturing_) {
        if (key == engine::key::Esc) {
            endCapture();
            help_ = "Cancelled.";
        } else if (key == 0x5300 || key == 0x08) {  // Del / Backspace: clear
            pad_.setBinding(captureAction_, Binding{});
            endCapture();
            save();
            rebuild();
            help_ = std::string(Controller::info(captureAction_).label) + " cleared.";
        }
        return true;
    }
    if (key >= kKeyAction && key < kKeyAction + 0x100) {
        const size_t i = size_t(key - kKeyAction);
        if (i < kPages[page_].actions.size()) beginCapture(kPages[page_].actions[i]);
        return true;
    }
    switch (key) {
    case kKeyPrev: page_ = (page_ + kPageCount - 1) % kPageCount; break;
    case kKeyNext: page_ = (page_ + 1) % kPageCount; break;
    case kKeyDefaults:
        pad_.resetDefaults();
        save();
        help_ = "Xbox default layout restored.";
        break;
    case kKeyEnabled:
        st.padEnabled = !st.padEnabled;
        saveSettings();
        break;
    case kKeyDeadZone:
        st.padDeadZonePct = st.padDeadZonePct >= 40 ? 0 : st.padDeadZonePct + 5;
        saveSettings();
        help_ = "Stick travel ignored around the centre.";
        break;
    case kKeySensitivity: {
        static const int kSteps[] = {50, 75, 100, 125, 150, 200};
        int next = kSteps[0];
        for (size_t i = 0; i < sizeof(kSteps) / sizeof(kSteps[0]); ++i)
            if (kSteps[i] == st.padSensitivityPct) next = kSteps[(i + 1) % (sizeof(kSteps) / sizeof(kSteps[0]))];
        st.padSensitivityPct = next;
        saveSettings();
        help_ = "Turn and pointer speed of the sticks.";
        break;
    }
    case kKeyBack: return false;
    case engine::key::AltX: quit_ = true; return false;
    default: return true;
    }
    rebuild();
    return true;
}

// The start menu's screen loop (launcher.cpp runScreen) with a per-frame
// tick for the capture; redraws every frame like the other pages.
bool ControllerPage::run(int startPage) {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    UiState& s = ui();
    in.resetRepeatTimers();
    in.flushKeyboard();
    in.setMode(engine::InputMode::Menu);
    page_ = std::clamp(startPage, 0, kPageCount - 1);
    rebuild();
    help_ = "Click an action, then press the button for it.";
    ui().focus = -1;
    bool running = true;
    while (running) {
        engine::paletteFade().request(0, clock.frameDt());
        clock.frameLimitWait();
        picBlitToScreen();
        char title[64];
        std::snprintf(title, sizeof title, " Controller: %s (%d/%d) ", kPages[page_].title, page_ + 1, kPageCount);
        fontSelect(FontId::Title);
        uiDrawTitleTab(title, 160 - textWidth(title) / 2, 4);
        if (page_ == kOptionsPage) {
            fontSelect(FontId::Dialog);
            const int y = kRowY0 + 4 * kRowH;
            drawPanel(kColX[0] + 4, y, 304, 26);
            drawTextShadow(kColX[0] + 8, y + 2, status(), 0x0f, 0x00);
            drawTextShadow(kColX[0] + 8, y + 14, "Bindings are saved in sealteam.cfg (bind_* lines).", 0x0f, 0x00);
        }
        drawHelp(help_);
        uiDrawButtons(list_);
        gfx().clipFull();
        clock.frameLimitWait();
        cursorDraw();
        present();
        clock.updateGameTime();

        tick();
        int key = in.getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        if (dx || dy) uiPointerUpdate(dx, dy, list_);
        uiButtonRelease(key);
        if (s.releaseCount != 0 && --s.releaseCount == 0 && s.focus != -1) key = list_[size_t(s.focus)].key;
        switch (key) {
        case 0: break;
        case engine::key::Up: uiPointerUpdate(0, -5, list_); break;
        case engine::key::Down: uiPointerUpdate(0, 5, list_); break;
        case engine::key::Left: uiPointerUpdate(-8, 0, list_); break;
        case engine::key::Right: uiPointerUpdate(8, 0, list_); break;
        case engine::key::Enter:
            if (s.focus != -1) uiButtonPress(list_[size_t(s.focus)].key);
            break;
        default:
            running = handle(key);
            break;
        }
    }
    if (capturing_) endCapture();
    save();  // the full table in sealteam.cfg, editable by hand
    return !quit_;
}

const DevCommand kControllerScreen("--controller-screen", "open the start menu's Controller page [PAGE 1..5]",
                                   [](const DevArgs& args) {
    screenTransition(PalMenu, false);
    picLoad(PicMainMenu);
    engine::paletteFade().setLevel(0);
    ControllerPage page;
    page.run(args.empty() ? 0 : std::atoi(args[0].c_str()) - 1);
    return 0;
});

} // namespace

bool runControllerPage() {
    ControllerPage page;
    return page.run();
}

} // namespace st::game
