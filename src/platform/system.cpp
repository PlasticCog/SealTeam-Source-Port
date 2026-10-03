#include "platform/system.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

namespace st {

System& sys() {
    static System instance;
    return instance;
}

bool System::init(const VideoConfig& vcfg) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        logError("SDL_Init: %s", SDL_GetError());
        return false;
    }
    // Game controllers are optional: without the subsystem the keyboard and
    // mouse still work.
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) logWarn("SDL gamepad: %s", SDL_GetError());
    logicalH_ = vcfg.aspectCorrect ? 240 : kScreenH;
    timer_.init();
    if (!video_.init(vcfg)) return false;
    gamepad_.init();
    if (!scripted_) video_.captureMouse(true);
    return true;
}

void System::shutdown() {
    gamepad_.shutdown();
    video_.shutdown();
    SDL_Quit();
}

void System::pump() {
    SDL_Event ev;
    bool windowEvent = false;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) throw QuitRequested{};
        if (ev.type >= SDL_EVENT_WINDOW_FIRST && ev.type <= SDL_EVENT_WINDOW_LAST) {
            // Let go of the mouse when the player switches away; take it back
            // with the next click inside the window.
            if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST) video_.captureMouse(false);
            if (ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED && !scripted_) video_.captureMouse(true);
            video_.markDirty();
            windowEvent = true;
            continue;
        }
        if (ev.type >= SDL_EVENT_GAMEPAD_AXIS_MOTION && ev.type <= SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED) {
            gamepad_.handleEvent(ev);
            continue;
        }
        if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !video_.mouseCaptured() && !scripted_) {
            video_.captureMouse(true);
            continue;  // the re-capturing click is not a game click
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
            video_.toggleFullscreen();
            continue;
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_Q && (ev.key.mod & SDL_KMOD_CTRL)) {
            // Ctrl+Q: quit to the desktop from anywhere, like closing the window.
            throw QuitRequested{};
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_F12) {
            // Screenshot for bug reports: sealteam-shot-N.bmp next to the program.
            static int n = 0;
            char name[64];
            std::snprintf(name, sizeof name, "sealteam-shot-%d.bmp", ++n);
            saveScreenshot(shotDir_ + name);
            continue;
        }
        // Mouse positions arrive in window pixels; map them to the logical
        // 320 x logicalH area (letterboxing and scaling removed).
        // Keep the raw motion deltas: the position is converted to render
        // coordinates, the deltas feed the game's virtual stick 1:1.
        const bool motion = ev.type == SDL_EVENT_MOUSE_MOTION;
        const float xrel = motion ? ev.motion.xrel : 0.0f, yrel = motion ? ev.motion.yrel : 0.0f;
        SDL_ConvertEventToRenderCoordinates(video_.renderer(), &ev);
        if (motion) {
            // Output pixels -> 320x200 page coordinates through the page area
            // (the 4:3 rectangle; a wide 3D view outside it maps to the edge).
            const PixelRect a = video_.pageArea();
            ev.motion.x = (ev.motion.x - float(a.x)) * float(kScreenW) / float(std::max(a.w, 1));
            ev.motion.y = (ev.motion.y - float(a.y)) * float(kScreenH) / float(std::max(a.h, 1));
            ev.motion.xrel = xrel;
            ev.motion.yrel = yrel;
        }
        input_.handleEvent(ev, kScreenW, kScreenH);
    }
    runTicks();
    // With explicit present (the mission loop) only the page flip shows a
    // frame; a window event (restore, resize, Alt-Tab back) still re-shows the
    // displayed page so a paused screen is never left blank.
    if (!video_.explicitPresent()) video_.present();
    else if (windowEvent) video_.present(true);
    if (!shotPath_.empty() && timer_.seconds() >= shotAt_) {
        saveScreenshot(shotPath_);
        shotPath_.clear();
        throw QuitRequested{};
    }
}

void System::scheduleScreenshot(const std::string& path, double afterSeconds) {
    shotPath_ = path;
    scripted_ = true;
    video_.captureMouse(false);
    shotAt_ = afterSeconds;
}

bool System::saveScreenshot(const std::string& path) {
    // Video composes the frame as shown (including a high-resolution layer).
    return video_.saveScreenshot(path);
}

void System::waitRetrace() {
    const u32 target = lastRetrace_ + 1;
    for (;;) {
        pump();
        const u32 now = timer_.retraceCount();
        if (now >= target) {
            lastRetrace_ = now;
            return;
        }
        SDL_Delay(1);
    }
}

void System::idle() {
    pump();
    SDL_Delay(1);
}

void System::installTickService(u32 divisor, std::function<void()> handler) {
    tickHandler_ = std::move(handler);
    tickHz_ = kPitHz / double(divisor ? divisor : 65536);
    tickEpoch_ = timer_.seconds();
    ticksDelivered_ = 0;
}

void System::runTicks() {
    if (!tickHandler_) return;
    const u64 due = u64((timer_.seconds() - tickEpoch_) * tickHz_);
    // After a long stall (debugger, window drag) don't replay seconds of
    // interrupts in one burst; drop anything beyond a quarter second.
    const u64 maxBurst = u64(tickHz_ / 4) + 1;
    if (due > ticksDelivered_ + maxBurst) ticksDelivered_ = due - maxBurst;
    while (ticksDelivered_ < due) {
        ++ticksDelivered_;
        tickHandler_();
    }
}

void System::delayMs(u32 ms) {
    const double end = timer_.seconds() + ms / 1000.0;
    while (timer_.seconds() < end) {
        pump();
        SDL_Delay(1);
    }
}

} // namespace st
