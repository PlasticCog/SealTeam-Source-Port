#include "platform/input.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace st {
namespace {

u8 toSet1(SDL_Scancode s) {
    switch (s) {
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_1: return 0x02;
    case SDL_SCANCODE_2: return 0x03;
    case SDL_SCANCODE_3: return 0x04;
    case SDL_SCANCODE_4: return 0x05;
    case SDL_SCANCODE_5: return 0x06;
    case SDL_SCANCODE_6: return 0x07;
    case SDL_SCANCODE_7: return 0x08;
    case SDL_SCANCODE_8: return 0x09;
    case SDL_SCANCODE_9: return 0x0a;
    case SDL_SCANCODE_0: return 0x0b;
    case SDL_SCANCODE_MINUS: return 0x0c;
    case SDL_SCANCODE_EQUALS: return 0x0d;
    case SDL_SCANCODE_BACKSPACE: return 0x0e;
    case SDL_SCANCODE_TAB: return 0x0f;
    case SDL_SCANCODE_Q: return 0x10;
    case SDL_SCANCODE_W: return 0x11;
    case SDL_SCANCODE_E: return 0x12;
    case SDL_SCANCODE_R: return 0x13;
    case SDL_SCANCODE_T: return 0x14;
    case SDL_SCANCODE_Y: return 0x15;
    case SDL_SCANCODE_U: return 0x16;
    case SDL_SCANCODE_I: return 0x17;
    case SDL_SCANCODE_O: return 0x18;
    case SDL_SCANCODE_P: return 0x19;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1a;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1b;
    case SDL_SCANCODE_RETURN: return 0x1c;
    case SDL_SCANCODE_KP_ENTER: return 0x1c;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1d;
    case SDL_SCANCODE_A: return 0x1e;
    case SDL_SCANCODE_S: return 0x1f;
    case SDL_SCANCODE_D: return 0x20;
    case SDL_SCANCODE_F: return 0x21;
    case SDL_SCANCODE_G: return 0x22;
    case SDL_SCANCODE_H: return 0x23;
    case SDL_SCANCODE_J: return 0x24;
    case SDL_SCANCODE_K: return 0x25;
    case SDL_SCANCODE_L: return 0x26;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_LSHIFT: return 0x2a;
    case SDL_SCANCODE_BACKSLASH: return 0x2b;
    case SDL_SCANCODE_Z: return 0x2c;
    case SDL_SCANCODE_X: return 0x2d;
    case SDL_SCANCODE_C: return 0x2e;
    case SDL_SCANCODE_V: return 0x2f;
    case SDL_SCANCODE_B: return 0x30;
    case SDL_SCANCODE_N: return 0x31;
    case SDL_SCANCODE_M: return 0x32;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_CAPSLOCK: return 0x3a;
    case SDL_SCANCODE_F1: return 0x3b;
    case SDL_SCANCODE_F2: return 0x3c;
    case SDL_SCANCODE_F3: return 0x3d;
    case SDL_SCANCODE_F4: return 0x3e;
    case SDL_SCANCODE_F5: return 0x3f;
    case SDL_SCANCODE_F6: return 0x40;
    case SDL_SCANCODE_F7: return 0x41;
    case SDL_SCANCODE_F8: return 0x42;
    case SDL_SCANCODE_F9: return 0x43;
    case SDL_SCANCODE_F10: return 0x44;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_SCROLLLOCK: return 0x46;
    case SDL_SCANCODE_KP_7: case SDL_SCANCODE_HOME: return 0x47;
    case SDL_SCANCODE_KP_8: case SDL_SCANCODE_UP: return 0x48;
    case SDL_SCANCODE_KP_9: case SDL_SCANCODE_PAGEUP: return 0x49;
    case SDL_SCANCODE_KP_MINUS: return 0x4a;
    case SDL_SCANCODE_KP_4: case SDL_SCANCODE_LEFT: return 0x4b;
    case SDL_SCANCODE_KP_5: return 0x4c;
    case SDL_SCANCODE_KP_6: case SDL_SCANCODE_RIGHT: return 0x4d;
    case SDL_SCANCODE_KP_PLUS: return 0x4e;
    case SDL_SCANCODE_KP_1: case SDL_SCANCODE_END: return 0x4f;
    case SDL_SCANCODE_KP_2: case SDL_SCANCODE_DOWN: return 0x50;
    case SDL_SCANCODE_KP_3: case SDL_SCANCODE_PAGEDOWN: return 0x51;
    case SDL_SCANCODE_KP_0: case SDL_SCANCODE_INSERT: return 0x52;
    case SDL_SCANCODE_KP_PERIOD: case SDL_SCANCODE_DELETE: return 0x53;
    case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;
    default: return 0;
    }
}

// ASCII a BIOS would deliver for keys that don't produce SDL text input.
u8 controlAscii(u8 scancode) {
    switch (scancode) {
    case sc::Esc: return 0x1b;
    case sc::Enter: return 0x0d;
    case sc::Backspace: return 0x08;
    case sc::Tab: return 0x09;
    default: return 0;
    }
}

} // namespace

