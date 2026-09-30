#include "platform/system.h"

#include <SDL3/SDL.h>

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
    logicalH_ = vcfg.aspectCorrect ? 240 : kScreenH;
    timer_.init();
    return video_.init(vcfg);
}

void System::shutdown() {
    video_.shutdown();
    SDL_Quit();
}

void System::pump() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) throw QuitRequested{};
        if (ev.type >= SDL_EVENT_WINDOW_FIRST && ev.type <= SDL_EVENT_WINDOW_LAST) {
            video_.markDirty();
            continue;
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
            video_.toggleFullscreen();
            continue;
        }
        // Mouse positions arrive in window pixels; map them to the logical
        // 320 x logicalH area (letterboxing and scaling removed).
        SDL_ConvertEventToRenderCoordinates(video_.renderer(), &ev);
        input_.handleEvent(ev, kScreenW, logicalH_);
    }
    runTicks();
    video_.present();
    if (!shotPath_.empty() && timer_.seconds() >= shotAt_) {
        saveScreenshot(shotPath_);
        shotPath_.clear();
        throw QuitRequested{};
    }
}

void System::scheduleScreenshot(const std::string& path, double afterSeconds) {
    shotPath_ = path;
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
