// Procedural scenery (1000:70B3..784D, docs/re/seg_1000.md 15). See veg.h.
#include "render/veg.h"

#include "data/exeimage.h"
#include "engine/rng.h"
#include "render/r3d.h"
#include "render/r3dmath.h"
#include "render/world.h"

#include <cstdlib>

namespace st::render {

namespace {

namespace of = game::obj3d_flag;

std::vector<game::Obj3D*> g_scatter;  // DS:2FBC pool
std::vector<game::Obj3D*> g_flyers;   // DS:3100 pool
BlockerProbe g_probe = nullptr;
bool g_force = true;                  // DS:ED20
s32 g_coverX = 0, g_coverZ = 0;       // DS:ED18 / ED1C
s32 g_camX = 0, g_camZ = 0;           // DS:CEB2 / CEB6
s16 g_camYaw = 0;                     // DS:CEBA
const game::Vec3* g_pmPos = nullptr;
bool g_pmAboard = false;

int entryType(int type) { return type; }
const game::ModelDesc* tableModel(int type) { return modelByIndex(entryType(type)); }

bool probe(s32 x, s32 z) { return g_probe && g_probe(x, z); }

s32 absDiff(s32 a, s32 b) {
    const s32 d = s32(u32(a) - u32(b));
    return d < 0 ? s32(0u - u32(d)) : d;
}

// veg_random_cover_type (1000:3AC1)
int randomCoverType() {
    auto& r = engine::rng();
    if (r.range(3)) return 0x29;
    if (r.range(2)) return 0x2F;
    if (r.range(2)) return 0x27;
    if (r.range(2)) return 0x2C;
    if (r.range(2)) return 0x2E;
    if (r.range(2)) return 0x2D;
    if (r.range(2)) return 0x28;
    if (r.range(2)) return 0x2A;
    return r.range(2) == 0 ? 0x26 : 0x2B;
}

// veg_random_tree_shape (1000:3A9D)
const game::ModelDesc* randomTreeShape() { return tableModel(engine::rng().range(2) == 0 ? 0x54 : 7); }

// The camera heading snapped as in 1000:752A: wrap(yaw + 45 deg) rounded down to 90 deg.
int snappedYaw(int yaw) {
    const int a = angleWrap(yaw + 0x168);
    return (a & 0xFFF8) - ((a >> 3) % 90) * 8;
}

// Pattern tables in DGROUP: DS:348C scatter (49 pairs), DS:3550 flyers (4), DS:3560 ground cover (98).
void patternAt(u16 base, int i, s16& u, s16& v) {
    u = exe().dgShort(u16(base + 4 * i));
    v = exe().dgShort(u16(base + 4 * i + 2));
}

// veg_place_ground_cover (1000:7181)
void placeGroundCover(const game::Vec3& pos, int detail) {
    const int n = detail < 5 ? 0x31 : 0x62;
    std::vector<WorldObject*> spares;
    for (int i = 0; i < kSpareObjects; ++i)
        if (WorldObject* w = spareObject(i)) spares.push_back(w);
    for (WorldObject* w : spares)
        if (w->body) w->body->flags &= u16(~of::kFreeHeading);
    const s32 bx = s32(u32(pos.x) & 0xFFFF0000u), bz = s32(u32(pos.z) & 0xFFFF0000u);
    for (int k = 0; k < n; ++k) {
        s16 ox, oz;
        patternAt(0x3560, k, ox, oz);
        const s32 cx = s32(u32(bx) + (u32(s32(ox)) << 8)), cz = s32(u32(bz) + (u32(s32(oz)) << 8));
        bool found = false;
        if (!g_force) {
            for (WorldObject* w : spares) {
                game::Obj3D* b = w->body;
                if (!b || (b->flags & of::kFreeHeading)) continue;
                if (((u32(b->pos.x) ^ u32(cx)) & 0x7FFF8000u) == 0 && ((u32(b->pos.z) ^ u32(cz)) & 0x7FFF8000u) == 0) {
                    b->flags |= 0x2001;
                    b->pos.y = 0;
                    found = true;
                    break;
                }
            }
        }
        if (found) continue;
        // fx_pool_alloc_wrapped: first spare whose body is not visible
        WorldObject* w = nullptr;
        for (WorldObject* s : spares)
            if (s->body && !(s->body->flags & of::kEnabled)) {
                w = s;
                break;
            }
        if (!w) continue;
        game::Obj3D* b = w->body;
        if (probe(cx, cz)) {
            b->flags &= u16(~of::kEnabled);
            continue;
        }
        b->flags |= 0x2001;
        auto& r = engine::rng();
        b->pos.x = s32(u32(cx) + (u32(r.range(0x80)) << 8));
        b->pos.z = s32(u32(cz) + (u32(r.range(0x80)) << 8));
        b->pos.y = 0;
        const int type = randomCoverType();
        const game::ModelTableEntry& e = modelTable(type);
        b->model = e.desc;
        w->model = e.desc;
        w->kind = e.kind;
        w->flags = 0;
        w->hit_points = 100;
        const u16 kind = u16(w->kind);
        if (kind == 3 || kind == 0x11) {
            b->heading = s16(r.range(0x168) << 3);
            w->cover = kind == 0x11 ? 0x4B : 0x19;
            w->height = 0x3F;
        } else {
            b->heading = s16(r.range(0x168) << 3);
            w->cover = kind == 6 ? 0x32 : (modelDsOffset(w->model) == 0x9350 ? 99 : 100);
            w->height = 0x7F;
        }
    }
    for (WorldObject* w : spares) {
        game::Obj3D* b = w->body;
        if (b && !(b->flags & of::kFreeHeading)) {
            b->flags &= u16(~of::kEnabled);
            b->pos.x = 0;
            b->pos.z = 0;
        }
    }
    g_force = false;
}

// veg_place_camera_scatter (1000:74BF)
void placeCameraScatter(const game::Camera& cam) {
    for (game::Obj3D* o : g_scatter) o->flags &= u16(~of::kFreeHeading);
    const s32 bx = s32(u32(cam.pos.x) & 0xFFFFC000u), bz = s32(u32(cam.pos.z) & 0xFFFFC000u);
    const int yaw = snappedYaw(cam.yaw);
    for (int k = 0; k < 49; ++k) {
        s16 u, v;
        patternAt(0x348C, k, u, v);
        v = s16(v - 0x40);
        mathRotate2d(u, v, 0, 0, yaw);
        const s32 cx = s32(u32(bx) + (u32(s32(s16(u & 0xFFC0))) << 8));
        const s32 cz = s32(u32(bz) + (u32(s32(s16(v & 0xFFC0))) << 8));
        game::Obj3D* found = nullptr;
        for (game::Obj3D* o : g_scatter) {
            if (o->flags & of::kFreeHeading) continue;
            if ((u32(o->pos.x) & 0xFFFFE000u) == u32(cx) && (u32(o->pos.z) & 0xFFFFE000u) == u32(cz)) {
                found = o;
                break;
            }
        }
        if (found) {
            found->flags |= of::kFreeHeading;
            found->pos.y = 0;
            continue;
        }
        game::Obj3D* o = nullptr;
        for (game::Obj3D* p : g_scatter)
            if (!(p->flags & of::kEnabled)) {
                o = p;
                break;
            }
        if (!o) continue;
        auto& r = engine::rng();
        o->flags |= 0x2001;
        o->pos.x = s32(u32(cx) + (u32(r.range(0xF)) << 8));
        o->pos.z = s32(u32(cz) + (u32(r.range(0xF)) << 8));
        o->pos.y = 0;
        o->heading = s16(r.range(0x167) << 3);
        o->model = randomTreeShape();
        if (probe(cx, cz)) o->flags &= u16(~of::kEnabled);
    }
    for (game::Obj3D* o : g_scatter) {
        if (!(o->flags & of::kFreeHeading)) o->flags &= u16(~of::kEnabled);
        else o->flags &= u16(~of::kFreeHeading);
    }
}

// veg_setup_distant_tree (1000:3B57) without mission state: the bird branch
// needs a nearby enemy distance and the Point Man's exhaustion (mission).
void setupFlyer(game::Obj3D* o) {
    auto& r = engine::rng();
    const int h = (r.range(0x168) | 1) << 3;
    o->heading = s16(h);
    int type;
    if (r.range(2)) {
        type = 0x52;
    } else if (r.range(10)) {
        type = 0x53;
        o->heading = s16(o->heading & 0xFFF0);
        o->pos.y = 0x300;
    } else {
        type = 0x37 + r.range(2);
        o->pos.y = type == 0x37 ? 0x5DC00 : 0xBB800;
        // pos_move_polar(pos, heading + 180 deg, 0, 0x2DB400): far away behind
        const int a = angleWrap(h + 0x5A0);
        o->pos.x -= s32(((long long)(0x2DB400) * mathSin(a)) >> 14);
        o->pos.z += s32(((long long)(0x2DB400) * mathCos(a)) >> 14);
    }
    o->model = tableModel(type);
}

// veg_place_distant_trees (1000:770B)
void placeFlyers(const game::Camera& cam, int detail) {
    if (detail <= 1) return;
    const s32 bx = s32(u32(cam.pos.x) & 0xFFFFC000u), bz = s32(u32(cam.pos.z) & 0xFFFFC000u);
    const int yaw = snappedYaw(cam.yaw);
    for (int k = 0; k < 4; ++k) {
        s16 u, v;
        patternAt(0x3550, k, u, v);
        mathRotate2d(u, v, 0, 0, yaw);
        const s32 cx = s32(u32(bx) + (u32(s32(s16(u & 0xFFC0))) << 8));
        const s32 cz = s32(u32(bz) + (u32(s32(s16(v & 0xFFC0))) << 8));
        game::Obj3D* o = nullptr;
        for (game::Obj3D* p : g_flyers)
            if (!(p->flags & of::kEnabled)) {
                o = p;
                break;
            }
        if (!o) continue;
        auto& r = engine::rng();
        o->flags |= 0x2001;
        o->pos.x = s32(u32(cx) + (u32(r.range(0x80)) << 8));
        o->pos.z = s32(u32(cz) + (u32(r.range(0x80)) << 8));
        o->pos.y = 0xC00;
        setupFlyer(o);
    }
}

}  // namespace

void vegSetPools(const std::vector<game::Obj3D*>& scatter, const std::vector<game::Obj3D*>& flyers) {
    g_scatter = scatter;
    g_flyers = flyers;
}

void vegCreatePools() {
    std::vector<game::Obj3D*> s, f;
    for (int i = 0; i < 80; ++i) s.push_back(worldAddObject(modelByIndex(7), 24000, 24000, 0));
    for (int i = 0; i < 8; ++i) f.push_back(worldAddObject(modelByIndex(0x51), 24000, 24000, 0));
    vegSetPools(s, f);
}

void setBlockerProbe(BlockerProbe fn) { g_probe = fn; }
void vegSetPointMan(const game::Vec3* pos, bool aboard) {
    g_pmPos = pos;
    g_pmAboard = aboard;
}

// veg_reset (1000:70B3)
void vegReset() {
    for (game::Obj3D* o : g_scatter) {
        o->flags &= u16(~of::kEnabled);
        o->pos.x = o->pos.z = 0x3A9800;
    }
    g_camX = g_camZ = 0;
    for (game::Obj3D* o : g_flyers) o->flags &= u16(~of::kEnabled);
    for (int i = 0; i < kSpareObjects; ++i)
        if (WorldObject* w = spareObject(i); w && w->body) {
            w->body->flags &= u16(~of::kEnabled);
            w->body->pos.x = w->body->pos.z = 0;
        }
    g_coverX = g_coverZ = 0;
    g_force = true;
}

// veg_update (1000:784D)
void vegUpdate(const game::Camera& cam, bool force) {
    const RenderContext& ctx = renderContext();
    const int detail = ctx.detail;
    if (detail >= 2) {
        const game::Vec3 pm = g_pmPos ? *g_pmPos : cam.pos;
        bool place = g_force;
        if (!place && !g_pmAboard && pm.y <= 0x12BFF)
            place = absDiff(g_coverX, pm.x) > 0x8000 || absDiff(g_coverZ, pm.z) > 0x8000;
        if (place) {
            g_coverX = pm.x;
            g_coverZ = pm.z;
            placeGroundCover(pm, detail);
        }
    }
    if (ctx.viewMode == 1) return;
    if (!force) {
        if (cam.pos.y > 0x12BFF || g_pmAboard) return;
        // evt_heading_diff (2dbd:0006): difference in whole degrees
        int dh = std::abs((cam.yaw >> 3) - (g_camYaw >> 3));
        if (dh > 180) dh = 360 - dh;
        if (absDiff(g_camX, cam.pos.x) <= 0x2000 && absDiff(g_camZ, cam.pos.z) <= 0x2000 && dh <= 15) return;
    }
    if (detail >= 3) {
        g_camX = cam.pos.x;
        g_camZ = cam.pos.z;
        g_camYaw = cam.yaw;
        placeCameraScatter(cam);
    }
    if (force) placeFlyers(cam, detail);
}

}  // namespace st::render