void Input::handleEvent(const SDL_Event& ev, int logicalW, int logicalH) {
    switch (ev.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const u8 code = toSet1(ev.key.scancode);
        if (!code) break;
        const bool down = ev.type == SDL_EVENT_KEY_DOWN;
        keys_[code] = down;
        lastScancode_ = down ? code : u8(code | 0x80);
        // The BIOS reports no keystroke for the modifier and lock keys
        // themselves (a queued Ctrl would be read as a key of its own and
        // the Ctrl+letter behind it flushed with it).
        const bool modifier = code == sc::LShift || code == sc::RShift || code == sc::Ctrl || code == sc::Alt ||
                              code == sc::CapsLock || code == sc::NumLock || code == sc::ScrollLock;
        if (!down || modifier) break;
        const SDL_Keymod mod = ev.key.mod;
        u8 ascii = controlAscii(code);
        const SDL_Keycode kc = ev.key.key;  // unshifted, layout aware
        if (!ascii && kc >= 32 && kc < 127) {
            ascii = u8(kc);
            const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
            const bool caps = (mod & SDL_KMOD_CAPS) != 0;
            if (ascii >= 'a' && ascii <= 'z') {
                if (mod & SDL_KMOD_ALT) ascii = 0;  // BIOS reports Alt+letter as scancode only
                else if (mod & SDL_KMOD_CTRL) ascii = u8(ascii - 'a' + 1);  // control code, whatever Shift / Caps Lock
                else if (shift != caps) ascii = u8(ascii - 32);
            } else if (shift) {
                static const char* kPlain = "1234567890-=[];'`\\,./";
                static const char* kShift = "!@#$%^&*()_+{}:\"~|<>?";
                for (int i = 0; kPlain[i]; ++i)
                    if (ascii == u8(kPlain[i])) { ascii = u8(kShift[i]); break; }
            }
        }
        if (queue_.size() < 16) queue_.push_back(u16((code << 8) | ascii));  // BIOS buffer holds 15
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        // The caller converts events to render coordinates (320 x logicalH).
        setMousePos(int(ev.motion.x * 320.0f / float(logicalW)), int(ev.motion.y * 200.0f / float(logicalH)));
        // Raw hand motion (see System::pump): 1 mouse count = 1 game pixel,
        // the sensitivity the DOS driver gave at the game's 4:6 mickey ratio.
        relX_ += float(ev.motion.xrel);
        relY_ += float(ev.motion.yrel);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        u8 bit = 0;
        if (ev.button.button == SDL_BUTTON_LEFT) bit = 1;
        else if (ev.button.button == SDL_BUTTON_RIGHT) bit = 2;
        else if (ev.button.button == SDL_BUTTON_MIDDLE) bit = 4;
        if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) mouse_.buttons |= bit;
        else mouse_.buttons &= u8(~bit);
        break;
    }
    default:
        break;
    }
}

void Input::clearKeys() {
    keys_.fill(false);
    queue_.clear();
}

bool Input::takeKey(u16 word) {
    const auto it = std::find(queue_.begin(), queue_.end(), word);
    if (it == queue_.end()) return false;
    queue_.erase(it);
    return true;
}

u16 Input::readKey() {
    if (queue_.empty()) return 0;
    const u16 k = queue_.front();
    queue_.pop_front();
    return k;
}

void Input::takeMotion(int& dx, int& dy) {
    dx = int(relX_);
    dy = int(relY_);
    relX_ -= float(dx);
    relY_ -= float(dy);
}

void Input::setMousePos(int x, int y) {
    mouse_.x = std::clamp(x, 0, 319);
    mouse_.y = std::clamp(y, 0, 199);
}

} // namespace st
