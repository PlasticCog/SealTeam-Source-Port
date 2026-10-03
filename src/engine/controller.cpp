#include "engine/controller.h"

#include "core/settings.h"
#include "engine/input_layer.h"
#include "engine/ticker.h"
#include "platform/system.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace st::engine {

namespace {

// Key codes of the game's handlers (docs/re/seg_19ac.md 5-6).
namespace k {
constexpr int Enter = 0x0d, Esc = 0x1b, Space = 0x20, Tab = 0x09;
constexpr int Up = 0x4800, Down = 0x5000, Left = 0x4b00, Right = 0x4d00;
constexpr int F1 = 0x3b00, F2 = 0x3c00, F3 = 0x3d00, F4 = 0x3e00, F5 = 0x3f00, F6 = 0x4000, F7 = 0x4100,
              F8 = 0x4200, F9 = 0x4300;
constexpr int AltT = 0x1400, AltU = 0x1600, AltI = 0x1700, AltP = 0x1900, AltN = 0x3100, AltX = 0x2d00;
} // namespace k

using I = PadInput;
constexpr Binding none{I::None, Layer::Base};
constexpr Binding base(I i) { return Binding{i, Layer::Base}; }
constexpr Binding shift(I i) { return Binding{i, Layer::Shift}; }
constexpr Binding orders(I i) { return Binding{i, Layer::Orders}; }

// The action table: order = PadAction. Default layout = Xbox controller.
const ActionInfo kActions[] = {
    {PadAction::Move, "move", "Move / turn", 0, pad::Motion, base(I::LeftStick)},
    {PadAction::Camera, "camera", "Camera", 0, pad::Motion, base(I::RightStick)},
    {PadAction::Shift, "shift", "Shift (hold)", 0, pad::Modifier, base(I::LB)},
    {PadAction::Orders, "orders", "Orders (hold)", 0, pad::Modifier, base(I::RB)},
    {PadAction::Select, "select", "Select / fire", k::Enter, pad::Button1, base(I::A)},
    {PadAction::Fire, "fire", "Fire", k::Enter, pad::Button1, base(I::RT)},
    {PadAction::Button2, "button2", "Cam pan / map", k::Space, pad::Button2, base(I::RS)},
    {PadAction::Cancel, "cancel", "Cancel (Esc)", k::Esc, 0, base(I::B)},
    {PadAction::NextWeapon, "next_weapon", "Next weapon", 'n', 0, base(I::X)},
    {PadAction::PostureDown, "posture_down", "Posture down", '-', pad::Repeat, base(I::Y)},
    {PadAction::PostureUp, "posture_up", "Posture up", '+', pad::Repeat, shift(I::Y)},
    {PadAction::CycleTool, "cycle_tool", "Next tool", ']', 0, shift(I::X)},
    {PadAction::UseTool, "use_tool", "Use tool", '[', 0, shift(I::RT)},
    {PadAction::Grenade, "grenade", "Grenade", 'g', 0, base(I::LT)},
    {PadAction::NextGrenade, "next_grenade", "Next grenade", k::AltN, 0, shift(I::LT)},
    {PadAction::NextTarget, "next_target", "Next target", k::Tab, 0, base(I::LS)},
    {PadAction::RateOfFire, "rate_of_fire", "Rate of fire", 'r', 0, shift(I::RS)},
    {PadAction::Map, "map", "Map screen", 'm', 0, base(I::Back)},
    {PadAction::Pause, "pause", "Pause", k::AltP, 0, base(I::Start)},
    {PadAction::Up, "up", "Up / speed up", k::Up, pad::Repeat, base(I::DpadUp)},
    {PadAction::Down, "down", "Down / slower", k::Down, pad::Repeat, base(I::DpadDown)},
    {PadAction::Left, "left", "Left / turn", k::Left, pad::Repeat, base(I::DpadLeft)},
    {PadAction::Right, "right", "Right / turn", k::Right, pad::Repeat, base(I::DpadRight)},
    {PadAction::ViewPointMan, "view_point_man", "Point man view", k::F1, 0, shift(I::A)},
    {PadAction::ViewTeam, "view_team", "Team view", k::F2, 0, shift(I::B)},
    {PadAction::ViewSupport1, "view_support1", "Support view 1", k::F3, 0, shift(I::DpadUp)},
    {PadAction::ViewSupport2, "view_support2", "Support view 2", k::F4, 0, shift(I::DpadDown)},
    {PadAction::ViewSupport3, "view_support3", "Support view 3", k::F5, 0, none},
    {PadAction::ViewSupport4, "view_support4", "Support view 4", k::F6, 0, none},
    {PadAction::ViewSplitA, "view_split_a", "Split A view", k::F7, 0, shift(I::DpadLeft)},
    {PadAction::ViewSplitB, "view_split_b", "Split B view", k::F8, 0, shift(I::DpadRight)},
    {PadAction::ViewTarget, "view_target", "Target view", k::F9, 0, shift(I::LS)},
    {PadAction::TimeCompression, "time_compression", "Time compression", k::AltT, 0, shift(I::Start)},
    {PadAction::AutoTarget, "auto_target", "Auto-target", k::AltU, 0, shift(I::Back)},
    {PadAction::TeamInfo, "team_info", "Team info", k::AltI, 0, none},
    {PadAction::ExposeTrap, "expose_trap", "Expose trap", 'x', 0, orders(I::Start)},
    {PadAction::Dive, "dive", "Dive", 'q', 0, orders(I::Back)},
    {PadAction::QuitGame, "quit", "Quit game", k::AltX, 0, none},
    {PadAction::RecentreCamera, "recentre_camera", "Recentre camera", key::RecentreCamera, 0, none},
    {PadAction::OrderFieldOfFire, "order_field_of_fire", "Field of fire", 'f', 0, orders(I::Y)},
    {PadAction::OrderAtTarget, "order_at_target", "Fire at target", 't', 0, orders(I::X)},
    {PadAction::OrderAtWill, "order_at_will", "Fire at will", 'w', 0, orders(I::A)},
    {PadAction::OrderCeaseFire, "order_cease_fire", "Cease fire", 'c', 0, orders(I::B)},
    {PadAction::OrderHalt, "order_halt", "Halt", 'h', 0, orders(I::DpadUp)},
    {PadAction::OrderSearch, "order_search", "Search", 's', 0, orders(I::DpadDown)},
    {PadAction::OrderSplit, "order_split", "Split team", 'p', 0, orders(I::DpadLeft)},
    {PadAction::OrderJoin, "order_join", "Join team", 'j', 0, orders(I::DpadRight)},
    {PadAction::OrderColumn, "order_column", "Column", 'l', 0, orders(I::LT)},
    {PadAction::OrderInLine, "order_in_line", "In line", 'i', 0, orders(I::RT)},
    {PadAction::OrderDiamond, "order_diamond", "Diamond", 'd', 0, orders(I::LS)},
    {PadAction::OrderVee, "order_vee", "Vee wedge", 'v', 0, orders(I::RS)},
};
static_assert(sizeof(kActions) / sizeof(kActions[0]) == size_t(PadAction::Count), "action table");

struct InputName {
    const char* cfg;
    const char* text;
};
const InputName kInputNames[] = {
    {"none", "-"},
    {"a", "A"}, {"b", "B"}, {"x", "X"}, {"y", "Y"}, {"lb", "LB"}, {"rb", "RB"}, {"lt", "LT"}, {"rt", "RT"},
    {"ls", "LS"}, {"rs", "RS"}, {"start", "Start"}, {"back", "Back"}, {"guide", "Guide"},
    {"dpad_up", "D-Up"}, {"dpad_down", "D-Down"}, {"dpad_left", "D-Left"}, {"dpad_right", "D-Right"},
    {"lx-", "LS left"}, {"lx+", "LS right"}, {"ly-", "LS up"}, {"ly+", "LS down"},
    {"rx-", "RS left"}, {"rx+", "RS right"}, {"ry-", "RS up"}, {"ry+", "RS down"},
    {"left_stick", "Left stick"}, {"right_stick", "Right stick"},
};
static_assert(sizeof(kInputNames) / sizeof(kInputNames[0]) == size_t(PadInput::Count), "input names");

constexpr u32 bit(PadInput in) { return 1u << unsigned(in); }
constexpr int kTriggerThreshold = 16384;   // half travel
constexpr int kDirectionThreshold = 16384;
constexpr s32 kRepeatDelay = 0x80;         // typematic delay (0.5 s)
constexpr s32 kRepeatPeriod = 0x08;        // then ~32 Hz; the keyboard throttle keeps one per 0x50

int sdlButton(PadInput in) {
    switch (in) {
    case I::A: return SDL_GAMEPAD_BUTTON_SOUTH;
    case I::B: return SDL_GAMEPAD_BUTTON_EAST;
    case I::X: return SDL_GAMEPAD_BUTTON_WEST;
    case I::Y: return SDL_GAMEPAD_BUTTON_NORTH;
    case I::LB: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    case I::RB: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    case I::LS: return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    case I::RS: return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    case I::Start: return SDL_GAMEPAD_BUTTON_START;
    case I::Back: return SDL_GAMEPAD_BUTTON_BACK;
    case I::Guide: return SDL_GAMEPAD_BUTTON_GUIDE;
    case I::DpadUp: return SDL_GAMEPAD_BUTTON_DPAD_UP;
    case I::DpadDown: return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    case I::DpadLeft: return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    case I::DpadRight: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    default: return -1;
    }
}

} // namespace

