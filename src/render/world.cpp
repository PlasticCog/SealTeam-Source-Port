// Object arena, world grid and the .w world loader (2255 section 7, 1000:3EFD).
#include "render/world.h"

#include "data/ealib.h"
#include "data/exeimage.h"
#include "gfx/gfx.h"
#include "platform/system.h"
#include "render/r3d.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace st::render {

namespace {

// ---------------------------------------------------------------- arena

constexpr int kArenaBytes = 0x4C90;   // obj_arena_init size (2255:0224)
constexpr int kArenaFirst = 2;        // first record offset (2255:3B63)
constexpr int kArenaMaxObjects = (kArenaBytes - kArenaFirst) / 0x12 + 1;

struct Arena {
    std::unique_ptr<Obj3D[]> objs{new Obj3D[kArenaMaxObjects]};
    u16 offsets[kArenaMaxObjects]{};
    int count = 0;
    int used = kArenaFirst;           // next free byte offset (DS:CF28 relative to the base)
    Obj3D* chainHead = nullptr;       // static objects in creation order (DS:CF1C)
    Obj3D* chainTail = nullptr;       // DS:CF2C
};

Arena& arena() {
    static Arena a;
    return a;
}

void (*g_onEnd)() = nullptr;
void (*g_onFree)() = nullptr;

// ---------------------------------------------------------------- .w objects

struct WorldFileState {
    int index = -1;
    game::WorldDesc desc{};
    std::vector<std::unique_ptr<WorldObject>> storage;
    std::vector<WorldObject*> table;   // g_world_objects
    int fileCount = 0;
    WorldObject* spares[kSpareObjects]{};
};

WorldFileState& wfs() {
    static WorldFileState s;
    return s;
}

s32 clampCell(s32 v, int n) { return std::clamp<s32>(v, 0, n - 1); }

}  // namespace

World& world() {
    static World w;
    return w;
}

void setWorldHooks(void (*onEnd)(), void (*onFree)()) {
    g_onEnd = onEnd;
    g_onFree = onFree;
}

// ---------------------------------------------------------------- world_begin / add / end / free

void worldBegin() {
    // view_init(0x74A, world, persist 0, subsample 2, max 200, cache on, age 25, rot 16)
    viewInit();
    // world_clear
    World& w = world();
    w.groundColor = 0xFF;
    w.skyColor = 0xFF;
    w.horizonTerm = 0;
    w.gridW = w.gridH = 0;
    w.cells.clear();
    w.dynamicList = nullptr;
    w.open = true;
    w.built = false;
    // obj_arena_init
    Arena& a = arena();
    a.count = 0;
    a.used = kArenaFirst;
    a.chainHead = a.chainTail = nullptr;
}

Obj3D* worldAddObject(const ModelDesc* desc, s32 x, s32 z, u16 flags) {
    Arena& a = arena();
    World& w = world();
    if (!w.open) fatal("world_add_object outside world_begin/world_free");
    // obj_arena_store: 0x12 bytes when the record is static (flag 2), else 0x18.
    const int size = (flags & game::obj3d_flag::kStatic) ? 0x12 : 0x18;
    if (a.used + size > kArenaBytes - 1 || a.count >= kArenaMaxObjects) fatal("Not enough memory.");
    Obj3D* o = &a.objs[a.count];
    *o = Obj3D{};
    o->model = desc;
    o->flags = flags;
    o->next = nullptr;
    o->pos.x = s32(u32(x) << 8);
    o->pos.y = desc ? s32(desc->default_height) : 0;  // zero-extended word (2255:0266)
    o->pos.z = s32(u32(z) << 8);
    a.offsets[a.count] = u16(a.used);
    a.used += size;
    ++a.count;
    if (flags & game::obj3d_flag::kStaticGrid) {
        // obj_arena_append: link at the end of the arena chain.
        if (!a.chainHead) a.chainHead = o;
        if (a.chainTail) a.chainTail->next = o;
        a.chainTail = o;
    } else {
        // obj_list_add: push at the front of the world's dynamic list.
        o->next = w.dynamicList;
        w.dynamicList = o;
    }
    return o;
}

void worldEnd() {
    Arena& a = arena();
    World& w = world();
    // obj_arena_close
    if (a.chainTail) a.chainTail->next = nullptr;
    // world_build_grid(world, chain, 24000, 24000): extents are longs in game
    // units; the width is the high word plus one if the low word is non-zero.
    const s32 ext = 24000;
    w.gridW = std::max(1, int(u32(ext) >> 16) + ((ext & 0xFFFF) != 0 ? 1 : 0));
    w.gridH = w.gridW;
    w.cells.assign(size_t(w.gridW * w.gridH), GridCell{});
    Obj3D* o = a.chainHead;
    while (o) {
        Obj3D* next = o->next;
        const int cx = clampCell(o->pos.x >> 24, w.gridW);
        const int cz = clampCell(o->pos.z >> 24, w.gridH);
        GridCell& c = w.cells[size_t(cz * w.gridW + cx)];
        o->next = c.objects;  // list_push: front
        c.objects = o;
        o = next;
    }
    // Cells whose objects may be visible from each other. The game's world is
    // a single cell, so this is never exercised; a multi-cell grid would list
    // every other cell (conservative).
    if (w.cells.size() > 1)
        for (GridCell& c : w.cells)
            for (const GridCell& d : w.cells)
                if (&c != &d && d.objects) c.visible.push_back(&d);
    w.built = true;
    if (g_onEnd) g_onEnd();
}

