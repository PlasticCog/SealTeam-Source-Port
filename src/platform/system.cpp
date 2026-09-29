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
    SDL_Surface* s = SDL_CreateSurface(kScreenW, kScreenH, SDL_PIXELFORMAT_INDEX8);
    if (!s) return false;
    SDL_Palette* palette = SDL_CreateSurfacePalette(s);
    SDL_Color colors[256];
    const u8* dac = video_.palette();
    for (int i = 0; i < 256; ++i) {
        colors[i].r = u8((dac[i * 3] << 2) | (dac[i * 3] >> 4));
        colors[i].g = u8((dac[i * 3 + 1] << 2) | (dac[i * 3 + 1] >> 4));
        colors[i].b = u8((dac[i * 3 + 2] << 2) | (dac[i * 3 + 2] >> 4));
        colors[i].a = 255;
    }
    if (palette) SDL_SetPaletteColors(palette, colors, 0, 256);
    const u8* vram = video_.vram();
    for (int y = 0; y < kScreenH; ++y) {
        u8* dst = static_cast<u8*>(s->pixels) + y * s->pitch;
        for (int x = 0; x < kScreenW; ++x) dst[x] = vram[(video_.scanoutStart() + u32(y * kScreenW + x)) % kVramSize];
    }
    const bool ok = SDL_SaveBMP(s, path.c_str());
    SDL_DestroySurface(s);
    logInfo("screenshot %s: %s", path.c_str(), ok ? "saved" : SDL_GetError());
    return ok;
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