Controller& controller() {
    static Controller instance;
    return instance;
}

const ActionInfo& Controller::info(PadAction a) { return kActions[size_t(a)]; }

void Controller::init() {
    loadFromSettings();
    heldNow_ = heldPrev_ = 0;
    std::memset(active_, 0, sizeof active_);
}

bool Controller::present() const { return settings().padEnabled && sys().gamepad().present(); }

// ---------------------------------------------------------------- names

std::string Controller::inputName(PadInput in) { return kInputNames[size_t(in)].cfg; }

std::string Controller::bindingText(const Binding& b) {
    if (b.input == I::None) return "-";
    std::string s;
    if (b.layer != Layer::Base) {
        const Binding& mod = controller().binding(b.layer == Layer::Shift ? PadAction::Shift : PadAction::Orders);
        s = (mod.input == I::None ? std::string(b.layer == Layer::Shift ? "Shift" : "Orders")
                                  : std::string(kInputNames[size_t(mod.input)].text)) + "+";
    }
    return s + kInputNames[size_t(b.input)].text;
}

std::string Controller::bindingCfg(const Binding& b) {
    if (b.input == I::None) return "none";
    std::string s = b.layer == Layer::Shift ? "shift+" : b.layer == Layer::Orders ? "orders+" : "";
    return s + kInputNames[size_t(b.input)].cfg;
}

