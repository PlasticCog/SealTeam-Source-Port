// --test-pad: headless check of the controller mapping layer. Pushes
// synthetic SDL gamepad events (SDL_PushEvent) for a scripted sequence and
// prints the key codes and motion the input layer hands to the game, with
// PASS / FAIL against the default Xbox layout (docs/controller.md).
#include "core/settings.h"
#include "engine/controller.h"
#include "engine/input_layer.h"
#include "engine/ticker.h"
#include "game/devtools.h"
#include "platform/system.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

namespace st::game {

namespace {

using engine::Controller;
using engine::InputMode;
using engine::PadAction;
using engine::PadInput;

void pushButton(SDL_GamepadButton b, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    ev.gbutton.which = 0;
    ev.gbutton.button = u8(b);
    ev.gbutton.down = down;
    SDL_PushEvent(&ev);
}

void pushAxis(SDL_GamepadAxis a, int value) {
    SDL_Event ev{};
    ev.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    ev.gaxis.which = 0;
    ev.gaxis.axis = u8(a);
    ev.gaxis.value = s16(value);
    SDL_PushEvent(&ev);
}

constexpr double kStep = 0.45;  // > 0x50 ticks (0.31 s) so a press passes the keyboard throttle

struct Sample {
    std::vector<int> keys;   // every non-zero key in order
    int dx = 0, dy = 0;      // last non-zero motion
    int motionFrames = 0;    // frames with motion
};

// Runs the game's per-frame input reads for `seconds` in `mode`.
Sample runFrames(double seconds, InputMode mode) {
    Sample s;
    auto& in = engine::input();
    in.setMode(mode);
    const double end = sys().timer().seconds() + seconds;
    while (sys().timer().seconds() < end) {
        engine::ticker().updateGameTime();
        const int key = in.getKey();
        if (key) s.keys.push_back(key);
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        if (dx || dy) {
            s.dx = dx;
            s.dy = dy;
            ++s.motionFrames;
        }
        sys().delayMs(10);
    }
    return s;
}

Sample& operator+=(Sample& a, const Sample& b) {
    a.keys.insert(a.keys.end(), b.keys.begin(), b.keys.end());
    if (b.motionFrames) {
        a.dx = b.dx;
        a.dy = b.dy;
    }
    a.motionFrames += b.motionFrames;
    return a;
}

// Press, hold briefly, release: the keys of both windows (a key-channel
// action is typed on the press edge and read in the same frame).
Sample tap(SDL_GamepadButton b, InputMode mode, double hold = 0.05) {
    pushButton(b, true);
    Sample s = runFrames(hold, mode);
    pushButton(b, false);
    s += runFrames(kStep, mode);
    return s;
}

Sample chord(SDL_GamepadButton mod, SDL_GamepadButton b, InputMode mode) {
    pushButton(mod, true);
    Sample s = runFrames(0.05, mode);
    pushButton(b, true);
    s += runFrames(0.05, mode);
    pushButton(b, false);
    pushButton(mod, false);
    s += runFrames(kStep, mode);
    return s;
}

Sample pull(SDL_GamepadAxis a, InputMode mode) {
    pushAxis(a, 32767);
    Sample s = runFrames(0.05, mode);
    pushAxis(a, 0);
    s += runFrames(kStep, mode);
    return s;
}

int count(const Sample& s, int key) {
    int n = 0;
    for (int k : s.keys) n += (k == key);
    return n;
}

std::string keyList(const Sample& s) {
    std::string out;
    int last = -1, run = 0;
    auto flush = [&] {
        if (last < 0) return;
        char buf[32];
        std::snprintf(buf, sizeof buf, run > 1 ? "%04Xx%d " : "%04X ", last, run);
        out += buf;
    };
    for (int k : s.keys) {
        if (k == last) { ++run; continue; }
        flush();
        last = k;
        run = 1;
    }
    flush();
    return out.empty() ? "(none)" : out;
}

int g_failures = 0;

void report(const char* what, const Sample& s, bool ok, const char* expect) {
    std::printf("%-4s %-34s keys: %-22s motion: (%d,%d) x%d   [expect %s]\n", ok ? "PASS" : "FAIL", what,
                keyList(s).c_str(), s.dx, s.dy, s.motionFrames, expect);
    if (!ok) ++g_failures;
}

void check(const char* what, bool ok) {
    std::printf("%-4s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

constexpr int kEnter = 0x0d, kEsc = 0x1b, kSpace = 0x20, kTab = 0x09;
constexpr int kUp = 0x4800, kDown = 0x5000, kRight = 0x4d00;
constexpr int kF1 = 0x3b00, kAltP = 0x1900;

int testPad(const DevArgs&) {
    Settings& st = settings();
    st.padEnabled = true;
    Controller& pad = engine::controller();
    pad.resetDefaults();
    sys().gamepad().setVirtual(true);
    // Real pads keep their state: make sure the run starts from rest.
    auto& in = engine::input();
    in.setMousePresent(false);  // the captured mouse must not add motion / keys
    sys().video().captureMouse(false);
    in.resetRepeatTimers();
    std::printf("--test-pad: default Xbox layout, synthetic SDL gamepad events\n");
    runFrames(0.4, InputMode::Action);  // settle timers

    // 1. A = Enter, repeating while held (joystick button 1 timer, 0x50 ticks).
    pushButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
    Sample s = runFrames(1.0, InputMode::Action);
    pushButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
    report("A held 1 s", s, count(s, kEnter) >= 2 && count(s, kEnter) <= 4 && s.keys.size() == size_t(count(s, kEnter)),
           "Enter x2..4");
    runFrames(kStep, InputMode::Action);

    // 2. Single presses of the key channel.
    struct Press { const char* what; SDL_GamepadButton b; int key; };
    const Press presses[] = {
        {"B", SDL_GAMEPAD_BUTTON_EAST, kEsc},
        {"X", SDL_GAMEPAD_BUTTON_WEST, 'n'},
        {"Y", SDL_GAMEPAD_BUTTON_NORTH, '-'},
        {"Start", SDL_GAMEPAD_BUTTON_START, kAltP},
        {"Back", SDL_GAMEPAD_BUTTON_BACK, 'm'},
        {"LS click", SDL_GAMEPAD_BUTTON_LEFT_STICK, kTab},
        {"D-pad right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT, kRight},
    };
    for (const Press& p : presses) {
        s = tap(p.b, InputMode::Action);
        char what[64], expect[32];
        std::snprintf(what, sizeof what, "%s tap", p.what);
        std::snprintf(expect, sizeof expect, "%04X once", p.key);
        report(what, s, s.keys.size() == 1 && s.keys[0] == p.key, expect);
    }

    // 3. Button 2 (RS click) = Space with the 0x100 tick timer outside menus.
    pushButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
    s = runFrames(1.2, InputMode::Action);
    pushButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
    report("RS held 1.2 s (action)", s, count(s, kSpace) >= 1 && count(s, kSpace) <= 2 && s.keys.size() == size_t(count(s, kSpace)),
           "Space x1..2");
    runFrames(kStep, InputMode::Action);

    // 4. Chords: Shift (LB) + A = F1, Orders (RB) + A = 'w', Shift + Y = '+'.
    s = chord(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_SOUTH, InputMode::Action);
    report("LB + A", s, s.keys.size() == 1 && s.keys[0] == kF1, "F1 only");
    s = chord(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_SOUTH, InputMode::Action);
    report("RB + A", s, s.keys.size() == 1 && s.keys[0] == 'w', "'w' only");
    s = chord(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_NORTH, InputMode::Action);
    report("LB + Y", s, s.keys.size() == 1 && s.keys[0] == '+', "'+' only");

    // 5. Triggers: LT = 'g', RT = Enter.
    s = pull(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, InputMode::Action);
    report("LT pull", s, s.keys.size() == 1 && s.keys[0] == 'g', "'g' once");
    s = pull(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, InputMode::Action);
    report("RT pull", s, s.keys.size() == 1 && s.keys[0] == kEnter, "Enter once");

    // 6. D-pad Up held: Up, then typematic repeats through the keyboard throttle.
    pushButton(SDL_GAMEPAD_BUTTON_DPAD_UP, true);
    s = runFrames(1.3, InputMode::Action);
    pushButton(SDL_GAMEPAD_BUTTON_DPAD_UP, false);
    report("D-pad up held 1.3 s", s, count(s, kUp) >= 2 && count(s, kUp) <= 6 && s.keys.size() == size_t(count(s, kUp)),
           "Up x2..6");
    runFrames(kStep, InputMode::Action);

    // 7. Left stick: Y axis -> Down / Up keys (0x60 ticks) + motion; X axis -> motion only.
    pushAxis(SDL_GAMEPAD_AXIS_LEFTY, -32767);
    s = runFrames(0.8, InputMode::Action);
    pushAxis(SDL_GAMEPAD_AXIS_LEFTY, 0);
    report("LS up 0.8 s (action)", s, count(s, kUp) >= 1 && count(s, kUp) <= 3 && s.keys.size() == size_t(count(s, kUp)) &&
           s.dx == 0 && s.dy < 0, "Up x1..3, dy < 0");
    runFrames(kStep, InputMode::Action);
    pushAxis(SDL_GAMEPAD_AXIS_LEFTX, 32767);
    s = runFrames(0.3, InputMode::Action);
    pushAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    report("LS full right (action)", s, s.keys.empty() && s.dx == 12 && s.dy == 0, "dx 12, no keys");
    runFrames(0.2, InputMode::Action);
    pushAxis(SDL_GAMEPAD_AXIS_LEFTX, 16000);
    s = runFrames(0.3, InputMode::Action);
    pushAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    report("LS half right (action)", s, s.keys.empty() && s.dx >= 1 && s.dx <= 4 && s.dy == 0, "dx 1..4 (curve)");
    runFrames(0.2, InputMode::Action);
    pushAxis(SDL_GAMEPAD_AXIS_LEFTX, 4000);
    s = runFrames(0.3, InputMode::Action);
    pushAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    report("LS inside dead zone", s, s.keys.empty() && s.motionFrames == 0, "nothing");

    // 8. Right stick: camera = motion, no axis keys.
    pushAxis(SDL_GAMEPAD_AXIS_RIGHTX, -32767);
    s = runFrames(0.5, InputMode::Action);
    pushAxis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
    report("RS full left (action)", s, s.keys.empty() && s.dx < 0 && s.dy == 0, "dx < 0, no keys");
    runFrames(0.2, InputMode::Action);
    pushAxis(SDL_GAMEPAD_AXIS_RIGHTY, 32767);
    s = runFrames(0.5, InputMode::Action);
    pushAxis(SDL_GAMEPAD_AXIS_RIGHTY, 0);
    report("RS full down (action)", s, s.keys.empty() && s.dx == 0 && s.dy > 0, "dy > 0, no keys");
    runFrames(0.2, InputMode::Action);

    // 9. Menu / map modes: pointer motion (>> 2), no axis keys, D-pad = arrows.
    pushAxis(SDL_GAMEPAD_AXIS_LEFTX, 32767);
    s = runFrames(0.3, InputMode::Menu);
    pushAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    report("LS full right (menu)", s, s.keys.empty() && s.dx == 25 && s.dy == 0, "dx 25");
    runFrames(0.2, InputMode::Menu);
    pushAxis(SDL_GAMEPAD_AXIS_LEFTY, 32767);
    s = runFrames(0.8, InputMode::Map);
    pushAxis(SDL_GAMEPAD_AXIS_LEFTY, 0);
    report("LS full down (map)", s, s.keys.empty() && s.dy == 25, "dy 25, no Down key");
    runFrames(0.2, InputMode::Map);
    s = tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, InputMode::Menu);
    report("D-pad down tap (menu)", s, s.keys.size() == 1 && s.keys[0] == kDown, "Down once");
    pushButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
    s = runFrames(0.2, InputMode::Menu);
    pushButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
    report("A tap (menu)", s, s.keys.size() == 1 && s.keys[0] == kEnter, "Enter once");
    runFrames(kStep, InputMode::Menu);

    // 10. Modal prompts read the BIOS queue directly: A / B answer them.
    {
        Controller::DialogScope scope(Controller::Dialog::YesNo);
        sys().input().flushKeys();
        pushButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
        sys().pump();
        pad.pump(InputMode::Action);
        const int y = sys().input().readKey();
        pushButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
        pushButton(SDL_GAMEPAD_BUTTON_EAST, true);
        sys().pump();
        pad.pump(InputMode::Action);
        const int n = sys().input().readKey();
        pushButton(SDL_GAMEPAD_BUTTON_EAST, false);
        sys().pump();
        pad.pump(InputMode::Action);
        check("Y/N prompt: A types 'y', B types 'n'", y == 'y' && n == 'n');
    }
    {
        Controller::DialogScope scope(Controller::Dialog::Enter);
        sys().input().flushKeys();
        pushButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
        sys().pump();
        pad.pump(InputMode::Action);
        const int e = sys().input().readKey();
        pushButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
        pushButton(SDL_GAMEPAD_BUTTON_WEST, true);
        sys().pump();
        pad.pump(InputMode::Action);
        const int muted = sys().input().readKey();
        pushButton(SDL_GAMEPAD_BUTTON_WEST, false);
        sys().pump();
        pad.pump(InputMode::Action);
        check("Enter prompt: A types Enter, X is muted", (e & 0xff) == kEnter && muted == 0);
    }
    runFrames(kStep, InputMode::Action);

    // 11. Remapping: capture, chord capture, rebind, persistence names.
    {
        engine::Binding b;
        pad.beginCapture();
        runFrames(0.05, InputMode::Menu);
        pushButton(SDL_GAMEPAD_BUTTON_WEST, true);
        runFrames(0.05, InputMode::Menu);
        const bool got = pad.captured(b);
        pushButton(SDL_GAMEPAD_BUTTON_WEST, false);
        runFrames(0.05, InputMode::Menu);
        check("capture: X pressed -> X (base layer)", got && b.input == PadInput::X && b.layer == engine::Layer::Base);
        pushButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
        runFrames(0.05, InputMode::Menu);
        pushButton(SDL_GAMEPAD_BUTTON_DPAD_UP, true);
        runFrames(0.05, InputMode::Menu);
        const bool got2 = pad.captured(b);
        pushButton(SDL_GAMEPAD_BUTTON_DPAD_UP, false);
        pushButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
        s = runFrames(0.05, InputMode::Menu);
        check("capture: LB held + D-pad up -> Shift+D-Up, no keys typed",
              got2 && b.input == PadInput::DpadUp && b.layer == engine::Layer::Shift && s.keys.empty());
        pad.endCapture();
        runFrames(kStep, InputMode::Action);

        pad.setBinding(PadAction::NextWeapon, engine::Binding{PadInput::DpadRight, engine::Layer::Base});
        check("rebind clears the old owner of D-pad right", pad.binding(PadAction::Right).input == PadInput::None);
        s = tap(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, InputMode::Action);
        report("D-pad right after rebind", s, s.keys.size() == 1 && s.keys[0] == 'n', "'n' once");
        pad.storeToSettings();
        check("settings: bind_next_weapon = dpad_right", st.padBindings["next_weapon"] == "dpad_right");
        check("settings: bind_right = none", st.padBindings["right"] == "none");
        pad.resetDefaults();
        check("reset: X = next weapon again", pad.binding(PadAction::NextWeapon).input == PadInput::X);
        engine::Binding parsed;
        check("cfg round trip: shift+dpad_up",
              Controller::parseBinding("shift+dpad_up", parsed) && Controller::bindingCfg(parsed) == "shift+dpad_up" &&
                  Controller::bindingCfg(engine::Binding{PadInput::A, engine::Layer::Orders}) == "orders+a");
        check("cfg: unknown input rejected", !Controller::parseBinding("bogus", parsed));
    }

    // 12. Controller switched off: nothing gets through.
    st.padEnabled = false;
    pushButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
    s = runFrames(kStep, InputMode::Action);
    pushButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
    runFrames(0.1, InputMode::Action);
    st.padEnabled = true;
    report("A held with pad disabled", s, s.keys.empty() && s.motionFrames == 0, "nothing");

    std::printf("--test-pad: %d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}

const DevCommand kTestPad("--test-pad", "check the controller mapping with synthetic gamepad events", testPad);

} // namespace

} // namespace st::game
