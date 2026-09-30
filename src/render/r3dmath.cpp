#include "render/r3dmath.h"

#include "data/exeimage.h"

#include <cstdlib>

namespace st::render {

namespace {

s16 g_sin[721];   // far 5327:000A
s16 g_atan[513];  // far 52E7:0008
bool g_ready = false;

}  // namespace

bool mathInit() {
    if (g_ready) return true;
    const u8* s = exe().at(0x5327, 0x000A);
    const u8* a = exe().at(0x52E7, 0x0008);
    if (!s || !a) {
        logError("r3d: trigonometry tables missing from st.exe");
        return false;
    }
    for (int i = 0; i < 721; ++i) g_sin[i] = rds16(s + 2 * i);
    for (int i = 0; i < 513; ++i) g_atan[i] = rds16(a + 2 * i);
    g_ready = true;
    return true;
}

s16 mathSin(int a) {
    if (a < 720) return g_sin[a];
    if (a < 1440) return g_sin[1440 - a];
    if (a < 2160) return s16(-g_sin[a - 1440]);
    return s16(-g_sin[2880 - a]);
}

s16 mathCos(int a) { return mathSin((a + 720) % kAngleFull); }

int angleWrap(int a) {
    a %= kAngleFull;
    return a < 0 ? a + kAngleFull : a;
}

int mathAtan2(int a, int b) {
    int q = 0;
    a = s16(a);
    b = s16(b);
    if (a < 0) {
        q |= 1;
        a = -a;
    }
    if (b < 0) {
        q |= 2;
        b = -b;
    }
    a &= 0xFFFF;
    b &= 0xFFFF;
    int t;
    if (s16(b) < s16(a)) {
        // 32/16 unsigned division of b << 9 by a.
        const u32 idx = a ? (u32(b) << 9) / u32(a) : 0;
        t = g_atan[idx];
    } else {
        const u32 idx = b ? (u32(a) << 9) / u32(b) : 0;
        t = 720 - g_atan[idx];
    }
    switch (q) {
    case 0: return t;
    case 1: return 1440 - t;
    case 3: return 1440 + t;
    default: return 2879 - t;
    }
}

namespace {

// Reduce two/three 32-bit deltas by whole bytes until every high word is a
// pure sign extension, then by 2 more bits (63D7 / 64FD).
void normalizeDeltas(s32* d, int n) {
    for (;;) {
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            const s16 h = hi16(d[i]);
            if (h != 0 && h != -1) ok = false;
        }
        if (ok) break;
        for (int i = 0; i < n; ++i) d[i] >>= 8;
    }
    for (int i = 0; i < n; ++i) d[i] = s16(lo16(d[i])) >> 2;
}

}  // namespace

int mathHeading(s32 x0, s32 z0, s32 x1, s32 z1) {
    s32 d[2] = {s32(u32(x1) - u32(x0)), s32(u32(z1) - u32(z0))};
    // 63D7 only tests the high words while shifting both by a byte.
    for (;;) {
        const s16 hx = hi16(d[0]), hz = hi16(d[1]);
        if ((hx == 0 || hx == -1) && (hz == 0 || hz == -1)) break;
        d[0] >>= 8;
        d[1] >>= 8;
    }
    const int ax = s16(lo16(d[0])) >> 2, az = s16(lo16(d[1])) >> 2;
    const int t = mathAtan2(ax, az);
    const int h = t - 720;
    return h < 0 ? t + 2160 : h;
}

int mathPitch(s32 ax, s32 ay, s32 az, s32 bx, s32 by, s32 bz) {
    s32 d[3] = {s32(u32(bx) - u32(ax)), s32(u32(by) - u32(ay)), s32(u32(bz) - u32(az))};
    normalizeDeltas(d, 3);
    const s32 dist = mathSqrtLong(d[0] * d[0] + d[2] * d[2]);
    return mathAtan2(hi16(dist), d[1]);
}

s32 mathSqrtLong(s32 v) {
    u32 x = u32(v);
    int n = 0;
    while (s16(x >> 16) >= 4) {
        x >>= 2;
        ++n;
    }
    x <<= 12;
    u32 r = 0;
    for (u32 bit = 0x8000; bit; bit >>= 1) {
        const u32 c = r + bit;
        const u32 sq = c * c;
        const s16 sh = s16(sq >> 16), xh = s16(x >> 16);
        if (sh < xh || (sh == xh && u16(sq) <= u16(x))) r = c;
    }
    u32 res = r << 16;
    const int sh = 6 - n;
    if (sh > 0) res >>= sh;
    else if (sh < 0) res <<= -sh;
    return s32(res);
}