void worldFree() {
    if (g_onFree) g_onFree();
    viewFree();
    sys().video().dropHiResLayers();
    gfx().refreshCoverage();
    World& w = world();
    w.cells.clear();
    w.gridW = w.gridH = 0;
    w.dynamicList = nullptr;
    w.open = false;
    w.built = false;
    Arena& a = arena();
    a.count = 0;
    a.used = kArenaFirst;
    a.chainHead = a.chainTail = nullptr;
}

// ---------------------------------------------------------------- arena queries

u16 objOffset(const Obj3D* obj) {
    Arena& a = arena();
    if (!obj || obj < &a.objs[0] || obj >= &a.objs[0] + a.count) return 0;
    return a.offsets[obj - &a.objs[0]];
}

Obj3D* objAtOffset(u16 offset) {
    Arena& a = arena();
    const u16* begin = a.offsets;
    const u16* end = begin + a.count;
    const u16* it = std::lower_bound(begin, end, offset);
    if (it == end || *it != offset) return nullptr;
    return &a.objs[it - begin];
}

int objCount() { return arena().count; }

Obj3D* objByIndex(int i) {
    Arena& a = arena();
    return (i >= 0 && i < a.count) ? &a.objs[i] : nullptr;
}

const GridCell* worldCellAt(s32 x, s32 z) {
    World& w = world();
    if (!w.built || w.cells.empty()) return nullptr;
    const int cx = clampCell(x >> 24, w.gridW);
    const int cz = clampCell(z >> 24, w.gridH);
    return &w.cells[size_t(cz * w.gridW + cx)];
}

// ---------------------------------------------------------------- wld_load (1000:3EFD)

std::string worldName(int index) {
    if (index < 0 || index >= kWorldNameCount) return {};
    return exe().dgStringPtr(u16(0x1CD0 + 2 * index));
}

bool wldLoad(int index) {
    WorldFileState& s = wfs();
    wldFree();
    s.index = index;
    const std::string base = worldName(index);
    std::vector<u8> data;
    if (base.empty() || !resources().read(base + exe().dgString(0x33BA), data)) {
        logError("wld_load: cannot read %s.w", base.c_str());
        return false;
    }
    const int n = int(data.size() / 18);
    for (int i = 0; i < n && int(s.table.size()) < kWorldFileMaxObjects; ++i) {
        const u8* r = &data[size_t(i) * 18];
        const u8 type = r[10];
        const ModelTableEntry& e = modelTable(type);
        auto wo = std::make_unique<WorldObject>();
        wo->model = e.desc;
        wo->body = worldAddObject(e.desc, s32(rd32(r + 2)), s32(rd32(r + 6)), e.object_flags);
        wo->kind = e.kind;
        // Heading code h: 2h degrees, plus one when 2h is not a multiple of 15.
        int deg = r[0] * 2;
        if (deg % 15 != 0) deg += 1;
        wo->body->heading = s16(deg << 3);
        wo->flags = rd16(r + 12);
        wo->cover = r[16];
        wo->height = r[17];
        wo->hit_points = s16(rd16(r + 14));
        const u16 k = u16(wo->kind);
        if ((k > 6 && k < 10) || k == 0x12 || k == 5 || k == 4 || k == 0xB) {
            wo->cover = 0;
            wo->height = 0x1F;
        }
        if (k == 0x10) wo->hit_points = 0x32;
        if (k == 3) wo->hit_points = 0x19;
        if (k == 6) wo->height = 0x7F;
        if (type == 0x28 || type == 0x2B) wo->cover = 99;
        if (type == 0x20) wo->cover = 100;
        s.table.push_back(wo.get());
        s.storage.push_back(std::move(wo));
    }
    s.fileCount = int(s.table.size());

    std::vector<u8> wd;
    s.desc = game::WorldDesc{};
    if (resources().read(base + exe().dgString(0x33BA) + exe().dgString(0x33BD), wd) && !wd.empty()) {
        s.desc.unk_00 = wd[0];
        const size_t len = std::min(wd.size() - 1, sizeof(s.desc.area_name) - 1);
        std::memcpy(s.desc.area_name, wd.data() + 1, len);
        s.desc.area_name[sizeof(s.desc.area_name) - 1] = 0;
    }

    // 126 spare ground-cover objects of model table entry 0x26, not enabled.
    const ModelTableEntry& e = modelTable(0x26);
    for (int i = 0; i < kSpareObjects; ++i) {
        auto wo = std::make_unique<WorldObject>();
        wo->model = e.desc;
        wo->body = worldAddObject(e.desc, 0, 0, u16(e.object_flags & 0xFFFE));
        wo->kind = e.kind;
        wo->flags = 0;
        if (u16(wo->kind) == 3) {
            wo->cover = 0x19;
            wo->height = 0x3F;
            wo->hit_points = 0x19;
        } else {
            if (u16(wo->kind) == 6) wo->hit_points = 0x32;
            wo->cover = 100;
            wo->height = 0x7F;
        }
        s.spares[i] = wo.get();
        if (int(s.table.size()) < kWorldFileMaxObjects) s.table.push_back(wo.get());
        s.storage.push_back(std::move(wo));
    }
    return true;
}

void wldFree() {
    WorldFileState& s = wfs();
    s.table.clear();
    s.storage.clear();
    s.fileCount = 0;
    std::fill(std::begin(s.spares), std::end(s.spares), nullptr);
}

int worldIndex() { return wfs().index; }
const game::WorldDesc& worldDesc() { return wfs().desc; }
std::vector<WorldObject*>& worldObjects() { return wfs().table; }
int worldFileObjectCount() { return wfs().fileCount; }
WorldObject* spareObject(int i) { return (i >= 0 && i < kSpareObjects) ? wfs().spares[i] : nullptr; }

}  // namespace st::render
