// r3d_render_view (2255:3090) and its helpers: frame set-up, object
// gathering and culling, draw order, sky/ground, object drawing (BSP,
// primitives, clipping), projection and the projection cache.
// docs/re/seg_2255.md sections 7-9. Addresses in comments are 2255:xxxx.
//
// Two raster modes share the geometry code:
//  * Original: everything is drawn into the 320x200 gfx draw page with the
//    original integer projection (including its quirks).
//  * Enhanced: the camera-space pipeline is the same, but projection is done
//    in double precision into the page's high-resolution layer (platform/
//    video: a fixed multiple of 320x200 or the window's own pixels, possibly
//    wider than 4:3), the gathering, culling and camera transform use 32/64-bit
//    distances so the draw distance can reach the whole world, and some
//    visual bugs are fixed (see docs/render.md).
#include "render/r3d.h"

#include "core/settings.h"
#include "engine/ticker.h"
#include "gfx/gfx.h"
#include "platform/system.h"
#include "render/model.h"
#include "render/r3dhires.h"
#include "render/world.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace st::render {

using s64 = long long;
using game::Obj3D;
namespace of = game::obj3d_flag;

namespace {

// ============================================================ state

struct ProjCache {
    u32 frame = 0;
    s16 angles[3]{};
    s16 zoom = 0;
    u32 aspect = 0;
    u16 descDs = 0, modelDs = 0;
    int K = 0;
    std::vector<s16> offsets;  // 2 per vertex
    std::vector<u16> prims;    // drawn primitives
};

struct RenderRec {
    u16 model = 0;          // +00 LOD model (DS offset)
    RenderRec* next = nullptr;  // +02
    u8 clip = 0;            // +04
    s8 K = 0;               // +05 (negative for far objects in the Enhanced preset)
    s16 x = 0, y = 0, z = 0;    // +06 +08 +0A
    u32 dist = 0;           // +0C
    ProjCache* cache = nullptr;  // +10
    Obj3D* parent = nullptr;     // +12
    Obj3D* obj = nullptr;        // +14
};

struct ViewEntry {
    Obj3D* obj = nullptr;
    ProjCache* cache = nullptr;
    Obj3D* parent = nullptr;
};

// View record DS:074A.
struct View {
    u32 frame = 0;             // +02
    int subsample = 2;         // +06
    int capacity = 200;        // +08
    std::vector<ViewEntry> list;  // +0A/+0C
    bool persist = false;      // +10
    // +12 clip shift cache
    int csW = -1, csH = -1, csZoom = -1, csShiftX = 0, csShiftY = 0;
    u8 csSideX = 0, csSideY = 0;
    // +1E frustum normal cache
    int fnW = -1, fnH = -1, fnZoom = -1;
    s16 nLrX = 0, nLrZ = 0, nTbY = 0, nTbZ = 0;
    // +34 frustum corner cache
    int fcW = -1, fcH = -1, fcZoom = -1;
    s32 cornerX = 0, cornerY = 0, cornerZ = 0;
    // +46 horizon length cache, +4C horizon term cache
    int hzW = -1, hzH = -1;
    s16 hzLen = 0;
    int htZoom = -1;
    s16 htWorld = 0, htTerm = 0;
    int cacheAge = 25;         // +54
    int cacheRot = 16;         // +56
    bool cacheOn = true;       // +58
    bool valid = false;
};

View g_view;
RenderContext g_ctx;
RenderStats g_stats;
DrawState g_drawState;
PrimCallback g_billboard = nullptr;
std::vector<std::unique_ptr<ProjCache>> g_cachePool;  // owned caches

// Camera / frame (F430..F44F)
s32 g_camX = 0, g_camY = 0, g_camZ = 0;
s16 g_heading = 0, g_pitch = 0, g_roll = 0;
int g_zoom = 8;
bool g_wait = false;
u8 g_status = 0;
bool g_inViewList = false;
bool g_skyOnly = false;

// Clip box in original 320x200 coordinates (gfx_set_clip).
int g_clipX0 = 0, g_clipY0 = 0, g_clipW = 320, g_clipH = 200, g_clipX1 = 319, g_clipY1 = 199;
int g_clipCx = 159, g_clipCy = 99;

// Camera matrices
Mat3 g_camMatPR;          // F464
Mat3 g_camMatInv;         // F484
s16 g_camMatKey[3] = {-1, -1, -1};  // 54EE..54F2
s32 g_corner[4][3]{};     // F496.. world-space frustum corner directions

// Clip plane shifts F614..F619
int g_shiftX = 0, g_shiftY = 0;
bool g_sideX = false, g_sideY = false;

// World boxes F4F2.. (index = size class - 10)
s32 g_wbXmin[10]{}, g_wbXmax[10]{}, g_wbZmin[10]{}, g_wbZmax[10]{};
int g_wbAge = 20;
s16 g_wbAngles[3]{};
s32 g_wbCam[3]{};
int g_wbW = -1, g_wbH = -1, g_wbZoom = -1;

// Aspect (r3d_set_default_aspect: 20:23)
constexpr u32 kAspectBa = (u32(20) << 16) / 23;  // DS:54E4 (0xDE9B)
constexpr u32 kAspectAb = (u32(23) << 16) / 20;  // DS:54E8 (0x12666)

// Object being drawn
bool g_precise = false;      // F612
u8 g_drawClip = 0;           // F642
const LodModel* g_drawModel = nullptr;
bool g_recording = false;    // F643
ProjCache* g_recCache = nullptr;

// Render record storage and the 5000-byte work buffer accounting.
std::vector<RenderRec> g_recs;
int g_recUsed = 0;
int g_workSize = 5000;
int g_recOff = 0;        // byte offset of the next record (grows down)
int g_batchOff = 0;      // byte offset of the batch pointer (grows up)
std::vector<RenderRec*> g_batch;
RenderRec* g_drawList = nullptr;

// High-resolution frame (Enhanced).
HiFrame g_hi;

// ============================================================ integer helpers

inline s32 add32(s32 a, s32 b) { return s32(u32(a) + u32(b)); }
inline s32 sub32(s32 a, s32 b) { return s32(u32(a) - u32(b)); }
inline s32 neg32(s32 a) { return s32(0u - u32(a)); }
inline s32 abs32(s32 a) { return a < 0 ? neg32(a) : a; }
inline s32 shl32(s32 a, int n) { return n >= 32 ? 0 : s32(u32(a) << n); }
inline s32 sar32(s32 a, int n) { return n >= 32 ? (a < 0 ? -1 : 0) : (a >> n); }
// 16-bit value rounded from a 16.16 number: high word + bit 15 of the low word.
inline s16 roundHi(s32 v) { return s16(hi16(v) + ((lo16(v) & 0x8000) ? 1 : 0)); }

bool enhanced() { return !settings().original(); }

// ============================================================ projection (9.1, 9.2)

// Zoom shift of the generated projection code (6E78).
s32 zoomShift(s32 d, int zoom) {
    if (zoom < 6) return shl32(d, zoom);
    // mov dh,dl / mov dl,ah / mov ah,al: << 8 without clearing the low byte.
    u32 u = u32(d);
    u32 dp = (u << 8) | (u & 0xFF);
    s32 v = s32(dp);
    if (zoom < 8) return sar32(v, 8 - zoom);
    return shl32(v, zoom - 8);
}

// 32/16 IDIV; returns false on divide overflow (quotient out of range, divisor 0).
bool idiv32(s32 n, s16 d, s16& q) {
    if (d == 0) return false;
    const long long r = (long long)n / d;
    if (r > 32767 || r < -32768) return false;
    q = s16(r);
    return true;
}

// r3d_div_precise (6F24).
s16 divPrecise(s32 n, s32 d) {
    if (d == 0) {
        if (n > 0) return 0x7F00;
        if (n == 0) return 0;
        return s16(-0x7F00);
    }
    bool neg = false;
    if (n < 0) {
        neg = !neg;
        n = neg32(n);
    }
    if (d < 0) {
        neg = !neg;
        d = neg32(d);
    }
    // 48-bit N = n << zoom (top word T).
    long long N = (long long)u32(n);
    for (int i = 0; i < std::min(std::max(g_zoom, 1), 14); ++i) N <<= 1;
    u32 D = u32(d);
    int e = 0;
    auto T = [&]() { return s16(u16(N >> 32)); };
    auto Dh = [&]() { return s16(u16(D >> 16)); };
    while (T() >= Dh()) {
        if (Dh() < 0x4000) D <<= 1;
        else N >>= 1;
        ++e;
    }
    while (T() < 0x4000 && Dh() < 0x4000) {
        N <<= 1;
        D <<= 1;
    }
    s16 q;
    const s32 num = s32(u32(N >> 16));
    if (!idiv32(num, Dh(), q)) q = s16(u16(N >> 16));  // skipped IDIV: AX keeps bits 16..31
    for (int i = 0; i < e; ++i) {
        const s32 w = s32(q) * 2;
        if (w > 32767 || w < -32768) {
            q = 0x7F00;
            break;
        }
        q = s16(w);
    }
    return neg ? s16(-q) : q;
}

// r3d_project_precise (7047): full 32-bit coordinates.
void projectPrecise(s32 x, s32 y, s32 z, s16& sx, s16& sy) {
    int t = g_clipCx + divPrecise(x, z);
    sx = (t > 32767 || t < -32768) ? s16(0x7F00) : s16(t);
    t = g_clipCy - divPrecise(y, z);
    sy = (t > 32767 || t < -32768) ? s16(0x7F00) : s16(t);
}

// r3d_project_vert (6D0A, generated): high words only, divide-overflow
// fix-ups of the INT 0 handler (6C7D/6CD3).
void projectNormal(s32 x32, s32 y32, s32 z32, s16& sx, s16& sy) {
    const s16 x = hi16(x32), y = hi16(y32), z = hi16(z32);
    s16 q;
    int t;
    // y division
    if (idiv32(zoomShift(y, g_zoom), z, q)) {
        t = g_clipCy - q;
    } else {
        s16 f = 0x7F00;
        if (y < 0) f = s16(-f);
        if (z < 0) f = s16(-f);
        t = g_clipCy - f;
    }
    sy = (t > 32767 || t < -32768) ? s16(0x7F00) : s16(t);
    // x division
    if (idiv32(zoomShift(x, g_zoom), z, q)) {
        t = g_clipCx + q;
    } else if (z != 0) {
        s16 f = 0x7F00;
        if (x < 0) f = s16(-f);
        if (z < 0) f = s16(-f);
        t = g_clipCx + f;
    } else {
        sx = x > 0 ? 10000 : x == 0 ? s16(g_clipCx) : s16(-10000);
        sy = y > 0 ? 10000 : y == 0 ? s16(g_clipCy) : s16(-10000);
        return;
    }
    sx = (t > 32767 || t < -32768) ? s16(0x7F00) : s16(t);
}

void projectVert(s32 x, s32 y, s32 z, s16& sx, s16& sy) {
    if (g_hi.on) {
        g_hi.project(x, y, z, sx, sy);
        return;
    }
    if (g_precise) projectPrecise(x, y, z, sx, sy);
    else projectNormal(x, y, z, sx, sy);
}

// ============================================================ frame set-up (8.1)

void setClip(int x, int y, int w, int h) {
    g_clipX0 = x;
    g_clipY0 = y;
    g_clipW = w;
    g_clipH = h;
    g_clipX1 = x + w - 1;
    g_clipY1 = y + h - 1;
    g_clipCx = int(u16(g_clipX0 + g_clipX1) >> 1);
    g_clipCy = int(u16(g_clipY0 + g_clipY1) >> 1);
}

// r3d_frustum_normals (4D9C)
void frustumNormals() {
    View& v = g_view;
    if (v.fnZoom == g_zoom && v.fnW == g_clipW && v.fnH == g_clipH) return;
    const s32 f = s32(1) << (g_zoom & 0x1F);
    auto pair = [&](int half, s16& nf, s16& nh) {
        const s32 len = mathSqrtLong(s32(half) * half + f * f);
        nf = s16(lo16(mathFdiv(s32(u32(f) << 16), len) >> 2));
        nh = s16(lo16(mathFdiv(s32(u32(half) << 16), len) >> 2));
    };
    pair(g_clipW >> 1, v.nLrX, v.nLrZ);
    pair(g_clipH >> 1, v.nTbY, v.nTbZ);
    v.fnZoom = g_zoom;
    v.fnW = g_clipW;
    v.fnH = g_clipH;
}

// r3d_camera_matrix (56C4) with r3d_view_matrices (993D).
void cameraMatrix() {
    if (g_camMatKey[0] == g_heading && g_camMatKey[1] == g_pitch && g_camMatKey[2] == g_roll) return;
    g_camMatKey[0] = g_heading;
    g_camMatKey[1] = g_pitch;
    g_camMatKey[2] = g_roll;
    auto neg = [](int a) {
        int n = -a;
        if (n < 0) n += kAngleFull;
        return n;
    };
    const int ah = neg(g_heading), ap = neg(g_pitch), ar = neg(g_roll);
    const s16 hc = mathCos(ah), hs = mathSin(ah);
    const s16 pc = mathCos(ap), ps = mathSin(ap);
    const s16 rc = mathCos(ar), rs = mathSin(ar);
    auto hi2 = [](s32 sum) { return hi16(s32(u32(sum) << 2)); };
    const s16 t1 = mul14(ps, rc), t2 = mul14(ps, rs);
    Mat3 A, B;
    B.m[8] = pc;
    A.m[7] = B.m[7] = s16(-ps);
    B.m[0] = rc;
    B.m[3] = s16(-rs);
    B.m[6] = 0;
    B.m[4] = A.m[4] = mul14(pc, rc);
    B.m[1] = A.m[1] = mul14(pc, rs);
    B.m[5] = t1;
    A.m[3] = hi2(s32(u32(s32(t1) * hs) - u32(s32(hc) * rs)));
    A.m[5] = hi2(s32(u32(s32(hs) * rs) + u32(s32(t1) * hc)));
    B.m[2] = t2;
    A.m[0] = hi2(s32(u32(s32(hc) * rc) + u32(s32(t2) * hs)));
    A.m[2] = hi2(s32(u32(s32(t2) * hc) - u32(s32(hs) * rc)));
    A.m[6] = mul14(hs, pc);
    A.m[8] = mul14(hc, pc);
    // F484 = transpose (before the aspect scaling)
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) g_camMatInv.m[3 * i + j] = A.m[3 * j + i];
    // Pixel aspect: the y rows of F452 and F464 are scaled by 20/23.
    const s32 f = s32((kAspectBa & 0x1FFFF) >> 1);
    auto sc = [&](s16 e) { return s16(lo16(s32(u32(s32(s16(e >> 1)) * f) >> 14))); };
    for (int j = 0; j < 3; ++j) {
        A.m[3 + j] = sc(A.m[3 + j]);
        B.m[3 + j] = sc(B.m[3 + j]);
    }
    xform().cam = A;
    g_camMatPR = B;
}

