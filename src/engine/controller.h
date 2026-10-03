// Game controller mapping layer (port-only, docs/controller.md).
//
// The game reads input as BIOS-style key codes (InputLayer::getKey) and a
// motion vector (InputLayer::getMotion); the original also had a game-port
// joystick whose buttons became Enter / Space and whose axes fed the motion
// vector and the Up / Down keys (docs/re/seg_19ac.md 3.2-3.3). This layer
// turns an SDL gamepad into exactly those inputs:
//
//  * "stick channel": the two joystick buttons and the stick axes are
//    handed to InputLayer, which applies the original's joystick timers;
//  * "key channel": every other bound action is typed into the BIOS key
//    queue on the press edge (and repeated like a held keyboard key for
//    the arrow-type actions), so the keyboard throttle of InputLayer
//    (one key per 0x50 ticks) applies unchanged.
//
// Bindings: each action has one input (a button, trigger, stick direction
// or a whole stick) on one layer (base, or while the Shift / Orders
// modifier is held). Persisted in sealteam.cfg as bind_<action> = <input>.
#pragma once

#include "core/common.h"

#include <string>

namespace st::engine {

enum class InputMode;  // input_layer.h

enum class PadInput : u8 {
    None,
    A, B, X, Y, LB, RB, LT, RT, LS, RS, Start, Back, Guide,
    DpadUp, DpadDown, DpadLeft, DpadRight,
    LxMinus, LxPlus, LyMinus, LyPlus, RxMinus, RxPlus, RyMinus, RyPlus,  // stick directions
    LeftStick, RightStick,                                               // whole stick (motion actions)
    Count
};

enum class Layer : u8 { Base, Shift, Orders };

struct Binding {
    PadInput input = PadInput::None;
    Layer layer = Layer::Base;
    bool operator==(const Binding& o) const { return input == o.input && layer == o.layer; }
    bool operator!=(const Binding& o) const { return !(*this == o); }
};

enum class PadAction : u8 {
    // Motion (whole sticks).
    Move, Camera,
    // Modifiers (held).
    Shift, Orders,
    // Stick channel: joystick buttons 1 and 2 of the original.
    Select, Fire, Button2,
    // Key channel.
    Cancel, NextWeapon, PostureDown, PostureUp, CycleTool, UseTool, Grenade, NextGrenade,
    NextTarget, RateOfFire, Map, Pause,
    Up, Down, Left, Right,
    ViewPointMan, ViewTeam, ViewSupport1, ViewSupport2, ViewSupport3, ViewSupport4,
    ViewSplitA, ViewSplitB, ViewTarget,
    TimeCompression, AutoTarget, TeamInfo, ExposeTrap, Dive, QuitGame,
    RecentreCamera,  // port: camera back behind the leader (also the middle mouse button)
    OrderFieldOfFire, OrderAtTarget, OrderAtWill, OrderCeaseFire,
    OrderHalt, OrderSearch, OrderSplit, OrderJoin,
    OrderColumn, OrderInLine, OrderDiamond, OrderVee,
    Count
};

namespace pad {
constexpr u8 Motion = 0x01;    // bound to a whole stick
constexpr u8 Modifier = 0x02;  // layer switch while held
constexpr u8 Button1 = 0x04;   // original joystick button 1 (Enter)
constexpr u8 Button2 = 0x08;   // original joystick button 2 (Space)
constexpr u8 Repeat = 0x10;    // repeats while held, like a typematic key
} // namespace pad

struct ActionInfo {
    PadAction id;
    const char* cfgName;   // settings key (bind_<cfgName>)
    const char* label;     // menu text
    int key;               // BIOS-style key code typed for the action (0: none)
    u8 flags;
    Binding def;           // default Xbox layout
};

class Controller {
public:
    enum class Dialog { None, Enter, YesNo };

    void init();                        // bindings from the settings
    bool present() const;               // pad connected and enabled

    // Per poll (InputLayer::getKey, dialog key polls): reads the pad,
    // types key-channel presses into the BIOS queue.
    void pump(InputMode mode);
    // Stick channel: bit 0 = joystick button 1 held, bit 1 = button 2 held.
    int stickButtons() const { return stickButtons_; }
    // Combined stick deflection for the motion vector (+-128 scale, dead
    // zone and response curve applied); `moveY` is the Move stick alone
    // (the original's Up / Down axis keys).
    void stick(int& jx, int& jy, int& moveY) const;

    // ui_dialog_prompt: while a modal dialog polls the BIOS queue directly,
    // Select / Fire and Cancel type Enter / Esc (or y / n); other actions
    // are muted so they cannot end up in a text entry.
    void setDialog(Dialog d) { dialog_ = d; }
    struct DialogScope {
        explicit DialogScope(Dialog d);
        ~DialogScope();
    };

    // Remapping.
    static int actionCount() { return int(PadAction::Count); }
    static const ActionInfo& info(PadAction a);
    const Binding& binding(PadAction a) const { return bindings_[size_t(a)]; }
    // Assigns and clears any other action that used the same input + layer.
    void setBinding(PadAction a, Binding b);
    void resetDefaults();
    void loadFromSettings();
    void storeToSettings();   // fills settings().padBindings (caller saves)

    // Capture for the remapping page: while active pump() types nothing;
    // captured() reports the first input pressed since beginCapture()
    // (with the layer of a held modifier), stick directions included.
    void beginCapture();
    void endCapture();
    bool capturing() const { return capture_; }
    bool captured(Binding& out);

    static std::string inputName(PadInput in);           // cfg / display name ("dpad_up")
    static std::string bindingText(const Binding& b);    // display text ("LB+A")
    static bool parseBinding(const std::string& text, Binding& out);
    static std::string bindingCfg(const Binding& b);     // "lb+a", "none"

private:
    bool held(PadInput in) const;
    u32 readInputs() const;             // bit per PadInput from the pad state
    void typeKey(int code);             // into the BIOS queue
    void stickValue(int sdlAxisX, int sdlAxisY, int& x, int& y) const;

    Binding bindings_[size_t(PadAction::Count)];
    u32 heldNow_ = 0, heldPrev_ = 0;   // raw inputs
    Layer layer_ = Layer::Base;
    int stickButtons_ = 0;
    Dialog dialog_ = Dialog::None;
    bool capture_ = false;
    bool waitRelease_ = false;   // after a capture: mute until every input is released
    bool captureArmed_ = false;
    u32 captureBase_ = 0;
    Binding captured_;
    bool captureHit_ = false;
    // Key-channel state per action.
    bool active_[size_t(PadAction::Count)] = {};
    s32 pressedAt_[size_t(PadAction::Count)] = {};
    s32 repeatAt_[size_t(PadAction::Count)] = {};
};

Controller& controller();

} // namespace st::engine
