#include "gfx/gfx.h"

#include "data/ealib.h"
#include "engine/ticker.h"
#include "gfx/font.h"
#include "platform/system.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace st {

namespace {

constexpr u32 kPageLinear[2] = {0x00000, 0x10000};  // planar 0x0000 / 0x4000

int sgn(int v) { return (v > 0) - (v < 0); }

} // namespace

Gfx& gfx() {
    static Gfx instance;
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

// ---------------------------------------------------------------- pages

void Gfx::init() {
    Video& v = sys().video();
    for (int p = 0; p < 2; ++p) {
        Bitmap& b = pages_[p];
        b.flags = 1;
        b.w = 320;
        b.h = 200;
        b.bpr = 80;
        b.data = v.vram() + kPageLinear[p];
    }
    std::memset(v.vram(), 0, kVramSize);
    setDrawPage(0);
    setDisplayPage(0);
    clipFull();
    beginColor(kSolid);
}

void Gfx::initPages() {
    setDrawPage(1);
    setDisplayPage(0);
}

void Gfx::setDrawPage(int page) { drawPage_ = page & 1; }

void Gfx::setDisplayPage(int page) {
    displayPage_ = page & 1;
    sys().video().setDisplayStart(kPageLinear[displayPage_]);
}

void Gfx::flip(bool flipPages, bool wait) {
    if (!flipPages) return;
    setDrawPage(displayPage_);
    setDisplayPage(1 - drawPage_);
    if (wait) {
        // Give the CRTC time to latch the new start address before the old
        // page is drawn over: a fixed 5-tick pause after every flip.
        engine::ticker().frameLimitReset();
        engine::ticker().frameLimitWait();
    }
}

void Gfx::copyPage(int src, int dst) {
    clipFull();
    std::memcpy(pages_[dst & 1].data, pages_[src & 1].data, size_t(kPageSize));
    sys().video().markDirty();
}

void Gfx::clear(u8 color) {
    const int sx0 = x0_, sy0 = y0_, sw = cw_, sh = ch_;
    clipFull();
    fillRows(0, 200, color);
    setClip(sx0, sy0, sw, sh);
}

// ---------------------------------------------------------------- clip

void Gfx::setClip(int x, int y, int w, int h) {
    x0_ = x;
    y0_ = y;
    cw_ = w;
    ch_ = h;
    x1_ = x + w - 1;
    y1_ = y + h - 1;
    cx_ = int(u16(x0_ + x1_) >> 1);
    cy_ = int(u16(y0_ + y1_) >> 1);
}

// ---------------------------------------------------------------- colour

void Gfx::beginColor(u16 colorWord) {
    color_ = u8(colorWord & 0xff);
    pattern_ = u8(colorWord >> 8);
}

// Pattern p is a 4x2 write mask: even rows use the low nibble, odd rows the
// high nibble; bit 3 is the leftmost pixel of each group of four. The single
// pixel routine has the rows the other way round (original quirk).
bool Gfx::patternAllows(int x, int y, bool pixelQuirk) const {
    if (pattern_ == 0xff) return true;
    const bool odd = (y & 1) != pixelQuirk;
    const int nib = odd ? (pattern_ >> 4) : (pattern_ & 0xf);
    return (nib >> (3 - (x & 3))) & 1;
}

void Gfx::plot(int x, int y) {
    if (patternAllows(x, y, false)) screen().row(y)[x] = color_;
}

// ---------------------------------------------------------------- filled primitives

void Gfx::rawSpan(int x0, int x1, int y) {
    if (y < y0_ || y > y1_) return;
    x0 = std::max(x0, x0_);
    if (x0 > x1_ || x1 < x0_) return;
    x1 = std::min(x1, x1_);
    if (x1 < x0) return;
    u8* row = screen().row(y);
    if (pattern_ == 0xff) {
        std::memset(row + x0, color_, size_t(x1 - x0 + 1));
    } else {
        for (int x = x0; x <= x1; ++x)
            if (patternAllows(x, y, false)) row[x] = color_;
    }
}

void Gfx::hspan(int x0, int x1, int y) { rawSpan(x0, x1, y); }

void Gfx::hline(int x0, int x1, int y, u16 c) {
    beginColor(c);
    rawSpan(x0, x1, y);
}

void Gfx::vline(int x, int y0, int y1, u16 c) { line(x, y0, x, y1, c); }

void Gfx::rect(int x, int y, int w, int h, u16 c) {
    hline(x, x + w - 1, y, c);
    vline(x + w - 1, y, y + h - 1, c);
    hline(x, x + w - 1, y + h - 1, c);
    vline(x, y, y + h - 1, c);
}

void Gfx::fillRect(int x, int y, int w, int h, u16 c) {
    if (h <= 0) return;
    beginColor(c);
    const int ya = std::max(y, y0_), yb = std::min(y + h - 1, y1_);
    for (int yy = ya; yy <= yb; ++yy) rawSpan(x, x + w - 1, yy);
}

void Gfx::fillRows(int y, int n, u8 c) {
    // Byte-granular row fill: from the clip's first 4-pixel column for the clip
    // width rounded down to 8 pixels.
    const int xs = x0_ & ~3;
    const int len = 8 * (cw_ >> 3);
    const int ya = std::max(y, y0_), yb = std::min(y + n - 1, y1_);
    for (int yy = ya; yy <= yb; ++yy) {
        u8* row = screen().row(yy);
        const int a = std::max(xs, 0), b = std::min(xs + len, 320);
        if (b > a) std::memset(row + a, c, size_t(b - a));
    }
}

void Gfx::fillSplitRows(const s16* xs, int y0, int y1, u8 left, u8 right) {
    const int colStart = (x0_ >> 2) * 4;
    const int colEnd = (x1_ >> 2) * 4 + 3;
    for (int y = y0; y <= y1; ++y) {
        const int x = xs[y - y0];
        if (x < x0_) {
            fillRows(y, 1, right);
        } else if (x >= x1_) {
            fillRows(y, 1, left);
        } else {
            if (y < y0_ || y > y1_) continue;
            u8* row = screen().row(y);
            for (int px = std::max(colStart, 0); px <= x; ++px) row[px] = left;
            for (int px = x + 1; px <= std::min(colEnd, 319); ++px) row[px] = right;
        }
    }
}

void Gfx::fillCircle(int x, int y, int r, u16 c) {
    if (r < 0 || r >= 750) return;
    if (r == 0) {
        pixel(x, y, c);
        return;
    }
    beginColor(c);
    if (r == 1) {
        rawSpan(x, x, y - 1);
        rawSpan(x - 1, x + 1, y);
        rawSpan(x, x, y + 1);
        return;
    }
    if (x + r < x0_ || x - r > x1_ || y + r < y0_ || y - r > y1_) return;
    const int ya = std::max(y - r, y0_), yb = std::min(y + r, y1_);
    std::vector<int> left(size_t(yb - ya + 1), 0);
    auto store = [&](int row, int lx) {
        if (row >= ya && row <= yb) left[size_t(row - ya)] = lx;
    };
    auto octants = [&](int a, int b) {
        store(y + b, x - a);
        store(y - b, x - a);
        store(y + a, x - b);
        store(y - a, x - b);
    };
    int d = 3 - 2 * r, i = 0, j = r;
    while (i < j) {
        octants(i, j);
        if (d < 0) {
            d += 4 * i + 5;
        } else {
            d += 4 * (i - j) + 10;
            --j;
        }
        ++i;
    }
    if (i == j) octants(i, j);
    for (int row = ya; row <= yb; ++row) {
        const int lx = left[size_t(row - ya)];
        rawSpan(lx, 2 * x - lx, row);
    }
}

namespace {

// One polygon chain edge stepper (edge set-up of 10FC, see seg_2255.md 3.4).
struct Edge {
    int x = 0, q = 0, r = 0, dy = 0, err = 0;
    bool neg = false;

    void setup(int xa, int ya, int xb, int yb, bool isLeft) {
        const int dx = xb - xa;
        dy = yb - ya;
        const int dyp = (std::abs(dy) <= std::abs(dx)) ? dy + 1 : dy;
        q = dx / dyp;
        r = dx % dyp;
        x = xa;
        neg = dx < 0;
        if (!neg) err = 2 * r - dy;
        else err = 2 * std::abs(r) - dy - 1;
        if ((isLeft && q <= -1) || (!isLeft && q >= 1)) step();
    }
    void step() {
        const int rr = std::abs(r);
        if (err >= 0) {
            x += neg ? q - 1 : q + 1;
            err += 2 * rr - 2 * dy;
        } else {
            x += q;
            err += 2 * rr;
        }
    }
};

} // namespace

void Gfx::fillPolygon(const s16* pts, int n, u16 c) {
    if (n <= 0) return;
    beginColor(c);
    auto px = [&](int i) { return int(pts[2 * i]); };
    auto py = [&](int i) { return int(pts[2 * i + 1]); };
    auto wrap = [&](int i) { return (i % n + n) % n; };

    int ymin = py(0), ymax = py(0), top = 0;
    for (int i = 0; i < n; ++i) {
        if (py(i) <= ymin) {
            if (py(i) < ymin) ymin = py(i);
            top = i;  // last index with y == ymin
        }
        ymax = std::max(ymax, py(i));
    }
    if (ymax <= y0_) return;

    // Start vertices of the two chains.
    int lStart = top, rStart = top;
    int flatCount = 0;
    for (int i = 0; i < n; ++i) {
        if (py(i) != ymin) continue;
        ++flatCount;
        if (px(i) < px(lStart)) lStart = i;
        if (px(i) > px(rStart)) rStart = i;
    }
    if (flatCount == n) {
        if (ymin <= y1_) {
            int mn = px(0), mx = px(0);
            for (int i = 1; i < n; ++i) {
                mn = std::min(mn, px(i));
                mx = std::max(mx, px(i));
            }
            rawSpan(mn, mx, ymin);
        }
        return;
    }

    // Direction each chain walks. With a single top vertex the left chain goes
    // to the neighbour with the smaller x (equal: index-1 is left).
    int lDir, rDir;
    if (flatCount > 1) {
        // Walk away from the flat run: the left start's neighbour that is not on
        // the top row is on its chain.
        lDir = (py(wrap(lStart - 1)) != ymin) ? -1 : 1;
        rDir = (py(wrap(rStart + 1)) != ymin) ? 1 : -1;
        if (lDir == rDir) rDir = -lDir;
    } else {
        const int prev = wrap(top - 1), next = wrap(top + 1);
        lDir = (px(prev) <= px(next)) ? -1 : 1;
        rDir = -lDir;
    }

    struct Chain {
        int cur, dir, nextY;
        Edge e;
        bool done = false;
    };
    auto advanceChain = [&](Chain& ch, bool isLeft) {
        // Move to the next edge that spans at least one row.
        for (int guard = 0; guard < n; ++guard) {
            const int nxt = wrap(ch.cur + ch.dir);
            if (py(nxt) < py(ch.cur)) {  // chain turned upwards: no vertex below
                ch.done = true;
                return;
            }
            if (py(nxt) == py(ch.cur)) {  // horizontal edge: skip
                ch.cur = nxt;
                continue;
            }
            ch.e.setup(px(ch.cur), py(ch.cur), px(nxt), py(nxt), isLeft);
            ch.nextY = py(nxt);
            ch.cur = nxt;
            return;
        }
        ch.done = true;
    };
    Chain L{lStart, lDir, 0, {}}, R{rStart, rDir, 0, {}};
    advanceChain(L, true);
    advanceChain(R, false);
    if (L.done || R.done) return;

    int y = ymin;
    // Advance both chains to the first visible row.
    while (y < y0_) {
        L.e.step();
        R.e.step();
        ++y;
        if (y == L.nextY) {
            L.e.x = px(L.cur);
            advanceChain(L, true);
            if (L.done) return;
        }
        if (y == R.nextY) {
            R.e.x = px(R.cur);
            advanceChain(R, false);
            if (R.done) return;
        }
    }

    int prevL = -32768, prevR = 32767;
    const int yEnd = std::min(ymax, y1_);
    while (y <= yEnd) {
        if (L.e.x > R.e.x) std::swap(L, R);
        const int a = std::min(L.e.x, prevR), b = std::max(R.e.x, prevL);
        rawSpan(a, b, y);
        prevL = L.e.x;
        prevR = R.e.x;
        L.e.step();
        R.e.step();
        ++y;
        bool finished = false;
        if (y == L.nextY) {
            L.e.x = px(L.cur);
            advanceChain(L, true);
            finished |= L.done;
        }
        if (y == R.nextY) {
            R.e.x = px(R.cur);
            advanceChain(R, false);
            finished |= R.done;
        }
        if (finished) {
            if (y <= yEnd) rawSpan(std::min(L.e.x, R.e.x), std::max(L.e.x, R.e.x), y);
            return;
        }
    }
}

void Gfx::pixel(int x, int y, u16 c) {
    beginColor(c);
    if (x < x0_ || x > x1_ || y < y0_ || y > y1_) return;
    if (patternAllows(x, y, true)) screen().row(y)[x] = color_;
}

// ---------------------------------------------------------------- lines

void Gfx::line(int x0, int y0, int x1, int y1, u16 c) {
    beginColor(c);
    if (std::abs(y0 - y1) < std::abs(x0 - x1)) lineXMajor(x0, y0, x1, y1);
    else lineYMajor(x0, y0, x1, y1);
}

// 2.14 fixed-point DDA, x-major (see seg_2255.md 3.5). Clipping moves an
// endpoint and restarts the whole set-up from the new coordinates.
void Gfx::lineXMajor(int x0, int y0, int x1, int y1) {
    for (int restart = 0; restart < 16; ++restart) {
        if (std::abs(y0 - y1) >= std::abs(x0 - x1)) {
            lineYMajor(x0, y0, x1, y1);
            return;
        }
        if (x0 >= x1) {
            std::swap(x0, x1);
            std::swap(y0, y1);
        }
        if (x0 > x1_ || x1 < x0_) return;
        int dx = x1 - x0, dy = y1 - y0;
        if (dy != 0) dy += sgn(dy);
        if (std::abs(dx) > 0x3fff || std::abs(dy) > 0x3fff) {
            dx >>= 1;
            dy >>= 1;
        }
        const s32 s = s32((s32(dy) * 32768 + dx) / (2 * dx));
        if (x0 < x0_) {
            y0 += int((s32(x0_ - x0) * s) >> 14);
            x0 = x0_;
            continue;
        }
        if (x1 > x1_) {
            y1 += int((s32(x1_ - x1) * s) >> 14);
            x1 = x1_;
            continue;
        }
        if (s >= 0) {
            if (y0 > y1_ || y1 < y0_) return;
            if (y0 < y0_) {
                x0 += int(((s32(y0_ - y0) << 16) / (s ? s : 1)) >> 2);
                y0 = y0_;
                continue;
            }
            if (y1 > y1_) {
                x1 += int(((s32(y1_ - y1) << 16) / (s ? s : 1)) >> 2);
                y1 = y1_;
                continue;
            }
        } else {
            if (y1 > y1_ || y0 < y0_) return;
            if (y0 > y1_) {
                x0 += int(((s32(y1_ - y0) << 16) / s) >> 2);
                y0 = y1_;
                continue;
            }
            if (y1 < y0_) {
                x1 += int(((s32(y0_ - y1) << 16) / s) >> 2);
                y1 = y0_;
                continue;
            }
        }
        u16 acc = u16((y0 & 2) << 14);
        if (s < 0) acc |= 0x3fff;
        u16 topBits = acc & 0xc000;
        const int nsteps = x1 - x0;
        int x = x0, y = y0;
        if (y >= y0_ && y <= y1_) plot(x, y);
        for (int k = 0; k < nsteps; ++k) {
            ++x;
            acc = u16(acc + u16(s));
            if ((acc & 0xc000) != topBits) {
                topBits = acc & 0xc000;
                if (k != nsteps - 1) y += sgn(s);
            }
            if (y >= y0_ && y <= y1_) plot(x, y);
        }
        return;
    }
}

void Gfx::lineYMajor(int x0, int y0, int x1, int y1) {
    for (int restart = 0; restart < 16; ++restart) {
        if (y0 >= y1 && !(y0 == y1 && x0 == x1)) {
            std::swap(x0, x1);
            std::swap(y0, y1);
        }
        if (y0 > y1_ || y1 < y0_) return;
        int dx = x1 - x0, dy = y1 - y0;
        if (std::abs(dx) > 0x3fff || std::abs(dy) > 0x3fff) {
            dx >>= 1;
            dy >>= 1;
        }
        const s32 s = dy ? s32((s32(dx) * 32768 + dy) / (2 * dy)) : 0;
        if (y0 < y0_) {
            x0 += int((s32(y0_ - y0) * s) >> 14);
            y0 = y0_;
            continue;
        }
        if (y1 > y1_) {
            x1 += int((s32(y1_ - y1) * s) >> 14);
            y1 = y1_;
            continue;
        }
        const int lo = std::min(x0, x1), hi = std::max(x0, x1);
        if (lo > x1_ || hi < x0_) return;
        if (s != 0) {
            if (x0 < x0_ || x0 > x1_) {
                const int cx = x0 < x0_ ? x0_ : x1_;
                y0 += int(((s32(cx - x0) << 16) / s) >> 2);
                x0 = cx;
                continue;
            }
            if (x1 < x0_ || x1 > x1_) {
                const int cx = x1 < x0_ ? x0_ : x1_;
                y1 += int(((s32(cx - x1) << 16) / s) >> 2);
                x1 = cx;
                continue;
            }
        }
        u16 acc = u16((x0 << 14) + 0x1fff);
        u16 topBits = acc & 0xc000;
        int x = x0;
        for (int y = y0; y <= y1; ++y) {
            if ((acc & 0xc000) != topBits) {
                topBits = acc & 0xc000;
                x += sgn(s);
            }
            if (x >= x0_ && x <= x1_) plot(x, y);
            acc = u16(acc + u16(s));
        }
        return;
    }
}

// ---------------------------------------------------------------- sprites

namespace {

// Walks one RLE row: u16 byte count, then runs.
const u8* nextRow(const u8* row) { return row + 2 + rd16(row); }

} // namespace

void Gfx::spriteScaled(int x, int y, int w, int h, const u8* spr) {
    if (w <= 0 || h <= 0 || !spr) return;
    const int sw = rd16(spr), sh = rd16(spr + 2);
    const u8 transparent = spr[4];
    if (sw <= 0 || sh <= 0) return;

    const int skipRows = std::max(0, y0_ - y), skipCols = std::max(0, x0_ - x);
    const int visH = std::min(y + h - 1, y1_) - (y + skipRows) + 1;
    const int visW = std::min(x + w - 1, x1_) - (x + skipCols) + 1;
    if (visH <= 0 || visW <= 0) return;

    const u8* src = spr + 8;
    int acc = sh >> 1;
    int n = skipRows;
    while (n > 0) {  // original order: advance, then accumulate (see notes)
        src = nextRow(src);
        acc += h;
        while (acc >= sh && n > 0) {
            acc -= sh;
            --n;
        }
    }
    int rowsLeft = visH;
    acc += h;
    while (acc < sh) {
        src = nextRow(src);
        acc += h;
    }
    int dy = y + skipRows;
    for (;;) {
        // draw_row
        u8* out = screen().row(dy);
        const int len = rd16(src);
        const u8* p = src + 2;
        const u8* end = p + len;
        int ax = sw >> 1, col = 0;
        bool rowDone = false;
        auto emit = [&](u8 pix) {
            ax += w;
            while (ax >= sw) {
                ax -= sw;
                if (col < skipCols) {
                    ++col;
                    continue;
                }
                u8 v = remap_ ? remap_[pix] : pix;
                if (v != transparent) out[x + col] = v;
                ++col;
                if (col == skipCols + visW) {
                    rowDone = true;
                    return;
                }
            }
        };
        while (p < end && !rowDone) {
            const u8 ctl = *p++;
            if (ctl & 0x80) {
                const u8 v = *p++;
                for (int k = 0; k < (ctl & 0x7f) && !rowDone; ++k) emit(v);
            } else {
                for (int k = 0; k < ctl && p < end && !rowDone; ++k) emit(*p++);
            }
        }
        ++dy;
        acc -= sh;
        if (--rowsLeft == 0) break;
        while (acc < sh) {
            src = nextRow(src);
            acc += h;
        }
    }
}

void Gfx::spriteCentered(const u8* spr, int cx, int cy, int w, int h) {
    if (std::abs(cx) >= 10000 || std::abs(cy) >= 10000) return;
    spriteScaled(cx - w / 2, cy - h / 2, w, h, spr);
}

// ---------------------------------------------------------------- bitmaps

bool Gfx::createBitmap(Bitmap& b, int w, int h) {
    b.flags = 0;
    b.w = w;
    b.h = h;
    b.bpr = ((w + 7) >> 3) * 2;
    b.storage.assign(size_t(b.stride()) * size_t(h), 0);
    b.data = b.storage.data();
    return true;
}

// Latched copies move whole 4-pixel groups.
void Gfx::blit(const Bitmap& src, int sx, int sy, Bitmap& dst, int dx, int dy, int w, int h) {
    const int groups = w >> 2;
    for (int r = 0; r < h; ++r) {
        const u8* s = src.data + (sy + r) * src.stride() + (sx & ~3);
        u8* d = dst.data + (dy + r) * dst.stride() + (dx & ~3);
        std::memcpy(d, s, size_t(groups) * 4);
    }
}

void Gfx::blitMasked(const Bitmap& src, int sx, int sy, Bitmap& dst, int dx, int dy, int w, int h,
                     const u8* mask) {
    const int pairs = w >> 3;
    for (int r = 0; r < h; ++r) {
        for (int k = 0; k < pairs; ++k) {
            const int o = (sy + r) * src.bpr + (sx >> 2) + 2 * k;  // planar byte offset
            const u8 m = mask[o >> 1];
            for (int half = 0; half < 2; ++half) {
                const int planes = half == 0 ? (m >> 4) : (m & 0xf);
                const u8* s = src.data + (sy + r) * src.stride() + (sx & ~3) + 8 * k + 4 * half;
                u8* d = dst.data + (dy + r) * dst.stride() + (dx & ~3) + 8 * k + 4 * half;
                for (int p = 0; p < 4; ++p)
                    if (planes & (1 << p)) d[p] = s[p];
            }
        }
    }
}

void Gfx::drawImage(const Image& img, int x, int y) {
    const int xa = std::max(x, x0_), xb = std::min(x + img.w - 1, x1_);
    const int ya = std::max(y, y0_), yb = std::min(y + img.h - 1, y1_);
    for (int yy = ya; yy <= yb; ++yy) {
        const u8* s = img.row(yy - y);
        u8* d = screen().row(yy);
        for (int xx = xa; xx <= xb; ++xx) d[xx] = s[xx - x];
    }
}

void Gfx::drawImageMasked(const Image& img, const Mask& mask, int x, int y) {
    const int xa = std::max(x, x0_), xb = std::min(x + img.w - 1, x1_);
    const int ya = std::max(y, y0_), yb = std::min(y + img.h - 1, y1_);
    for (int yy = ya; yy <= yb; ++yy) {
        const u8* s = img.row(yy - y);
        u8* d = screen().row(yy);
        for (int xx = xa; xx <= xb; ++xx)
            if (mask.opaque(xx - x, yy - y)) d[xx] = s[xx - x];
    }
}

// ---------------------------------------------------------------- text

int Gfx::drawChar(u8 c, int x, int y) {
    if (!font_) return 0;
    const int w = font_->glyphWidth(c);
    const int h = font_->height();
    if (w <= 0) return 0;
    // No partial clipping: the whole glyph box must be inside the clip.
    if (x < x0_ || x + w - 1 > x1_ || y < y0_ || y + h - 1 > y1_) return w;
    for (int r = 0; r < h; ++r) {
        u8* row = screen().row(y + r);
        for (int i = 0; i < w; ++i)
            if (font_->pixel(c, i, r)) row[x + i] = textFg_;
    }
    return w;
}

int Gfx::drawString(std::string_view s, int x, int y) {
    for (char ch : s) x += drawChar(u8(ch), x, y);
    return x;
}

void Gfx::draw4x6String(const Font& f, std::string_view s, int x, int y) {
    if (textOpaque_) fillRect(x, y, 4 * int(s.size()), 6, u16(kSolid | textBg_));
    for (char ch : s) {
        for (int r = 0; r < 6; ++r) {
            const int yy = y + r;
            if (yy < 0 || yy >= 200) continue;
            u8* row = screen().row(yy);
            for (int i = 0; i < 4; ++i) {
                const int xx = x + i;
                if (xx >= 0 && xx < 320 && f.pixel(u8(ch), i, r)) row[xx] = textFg_;
            }
        }
        x += 4;
    }
}

} // namespace st
