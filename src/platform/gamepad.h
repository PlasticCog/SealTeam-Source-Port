// Game controller device state through the SDL3 gamepad API: hot-plugging
// (SDL_EVENT_GAMEPAD_ADDED / REMOVED), buttons, sticks and triggers of the
// first connected pad. The engine's controller mapping layer
// (engine/controller.h) turns this state into the key codes and motion the
// game understands; this class only mirrors the hardware.
#pragma once

#include "core/common.h"

#include <array>
#include <string>

union SDL_Event;
struct SDL_Gamepad;

namespace st {

class Gamepad {
public:
    // Opens a pad that is already connected (SDL reports later ones by event).
    void init();
    void shutdown();
    void handleEvent(const SDL_Event& ev);

    // A pad is connected (or a synthetic pad was enabled for tests).
    bool present() const { return pad_ != nullptr || virtual_; }
    // Tests inject events with SDL_PushEvent; this makes the layer treat the
    // events as coming from a connected pad.
    void setVirtual(bool on) { virtual_ = on; }
    const std::string& name() const { return name_; }

    // SDL_GamepadButton index -> held.
    bool button(int b) const { return b >= 0 && b < int(buttons_.size()) && buttons_[size_t(b)]; }
    // SDL_GamepadAxis index -> raw value (-32768..32767; triggers 0..32767).
    int axis(int a) const { return a >= 0 && a < int(axes_.size()) ? axes_[size_t(a)] : 0; }

private:
    void open(u32 id);
    void close();

    SDL_Gamepad* pad_ = nullptr;
    u32 id_ = 0;
    bool virtual_ = false;
    std::string name_;
    std::array<bool, 32> buttons_{};
    std::array<int, 8> axes_{};
};

} // namespace st
