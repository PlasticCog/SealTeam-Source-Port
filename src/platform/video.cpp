#include "platform/video.h"

#include "platform/window_icon.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

namespace st {

namespace {

constexpr u32 kPageStart[2] = {0x00000, 0x10000};  // linear pixel of pages 0 / 1

}  // namespace

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
    textureScale_ = 1;
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
    if (SDL_Surface* icon = SDL_CreateSurfaceFrom(kWindowIconSize, kWindowIconSize, SDL_PIXELFORMAT_RGBA32,
                                                  const_cast<std::uint8_t*>(kWindowIconRgba), kWindowIconSize * 4)) {
        SDL_SetWindowIcon(window_, icon);
        SDL_DestroySurface(icon);
    }
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

// ---------------------------------------------------------------- high-resolution layers

HiResLayer* Video::hiResLayer(int page) {
    HiResLayer& l = layers_[page & 1];
    return l.scale > 1 ? &l : nullptr;
}

HiResLayer& Video::activateHiResLayer(int page, int scale) {
    HiResLayer& l = layers_[page & 1];
    if (l.scale != scale) {
        l.scale = scale;
        l.w = kScreenW * scale;
        l.h = kScreenH * scale;
        l.pixels.assign(size_t(l.w) * size_t(l.h), 0);
        l.coverage.assign(size_t(kPageSize), 0);
    }
    dirty_ = true;
    return l;
}

void Video::copyHiResLayer(int src, int dst) {
    src &= 1;
    dst &= 1;
    if (src == dst) return;
    const HiResLayer& s = layers_[src];
    HiResLayer& d = layers_[dst];
    if (s.scale > 1) {
        d = s;
    } else if (d.scale > 1) {
        std::fill(d.coverage.begin(), d.coverage.end(), 0);
    }
}

void Video::dropHiResLayers() {
    for (HiResLayer& l : layers_) l = HiResLayer{};
    dirty_ = true;
}

int Video::composeScale() const {
    // The layer of the displayed page counts only if it covers something.
    for (int p = 0; p < 2; ++p) {
        const HiResLayer& l = layers_[p];
        if (l.scale > 1 && displayStart_ == kPageStart[p]) return l.scale;
    }
    return 1;
}

// Indexed pixels of the frame the CRT would show, at `scale` x resolution:
// pixels with layer coverage take the layer, the others the (upscaled) page.
void Video::compose(u8* out, int scale) const {
    const u32 start = scanoutStart();
    const HiResLayer* layer = nullptr;
    u32 pageStart = 0;
    for (int p = 0; p < 2; ++p)
        if (layers_[p].scale == scale && scale > 1 && displayStart_ == kPageStart[p]) {
            layer = &layers_[p];
            pageStart = kPageStart[p];
        }
    const int W = kScreenW * scale;
    for (int y = 0; y < kScreenH; ++y) {
        for (int x = 0; x < kScreenW; ++x) {
            const u32 addr = (start + u32(y * kScreenW + x)) % kVramSize;
            const u32 rel = addr - pageStart;  // position inside the displayed page
            const bool covered = layer && addr >= pageStart && rel < u32(kPageSize) && layer->coverage[rel];
            if (covered) {
                const int px = int(rel % kScreenW), py = int(rel / kScreenW);
                for (int j = 0; j < scale; ++j) {
                    const u8* src = &layer->pixels[size_t(py * scale + j) * size_t(layer->w) + size_t(px * scale)];
                    std::memcpy(out + size_t(y * scale + j) * size_t(W) + size_t(x * scale), src, size_t(scale));
                }
            } else {
                const u8 v = vram_[addr];
                for (int j = 0; j < scale; ++j)
                    std::memset(out + size_t(y * scale + j) * size_t(W) + size_t(x * scale), v, size_t(scale));
            }
        }
    }
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

    const int scale = composeScale();
    if (scale != textureScale_) {
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     kScreenW * scale, kScreenH * scale);
        if (!texture_) {
            logError("SDL_CreateTexture: %s", SDL_GetError());
            return;
        }
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
        textureScale_ = scale;
    }

    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch)) {
        if (scale == 1) {
            for (int y = 0; y < kScreenH; ++y) {
                auto* dst = reinterpret_cast<Uint32*>(static_cast<u8*>(pixels) + y * pitch);
                for (int x = 0; x < kScreenW; ++x) {
                    const u32 addr = (scanoutStart() + u32(y * kScreenW + x)) % kVramSize;
                    dst[x] = lut[vram_[addr]];
                }
            }
        } else {
            const int W = kScreenW * scale, H = kScreenH * scale;
            std::vector<u8> frame(size_t(W) * size_t(H));
            compose(frame.data(), scale);
            for (int y = 0; y < H; ++y) {
                auto* dst = reinterpret_cast<Uint32*>(static_cast<u8*>(pixels) + y * pitch);
                const u8* src = &frame[size_t(y) * size_t(W)];
                for (int x = 0; x < W; ++x) dst[x] = lut[src[x]];
            }
        }
        SDL_UnlockTexture(texture_);
    }
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

bool Video::saveScreenshot(const std::string& path) {
    const int scale = composeScale();
    const int W = kScreenW * scale, H = kScreenH * scale;
    SDL_Surface* s = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_INDEX8);
    if (!s) return false;
    SDL_Palette* palette = SDL_CreateSurfacePalette(s);
    SDL_Color colors[256];
    for (int i = 0; i < 256; ++i) {
        colors[i].r = u8((dac_[i * 3] << 2) | (dac_[i * 3] >> 4));
        colors[i].g = u8((dac_[i * 3 + 1] << 2) | (dac_[i * 3 + 1] >> 4));
        colors[i].b = u8((dac_[i * 3 + 2] << 2) | (dac_[i * 3 + 2] >> 4));
        colors[i].a = 255;
    }
    if (palette) SDL_SetPaletteColors(palette, colors, 0, 256);
    std::vector<u8> frame(size_t(W) * size_t(H));
    if (scale == 1) {
        for (int y = 0; y < kScreenH; ++y)
            for (int x = 0; x < kScreenW; ++x)
                frame[size_t(y * W + x)] = vram_[(scanoutStart() + u32(y * kScreenW + x)) % kVramSize];
    } else {
        compose(frame.data(), scale);
    }
    for (int y = 0; y < H; ++y)
        std::memcpy(static_cast<u8*>(s->pixels) + y * s->pitch, &frame[size_t(y) * size_t(W)], size_t(W));
    const bool ok = SDL_SaveBMP(s, path.c_str());
    SDL_DestroySurface(s);
    logInfo("screenshot %s (%dx%d): %s", path.c_str(), W, H, ok ? "saved" : SDL_GetError());
    return ok;
}

void Video::captureMouse(bool on) {
    if (!window_) return;
    SDL_SetWindowRelativeMouseMode(window_, on);
    mouseCaptured_ = on;
}

void Video::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_, fullscreen_);
    dirty_ = true;
}

} // namespace st