bool Controller::parseBinding(const std::string& text, Binding& out) {
    std::string t = text;
    out = none;
    if (t == "none" || t.empty()) return true;
    if (t.rfind("shift+", 0) == 0) { out.layer = Layer::Shift; t = t.substr(6); }
    else if (t.rfind("orders+", 0) == 0) { out.layer = Layer::Orders; t = t.substr(7); }
    for (size_t i = 1; i < size_t(PadInput::Count); ++i) {
        if (t == kInputNames[i].cfg) {
            out.input = PadInput(i);
            return true;
        }
    }
    out = none;
    return false;
}

// ---------------------------------------------------------------- bindings

void Controller::resetDefaults() {
    for (size_t i = 0; i < size_t(PadAction::Count); ++i) bindings_[i] = kActions[i].def;
}

void Controller::setBinding(PadAction a, Binding b) {
    const ActionInfo& ai = info(a);
    if (ai.flags & pad::Modifier) b.layer = Layer::Base;
    if (b.input != I::None) {
        for (size_t i = 0; i < size_t(PadAction::Count); ++i)
            if (PadAction(i) != a && bindings_[i] == b) bindings_[i] = none;
    }
    bindings_[size_t(a)] = b;
}

void Controller::loadFromSettings() {
    resetDefaults();
    const Settings& s = settings();
    for (size_t i = 0; i < size_t(PadAction::Count); ++i) {
        const auto it = s.padBindings.find(kActions[i].cfgName);
        if (it == s.padBindings.end()) continue;
        Binding b;
        if (parseBinding(it->second, b)) {
            if (kActions[i].flags & pad::Motion) {
                if (b.input != I::LeftStick && b.input != I::RightStick && b.input != I::None) continue;
                b.layer = Layer::Base;
            }
            bindings_[i] = b;
        } else {
            logWarn("sealteam.cfg: bind_%s: unknown input '%s'", kActions[i].cfgName, it->second.c_str());
        }
    }
}

