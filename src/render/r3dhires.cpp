#include "render/r3dhires.h"

#include "core/settings.h"
#include "gfx/gfx.h"
#include "platform/system.h"
#include "render/model.h"
#include "render/r3d.h"

#include <algorithm>
#include <cmath>

namespace st::render {

namespace {

struct DMat {
    double m[9];
};

DMat toD(const Mat3& a) {
    DMat r;
    for (int i = 0; i < 9; ++i) r.m[i] = a.m[i] / 16384.0;
    return r;
}
DMat mul(const DMat& a, const DMat& b) {
    DMat r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[3 * i + j] = a.m[3 * i] * b.m[j] + a.m[3 * i + 1] * b.m[3 + j] + a.m[3 * i + 2] * b.m[6 + j];
    return r;
}
DMat identity() { return DMat{{1, 0, 0, 0, 1, 0, 0, 0, 1}}; }

s16 clamp16(double v) { return s16(std::clamp(std::lround(v), -32000L, 32000L)); }

}  // namespace

HiResLayer* hiResLayerForDrawPage() {
    const int N = settings().effectiveRenderScale();
    if (N <= 1) return nullptr;
    HiResLayer& l = sys().video().activateHiResLayer(gfx().drawPage(), N);
    gfx().refreshCoverage();
    return &l;
}

void HiFrame::begin(int n, int rx, int ry, int rw, int rh, int clipCx, int clipCy, int zoomShift, s32 cx32,
                    s32 cy32, s32 cz32) {
    on = n > 1;
    N = on ? n : 1;
    vx = rx;
    vy = ry;
    vw = rw;
    vh = rh;
    zoom = zoomShift;
    camX = cx32;
    camY = cy32;
    camZ = cz32;
    if (!on) return;
    page = gfx().drawPage();
    layer = &sys().video().activateHiResLayer(page, N);
    // Original pixel p covers layer pixels p*N .. p*N+N-1; its centre is the
    // projection of original coordinate p.
    cxd = double(clipCx) * N + (N - 1) * 0.5;
    cyd = double(clipCy) * N + (N - 1) * 0.5;
    cx = int(std::lround(cxd));
    cy = int(std::lround(cyd));
    bmp.flags = 0;
    bmp.w = layer->w;
    bmp.h = layer->h;
    bmp.bpr = layer->w / 4;
    bmp.data = layer->pixels.data();
}

void HiFrame::beginDraw() {
    if (!on) return;
    Gfx& gx = gfx();
    gx.setTarget(&bmp);
    gx.setClip(vx * N, vy * N, vw * N, vh * N);
}

void HiFrame::end() {
    if (!on) return;
    Gfx& gx = gfx();
    gx.setTarget(nullptr);
    gx.refreshCoverage();
    // The viewport now shows the layer. The page gets a point-sampled copy
    // so that code reading the page (page copies, blits) sees the 3D view.
    u8* pagePix = gx.page(page).data;
    const int o = N / 2;
    for (int y = std::max(vy, 0); y < std::min(vy + vh, 200); ++y)
        for (int x = std::max(vx, 0); x < std::min(vx + vw, 320); ++x) {
            layer->coverage[size_t(y * 320 + x)] = 1;
            pagePix[y * 320 + x] = layer->pixels[size_t(y * N + o) * size_t(layer->w) + size_t(x * N + o)];
        }
    sys().video().markDirty();
}

void HiFrame::project(s32 x, s32 y, s32 z, s16& sx, s16& sy) const {
    const double k = double(N) * double(1 << zoom);
    if (z == 0) {
        sx = x > 0 ? 32000 : x < 0 ? -32000 : s16(cx);
        sy = y > 0 ? -32000 : y < 0 ? 32000 : s16(cy);
        return;
    }
    sx = clamp16(cxd + k * double(x) / double(z));
    sy = clamp16(cyd - k * double(y) / double(z));
}

void HiFrame::line(int x0, int y0, int x1, int y1, u16 c) const {
    Gfx& gx = gfx();
    const bool xMajor = std::abs(y0 - y1) < std::abs(x0 - x1);
    for (int k = 0; k < N; ++k) {
        const int o = k - (N - 1) / 2;
        if (xMajor) gx.line(x0, y0 + o, x1, y1 + o, c);
        else gx.line(x0 + o, y0, x1 + o, y1, c);
    }
}

void HiFrame::point(int x, int y, u16 c) const {
    const int o = (N - 1) / 2;
    gfx().fillRect(x - o, y - o, N, N, c);
}

// Floating-point version of the generated vertex code: object centre and
// vertices in 16.16 camera units (the units of the render record).
void HiFrame::transform(const LodModel& L) const {
    const XformState& x = xform();
    const DrawState& ds = drawState();
    const DMat cam = toD(x.cam);
    const bool rotated = x.objAngles[0] || x.objAngles[1] || x.objAngles[2];
    const DMat obj = rotated ? toD(x.obj) : identity();
    const DMat body = mul(cam, obj);
    // Centre with full precision (r3d_obj_to_camera without truncation).
    double C[3];
    if (ds.obj && ds.obj->model) {
        const int s = ds.obj->model->scale_shift;
        const double f = std::ldexp(1.0, ds.K - 8 - s);
        const double r[3] = {double(s32(u32(ds.obj->pos.x) - u32(camX))) * f,
                             double(s32(u32(ds.obj->pos.y) - u32(camY))) * f,
                             double(s32(u32(ds.obj->pos.z) - u32(camZ))) * f};
        for (int i = 0; i < 3; ++i) C[i] = cam.m[3 * i] * r[0] + cam.m[3 * i + 1] * r[1] + cam.m[3 * i + 2] * r[2];
    } else {
        for (int i = 0; i < 3; ++i) C[i] = hi16(x.centre[i]);
    }
    const double unit = std::ldexp(1.0, x.K);
    double V[kVertexSlots][3];
    auto range = [&](const DMat& M, int first, int last) {
        for (int i = first; i <= last && i < L.nv && i < kVertexSlots; ++i) {
            const CompiledVertex& cv = L.verts[size_t(i)];
            const double* base = cv.parent == 0xFF ? C : V[cv.parent];
            const double d[3] = {cv.delta[0] * unit, cv.delta[1] * unit, cv.delta[2] * unit};
            for (int c = 0; c < 3; ++c)
                V[i][c] = base[c] + M.m[3 * c] * d[0] + M.m[3 * c + 1] * d[1] + M.m[3 * c + 2] * d[2];
        }
    };
    range(body, 0, L.bodyLast);
    for (const ModelPart& p : L.parts) {
        const s16 a0 = dsS16(p.angles), a1 = dsS16(u16(p.angles + 2)), a2 = dsS16(u16(p.angles + 4));
        if (a0 || a1 || a2) {
            const DMat part = toD(mathRotMatrix(angleWrap(a0), angleWrap(a1), angleWrap(a2)));
            range(mul(cam, rotated ? mul(obj, part) : part), p.first, p.last);
        } else {
            range(body, p.first, p.last);
        }
    }
    VertexSlot* slots = vertexSlots();
    for (int i = 0; i < L.nv && i < kVertexSlots; ++i) {
        slots[i].x = s32(std::llround(std::clamp(V[i][0] * 65536.0, -2.0e9, 2.0e9)));
        slots[i].y = s32(std::llround(std::clamp(V[i][1] * 65536.0, -2.0e9, 2.0e9)));
        slots[i].z = s32(std::llround(std::clamp(V[i][2] * 65536.0, -2.0e9, 2.0e9)));
    }
}

// r3d_draw_sky_ground (671C) in layer pixels.
void HiFrame::skyGround(const s32 P[3], u8 ground, u8 sky, s16 roll, s16 pitch) const {
    Gfx& gx = gfx();
    const int y0 = vy * N, y1 = (vy + vh) * N - 1;
    const int x0 = vx * N, x1 = (vx + vw) * N - 1;
    auto fillAll = [&](u8 c) { gx.fillRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, u16(Gfx::kSolid | c)); };
    const double z = P[2] / 65536.0;
    const double k = double(N) * double(1 << zoom);
    const double sx = z != 0 ? cxd + k * (P[0] / 65536.0) / z : (P[0] > 0 ? 1e9 : -1e9);
    const double sy = z != 0 ? cyd - k * (P[1] / 65536.0) / z : (P[1] > 0 ? -1e9 : 1e9);
    if (std::abs(sx) >= 15001.0 * N || std::abs(sy) >= 15001.0 * N) {
        fillAll((pitch >= 0 && pitch < 1440) ? sky : ground);
        return;
    }
    const double L = N * std::max(vw * 20.0 / 23.0, vh * 23.0 / 20.0);
    const double a = -roll * (2.0 * 3.14159265358979323846 / kAngleFull);
    const double c = std::cos(a), s = std::sin(a);
    const double Cx = sx, Cy = sy;
    double Ax = Cx - L * c, Ay = Cy - L * s;
    double Bx = Cx + L * c, By = Cy + L * s;
    Ax = Cx + (Ax - Cx) * 23.0 / 20.0;
    Bx = Cx + (Bx - Cx) * 23.0 / 20.0;
    u8 bandLeft = Ay < By ? ground : sky, other = Ay < By ? sky : ground;
    u8 above = Bx < Ax ? ground : sky, below = Bx < Ax ? sky : ground;
    if (pitch > 720 && pitch < 2160) {
        std::swap(bandLeft, other);
        std::swap(above, below);
    }
    double rBx = Bx, rBy = By;
    if (rBy < Cy) {
        rBx = 2 * Cx - rBx;
        rBy = 2 * Cy - rBy;
    }
    const double dy = rBy - Cy;
    const long cyi = std::lround(Cy);
    for (int y = y0; y <= y1; ++y) {
        u8 left, right;
        double xs;
        if (dy < 0.5) {
            const u8 col = y < cyi ? above : y == cyi ? ground : below;
            gx.hline(x0, x1, y, u16(Gfx::kSolid | col));
            continue;
        }
        if (y < Cy - dy) {
            gx.hline(x0, x1, y, u16(Gfx::kSolid | above));
            continue;
        }
        if (y > Cy + dy) {
            gx.hline(x0, x1, y, u16(Gfx::kSolid | below));
            continue;
        }
        xs = Cx + (y - Cy) * (rBx - Cx) / dy;
        left = bandLeft;
        right = other;
        const long xi = std::lround(std::floor(xs));
        if (xi >= x0) gx.hline(x0, int(std::min<long>(xi, x1)), y, u16(Gfx::kSolid | left));
        if (xi + 1 <= x1) gx.hline(int(std::max<long>(xi + 1, x0)), x1, y, u16(Gfx::kSolid | right));
    }
}

}  // namespace st::render
