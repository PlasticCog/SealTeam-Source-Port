// Keyboard, mouse and joystick state, exposed the way the DOS game saw it:
// a key-down table indexed by PC/XT set-1 scancodes (what an INT 9 handler
// reads from port 0x60), a BIOS-style keystroke queue of (scancode << 8 | ascii)
// words (INT 16h), and mouse coordinates in 320x200 screen space (INT 33h).
#pragma once

#include "core/common.h"

#include <array>
#include <deque>

union SDL_Event;

namespace st {

// Commonly used set-1 scancodes.
namespace sc {
constexpr u8 Esc = 0x01, Key1 = 0x02, Key0 = 0x0b, Minus = 0x0c, Equals = 0x0d, Backspace = 0x0e,
             Tab = 0x0f, Q = 0x10, W = 0x11, E = 0x12, R = 0x13, T = 0x14, Y = 0x15, U = 0x16,
             I = 0x17, O = 0x18, P = 0x19, Enter = 0x1c, Ctrl = 0x1d, A = 0x1e, S = 0x1f, D = 0x20,
             F = 0x21, G = 0x22, H = 0x23, J = 0x24, K = 0x25, L = 0x26, LShift = 0x2a, Z = 0x2c,
             X = 0x2d, C = 0x2e, V = 0x2f, B = 0x30, N = 0x31, M = 0x32, RShift = 0x36, Alt = 0x38,
             Space = 0x39, CapsLock = 0x3a, F1 = 0x3b, F10 = 0x44, NumLock = 0x45, ScrollLock = 0x46,
             Home = 0x47, Up = 0x48, PgUp = 0x49, KpMinus = 0x4a, Left = 0x4b, Center = 0x4c,
             Right = 0x4d, KpPlus = 0x4e, End = 0x4f, Down = 0x50, PgDn = 0x51, Ins = 0x52,
             Del = 0x53, F11 = 0x57, F12 = 0x58;
}

// Port: synthetic keystroke words that no keyboard sends (scan codes above
// 0x80), typed into the BIOS queue by the platform and controller layers.
namespace synth {
constexpr u16 RecentreCamera = 0xF100;  // middle mouse button: camera back behind the leader
}

struct MouseState {
    int x = 160, y = 100;  // 320x200 coordinates
    u8 buttons = 0;        // bit0 left, bit1 right, bit2 middle
};

class Input {
public:
    void handleEvent(const SDL_Event& ev, int logicalW, int logicalH);

    bool keyDown(u8 scancode) const { return keys_[scancode & 0x7f]; }
    void clearKeys();

    // BIOS keystroke queue.
    bool keyAvailable() const { return !queue_.empty(); }
    u16 readKey();          // pops; 0 if empty
    u16 peekKey() const { return queue_.empty() ? 0 : queue_.front(); }
    void flushKeys() { queue_.clear(); }
    // Remove the first queued keystroke equal to `word`; false if there is none.
    bool takeKey(u16 word);
    // Inject a keystroke (scancode << 8 | ascii) as if typed: used by the
    // controller mapping layer so pad buttons go through the same BIOS
    // queue and throttling as keyboard keys.
    void pushKey(u16 k) { if (queue_.size() < 16) queue_.push_back(k); }

    const MouseState& mouse() const { return mouse_; }
    void setMousePos(int x, int y);
    // Mouse movement since the last call, in 320x200 screen pixels (the
    // fractional remainder is kept for the next call).
    void takeMotion(int& dx, int& dy);
    // Inject relative motion (scripted tests).
    void addMotion(int dx, int dy) { relX_ += float(dx); relY_ += float(dy); }
    // Port: mouse wheel notches since the last call, up = positive (the
    // fraction of a precision wheel is kept for the next call).
    int takeWheel();
    void addWheel(int notches) { wheel_ += float(notches); }  // scripted tests

    // Last raw scancode seen (make or break with bit 7), like port 0x60.
    u8 lastScancode() const { return lastScancode_; }

private:
    std::array<bool, 128> keys_{};
    std::deque<u16> queue_;
    MouseState mouse_;
    float relX_ = 0.0f, relY_ = 0.0f;
    float wheel_ = 0.0f;
    u8 lastScancode_ = 0;
};

} // namespace st
