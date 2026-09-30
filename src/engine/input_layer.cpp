#include "engine/input_layer.h"

#include "engine/controller.h"
#include "engine/ticker.h"
#include "platform/system.h"

#include <algorithm>
#include <cstdlib>

namespace st::engine {

namespace {
constexpr int kStickRange = 211;
constexpr int kStickCentre = 105;
} // namespace

InputLayer& input() {
    static InputLayer instance;
    return instance;
}

void InputLayer::init() {
    // The game port joystick is a game controller through the mapping layer
    // (engine/controller.h); presence is re-checked at every poll (hot-plug).
    controller().init();
    joyPresent_ = controller().present();
    mousePresent_ = true;
    mouseX_ = mouseY_ = kStickCentre;
    centreX_ = mouseX_;
    centreY_ = mouseY_;
    mode_ = InputMode::Menu;
}

bool InputLayer::elapsed(s32& timer, int period) {
    const s32 now = ticker().time();
    if (now - timer < period) return false;
    timer = now;
    return true;
}

void InputLayer::resetRepeatTimers() {
    const s32 now = ticker().time();
    tJoyButton1_ = tJoyButton2_ = tJoyAxis_ = now;
    tMouseLeft_ = tMouseRight_ = tMouseAxis_ = tKeyboard_ = now;
}

void InputLayer::resetButton2Timers() {
    tJoyButton2_ = 0;
    tMouseRight_ = 0;
}

void InputLayer::flushKeyboard() { sys().input().flushKeys(); }

// Accumulate host mouse movement into the virtual stick position, clamped to
// the range the game programs into the mouse driver.
void InputLayer::pumpMouse() {
    int dx = 0, dy = 0;
    sys().input().takeMotion(dx, dy);
    mouseX_ = std::clamp(mouseX_ + dx, 0, kStickRange);
    mouseY_ = std::clamp(mouseY_ + dy, 0, kStickRange);
}

// INT 16h: AH=1 poll, AH=0 read; ASCII if non-zero, else scancode << 8.
int InputLayer::pollBiosKey() {
    Input& in = sys().input();
    if (!in.keyAvailable()) return 0;
    const u16 k = in.readKey();
    return (k & 0xff) ? (k & 0xff) : k;
}

int InputLayer::getKey() {
    sys().pump();
    // 1. Joystick (a game controller): button 1 -> Enter, button 2 -> Space
    //    and the Y axis -> Down / Up with the original's timers. The mapping
    //    layer types every other bound action into the BIOS queue, so it is
    //    read by the keyboard path below with the keyboard's throttle.
    Controller& pad = controller();
    pad.pump(mode_);
    joyPresent_ = pad.present();
    if (joyPresent_) {
        const int buttons = pad.stickButtons();
        if (buttons == 1 && elapsed(tJoyButton1_, 0x50)) return key::Enter;
        if (buttons == 2 && elapsed(tJoyButton2_, mode_ == InputMode::Menu ? 0x50 : 0x100)) return key::Space;
        if (mode_ == InputMode::Action && elapsed(tJoyAxis_, 0x60)) {
            int jx, jy, moveY;
            pad.stick(jx, jy, moveY);
            if (moveY > 0x30) return key::Down;
            if (moveY < -0x30) return key::Up;
        }
    }
    // 2. Mouse buttons.
    if (mousePresent_) {
        pumpMouse();
        const u8 buttons = sys().input().mouse().buttons;
        if ((buttons & 1) && elapsed(tMouseLeft_, 0x50)) return key::Enter;
        if ((buttons & 2) && elapsed(tMouseRight_, mode_ == InputMode::Menu ? 0x50 : 0x100)) return key::Space;
        if (mode_ == InputMode::Action && elapsed(tMouseAxis_, 0x60)) {
            const int dy = mouseY_ - centreY_;
            mouseY_ = kStickCentre;
            if (dy > 0x56) return key::Down;
            if (dy < -0x56) return key::Up;
        }
    }
    // 3. Keyboard, throttled: one key per 0x50 ticks, the rest is discarded.
    if (elapsed(tKeyboard_, 0x50) || keyPollNow_) {
        int k = pollBiosKey();
        flushKeyboard();
        keyPollNow_ = (k == 0);
        if (k >= 'A' && k <= 'Z') k |= 0x60;
        if (mode_ != InputMode::Menu && k == key::Space) k = 'm';
        return k;
    }
    flushKeyboard();
    return 0;
}

void InputLayer::getMotion(int& dx, int& dy) {
    dx = dy = 0;
    // Joystick first (4e98:0074 scale, +-128); the mouse only when the stick
    // gave (0, 0). Shifts are arithmetic like the original's.
    if (joyPresent_) {
        int jx, jy, moveY;
        controller().stick(jx, jy, moveY);
        if (mode_ == InputMode::Action) {
            dx = std::abs(jx) > 5 ? (std::clamp(jx, -100, 100) >> 3) : 0;
            dy = std::abs(jy) > 5 ? (std::clamp(jy, -100, 100) >> 4) : 0;
        } else {
            dx = std::clamp(jx, -100, 100) >> 2;
            dy = std::clamp(jy, -100, 100) >> 2;
        }
        if (dx != 0 || dy != 0) return;
    }
    if (!mousePresent_) return;
    pumpMouse();
    const int jx = mouseX_ - centreX_, jy = mouseY_ - centreY_;
    mouseX_ = mouseY_ = kStickCentre;
    if (mode_ == InputMode::Action) {
        dx = std::abs(jx) > 2 ? (std::clamp(jx, -100, 100) >> 3) : 0;
        dy = jy;
    } else {
        dx = jx;
        dy = jy;
    }
}

} // namespace st::engine
