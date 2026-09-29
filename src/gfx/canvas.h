// Drawing state and primitives of the original graphics library: a target
// page, an inclusive clip rectangle, text colours and the current font.
// Mirrors the globals the original keeps in DGROUP (clip DS:F23C..F242, text
// colours DS:5074/5075, opaque flag DS:F424, font DS:EF00).
#pragma once

#include "core/common.h"
#include "gfx/image.h"

#include <string_view>

namespace st {

class Font;

// 1 bit per pixel shape mask (.msk): rows of ceil(w/8) bytes, MSB = leftmost,
// set = opaque. Dimensions come from the matching picture.
struct Mask {
    int w = 0, h = 0;
    std::vector<u8> bits;

    bool opaque(int x, int y) const {
        const int rowBytes = (w + 7) >> 3;
        return (bits[size_t(y * rowBytes + (x >> 3))] >> (7 - (x & 7))) & 1;
    }
};

bool loadMask(std::string_view name, int w, int h, Mask& out);

class Canvas {
public:
    // Draw into a 320-pixel-wide page (a pointer into VGA memory).
    void setTarget(u8* page) { page_ = page; }
    u8* target() const { return page_; }

    // Clip rectangle given as origin + size, stored inclusive (2255:0FD0).
    void setClip(int x, int y, int w, int h);
    void resetClip() { setClip(0, 0, 320, 200); }
    int clipX0() const { return cx0_; }
    int clipY0() const { return cy0_; }
    int clipX1() const { return cx1_; }
    int clipY1() const { return cy1_; }

    void putPixel(int x, int y, u8 c);
    void fillRect(int x, int y, int w, int h, u8 c);
    // Opaque copy, clipped.
    void blit(const Image& img, int x, int y);
    // Copy only pixels whose mask bit is set, clipped.
    void blitMasked(const Image& img, const Mask& mask, int x, int y);

    // Text.
    void setFont(const Font* f) { font_ = f; }
    const Font* font() const { return font_; }
    void setTextColors(u8 fg, u8 bg) { fg_ = fg; bg_ = bg; }
    u8 textFg() const { return fg_; }
    void setTextOpaque(bool on) { opaque_ = on; }
    // Proportional glyph (4dec): skipped entirely unless the whole glyph box
    // lies inside the clip rectangle. Returns the glyph width.
    int drawChar(u8 c, int x, int y);
    // Draws glyph by glyph, advancing by each glyph width; returns the end x.
    int drawString(std::string_view s, int x, int y);
    // 4x6 font text (4db9/4dcf): fixed 4-pixel cells, optional background fill.
    void draw4x6String(const Font& f4x6, std::string_view s, int x, int y);

private:
    u8* page_ = nullptr;
    int cx0_ = 0, cy0_ = 0, cx1_ = 319, cy1_ = 199;
    const Font* font_ = nullptr;
    u8 fg_ = 15, bg_ = 0;
    bool opaque_ = false;
};

Canvas& canvas();

} // namespace st