s32 mathFmul(s32 a, s32 b) {
    bool neg = false;
    u32 ua = u32(a), ub = u32(b);
    if (a < 0) {
        ua = u32(0u - u32(a));
        neg = !neg;
    }
    if (b < 0) {
        ub = u32(0u - u32(b));
        neg = !neg;
    }
    const u32 al = ua & 0xFFFF, ah = ua >> 16, bl = ub & 0xFFFF, bh = ub >> 16;
    // The low x low product contributes only its high word.
    const u32 r = ((al * bl) >> 16) + ah * bl + al * bh + ((ah * bh) << 16);
    return neg ? s32(0u - r) : s32(r);
}

s32 mathFdiv(s32 a, s32 b) {
    bool neg = false;
    if (a < 0) {
        a = s32(0u - u32(a));
        neg = !neg;
    }
    if (b < 0) {
        b = s32(0u - u32(b));
        neg = !neg;
    }
    while (hi16(b) != 0) {
        a >>= 1;
        b >>= 1;
    }
    const u16 d = lo16(b);
    u32 res;
    if (u16(hi16(a)) >= d) {
        // Overflow: the quotient becomes 0 with the sign word as high part.
        res = neg ? 0xFFFF0000u : 0u;
    } else {
        const u32 q = u32(a) / d;
        const u32 rem = u32(a) % d;
        const u32 frac = (rem << 16) / d;
        res = (q << 16) | (frac & 0xFFFF);
    }
    return neg ? s32(0u - res) : s32(res);
}

Mat3 mathRotMatrix(int a0, int a1, int a2) {
    const s16 c0 = mathCos(a0), s0 = mathSin(a0);
    const s16 c1 = mathCos(a1), s1 = mathSin(a1);
    const s16 c2 = mathCos(a2), s2 = mathSin(a2);
    Mat3 m;
    m.m[0] = c2; m.m[1] = s2; m.m[2] = 0;
    m.m[3] = s16(-s2); m.m[4] = c2; m.m[5] = 0;
    m.m[6] = 0; m.m[7] = 0; m.m[8] = 0x4000;
    Mat3 rx;
    rx.m[0] = 0x4000; rx.m[1] = 0; rx.m[2] = 0;
    rx.m[3] = 0; rx.m[4] = c1; rx.m[5] = s1;
    rx.m[6] = 0; rx.m[7] = s16(-s1); rx.m[8] = c1;
    mathMatMul(m, rx);
    Mat3 ry;
    ry.m[0] = c0; ry.m[1] = 0; ry.m[2] = s16(-s0);
    ry.m[3] = 0; ry.m[4] = 0x4000; ry.m[5] = 0;
    ry.m[6] = s0; ry.m[7] = 0; ry.m[8] = c0;
    mathMatMul(m, ry);
    return m;
}

void mathMatMul(Mat3& b, const Mat3& a) {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const s32 sum = s32(a.m[3 * i]) * b.m[j] + s32(a.m[3 * i + 1]) * b.m[3 + j] +
                            s32(a.m[3 * i + 2]) * b.m[6 + j];
            r.m[3 * i + j] = hi16(s32(u32(sum) << 2));
        }
    b = r;
}

void mathMatVec(s32 v[3], const Mat3& m) {
    const s32 x = hi16(v[0]), y = hi16(v[1]), z = hi16(v[2]);
    for (int i = 0; i < 3; ++i) {
        const s32 sum = s32(u32(s32(m.m[3 * i]) * x) + u32(s32(m.m[3 * i + 1]) * y) + u32(s32(m.m[3 * i + 2]) * z));
        v[i] = s32(u32(sum) << 2);
    }
}

void mathRotate2d(s16& x, s16& y, s16 cx, s16 cy, int a) {
    if (a < 0) a += kAngleFull;
    if (a >= kAngleFull) a -= kAngleFull;
    const s16 s = s16(mathSin(a) * 2), c = s16(mathCos(a) * 2);
    const s16 dx = s16(x - cx), dy = s16(y - cy);
    auto r = [](s32 p) {  // high word of p << 1, rounded by bit 15 of the low word
        const u32 q = u32(p) << 1;
        return s16((q >> 16) + ((q & 0x8000) ? 1 : 0));
    };
    const s16 nx = s16(r(s32(dx) * c) - r(s32(dy) * s) + cx);
    const s16 ny = s16(r(s32(s) * dx) + r(s32(dy) * c) + cy);
    x = nx;
    y = ny;
}

}  // namespace st::render
