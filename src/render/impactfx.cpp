#include "render/impactfx.h"

#include "engine/palette_fade.h"
#include "gfx/gfx.h"
#include "render/r3d.h"
#include "render/sprites.h"
#include "render/world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace st::render {

using game::ImpactSurface;

namespace {

constexpr int kSurfaces = 8;
constexpr int kPoolSize = 64;
constexpr int kPuffPool = 16;
constexpr int kShades = 3;

// ---------------------------------------------------------------- remap tables

// Tint of a surface for a source luminance Y (0..63): the 6-bit RGB the
// remapped pixel should have. Dust / None keep the original colours.
void surfaceTint(ImpactSurface s, double Y, double& r, double& g, double& b) {
    switch (s) {
    case ImpactSurface::Blood: r = 0.7 * Y + 24, g = 0.1 * Y, b = 0.1 * Y; break;  // red floor: dark pixels stay red, not brown
    case ImpactSurface::Metal: r = 1.3 * Y + 16, g = 1.15 * Y + 10, b = 0.5 * Y; break;
    case ImpactSurface::Wood: r = 0.75 * Y + 6, g = 0.5 * Y + 3, b = 0.25 * Y; break;
    case ImpactSurface::Stone: r = Y, g = Y, b = Y; break;
    case ImpactSurface::Foliage: r = 0.35 * Y, g = 0.8 * Y + 4, b = 0.3 * Y; break;
    case ImpactSurface::Water: r = 0.55 * Y + 8, g = 0.75 * Y + 10, b = 1.1 * Y + 16; break;
    default: r = g = b = Y; break;
    }
    r = std::min(r, 63.0);
    g = std::min(g, 63.0);
    b = std::min(b, 63.0);
}

struct Tables {
    Palette pal{};                       // the palette the tables were built from
    bool valid = false;
    u8 remap[kSurfaces][256];
    u8 particle[kSurfaces][kShades];

    // Palette entry nearest to (r, g, b) in 6-bit RGB (squared distance).
    u8 nearest(double r, double g, double b) const {
        int best = 0;
        double bestD = 1e30;
        for (int j = 0; j < 256; ++j) {
            const double dr = pal[size_t(j * 3)] - r, dg = pal[size_t(j * 3 + 1)] - g, db = pal[size_t(j * 3 + 2)] - b;
            const double d = dr * dr + dg * dg + db * db;
            if (d < bestD) {
                bestD = d;
                best = j;
            }
        }
        return u8(best);
    }

    void rebuild(const Palette& src) {
        pal = src;
        valid = true;
        double lum[256];
        for (int i = 0; i < 256; ++i)
            lum[i] = (299.0 * pal[size_t(i * 3)] + 587.0 * pal[size_t(i * 3 + 1)] + 114.0 * pal[size_t(i * 3 + 2)]) / 1000.0;
        for (int s = 0; s < kSurfaces; ++s) {
            const ImpactSurface surf = ImpactSurface(s);
            for (int i = 0; i < 256; ++i) {
                if (surf == ImpactSurface::None || surf == ImpactSurface::Dust) {
                    remap[s][i] = u8(i);
                    continue;
                }
                double r, g, b;
                surfaceTint(surf, lum[i], r, g, b);
                remap[s][i] = nearest(r, g, b);
            }
            // The sprite writer tests transparency after the remap: 255 is the
            // RLE sprites' transparent colour (header byte 4) and 0 the page
            // background; the game's remap2..5 keep both as well.
            remap[s][0] = 0;
            remap[s][255] = 255;
            // Particle shades: white, yellow and mid grey of the EGA set through
            // the surface tint; dust is a sandy tone of its own.
            static const u8 kBright[kShades] = {15, 14, 7};
            for (int k = 0; k < kShades; ++k) {
                if (surf == ImpactSurface::None || surf == ImpactSurface::Dust) {
                    static const double kSand[kShades][3] = {{50, 44, 32}, {42, 37, 27}, {56, 51, 40}};
                    particle[s][k] = nearest(kSand[k][0], kSand[k][1], kSand[k][2]);
                } else {
                    particle[s][k] = remap[s][kBright[k]];
                }
            }
        }
    }

