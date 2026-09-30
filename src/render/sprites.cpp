// Billboard sprites: the type-4 primitive callback spr_draw_billboard_cb
// (348e:0C52) with the soldier frame selection and drawing of segment 348e
// and the scenery helpers of segment 1000 (docs/re/seg_2dbd.md 4, seg_1000.md 16).
#include "render/sprites.h"

#include "data/ealib.h"
#include "engine/rng.h"
#include "gfx/gfx.h"
#include "render/r3d.h"
#include "render/r3dmath.h"
#include "render/sky.h"
#include "render/world.h"

#include <cmath>
#include <cstring>
#include <map>
#include <string>

namespace st::render {

namespace {

struct SpriteSet {
    std::string name;
    int frames = 0;
    std::vector<SpriteImage> img;  // frames * 8
};

std::map<std::string, SpriteSet> g_sets;
std::vector<u8> g_texJungle, g_texBush, g_texGrass, g_texImpact;  // DS:EC72..EC80
SpriteImage g_fxmu[8];
bool g_loaded = false;
UnitLookup g_unitLookup = nullptr;
EffectPainter g_effectPainter = nullptr;
AnimUpdate g_animUpdate = nullptr;
const game::Unit* g_pointMan = nullptr;
int g_rotation = 0;  // DS:1284

// Soldier sets in the order of 348e:19B0 and their frame counts.
struct SetDef {
    const char* name;
    int frames;
};
constexpr SetDef kSoldierSets[] = {
    {"u", 1},  {"c", 1},  {"p", 1},  {"us", 8}, {"uq", 6}, {"cs", 6}, {"ps", 4}, {"cp", 2},
    {"uf", 2}, {"ca", 2}, {"pi", 1}, {"cf", 1}, {"ua", 2}, {"uc", 2}, {"up", 2}, {"pa", 2},
    {"cr", 2}, {"cw", 2}, {"uw", 2}, {"d", 1},  {"ds", 4}, {"dp", 2},
};
// Headgear h = 1..7 (DS:1D4E): band, bere(t), bush, flop(py), hbnd, coni(cal), pith.
constexpr const char* kHeadgear[] = {"band", "bere", "bush", "flop", "hbnd", "coni", "pith"};

bool loadImage(const std::string& base, SpriteImage& out, bool withRlx) {
    if (!resources().read(base + ".rle", out.rle)) {
        out.rle.clear();
        return false;
    }
    std::vector<u8> r;
    if (withRlx && resources().read(base + ".rlx", r) && r.size() >= 12) {
        out.rlx.zero_00 = rd16(&r[0]);
        out.rlx.anchor_x = rd16(&r[2]);
        out.rlx.anchor_y = rd16(&r[4]);
        out.rlx.head_frame = r[6];
        out.rlx.layer = r[7];
        out.rlx.head_x = rds16(&r[8]);
        out.rlx.head_y = rds16(&r[10]);
        out.hasRlx = true;
    }
    return true;
}

void loadSet(const char* name, int frames, bool withRlx) {
    SpriteSet s;
    s.name = name;
    s.frames = frames;
    s.img.resize(size_t(frames) * 8);
    for (int f = 0; f < frames; ++f)
        for (int r = 0; r < 8; ++r)
            loadImage(std::string(name) + "f" + std::to_string(f + 1) + "r" + std::to_string(r + 1),
                      s.img[size_t(f * 8 + r)], withRlx);
    g_sets[name] = std::move(s);
}

// math_mul_8_8 (2255:1062): (a * b) >> 8.
int mul88(int a, int b) { return s16((s32(s16(a)) * s16(b)) >> 8); }

int sprW(const u8* rle) { return rle ? rd16(rle) : 0; }
int sprH(const u8* rle) { return rle ? rd16(rle + 2) : 0; }

// spr_sprite_topleft (348e:03BC)
void topLeft(int x, int y, int w, int h, int s, int& l, int& t) {
    l = x - (s16(w) >> 1);
    t = y + mul88(5, s) - h;
}

// spr_draw_scaled (348e:03ED)
void drawScaled(int x, int y, int s, const u8* rle) {
    const int w = mul88(sprW(rle), s), h = mul88(sprH(rle), s);
    int l, t;
    topLeft(x, y, w, h, s, l, t);
    gfx().spriteScaled(l, t, w, h, rle);
}

const SpriteImage* frameOf(const char* set, int f, int r) {
    const auto it = g_sets.find(set);
    if (it == g_sets.end()) return nullptr;
    const SpriteSet& s = it->second;
    if (f < 0 || f >= s.frames || r < 0 || r > 7) return nullptr;
    const SpriteImage& img = s.img[size_t(f * 8 + r)];
    return img.rle.empty() ? nullptr : &img;
}

// Lowers the clip bottom to the ground line while drawing a body part.
struct GroundClip {
    int saved;
    explicit GroundClip(int line) : saved(gfx().clipY1()) {
        if (line < saved) gfx().setClipBottom(line);
    }
    ~GroundClip() { gfx().setClipBottom(saved); }
};

// spr_draw_body (348e:07CC)
void drawBody(int x, int y, int s, const SpriteImage& img, int depth) {
    const int w = img.w(), h = img.h();
    const int X = x + mul88((s16(w) >> 1) - img.rlx.anchor_x, s);
    int Y = y + mul88(h - img.rlx.anchor_y, s);
    const int sh = mul88(h, s);
    int l, t;
    topLeft(x, y, 2, sh, s, l, t);
    GroundClip gc(sh + t);
    Y += sh - s16(s16((8 - depth) * sh) >> 3);
    drawScaled(X, Y, s, img.rle.data());
}

// spr_draw_helmet (348e:065C)
void drawHelmet(int x, int y, int s, const SpriteImage& body, int hat, int headRot, int depth) {
    if (hat == 0) return;
    const SpriteImage* hi = frameOf(kHeadgear[(hat - 1) % 7], body.rlx.head_frame, headRot);
    if (!hi) return;
    const int bw = body.w(), bh = body.h();
    int L, T;
    topLeft(x, y, mul88(bw, s), mul88(bh, s), s, L, T);
    L += mul88((s16(bw) >> 1) - body.rlx.anchor_x, s);
    T += mul88(bh - body.rlx.anchor_y, s);
    const int hsh = mul88(bh, s);
    int l2, t2;
    topLeft(x, y, 2, hsh, s, l2, t2);
    GroundClip gc(hsh + t2);
    T += hsh - s16(s16((8 - depth) * hsh) >> 3);
    L += mul88(body.rlx.head_x, s);
    T += mul88(body.rlx.head_y, s);
    if (s < 0x100) T += mul88(body.rlx.head_y, s) & 1;
    drawScaled(L, T, s, hi->rle.data());
}

// spr_calc_rotation (348e:05B9)
int calcRotation(int heading, const game::Vec3& pos) {
    const game::Vec3& cam = renderContext().cameraPos;
    const int bearing = (cam.x == pos.x && cam.z == pos.z && &cam == &pos) ? 0 : (mathHeading(cam.x, cam.z, pos.x, pos.z) >> 3);
    // geo_relative_bearing (1000:3392)
    int r = -((s16(heading) >> 3) - 180);
    if (r < 0) r += 360;
    r += s16(bearing << 3) >> 3;
    if (r > 359) r -= 360;
    r += 22;
    if (r >= 360) r -= 360;
    const int v = (r * 8) / 360;
    return v < 0 ? -v : v;
}

// spr_draw_grass_tuft (348e:0443)
void drawGrassTuft(int x, int y, int s, const SpriteImage& body, const game::Unit* u, int depth) {
    const game::Mover* m = u->mover;
    if (!m || u8(m->move_mode) != 2) return;
    if (!(m->flags & 0x04) && depth == 0) return;
    if (u->team && (int(u->team->type) == 4 || int(u->team->type) == 5) && !(m->flags & 0x20)) return;
    if (g_texGrass.empty()) return;
    const int w = mul88(body.w(), s), h = mul88(body.h(), s);
    int L, T;
    topLeft(x, y, w, h, s, L, T);
    int gw = w;
    if (gw != 0) gw -= engine::rng().range(2);
    int gh = mul88(sprH(g_texGrass.data()), s);
    if (gh != 0) gh -= engine::rng().range(std::max(1, int(u8(m->move_mode)) * 2));
    gfx().setSpriteRemap(remapTable(2));
    gfx().spriteScaled(L, T + h - (s16(gh) >> 1), gw, gh, g_texGrass.data());
    gfx().setSpriteRemap(nullptr);
}

// spr_draw_soldier_frame (348e:089F)
void drawSoldierFrame(int x, int y, int s, const char* set, int f, int r, const game::Unit* u) {
    int hat = 0;
    const int ttype = u->team ? int(u->team->type) : -1;
    if (g_pointMan && u == g_pointMan) hat = 2;
    else if (ttype == 4 || ttype == 5 || ttype == 6) hat = ttype == 5 ? 7 : 6;
    else if (ttype == 0 && u->se) {
        const int v = u->se->camouflage % 6;
        hat = (v == 0 || v == 1) ? 3 : v;
    }
    int headRot = r;
    if (u->mover && u->body) {
        headRot = calcRotation(u->mover->aim_heading << 3, u->body->pos);
        if (headRot > r) headRot = std::abs(r + 1) % 8;
        else if (headRot < r) headRot = std::abs(r - 1) % 8;
    }
    const SpriteImage* img = frameOf(set, f, r);
    if (!img) return;
    int depth = 0;
    if (u->mover && u->mover->height < 0) depth = u->mover->height / -3;
    if (img->rlx.layer > 0x80) {
        drawHelmet(x, y, s, *img, hat, headRot, depth);
        drawBody(x, y, s, *img, depth);
    } else {
        drawBody(x, y, s, *img, depth);
        drawHelmet(x, y, s, *img, hat, headRot, depth);
    }
    gfx().setSpriteRemap(nullptr);
    drawGrassTuft(x, y, s, *img, u, depth);
}

// Frame of a timed animation: sign(m) * (|m| >> k), m = e mod p (C remainder).
int timedFrame(s32 e, int p, int k) {
    const int m = int(e % p);
    const int a = (m < 0 ? -m : m) >> k;
    return m < 0 ? -a : a;
}

bool surrendered(const game::Unit* u) { return u->brain && u->brain->surrendered != 0; }

void drawUnit(int x, int y, int scale, game::Unit* u) {
    const game::Anim* a = u->anim;
    if (u->status) scale += (int(u->status->size / 20) - 3) * (s16(scale) >> 4);
    int running;
    if (g_animUpdate) running = g_animUpdate(u);
    else running = (a->state != a->return_state && (a->clock - a->start) < a->duration) ? 1 : 0;
    const s32 e = a->clock - a->start;
    const int st = int(a->state);
    const game::Mover* m = u->mover;
    const int rot = g_rotation;
    if (running && !surrendered(u)) {
        switch (st) {
        case 4:
        case 5: {
            int f = timedFrame(e, 0x80, 6);
            if (st == 5) f = 1 - f;
            if (f > 1) f = 1;
            return drawSoldierFrame(x, y, scale, "uc", f, rot, u);
        }
        case 6: return drawSoldierFrame(x, y, scale, "up", timedFrame(e, 0x100, 7), rot, u);
        case 7:
        case 8: {
            int f = timedFrame(e, 0x80, 6);
            if (st == 8) f = 1 - f;
            return drawSoldierFrame(x, y, scale, "cp", f, rot, u);
        }
        case 9: return drawSoldierFrame(x, y, scale, "uf", timedFrame(e, 0x200, 8), rot, u);
        case 11:
        case 12: return drawSoldierFrame(x, y, scale, "ua", st == 12 ? timedFrame(e, 0x100, 7) : 0, rot, u);
        case 13: return drawSoldierFrame(x, y, scale, "cf", 0, rot, u);
        case 14:
        case 15: return drawSoldierFrame(x, y, scale, "ca", st == 15 ? timedFrame(e, 0x100, 7) : 0, rot, u);
        case 16: return drawSoldierFrame(x, y, scale, "cr", timedFrame(e, 0x300, 8) % 2, rot, u);
        case 17: return drawSoldierFrame(x, y, scale, "cw", (int(e % 0x300) / 0x110) % 2, rot, u);
        case 18:
        case 19: return drawSoldierFrame(x, y, scale, "pa", st == 19 ? timedFrame(e, 0x100, 7) : 0, rot, u);
        default: return;  // 10 invisible, others not drawn
        }
    }
    const s32 c = a->clock;
    const bool sur = surrendered(u);
    const int mode = m ? int(u8(m->move_mode)) : 0;
    if (m && mode == 2 && u8(m->posture) == 0) {
        const int f = int(c % 0x100) / 0x2B;
        if (sur) return drawSoldierFrame(x, y, scale, "ds", f % 4, rot, u);
        return drawSoldierFrame(x, y, scale, "uq", f, rot, u);
    }
    if (mode == 0) {
        switch (st) {
        case 1: return drawSoldierFrame(x, y, scale, sur ? "d" : "c", sur ? 1 : 0, rot, u);
        case 2: return drawSoldierFrame(x, y, scale, sur ? "dp" : "p", 0, rot, u);
        case 3: return drawSoldierFrame(x, y, scale, "pi", 0, rot, u);
        default: return drawSoldierFrame(x, y, scale, sur ? "d" : "u", 0, rot, u);
        }
    }
    switch (a->posture) {
    case 1: {
        int f = timedFrame(c, 0x300, 7);
        if (mode == 3) f = 5 - f;
        if (sur) return drawSoldierFrame(x, y, scale, "ds", f % 4, rot, u);
        return drawSoldierFrame(x, y, scale, "cs", f, rot, u);
    }
    case 2: {
        int f = int(c % 0x300) / 0xC0;
        if (mode == 3) f = 3 - f;
        if (sur) return drawSoldierFrame(x, y, scale, "dp", f % 2, rot, u);
        return drawSoldierFrame(x, y, scale, "ps", f, rot, u);
    }
    default: {
        int f = int(c % 0x300) / 0x60;
        if (mode == 3) f = 7 - f;
        if (sur) return drawSoldierFrame(x, y, scale, "ds", f % 4, rot, u);
        if (m && (m->flags & 0x08)) return drawSoldierFrame(x, y, scale, "uw", f % 2, rot, u);
        return drawSoldierFrame(x, y, scale, "us", f, rot, u);
    }
    }
}

// spr_draw_scenery_billboard (348e:0A90)
void drawScenery(int x, int y, int scale, const game::Obj3D* obj) {
    const u16 ds = modelDsOffset(obj->model);
    if (ds == 0x878E) {
        if (!g_texJungle.empty()) sprDrawClippedBottom(x, y, scale, g_texJungle.data());
        return;
    }
    if (ds == 0x6DCC) {
        if (!g_texBush.empty()) sprDrawBottomCentered(x, y, scale, g_texBush.data());
        return;
    }
    if (g_effectPainter && g_effectPainter(obj, x, y, scale, g_rotation)) return;
    if (ds == 0x7C02) return;
    if (ds != 0x95F0) return;
    // Lone muzzle flash: fxmu[(rotation + 4) % 8] with remap4, twice the scale.
    g_rotation = (g_rotation + 4) % 8;
    const SpriteImage& fx = g_fxmu[g_rotation];
    if (fx.rle.empty()) return;
    gfx().setSpriteRemap(remapTable(2));
    sprDrawCentered(x, y, scale << 1, fx.rle.data());
    gfx().setSpriteRemap(nullptr);
}

// spr_draw_billboard_cb (348e:0C52)
void billboardCb(u16 /*prim*/, int x, int y) {
    const RenderContext& ctx = renderContext();
    if (ctx.viewMode == 1 || ctx.viewMode == 0xC) return;
    const DrawState& d = drawState();
    const int N = frameScale();
    // Scale: projected width of 0x60 model units at the object's depth
    // (Enhanced: a negative K scales the width instead of the depth).
    s32 vx = mk32(0x60, 0), vz = s32(u32(s32(d.z)) << 16);
    if (d.K >= 0) vz >>= d.K;
    else vx >>= -d.K;
    const s32 v[3] = {vx, 0, vz};
    s16 sx, sy;
    project(v, sx, sy);
    int scale = s16(sx - projectionCentreX());
    if (scale > 0x640 * N) scale = 0x640 * N;
    if (x < -0x640 * N || x > 0x780 * N || y < -0x640 * N || y > 0x708 * N || scale <= N) return;
    const game::Obj3D* obj = d.obj;
    game::Unit* u = g_unitLookup ? g_unitLookup(obj) : nullptr;
    if (u && !u->anim) return;
    s16 heading;
    if (u && u->mover && s32(u->mover->fired_until - 0x80) > ctx.time)
        heading = s16(u->mover->aim_heading << 3);
    else
        heading = obj->heading;
    g_rotation = calcRotation(heading, obj->pos);
    if (u && u->anim->kind >= 3) {
        if (scale <= 2 * N) return;
        const int k = u->anim->kind;
        const u8* table = nullptr;
        if (k >= 8 && k <= 11) table = remapTable(11 - k);
        else if (k >= 3 && k <= 6) table = remapTable(k - 3);
        else if (k == 7) table = gradSkyTable();  // DS:00FA: the sky gradient (original)
        if (table) gfx().setSpriteRemap(table);
    }
    if (!u) return drawScenery(x, y, scale, obj);
    drawUnit(x, y, scale, u);
    gfx().setSpriteRemap(nullptr);
}

}  // namespace

bool spritesLoad() {
    setBillboardCallback(billboardCb);
    if (g_loaded) return true;
    resources().read("jungle.rle", g_texJungle);
    resources().read("bush.rle", g_texBush);
    resources().read("grass.rle", g_texGrass);
    resources().read("impcf1r1.rle", g_texImpact);
    for (int r = 0; r < 8; ++r) loadImage("fxmuf1r" + std::to_string(r + 1), g_fxmu[r], false);
    // Effect banks of mis_load_sprites (1000:6A9C), for the effect painter.
    for (const char* fx : {"fxmu", "fxsm", "exsm", "exlg", "impc"}) loadSet(fx, 1, false);
    loadSet("hnds", 5, false);
    for (const SetDef& s : kSoldierSets) loadSet(s.name, s.frames, true);
    for (const char* h : kHeadgear) loadSet(h, 5, false);
    g_loaded = true;
    return true;
}

void spritesFree() {
    g_sets.clear();
    g_texJungle.clear();
    g_texBush.clear();
    g_texGrass.clear();
    g_texImpact.clear();
    for (SpriteImage& s : g_fxmu) s = SpriteImage{};
    g_loaded = false;
}

const SpriteImage* spriteFrame(const char* set, int f, int r) { return frameOf(set, f, r); }
void setUnitLookup(UnitLookup fn) { g_unitLookup = fn; }
void setEffectPainter(EffectPainter fn) { g_effectPainter = fn; }
void setAnimUpdate(AnimUpdate fn) { g_animUpdate = fn; }
void setPointMan(const game::Unit* pm) { g_pointMan = pm; }
int spriteRotation() { return g_rotation; }

// 1000:6C80
void sprDrawCentered(int x, int y, int scale, const u8* rle) {
    const int w = std::max(1, mul88(sprW(rle), scale));
    const int h = std::max(1, mul88(sprH(rle), scale));
    gfx().spriteScaled(x - (s16(w) >> 1), y - (s16(h) >> 1), w, h, rle);
}

// 1000:6CFD
void sprDrawBottomCentered(int x, int y, int scale, const u8* rle) {
    if (renderContext().detail < 1) return;
    const int w = mul88(sprW(rle), scale), h = mul88(sprH(rle), scale);
    gfx().spriteScaled(x - (s16(w) >> 1), y - h, w, h, rle);
}

// 1000:6D56: never larger than the image itself, x snapped to 8 pixels.
// Enhanced snaps to 8 pixels of the layer, not of the page: on the page grid
// the horizon treeline could only move in 8 * scale pixel jumps (96 at 4K),
// which made it lurch sideways whenever the camera panned.
void sprDrawClippedBottom(int x, int y, int scale, const u8* rle) {
    if (renderContext().detail < 4) return;
    const double kx = frameScaleX(), ky = frameScaleY();
    int w = mul88(sprW(rle), scale), h = mul88(sprH(rle), scale);
    if (int(sprW(rle) * kx) < w) w = int(sprW(rle) * kx);
    if (int(sprH(rle) * ky) < h) h = int(sprH(rle) * ky);
    const int xs = x & ~7;
    gfx().spriteScaled(xs - (s16(w) >> 1), y - h, w, h, rle);
}

}  // namespace st::render
