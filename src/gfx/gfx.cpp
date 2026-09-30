#include "gfx/gfx.h"

#include "data/ealib.h"
#include "engine/ticker.h"
#include "gfx/font.h"
#include "platform/system.h"
#include "platform/video.h"

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

void Gfx::setDrawPage(int page) {
    drawPage_ = page & 1;
    refreshCoverage();
}

u8* Gfx::coverageOf(const Bitmap& b) const {
    for (int p = 0; p < 2; ++p)
        if (&b == &pages_[p] || b.data == pages_[p].data) {
            HiResLayer* l = sys().video().hiResLayer(p);
            return l ? l->coverage.data() : nullptr;
        }
    return nullptr;
}

void Gfx::refreshCoverage() { cov_ = coverageOf(pages_[drawPage_]); }

void Gfx::touchSlow(int x0, int x1, int y) {
    if (y < 0 || y >= 200) return;
    x0 = std::max(x0, 0);
    x1 = std::min(x1, 319);
    if (x1 >= x0) std::memset(cov_ + y * 320 + x0, 0, size_t(x1 - x0 + 1));
}

void Gfx::setDisplayPage(int page) {
    displayPage_ = page & 1;
    sys().video().setDisplayStart(kPageLinear[displayPage_]);
}

void Gfx::flip(bool flipPages, bool wait) {
    if (!flipPages) return;
    setDrawPage(displayPage_);
    setDisplayPage(1 - drawPage_);
    sys().video().present(true);  // the flip is the moment the new frame becomes visible
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
    sys().video().copyHiResLayer(src & 1, dst & 1);
    refreshCoverage();
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
    if (patternAllows(x, y, false)) {
        surf().row(y)[x] = color_;
        touch(x, x, y);
    }
}

// ---------------------------------------------------------------- filled primitives

