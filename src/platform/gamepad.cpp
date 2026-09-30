#include "platform/gamepad.h"

#include <SDL3/SDL.h>

namespace st {

void Gamepad::init() {
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        if (count > 0) open(ids[0]);
        SDL_free(ids);
    }
}

void Gamepad::shutdown() { close(); }

void Gamepad::open(u32 id) {
    if (pad_) return;
    pad_ = SDL_OpenGamepad(id);
    if (!pad_) {
        logWarn("gamepad %u: %s", unsigned(id), SDL_GetError());
        return;
    }
    id_ = id;
    const char* n = SDL_GetGamepadName(pad_);
    name_ = n ? n : "gamepad";
    logInfo("gamepad connected: %s", name_.c_str());
}

void Gamepad::close() {
    if (!pad_) return;
    SDL_CloseGamepad(pad_);
    pad_ = nullptr;
    id_ = 0;
    name_.clear();
    buttons_.fill(false);
    axes_.fill(0);
}

void Gamepad::handleEvent(const SDL_Event& ev) {
    switch (ev.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        open(ev.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (pad_ && ev.gdevice.which == id_) {
            logInfo("gamepad removed: %s", name_.c_str());
            close();
            init();  // fall back to another connected pad
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        // Events of every pad are merged (synthetic test events carry id 0).
        if (ev.gbutton.button < buttons_.size()) buttons_[ev.gbutton.button] = ev.gbutton.down;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (ev.gaxis.axis < axes_.size()) axes_[ev.gaxis.axis] = ev.gaxis.value;
        break;
    default:
        break;
    }
}

} // namespace st