void Controller::storeToSettings() {
    Settings& s = settings();
    for (size_t i = 0; i < size_t(PadAction::Count); ++i)
        s.padBindings[kActions[i].cfgName] = bindingCfg(bindings_[i]);
}

// ---------------------------------------------------------------- pad state

u32 Controller::readInputs() const {
    const Gamepad& g = sys().gamepad();
    u32 m = 0;
    for (size_t i = 1; i < size_t(PadInput::Count); ++i) {
        const int b = sdlButton(PadInput(i));
        if (b >= 0 && g.button(b)) m |= 1u << i;
    }
    if (g.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerThreshold) m |= bit(I::LT);
    if (g.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerThreshold) m |= bit(I::RT);
    const int lx = g.axis(SDL_GAMEPAD_AXIS_LEFTX), ly = g.axis(SDL_GAMEPAD_AXIS_LEFTY);
    const int rx = g.axis(SDL_GAMEPAD_AXIS_RIGHTX), ry = g.axis(SDL_GAMEPAD_AXIS_RIGHTY);
    if (lx < -kDirectionThreshold) m |= bit(I::LxMinus);
    if (lx > kDirectionThreshold) m |= bit(I::LxPlus);
    if (ly < -kDirectionThreshold) m |= bit(I::LyMinus);
    if (ly > kDirectionThreshold) m |= bit(I::LyPlus);
    if (rx < -kDirectionThreshold) m |= bit(I::RxMinus);
    if (rx > kDirectionThreshold) m |= bit(I::RxPlus);
    if (ry < -kDirectionThreshold) m |= bit(I::RyMinus);
    if (ry > kDirectionThreshold) m |= bit(I::RyPlus);
    return m;
}

bool Controller::held(PadInput in) const { return in != I::None && (heldNow_ & bit(in)) != 0; }

// One stick: radial dead zone, quadratic response, +-128 like the game
// port driver's scale (4e98:0074), times the sensitivity setting.
void Controller::stickValue(int sdlAxisX, int sdlAxisY, int& x, int& y) const {
    const Gamepad& g = sys().gamepad();
    const Settings& s = settings();
    const double ax = g.axis(sdlAxisX) / 32767.0, ay = g.axis(sdlAxisY) / 32767.0;
    const double dz = std::clamp(s.padDeadZonePct, 0, 60) / 100.0;
    const double mag = std::sqrt(ax * ax + ay * ay);
    x = y = 0;
    if (mag <= dz || mag <= 0.0) return;
    double m = std::min(1.0, (mag - dz) / (1.0 - dz));
    m = m * m;  // fine control near the centre, full deflection unchanged
    const double scale = 128.0 * m / mag * (std::clamp(s.padSensitivityPct, 25, 200) / 100.0);
    x = std::clamp(int(std::lround(ax * scale)), -128, 127);
    y = std::clamp(int(std::lround(ay * scale)), -128, 127);
}

void Controller::stick(int& jx, int& jy, int& moveY) const {
    jx = jy = moveY = 0;
    if (!present() || capture_) return;
    int mx = 0, my = 0, cx = 0, cy = 0;
    const PadInput mv = bindings_[size_t(PadAction::Move)].input;
    const PadInput cam = bindings_[size_t(PadAction::Camera)].input;
    if (mv == I::LeftStick) stickValue(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, mx, my);
    else if (mv == I::RightStick) stickValue(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, mx, my);
    if (cam == I::LeftStick) stickValue(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, cx, cy);
    else if (cam == I::RightStick) stickValue(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, cx, cy);
    jx = std::clamp(mx + cx, -128, 127);
    jy = std::clamp(my + cy, -128, 127);
    moveY = my;
}

