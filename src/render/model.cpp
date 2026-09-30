// Model loading (2255:0177 mdl_load_all, 003B mdl_load_pnt, 4542 mdl_compile)
// and the vertex transform that replaces the generated code.
#include "render/model.h"

#include "data/ealib.h"
#include "data/exeimage.h"
#include "render/world.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

namespace st::render {

namespace {

constexpr u16 kModelTableSeg = 0x514B;
constexpr u32 kDGroupLinear = u32(ExeImage::kDGroup - ExeImage::kLoadSeg) * 16;

std::vector<u8> g_ds;                  // DGROUP copy
std::vector<ModelInfo> g_models;       // table order
std::vector<ModelTableEntry> g_table;  // table order
std::map<u16, int> g_byDs;             // descriptor DS offset -> index
std::map<u16, std::pair<int, int>> g_lodByDs;  // LOD model DS -> (model, lod)
std::vector<std::string> g_names;
bool g_loaded = false;

VertexSlot g_slots[kVertexSlots];
XformState g_xf;

Hook hookFromFar(u32 farPtr) {
    const u16 seg = u16(farPtr >> 16), off = u16(farPtr);
    if (seg == 0) return Hook::None;
    if (seg != 0x1255) return Hook::Unknown;  // 2255 (load relative)
    switch (off) {
    case 0x02D1: return Hook::Uh1Rotor;
    case 0x031C: return Hook::Mike2;
    case 0x037A: return Hook::Lssc;
    case 0x03B9: return Hook::Ripples;
    case 0x03F4: return Hook::Rippleb;
    case 0x0435: return Hook::Tunnel;
    case 0x0470: return Hook::Cobra;
    case 0x0494: return Hook::Pit;
    default: return Hook::Unknown;
    }
}

// Size in bytes of a primitive record (6.1).
int primSize(u16 p) {
    switch (ds8(p) & 7) {
    case 0: return 7 + ds8(u16(p + 6));
    case 1: return 7;
    case 2: return 6;
    default: return 8;
    }
}

// mdl_find_subobjects (4500): BSP leaves with flag 4 in tree order (A before B).
void findParts(u16 node, std::vector<u16>& out, int depth = 0) {
    if (depth > 64) return;
    const u8 t = ds8(node);
    if (t & 1) {
        if (t & 4) out.push_back(u16(node + 2 + 2 * ds8(u16(node + 1))));
        return;
    }
    findParts(ds16(u16(node + 3)), out, depth + 1);
    findParts(ds16(u16(node + 5)), out, depth + 1);
}

// mdl_signed_digits (A744) + mdl_digits_optimize (A6C8).
void signedDigits(s16 value, int W, s8 d[15]) {
    s8 buf[16] = {};
    s8 s = 1;
    u16 v = u16(value);
    if (value < 0) {
        v = u16(-value);
        s = -1;
    }
    for (int b = 0; b <= 15; ++b) {
        if (!((v >> b) & 1)) continue;
        if (b <= W) {
            buf[b] = s;
        } else if (W >= 0) {
            for (int k = 0; k <= b - W; ++k) buf[W] = s8(buf[W] + s);
        }
    }
    // Replace runs of >= 3 equal-signed digits (A6C8).
    int p = 0;
    const int end = W;
    while (p < W) {
        if (buf[p] == 0) {
            ++p;
            continue;
        }
        const s8 ah = buf[p] > 0 ? 1 : -1;
        int q = p, cx = 0;
        for (;;) {
            if (q >= end) break;
            ++q;
            ++cx;
            if (buf[q] == 0) break;
            if ((buf[q] > 0) != (ah > 0)) break;
        }
        if (cx < 3) {
            ++p;
            continue;
        }
        buf[p] = s8(buf[p] - 2 * ah);
        for (int i = p + 1; i < q; ++i) buf[i] = s8(buf[i] - ah);
        buf[q] = s8(buf[q] + ah);
        // examine p again
    }
    for (int i = 0; i < 15; ++i) d[i] = buf[i];
}

bool compileLod(LodModel& L, const u8* file, size_t fileSize, int lodIndex) {
    const u8 pflags = file[lodIndex];
    const u16 vo = rd16(file + 4 + 2 * lodIndex), po = rd16(file + 10 + 2 * lodIndex);
    if (!vo || !po || size_t(vo) + size_t(L.nv) * 6 > fileSize || size_t(po) + size_t(L.nv) > fileSize) {
        logError("pnt: LOD %d has no vertex data", lodIndex);
        return false;
    }
    L.wide = (pflags & 8) != 0;
    // Window: floor(log2(max |coordinate|)), at most 14 (4542).
    int maxc = 0;
    for (int i = 0; i < L.nv; ++i)
        for (int c = 0; c < 3; ++c) maxc = std::max(maxc, std::abs(int(rds16(file + vo + 6 * i + 2 * c))));
    int W = 14;
    int t = 0x4000;
    while (maxc < t) {
        --W;
        t >>= 1;
        if (W < 0) break;
    }
    L.window = W;
    L.verts.assign(size_t(L.nv), CompiledVertex{});
    for (int i = 0; i < L.nv; ++i) {
        CompiledVertex& cv = L.verts[size_t(i)];
        cv.parent = file[po + i];
        s16 d[3];
        for (int c = 0; c < 3; ++c) d[c] = rds16(file + vo + 6 * i + 2 * c);
        if (cv.parent != 0xFF && cv.parent < L.nv)
            for (int c = 0; c < 3; ++c) d[c] = s16(d[c] - rds16(file + vo + 6 * cv.parent + 2 * c));
        for (int c = 0; c < 3; ++c) {
            cv.delta[c] = d[c];
            signedDigits(d[c], W, cv.digit[c]);
        }
    }
    std::vector<u16> parts;
    if (!(L.flags & 1)) findParts(L.root, parts);
    L.parts.clear();
    for (u16 a : parts) L.parts.push_back(ModelPart{a, ds8(u16(a + 7)), ds8(u16(a + 8))});
    L.bodyLast = L.parts.empty() ? L.nv - 1 : int(L.parts[0].first) - 1;
    L.compiled = true;
    return true;
}

// mdl_apply_colors (4415): one colour byte per primitive into the colour
// word's index byte (type-4 records consume a byte but keep their pointer).
void applyColors(const LodModel& L, const u8* colors) {
    u16 p = u16(L.ds + 0x0E);
    for (int i = 0; i < L.np; ++i) {
        const int type = ds8(p) & 7;
        if (type <= 3) dsW8(u16(p + 3), colors[i]);
        p = u16(p + primSize(p));
    }
}

void buildTables() {
    XformState& x = g_xf;
    for (int a = 0; a < 3; ++a) {
        s32* row14 = x.table[a * 16 + 14];
        row14[0] = row14[1] = row14[2] = 0;
        row14[a] = mk32(0x4000, 0);
        const bool rotated = x.objAngles[0] || x.objAngles[1] || x.objAngles[2];
        if (rotated) mathMatVec(row14, x.obj);
        mathMatVec(row14, x.cam);
        // xf_table_halves (748A): rows 13..0 are successive arithmetic halvings
        // of the high word; their low words stay 0.
        for (int c = 0; c < 3; ++c) {
            s16 h = hi16(row14[c]);
            for (int m = 13; m >= 0; --m) {
                h = s16(h >> 1);
                x.table[a * 16 + m][c] = mk32(h, 0);
            }
        }
    }
    x.key[0] = x.camAngles[0];
    x.key[1] = x.camAngles[1];
    x.key[2] = x.camAngles[2];
    x.key[3] = x.objAngles[0];
    x.key[4] = x.objAngles[1];
    x.key[5] = x.objAngles[2];
}

// xf_tables_current (790C): rebuild when the camera or object angles changed.
void tablesCurrent() {
    XformState& x = g_xf;
    if (x.key[0] == x.camAngles[0] && x.key[1] == x.camAngles[1] && x.key[2] == x.camAngles[2] &&
        x.key[3] == x.objAngles[0] && x.key[4] == x.objAngles[1] && x.key[5] == x.objAngles[2])
        return;
    buildTables();
}

// One vertex range of the generated routine, for all three components.
void transformRange(const LodModel& L, int first, int last) {
    XformState& x = g_xf;
    const int K = x.K;
    for (int i = first; i <= last && i < L.nv && i < kVertexSlots; ++i) {
        const CompiledVertex& cv = L.verts[size_t(i)];
        VertexSlot& v = g_slots[i];
        s32* out[3] = {&v.x, &v.y, &v.z};
        for (int c = 0; c < 3; ++c) {
            s32 base;
            if (cv.parent == 0xFF) base = x.centre[c];
            else base = *(&g_slots[cv.parent].x + c);
            if (L.wide) {
                u32 acc = u32(base);
                for (int a = 0; a < 3; ++a)
                    for (int k = 0; k < 15; ++k) {
                        const int d = cv.digit[a][k];
                        if (!d) continue;
                        const int row = k + K;
                        const u32 t = a * 16 + row < 48 ? u32(x.table[a * 16 + row][c]) : 0u;
                        acc += u32(d) * t;
                    }
                *out[c] = s32(acc);
            } else {
                u16 acc = u16(hi16(base));
                for (int a = 0; a < 3; ++a)
                    for (int k = 0; k < 15; ++k) {
                        const int d = cv.digit[a][k];
                        if (!d) continue;
                        const int row = k + K;
                        const u16 t = a * 16 + row < 48 ? u16(hi16(x.table[a * 16 + row][c])) : 0;
                        acc = u16(acc + u16(d * t));
                    }
                // Only the high word is written; the low word keeps what an
                // earlier 32-bit model left there (2255 porting notes).
                *out[c] = mk32(s16(acc), lo16(*out[c]));
            }
        }
    }
}

}  // namespace

// ---------------------------------------------------------------- DS image

u8* dsImage() { return g_ds.data(); }

u16 farWord(u16 seg, u16 off) {
    const u32 lin = u32(seg - ExeImage::kLoadSeg) * 16 + off;
    if (lin >= kDGroupLinear && lin + 1 < kDGroupLinear + 0x10000 && !g_ds.empty())
        return ds16(u16(lin - kDGroupLinear));
    const u8* p = exe().at(u16(ExeImage::kLoadSeg + (lin >> 4)), u16(lin & 15));
    return p ? rd16(p) : 0;
}

VertexSlot* vertexSlots() { return g_slots; }
XformState& xform() { return g_xf; }

void xfInvalidate() {
    for (s16& k : g_xf.key) k = -1;
    g_xf.objMatAngles[0] = g_xf.objMatAngles[1] = g_xf.objMatAngles[2] = -1;
}

// The generated routine (mdl_compile): prologue xf_tables_current, then per
// part [part-begin] body [part-end]. A part with all-zero angles does not
// rebuild the tables, so it keeps whatever tables are current - including
// those of a preceding rotated part (original behaviour).
void xfTransform(const LodModel& L) {
    XformState& x = g_xf;
    tablesCurrent();
    transformRange(L, 0, L.bodyLast);
    for (const ModelPart& p : L.parts) {
        const s16 a0 = dsS16(p.angles), a1 = dsS16(u16(p.angles + 2)), a2 = dsS16(u16(p.angles + 4));
        const bool rotated = a0 || a1 || a2;
        Mat3 saved = x.obj;
        if (rotated) {
            // part-begin template (A640)
            Mat3 part = mathRotMatrix(a0, a1, a2);
            if (x.objAngles[0] || x.objAngles[1] || x.objAngles[2]) mathMatMul(part, saved);
            x.obj = part;
            const s16 keep = x.objAngles[0];
            x.objAngles[0] = -1;  // forces xf_build_tables to apply F62C
            buildTables();
            x.objAngles[0] = keep;
        }
        transformRange(L, p.first, p.last);
        if (rotated) {
            // part-end template (A6AC)
            x.obj = saved;
            x.key[0] = -1;
        }
    }
}

const ModelInfo* modelInfo(const game::ModelDesc* desc) {
    if (!desc || g_models.empty()) return nullptr;
    const ModelInfo* base = g_models.data();
    const auto* p = reinterpret_cast<const u8*>(desc);
    const auto* b = reinterpret_cast<const u8*>(&base[0].desc);
    const size_t stride = sizeof(ModelInfo);
    if (p < b) return nullptr;
    const size_t i = size_t(p - b) / stride;
    if (i >= g_models.size() || &g_models[i].desc != desc) return nullptr;
    return &g_models[i];
}

const LodModel* lodModelAt(u16 ds) {
    const auto it = g_lodByDs.find(ds);
    if (it == g_lodByDs.end()) return nullptr;
    return &g_models[size_t(it->second.first)].lod[it->second.second];
}

void modelsSetTimeOfDay(int hour) {
    if (g_ds.empty()) return;
    dsW16(0xC5DE, 0x7800);
    int d = hour - 12;
    if (d < 0) d = -d;
    const int i = d >> 2;
    dsW16(0xAEE7, u16(0x5A00 | ds8(u16(0x07B6 + i))));
    const u16 t = u16(0xFF00 | ds8(u16(0x07BA + i)));
    dsW16(0xC49A, t);
    dsW16(0xC493, t);
    dsW16(0xC4A1, t);
}

// ---------------------------------------------------------------- world.h model API

bool modelsLoaded() { return g_loaded; }

bool modelsLoad() {
    if (g_loaded) return true;
    if (!mathInit()) return false;
    const u8* dg = exe().dg(0);
    if (!dg) {
        logError("models: st.exe not loaded");
        return false;
    }
    g_ds.assign(dg, dg + 0x10000);

    // r3d_set_default_aspect (3E73): aspect 20:23, y is scaled.
    const u32 aspectBa = (u32(20) << 16) / 23;  // DS:54E4

    g_models.clear();
    g_table.clear();
    g_byDs.clear();
    g_lodByDs.clear();
    g_names.clear();
    std::vector<u16> descs;
    for (int i = 0; i < 128; ++i) {
        const u8* e = exe().at(kModelTableSeg, u16(i * 6));
        if (!e || rd16(e) == 0) break;
        descs.push_back(rd16(e));
    }
    g_models.resize(descs.size());
    g_table.resize(descs.size());
    g_names.resize(descs.size());
    for (size_t i = 0; i < descs.size(); ++i) {
        const u16 d = descs[i];
        const u8* e = exe().at(kModelTableSeg, u16(i * 6));
        ModelInfo& m = g_models[i];
        m.ds = d;
        m.tableIndex = int(i);
        game::ModelDesc& md = m.desc;
        md.layer = ds8(d);
        md.unk_01 = ds8(u16(d + 1));
        md.radius = dsS16(u16(d + 2));
        md.radius_x = md.radius;  // g_aspect_x is 0 in the game
        md.radius_y = s16(u32(s32(md.radius) * s32(aspectBa)) >> 16);
        dsW16(u16(d + 4), u16(md.radius_x));
        dsW16(u16(d + 6), u16(md.radius_y));
        md.radius_world = s32(u32(ds16(u16(d + 8))) | (u32(ds16(u16(d + 10))) << 16));
        md.scale_shift = s8(ds8(u16(d + 0x0C)));
        md.unk_0d = ds8(u16(d + 0x0D));
        for (int k = 0; k < 3; ++k) {
            md.lod_distance[k] = ds16(u16(d + 0x0E + 2 * k));
            md.lod_model[k] = ds16(u16(d + 0x14 + 2 * k));
        }
        md.size_class = dsS16(u16(d + 0x1A));
        md.lod_callback = nullptr;  // no game model has one (segment 0)
        md.unk_20 = ds16(u16(d + 0x20));
        g_names[i] = exe().dgString(ds16(u16(d + 0x22)));
        md.pnt_name = reinterpret_cast<const char*>(&g_ds[ds16(u16(d + 0x22))]);
        md.swap_copy = nullptr;
        md.swap_reloc = ds16(u16(d + 0x28));
        md.swap_size = ds16(u16(d + 0x2A));
        md.default_height = ds16(u16(d + 0x2C));
        md.game_flags = ds16(u16(d + 0x2E));
        auto rd32ds = [&](u16 o) { return s32(u32(ds16(o)) | (u32(ds16(u16(o + 2))) << 16)); };
        md.box_min_x = rd32ds(u16(d + 0x30));
        md.box_max_x = rd32ds(u16(d + 0x34));
        md.box_min_y = rd32ds(u16(d + 0x38));
        md.box_max_y = rd32ds(u16(d + 0x3C));
        md.box_min_z = rd32ds(u16(d + 0x40));
        md.box_max_z = rd32ds(u16(d + 0x44));
        const u16 hm = ds16(u16(d + 0x48));
        md.heightmap = hm ? exe().dg(hm) : nullptr;
        g_byDs[d] = int(i);

        ModelTableEntry& te = g_table[i];
        te.desc = &m.desc;
        te.kind = game::TerrainKind(rd16(e + 2));
        te.object_flags = rd16(e + 4);
    }

    // mdl_load_pnt per model
    for (size_t i = 0; i < g_models.size(); ++i) {
        ModelInfo& m = g_models[i];
        std::vector<u8> file;
        if (!resources().read(g_names[i] + ".pnt", file) || file.size() < 0x1C) {
            logError("models: cannot read %s.pnt", g_names[i].c_str());
            return false;
        }
        m.lodCount = 0;
        for (int k = 0; k < 3; ++k) {
            const u16 lm = m.desc.lod_model[k];
            if (!lm) break;
            LodModel& L = m.lod[k];
            L.ds = lm;
            L.np = ds8(lm);
            L.nv = ds8(u16(lm + 1));
            L.hook = hookFromFar(u32(ds16(u16(lm + 2))) | (u32(ds16(u16(lm + 4))) << 16));
            L.root = ds16(u16(lm + 0x0A));
            L.nFacing = ds8(u16(lm + 0x0C));
            L.flags = ds8(u16(lm + 0x0D));
            if (L.nv > kVertexSlots) {
                logError("models: %s has %d vertices", g_names[i].c_str(), L.nv);
                return false;
            }
            if (L.flags & 2) {
                if (!compileLod(L, file.data(), file.size(), k)) return false;
            }
            const u16 co = rd16(&file[0x10 + 2 * k]);
            if (co && size_t(co) + size_t(L.np) <= file.size()) applyColors(L, &file[co]);
            g_lodByDs[lm] = {int(i), k};
            m.lodCount = k + 1;
        }
    }
    modelsSetTimeOfDay(12);
    // r3d_set_aspect invalidates every cache key.
    xfInvalidate();
    g_loaded = true;
    logInfo("models: %d models loaded", int(g_models.size()));
    return true;
}

const ModelTableEntry& modelTable(int index) {
    static ModelTableEntry none{};
    if (index < 0 || index >= int(g_table.size())) return none;
    return g_table[size_t(index)];
}

const ModelDesc* modelByIndex(int index) { return modelTable(index).desc; }

const ModelDesc* modelByDs(u16 dsOffset) {
    const auto it = g_byDs.find(dsOffset);
    return it == g_byDs.end() ? nullptr : &g_models[size_t(it->second)].desc;
}

u16 modelDsOffset(const ModelDesc* desc) {
    const ModelInfo* m = modelInfo(desc);
    return m ? m->ds : 0;
}

int modelIndex(const ModelDesc* desc) {
    const ModelInfo* m = modelInfo(desc);
    return m ? m->tableIndex : -1;
}

const char* modelName(const ModelDesc* desc) {
    const ModelInfo* m = modelInfo(desc);
    return m ? g_names[size_t(m->tableIndex)].c_str() : "";
}

}  // namespace st::render
