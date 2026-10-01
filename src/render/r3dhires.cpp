#include "render/r3dhires.h"

#include "core/settings.h"
#include "gfx/gfx.h"
#include "platform/system.h"
#include "render/model.h"
#include "render/r3d.h"

#include <algorithm>
#include <array>
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

void bitmapOf(HiResLayer& l, Bitmap& b) {
    b.flags = 0;
    b.w = l.w;
    b.h = l.h;
    b.bpr = l.w / 4;
    b.data = l.pixels.data();
}

// Page clip saved by layerTargetBegin.
int g_saveX0, g_saveY0, g_saveW, g_saveH;

}  // namespace

HiResLayer* hiResLayerForDrawPage() {
    const Settings& s = settings();
    if (s.original()) return nullptr;
    HiResLayer& l = sys().video().activateHiResLayer(gfx().drawPage(), s.effectiveRenderScale(), s.effectiveWideView());
    gfx().refreshCoverage();
    return &l;
}

bool layerTargetBegin(HiResLayer& l, Bitmap& bmp) {
    Gfx& gx = gfx();
    if (gx.target()) return false;
    bitmapOf(l, bmp);
    g_saveX0 = gx.clipX0();
    g_saveY0 = gx.clipY0();
    g_saveW = gx.clipX1() - gx.clipX0() + 1;
    g_saveH = gx.clipY1() - gx.clipY0() + 1;
    int X0, Y0, X1, Y1;
    l.rectOf(gx.clipX0(), gx.clipY0(), gx.clipX1(), gx.clipY1(), true, X0, Y0, X1, Y1);
    gx.setTarget(&bmp);
    gx.setClip(X0, Y0, X1 - X0 + 1, Y1 - Y0 + 1);
    return true;
}

void layerTargetEnd() {
    Gfx& gx = gfx();
    gx.setTarget(nullptr);
    gx.setClip(g_saveX0, g_saveY0, g_saveW, g_saveH);
}

void layerShowPage(HiResLayer& l) {
    std::fill(l.coverage.begin(), l.coverage.end(), u8(1));
    std::fill(l.extRows.begin(), l.extRows.end(), u8(1));
    l.extX0 = 0;
    l.extY0 = 0;
    l.extX1 = l.w - 1;
    l.extY1 = l.h - 1;
    gfx().refreshCoverage();
    sys().video().markDirty();
}

void HiFrame::begin(int rx, int ry, int rw, int rh, int ccx, int ccy, int zoomShift, s32 cx32, s32 cy32,
                    s32 cz32, bool fullScreen) {
    const Settings& s = settings();
    on = !s.original();
    fullHeight = on && fullScreen && s.effectiveFullScreen3d();
    vx = rx;
    vy = ry;
    vw = rw;
    vh = rh;
    clipCx = ccx;
    clipCy = ccy;
    zoom = zoomShift;
    camX = cx32;
    camY = cy32;
    camZ = cz32;
    if (!on) {
        cvx = rx, cvy = ry, cvw = rw, cvh = rh;
        return;
    }
    page = gfx().drawPage();
    layer = &sys().video().activateHiResLayer(page, s.effectiveRenderScale(), s.effectiveWideView());
    kx = layer->scaleX();
    ky = layer->scaleY();
    ox = layer->ox;
    oy = layer->oy;
    // Full-screen 3D: the view covers the page's whole height (the HUD bands
    // included; the sides as in wide view), the camera rect only fixes the
    // projection centre.
    if (fullHeight) {
        vy = 0;
        vh = 200;
    }
    layer->rectOf(vx, vy, vx + vw - 1, vy + vh - 1, true, X0, Y0, X1, Y1);
    // Original pixel p covers layer pixels ox + p*kx ..; its centre is the
    // projection of original coordinate p.
    cxd = toLayerX(clipCx);
    cyd = toLayerY(clipCy);
    cx = int(std::lround(cxd));
    cy = int(std::lround(cyd));
    // The page rectangle that covers the (possibly extended) layer viewport:
    // the frustum, culling and clipping work with this width and height.
    cvx = layer->pageX(X0);
    cvy = layer->pageY(Y0);
    if (X0 < layer->ox) cvx = -int(std::ceil((layer->ox - X0) / kx));
    if (Y0 < layer->oy) cvy = -int(std::ceil((layer->oy - Y0) / ky));
    int cvx1 = layer->pageX(X1), cvy1 = layer->pageY(Y1);
    if (X1 >= layer->ox + layer->pw) cvx1 = 319 + int(std::ceil((X1 + 1 - (layer->ox + layer->pw)) / kx));
    if (Y1 >= layer->oy + layer->ph) cvy1 = 199 + int(std::ceil((Y1 + 1 - (layer->oy + layer->ph)) / ky));
    if (fullHeight) {
        // The frustum and the clip shifts are symmetric around the projection
        // centre (half width / height): make the virtual clip symmetric around
        // the camera rect's centre, which the page rows are not (8 rows above
        // the main view, 21 below), so the larger side is never culled.
        const int half = std::max(clipCy - cvy, cvy1 - clipCy);
        cvy = clipCy - half;
        cvy1 = clipCy + half;
    }
    cvw = cvx1 - cvx + 1;
    cvh = cvy1 - cvy + 1;
    thick = std::max(1, int(std::lround(std::min(kx, ky))));
    bitmapOf(*layer, bmp);
}

