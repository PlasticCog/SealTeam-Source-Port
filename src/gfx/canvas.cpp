#include "gfx/canvas.h"

#include "data/ealib.h"
#include "gfx/font.h"
#include "platform/video.h"

#include <algorithm>

namespace st {

Canvas& canvas() {
    static Canvas instance;
    return instance;
}

bool loadMask(std::string_view name, int w, int h, Mask& out) {
    std::vector<u8> data;
    if (!resources().read(name, data)) return false;
    const size_t need = size_t(((w + 7) >> 3) * h);
    if (data.size() < need) return false;
    out.w = w;
    out.h = h;
    out.bits.assign(data.begin(), data.begin() + long(need));
    return true;
}

void Canvas::setClip(int x, int y, int w, int h) {
    cx0_ = x;
    cy0_ = y;
    cx1_ = x + w - 1;
    cy1_ = y + h - 1;
}

void Canvas::putPixel(int x, int y, u8 c) {
    if (x < cx0_ || x > cx1_ || y < cy0_ || y > cy1_) return;
    page_[y * kScreenW + x] = c;
}

void Canvas::fillRect(int x, int y, int w, int h, u8 c) {
    const int x0 = std::max(x, cx0_), x1 = std::min(x + w - 1, cx1_);
    const int y0 = std::max(y, cy0_), y1 = std::min(y + h - 1, cy1_);
    for (int yy = y0; yy <= y1; ++yy)
        for (int xx = x0; xx <= x1; ++xx) page_[yy * kScreenW + xx] = c;
}

void Canvas::blit(const Image& img, int x, int y) {
    const int x0 = std::max(x, cx0_), x1 = std::min(x + img.w - 1, cx1_);
    const int y0 = std::max(y, cy0_), y1 = std::min(y + img.h - 1, cy1_);
    for (int yy = y0; yy <= y1; ++yy) {
        const u8* src = img.row(yy - y);
        for (int xx = x0; xx <= x1; ++xx) page_[yy * kScreenW + xx] = src[xx - x];
    }
}

void Canvas::blitMasked(const Image& img, const Mask& mask, int x, int y) {
    const int x0 = std::max(x, cx0_), x1 = std::min(x + img.w - 1, cx1_);
    const int y0 = std::max(y, cy0_), y1 = std::min(y + img.h - 1, cy1_);
    for (int yy = y0; yy <= y1; ++yy) {
        const u8* src = img.row(yy - y);
        for (int xx = x0; xx <= x1; ++xx)
            if (mask.opaque(xx - x, yy - y)) page_[yy * kScreenW + xx] = src[xx - x];
    }
}

int Canvas::drawChar(u8 c, int x, int y) {
    if (!font_) return 0;
    const int w = font_->glyphWidth(c);
    const int h = font_->height();
    if (w <= 0) return 0;
    if (x < cx0_ || x + w - 1 > cx1_ || y < cy0_ || y + h - 1 > cy1_) return w;
    for (int r = 0; r < h; ++r)
        for (int i = 0; i < w; ++i)
            if (font_->pixel(c, i, r)) page_[(y + r) * kScreenW + x + i] = fg_;
    return w;
}

int Canvas::drawString(std::string_view s, int x, int y) {
    for (char ch : s) x += drawChar(u8(ch), x, y);
    return x;
}

void Canvas::draw4x6String(const Font& f, std::string_view s, int x, int y) {
    // The original requires x to be a multiple of 4 and has no clipping here;
    // clip anyway so a bad call cannot write outside the page.
    if (opaque_) fillRect(x, y, 4 * int(s.size()), 6, bg_);
    for (char ch : s) {
        for (int r = 0; r < 6; ++r)
            for (int i = 0; i < 4; ++i)
                if (f.pixel(u8(ch), i, r)) putPixel(x + i, y + r, fg_);
        x += 4;
    }
}

} // namespace st
