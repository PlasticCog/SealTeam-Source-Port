// The game's input layer (segment 19ac:2574-2B3B, docs/re/seg_19ac.md 3):
// keyboard, mouse and joystick merged into BIOS-style key codes plus a motion
// vector. Key codes are ASCII, or scancode << 8 for keys without ASCII.
#pragma once

#include "core/common.h"

namespace st::engine {

namespace key {
constexpr int Enter = 0x0d, Esc = 0x1b, Space = 0x20;
constexpr int Up = 0x4800, Down = 0x5000, Left = 0x4b00, Right = 0x4d00;
constexpr int AltS = 0x1f00, AltD = 0x2000, AltM = 0x3200, AltX = 0x2d00;
constexpr int F10 = 0x4400;
// Port: Ctrl+H as its BIOS word (scan 0x23, ASCII 8), kept apart from
// Backspace (ASCII 8 too) by InputLayer::pollBiosKey.
constexpr int Help = 0x2308;
} // namespace key

enum class InputMode { Action = 0, Menu = 1, Map = 2 };  // g_input_mode DS:D820

class InputLayer {
public:
    void init();                             // input_init (2574)
    void setMode(InputMode m) { mode_ = m; }
    InputMode mode() const { return mode_; }

    int getKey();                            // input_get_key (2647)
    void getMotion(int& dx, int& dy);        // input_get_motion (29CD)
    void resetRepeatTimers();                // input_reset_repeat_timers (25F6)
    void resetButton2Timers();               // input_reset_button2_timers (262F)
    void flushKeyboard();                    // input_flush_keyboard (263E)

    bool mousePresent() const { return mousePresent_; }
    bool joystickPresent() const { return joyPresent_; }
    void setMousePresent(bool on) { mousePresent_ = on; }  // tests: pad only

    // Port: Ctrl+H on any screen shows the key reference. The game installs
    // the screen; getKey() runs it in place of the key and returns 0.
    void setHelpHook(void (*hook)()) { helpHook_ = hook; }
    void showHelp() { if (helpHook_) helpHook_(); }

private:
    int pollBiosKey();                       // 2B3B
    void pumpMouse();                        // mouse driver emulation
    bool elapsed(s32& timer, int period);

    InputMode mode_ = InputMode::Menu;
    bool joyPresent_ = false;
    bool mousePresent_ = true;
    bool keyPollNow_ = false;
    void (*helpHook_)() = nullptr;
    // Repeat timers (DS:D814..D82E).
    s32 tJoyButton1_ = 0, tJoyButton2_ = 0, tJoyAxis_ = 0;
    s32 tMouseLeft_ = 0, tMouseRight_ = 0, tMouseAxis_ = 0, tKeyboard_ = 0;
    // The mouse is used as a virtual stick in the range 0..211 around 105.
    int mouseX_ = 105, mouseY_ = 105;
    int centreX_ = 105, centreY_ = 105;
};

InputLayer& input();

} // namespace st::engine