void HiFrame::beginDraw() {
    if (!on) return;
    Gfx& gx = gfx();
    gx.setTarget(&bmp);
    gx.setClip(X0, Y0, X1 - X0 + 1, Y1 - Y0 + 1);
}

void HiFrame::end() {
    if (!on) return;
    Gfx& gx = gfx();
    gx.setTarget(nullptr);
    gx.refreshCoverage();
    // The viewport now shows the layer. The page gets a point-sampled copy
    // so that code reading the page (page copies, blits) sees the 3D view.
    u8* pagePix = gx.page(page).data;
    HiResLayer& l = *layer;
    for (int y = std::max(vy, 0); y < std::min(vy + vh, 200); ++y) {
        const int Y = (l.rowStart[size_t(y)] + l.rowStart[size_t(y) + 1]) / 2;
        l.extRows[size_t(y)] = 1;
        for (int x = std::max(vx, 0); x < std::min(vx + vw, 320); ++x) {
            const int X = (l.colStart[size_t(x)] + l.colStart[size_t(x) + 1]) / 2;
            l.coverage[size_t(y * 320 + x)] = 1;
            pagePix[y * 320 + x] = l.pixels[size_t(Y) * size_t(l.w) + size_t(X)];
        }
    }
    l.extX0 = X0;
    l.extY0 = Y0;
    l.extX1 = X1;
    l.extY1 = Y1;
    l.hudOnce = 0;
    sys().video().markDirty();
}

namespace {

// Darkening table of the current DAC palette: index -> the palette entry
// nearest to 45 % of its colour.
struct DarkTable {
    std::array<u8, 768> pal{};
    bool valid = false;
    u8 lut[256];

    const u8* get() {
        const u8* dac = sys().video().palette();
        if (valid && std::equal(pal.begin(), pal.end(), dac)) return lut;
        std::copy_n(dac, 768, pal.begin());
        valid = true;
        for (int i = 0; i < 256; ++i) {
            const int tr = pal[size_t(i * 3)] * 45 / 100, tg = pal[size_t(i * 3 + 1)] * 45 / 100,
                      tb = pal[size_t(i * 3 + 2)] * 45 / 100;
            int best = 0, bestD = 1 << 30;
            for (int j = 0; j < 256; ++j) {
                const int dr = pal[size_t(j * 3)] - tr, dg = pal[size_t(j * 3 + 1)] - tg, db = pal[size_t(j * 3 + 2)] - tb;
                const int d = 2 * dr * dr + 3 * dg * dg + db * db;
                if (d < bestD) {
                    bestD = d;
                    best = j;
                }
            }
            lut[i] = u8(best);
        }
        return lut;
    }
};

DarkTable g_dark;

}  // namespace