// r3d_build_world_boxes (8332)
void buildWorldBoxes() {
    auto minmax = [&](int comp, s32& mn, s32& mx) {
        mn = 0;
        mx = 0;
        for (int k = 0; k < 4; ++k) {
            mn = std::min(mn, g_corner[k][comp]);
            mx = std::max(mx, g_corner[k][comp]);
        }
        mn >>= 8;
        mx >>= 8;
    };
    s32 xmn, xmx, zmn, zmx;
    minmax(0, xmn, xmx);
    minmax(2, zmn, zmx);
    for (int c = 0; c < 10; ++c) {
        g_wbXmin[c] = add32(g_camX, shl32(xmn, c));
        g_wbXmax[c] = add32(g_camX, shl32(xmx, c));
        g_wbZmin[c] = add32(g_camZ, shl32(zmn, c));
        g_wbZmax[c] = add32(g_camZ, shl32(zmx, c));
    }
}

// r3d_update_world_boxes (8599)
void updateWorldBoxes() {
    auto rebuild = [&]() {
        buildWorldBoxes();
        g_wbW = g_clipW;
        g_wbH = g_clipH;
        g_wbZoom = g_zoom;
        g_wbCam[0] = g_camX;
        g_wbCam[1] = g_camY;
        g_wbCam[2] = g_camZ;
        g_wbAngles[0] = g_heading;
        g_wbAngles[1] = g_pitch;
        g_wbAngles[2] = g_roll;
        g_wbAge = 0;
    };
    if (g_wbAge > 19) return rebuild();
    const s16 cur[3] = {g_heading, g_pitch, g_roll};
    for (int i = 0; i < 3; ++i) {
        int d = g_wbAngles[i] - cur[i];
        if (g_wbAngles[i] < cur[i]) d = -d;
        if (s16(d) > 0x77) return rebuild();
    }
    if (g_clipW != g_wbW || g_clipH != g_wbH || g_zoom != g_wbZoom) return rebuild();
    const s32 dx = sub32(g_camX, g_wbCam[0]);
    if (dx <= -0x6600 || dx >= 0x6600) {
        for (int c = 0; c < 10; ++c) {
            g_wbXmin[c] = add32(g_wbXmin[c], dx);
            g_wbXmax[c] = add32(g_wbXmax[c], dx);
        }
        g_wbCam[0] = g_camX;
    }
    const s32 dz = sub32(g_camZ, g_wbCam[2]);
    if (dz <= -0x6600 || dz >= 0x6600) {
        for (int c = 0; c < 10; ++c) {
            g_wbZmin[c] = add32(g_wbZmin[c], dz);
            g_wbZmax[c] = add32(g_wbZmax[c], dz);
        }
        g_wbCam[2] = g_camZ;
    }
    ++g_wbAge;
}

// r3d_frustum_corners (5838)
void frustumCorners() {
    View& v = g_view;
    if (v.fcW != g_clipW || v.fcH != g_clipH || v.fcZoom != g_zoom) {
        v.fcW = g_clipW;
        v.fcH = g_clipH;
        v.fcZoom = g_zoom;
        const int sh = 9 - g_zoom;
        auto shv = [&](int x) {
            u16 u = u16(x);
            if (sh > 0) u = u16(u << sh);
            else if (sh < 0) u = u16(u >> -sh);
            return u;
        };
        v.cornerX = mk32(s16(shv(g_clipW)), 0);
        v.cornerY = mk32(s16(shv(g_clipH)), 0);
        v.cornerZ = mk32(0x400, 0);
        v.cornerY = mathFmul(s32(kAspectAb), v.cornerY);  // g_aspect_y
    }
    const s32 X = v.cornerX, Y = v.cornerY, Z = v.cornerZ;
    const s32 c[4][3] = {{neg32(X), Y, Z}, {X, Y, Z}, {neg32(X), neg32(Y), Z}, {X, neg32(Y), Z}};
    for (int k = 0; k < 4; ++k) {
        for (int i = 0; i < 3; ++i) g_corner[k][i] = c[k][i];
        mathMatVec(g_corner[k], g_camMatInv);
    }
    g_skyOnly = hi16(g_corner[0][1]) > 10 && hi16(g_corner[1][1]) > 10 && hi16(g_corner[2][1]) > 10 &&
                hi16(g_corner[3][1]) > 10;
    updateWorldBoxes();
}

// r3d_pow2_ratio (300C): integer part `ip`, fraction `fr` of a 16.16 slope.
void pow2Ratio(int& shift, bool& side, u16 fr, s16 ip) {
    if (ip < 1) {
        side = true;
        if (ip == 0 && fr == 0) {
            shift = 16;
            return;
        }
        shift = 0;
        if (ip < 2) {
            while (ip < 1) {
                ++shift;
                const bool c = (fr & 0x8000) != 0;
                fr = u16(fr << 1);
                ip = s16((ip << 1) | (c ? 1 : 0));
            }
        }
        if (fr != 0) --shift;
    } else {
        side = false;
        shift = 14;
        if (ip < 0x4001) {
            while (ip < 0x4000) {
                --shift;
                const bool c = (fr & 0x8000) != 0;
                fr = u16(fr << 1);
                ip = s16((ip << 1) | (c ? 1 : 0));
            }
        }
        if ((u16(ip) & 0xBFFF) != 0 || fr != 0) ++shift;
    }
}

// r3d_clip_shifts (2F40)
void clipShifts() {
    View& v = g_view;
    if (v.csW == g_clipW && v.csH == g_clipH && v.csZoom == g_zoom) {
        g_sideX = v.csSideX;
        g_sideY = v.csSideY;
        g_shiftX = v.csShiftX;
        g_shiftY = v.csShiftY;
        return;
    }
    auto slope = [&](int half, int& shift, bool& side) {
        u16 ip = u16(half), fr = 0;
        for (int i = 0; i < g_zoom; ++i) {
            const bool c = ip & 1;
            ip = u16(s16(ip) >> 1);
            fr = u16((fr >> 1) | (c ? 0x8000 : 0));
        }
        pow2Ratio(shift, side, fr, s16(ip));
    };
    slope(g_clipW / 2, g_shiftX, g_sideX);
    slope(g_clipH / 2, g_shiftY, g_sideY);
    v.csW = g_clipW;
    v.csH = g_clipH;
    v.csZoom = g_zoom;
    v.csSideX = g_sideX;
    v.csSideY = g_sideY;
    v.csShiftX = g_shiftX;
    v.csShiftY = g_shiftY;
}