    void refresh() {
        const Palette& cur = engine::paletteFade().palette();
        if (valid && std::memcmp(pal.data(), cur.data(), cur.size()) == 0) return;
        rebuild(cur);
    }
};

Tables g_tables;

int surfaceIndex(ImpactSurface s) { return std::clamp(int(s), 0, kSurfaces - 1); }

// ---------------------------------------------------------------- particles

struct Particle {
    double x = 0, y = 0, z = 0;     // world position, game units
    double vx = 0, vy = 0, vz = 0;  // game units per tick
    int life = 0;                   // ticks left (0 = free)
    u32 serial = 0;                 // spawn order, the oldest is replaced first
    u8 color = 0;
};

// A visual-only puff: the "impc" frame of the hit kind through the remap.
struct Puff {
    game::Vec3 pos{};
    s32 start = 0;                  // game time it started
    int frame = 0;                  // impc frame (rotation index)
    u32 serial = 0;                 // 0 = free
    ImpactSurface surface = ImpactSurface::None;
};

Particle g_pool[kPoolSize];
Puff g_puffs[kPuffPool];
u32 g_serial = 0;
s32 g_time = 0;                     // the clock the puffs grow with

constexpr double kGravity = 0.0010;  // game units per tick^2

// Private linear congruential generator of the burst (never the game RNG).
struct Lcg {
    u32 s;
    explicit Lcg(u32 seed) : s(seed * 2654435761u + 0x9E3779B9u) {}
    u32 next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    int range(int n) { return int(next() % u32(n)); }
    double unit() { return (next() & 0xFFFF) / 65536.0; }
};

Particle& allocParticle() {
    Particle* oldest = &g_pool[0];
    for (Particle& p : g_pool) {
        if (p.life <= 0) return p;
        if (p.serial < oldest->serial) oldest = &p;
    }
    return *oldest;
}

Puff& allocPuff() {
    Puff* oldest = &g_puffs[0];
    for (Puff& p : g_puffs) {
        if (p.serial == 0) return p;
        if (p.serial < oldest->serial) oldest = &p;
    }
    return *oldest;
}

// math_mul_8_8 (2255:1062): (a * b) >> 8.
int mul88(int a, int b) { return s16((s32(s16(a)) * s16(b)) >> 8); }

// spr_draw_explosion (1000:6DD5) for an impact, as the mission's painter
// draws it: size by the time since the start (8.8 scale e = 4 * elapsed, at
// least 0x40), shown while e < 3 * (billboard scale / 4). The billboard
// scale is spr_draw_billboard_cb's projected width of 0x60 model units of
// the burst shape at the puff's depth.
void drawPuff(Puff& p) {
    const SpriteImage* img = spriteFrame("impc", 0, p.frame);
    double sx, sy, depth;
    if (!img || !projectWorld(p.pos, sx, sy, &depth)) {
        p.serial = 0;
        return;
    }
    const game::ModelDesc* burst = modelByDs(0x7C02);
    const int shift = burst ? burst->scale_shift : 0;
    const double unit = shift >= 0 ? double(1 << shift) : 1.0 / double(1 << -shift);  // game units per model unit
    const double width = frameScaleX() * double(1 << frameZoom()) * 0x60 * unit / depth;
    const int scale = std::min(int(width), 0x640 * frameScale()) >> 2;
    int e = (g_time - p.start) * 4;
    if (e < 0x40) e = 0x40;
    if (e >= scale * 3 || e > 0x7FFF) {
        p.serial = 0;
        return;
    }
    const int w = mul88(img->w(), e), h = mul88(img->h(), e);
    if (w <= 0 || h <= 0) return;
    Gfx& gx = gfx();
    gx.setSpriteRemap(impactRemap(p.surface));
    gx.spriteScaled(int(std::lround(sx)) - (w >> 1), int(std::lround(sy)) - (h >> 1), w, h, img->rle.data());
    gx.setSpriteRemap(nullptr);
}

}  // namespace

const u8* impactRemap(ImpactSurface s) {
    if (s == ImpactSurface::None || s == ImpactSurface::Dust) return nullptr;
    g_tables.refresh();
    return g_tables.remap[surfaceIndex(s)];
}

u8 impactParticleColor(ImpactSurface s, int k) {
    g_tables.refresh();
    return g_tables.particle[surfaceIndex(s)][((k % kShades) + kShades) % kShades];
}

void impactParticlesSpawn(const game::Vec3& pos, ImpactSurface s, s32 time) {
    Lcg rng{u32(time)};
    const int n = 6 + rng.range(5);
    for (int i = 0; i < n; ++i) {
        Particle& p = allocParticle();
        const double a = rng.unit() * 6.283185307;
        const double out = 0.015 + 0.045 * rng.unit();
        p.x = pos.x / 256.0;
        p.y = pos.y / 256.0;
        p.z = pos.z / 256.0;
        p.vx = std::cos(a) * out;
        p.vz = std::sin(a) * out;
        p.vy = 0.04 + 0.08 * rng.unit();
        p.life = 0x60 + rng.range(0x41);
        p.color = impactParticleColor(s, rng.range(kShades));
        p.serial = ++g_serial;
    }
}

void impactPuffAdd(const game::Vec3& pos, int frame, ImpactSurface s, s32 time) {
    Puff& p = allocPuff();
    p.pos = pos;
    p.start = time;
    p.frame = frame;
    p.surface = s;
    p.serial = ++g_serial;
    g_time = time;
}

void impactFxUpdate(int ticks, s32 time) {
    g_time = time;
    if (ticks <= 0) return;
    for (Particle& p : g_pool) {
        if (p.life <= 0) continue;
        p.vy -= kGravity * ticks;
        p.x += p.vx * ticks;
        p.y += p.vy * ticks;
        p.z += p.vz * ticks;
        p.life -= ticks;
        if (p.y < 0) p.life = 0;  // into the ground
    }
}

void impactFxDraw() {
    for (Puff& p : g_puffs)
        if (p.serial) drawPuff(p);
    const int side = std::max(1, int(std::lround(frameScaleX() / 2)));
    Gfx& gx = gfx();
    for (const Particle& p : g_pool) {
        if (p.life <= 0) continue;
        const game::Vec3 w{s32(std::lround(p.x * 256)), s32(std::lround(p.y * 256)), s32(std::lround(p.z * 256))};
        double sx, sy;
        if (!projectWorld(w, sx, sy)) continue;
        if (sx < -32000 || sx > 32000 || sy < -32000 || sy > 32000) continue;
        gx.fillRect(int(std::lround(sx)) - side / 2, int(std::lround(sy)) - side / 2, side, side,
                    u16(Gfx::kSolid | p.color));
    }
}

void impactFxReset() {
    for (Particle& p : g_pool) p = Particle{};
    for (Puff& p : g_puffs) p = Puff{};
    g_serial = 0;
}

int impactParticleCount() {
    int n = 0;
    for (const Particle& p : g_pool)
        if (p.life > 0) ++n;
    return n;
}

const char* impactSurfaceName(ImpactSurface s) {
    static const char* kNames[kSurfaces] = {"None", "Dust", "Water", "Wood", "Stone", "Foliage", "Metal", "Blood"};
    return kNames[surfaceIndex(s)];
}

}  // namespace st::render
