#include "platform/system.h"

#include <SDL.h>

namespace st {

System& sys() {
    static System instance;
    return instance;
}

bool System::init(const VideoConfig& vcfg) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
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
        switch (ev.type) {
        case SDL_QUIT:
            throw QuitRequested{};
        case SDL_KEYDOWN:
            if (ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT)) {
                video_.toggleFullscreen();
                break;
            }
            input_.handleEvent(ev, 320, logicalH_);
            break;
        case SDL_WINDOWEVENT:
            video_.markDirty();
            break;
        default:
            input_.handleEvent(ev, 320, logicalH_);
            break;
        }
    }
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
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, kScreenW, kScreenH, 8, SDL_PIXELFORMAT_INDEX8);
    if (!s) return false;
    SDL_Color colors[256];
    const u8* dac = video_.palette();
    for (int i = 0; i < 256; ++i) {
        colors[i].r = u8((dac[i * 3] << 2) | (dac[i * 3] >> 4));
        colors[i].g = u8((dac[i * 3 + 1] << 2) | (dac[i * 3 + 1] >> 4));
        colors[i].b = u8((dac[i * 3 + 2] << 2) | (dac[i * 3 + 2] >> 4));
        colors[i].a = 255;
    }
    SDL_SetPaletteColors(s->format->palette, colors, 0, 256);
    const u8* vram = video_.vram();
    for (int y = 0; y < kScreenH; ++y) {
        u8* dst = static_cast<u8*>(s->pixels) + y * s->pitch;
        for (int x = 0; x < kScreenW; ++x) dst[x] = vram[(video_.displayStart() + u32(y * kScreenW + x)) % kVramSize];
    }
    const bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
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

void System::delayMs(u32 ms) {
    const double end = timer_.seconds() + ms / 1000.0;
    while (timer_.seconds() < end) {
        pump();
        SDL_Delay(1);
    }
}

} // namespace st
