#include "platform/video.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace st {

bool Video::init(const VideoConfig& cfg) {
    const int logicalH = cfg.aspectCorrect ? 240 : kScreenH;
    fullscreen_ = cfg.fullscreen;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (fullscreen_) flags |= SDL_WINDOW_FULLSCREEN;

    if (!SDL_CreateWindowAndRenderer("SEAL Team", kScreenW * cfg.scale, logicalH * cfg.scale, flags,
                                     &window_, &renderer_)) {
        logError("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    // The 320x200 frame is stretched over a 320x240 logical area when
    // aspect correction is on, reproducing the non-square pixels of a CRT.
    SDL_SetRenderLogicalPresentation(renderer_, kScreenW, logicalH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                 kScreenW, kScreenH);
    if (!texture_) {
        logError("SDL_CreateTexture: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
    SDL_HideCursor();  // the game draws its own cursor
    present(true);
    return true;
}

void Video::shutdown() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    texture_ = nullptr;
    renderer_ = nullptr;
    window_ = nullptr;
}

void Video::setPalette(const u8* rgb6, int first, int count) {
    first = std::clamp(first, 0, 256);
    count = std::clamp(count, 0, 256 - first);
    for (int i = 0; i < count * 3; ++i) dac_[first * 3 + i] = rgb6[i] & 0x3f;
    dirty_ = true;
}

void Video::getPalette(u8* rgb6, int first, int count) const {
    first = std::clamp(first, 0, 256);
    count = std::clamp(count, 0, 256 - first);
    std::copy_n(dac_.begin() + first * 3, count * 3, rgb6);
}

void Video::present(bool force) {
    if (!renderer_ || (!dirty_ && !force)) return;
    dirty_ = false;

    // 6-bit DAC value -> 8-bit, the same expansion a VGA DAC produces.
    Uint32 lut[256];
    for (int i = 0; i < 256; ++i) {
        auto c = [&](int k) { const int v = dac_[i * 3 + k]; return Uint32((v << 2) | (v >> 4)); };
        lut[i] = 0xff000000u | (c(0) << 16) | (c(1) << 8) | c(2);
    }

    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch)) {
        for (int y = 0; y < kScreenH; ++y) {
            auto* dst = reinterpret_cast<Uint32*>(static_cast<u8*>(pixels) + y * pitch);
            for (int x = 0; x < kScreenW; ++x) {
                const u32 addr = (displayStart_ + u32(y * kScreenW + x)) % kVramSize;
                dst[x] = lut[vram_[addr]];
            }
        }
        SDL_UnlockTexture(texture_);
    }
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

void Video::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_, fullscreen_);
    dirty_ = true;
}

} // namespace st