// ---------------------------------------------------------------- pump

void Controller::typeKey(int code) { sys().input().pushKey(u16(code)); }

void Controller::pump(InputMode mode) {
    (void)mode;  // every key means the same in all modes (the joystick path of the original)
    heldPrev_ = heldNow_;
    heldNow_ = present() ? readInputs() : 0;
    stickButtons_ = 0;

    // Modifier layer.
    const PadInput shiftIn = bindings_[size_t(PadAction::Shift)].input;
    const PadInput ordersIn = bindings_[size_t(PadAction::Orders)].input;
    layer_ = held(shiftIn) ? Layer::Shift : held(ordersIn) ? Layer::Orders : Layer::Base;

    if (capture_) {
        // Remapping page: report the first new input, produce nothing.
        const u32 rising = heldNow_ & ~heldPrev_ & ~(bit(I::LeftStick) | bit(I::RightStick));
        if (!captureArmed_) captureArmed_ = true;  // edges from now on
        else if (rising && !captureHit_) {
            for (size_t i = 1; i < size_t(PadInput::Count); ++i) {
                const PadInput in = PadInput(i);
                if (!(rising & bit(in)) || in == shiftIn || in == ordersIn) continue;
                captured_ = Binding{in, layer_};
                captureHit_ = true;
                break;
            }
        }
        std::memset(active_, 0, sizeof active_);
        return;
    }
    if (waitRelease_) {
        // The press that ended a capture (or any chord still held) is not a key.
        if (heldNow_ != 0) {
            std::memset(active_, 0, sizeof active_);
            return;
        }
        waitRelease_ = false;
    }

    const s32 now = ticker().time();
    for (size_t i = 0; i < size_t(PadAction::Count); ++i) {
        const ActionInfo& ai = kActions[i];
        if (ai.flags & (pad::Motion | pad::Modifier)) continue;
        const Binding& b = bindings_[i];
        const bool on = b.input != I::None && held(b.input) && layer_ == b.layer;
        const bool edge = on && !active_[i];
        active_[i] = on;
        if (!on) continue;

        if (dialog_ != Dialog::None) {
            // Modal prompt: only confirm / cancel, typed once per press.
            if (!edge) continue;
            if (ai.flags & pad::Button1) typeKey(dialog_ == Dialog::YesNo ? 'y' : k::Enter);
            else if (ai.id == PadAction::Cancel) typeKey(dialog_ == Dialog::YesNo ? 'n' : k::Esc);
            continue;
        }
        if (ai.flags & pad::Button1) { stickButtons_ |= 1; continue; }
        if (ai.flags & pad::Button2) { stickButtons_ |= 2; continue; }
        if (edge) {
            typeKey(ai.key);
            pressedAt_[i] = now;
            repeatAt_[i] = now;
        } else if ((ai.flags & pad::Repeat) && now - pressedAt_[i] >= kRepeatDelay &&
                   now - repeatAt_[i] >= kRepeatPeriod) {
            typeKey(ai.key);
            repeatAt_[i] = now;
        }
    }
}

// ---------------------------------------------------------------- capture

void Controller::beginCapture() {
    capture_ = true;
    captureArmed_ = false;
    captureHit_ = false;
    captured_ = none;
}

void Controller::endCapture() {
    capture_ = false;
    captureHit_ = false;
    std::memset(active_, 0, sizeof active_);
    // A press that ended the capture must not turn into a key.
    waitRelease_ = heldNow_ != 0;
}

bool Controller::captured(Binding& out) {
    if (!captureHit_) return false;
    out = captured_;
    captureHit_ = false;
    return true;
}

Controller::DialogScope::DialogScope(Dialog d) { controller().setDialog(d); }
Controller::DialogScope::~DialogScope() { controller().setDialog(Dialog::None); }

} // namespace st::engine
