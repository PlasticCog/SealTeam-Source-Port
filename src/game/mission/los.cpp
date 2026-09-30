// Segment 4ff8: line-of-sight / collision queries (docs/re/seg_libs.md 15).
//
// Two static quadtrees over the grid objects (tree A: flag 0x1000 sight
// blockers, tree B: flag 0x0100 terrain features), the dynamic object list and
// the ground plane, tested against a segment grown by a radius. The original
// keeps the whole query state in DS globals; here it lives in file-static
// structs (the module is not re-entrant, like the original). Queries do not
// allocate.
#include "game/mission/los.h"

#include "game/mission/geo.h"
#include "game/mission/state.h"
#include "game/mission/world.h"
#include "render/model.h"

#include <initializer_list>
#include <vector>

namespace st::game::mission {

namespace {

namespace F = obj3d_flag;

Obj3D g_groundSentinel{};

// High word of a 32-bit world coordinate ("hi16" units of the quadtree and the
// coarse tests).
inline s16 hi16(s32 v) { return s16(u32(v) >> 16); }
inline u16 hi16u(s32 v) { return u16(u32(v) >> 16); }
// Arithmetic >> 8 of a 24.8 value (the byte shuffles + CBW of 4ff8:0190..).
inline s32 sar8(s32 v) { return v >> 8; }
// (v << 8) truncated to 32 bits (the hit point writes of 4ff8:09D5 / 0AF4).
inline s32 shl8(s32 v) { return s32(u32(v) << 8); }
inline s16 neg16(s16 v) { return s16(u16(0) - u16(v)); }
inline s32 axis(const Vec3& v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

// IMUL r16 followed by IDIV r16: 32/16 signed division truncating toward zero.
// The 8086 raises a divide error (the game stops) when the divisor is 0 or the
// quotient does not fit 16 bits; the port returns 0 or the truncated
// quotient instead (see the call sites for when this can happen).
inline s16 idiv16(s32 num, s16 den) {
    if (den == 0) return 0;
    return s16(num / den);
}

// 16-bit SAR / SHL by CL (the 286+ masks the count to 5 bits).
inline s16 sar16(s16 v, int n) {
    n &= 31;
    if (n >= 16) return v < 0 ? s16(-1) : s16(0);
    return s16(v >> n);
}
inline u16 shl16(u16 v, int n) {
    n &= 31;
    return n >= 16 ? u16(0) : u16(v << n);
}

// ---------------------------------------------------------------------------
// Quadtree (4ff8:000C..006C, 0CAA, 0CD0)
// ---------------------------------------------------------------------------

// The original packs a tree into a 5000-byte far block, root at offset 0:
// internal node = byte 0, u16 midX, u16 midZ, four u16 child offsets (13
// bytes); leaf = byte 1, u16 object offsets, 0 (1 + 2n + 2 bytes). The port
// stores the same tree as node records and only mirrors the block offsets to
// detect trees the original could not have built.
constexpr u32 kBlockBytes = 0x1388;  // 5000

struct Node {
    bool leaf = false;
    u16 midX = 0;           // split lines in hi16 units
    u16 midZ = 0;
    u32 child[4] = {};      // (x<mid,z<mid), (x>=mid,z<mid), (x<mid,z>=mid), (x>=mid,z>=mid)
    u32 first = 0;          // leaf: objects[first .. first+count), cell-list order
    u32 count = 0;
};

struct Tree {
    std::vector<Node> nodes;      // root = nodes[0]; empty = not built
    std::vector<Obj3D*> objects;  // leaf lists
    u16 mask = 0;                 // DS:F6DE while building (without the "active" bit)
    u32 blockUsed = 0;            // DS:F6DC: next free byte of the original block
    bool overflow = false;        // the original would have stopped with "Not enough memory."
};

struct Trees {
    Tree a;  // g_los_tree_a DS:F6D6 (mask 0x1000)
    Tree b;  // g_los_tree_b DS:F6D8 (mask 0x0100)
};
Trees g_trees;

// World grid (DS:07A4): the game world is always a single 1 x 1 cell (render/
// world.h), whose list is world.h staticObjects(). The root node covers hi16
// (0,0)..(width << 8, depth << 8), and the cell range a node scans,
// max(hibyte(min) - 1, 0) .. min(hibyte(max) + 1, size - 1), is always (0,0).
constexpr u16 kGridWidth = 1;  // DS:07A8
constexpr u16 kGridDepth = 1;  // DS:07AA

// 4ff8:0CD0 los_build_node(xmin, zmin, xmax, zmax), hi16 units. A node takes
// the objects of the mask whose model radius + 200 overlaps its range; when
// the 10th one qualifies it becomes an internal node and builds its four
// children instead (a leaf holds at most 9 objects unless a span is <= 8).
u32 losBuildNode(Tree& t, u16 xmin, u16 zmin, u16 xmax, u16 zmax) {
    const u16 xspan = u16(xmax - xmin + 1);  // DS:5D14
    const u16 midX = u16(xmin + (xspan >> 1));
    const u16 zspan = u16(zmax - zmin + 1);  // DS:5D16
    const u16 midZ = u16(zmin + (zspan >> 1));
    const u32 at = t.blockUsed;               // DS:5D1A offset of this node in the block
    if (at >= 0x137B) t.overflow = true;      // original: fatal_no_memory
    u16 limit = (s16(xspan) <= 8 || s16(zspan) <= 8) ? u16(0xFFFF) : u16(10);

    const u32 index = u32(t.nodes.size());
    t.nodes.emplace_back();
    const u32 first = u32(t.objects.size());
    u32 di = at + 1;
    bool split = false;
    for (Obj3D* o : staticObjects()) {
        if (!(o->flags & t.mask)) continue;
        if (!o->model) continue;  // the original would read the radius at DS:0008
        const s32 e = o->model->radius_world;
        if (hi16(wrapAdd(wrapAdd(o->pos.x, e), 200)) < s16(xmin)) continue;
        if (hi16(wrapSub(wrapSub(o->pos.x, e), 200)) >= s16(xmax)) continue;
        if (hi16(wrapAdd(wrapAdd(o->pos.z, e), 200)) < s16(zmin)) continue;
        if (hi16(wrapSub(wrapSub(o->pos.z, e), 200)) >= s16(zmax)) continue;
        if (di >= kBlockBytes) t.overflow = true;
        di += 2;
        t.objects.push_back(o);
        if (--limit == 0) {
            split = true;
            break;
        }
    }

    if (!split) {
        if (di > kBlockBytes - 2) t.overflow = true;
        t.blockUsed = di + 2;  // 0 terminator
        Node& n = t.nodes[index];
        n.leaf = true;
        n.first = first;
        n.count = u32(t.objects.size()) - first;
        return index;
    }

    // The internal node header overwrites the partial leaf; the children scan again.
    t.objects.resize(first);
    t.blockUsed = at + 13;
    t.nodes[index].midX = midX;
    t.nodes[index].midZ = midZ;
    const u32 c0 = losBuildNode(t, xmin, zmin, midX, midZ);
    const u32 c1 = losBuildNode(t, midX, zmin, xmax, midZ);
    const u32 c2 = losBuildNode(t, xmin, midZ, midX, zmax);
    const u32 c3 = losBuildNode(t, midX, midZ, xmax, zmax);
    Node& n = t.nodes[index];
    n.child[0] = c0;
    n.child[1] = c1;
    n.child[2] = c2;
    n.child[3] = c3;
    return index;
}

// 4ff8:0021 los_build_tree(mask). The original allocates the 5000-byte block,
// builds, then shrinks the block to the used size (failures are fatal).
void losBuildTree(Tree& t, u16 mask) {
    t.nodes.clear();
    t.objects.clear();
    t.mask = mask;
    t.blockUsed = 0;
    t.overflow = false;
    losBuildNode(t, 0, 0, u16(kGridWidth << 8), u16(kGridDepth << 8));
    if (t.overflow)
        logWarn("LOS tree %04X needs %u bytes; the original stops with \"Not enough memory.\" above %u", mask,
                unsigned(t.blockUsed), unsigned(kBlockBytes));
}

// 4ff8:0CAA los_find_leaf: from the root take child (x_hi >= midX) +
// 2 * (z_hi >= midZ) (unsigned compares) until a leaf.
u32 losFindLeaf(const Tree& t, u16 xHi, u16 zHi) {
    u32 i = 0;
    while (!t.nodes[i].leaf) {
        const Node& n = t.nodes[i];
        i = n.child[(xHi >= n.midX ? 1 : 0) + (zHi >= n.midZ ? 2 : 0)];
    }
    return i;
}

// ---------------------------------------------------------------------------
// Query state
// ---------------------------------------------------------------------------

struct Query {
    const Obj3D* ignore = nullptr;  // DS:F6E4
    Vec3 from{};                    // DS:5C9E g_los_from
    Vec3 to{};                      // DS:5CAA
    s32 radius = 0;                 // DS:F6E0
    u16 mask = 0;                   // DS:F6DE mask | 1 ("active")
    bool includeFlagged = false;    // DS:F716
    Vec3* hitOut = nullptr;         // DS:ECFE
    bool hitAliased = false;        // hitOut is g_los_hit itself (both original callers)
    s32 fromS[3] = {};              // DS:F6FE from >> 8 per axis
    s32 toS[3] = {};                // DS:F70A to >> 8
    s16 fromLo[3] = {};             // DS:F6E6.. hi16(from - radius) per axis
    s16 fromHi[3] = {};             //           hi16(from + radius)
    s16 toLo[3] = {};               // DS:F6F2.. hi16(to - radius)
    s16 toHi[3] = {};               //           hi16(to + radius)
};
Query g_q;

// Scratch of los_test_object (>> 8 units, object/model space).
struct Local {
    s32 c[3];   // DS:5CB8 object position >> 8
    s16 p1[3];  // DS:5CC4 segment start
    s16 p2[3];  // DS:5CCA segment end
    s16 lo[3];  // DS:5CD0/5CD4/5CD8 model box min - radius
    s16 hi[3];  // DS:5CD2/5CD6/5CDA model box max + radius
};

// Hit point output. Original quirk (4ff8:017B, 09CD, 0A3F, 0AEC): the pointer
// argument is kept at DS:ECFE, which is word 0 of g_los_hit itself, and both
// callers pass &g_los_hit. So every query sets the low word of g_los_hit.x to
// 0xECFE (losTrace), a write goes to the address in that word, and the first
// write replaces it with the low word of the new x. A further write in the
// same query (possible only after a container was hit; the game has none)
// goes to that address, e.g. DS:ED00, i.e. partly into g_los_hit. Only the
// part of such a write that lands in g_los_hit (DS:ECFE..ED09) is modelled;
// the original corrupts whatever else is there.
void losWriteHit(const Vec3& v) {
    if (!g_q.hitAliased) {
        if (g_q.hitOut) *g_q.hitOut = v;
        return;
    }
    Vec3& hit = ms().losHit;
    const u16 ptr = u16(u32(hit.x));
    if (ptr == 0) return;
    const s32 cur[3] = {hit.x, hit.y, hit.z};
    const s32 val[3] = {v.x, v.y, v.z};
    u8 img[12], src[12];
    for (int i = 0; i < 12; ++i) {
        img[i] = u8(u32(cur[i / 4]) >> (8 * (i % 4)));
        src[i] = u8(u32(val[i / 4]) >> (8 * (i % 4)));
    }
    for (int i = 0; i < 12; ++i) {
        const int d = int(u16(ptr + i)) - 0xECFE;
        if (d >= 0 && d < 12) img[d] = src[i];
    }
    s32 out[3];
    for (int k = 0; k < 3; ++k)
        out[k] = s32(u32(img[4 * k]) | (u32(img[4 * k + 1]) << 8) | (u32(img[4 * k + 2]) << 16) |
                     (u32(img[4 * k + 3]) << 24));
    hit = {out[0], out[1], out[2]};
}

// ---------------------------------------------------------------------------
// Object tests
// ---------------------------------------------------------------------------

// 4ff8:0098 los_obj_coarse_overlap: per axis (signed hi16 compares) reject
// when hi16(c - R) is above both hi16(p + r), or hi16(c + R) below both
// hi16(p - r) (R = model radius, r = query radius).
bool losObjCoarseOverlap(const Obj3D* o, s32 modelRadius) {
    for (int a = 0; a < 3; ++a) {
        const s32 c = axis(o->pos, a);
        const s16 lo = hi16(wrapSub(c, modelRadius));
        if (g_q.fromHi[a] < lo && g_q.toHi[a] < lo) return false;
        const s16 hi = hi16(wrapAdd(c, modelRadius));
        if (g_q.fromLo[a] > hi && g_q.toLo[a] > hi) return false;
    }
    return true;
}

// 4ff8:0384 los_outcode_p1 / 03CA los_outcode_p2 (signed compares).
u8 losOutcode(const s16* p, const Local& L) {
    u8 c = 0;
    if (p[0] < L.lo[0]) c |= 0x04;
    if (p[0] > L.hi[0]) c |= 0x08;
    if (p[1] < L.lo[1]) c |= 0x02;
    if (p[1] > L.hi[1]) c |= 0x01;
    if (p[2] < L.lo[2]) c |= 0x10;
    if (p[2] > L.hi[2]) c |= 0x20;
    return c;
}

// One clip step of 4ff8:0757.. (p1) / 0850.. (p2): move p onto the first
// outside plane in the order 4, 8, 1, 2, 0x10, 0x20. For plane B of axis k:
// t = B - p[k]; the other axes (ascending) advance by t * d[j] / d[k]
// (16x16->32 IMUL, 32/16 IDIV truncating toward zero); p[k] = B. d = p2 - p1
// (16-bit). Because the two outcodes share no bit, d[k] is never 0; a
// quotient overflow (a divide error in the original) needs endpoints more than
// 32767 map units apart inside the coarse box, which the game never produces.
void losClipPoint(s16* p, const s16* d, u8 oc, const Local& L) {
    int k;
    s16 b;
    if (oc & 0x04) { k = 0; b = L.lo[0]; }
    else if (oc & 0x08) { k = 0; b = L.hi[0]; }
    else if (oc & 0x01) { k = 1; b = L.hi[1]; }
    else if (oc & 0x02) { k = 1; b = L.lo[1]; }
    else if (oc & 0x10) { k = 2; b = L.lo[2]; }
    else if (oc & 0x20) { k = 2; b = L.hi[2]; }
    else return;
    const s16 t = s16(b - p[k]);
    for (int j = 0; j < 3; ++j)
        if (j != k) p[j] = s16(p[j] + idiv16(s32(t) * d[j], d[k]));
    p[k] = b;
}

// 4ff8:0410 los_hmap_sample(x, y, z): hit when y <= the surface height at
// (x, z); then p1 = (x, h, z). Samples outside the map miss.
bool losHmapSample(const Heightmap& hm, s16 x, s16 y, s16 z, Local& L) {
    const s16 col = s16(sar16(x, u8(hm.cellShift)) + hm.xoff);
    const s16 row = s16(sar16(z, u8(hm.cellShift)) + hm.zoff);
    if (col < 0 || col >= hm.width || row < 0 || row >= hm.depth) return false;
    const u16 idx = u16(row * hm.width + col);  // IMUL low word + ADD (16-bit offset)
    const s16 h = s16(shl16(hm.bytes[idx], u8(hm.heightShift)));
    if (y > h) return false;
    L.p1[0] = x;
    L.p1[1] = h;
    L.p1[2] = z;
    return true;
}

// 4ff8:0478 los_hmap_test_segment: clipped p1, the midpoint, p2; first hit wins.
bool losHmapTestSegment(const Heightmap& hm, Local& L) {
    if (losHmapSample(hm, L.p1[0], L.p1[1], L.p1[2], L)) return true;
    const s16 mx = s16(s16(L.p1[0] + L.p2[0]) >> 1);  // 16-bit ADD, SAR 1
    const s16 my = s16(s16(L.p1[1] + L.p2[1]) >> 1);
    const s16 mz = s16(s16(L.p1[2] + L.p2[2]) >> 1);
    if (losHmapSample(hm, mx, my, mz, L)) return true;
    return losHmapSample(hm, L.p2[0], L.p2[1], L.p2[2], L);
}

// 2255:613F math_sin / 2255:618C math_cos for any 16-bit input: the word at
// 5327:(2i + 0xA) with 16-bit offset arithmetic, so indices outside 0..720
// read whatever lies around the table (far data, or DGROUP from 5327:3980 on;
// render::farWord gives the same memory). geo.h mathSin/mathCos clamp instead.
s16 losTableWord(s16 i) { return s16(render::farWord(0x5327, u16(u16(i) * 2u + 0xAu))); }

s16 losQuadrant(s16 b) {
    if (b < 0x2D0) return losTableWord(b);
    if (b < 0x5A0) return losTableWord(s16(0x5A0 - b));
    if (b < 0x870) return neg16(losTableWord(s16(b - 0x5A0)));
    return neg16(losTableWord(s16(0xB40 - b)));
}

s16 losCosAny(s16 a) {
    s16 b = s16(a + 0x2D0);
    if (b >= 0xB40) b = s16(b - 0xB40);
    return losQuadrant(b);
}

// R(p) of math_rotate_2d: high word of p << 1, plus 1 if bit 15 of its low word is set.
s16 losRound(s32 p) {
    const u32 v = u32(p) << 1;
    return s16((v >> 16) + ((v & 0x8000u) ? 1u : 0u));
}

// 2255:62D0 math_rotate_2d about (0,0). It normalises the angle by a single
// +-2880 step, so an angle outside -2880..5759 reaches math_sin/math_cos out
// of range; only the heading-2160 quirk below passes such angles (the hit
// point's model-space z, up to +-9216 for the river models). In range this is
// geo.h mathRotate2d.
void losRotate2d(s16& x, s16& z, s16 a8) {
    s16 a = a8;
    if (a < 0) a = s16(a + 0xB40);
    if (a >= 0xB40) a = s16(a - 0xB40);
    if (a >= 0 && a < 0xB40) {
        mathRotate2d(x, z, 0, 0, a);
        return;
    }
    const s16 s2 = s16(u16(losQuadrant(a)) << 1);
    const s16 c2 = s16(u16(losCosAny(a)) << 1);
    const s16 dx = x, dz = z;
    x = s16(losRound(s32(dx) * c2) - losRound(s32(s2) * dz));
    z = s16(losRound(s32(s2) * dx) + losRound(s32(dz) * c2));
}

// 4ff8:04BC los_rotate_segment: rotate (x,z) of p1 and p2 by -a about (0,0)
// (2255:62D0 with the origin DS:54F6).
void losRotateSegment(Local& L, s16 a) {
    const s16 na = neg16(a);
    mathRotate2d(L.p1[0], L.p1[2], 0, 0, na);
    mathRotate2d(L.p2[0], L.p2[2], 0, 0, na);
}

// 4ff8:050C los_test_object: the segment in the object's model space, clipped
// against the model box grown by the radius, optional heightmap; writes the
// hit point (clipped p1) on a hit.
bool losTestObject(const Obj3D* o) {
    const ModelDesc* m = o->model;  // DS:5CFE
    if (!m) return false;           // the original would read a descriptor at DS:0000
    if (!losObjCoarseOverlap(o, m->radius_world)) return false;

    Local L;
    for (int a = 0; a < 3; ++a) L.c[a] = sar8(axis(o->pos, a));
    // Local endpoints; one that does not fit 16 bits is a miss.
    for (int a = 0; a < 3; ++a) {
        const s32 d = wrapSub(g_q.fromS[a], L.c[a]);
        if (d != s32(s16(d))) return false;
        L.p1[a] = s16(d);
    }
    for (int a = 0; a < 3; ++a) {
        const s32 d = wrapSub(g_q.toS[a], L.c[a]);
        if (d != s32(s16(d))) return false;
        L.p2[a] = s16(d);
    }

    // Heading (none with flag 2): quarter turns are exact swaps; other values
    // rotate by -h only with flag 0x2000, else the heading is ignored.
    const s16 h = (o->flags & F::kStatic) ? s16(0) : s16(o->heading);  // DS:5CDF
    bool rotated = false;                                                // DS:5CE5
    if (h == 0x2D0) {
        for (s16* p : {L.p1, L.p2}) {
            const s16 x = p[0];
            p[0] = p[2];
            p[2] = neg16(x);
        }
    } else if (h == 0x5A0) {
        for (s16* p : {L.p1, L.p2}) {
            p[0] = neg16(p[0]);
            p[2] = neg16(p[2]);
        }
    } else if (h != 0) {
        if (h == 0x870) {
            for (s16* p : {L.p1, L.p2}) {
                const s16 x = p[0];
                p[0] = neg16(p[2]);
                p[2] = x;
            }
        }
        // Original quirk (4ff8:067E): the 2160 case falls through into the
        // general case, so with flag 0x2000 it is rotated by another -2160
        // (the model is tested as if its heading were 4320 = 1440).
        if (o->flags & F::kFreeHeading) {
            rotated = true;
            losRotateSegment(L, h);
        }
    }

    // Model box grown by the radius, >> 8 into 16 bits.
    const s32 r = g_q.radius;
    L.lo[0] = s16(u32(wrapSub(m->box_min_x, r)) >> 8);
    L.hi[0] = s16(u32(wrapAdd(m->box_max_x, r)) >> 8);
    L.lo[1] = s16(u32(wrapSub(m->box_min_y, r)) >> 8);
    L.hi[1] = s16(u32(wrapAdd(m->box_max_y, r)) >> 8);
    L.lo[2] = s16(u32(wrapSub(m->box_min_z, r)) >> 8);
    L.hi[2] = s16(u32(wrapAdd(m->box_max_z, r)) >> 8);

    // Cohen-Sutherland: accept when both outcodes are 0 or after 14 clip
    // steps (the counter DS:5CDE starts at 15), reject when they share a bit;
    // otherwise clip p1 if it is outside, else p2.
    u8 oc1 = losOutcode(L.p1, L);  // DS:5CDC
    u8 oc2 = losOutcode(L.p2, L);  // DS:5CDD
    u8 counter = 15;
    for (;;) {
        if ((oc1 | oc2) == 0) break;
        if (--counter == 0) break;
        if (oc1 & oc2) return false;
        const s16 d[3] = {s16(L.p2[0] - L.p1[0]), s16(L.p2[1] - L.p1[1]), s16(L.p2[2] - L.p1[2])};
        if (oc1) {
            losClipPoint(L.p1, d, oc1, L);
            oc1 = losOutcode(L.p1, L);
        } else {
            losClipPoint(L.p2, d, oc2, L);
            oc2 = losOutcode(L.p2, L);
        }
    }

    if (m->heightmap) {
        const Heightmap* hm = modelHeightmap(m);
        if (hm && !losHmapTestSegment(*hm, L)) return false;
    }

    // Undo the heading on p1.
    if (h == 0x2D0) {
        const s16 x = L.p1[0];
        L.p1[0] = neg16(L.p1[2]);
        L.p1[2] = x;
    } else if (h == 0x5A0) {
        L.p1[0] = neg16(L.p1[0]);
        L.p1[2] = neg16(L.p1[2]);
    } else if (h != 0) {
        s16 angle = h;
        if (h == 0x870) {
            const s16 x = L.p1[0];
            L.p1[0] = L.p1[2];
            L.p1[2] = neg16(x);
            // Original quirk (4ff8:098E): AX, which should still hold 2160,
            // is reused for the swap, so the inverse rotation below turns by
            // the model-space z of the hit point instead of +2160 (often an
            // angle outside the sine table, see losRotate2d).
            angle = L.p1[0];
        }
        if (rotated) losRotate2d(L.p1[0], L.p1[2], angle);
    }

    losWriteHit({shl8(wrapAdd(s32(L.p1[0]), L.c[0])), shl8(wrapAdd(s32(L.p1[1]), L.c[1])),
                 shl8(wrapAdd(s32(L.p1[2]), L.c[2]))});
    return true;
}

// Flag 0x0800 filter: such an object carries a near pointer P after its
// header (+0x12 with flag 2, else +0x18) and is skipped when word [P+0xC] & 8
// (unless the query includes flagged objects). No object of the game sets
// flag 0x0800 and Obj3D has no such record, so the record counts as absent.
bool losLinkedRecordHides(const Obj3D* o) {
    (void)o;
    return false;
}

// 4ff8:0B36 los_test_leaf / 4ff8:0BF6 los_test_list (identical filtering):
// the first object in list order that is hit. A container (flag 0x0020) that
// is hit returns the first hit of its child list (+0x18) if any, else the walk
// goes on with the next object.
Obj3D* losTestList(Obj3D* const* objs, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        Obj3D* o = objs[i];
        const u16 f = o->flags;
        if ((f & g_q.mask) != g_q.mask || o == g_q.ignore) continue;
        if (!g_q.includeFlagged && (f & (F::kLinkedRecord | F::kGroup)) == F::kLinkedRecord &&
            losLinkedRecordHides(o))
            continue;
        if (!losTestObject(o)) continue;
        if (f & F::kGroup) {
            // Containers are unused by the game and Obj3D has no child list
            // (word +0x18), so it is empty here.
            if (Obj3D* r = losTestList(nullptr, 0)) return r;
            continue;
        }
        // The original re-checks the 0x0800 filter here (it cannot differ).
        if (!g_q.includeFlagged && (f & F::kLinkedRecord) && losLinkedRecordHides(o)) continue;
        return o;
    }
    return nullptr;
}

Obj3D* losTestLeaf(const Tree& t, u32 leaf) {
    const Node& n = t.nodes[leaf];
    return losTestList(t.objects.data() + n.first, n.count);
}

// 4ff8:0A2C los_test_ground: plane y = 0 (y < 0 underground), >> 8 units.
bool losTestGround() {
    if (g_q.toS[1] >= 0) {
        if (g_q.fromS[1] >= 0) return false;
        losWriteHit(g_q.from);  // starts underground: hit at `from` (full precision)
        return true;
    }
    // Relative to base = min(from, to) in x and z (16-bit differences).
    const s32 baseX = g_q.fromS[0] <= g_q.toS[0] ? g_q.fromS[0] : g_q.toS[0];  // DS:5CE6
    const s32 baseZ = g_q.fromS[2] <= g_q.toS[2] ? g_q.fromS[2] : g_q.toS[2];  // DS:5CEA
    s16 x1 = s16(wrapSub(g_q.fromS[0], baseX));  // DS:5CEE
    const s16 x2 = s16(wrapSub(g_q.toS[0], baseX));
    s16 z1 = s16(wrapSub(g_q.fromS[2], baseZ));  // DS:5CF0
    const s16 z2 = s16(wrapSub(g_q.toS[2], baseZ));
    const s16 dx = s16(x2 - x1);
    const s16 dy = s16(wrapSub(g_q.toS[1], g_q.fromS[1]));
    const s16 dz = s16(z2 - z1);
    const s16 fy = s16(g_q.fromS[1]);
    // Original: from.y == to.y < 0 divides by zero (divide error, the game
    // stops); the port's idiv16 gives 0 there, i.e. the hit is at from's x/z.
    // With both endpoints underground the quotient can also overflow (another
    // divide error); the port truncates it. The game's queries start at or
    // above the ground, so neither should happen in practice.
    x1 = s16(x1 - idiv16(s32(dx) * fy, dy));
    z1 = s16(z1 - idiv16(s32(dz) * fy, dy));
    losWriteHit({shl8(wrapAdd(s32(x1), baseX)), 0, shl8(wrapAdd(s32(z1), baseZ))});
    return true;
}

} // namespace

Obj3D* losGroundHit() { return &g_groundSentinel; }

// 4ff8:000C
void losInit() {
    losBuildTree(g_trees.a, F::kLosTreeA);
    losBuildTree(g_trees.b, F::kLosTreeB);
}

// 4ff8:006C
void losFreeTrees() {
    for (Tree* t : {&g_trees.a, &g_trees.b}) {
        t->nodes.clear();
        t->objects.clear();
    }
}

// 4ff8:0148
Obj3D* losTrace(const Obj3D* ignore, const Vec3& from, const Vec3& to, s32 radius, Vec3* hitOut, u16 mask,
                bool includeFlagged, bool testGround, bool testDynamic) {
    Query& q = g_q;
    q.ignore = ignore;
    q.from = from;  // copied first: the hit write may land on the caller's vectors
    q.to = to;
    q.radius = radius;
    q.hitOut = hitOut;
    q.hitAliased = hitOut == &ms().losHit;
    if (q.hitAliased) {
        // Original quirk: the pointer (DS:ECFE) is stored in word 0 of g_los_hit (see losWriteHit).
        Vec3& hit = ms().losHit;
        hit.x = s32((u32(hit.x) & 0xFFFF0000u) | 0xECFEu);
    }
    q.mask = u16(mask | F::kEnabled);
    q.includeFlagged = includeFlagged;
    for (int a = 0; a < 3; ++a) {
        const s32 f = axis(q.from, a), t = axis(q.to, a);
        q.fromS[a] = sar8(f);
        q.toS[a] = sar8(t);
        q.fromLo[a] = hi16(wrapSub(f, radius));
        q.fromHi[a] = hi16(wrapAdd(f, radius));
        q.toLo[a] = hi16(wrapSub(t, radius));
        q.toHi[a] = hi16(wrapAdd(t, radius));
    }

    const Tree* tree = nullptr;
    if (q.mask == (F::kLosTreeA | F::kEnabled)) tree = &g_trees.a;
    else if (q.mask == (F::kLosTreeB | F::kEnabled)) tree = &g_trees.b;
    else fatal("los_trace: bad mask %04X (internal error 0x31)", mask);

    // Only the leaves of the two endpoints are searched, and the first object
    // hit in list order wins (not the nearest). No world, no trees: nothing static.
    if (!tree->nodes.empty()) {
        const u32 l1 = losFindLeaf(*tree, hi16u(q.from.x), hi16u(q.from.z));
        const u32 l2 = losFindLeaf(*tree, hi16u(q.to.x), hi16u(q.to.z));
        if (Obj3D* r = losTestLeaf(*tree, l1)) return r;
        if (l2 != l1)
            if (Obj3D* r = losTestLeaf(*tree, l2)) return r;
    }
    if (testDynamic) {
        const std::vector<Obj3D*>& dyn = dynamicObjects();  // DS:07B0, newest first
        if (Obj3D* r = losTestList(dyn.data(), dyn.size())) return r;
    }
    if (testGround && losTestGround()) return losGroundHit();
    return nullptr;
}

} // namespace st::game::mission