// ============================================================ gathering (8.2-8.4)

// Enhanced draw distance: P percent of the original, kDrawDistanceMax = the
// whole world.
int distancePct() { return settings().effectiveDrawDistancePct(); }
// Raise of the world-box size class so the view pyramid boxes reach P/100
// times deeper (class c covers depth 2^(c+8) world units); 10 = never culled.
int sizeClassBonus() {
    const int p = distancePct();
    if (p == kDrawDistanceMax) return 10;
    int b = 0;
    while ((100 << b) < p) ++b;
    return b;
}
int floorLog2(s64 v) {
    int lg = 0;
    while ((v >> (lg + 1)) != 0) ++lg;
    return lg;
}

// r3d_cull_world_box (82AC): true if the object's box overlaps the view pyramid box.
bool cullWorldBox(const Obj3D* o, const game::ModelDesc* d) {
    int c = d->size_class;
    if (c >= 20) return true;
    if (c < 10) c = 10;
    c += sizeClassBonus();
    if (c >= 20) return true;
    c -= 10;
    const s32 r = d->radius_world;
    if (add32(o->pos.x, r) < g_wbXmin[c]) return false;
    if (sub32(o->pos.x, r) > g_wbXmax[c]) return false;
    if (add32(o->pos.z, r) < g_wbZmin[c]) return false;
    if (sub32(o->pos.z, r) > g_wbZmax[c]) return false;
    return true;
}

