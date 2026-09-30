// Port of the author's VGA graphics driver, mode 6 (Mode X 320x200, two
// pages) — segment 2255, documented in docs/re/seg_2255.md section 3 and
// docs/re/seg_libs.md sections 6-8 (text).
//
// Memory model: VRAM is the emulated 256 KB in platform/video (linear pixel =
// planar byte * 4 + plane). Page 0 is planar A000:0000 (linear 0), page 1 is
// A400:0000 (linear 0x10000); within a page pixel (x, y) is linear 320*y + x.
// Off-screen bitmaps live in ordinary host memory with the same row layout.
//
// Colours are 16-bit "colour words": low byte palette index, high byte a
// dither pattern (0xFF = solid) used as a 4x2 write mask.
#pragma once

#include "core/common.h"
#include "gfx/image.h"
#include "platform/video.h"

#include <string_view>
#include <vector>

namespace st {

class Font;

// Bitmap / surface descriptor (original: 10 bytes, flags/width/height/bpr/seg).
struct Bitmap {
    u8 flags = 0;          // bit 0: screen page layout
    int w = 0, h = 0;
    int bpr = 0;           // planar bytes per row; pixel stride = 4 * bpr
    u8* data = nullptr;    // pixel (x, y) at data[y * 4 * bpr + x]
    std::vector<u8> storage;

    int stride() const { return bpr * 4; }
    u8* row(int y) { return data + y * stride(); }
};

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

class Gfx {
public:
    static constexpr u16 kSolid = 0xff00;  // colour word high byte for "no pattern"

    // gfx_init(6) state: draw page 0, display page 0, full clip, solid colour.
    void init();
    // gfx_pages_init (060D): draw on page 1 while page 0 is shown.
    void initPages();

    // --- pages (0649, 0840, AD4B, 0735)
    void setDrawPage(int page);
    void setDisplayPage(int page);
    int drawPage() const { return drawPage_; }
    int displayPage() const { return displayPage_; }
    // Swap roles of the pages; with `wait` also run the 5-tick frame limiter.
    void flip(bool flipPages, bool wait);
    void copyPage(int src, int dst);
    void clear(u8 color);
    Bitmap& screen() { return pages_[drawPage_]; }
    Bitmap& page(int n) { return pages_[n]; }

    // --- render target (Enhanced 3D): all primitives draw into `t` instead of
    // the draw page until reset with nullptr. Coordinates and the clip box are
    // then in the target's pixels.
    void setTarget(Bitmap* t) { target_ = t; }
    Bitmap* target() const { return target_; }
    // Re-read the draw page's high-resolution layer coverage (platform/video):
    // every 2D write to a page clears the coverage of the pixels it touches so
    // it shows on top of the high-resolution 3D view.
    void refreshCoverage();

    // --- clip box (0FD0), inclusive; (cx, cy) is also the 3D projection centre
    void setClip(int x, int y, int w, int h);
    void clipFull() { setClip(0, 0, 320, 200); }
    int clipX0() const { return x0_; }
    int clipY0() const { return y0_; }
    int clipX1() const { return x1_; }
    int clipY1() const { return y1_; }
    int clipCx() const { return cx_; }
    // Move only the bottom edge (the soldier sprite code writes DS:F242 directly).
    void setClipBottom(int y1) { y1_ = y1; }
    int clipCy() const { return cy_; }

    // --- colour and filled primitives
    void beginColor(u16 colorWord);                     // 18C5
    void hspan(int x0, int x1, int y);                  // 205A, current colour
    void hline(int x0, int x1, int y, u16 c);
    void vline(int x, int y0, int y1, u16 c);
    void rect(int x, int y, int w, int h, u16 c);       // 0786 outline
    void fillRect(int x, int y, int w, int h, u16 c);   // 25CE
    void fillRows(int y, int n, u8 c);                  // 0AE6, solid only
    void fillSplitRows(const s16* xs, int y0, int y1, u8 left, u8 right);  // 0B74
    void fillCircle(int x, int y, int r, u16 c);        // 099A
    void fillPolygon(const s16* pts, int n, u16 c);     // 10FC, pts = x,y pairs
    void pixel(int x, int y, u16 c);                    // 1F9E
    void line(int x0, int y0, int x1, int y1, u16 c);   // 1962

    // --- sprites and bitmaps
    // Scaled RLE sprite (.RLE layout) with top-left (x, y) scaled to w x h (B0D6).
    void spriteScaled(int x, int y, int w, int h, const u8* spr);
    void spriteCentered(const u8* spr, int cx, int cy, int w, int h);  // 28FA
    // Colour remap for sprites (DS:D79A flag, table at far DS:00E6).
    void setSpriteRemap(const u8* table) { remap_ = table; }
    bool createBitmap(Bitmap& b, int w, int h);          // 2867
    void blit(const Bitmap& src, int sx, int sy, Bitmap& dst, int dx, int dy, int w, int h);  // A7E9
    void blitMasked(const Bitmap& src, int sx, int sy, Bitmap& dst, int dx, int dy, int w, int h,
                    const u8* mask);                    // AA26
    // Convenience for decoded pictures: opaque or .msk-masked copy, clipped.
    void drawImage(const Image& img, int x, int y);
    void drawImageMasked(const Image& img, const Mask& mask, int x, int y);

    // --- text (4dec, 4db9/4dcf)
    void setFont(const Font* f) { font_ = f; }
    const Font* font() const { return font_; }
    void setTextColors(u8 fg, u8 bg) { textFg_ = fg; textBg_ = bg; }
    u8 textFg() const { return textFg_; }
    void setTextOpaque(bool on) { textOpaque_ = on; }
    int drawChar(u8 c, int x, int y);
    int drawString(std::string_view s, int x, int y);
    void draw4x6String(const Font& f4x6, std::string_view s, int x, int y);

private:
    Bitmap& surf() { return target_ ? *target_ : pages_[drawPage_]; }
    int surfW() { return target_ ? target_->w : 320; }
    int surfH() { return target_ ? target_->h : 200; }
    // Clear high-resolution coverage for pixels x0..x1 of row y of the draw page.
    void touch(int x0, int x1, int y) {
        if (layer_ && !target_) touchSlow(x0, x1, y);
    }
    void touchSlow(int x0, int x1, int y);
    HiResLayer* layerOf(const Bitmap& b) const;  // the page's layer, nullptr if none / not a page
    static void uncover(HiResLayer& l, int x0, int x1, int y);
    bool patternAllows(int x, int y, bool pixelQuirk) const;
    void plot(int x, int y);                // span-convention pattern, no clip
    void rawSpan(int x0, int x1, int y);    // clipped span with current colour
    void lineXMajor(int x0, int y0, int x1, int y1);
    void lineYMajor(int x0, int y0, int x1, int y1);

    Bitmap pages_[2];
    Bitmap* target_ = nullptr;
    HiResLayer* layer_ = nullptr;  // the draw page's high-resolution layer (nullptr: none)
    int drawPage_ = 0, displayPage_ = 0;
    int x0_ = 0, y0_ = 0, x1_ = 319, y1_ = 199, cw_ = 320, ch_ = 200, cx_ = 159, cy_ = 99;
    u8 color_ = 0;
    u8 pattern_ = 0xff;
    const u8* remap_ = nullptr;
    const Font* font_ = nullptr;
    u8 textFg_ = 15, textBg_ = 0;
    bool textOpaque_ = false;
};

Gfx& gfx();

} // namespace st
