#include "platform/video.h"

#include "platform/window_icon.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <thread>

namespace st {

namespace {

constexpr u32 kPageStart[2] = {0x00000, 0x10000};  // linear pixel of pages 0 / 1

}  // namespace

// ---------------------------------------------------------------- layer geometry

void HiResLayer::rectOf(int x0, int y0, int x1, int y1, bool extend, int& X0, int& Y0, int& X1, int& Y1) const {
    x0 = std::clamp(x0, 0, kScreenW);
    x1 = std::clamp(x1 + 1, 0, kScreenW);
    y0 = std::clamp(y0, 0, kScreenH);
    y1 = std::clamp(y1 + 1, 0, kScreenH);
    X0 = colStart[size_t(x0)];
    X1 = colStart[size_t(x1)] - 1;
    Y0 = rowStart[size_t(y0)];
    Y1 = rowStart[size_t(y1)] - 1;
    if (extend && fill) {
        if (x0 <= 0) X0 = 0;
        if (x1 >= kScreenW) X1 = w - 1;
        if (y0 <= 0) Y0 = 0;
        if (y1 >= kScreenH) Y1 = h - 1;
    }
}

int HiResLayer::pageX(int X) const {
    if (X < ox) return 0;
    if (X >= ox + pw) return kScreenW - 1;
    return colPage[size_t(X)];
}

int HiResLayer::pageY(int Y) const {
    if (Y < oy) return 0;
    if (Y >= oy + ph) return kScreenH - 1;
    return rowPage[size_t(Y)];
}

// ---------------------------------------------------------------- window

bool Video::init(const VideoConfig& cfg) {
    const int logicalH = cfg.aspectCorrect ? 240 : kScreenH;
    fullscreen_ = cfg.fullscreen;
    aspectCorrect_ = cfg.aspectCorrect;
    smooth_ = cfg.smooth;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (fullscreen_) flags |= SDL_WINDOW_FULLSCREEN;

    const int winW = cfg.width > 0 ? cfg.width : kScreenW * cfg.scale;
    const int winH = cfg.height > 0 ? cfg.height : logicalH * cfg.scale;
    if (!SDL_CreateWindowAndRenderer("SEAL Team", winW, winH, flags, &window_, &renderer_)) {
        logError("SDL_CreateWindowAndRenderer: %s", SDL_GetError());
        return false;
    }
    // No vsync: a blocking present would quantise the game's own 51 fps
    // frame pacing (5-tick page-flip wait) to whole monitor refreshes.
    SDL_SetRenderVSync(renderer_, 0);
    // The frame is placed by present() itself (pageArea): the 320x200 page
    // is stretched over the centred 4:3 (or 8:5) area, a native-resolution
    // layer covers the whole window pixel for pixel.
    SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    if (SDL_Surface* icon = SDL_CreateSurfaceFrom(kWindowIconSize, kWindowIconSize, SDL_PIXELFORMAT_RGBA32,
                                                  const_cast<std::uint8_t*>(kWindowIconRgba), kWindowIconSize * 4)) {
        SDL_SetWindowIcon(window_, icon);
        SDL_DestroySurface(icon);
    }
    SDL_HideCursor();  // the game draws its own cursor
    present(true);
    return texture_ != nullptr;
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

void Video::outputSize(int& w, int& h) const {
    w = kScreenW * 3;
    h = 240 * 3;
    if (renderer_) SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    if (overrideW_ > 0 && overrideH_ > 0) {
        w = overrideW_;
        h = overrideH_;
    }
    w = std::max(w, kScreenW);
    h = std::max(h, kScreenH);
}

// The largest 4:3 (aspect correct) or 8:5 (square pixels) rectangle centred
// in a w x h output.
PixelRect Video::pageAreaIn(int w, int h) const {
    const int an = aspectCorrect_ ? 4 : 8, ad = aspectCorrect_ ? 3 : 5;
    PixelRect r;
    if (w * ad <= h * an) {
        r.w = w;
        r.h = w * ad / an;
    } else {
        r.h = h;
        r.w = h * an / ad;
    }
    r.x = (w - r.w) / 2;
    r.y = (h - r.h) / 2;
    return r;
}

PixelRect Video::pageArea() const {
    // Always the real window: an override frame is scaled into it by present().
    int w = kScreenW * 3, h = 240 * 3;
    if (renderer_) SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    return pageAreaIn(std::max(w, 1), std::max(h, 1));
}

// ---------------------------------------------------------------- high-resolution layers

HiResLayer* Video::hiResLayer(int page) {
    HiResLayer& l = layers_[page & 1];
    return l.active ? &l : nullptr;
}

HiResLayer& Video::activateHiResLayer(int page, int scale, bool fill) {
    HiResLayer& l = layers_[page & 1];
    int w, h, ox, oy, pw, ph;
    if (scale > 0) {
        w = pw = kScreenW * scale;
        h = ph = kScreenH * scale;
        ox = oy = 0;
        fill = false;
    } else {
        outputSize(w, h);
        const PixelRect a = pageAreaIn(w, h);
        ox = a.x;
        oy = a.y;
        pw = a.w;
        ph = a.h;
    }
    if (!l.active || l.scale != scale || l.w != w || l.h != h || l.ox != ox || l.oy != oy || l.pw != pw ||
        l.ph != ph) {
        l.active = true;
        l.scale = scale;
        l.w = w;
        l.h = h;
        l.ox = ox;
        l.oy = oy;
        l.pw = pw;
        l.ph = ph;
        l.colStart.resize(size_t(kScreenW) + 1);
        l.rowStart.resize(size_t(kScreenH) + 1);
        for (int x = 0; x <= kScreenW; ++x) l.colStart[size_t(x)] = ox + x * pw / kScreenW;
        for (int y = 0; y <= kScreenH; ++y) l.rowStart[size_t(y)] = oy + y * ph / kScreenH;
        l.colPage.assign(size_t(w), s16(-1));
        l.rowPage.assign(size_t(h), s16(-1));
        for (int x = 0; x < kScreenW; ++x)
            for (int X = l.colStart[size_t(x)]; X < l.colStart[size_t(x) + 1]; ++X) l.colPage[size_t(X)] = s16(x);
        for (int y = 0; y < kScreenH; ++y)
            for (int Y = l.rowStart[size_t(y)]; Y < l.rowStart[size_t(y) + 1]; ++Y) l.rowPage[size_t(Y)] = s16(y);
        l.pixels.assign(size_t(w) * size_t(h), 0);
        l.coverage.assign(size_t(kPageSize), 0);
        l.extRows.assign(size_t(kScreenH), 0);
        l.extX0 = l.extY0 = 0;
        l.extX1 = l.extY1 = -1;
    }
    l.fill = fill;
    dirty_ = true;
    return l;
}

void Video::copyHiResLayer(int src, int dst) {
    src &= 1;
    dst &= 1;
    if (src == dst) return;
    const HiResLayer& s = layers_[src];
    HiResLayer& d = layers_[dst];
    if (s.active) {
        d = s;
    } else if (d.active) {
        std::fill(d.coverage.begin(), d.coverage.end(), 0);
        std::fill(d.extRows.begin(), d.extRows.end(), 0);
    }
}

void Video::dropHiResLayers() {
    for (HiResLayer& l : layers_) l = HiResLayer{};
    dirty_ = true;
}

const HiResLayer* Video::displayedLayer() const {
    for (int p = 0; p < 2; ++p)
        if (layers_[p].active && displayStart_ == kPageStart[p]) return &layers_[p];
    return nullptr;
}

// One row of the frame the CRT would show, at layer resolution: page pixels
// with layer coverage take the layer, the others the (upscaled) page; outside
// the page area the extension of the 3D view or black.
void Video::composeRow(const HiResLayer& l, int Y, u8* out) const {
    const u32 start = scanoutStart();
    const u32 pageStart = (&l == &layers_[1]) ? kPageStart[1] : kPageStart[0];
    const int W = l.w;
    const bool shifted = displayOffset_ != 0;
    const int py = l.pageY(Y);
    const bool insideRows = Y >= l.oy && Y < l.oy + l.ph;
    const bool extRow = l.extRows[size_t(py)] && Y >= l.extY0 && Y <= l.extY1;
    auto extRun = [&](int X0, int X1) {  // columns outside the page area
        if (X1 < X0) return;
        const int a = std::max(X0, l.extX0), b = std::min(X1, l.extX1);
        if (!extRow || b < a) {
            std::memset(out + X0, 0, size_t(X1 - X0 + 1));
            return;
        }
        if (a > X0) std::memset(out + X0, 0, size_t(a - X0));
        std::memcpy(out + a, &l.pixels[size_t(Y) * size_t(W) + size_t(a)], size_t(b - a + 1));
        if (b < X1) std::memset(out + b + 1, 0, size_t(X1 - b));
    };
    if (!insideRows) {
        extRun(0, W - 1);
        return;
    }
    extRun(0, l.ox - 1);
    extRun(l.ox + l.pw, W - 1);
    const size_t rowBase = size_t(Y) * size_t(W);
    for (int px = 0; px < kScreenW; ++px) {
        const int c0 = l.colStart[size_t(px)], c1 = l.colStart[size_t(px) + 1];
        if (c1 <= c0) continue;
        const u32 addr = (start + u32(py * kScreenW + px)) % kVramSize;
        const u32 rel = addr - pageStart;  // position inside the displayed page
        const bool covered = addr >= pageStart && rel < u32(kPageSize) && l.coverage[rel];
        if (!covered) {
            std::memset(out + c0, vram_[addr], size_t(c1 - c0));
        } else if (!shifted) {
            std::memcpy(out + c0, &l.pixels[rowBase + size_t(c0)], size_t(c1 - c0));
        } else {
            // Screen shake: the layer block of the shifted page pixel, with
            // the same offset inside the block (clamped: blocks may differ
            // by a pixel at non-integer scales).
            const int sx = int(rel % kScreenW), sy = int(rel / kScreenW);
            const int bx0 = l.colStart[size_t(sx)], bx1 = l.colStart[size_t(sx) + 1] - 1;
            const int by0 = l.rowStart[size_t(sy)], by1 = l.rowStart[size_t(sy) + 1] - 1;
            const int SY = std::min(by0 + (Y - l.rowStart[size_t(py)]), by1);
            for (int X = c0; X < c1; ++X) {
                const int SX = std::min(bx0 + (X - c0), bx1);
                out[X] = l.pixels[size_t(SY) * size_t(W) + size_t(SX)];
            }
        }
    }
}

void Video::composeIndexed(const HiResLayer& l, u8* out) const {
    for (int Y = 0; Y < l.h; ++Y) composeRow(l, Y, out + size_t(Y) * size_t(l.w));
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

    const HiResLayer* layer = displayedLayer();
    const int W = layer ? layer->w : kScreenW, H = layer ? layer->h : kScreenH;
    if (W != texW_ || H != texH_ || !texture_) {
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
        if (!texture_) {
            logError("SDL_CreateTexture: %s", SDL_GetError());
            texW_ = texH_ = 0;
            return;
        }
        texW_ = W;
        texH_ = H;
    }
    // A layer is shown pixel for pixel (nearest); the plain page may be
    // filtered if the player asked for smooth scaling.
    SDL_SetTextureScaleMode(texture_, (!layer && smooth_) ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);

    const Uint64 t0 = SDL_GetPerformanceCounter();
    Uint64 tl = t0, t1 = t0;
    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch)) {
        tl = SDL_GetPerformanceCounter();
        if (!layer) {
            for (int y = 0; y < kScreenH; ++y) {
                auto* dst = reinterpret_cast<Uint32*>(static_cast<u8*>(pixels) + y * pitch);
                for (int x = 0; x < kScreenW; ++x) {
                    const u32 addr = (scanoutStart() + u32(y * kScreenW + x)) % kVramSize;
                    dst[x] = lut[vram_[addr]];
                }
            }
        } else {
            // Compose and convert row by row; large frames on a few threads
            // (a 4K frame is 8.3 M pixels).
            auto rows = [&](int y0, int y1) {
                std::vector<u8> line(static_cast<size_t>(W), u8(0));
                for (int y = y0; y < y1; ++y) {
                    composeRow(*layer, y, line.data());
                    auto* dst = reinterpret_cast<Uint32*>(static_cast<u8*>(pixels) + y * pitch);
                    for (int x = 0; x < W; ++x) dst[x] = lut[line[size_t(x)]];
                }
            };
            const int nThreads = std::clamp(int(size_t(W) * size_t(H) / 1000000), 1, 8);
            if (nThreads <= 1) {
                rows(0, H);
            } else {
                std::vector<std::thread> pool;
                for (int t = 0; t < nThreads; ++t)
                    pool.emplace_back(rows, H * t / nThreads, H * (t + 1) / nThreads);
                for (std::thread& t : pool) t.join();
            }
        }
        t1 = SDL_GetPerformanceCounter();
        SDL_UnlockTexture(texture_);
    }
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    // Where the frame goes: a native layer covers the window (its page area
    // is the window's), everything else is stretched over the page area.
    int outW, outH;
    SDL_GetCurrentRenderOutputSize(renderer_, &outW, &outH);
    SDL_FRect dst;
    if (layer && layer->scale == 0 && overrideW_ == 0) {
        dst = SDL_FRect{0.0f, 0.0f, float(outW), float(outH)};
    } else if (layer && layer->scale == 0) {
        // Override size: scale the whole oversized frame into the window
        // keeping its aspect.
        const PixelRect a = pageAreaIn(std::max(outW, 1), std::max(outH, 1));
        const float sx = float(a.w) / float(layer->pw), sy = float(a.h) / float(layer->ph);
        dst = SDL_FRect{float(a.x) - float(layer->ox) * sx, float(a.y) - float(layer->oy) * sy,
                        float(layer->w) * sx, float(layer->h) * sy};
    } else {
        const PixelRect a = pageAreaIn(std::max(outW, 1), std::max(outH, 1));
        dst = SDL_FRect{float(a.x), float(a.y), float(a.w), float(a.h)};
    }
    SDL_RenderTexture(renderer_, texture_, nullptr, &dst);
    SDL_RenderPresent(renderer_);
    const Uint64 t2 = SDL_GetPerformanceCounter();
    const double f = 1.0 / double(SDL_GetPerformanceFrequency());
    const double l = double(tl - t0) * f, c = double(t1 - tl) * f, u = double(t2 - t1) * f;
    ++stats_.frames;
    stats_.lock += l;
    stats_.lockMax = std::max(stats_.lockMax, l);
    stats_.compose += c;
    stats_.composeMax = std::max(stats_.composeMax, c);
    stats_.upload += u;
    stats_.uploadMax = std::max(stats_.uploadMax, u);
}

bool Video::saveScreenshot(const std::string& path) {
    const HiResLayer* layer = displayedLayer();
    const int W = layer ? layer->w : kScreenW, H = layer ? layer->h : kScreenH;
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
    if (!layer) {
        for (int y = 0; y < kScreenH; ++y)
            for (int x = 0; x < kScreenW; ++x)
                frame[size_t(y * W + x)] = vram_[(scanoutStart() + u32(y * kScreenW + x)) % kVramSize];
    } else {
        composeIndexed(*layer, frame.data());
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
    // Relative mode can be refused while the window is not yet shown or
    // focused; record what actually happened so the pump can retry on the
    // next focus gain or click instead of believing the mouse is captured.
    const bool ok = SDL_SetWindowRelativeMouseMode(window_, on);
    mouseCaptured_ = on && ok;
    if (mouseCaptured_) SDL_HideCursor();
}

void Video::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_, fullscreen_);
    dirty_ = true;
}

void Video::setWindowSize(int w, int h) {
    if (!window_) return;
    SDL_SetWindowSize(window_, w, h);
    dirty_ = true;
}

} // namespace st