// r3d_obj_to_camera (506B)
void objToCamera(const Obj3D* o, RenderRec& rec) {
    const game::ModelDesc* d = o->model;
    const int s = d->scale_shift;
    s32 r[3] = {sub32(o->pos.x, g_camX), sub32(o->pos.y, g_camY), sub32(o->pos.z, g_camZ)};
    for (s32& v : r) {
        if (s > 0) v = sar32(v, s);
        else if (s < 0) v = shl32(v, -s);
        v = shl32(v, 8);
    }
    auto a16 = [](s16 v) { return s16(v < 0 ? -v : v); };
    const s16 m = s16(s16(a16(hi16(r[2])) | a16(hi16(r[0])) | a16(hi16(r[1]))) + d->radius + 1);
    int K = 0;
    if (m < 0x2000) {
        K = 1;
        if (m < 0x1000) {
            K = 2;
            if (m < 0x800) {
                K = 3;
                if (m < 0x400) {
                    K = 4;
                    if (m < 0x200) {
                        K = 5;
                        if (m < 0x100) {
                            K = 6;
                            if (m < 0x80) {
                                const s8 cl = s8(m);
                                K = 7;
                                if (cl < 0x40) {
                                    K = 8;
                                    if (cl < 0x20) {
                                        K = 9;
                                        if (cl < 0x10) {
                                            K = 10;
                                            if (cl < 8) {
                                                K = 11;
                                                if (cl < 4) {
                                                    K = 12;
                                                    if (cl < 2) K = 13;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    rec.K = s8(K);
    for (s32& v : r) v = shl32(v, K);
    mathMatVec(r, xform().cam);
    rec.x = hi16(r[0]);
    rec.y = hi16(r[1]);
    rec.z = hi16(r[2]);
}

// The same in 64-bit for the Enhanced preset: the magnitude test cannot wrap
// and K becomes negative for objects farther than 2^14 model units, so the
// camera-space centre always fits the 14 bits the rest of the pipeline
// expects (camera units are then 2^-K model units).
void objToCameraWide(const Obj3D* o, RenderRec& rec) {
    const game::ModelDesc* d = o->model;
    const int s = d->scale_shift;
    s64 r[3] = {sub32(o->pos.x, g_camX), sub32(o->pos.y, g_camY), sub32(o->pos.z, g_camZ)};
    s64 m = 0;
    for (s64& v : r) {
        v = (8 - s) >= 0 ? (v << (8 - s)) : (v >> (s - 8));  // 16.16 model units
        const s64 h = v >> 16;
        m |= h < 0 ? -h : h;
    }
    m += d->radius + 1;
    int K = 13 - floorLog2(std::max<s64>(m, 1));
    if (K > 13) K = 13;
    rec.K = s8(K);
    s32 r32[3];
    for (int i = 0; i < 3; ++i) r32[i] = s32(K >= 0 ? (r[i] << K) : (r[i] >> -K));
    mathMatVec(r32, xform().cam);
    rec.x = hi16(r32[0]);
    rec.y = hi16(r32[1]);
    rec.z = hi16(r32[2]);
}

// r3d_cull_sphere (4E9C)
bool cullSphere(RenderRec& rec, const game::ModelDesc* d) {
    const View& v = g_view;
    if (s16(s16(rec.z >> rec.K) + d->radius) < 0) {
        ++g_stats.out;
        return false;
    }
    const s16 rx = d->radius_x, ry = d->radius_y;
    s32 p1 = s32(rec.x) * v.nLrX, p2 = s32(rec.y) * v.nTbY;
    s32 p3 = s32(rec.z) * v.nLrZ, p4 = s32(rec.z) * v.nTbZ;
    const int k = int(rec.K) - 2;
    auto shf = [&](s32 p) { return k >= 0 ? sar32(p, k) : shl32(p, -k); };
    const s16 h1 = hi16(shf(p1)), h2 = hi16(shf(p2)), h3 = hi16(shf(p3)), h4 = hi16(shf(p4));
    const s16 L = s16(h3 + h1), Rt = s16(h3 - h1), Bt = s16(h4 + h2), T = s16(h4 - h2);
    if (T < -ry || Bt < -ry || L < -rx || Rt < -rx) {
        ++g_stats.out;
        return false;
    }
    if (T > ry && Bt > ry && L > rx && Rt > rx) {
        ++g_stats.in;
        rec.clip = 0;
        return true;
    }
    ++g_stats.clipped;
    rec.clip = u8((T <= ry ? 1 : 0) | (Bt <= ry ? 2 : 0) | (L <= rx ? 4 : 0) | (Rt <= rx ? 8 : 0));
    return true;
}

// r3d_cull_sphere without the 16-bit wrap-around (Enhanced, K may be negative).
bool cullSphereWide(RenderRec& rec, const game::ModelDesc* d) {
    const View& v = g_view;
    const int K = rec.K;
    auto shr = [](s64 p, int k) { return k >= 0 ? (p >> k) : (p << -k); };
    if (shr(rec.z, K) + d->radius < 0) {
        ++g_stats.out;
        return false;
    }
    const s64 rx = d->radius_x, ry = d->radius_y;
    const s64 h1 = shr(s64(rec.x) * v.nLrX, K - 2) >> 16, h2 = shr(s64(rec.y) * v.nTbY, K - 2) >> 16;
    const s64 h3 = shr(s64(rec.z) * v.nLrZ, K - 2) >> 16, h4 = shr(s64(rec.z) * v.nTbZ, K - 2) >> 16;
    const s64 L = h3 + h1, Rt = h3 - h1, Bt = h4 + h2, T = h4 - h2;
    if (T < -ry || Bt < -ry || L < -rx || Rt < -rx) {
        ++g_stats.out;
        return false;
    }
    if (T > ry && Bt > ry && L > rx && Rt > rx) {
        ++g_stats.in;
        rec.clip = 0;
        return true;
    }
    ++g_stats.clipped;
    rec.clip = u8((T <= ry ? 1 : 0) | (Bt <= ry ? 2 : 0) | (L <= rx ? 4 : 0) | (Rt <= rx ? 8 : 0));
    return true;
}

// r3d_obj_visible (53BB)
bool objVisible(Obj3D* o, RenderRec& rec) {
    if (!(o->flags & of::kEnabled)) return false;
    if (o->flags & of::kCullSkip) {
        o->flags &= u16(~of::kCullSkip);
        return false;
    }
    if (g_skyOnly && (o->flags & of::kSkyHide)) return false;
    const game::ModelDesc* d = o->model;
    if (!d) return false;
    const u32 dist =
        u32(abs32(sub32(o->pos.x, g_camX))) + u32(abs32(sub32(o->pos.y, g_camY))) + u32(abs32(sub32(o->pos.z, g_camZ)));
    rec.dist = dist;
    const bool enh = enhanced();
    // D never exceeds 16 bits (a world is 24000 game units square).
    const u32 D = (dist >> 16) + 1;
    auto thr = [&](int k) -> u32 {
        const u32 t = d->lod_distance[k];
        if (!enh || t == 0) return t;
        const int p = distancePct();
        if (p == kDrawDistanceMax) return 0xFFFFFFFFu;
        return u32(std::min<u64>(u64(t) * u64(p) / 100, 0xFFFFFFFFu));
    };
    int lod = 2 - (thr(0) < D) - (thr(1) < D) - (thr(2) < D);
    bool far = lod < 0;
    if (!far && !g_inViewList && !cullWorldBox(o, d)) far = true;
    if (far) {
        ++g_stats.far;
        if (!g_view.persist) o->flags |= of::kCullSkip;
        return false;
    }
    if (enh) objToCameraWide(o, rec);
    else objToCamera(o, rec);
    // rec.parent: no group objects exist in the game
    if (o->flags & of::kClipSkip) {
        o->flags &= u16(~of::kClipSkip);
        rec.clip = 0x0F;
    } else {
        if (!(enh ? cullSphereWide(rec, d) : cullSphere(rec, d))) return false;
        if (rec.clip != 0 && !g_view.persist) o->flags |= of::kClipSkip;
    }
    rec.model = d->lod_model[lod];
    return true;
}

// r3d_draw_order_cmp (5553): < 0 if p is drawn before q.
int drawOrderCmp(const RenderRec* p, const RenderRec* q) {
    const u8 lp = p->obj->model->layer, lq = q->obj->model->layer;
    if (lp < lq) return -1;
    if (lp > lq) return 1;
    if (lp < 0x80) return 0;
    const s16 ph = s16(p->dist >> 16), qh = s16(q->dist >> 16);
    if (qh < ph) return -1;
    if (qh == ph) {
        const u16 pl = u16(p->dist), ql = u16(q->dist);
        if (ql < pl) return -1;
        if (ql == pl) return 0;
    }
    return 1;
}

// r3d_quicksort (AE48): Hoare partition, first element pivot.
void quicksort(RenderRec** lo, RenderRec** hi) {
    while (lo < hi) {
        RenderRec** i = lo;
        RenderRec** j = hi + 1;
        for (;;) {
            ++i;
            while (i < hi && drawOrderCmp(*i, *lo) <= 0) ++i;
            --j;
            while (j > lo && drawOrderCmp(*j, *lo) >= 0) --j;
            if (i < j) {
                std::swap(*i, *j);
                continue;
            }
            break;
        }
        std::swap(*lo, *j);
        if ((j - lo) < (hi - j)) {
            quicksort(lo, j - 1);
            lo = j + 1;
        } else {
            quicksort(j + 1, hi);
            hi = j - 1;
        }
    }
}

// r3d_sort_batch (ADFA)
void sortBatch(std::vector<RenderRec*>& a) {
    const int n = int(a.size());
    if (n < 2) return;
    bool ordered = true;
    for (int i = 0; i + 1 < n; ++i)
        if (drawOrderCmp(a[size_t(i)], a[size_t(i + 1)]) > 0) {
            ordered = false;
            break;
        }
    if (!ordered) quicksort(&a[0], &a[size_t(n - 1)]);
}

// r3d_merge_draw_list (2E96)
void mergeBatch(const std::vector<RenderRec*>& a) {
    if (a.empty()) return;
    if (!g_drawList) {
        RenderRec* tail = nullptr;
        for (RenderRec* r : a) {
            if (!tail) g_drawList = r;
            else tail->next = r;
            tail = r;
        }
        tail->next = nullptr;
        return;
    }
    RenderRec* prev = nullptr;
    RenderRec* cur = g_drawList;
    size_t k = 0;
    while (k < a.size()) {
        RenderRec* e = a[k];
        if (!cur || drawOrderCmp(e, cur) < 1) {
            if (!prev) g_drawList = e;
            else prev->next = e;
            e->next = cur;
            prev = e;
            ++k;
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
}

RenderRec* newRec() {
    // The pool is sized once per frame (see the g_recUsed reset): growing it
    // here would reallocate under the raw RenderRec* pointers held by the
    // batch and the view list and crash the sort (seen on turns and bursts).
    if (g_recUsed >= int(g_recs.size())) fatal("renderer: record pool exhausted (%d)", g_recUsed);
    RenderRec* r = &g_recs[size_t(g_recUsed)];
    *r = RenderRec{};
    return r;
}
// Commit a record to the batch with the work buffer accounting of the
// original (records grow down from the end, pointers up from the start).
// `slack` is 26 in r3d_gather_list and 4+22 in the view-list passes.
void commitRec(RenderRec* r, bool gatherPass) {
    ++g_recUsed;
    g_batch.push_back(r);
    const int oldBatch = g_batchOff;
    g_batchOff += 2;
    g_recOff -= 0x16;
    if (gatherPass) {
        if (g_recOff <= oldBatch + 26) g_status = 1;
    } else {
        if (g_recOff - 0x16 <= oldBatch + 4) g_status = 1;
    }
}

void flushBatch(bool sort = true) {
    if (!g_batch.empty()) {
        if (sort) sortBatch(g_batch);
        mergeBatch(g_batch);
    }
    g_batch.clear();
    g_batchOff = 0;
}

// r3d_gather_list (55A8)
void gatherList(Obj3D* list, int skip, int stride) {
    if (!list) return;
    for (int n = skip; n > 0; --n) {
        list = list->next;
        if (!list) return;
    }
    for (;;) {
        if (!(list->flags & of::kInViewList)) {
            ++g_stats.tested;
            RenderRec* r = newRec();
            r->obj = list;
            if (objVisible(list, *r)) {
                list->flags |= of::kInViewList;
                r->cache = nullptr;
                commitRec(r, true);
                if (g_status) return;
            }
        }
        for (int n = stride; n > 0; --n) {
            list = list->next;
            if (!list) return;
        }
    }
}

// ============================================================ primitives (8.6, 8.7)

VertexSlot& slot(int i) { return vertexSlots()[i & (kVertexSlots - 1)]; }

void projectSlotCached(VertexSlot& v) {
    if (v.sx == 0x7FFE) projectVert(v.x, v.y, v.z, v.sx, v.sy);
}

// r3d_face_side (959A) + r3d_face_dot (9662)
u8 faceSide(u16 f) {
    VertexSlot& P0 = slot(ds8(u16(f + 7)));
    VertexSlot& P1 = slot(ds8(u16(f + 8)));
    VertexSlot& P2 = slot(ds8(u16(f + 9)));
    auto sub = [](s16 a, s16 b, bool& ovf) {
        const int r = int(a) - int(b);
        if (r > 32767 || r < -32768) ovf = true;
        return s16(r);
    };
    s32 n[3];
    bool ovf = false;
    auto compute = [&](bool half) {
        auto c = [&](const VertexSlot& v, int k) {
            const s16 h = hi16(k == 0 ? v.x : k == 1 ? v.y : v.z);
            return half ? s16(h >> 1) : h;
        };
        auto d = [&](const VertexSlot& a, const VertexSlot& b, int k) { return sub(c(a, k), c(b, k), ovf); };
        const s32 e1z = d(P0, P1, 2), e1y = d(P0, P1, 1), e1x = d(P0, P1, 0);
        const s32 e2z = d(P2, P1, 2), e2y = d(P2, P1, 1), e2x = d(P2, P1, 0);
        n[0] = s32(u32(e1y * e2z) - u32(e1z * e2y));
        n[1] = s32(u32(e1z * e2x) - u32(e1x * e2z));
        n[2] = s32(u32(e1x * e2y) - u32(e1y * e2x));
    };
    compute(false);
    if (ovf) compute(true);
    auto fits = [](s32 v) { const s16 h = hi16(v); return h == 0 || h == -1; };
    while (!(fits(n[0]) && fits(n[1]) && fits(n[2])))
        for (s32& v : n) v >>= 4;
    for (s32& v : n) v >>= 2;
    const u32 dot = u32(s32(s16(n[0])) * hi16(P1.x)) + u32(s32(s16(n[1])) * hi16(P1.y)) +
                    u32(s32(s16(n[2])) * hi16(P1.z));
    const u8 r = s32(dot) < 0 ? 0x18 : 0x10;
    if (ds8(u16(f + 5)) == 0) dsW8(u16(f + 5), r);
    return ds8(u16(f + 5));
}

// r3d_prim_visible (78E8)
bool primVisible(u16 p) {
    const u8 mode = ds8(p) & 0x18;
    if (mode == 0) return true;
    if (mode == 8) return false;
    const u16 f = ds16(u16(p + 1));
    u8 c = ds8(u16(f + 5));
    if (c == 0) c = faceSide(f);
    return c == mode;
}

// ---- clip ring (DS:5926): 16-byte vertices
struct ClipV {
    s32 x = 0, y = 0, z = 0;
    int src = -1;     // +0C source vertex slot index
    bool isNew = false;  // +0E
};
using ClipFn = void (*)(const ClipV& a, const ClipV& b, ClipV& out);

// Rounded high words of both endpoints (clip_*_div).
struct R6 {
    s16 x1, y1, z1, x2, y2, z2;
};
R6 rounded(const ClipV& a, const ClipV& b) {
    return {roundHi(a.x), roundHi(a.y), roundHi(a.z), roundHi(b.x), roundHi(b.y), roundHi(b.z)};
}

// Shared t computation: num = d1 << 12, den (32-bit), normalised to 15
// bits, 32/16 IDIV retried with a halved numerator on overflow.
s16 clipT(s32 d1, s32 den) {
    s32 num = shl32(d1, 12);
    bool neg = false;
    if (den < 0) {
        neg = true;
        den = neg32(den);
    }
    while (hi16(den) != 0 || s16(lo16(den)) < 0) {
        num >>= 1;
        den >>= 1;
    }
    s16 d = s16(lo16(den));
    if (neg) d = s16(-d);
    s16 q;
    int guard = 0;
    while (!idiv32(num, d, q)) {
        num >>= 1;
        if (++guard > 40) {
            q = 0;
            break;
        }
    }
    return q;
}
s32 lerp12(s16 base, s16 t, s16 delta) { return add32(shl32(s32(t) * delta, 4), mk32(base, 0)); }

// clip_top_div (7954): plane y = z.
void clipTopDiv(const ClipV& a, const ClipV& b, ClipV& o) {
    const bool degenerate = (neg32(a.z) == a.y) && (neg32(b.z) == b.y);
    const R6 r = rounded(a, b);
    const s32 d1 = s32(r.z1) - r.y1;
    const s32 den = sub32(d1, s32(r.z2) - r.y2);
    if (den == 0) {
        const bool pick1 = hi16(a.y) > hi16(a.z) ||
                           (hi16(a.y) == hi16(a.z) && s16(lo16(a.y)) > s16(lo16(a.z)) && r.y1 <= r.z1);
        o = pick1 ? a : b;
    } else {
        const s16 t = clipT(d1, den);
        o.x = lerp12(r.x1, t, s16(r.x2 - r.x1));
        o.y = o.z = lerp12(r.y1, t, s16(r.y2 - r.y1));
    }
    o.isNew = true;
    if (degenerate) o.y = o.z = 0;
}

// clip_bottom_div (7B59): plane y = -z.
void clipBottomDiv(const ClipV& a, const ClipV& b, ClipV& o) {
    const bool degenerate = (a.y == a.z) && (b.y == b.z);
    const R6 r = rounded(a, b);
    const s32 d1 = s32(r.y1) + r.z1;
    const s32 den = sub32(d1, s32(r.y2) + r.z2);
    if (den == 0) {
        const s32 ny = neg32(a.y);
        const bool pick1 = hi16(ny) > hi16(a.z) ||
                           (hi16(ny) == hi16(a.z) && s16(lo16(ny)) > s16(lo16(a.z)) && s16(-r.y1) <= r.z1);
        o = pick1 ? a : b;
    } else {
        const s16 t = clipT(d1, den);
        o.x = lerp12(r.x1, t, s16(r.x2 - r.x1));
        o.y = lerp12(r.y1, t, s16(r.y2 - r.y1));
        o.z = neg32(o.y);
    }
    o.isNew = true;
    if (degenerate) o.y = o.z = 0;
}

// clip_left_div (7D60): plane x = -z.
void clipLeftDiv(const ClipV& a, const ClipV& b, ClipV& o) {
    const R6 r = rounded(a, b);
    const s32 d1 = s32(r.x1) + r.z1;
    const s32 den = sub32(d1, s32(r.x2) + r.z2);
    if (den == 0) {
        const s32 nx = neg32(a.x);
        const bool pick1 = hi16(nx) > hi16(a.z) ||
                           (hi16(nx) == hi16(a.z) && s16(lo16(nx)) > s16(lo16(a.z)) && s16(-r.x1) <= r.z1);
        o = pick1 ? a : b;
    } else {
        const s16 t = clipT(d1, den);
        o.z = lerp12(r.z1, t, s16(r.z2 - r.z1));
        o.x = neg32(o.z);
        o.y = lerp12(r.y1, t, s16(r.y2 - r.y1));
    }
    o.isNew = true;
}

// clip_right_div (7F18, never called by the original): plane x = z.
void clipRightDiv(const ClipV& a, const ClipV& b, ClipV& o) {
    const R6 r = rounded(a, b);
    const s32 d1 = s32(r.z1) - r.x1;
    const s32 den = sub32(d1, s32(r.z2) - r.x2);
    if (den == 0) {
        o = (a.x > a.z) ? a : b;
    } else {
        const s16 t = clipT(d1, den);
        o.z = lerp12(r.z1, t, s16(r.z2 - r.z1));
        o.x = o.z;
        o.y = lerp12(r.y1, t, s16(r.y2 - r.y1));
    }
    o.isNew = true;
}

// Midpoint of two 32-bit values (clip_*_bisect): (a+b)>>1 unless the sum
// overflows, then (a>>1)+(b>>1).
s32 mid32(s32 a, s32 b) {
    const long long s = (long long)a + b;
    if (s > 0x7FFFFFFFLL || s < -0x80000000LL) return add32(a >> 1, b >> 1);
    return s32(s >> 1);
}

// Bisection toward the plane `dist(v) = 0` with dist >= 0 inside. `fix`
// projects the final point onto the plane if it is still outside.
template <class Dist, class Fix>
void bisect(const ClipV& p1, const ClipV& p2, ClipV& o, Dist dist, Fix fix) {
    ClipV a = p1, b = p2;
    if (dist(p1) < 0) std::swap(a, b);  // a inside, b outside
    ClipV m;
    for (int it = 0; it < 32; ++it) {
        m.x = mid32(a.x, b.x);
        m.y = mid32(a.y, b.y);
        m.z = mid32(a.z, b.z);
        const long long dm = dist(m);
        if (dm < 0) b = m;
        else if (dm > 0) a = m;
        else break;
    }
    if (dist(m) < 0) fix(m);
    o.x = m.x;
    o.y = m.y;
    o.z = m.z;
}
long long dTop(const ClipV& v) { return (long long)v.z - v.y; }
long long dBottom(const ClipV& v) { return (long long)v.z + v.y; }
long long dLeft(const ClipV& v) { return (long long)v.z + v.x; }
long long dRight(const ClipV& v) { return (long long)v.z - v.x; }

bool crossesZ(const ClipV& a, const ClipV& b) {
    const bool an = hi16(a.z) < 0, bn = hi16(b.z) < 0;
    return an != bn;
}

// clip_top_bisect_cb (3814)
void clipTopBisectCb(const ClipV& a, const ClipV& b, ClipV& o) {
    if (!crossesZ(a, b)) return clipTopDiv(a, b, o);
    const bool zero = add32(a.y, a.z) == 0 && add32(b.y, b.z) == 0;
    bisect(a, b, o, dTop, [](ClipV& m) { m.y = m.z; });
    o.isNew = true;
    if (zero) o.y = o.z = 0;
}
// clip_bottom_bisect_cb (38C6)
void clipBottomBisectCb(const ClipV& a, const ClipV& b, ClipV& o) {
    if (!crossesZ(a, b)) return clipBottomDiv(a, b, o);
    const bool zero = a.y == a.z && b.y == b.z;
    bisect(a, b, o, dBottom, [](ClipV& m) { m.y = neg32(m.z); });
    o.isNew = true;
    if (zero) o.y = o.z = 0;
}
// clip_left_bisect_cb (3978)
void clipLeftBisectCb(const ClipV& a, const ClipV& b, ClipV& o) {
    bisect(a, b, o, dLeft, [](ClipV& m) { m.x = neg32(m.z); });
    o.isNew = true;
}
void clipRightBisectCb(const ClipV& a, const ClipV& b, ClipV& o) {
    bisect(a, b, o, dRight, [](ClipV& m) { m.x = m.z; });
    o.isNew = true;
}

// Sutherland-Hodgman over the ring (clip_poly_* 9C14/9D2E/9E5D/9F89).
template <class Inside>
int clipPoly(std::vector<ClipV>& ring, int n, Inside inside, ClipFn fn) {
    std::vector<ClipV> out;
    out.reserve(size_t(n) * 2 + 2);
    const ClipV* prev = &ring[size_t(n - 1)];
    for (int i = 0; i < n; ++i) {
        const ClipV& cur = ring[size_t(i)];
        const bool ci = inside(cur), pi = inside(*prev);
        if (!ci) {
            if (pi) {
                ClipV o;
                fn(*prev, cur, o);
                out.push_back(o);
            }
        } else {
            if (!pi) {
                ClipV o;
                fn(*prev, cur, o);
                out.push_back(o);
            }
            out.push_back(cur);
        }
        prev = &cur;
    }
    ring.assign(out.begin(), out.end());
    return int(out.size());
}
bool inTop(const ClipV& v) { return v.y <= v.z; }                 // y <= z
bool inBottom(const ClipV& v) { return neg32(v.y) <= v.z; }        // -y <= z
bool inLeft(const ClipV& v) { return neg32(v.x) <= v.z; }          // -x <= z
bool inRight(const ClipV& v) { return v.x <= v.z; }

// Recording for the projection cache (F643/F644).
void recordPrim(u16 p) {
    if (g_recording && g_recCache) g_recCache->prims.push_back(p);
}

void rasterPolygon(const std::vector<s16>& pts, int n, u16 color) {
    if (n <= 0) return;
    gfx().fillPolygon(pts.data(), n, color);
}

// Type 0 polygon (86F8)
void drawPolygon(u16 p) {
    recordPrim(p);
    const int n = ds8(u16(p + 6));
    std::vector<int> idx(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) idx[size_t(i)] = ds8(u16(p + 7 + i));
    const u16 color = ds16(u16(p + 3));
    std::vector<s16> pts;
    auto projectAll = [&]() {
        pts.resize(size_t(n) * 2);
        for (int i = 0; i < n; ++i) {
            VertexSlot& v = slot(idx[size_t(i)]);
            projectSlotCached(v);
            pts[size_t(2 * i)] = v.sx;
            pts[size_t(2 * i + 1)] = v.sy;
        }
        rasterPolygon(pts, n, color);
    };
    if (g_drawClip == 0) return projectAll();
    // Vertical test with the plane shift.
    bool allIn = true;
    for (int i = 0; i < n && allIn; ++i) {
        const VertexSlot& v = slot(idx[size_t(i)]);
        s32 y = abs32(v.y), z = v.z;
        if (g_shiftY) {
            if (g_sideY) z = sar32(z, g_shiftY);
            else y = sar32(y, g_shiftY);
        }
        if (y > z) allIn = false;
    }
    if (allIn) return projectAll();

    std::vector<ClipV> ring(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const VertexSlot& v = slot(idx[size_t(i)]);
        ClipV& c = ring[size_t(i)];
        c.x = v.x;
        c.y = v.y;
        c.z = v.z;
        if (g_shiftY) {
            if (g_sideY) c.z = sar32(c.z, g_shiftY);
            else {
                c.x = sar32(c.x, g_shiftY);
                c.y = sar32(c.y, g_shiftY);
            }
        }
        c.src = idx[size_t(i)];
        c.isNew = false;
    }
    int cnt = n;
    const u8 cf = g_drawClip;
    const ClipFn topFn = g_precise ? clipTopBisectCb : clipTopDiv;
    const ClipFn botFn = g_precise ? clipBottomBisectCb : clipBottomDiv;
    if (cf & 3) {
        if ((cf & 1) && !(cf & 2)) {
            cnt = clipPoly(ring, cnt, inTop, topFn);
        } else if ((cf & 2) && !(cf & 1)) {
            cnt = clipPoly(ring, cnt, inBottom, botFn);
        } else if (hi16(ring[0].y) >= 0 && hi16(ring.size() > 1 ? ring[1].x : 0) >= 0) {
            // Order test reads ring[1] x instead of y (original quirk).
            cnt = clipPoly(ring, cnt, inTop, topFn);
            if (cnt > 0) cnt = clipPoly(ring, cnt, inBottom, botFn);
        } else {
            cnt = clipPoly(ring, cnt, inBottom, botFn);
            if (cnt > 0) cnt = clipPoly(ring, cnt, inTop, topFn);
        }
        if (cnt <= 0) return;
    }
    // Undo the vertical shift.
    if (g_shiftY) {
        for (int i = 0; i < cnt; ++i) {
            ClipV& c = ring[size_t(i)];
            if (c.isNew) {
                if (g_sideY) c.z = shl32(c.z, g_shiftY);
                else {
                    c.x = shl32(c.x, g_shiftY);
                    c.y = shl32(c.y, g_shiftY);
                }
            } else {
                const VertexSlot& v = slot(c.src);
                c.x = v.x;
                c.y = v.y;
                c.z = v.z;
            }
        }
    }
    // Horizontal clipping needed?
    bool needH = false;
    for (int i = 0; i < cnt && !needH; ++i) {
        const ClipV& c = ring[size_t(i)];
        const s8 zb = s8(u32(c.z) >> 24);
        if (zb > 0) continue;
        if (zb < 0 || c.z == 0) {
            needH = true;
            break;
        }
        const int zh = hi16(c.z);
        int xb = s8(u32(c.x) >> 24), yb = s8(u32(c.y) >> 24);
        xb = u8(xb < 0 ? -xb : xb);
        yb = u8(yb < 0 ? -yb : yb);
        if (zh < xb || zh < yb) needH = true;
    }
    if (needH) {
        if (g_shiftX) {
            for (int i = 0; i < cnt; ++i) {
                ClipV& c = ring[size_t(i)];
                if (g_sideX) c.z = sar32(c.z, g_shiftX);
                else {
                    c.x = sar32(c.x, g_shiftX);
                    c.y = sar32(c.y, g_shiftX);
                }
            }
        }
        const ClipFn leftFn = g_precise ? clipLeftBisectCb : clipLeftDiv;
        if (cf & 4) {
            cnt = clipPoly(ring, cnt, inLeft, leftFn);
            if (cnt <= 0) return;
        }
        if (cf & 8) {
            if (enhanced()) {
                // Enhanced: clip against the right plane (the original calls
                // the left-plane clipper a second time, 8BD4).
                cnt = clipPoly(ring, cnt, inRight, g_precise ? clipRightBisectCb : clipRightDiv);
            } else {
                cnt = clipPoly(ring, cnt, inLeft, leftFn);
            }
            if (cnt <= 0) return;
        }
        if (g_shiftX) {
            for (int i = 0; i < cnt; ++i) {
                ClipV& c = ring[size_t(i)];
                if (g_sideX) c.z = shl32(c.z, g_shiftX);
                else {
                    c.x = shl32(c.x, g_shiftX);
                    c.y = shl32(c.y, g_shiftX);
                }
            }
        }
    }
    pts.resize(size_t(cnt) * 2);
    for (int i = 0; i < cnt; ++i) {
        const ClipV& c = ring[size_t(i)];
        s16 sx, sy;
        if (c.isNew) {
            projectVert(c.x, c.y, c.z, sx, sy);
        } else {
            VertexSlot& v = slot(c.src);
            if (v.sx != 0x7FFE) {
                sx = v.sx;
                sy = v.sy;
            } else {
                projectVert(c.x, c.y, c.z, sx, sy);
                v.sx = sx;
                v.sy = sy;
            }
        }
        pts[size_t(2 * i)] = sx;
        pts[size_t(2 * i + 1)] = sy;
    }
    rasterPolygon(pts, cnt, color);
}

// clip_line_frustum (59BC): top/bottom planes on rounded 16-bit words.
int clipLineFrustum(const VertexSlot& a, const VertexSlot& b, s32 out0[3], s32 out1[3]) {
    s16 p[2][3] = {{roundHi(a.x), roundHi(a.y), roundHi(a.z)}, {roundHi(b.x), roundHi(b.y), roundHi(b.z)}};
    if (g_shiftY) {
        if (g_sideY) {
            p[0][2] = s16(p[0][2] >> g_shiftY);
            p[1][2] = s16(p[1][2] >> g_shiftY);
        } else {
            for (int k = 0; k < 2; ++k)
                for (int c = 0; c < 2; ++c) p[k][c] = s16(p[k][c] >> g_shiftY);
        }
    }
    int result = 0;
    auto outcode = [](const s16* v) {
        int o = 0;
        if (v[1] > v[2]) o |= 1;
        if (v[1] < s16(-v[2])) o |= 2;
        return o;
    };
    auto move = [&](s16* v, const s16* w, s32 d1, s32 den, bool top) {
        const s16 t = clipT(d1, den);
        for (int c = 0; c < 2; ++c) {
            const s32 prod = s32(t) * s16(w[c] - v[c]);
            const s32 r11 = prod >> 11;
            v[c] = s16(v[c] + s16(prod >> 12) + (r11 & 1));
        }
        v[2] = top ? v[1] : s16(-v[1]);
    };
    for (int it = 0; it < 8; ++it) {
        const int o0 = outcode(p[0]), o1 = outcode(p[1]);
        if (o0 & o1) {
            result = -1;
            break;
        }
        if (!o0 && !o1) break;
        if (o0 & 1) {
            result |= 1;
            const s32 d0 = s32(p[0][2]) - p[0][1];
            move(p[0], p[1], d0, d0 - (s32(p[1][2]) - p[1][1]), true);
        } else if (o0 & 2) {
            result |= 1;
            const s32 d0 = s32(p[0][2]) + p[0][1];
            move(p[0], p[1], d0, d0 - (s32(p[1][2]) + p[1][1]), false);
        } else if (o1 & 1) {
            result |= 2;
            const s32 d1 = s32(p[1][2]) - p[1][1];
            move(p[1], p[0], d1, d1 - (s32(p[0][2]) - p[0][1]), true);
        } else {
            result |= 2;
            const s32 d1 = s32(p[1][2]) + p[1][1];
            move(p[1], p[0], d1, d1 - (s32(p[0][2]) + p[0][1]), false);
        }
    }
    if (result < 0) return -1;
    auto emit = [&](const s16* v, s32* o) {
        s16 x = v[0], y = v[1], z = v[2];
        if (g_shiftY) {
            if (g_sideY) z = s16(z << g_shiftY);
            else {
                x = s16(x << g_shiftY);
                y = s16(y << g_shiftY);
            }
        }
        o[0] = mk32(x, 0);
        o[1] = mk32(y, 0);
        o[2] = mk32(z, 0);
    };
    if (result & 1) emit(p[0], out0);
    if (result & 2) emit(p[1], out1);
    return result;
}

// clip_line_bisect (75B6): precise mode, 32-bit with bisection.
int clipLineBisect(const VertexSlot& a, const VertexSlot& b, s32 out0[3], s32 out1[3]) {
    ClipV p[2];
    const VertexSlot* src[2] = {&a, &b};
    for (int k = 0; k < 2; ++k) {
        p[k].x = src[k]->x;
        p[k].y = src[k]->y;
        p[k].z = src[k]->z;
        if (g_shiftY) {
            if (g_sideY) p[k].z = sar32(p[k].z, g_shiftY);
            else {
                p[k].x = sar32(p[k].x, g_shiftY);
                p[k].y = sar32(p[k].y, g_shiftY);
            }
        }
    }
    int result = 0;
    auto outcode = [](const ClipV& v) {
        int o = 0;
        if (v.y > v.z) o |= 1;
        if (neg32(v.z) > v.y) o |= 2;
        return o;
    };
    for (int it = 0; it < 8; ++it) {
        const int o0 = outcode(p[0]), o1 = outcode(p[1]);
        if (o0 & o1) {
            result = -1;
            break;
        }
        if (!o0 && !o1) break;
        ClipV r;
        if (o0 & 1) {
            result |= 1;
            bisect(p[0], p[1], r, dTop, [](ClipV& m) { m.y = m.z; });
            p[0].x = r.x, p[0].y = r.y, p[0].z = r.z;
        } else if (o0 & 2) {
            result |= 1;
            bisect(p[0], p[1], r, dBottom, [](ClipV& m) { m.y = neg32(m.z); });
            p[0].x = r.x, p[0].y = r.y, p[0].z = r.z;
        } else if (o1 & 1) {
            result |= 2;
            bisect(p[0], p[1], r, dTop, [](ClipV& m) { m.y = m.z; });
            p[1].x = r.x, p[1].y = r.y, p[1].z = r.z;
        } else {
            result |= 2;
            bisect(p[0], p[1], r, dBottom, [](ClipV& m) { m.y = neg32(m.z); });
            p[1].x = r.x, p[1].y = r.y, p[1].z = r.z;
        }
    }
    for (ClipV& v : p) {
        if (g_shiftY) {
            if (g_sideY) v.z = shl32(v.z, g_shiftY);
            else {
                v.x = shl32(v.x, g_shiftY);
                v.y = shl32(v.y, g_shiftY);
            }
        }
    }
    if (result < 0) return -1;
    if (result & 1) out0[0] = p[0].x, out0[1] = p[0].y, out0[2] = p[0].z;
    if (result & 2) out1[0] = p[1].x, out1[1] = p[1].y, out1[2] = p[1].z;
    return result;
}

// Type 1 line (8CB9)
void drawLine(u16 p) {
    recordPrim(p);
    VertexSlot& a = slot(ds8(u16(p + 5)));
    VertexSlot& b = slot(ds8(u16(p + 6)));
    s16 x0, y0, x1, y1;
    if (g_drawClip == 0) {
        projectSlotCached(a);
        projectSlotCached(b);
        x0 = a.sx, y0 = a.sy, x1 = b.sx, y1 = b.sy;
    } else {
        s32 o0[3]{}, o1[3]{};
        const int r = g_precise ? clipLineBisect(a, b, o0, o1) : clipLineFrustum(a, b, o0, o1);
        if (r < 0) return;
        if (r & 1) projectVert(o0[0], o0[1], o0[2], x0, y0);
        else {
            projectSlotCached(a);
            x0 = a.sx, y0 = a.sy;
        }
        if (r & 2) projectVert(o1[0], o1[1], o1[2], x1, y1);
        else {
            projectSlotCached(b);
            x1 = b.sx, y1 = b.sy;
        }
    }
    const u16 c = ds16(u16(p + 3));
    if (g_hi.on) g_hi.line(x0, y0, x1, y1, c);
    else gfx().line(x0, y0, x1, y1, c);
}

// Type 2 point (8E08)
void drawPoint(u16 p) {
    recordPrim(p);
    VertexSlot& v = slot(ds8(u16(p + 5)));
    if (hi16(v.z) < 0) return;
    if (hi16(v.z) == 0 && s16(lo16(v.z)) < 0) return;
    const u16 c = ds16(u16(p + 3));
    projectSlotCached(v);
    if (g_hi.on) g_hi.point(v.sx, v.sy, c);
    else gfx().pixel(v.sx, v.sy, c);
}

bool zPositive(const VertexSlot& v) { return hi16(v.z) > 0 || (hi16(v.z) == 0 && lo16(v.z) != 0); }

// Type 3 disc (8E67)
void drawDisc(u16 p) {
    recordPrim(p);
    VertexSlot& v = slot(ds8(u16(p + 7)));
    if (!zPositive(v)) return;
    projectSlotCached(v);
    const int K = xform().K;
    const s16 rho = s16(K >= 0 ? (ds16(u16(p + 5)) << K) : (ds16(u16(p + 5)) >> -K));
    s16 sx, sy;
    projectVert(mk32(rho, 0), 0, v.z, sx, sy);
    const int cx = g_hi.on ? g_hi.cx : g_clipCx;
    gfx().fillCircle(v.sx, v.sy, s16(sx - cx), ds16(u16(p + 3)));
}

// Type 4 callback (8EF6)
void drawCallback(u16 p) {
    recordPrim(p);
    VertexSlot& v = slot(ds8(u16(p + 7)));
    if (!zPositive(v)) return;
    projectSlotCached(v);
    const u32 fn = u32(ds16(u16(p + 3))) | (u32(ds16(u16(p + 5))) << 16);
    if (fn == 0x248E0C52u && g_billboard) g_billboard(p, v.sx, v.sy);
}

void drawPrim(u16 p) {
    switch (ds8(p) & 7) {
    case 0: drawPolygon(p); break;
    case 1: drawLine(p); break;
    case 2: drawPoint(p); break;
    case 3: drawDisc(p); break;
    case 4: drawCallback(p); break;
    default: break;
    }
}

// r3d_draw_bsp (824B)
void drawBsp(u16 node, int depth = 0) {
    if (depth > 64) return;
    const u8 t = ds8(node);
    if (!(t & 1)) {
        if (primVisible(node)) {
            drawBsp(ds16(u16(node + 5)), depth + 1);
            drawBsp(ds16(u16(node + 3)), depth + 1);
        } else {
            drawBsp(ds16(u16(node + 3)), depth + 1);
            drawBsp(ds16(u16(node + 5)), depth + 1);
        }
        return;
    }
    if (!(t & 2)) return;
    const int n = ds8(u16(node + 1));
    for (int i = 0; i < n; ++i) {
        const u16 p = ds16(u16(node + 2 + 2 * i));
        if (primVisible(p)) drawPrim(p);
    }
}

// ============================================================ model hooks (6.5)

void runHook(Hook h, const Obj3D* o) {
    switch (h) {
    case Hook::Uh1Rotor: {
        // 02D1: ((a >> 3) + 1200 * dt / 256) mod 360, stored << 3 in both rotors.
        const s32 inc = (s32(0x4B0) * s16(g_ctx.frameTicks)) >> 8;
        const s32 a = s32(dsS16(0xCACF) >> 3) + inc;
        const s16 r = s16(a % 360);
        dsW16(0xCACF, u16(r << 3));
        dsW16(0xCA7B, u16(r << 3));
        break;
    }
    case Hook::Mike2: {
        if (!o) break;
        dsW8(0x91CE, o->pitch == 0 ? 3 : 1);
        const u8 f = o->pitch >= 0x20 ? 3 : 1;
        dsW8(0x9214, f);
        dsW8(0x9223, f);
        // 1000:3300 geo_bearing(boat, reinsertion point) in degrees
        const int b = mathHeading(o->pos.x, o->pos.z, g_ctx.reinsertPoint.x, g_ctx.reinsertPoint.z) >> 3;
        dsW16(0x912A, u16(b << 3));
        dsW8(0x911A, 7);
        break;
    }
    case Hook::Lssc: {
        if (!o) break;
        dsW8(0x8D9C, o->pitch == 0 ? 3 : 1);
        const u8 f = o->pitch >= 0x20 ? 3 : 1;
        dsW8(0x8D8D, f);
        dsW8(0x8D7E, f);
        break;
    }
    case Hook::Ripples:
        if (!o) break;
        dsW8(0xB189, o->pitch == 0 ? 3 : 1);
        dsW8(0xB197, o->pitch == 0 ? 1 : 3);
        break;
    case Hook::Rippleb:
        if (!o) break;
        dsW8(0xB097, o->pitch == 0 ? 3 : 1);
        dsW8(0xB0A1, o->pitch == 0 ? 3 : 1);
        dsW8(0xB0B1, o->pitch == 0 ? 1 : 3);
        break;
    case Hook::Tunnel:
        if (!o) break;
        dsW8(0xC581, o->pitch == 0 ? 3 : 1);
        dsW8(0xC585, o->pitch == 0 ? 1 : 3);
        break;
    case Hook::Cobra: {
        // 0470: the angle grows without a modulo (it wraps at 16 bits and
        // then indexes outside the sine table). Enhanced keeps it in range.
        s16 a = s16(((dsS16(0x75A5) >> 3) + 60) << 3);
        if (enhanced()) a = s16(((a % kAngleFull) + kAngleFull) % kAngleFull);
        dsW16(0x75A5, u16(a));
        dsW16(0x75C6, u16(a));
        break;
    }
    case Hook::Pit: {
        if (!o) break;
        int d = g_ctx.todHour - 12;
        if (d < 0) d = -d;
        const int c = ds8(u16(0x07B6 + (d >> 2))) + (o->pitch != 0 ? 0x20 : 0);
        dsW16(0xAEE7, u16(c | 0x5A00));
        break;
    }
    default: break;
    }
}

// ============================================================ drawing one object (8.6)

u32 aspectWord() { return kAspectBa; }

ProjCache* allocCache() {
    g_cachePool.push_back(std::make_unique<ProjCache>());
    return g_cachePool.back().get();
}
void freeCache(ProjCache* c) {
    if (!c) return;
    for (auto it = g_cachePool.begin(); it != g_cachePool.end(); ++it)
        if (it->get() == c) {
            g_cachePool.erase(it);
            return;
        }
}

// r3d_cache_store (814B)
void cacheStore(ProjCache* c, const LodModel& L, u16 descDs) {
    c->angles[0] = g_heading;
    c->angles[1] = g_pitch;
    c->angles[2] = g_roll;
    c->zoom = s16(g_zoom);
    c->aspect = aspectWord();
    c->descDs = descDs;
    c->modelDs = L.ds;
    c->frame = g_view.frame;
    c->K = xform().K;
    s16 cx, cy;
    const XformState& x = xform();
    projectVert(x.centre[0], x.centre[1], x.centre[2], cx, cy);
    c->offsets.resize(size_t(L.nv) * 2);
    for (int i = 0; i < L.nv; ++i) {
        c->offsets[size_t(2 * i)] = s16(slot(i).sx - cx);
        c->offsets[size_t(2 * i + 1)] = s16(slot(i).sy - cy);
    }
}

// r3d_cache_draw (81D0)
void cacheDraw(const LodModel& L, ProjCache* c) {
    g_recording = false;
    xform().K = c->K;
    s16 cx, cy;
    const XformState& x = xform();
    projectVert(x.centre[0], x.centre[1], x.centre[2], cx, cy);
    for (int i = 0; i < L.nv; ++i) {
        slot(i).sx = s16(c->offsets[size_t(2 * i)] + cx);
        slot(i).sy = s16(c->offsets[size_t(2 * i + 1)] + cy);
    }
    for (u16 p : c->prims) drawPrim(p);
}

// r3d_draw_object (B37E)
void drawObject(RenderRec& rec) {
    Obj3D* o = rec.obj;
    const game::ModelDesc* desc = o->model;
    const ModelInfo* mi = modelInfo(desc);
    const LodModel* L = lodModelAt(rec.model);
    if (!mi || !L) return;
    g_drawModel = L;
    g_drawClip = rec.clip;
    XformState& x = xform();
    if (o->flags & of::kStatic) {
        x.objAngles[0] = x.objAngles[1] = x.objAngles[2] = 0;
    } else {
        x.objAngles[0] = o->heading;
        x.objAngles[1] = o->pitch;
        x.objAngles[2] = o->roll;
    }
    const s32 rw = desc->radius_world;
    g_precise = s16(hi16(rw)) >= 8 && rec.dist <= u32(rw) && !(o->flags & of::kNoPrecise);
    g_drawState.obj = o;
    g_drawState.x = rec.x;
    g_drawState.y = rec.y;
    g_drawState.z = rec.z;
    g_drawState.K = rec.K;
    g_drawState.clip = rec.clip;
    if (L->hook != Hook::None) runHook(L->hook, o);
    x.centre[0] = mk32(rec.x, 0);
    x.centre[1] = mk32(rec.y, 0);
    x.centre[2] = mk32(rec.z, 0);
    x.K = rec.K;
    ProjCache* cache = rec.cache;
    const View& v = g_view;
    if (cache && rec.clip == 0 && !g_hi.on) {
        const u32 age = v.frame - cache->frame;
        bool valid = s32(age) >= 0 && age <= u32(v.cacheAge);
        const s16 cur[3] = {g_heading, g_pitch, g_roll};
        for (int i = 0; i < 3 && valid; ++i) {
            int d = cur[i] - cache->angles[i];
            if (d < 0) d = -d;
            if (s16(d) >= v.cacheRot) valid = false;
        }
        // "outside the radius": high words compared signed; with equal high
        // words the original accepts only dist < radius (flag misuse).
        const s16 dh = s16(rec.dist >> 16), rh = hi16(rw);
        if (valid) valid = dh > rh || (dh == rh && u16(rec.dist) < lo16(rw));
        if (valid) valid = cache->descDs == mi->ds && cache->modelDs == L->ds && cache->zoom == g_zoom &&
                           cache->aspect == aspectWord();
        if (valid) {
            cacheDraw(*L, cache);
            return;
        }
    }
    const bool useCache = v.cacheOn && (o->flags & of::kProjCache) && rec.clip == 0 && !g_hi.on &&
                          s16(s16(hi16(rw) * 4) + 0xB) < s16(rec.dist >> 16);
    if (useCache) {
        if (cache && (cache->modelDs != L->ds || cache->descDs != mi->ds)) {
            freeCache(cache);
            cache = nullptr;
        }
        if (!cache) cache = allocCache();
    } else if (cache) {
        freeCache(cache);
        cache = nullptr;
    }
    // mdl_reset_facing (8131)
    u16 p = u16(L->ds + 0x0E);
    for (int i = 0; i < L->nFacing; ++i) {
        dsW8(u16(p + 5), 0);
        p = u16(p + 7 + ds8(u16(p + 6)));
    }
    // Object matrix
    if (x.objAngles[0] || x.objAngles[1] || x.objAngles[2]) {
        if (x.objAngles[0] != x.objMatAngles[0] || x.objAngles[1] != x.objMatAngles[1] ||
            x.objAngles[2] != x.objMatAngles[2]) {
            x.objMatAngles[0] = x.objAngles[0];
            x.objMatAngles[1] = x.objAngles[1];
            x.objMatAngles[2] = x.objAngles[2];
            x.obj = mathRotMatrix(x.objAngles[0], x.objAngles[1], x.objAngles[2]);
        }
    }
    if (L->compiled) {
        if (g_hi.on) g_hi.transform(*L);
        else xfTransform(*L);
    }
    // xf_invalidate_proj (80C0)
    for (int i = 0; i < L->nv; ++i) slot(i).sx = 0x7FFE;
    rec.cache = cache;
    if (cache) {
        g_recording = true;
        g_recCache = cache;
        cache->prims.clear();
    } else {
        g_recording = false;
    }
    drawBsp(L->root);
    if (cache) cacheStore(cache, *L, mi->ds);
    g_recording = false;
    g_recCache = nullptr;
    ++g_stats.drawn;
}

// ============================================================ sky and ground (8.5)

// r3d_horizon_rows (601D)
void horizonRows(s16* tab, int& ymax, int& ymin, s16 By, s16 Bx, s16 Cy, s16 Cx) {
    if (By < Cy) {
        Bx = s16(2 * Cx - Bx);
        By = s16(2 * Cy - By);
    }
    int dy = s16(By - Cy);
    ymin = s16(Cy - dy);
    ymax = s16(Cy + dy);
    if (dy == 0) return;
    s32 slope;
    if (dy == 1) {
        slope = s32(s16(Bx - Cx)) * 256;
    } else {
        const s16 d = s16(Bx - Cx);
        const s32 num = s32(d) * 512 + dy;
        s16 q;
        if (!idiv32(num, s16(dy * 2), q)) {
            q = num < 0 ? s16(0x8001) : 0x7FFF;
            if (s16(dy * 2) < 0) q = s16(-q);
        }
        slope = q;
    }
    s32 acc = s32(Cx) * 256 + 127;
    int up = Cy, dn = Cy;
    for (int k = 0; k <= dy; ++k) {
        const s16 x = s16(acc >> 8);
        if (up >= g_clipY0 && up <= g_clipY1) tab[up] = x;
        if (dn >= g_clipY0 && dn <= g_clipY1) tab[dn] = s16(2 * Cx - x);
        acc = add32(acc, slope);
        ++up;
        --dn;
    }
}

void frameLimitWait() {
    if (g_wait) engine::ticker().frameLimitWait();
}

// r3d_draw_sky_ground (671C)
void drawSkyGround(u8 ground, u8 sky) {
    Gfx& gx = gfx();
    if (ground == sky) {
        if (ground == 0xFF) return;
        frameLimitWait();
        gx.fillRows(g_clipY0, g_clipH, ground);
        return;
    }
    World& w = world();
    View& v = g_view;
    if (w.horizonTerm != v.htWorld || v.htZoom != g_zoom) {
        v.htTerm = s16(lo16(sar32(s32(w.horizonTerm) * 30000, g_zoom)));
        v.htWorld = w.horizonTerm;
        v.htZoom = g_zoom;
    }
    s32 P[3] = {0, mk32(v.htTerm, 0), mk32(30000, 0)};
    mathMatVec(P, g_camMatPR);
    if (g_hi.on) {
        frameLimitWait();
        g_hi.skyGround(P, ground, sky, g_roll, g_pitch);
        return;
    }
    s16 sx, sy;
    const bool savedPrecise = g_precise;
    g_precise = true;
    projectPrecise(P[0], P[1], P[2], sx, sy);
    g_precise = savedPrecise;
    auto a16 = [](s16 x) { return int(x < 0 ? -x : x); };
    if (a16(sx) < 15001 && a16(sy) < 15001) {
        if (v.hzW != g_clipW || v.hzH != g_clipH) {
            const s16 a = hi16(s32(u32(s32(g_clipW)) * kAspectBa));
            const s16 b = hi16(s32(u32(s32(g_clipH)) * kAspectAb));
            v.hzLen = std::max(a, b);
            v.hzW = g_clipW;
            v.hzH = g_clipH;
        }
        const s16 L = v.hzLen;
        const s16 Cx = sx, Cy = sy;
        s16 Ax = s16(sx - L), Ay = sy, Bx = s16(sx + L), By = sy;
        mathRotate2d(Ax, Ay, Cx, Cy, -g_roll);
        mathRotate2d(Bx, By, Cx, Cy, -g_roll);
        Ax = s16(hi16(s32(s16(Ax - Cx)) * s32(kAspectAb)) + Cx);
        Bx = s16(hi16(s32(s16(Bx - Cx)) * s32(kAspectAb)) + Cx);
        s16 tab[200]{};
        int ymax, ymin;
        horizonRows(tab, ymax, ymin, By, Bx, Cy, Cx);
        if (ymin <= g_clipY1 && ymax >= g_clipY0) {
            if (ymin < g_clipY0) ymin = g_clipY0;
            if (ymax > g_clipY1) ymax = g_clipY1;
            u8 bandLeft = Ay < By ? ground : sky;
            u8 other = Ay < By ? sky : ground;
            if (ymax < ymin) ymin = ymax;
            u8 above = Bx < Ax ? ground : sky;
            u8 below = Bx < Ax ? sky : ground;
            if (g_pitch > 720 && g_pitch < 2160) {
                std::swap(bandLeft, other);
                std::swap(above, below);
            }
            frameLimitWait();
            if (g_clipY0 < ymin) gx.fillRows(g_clipY0, ymin - g_clipY0, above);
            if (ymax < g_clipY1) gx.fillRows(ymax + 1, g_clipY1 - ymax, below);
            if (ymax != ymin) gx.fillSplitRows(&tab[ymin], ymin, ymax, bandLeft, other);
            else gx.fillRows(ymin, 1, ground);
            return;
        }
    }
    frameLimitWait();
    gx.fillRows(g_clipY0, g_clipH, (g_pitch >= 0 && g_pitch < 1440) ? sky : ground);
}

// ============================================================ misc

// tgt_update_reticle (1000:65D3): project the render record of the target.
void updateReticle() {
    if (!g_ctx.reticleTarget) return;
    for (RenderRec* r = g_drawList; r; r = r->next)
        if (r->obj == g_ctx.reticleTarget) {
            const bool savedPrecise = g_precise;
            g_precise = false;
            s16 sx, sy;
            if (g_hi.on) {
                g_hi.project(mk32(r->x, 0), mk32(r->y, 0), mk32(r->z, 0), sx, sy);
                sx = s16(std::clamp(std::lround(g_hi.toPageX(sx)), -32000L, 32000L));
                sy = s16(std::clamp(std::lround(g_hi.toPageY(sy)), -32000L, 32000L));
            } else {
                projectNormal(mk32(r->x, 0), mk32(r->y, 0), mk32(r->z, 0), sx, sy);
            }
            g_precise = savedPrecise;
            g_ctx.reticleX = sx;
            g_ctx.reticleY = sy;
            return;
        }
}

}  // namespace

// ============================================================ public API

RenderContext& renderContext() { return g_ctx; }
const RenderStats& renderStats() { return g_stats; }
const DrawState& drawState() { return g_drawState; }
void setBillboardCallback(PrimCallback fn) { g_billboard = fn; }
int frameScale() { return g_hi.on ? std::max(1, int(std::ceil(std::max(g_hi.kx, g_hi.ky)))) : 1; }
double frameScaleX() { return g_hi.on ? g_hi.kx : 1.0; }
double frameScaleY() { return g_hi.on ? g_hi.ky : 1.0; }
int projectionCentreX() { return g_hi.on ? g_hi.cx : g_clipCx; }

void project(const s32 v[3], s16& sx, s16& sy) { projectVert(v[0], v[1], v[2], sx, sy); }

void projectPage(const s32 v[3], s16& sx, s16& sy) { projectNormal(v[0], v[1], v[2], sx, sy); }

void viewInit() {
    viewFree();
    g_view = View{};
    g_view.valid = true;
    g_wbAge = 20;
}

void viewFree() {
    for (ViewEntry& e : g_view.list) {
        if (e.obj) e.obj->flags &= u16(~of::kInViewList);
        freeCache(e.cache);
    }
    g_view.list.clear();
    g_cachePool.clear();
}

int renderView(const game::Camera& cam, bool wait) {
    return renderView(cam.pos.x, cam.pos.y, cam.pos.z, cam.yaw, cam.pitch, cam.roll, cam.rect_x, cam.rect_y,
                      cam.rect_w, cam.rect_h, cam.zoom, wait);
}

int renderView(s32 x, s32 y, s32 z, int heading, int pitch, int roll, int rx, int ry, int rw, int rh, int zoom,
               bool wait) {
    World& w = world();
    if (!w.built || !modelsLoaded()) return kRenderOk;
    if (!g_view.valid) viewInit();
    g_status = 0;
    g_stats = RenderStats{};
    g_camX = x;
    g_camY = y;
    g_camZ = z;
    g_heading = s16(heading);
    g_pitch = s16(pitch);
    g_roll = s16(roll);
    g_zoom = zoom;
    g_wait = wait;
    XformState& xf = xform();
    xf.camAngles[0] = g_heading;
    xf.camAngles[1] = g_pitch;
    xf.camAngles[2] = g_roll;
    g_ctx.cameraPos = game::Vec3{x, y, z};

    setClip(rx, ry, rw, rh);
    gfx().setClip(rx, ry, rw, rh);
    // Enhanced: extended draw distance needs room for more objects.
    const bool enh = enhanced();
    g_workSize = enh ? 5000 * 64 : 5000;
    g_view.capacity = enh ? 200 * 64 : 200;
    g_hi.begin(rx, ry, rw, rh, g_clipCx, g_clipCy, zoom, x, y, z);
    if (g_hi.on) {
        // A wide window widens the viewport: the frustum, the culling and
        // the clip planes use the page rectangle that covers it (the
        // projection centre stays the camera rect's).
        g_clipX0 = g_hi.cvx;
        g_clipY0 = g_hi.cvy;
        g_clipW = g_hi.cvw;
        g_clipH = g_hi.cvh;
        g_clipX1 = g_clipX0 + g_clipW - 1;
        g_clipY1 = g_clipY0 + g_clipH - 1;
    }

    frustumNormals();
    cameraMatrix();
    frustumCorners();
    clipShifts();

    // 8.2 gathering
    const GridCell* cell = worldCellAt(g_camX, g_camZ);
    g_drawList = nullptr;
    g_recUsed = 0;
    // Room for every record the work buffer can hold (26 bytes each in the
    // original accounting) plus the persistent view list; never resized mid-frame.
    const size_t recCap = size_t(g_workSize / 26) + size_t(g_view.capacity) + 64;
    if (g_recs.size() < recCap) g_recs.resize(recCap);
    g_batch.clear();
    g_batchOff = 0;
    g_recOff = g_workSize - 0x16;
    const int Nsub = g_view.subsample;
    const int skip = int(u32(Nsub - 1) & g_view.frame);
    if (cell) {
        gatherList(cell->objects, skip, Nsub);
        flushBatch();
        for (size_t k = size_t(skip); k < cell->visible.size() && !g_status; k += size_t(Nsub)) {
            gatherList(cell->visible[k]->objects, 0, 1);
            flushBatch();
        }
    }
    if (!g_status) {
        gatherList(w.dynamicList, skip, Nsub);
        flushBatch();
    }
    // Persistent view list: every listed object is re-tested (no world box).
    g_inViewList = true;
    for (ViewEntry& e : g_view.list) {
        bool keep = false;
        if (!g_status) {
            ++g_stats.tested;
            RenderRec* r = newRec();
            r->parent = e.parent;
            r->obj = e.obj;
            if (objVisible(e.obj, *r)) {
                r->cache = e.cache;
                commitRec(r, false);
                keep = true;
            }
        }
        if (!keep) {
            e.obj->flags &= u16(~of::kInViewList);
            freeCache(e.cache);
            e.cache = nullptr;
        }
    }
    g_inViewList = false;
    flushBatch((g_view.frame & 1) != 0);
    // (the five temporary slots of r3d_temp_add are never used by the game)

    if (g_hi.on) g_hi.beginDraw();
    drawSkyGround(w.groundColor, w.skyColor);
    for (RenderRec* r = g_drawList; r; r = r->next) drawObject(*r);
    updateReticle();

    // Rebuild the view list from the draw list.
    std::vector<ViewEntry> list;
    int count = 0;
    for (RenderRec* r = g_drawList; r; r = r->next) {
        if (count < g_view.capacity) {
            list.push_back(ViewEntry{r->obj, r->cache, r->parent});
        } else {
            if (g_view.capacity > 0) g_status = 2;
            r->obj->flags &= u16(~of::kInViewList);
            freeCache(r->cache);
            r->cache = nullptr;
        }
        ++count;
    }
    g_view.list = std::move(list);
    g_stats.listed = int(g_view.list.size());
    ++g_view.frame;
    if (g_hi.on) g_hi.end();
    // Restore the 2D clip to the viewport in page coordinates.
    gfx().setClip(rx, ry, rw, rh);
    return g_status;
}

}  // namespace st::render