void hudBackingRect(int x, int y, int w, int h, u8 once) {
    if (w <= 0 || h <= 0) return;
    HiResLayer* l = sys().video().hiResLayer(gfx().drawPage());
    if (!l) return;
    // The page pixels under the strip show the layer again: what was drawn
    // there before (the previous digits of the time-compression clock, which
    // the original wipes with its black box) disappears, the element drawn
    // next uncovers its own pixels.
    for (int py = std::max(y, 0); py < std::min(y + h, kScreenH); ++py)
        for (int px = std::max(x, 0); px < std::min(x + w, kScreenW); ++px) l->coverage[size_t(py * kScreenW + px)] = 1;
    if (once) {
        if (l->hudOnce & once) {
            sys().video().markDirty();
            return;
        }
        l->hudOnce |= once;
    }
    int X0, Y0, X1, Y1;
    l->rectOf(x, y, x + w - 1, y + h - 1, false, X0, Y0, X1, Y1);
    X0 = std::max(X0, 0);
    Y0 = std::max(Y0, 0);
    X1 = std::min(X1, l->w - 1);
    Y1 = std::min(Y1, l->h - 1);
    if (X1 < X0 || Y1 < Y0) return;
    const u8* lut = g_dark.get();
    for (int Y = Y0; Y <= Y1; ++Y) {
        u8* row = &l->pixels[size_t(Y) * size_t(l->w)];
        for (int X = X0; X <= X1; ++X) row[X] = lut[row[X]];
    }
    sys().video().markDirty();
}

void HiFrame::project(s32 x, s32 y, s32 z, s16& sx, s16& sy) const {
    const double f = double(1 << zoom);
    if (z == 0) {
        sx = x > 0 ? 32000 : x < 0 ? -32000 : s16(cx);
        sy = y > 0 ? -32000 : y < 0 ? 32000 : s16(cy);
        return;
    }
    sx = clamp16(cxd + kx * f * double(x) / double(z));
    sy = clamp16(cyd - ky * f * double(y) / double(z));
}

void HiFrame::line(int x0, int y0, int x1, int y1, u16 c) const {
    Gfx& gx = gfx();
    const bool xMajor = std::abs(y0 - y1) < std::abs(x0 - x1);
    for (int k = 0; k < thick; ++k) {
        const int o = k - (thick - 1) / 2;
        if (xMajor) gx.line(x0, y0 + o, x1, y1 + o, c);
        else gx.line(x0 + o, y0, x1 + o, y1, c);
    }
}

void HiFrame::point(int x, int y, u16 c) const {
    const int o = (thick - 1) / 2;
    gfx().fillRect(x - o, y - o, thick, thick, c);
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

// r3d_draw_sky_ground (671C): the horizon geometry in page pixels (double),
// rasterised per layer row.
void HiFrame::skyGround(const s32 P[3], u8 ground, u8 sky, s16 roll, s16 pitch) const {
    Gfx& gx = gfx();
    auto fillAll = [&](u8 c) { gx.fillRect(X0, Y0, X1 - X0 + 1, Y1 - Y0 + 1, u16(Gfx::kSolid | c)); };
    const double z = P[2] / 65536.0;
    const double f = double(1 << zoom);
    const double Cx = z != 0 ? clipCx + f * (P[0] / 65536.0) / z : (P[0] > 0 ? 1e9 : -1e9);
    const double Cy = z != 0 ? clipCy - f * (P[1] / 65536.0) / z : (P[1] > 0 ? -1e9 : 1e9);
    if (std::abs(Cx) >= 15001.0 || std::abs(Cy) >= 15001.0) {
        fillAll((pitch >= 0 && pitch < 1440) ? sky : ground);
        return;
    }
    const double L = std::max(cvw * 20.0 / 23.0, cvh * 23.0 / 20.0);
    const double a = -roll * (2.0 * 3.14159265358979323846 / kAngleFull);
    const double c = std::cos(a), s = std::sin(a);
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
    for (int Y = Y0; Y <= Y1; ++Y) {
        const double y = toPageY(Y);
        if (dy < 0.5) {
            const u8 col = y < Cy - 0.5 ? above : y < Cy + 0.5 ? ground : below;
            gx.hline(X0, X1, Y, u16(Gfx::kSolid | col));
            continue;
        }
        if (y < Cy - dy) {
            gx.hline(X0, X1, Y, u16(Gfx::kSolid | above));
            continue;
        }
        if (y > Cy + dy) {
            gx.hline(X0, X1, Y, u16(Gfx::kSolid | below));
            continue;
        }
        const double xs = toLayerX(Cx + (y - Cy) * (rBx - Cx) / dy);
        const long xi = std::lround(std::floor(xs));
        if (xi >= X0) gx.hline(X0, int(std::min<long>(xi, X1)), Y, u16(Gfx::kSolid | bandLeft));
        if (xi + 1 <= X1) gx.hline(int(std::max<long>(xi + 1, X0)), X1, Y, u16(Gfx::kSolid | other));
    }
}

}  // namespace st::render
