#include "render/sky.h"

#include "data/ealib.h"
#include "data/exeimage.h"
#include "core/settings.h"
#include "engine/ticker.h"
#include "gfx/gfx.h"
#include "render/r3d.h"
#include "render/r3dhires.h"
#include "render/world.h"

#include <algorithm>
#include <string>

namespace st::render {

namespace {

u8 g_gradSky[256];
u8 g_gradGround[256];
u8 g_remap[4][256];
bool g_loaded = false;
SkyState g_sky;

bool loadTable(const std::string& name, u8* out) {
    std::vector<u8> d;
    if (!resources().read(name, d) || d.size() < 256) {
        logError("sky: cannot read %s", name.c_str());
        return false;
    }
    std::copy_n(d.begin(), 256, out);
    return true;
}

}  // namespace

bool skyLoad() {
    if (g_loaded) return true;
    bool ok = loadTable("gradsky1.bin", g_gradSky) && loadTable("gradgrn1.bin", g_gradGround);
    for (int i = 0; i < 4 && ok; ++i) ok = loadTable("remap" + std::to_string(i + 2) + ".bin", g_remap[i]);
    g_loaded = ok;
    return ok;
}

const u8* remapTable(int i) { return (i >= 0 && i < 4 && g_loaded) ? g_remap[i] : nullptr; }
const u8* gradSkyTable() { return g_loaded ? g_gradSky : nullptr; }
const u8* gradGroundTable() { return g_loaded ? g_gradGround : nullptr; }

SkyState& skyState() { return g_sky; }

int todPaletteIndex(int hour, int minute) {
    if (hour > 4) {
        if (hour < 9) {
            g_sky.skyOfs = (hour - 4) * 0x20 + (s8(minute) >> 1);
            g_sky.groundOfs = g_sky.skyOfs;
            return 2;
        }
        if (hour < 18) {
            g_sky.skyOfs = g_sky.groundOfs = 0x80;
            return 0;
        }
        if (hour < 21) {
            g_sky.skyOfs = g_sky.groundOfs = 0x80;
            return 2;
        }
    }
    g_sky.skyOfs = g_sky.groundOfs = 0;
    return 1;
}

std::string todPaletteName(int index) {
    // Palette names: near pointers at DS:0102 (0 pal2, 1 paln, 2 pals, ...).
    return exe().dgStringPtr(u16(0x0102 + 2 * index)) + ".pal";
}

void todDrawSkyGround(const game::Camera& cam, int detail, s32 time, int hour) {
    World& w = world();
    w.skyColor = 0xFF;
    w.groundColor = 0xFF;
    const int horizon = (cam.rect_h >> 1) + cam.rect_y;
    engine::ticker().frameLimitWait();
    Gfx& gx = gfx();
    HiResLayer* layer = detail >= 2 ? hiResLayerForDrawPage() : nullptr;
    // Full-screen 3D (Enhanced): the layer's gradient covers the page's whole
    // height (the rows of the HUD bands and, in a tall window, the layer rows
    // beyond the page), the page keeps the clipped rows of the original.
    const bool full = layer && renderContext().fullScreen3d && settings().effectiveFullScreen3d();
    Bitmap hb;
    auto row = [&](int y, u8 c) {
        gx.hline(0, 0x13F, y, u16(Gfx::kSolid | c));
        if (layer) {
            // Same rows in the high-resolution layer (the viewport shows the
            // layer once the 3D view has been rendered on top), across the
            // whole width of the (possibly widened) view.
            const int x0 = gx.clipX0(), x1 = gx.clipX1();
            const int y0 = full ? 0 : gx.clipY0(), y1 = full ? 199 : gx.clipY1();
            if (y < y0 || y > y1) return;
            int X0, Y0, X1, Y1;
            layer->rectOf(x0, y, x1, y, true, X0, Y0, X1, Y1);
            if (full && y == 0) Y0 = 0;
            if (full && y == 199) Y1 = layer->h - 1;
            // layerTargetBegin clips to the page clip's layer rectangle (the
            // camera rect): in full-screen 3D the rows of the bands need the
            // page's whole height.
            const int cy0 = gx.clipY0(), ch = gx.clipY1() - cy0 + 1;
            if (full) gx.setClip(x0, 0, x1 - x0 + 1, 200);
            if (layerTargetBegin(*layer, hb)) {
                for (int Y = Y0; Y <= Y1; ++Y) gx.hline(X0, X1, Y, u16(Gfx::kSolid | c));
                layerTargetEnd();
            }
            if (full) gx.setClip(x0, cy0, x1 - x0 + 1, ch);
        }
    };
    // 100 rows above and below the horizon reach the bands only in full-screen
    // 3D (the horizon is row 93; the gradient saturates beyond its tables).
    const int rows = full ? 200 : 100;
    for (int i = 0; i < rows; ++i) {
        const int d = detail >= 3 ? i : 0;
        const int ys = horizon - i - 1;
        if (ys >= 0) {
            int k = g_sky.skyOfs - d;
            if (k < (g_sky.skyOfs >> 2)) k = g_sky.skyOfs >> 2;
            u8 c = g_gradSky[k & 0xFF];
            c = std::clamp<u8>(c, 0x80, 0x9F);
            if (detail < 2) {
                if (i == 0) w.skyColor = c;
            } else {
                row(ys, c);
            }
        }
        const int yg = horizon + i;
        if (yg < 200) {
            int k = g_sky.groundOfs + d;
            if (k > 0xFF) k = 0xFF;
            u8 c = g_gradGround[k & 0xFF];
            c = std::clamp<u8>(c, 0x20, 0x5F);
            if (detail < 2) {
                w.groundColor = c;
                break;
            }
            row(yg, c);
        }
    }
    // Brighten the dawn gradient every 0xF00 ticks.
    const s32 elapsed = s32(u32(time) - u32(g_sky.dawnTime));
    bool tick = false;
    if (elapsed > 0xEFF) {
        g_sky.dawnTime = time;
        tick = time != 0;
    }
    if (tick && hour > 4 && hour < 8 && g_sky.skyOfs < 0xFF) {
        if (++g_sky.skyOfs > 0xFF) g_sky.skyOfs = 0;
        if (++g_sky.groundOfs > 0xFF) g_sky.groundOfs = 0;
    }
}

void viewClearMapGround(const game::Camera& cam) {
    engine::ticker().frameLimitWait();
    Gfx& gx = gfx();
    const u16 c = u16(Gfx::kSolid | g_gradGround[0xB6]);
    gx.fillRect(cam.rect_x, cam.rect_y, cam.rect_w, cam.rect_h, c);
    // Enhanced: the map view is rendered into the high-resolution layer, which
    // must start from the same ground colour (like the gradient rows above).
    if (HiResLayer* layer = hiResLayerForDrawPage()) {
        Bitmap hb;
        int X0, Y0, X1, Y1;
        layer->rectOf(cam.rect_x, cam.rect_y, cam.rect_x + cam.rect_w - 1, cam.rect_y + cam.rect_h - 1, true, X0,
                      Y0, X1, Y1);
        if (layerTargetBegin(*layer, hb)) {
            gx.fillRect(X0, Y0, X1 - X0 + 1, Y1 - Y0 + 1, c);
            layerTargetEnd();
        }
    }
}

}  // namespace st::render
