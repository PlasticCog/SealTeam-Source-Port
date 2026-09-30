#include "game/mission/geo.h"

#include "game/mission/exedata.h"

namespace st::game::mission {

namespace {

// Table lookup with the index kept inside the table (the original reads
// outside it for out-of-range angles; callers never pass those).
int sinT(int i) {
    if (i < 0) i = 0;
    if (i > 720) i = 720;
    return gd().sinTable[i];
}

int atanT(int i) {
    if (i < 0) i = 0;
    if (i > 512) i = 512;
    return gd().atanTable[i];
}

// Quadrant selection shared by math_sin and math_cos (2255:6146.. / 61A1..).
int sinQuadrant(int b) {
    if (b < 0x2D0) return sinT(b);
    if (b < 0x5A0) return sinT(0x5A0 - b);
    if (b < 0x870) return -sinT(b - 0x5A0);
    return -sinT(0xB40 - b);
}

// R(p) of math_rotate_2d: high word of (p << 1) plus bit 15 of its low word.
s16 roundProduct(s32 p) {
    const u32 v = u32(p) << 1;
    s16 hi = s16(v >> 16);
    if (v & 0x8000u) hi = s16(hi + 1);
    return hi;
}

// Arithmetic shift of a 32-bit value (the >>8 / >>2 normalisation steps).
s32 sar(s32 v, int n) { return v >> n; }

bool fitsWord(s32 v) {
    const s16 hi = s16(u32(v) >> 16);
    return hi == 0 || hi == -1;
}

} // namespace

int mathSin(int a8) { return sinQuadrant(a8); }

int mathCos(int a8) {
    int b = a8 + 0x2D0;
    if (b >= 0xB40) b -= 0xB40;
    return sinQuadrant(b);
}

s32 mathSinFx(int a8) { return s32(mathSin(a8)) * 4; }
s32 mathCosFx(int a8) { return s32(mathCos(a8)) * 4; }

int angleWrap(int a8) {
    s16 a = s16(a8);
    while (a < 0) a = s16(a + 0xB40);
    while (a >= 0xB40) a = s16(a - 0xB40);
    return a;
}

int mathAtan2(int a, int b) {
    int q = 0;
    s16 x = s16(a), y = s16(b);
    if (x < 0) { q |= 1; x = s16(-x); }
    if (y < 0) { q |= 2; y = s16(-y); }
    int t;
    if (y >= x) {
        // (x << 9) / y, 32/16 unsigned division; 0 when y == 0.
        const u32 num = u32(u16(x)) << 9;
        const int idx = y == 0 ? 0 : int(num / u16(y));
        t = 0x2D0 - atanT(idx);
    } else {
        const u32 num = u32(u16(y)) << 9;
        t = atanT(int(num / u16(x)));
    }
    switch (q) {
    case 0: return t;
    case 1: return 0x5A0 - t;
    case 3: return t + 0x5A0;
    default: return 0xB3F - t;  // quadrant 2: 2879, not 2880 (original)
    }
}

int mathHeading(s32 x0, s32 z0, s32 x1, s32 z1) {
    s32 dx = wrapSub(x1, x0);
    s32 dz = wrapSub(z1, z0);
    while (!fitsWord(dx) || !fitsWord(dz)) {
        dx = sar(dx, 8);
        dz = sar(dz, 8);
    }
    dx = sar(dx, 2);
    dz = sar(dz, 2);
    int h = mathAtan2(s16(dx), s16(dz)) - 0x2D0;
    if (h < 0) h += 0xB40;
    return h;
}

s32 mathSqrtLong(s32 v) {
    u32 x = u32(v);
    int n = 0;
    while (s16(x >> 16) >= 4) {
        x >>= 2;
        ++n;
    }
    x <<= 12;
    u16 r = 0;
    for (u16 bit = 0x8000; bit != 0; bit >>= 1) {
        const u16 trial = u16(r + bit);
        const u32 sq = u32(trial) * u32(trial);
        const s16 sqHi = s16(sq >> 16), xHi = s16(x >> 16);
        // Signed compare of the high words, unsigned of the low words.
        if (sqHi > xHi) continue;
        if (sqHi == xHi && u16(sq) > u16(x)) continue;
        r = trial;
    }
    u32 res = u32(r) << 16;
    const int sh = 6 - n;
    if (sh > 0) res >>= sh;
    else if (sh < 0) res <<= -sh;
    return s32(res);
}

int mathPitch(const Vec3& a, const Vec3& b) {
    s32 dx = wrapSub(b.x, a.x);
    s32 dy = wrapSub(b.y, a.y);
    s32 dz = wrapSub(b.z, a.z);
    while (!fitsWord(dx) || !fitsWord(dy) || !fitsWord(dz)) {
        dx = sar(dx, 8);
        dy = sar(dy, 8);
        dz = sar(dz, 8);
    }
    dx = sar(dx, 2);
    dy = sar(dy, 2);
    dz = sar(dz, 2);
    const s32 sx = s32(s16(dx)) * s32(s16(dx));
    const s32 sz = s32(s16(dz)) * s32(s16(dz));
    const s32 d = s32(u32(mathSqrtLong(wrapAdd(sx, sz))) >> 16);
    return mathAtan2(s16(d), s16(dy));
}

void mathRotate2d(s16& x, s16& y, s16 cx, s16 cy, int a8) {
    int a = s16(a8);
    if (a < 0) a += 0xB40;
    if (a >= 0xB40) a -= 0xB40;
    const s16 s2 = s16(mathSin(a) * 2);
    const s16 c2 = s16(mathCos(a) * 2);
    const s16 dx = s16(x - cx);
    const s16 dy = s16(y - cy);
    const s16 nx = s16(roundProduct(s32(dx) * c2) - roundProduct(s32(s2) * dy) + cx);
    const s16 ny = s16(roundProduct(s32(s2) * dx) + roundProduct(s32(dy) * c2) + cy);
    x = nx;
    y = ny;
}

void posMovePolar(s32 d, int pitch8, int heading8, Vec3& pos) {
    if (d <= 0) return;
    const bool big = d > 0x4000;
    // Small distances use d itself, large ones the middle word (d >> 8).
    const s16 len = big ? s16(u32(d) >> 8) : s16(d);
    const int shift = big ? 8 : 0;
    s16 h = len;
    if (pitch8 != 0) {
        s16 px = len, py = 0;
        mathRotate2d(px, py, 0, 0, pitch8);
        pos.y = wrapAdd(pos.y, s32(py) * (1 << shift));
        h = px;
    }
    s16 hx = 0, hz = h;
    mathRotate2d(hx, hz, 0, 0, heading8);
    pos.x = wrapAdd(pos.x, s32(hx) * (1 << shift));
    pos.z = wrapAdd(pos.z, s32(hz) * (1 << shift));
}

int geoDistance(const Vec3& a, const Vec3& b) {
    s32 dz = wrapSub(b.z, a.z);
    if (dz < 0) dz = wrapSub(0, dz);
    s32 dx = wrapSub(b.x, a.x);
    if (dx < 0) dx = wrapSub(0, dx);
    s32 mn = dz, mx = dx;
    if (dz > dx) { mn = dx; mx = dz; }
    const s32 v = sar(wrapAdd(sar(wrapAdd(mn, wrapAdd(mn, mn)), 3), mx), 8);
    const s16 r = s16(v);
    return r < 0 ? 0x7FFF : r;
}

int geoBearing(const Vec3& a, const Vec3& b) {
    if (&a == &b) return 0;
    return mathHeading(a.x, a.z, b.x, b.z) >> 3;
}

int geoPitch(const Vec3& a, const Vec3& b) { return mathPitch(a, b) >> 3; }

int geoRelativeBearing(int a8, int b8) {
    int v = 0xB4 - (s16(a8) >> 3);
    if (v < 0) v += 0x168;
    v += s16(b8) >> 3;
    if (v > 0x167) v -= 0x168;
    return v;
}

bool geoInFov(const Vec3* a, int halfDeg, int headingDeg, const Vec3* b) {
    if (!a || !b) return false;
    // 16-bit absolute value: -32768 stays negative (and the test then fails).
    s16 half = s16(halfDeg);
    if (half < 0) half = s16(-half);
    if (half > 0xB4) half = 0xB4;
    int d = s16(geoBearing(*a, *b) - headingDeg + 0xB4);
    if (d >= 0x168) d %= 0x168;
    else if (d < 0) d += 0x168;
    return 0xB4 - half <= d && d <= half + 0xB4;
}

int geoMidpoint(const Vec3& a, const Vec3& b, Vec3& out) {
    out.x = wrapAdd(sar(wrapSub(b.x, a.x), 1), a.x);
    out.z = wrapAdd(sar(wrapSub(b.z, a.z), 1), a.z);
    out.y = wrapAdd(sar(wrapSub(b.y, a.y), 1), a.y);
    return geoDistance(a, b);
}

void geoOffsetXz(const Vec3& pos, int d, Vec3& out) {
    const s32 o = s32(s16(d)) * 256;
    out.x = wrapAdd(pos.x, o);
    out.z = wrapAdd(pos.z, o);
}

} // namespace st::game::mission