void Gfx::rawSpan(int x0, int x1, int y) {
    if (y < y0_ || y > y1_) return;
    x0 = std::max(x0, x0_);
    if (x0 > x1_ || x1 < x0_) return;
    x1 = std::min(x1, x1_);
    if (x1 < x0) return;
    touch(x0, x1, y);
    u8* row = surf().row(y);
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
    const int ya = std::max(y, std::max(y0_, 0)), yb = std::min(y + n - 1, std::min(y1_, surfH() - 1));
    for (int yy = ya; yy <= yb; ++yy) {
        u8* row = surf().row(yy);
        const int a = std::max(xs, 0), b = std::min(xs + len, surfW());
        if (b > a) {
            std::memset(row + a, c, size_t(b - a));
            touch(a, b - 1, yy);
        }
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
            if (y < y0_ || y > y1_ || y < 0 || y >= surfH()) continue;
            u8* row = surf().row(y);
            const int wmax = surfW() - 1;
            for (int px = std::max(colStart, 0); px <= std::min(x, wmax); ++px) row[px] = left;
            for (int px = std::max(x + 1, 0); px <= std::min(colEnd, wmax); ++px) row[px] = right;
            touch(colStart, colEnd, y);
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

// ---- polygon filler 2255:10FC (seg_2255.md 3.4) ------------------------------
// The original works on 16-bit words throughout; w16() reproduces the wrap.
inline s16 w16(int v) { return s16(u16(unsigned(v))); }
inline s16 abs16(s16 v) { return v < 0 ? w16(-int(v)) : v; }

// One chain of the filler. The original keeps a "left" and a "right" set of
// variables (DS:5098/509C/50A0/50A4/50A6/50B2 and 509A/509E/50A8/50AC/50AE/
// 50B0) plus step values patched into its stepping code, and exchanges the
// complete sets when the chains cross (1844).
struct PolyChain {
    int cur = 0, end = 0;     // vertices the current edge runs from / to
    s16 x = 0;                // x on the current row
    s16 endX = 0, endY = 0;   // end vertex; endY is the row test of 152F/1535
    s16 err = 0;
    s16 q = 0, qAlt = 0;      // x step when err < 0 / err >= 0
    s16 inc = 0, incAlt = 0;  // err step when err < 0 / err >= 0

    void step() {  // 14E4
        if (err < 0) {
            err = w16(err + inc);
            x = w16(x + q);
        } else {
            err = w16(err + incAlt);
            x = w16(x + qAlt);
        }
    }
};

enum class EdgeSetup { Ok, Flat, Exhausted };

// Edge set-up 1615 (left slot) / 1732 (right slot): from (e.x, y) to the end
// vertex. 32-bit differences, both halved until |dy| < 0x3FFF, IDIV by dy
// (dy + sign(dy) when |dy| <= |dx|), Bresenham-style error terms.
EdgeSetup polySetupEdge(PolyChain& e, int endIdx, s16 endX, s16 endY, s16 y, bool rightSlot,
                        int& edgesLeft) {
    if (--edgesLeft < 0) return EdgeSetup::Exhausted;
    e.end = endIdx;
    e.endX = endX;
    e.endY = endY;
    const s32 dy32 = s32(endY) - s32(y);
    if (dy32 == 0) return EdgeSetup::Flat;
    const s32 dx32 = s32(endX) - s32(e.x);
    u16 dyLo = u16(u32(dy32)), dxLo = u16(u32(dx32));
    s16 dyHi = s16(dy32 >> 16);
    const s16 dxHi = s16(dx32 >> 16);
    // Quirk (1667): until a halving happens the sign used below is the sign
    // of the LOW word of dx, so |dx| >= 0x8000 takes the wrong branch.
    bool negative = s16(dxLo) < 0;
    for (;;) {
        const s16 ext = s16(dyLo) < 0 ? -1 : 0;
        bool fits;
        if (ext != dyHi) fits = false;
        else if (dyHi < 0) fits = s16(dyLo) > -16383;
        else fits = s16(dyLo) < 0x3FFF;
        if (fits) break;
        dyLo = u16((dyLo >> 1) | ((u16(dyHi) & 1u) << 15));
        dyHi = s16(dyHi >> 1);
        // The high word of dx is reloaded unhalved every time (harmless: it
        // is 0 or -1 for 16-bit coordinates).
        dxLo = u16((dxLo >> 1) | ((u16(dxHi) & 1u) << 15));
        negative = s16(dxHi >> 1) < 0;
    }
    const s16 dy = s16(dyLo), dxl = s16(dxLo);
    s16 div = dy;
    if (!(abs16(dy) > abs16(dxl))) div = w16(div + (div < 0 ? -1 : 1));
    const s32 dividend = s32(u32(u16(dxHi)) << 16 | dxLo);
    const s16 q = w16(dividend / div), r = w16(dividend % div);
    e.q = q;
    if (!negative) {
        const s16 twoR = w16(r * 2);
        e.inc = twoR;
        e.err = w16(twoR - dy);
        e.incAlt = w16(twoR - 2 * dy);
        e.qAlt = w16(q + 1);
        // Right edges moving right by >= 1 pixel per row take one step at
        // once. Quirk (1803): the choice tests 2r - 2dy, not the error term.
        if (rightSlot && q >= 1) {
            if (e.incAlt >= 0) {
                e.x = w16(e.x + e.qAlt);
                e.err = w16(e.err + e.incAlt);
            } else {
                e.x = w16(e.x + q);
                e.err = w16(e.err + twoR);
            }
        }
    } else {
        const s16 twoR = w16(-r * 2);
        e.inc = twoR;
        e.err = w16(twoR - dy - 1);
        e.incAlt = w16(twoR - 2 * dy);
        e.qAlt = w16(q - 1);
        // Left edges moving left by >= 1 pixel per row take one step at once.
        if (!rightSlot && q <= -1) e.step();
    }
    return EdgeSetup::Ok;
}

// gfx_poly_clip_top (140E): move the start (xs, ys) of an edge that crosses
// the top clip row down to it by integer bisection (not by stepping the
// edge), so the edge restarts from the cut point with a fresh set-up.
void polyClipTop(s16& xs, s16& ys, s16 xe, s16 ye, s16 y0) {
    const s16 hx = w16((xe >> 1) - (xs >> 1));
    const bool negative = hx < 0;
    s16 mx = w16(xs + hx);
    s16 my = w16(ys + w16((ye >> 1) - (ys >> 1)));
    for (int guard = 0; guard < 64; ++guard) {
        if (my == y0) {
            xs = mx;
            ys = y0;
            return;
        }
        if (my > y0) {
            xe = mx;
            ye = my;
        } else {
            xs = mx;
            ys = my;
        }
        // A halving that gives 0 keeps the dropped bit (rcl at 1441 / 1484).
        if (!negative) {
            const s16 d = w16(xe - xs);
            s16 h = s16(d >> 1);
            if (h == 0) h = s16(d & 1);
            mx = w16(xs + h);
        } else {
            const s16 d = w16(-(int(xe) - int(xs)));
            s16 h = s16(d >> 1);
            if (h == 0) h = s16(d & 1);
            mx = w16(xs - h);
        }
        const s16 hy = s16(w16(ye - ys) >> 1);
        if (hy == 0) return;  // 144F: keeps (xs, ys)
        my = w16(ys + hy);
    }
}

} // namespace

// gfx_fill_polygon (2255:10FC). Faithful port including the chain start rules
// for flat tops, the bisection top clip, the per-edge set-up quirks, span
// joining with the previous row and the end-of-chain handling (the last row
// is drawn without joining).
void Gfx::fillPolygon(const s16* pts, int n, u16 c) {
    if (n <= 0) return;
    beginColor(c);
    auto X = [&](int i) { return pts[2 * i]; };
    auto Y = [&](int i) { return pts[2 * i + 1]; };
    const int last = n - 1;
    auto fwd = [&](int i) { return i + 1 > last ? 0 : i + 1; };
    auto back = [&](int i) { return i - 1 < 0 ? last : i - 1; };
    // Vertex after `to` on a chain that came from `from` (130D, 1545, 15AA).
    auto following = [&](int to, int from) {
        const int b = fwd(to);
        return b == from ? back(to) : b;
    };
    const s16 cy0 = s16(y0_), cy1 = s16(y1_);

    // Topmost vertex (the last of equals), the previous topmost if the top
    // is flat, and the bottom row (112E).
    int top = 0, top2 = -1;
    s16 ymin = Y(0), ymax = Y(0);
    for (int i = 1; i < n; ++i) {
        if (Y(i) <= ymin) {
            top2 = Y(i) == ymin ? top : -1;
            ymin = Y(i);
            top = i;
        }
        if (Y(i) >= ymax) ymax = Y(i);
    }
    if (ymax <= cy0) return;

    PolyChain L, R;
    s16 bp;
    if (top2 != -1) {
        // Flat top (117B): left start = leftmost top vertex (last of equals),
        // right start = rightmost (first of equals).
        s16 lx = X(top), rx = s16(-32768);
        int lIdx = top, rIdx = top2, count = 0;
        for (int i = 0; i < n; ++i) {
            if (Y(i) != ymin) continue;
            ++count;
            if (lx >= X(i)) {
                lx = X(i);
                lIdx = i;
            }
            if (rx < X(i)) {
                rx = X(i);
                rIdx = i;
            }
        }
        L.x = X(lIdx);
        R.x = X(rIdx);
        bp = Y(lIdx);
        if (count == n) {  // the whole polygon is one row
            if (bp <= cy1) rawSpan(std::min(L.x, R.x), std::max(L.x, R.x), bp);
            return;
        }
        // The left chain leaves its start backwards if that neighbour is off
        // the top row, else forwards, skipping identical points; if both
        // neighbours are on the row the start moves on (11FE). The right
        // chain tries forwards first (124D).
        auto findChain = [&](int& startIdx, bool backFirst, PolyChain& ch) {
            for (int guard = 0; guard <= n; ++guard) {
                const s16 cx = X(startIdx);
                int result = -1, s = startIdx;
                for (int pass = 0; pass < 2 && result < 0; ++pass) {
                    const bool backwards = (pass == 0) == backFirst;
                    int d = startIdx;
                    for (int k = 0; k <= n; ++k) {
                        s = d;
                        d = backwards ? back(d) : fwd(d);
                        if (Y(d) != bp) {
                            result = d;
                            break;
                        }
                        if (cx != X(d)) break;
                    }
                }
                if (result >= 0) {
                    ch.cur = s;
                    ch.end = result;
                    return true;
                }
                startIdx = fwd(startIdx);
            }
            return false;
        };
        if (!findChain(lIdx, true, L) || !findChain(rIdx, false, R)) return;
    } else {
        // Single top vertex (129F): the left chain takes the neighbour with the
        // smaller x (equal: the previous vertex).
        L.cur = R.cur = top;
        L.x = R.x = X(top);
        bp = Y(top);
        const int a = fwd(top), d = back(top);
        if (X(a) < X(d)) {
            L.end = a;
            R.end = d;
        } else {
            L.end = d;
            R.end = a;
        }
    }

    // Top clip (12F4): walk each chain to its first edge ending below the
    // clip row and cut that edge there.
    if (bp < cy0) {
        const s16 topY = bp;
        auto clipChain = [&](PolyChain& ch) {
            for (int guard = 0; Y(ch.end) <= cy0; ++guard) {
                if (guard > n) return false;
                const int nb = following(ch.end, ch.cur);
                ch.cur = ch.end;
                ch.end = nb;
                ch.x = X(ch.cur);
                bp = Y(ch.cur);
            }
            polyClipTop(ch.x, bp, X(ch.end), Y(ch.end), cy0);
            return true;
        };
        if (!clipChain(L)) return;
        bp = topY;  // the left chain's cut row is dropped; the right one's is used
        if (!clipChain(R)) return;
    }

    int edgesLeft = n;  // DS:508A: at most n edge set-ups
    s16 prevL = s16(-32768), prevR = 32767;  // previous span (50DC / 50DE)
    enum class St { Draw, RightAdvance, LeftCheck, LeftAdvance, Final };
    St st = St::Draw;
    switch (polySetupEdge(L, L.end, X(L.end), Y(L.end), bp, false, edgesLeft)) {
    case EdgeSetup::Exhausted: st = St::Final; break;
    case EdgeSetup::Flat: st = St::LeftAdvance; break;  // cannot happen here
    case EdgeSetup::Ok:
        switch (polySetupEdge(R, R.end, X(R.end), Y(R.end), bp, true, edgesLeft)) {
        case EdgeSetup::Exhausted: st = St::Final; break;
        case EdgeSetup::Flat: st = St::RightAdvance; break;  // cannot happen here
        case EdgeSetup::Ok:
            if (bp > cy1) return;
            break;
        }
        break;
    }

    for (int guard = 0; guard < 0x10000; ++guard) {
        switch (st) {
        case St::Draw: {  // 14C3
            if (L.x > R.x) std::swap(L, R);
            // Join with the previous row so steep edges leave no gaps; the
            // joined span is what the next row joins to.
            s16 a = L.x, b = R.x;
            if (a > prevR) a = prevR;
            if (b < prevL) b = prevL;
            prevL = a;
            prevR = b;
            rawSpan(a, b, bp);
            L.step();
            R.step();
            bp = w16(bp + 1);
            if (bp > cy1) return;
            if (bp >= R.endY) {
                prevR = R.x;
                st = St::RightAdvance;
            } else {
                st = St::LeftCheck;
            }
            break;
        }
        case St::RightAdvance: {  // 1545
            const int nb = following(R.end, R.cur);
            R.cur = R.end;
            R.end = nb;
            if (bp > Y(nb)) {  // chain turns upwards: last row
                R.x = prevR;
                st = St::Final;
                break;
            }
            R.x = R.endX;
            bp = R.endY;
            switch (polySetupEdge(R, nb, X(nb), Y(nb), bp, true, edgesLeft)) {
            case EdgeSetup::Exhausted: st = St::Final; break;
            case EdgeSetup::Flat: st = St::RightAdvance; break;
            case EdgeSetup::Ok: st = St::LeftCheck; break;
            }
            break;
        }
        case St::LeftCheck:  // 1535
            if (bp >= L.endY) {
                prevL = L.x;
                st = St::LeftAdvance;
            } else {
                st = St::Draw;
            }
            break;
        case St::LeftAdvance: {  // 15AA
            const int nb = following(L.end, L.cur);
            L.cur = L.end;
            L.end = nb;
            if (bp > Y(nb)) {
                L.x = prevL;
                st = St::Final;
                break;
            }
            L.x = L.endX;
            bp = L.endY;
            switch (polySetupEdge(L, nb, X(nb), Y(nb), bp, false, edgesLeft)) {
            case EdgeSetup::Exhausted: st = St::Final; break;
            case EdgeSetup::Flat: st = St::LeftAdvance; break;
            case EdgeSetup::Ok: st = St::Draw; break;
            }
            break;
        }
        case St::Final: {  // 15E6: last row, not joined
            if (bp > cy1) return;
            s16 a = L.x, b = R.x;
            if (!(a < b)) std::swap(a, b);
            rawSpan(a, b, bp);
            return;
        }
        }
    }
}

void Gfx::pixel(int x, int y, u16 c) {
    beginColor(c);
    if (x < x0_ || x > x1_ || y < y0_ || y > y1_) return;
    if (patternAllows(x, y, true)) {
        surf().row(y)[x] = color_;
        touch(x, x, y);
    }
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
        u8* out = surf().row(dy);
        touch(x + skipCols, x + skipCols + visW - 1, dy);
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
    if (u8* cov = coverageOf(dst)) {
        // Page to page: the high-resolution layer travels with the pixels.
        HiResLayer* sl = nullptr;
        HiResLayer* dl = nullptr;
        for (int p = 0; p < 2; ++p) {
            if (&src == &pages_[p] || src.data == pages_[p].data) sl = sys().video().hiResLayer(p);
            if (&dst == &pages_[p] || dst.data == pages_[p].data) dl = sys().video().hiResLayer(p);
        }
        const u8* scov = (sl && dl && sl->scale == dl->scale) ? sl->coverage.data() : nullptr;
        for (int r = 0; r < h; ++r) {
            const int yd = dy + r, ys = sy + r;
            if (yd < 0 || yd >= 200) continue;
            const int a = std::max(dx & ~3, 0), b = std::min((dx & ~3) + groups * 4, 320);
            if (b <= a) continue;
            if (!scov || ys < 0 || ys >= 200) {
                std::memset(cov + yd * 320 + a, 0, size_t(b - a));
                continue;
            }
            const int xs0 = (sx & ~3) + (a - (dx & ~3));
            std::memcpy(cov + yd * 320 + a, scov + ys * 320 + xs0, size_t(b - a));
            const int N = sl->scale;
            for (int j = 0; j < N; ++j)
                std::memcpy(&dl->pixels[size_t(yd * N + j) * size_t(dl->w) + size_t(a * N)],
                            &sl->pixels[size_t(ys * N + j) * size_t(sl->w) + size_t(xs0 * N)], size_t((b - a) * N));
        }
    }
    for (int r = 0; r < h; ++r) {
        const u8* s = src.data + (sy + r) * src.stride() + (sx & ~3);
        u8* d = dst.data + (dy + r) * dst.stride() + (dx & ~3);
        std::memcpy(d, s, size_t(groups) * 4);
    }
}

void Gfx::blitMasked(const Bitmap& src, int sx, int sy, Bitmap& dst, int dx, int dy, int w, int h,
                     const u8* mask) {
    const int pairs = w >> 3;
    if (u8* cov = coverageOf(dst))
        for (int r = 0; r < h; ++r)
            if (dy + r >= 0 && dy + r < 200) {
                const int a = std::max(dx & ~3, 0), b = std::min((dx & ~3) + pairs * 8, 320);
                if (b > a) std::memset(cov + (dy + r) * 320 + a, 0, size_t(b - a));
            }
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
        u8* d = surf().row(yy);
        for (int xx = xa; xx <= xb; ++xx) d[xx] = s[xx - x];
        touch(xa, xb, yy);
    }
}

void Gfx::drawImageMasked(const Image& img, const Mask& mask, int x, int y) {
    const int xa = std::max(x, x0_), xb = std::min(x + img.w - 1, x1_);
    const int ya = std::max(y, y0_), yb = std::min(y + img.h - 1, y1_);
    for (int yy = ya; yy <= yb; ++yy) {
        const u8* s = img.row(yy - y);
        u8* d = surf().row(yy);
        for (int xx = xa; xx <= xb; ++xx)
            if (mask.opaque(xx - x, yy - y)) d[xx] = s[xx - x];
        touch(xa, xb, yy);
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
        u8* row = surf().row(y + r);
        for (int i = 0; i < w; ++i)
            if (font_->pixel(c, i, r)) row[x + i] = textFg_;
        touch(x, x + w - 1, y + r);
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
            if (yy < 0 || yy >= surfH()) continue;
            u8* row = surf().row(yy);
            for (int i = 0; i < 4; ++i) {
                const int xx = x + i;
                if (xx >= 0 && xx < surfW() && f.pixel(u8(ch), i, r)) row[xx] = textFg_;
            }
            touch(x, x + 3, yy);
        }
        x += 4;
    }
}

} // namespace st
