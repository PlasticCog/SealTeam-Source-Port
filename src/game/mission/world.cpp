// The mission's world interface (world.h) on top of the renderer's world
// (render/world.h): one object arena shared by the simulation and the
// renderer. This file only adds what the simulation needs on top: the
// dynamic/static object lists as vectors for the LOS module, heightmap
// descriptors, the effect object pools and the body -> unit map.
#include "game/mission/world.h"

#include "data/exeimage.h"
#include "game/mission/craft.h"
#include "game/mission/los.h"
#include "game/mission/state.h"
#include "render/world.h"

#include <map>
#include <string>
#include <unordered_map>

namespace st::game::mission {

namespace {

struct WorldLink {
    std::vector<Obj3D*> dynamicList;   // newest first (render::world().dynamicList)
    std::vector<Obj3D*> staticList;    // cell-list order
    bool dynamicDirty = true;
    std::unordered_map<const Obj3D*, WorldObject*> bodyToWorldObject;
    std::unordered_map<const Obj3D*, Unit*> bodyToUnit;
    std::map<const void*, Heightmap> heightmaps;
    FxPools fx;
};

WorldLink& wl() {
    static WorldLink w;
    return w;
}

void ensureModels() {
    if (!render::modelsLoaded() && !render::modelsLoad()) fatal("mission: models could not be loaded");
}

void refreshDynamic() {
    WorldLink& w = wl();
    if (!w.dynamicDirty) return;
    w.dynamicList.clear();
    for (Obj3D* o = render::world().dynamicList; o; o = o->next) w.dynamicList.push_back(o);
    w.dynamicDirty = false;
}

void refreshStatic() {
    WorldLink& w = wl();
    w.staticList.clear();
    for (const render::GridCell& c : render::world().cells)
        for (Obj3D* o = c.objects; o; o = o->next) w.staticList.push_back(o);
}

// world_end hook: the original calls los_init (4FF8:000C) at the end of world_end.
void onWorldEnd() {
    refreshStatic();
    wl().dynamicDirty = true;
    refreshDynamic();
    losInit();
}

void onWorldFree() { losFreeTrees(); }

} // namespace

const ModelDesc* model(u16 dsOffset) {
    if (dsOffset == 0) return nullptr;
    ensureModels();
    const ModelDesc* d = render::modelByDs(dsOffset);
    if (!d) fatal("mission: no model descriptor at DS:%04X", dsOffset);
    return d;
}

u16 modelOffset(const ModelDesc* desc) { return desc ? render::modelDsOffset(desc) : 0; }

const ModelTableEntry& modelEntry(int i) {
    ensureModels();
    if (i < 0 || i >= kModelCount) i = kModelCount - 1;
    return render::modelTable(i);
}

const Heightmap* modelHeightmap(const ModelDesc* desc) {
    if (!desc || !desc->heightmap) return nullptr;
    WorldLink& w = wl();
    auto it = w.heightmaps.find(desc->heightmap);
    if (it != w.heightmaps.end()) return &it->second;
    // Descriptor bytes in the DGROUP image: +0 far pointer (stored unrelocated),
    // +4 width, +6 depth, +8 x offset, +A z offset, +C cell shift, +E height shift.
    const u8* h = static_cast<const u8*>(desc->heightmap);
    Heightmap hm;
    hm.bytes = exe().at(u16(rd16(h + 2) + ExeImage::kLoadSeg), rd16(h));
    hm.width = rds16(h + 4);
    hm.depth = rds16(h + 6);
    hm.xoff = rds16(h + 8);
    hm.zoff = rds16(h + 0xA);
    hm.cellShift = rds16(h + 0xC);
    hm.heightShift = rds16(h + 0xE);
    if (!hm.bytes) return nullptr;
    return &(w.heightmaps[desc->heightmap] = hm);
}

const char* modelName(const ModelDesc* desc) { return desc ? render::modelName(desc) : ""; }

void worldBegin() {
    ensureModels();
    WorldLink& w = wl();
    render::setWorldHooks(onWorldEnd, onWorldFree);
    render::worldBegin();
    w.dynamicList.clear();
    w.staticList.clear();
    w.dynamicDirty = true;
    w.bodyToWorldObject.clear();
    w.bodyToUnit.clear();
    w.fx = FxPools{};
}

void worldEnd() { render::worldEnd(); }

Obj3D* worldAddObject(const ModelDesc* desc, s32 x, s32 z, u16 flags) {
    wl().dynamicDirty = true;
    return render::worldAddObject(desc, x, z, flags);
}

int objIndex(const Obj3D* o) {
    const u16 off = render::objOffset(o);
    if (off < 2) return -1;
    // The original's record index: (arena offset - 2) / 0x18 (1000:3D05).
    return (off - 2) / 0x18;
}

Obj3D* objByIndex(int k) { return k < 0 ? nullptr : render::objAtOffset(u16(2 + 0x18 * k)); }

int objCount() { return render::objCount(); }

const std::vector<Obj3D*>& dynamicObjects() {
    refreshDynamic();
    return wl().dynamicList;
}

const std::vector<Obj3D*>& staticObjects() { return wl().staticList; }

std::vector<WorldObject*>& worldObjects() { return render::worldObjects(); }

WorldObject* spareObject(int i) { return render::spareObject(i); }

WorldObject* worldObjectOfBody(const Obj3D* o) {
    auto it = wl().bodyToWorldObject.find(o);
    return it == wl().bodyToWorldObject.end() ? nullptr : it->second;
}

bool wldLoad(int world) {
    MissionState& S = ms();
    S.worldIndex = world;
    wl().dynamicDirty = true;
    if (!render::wldLoad(world)) return false;
    for (WorldObject* wo : render::worldObjects()) wl().bodyToWorldObject[wo->body] = wo;
    for (int i = 0; i < kSpareObjects; ++i)
        if (WorldObject* wo = render::spareObject(i)) wl().bodyToWorldObject[wo->body] = wo;
    S.areaName = render::worldDesc().area_name;
    return true;
}

FxPools& fxPools() { return wl().fx; }

void fxCreatePools() {
    MissionState& S = ms();
    FxPools& fx = wl().fx;
    fx.fxObjectCount = 0;
    const ModelTableEntry& prjModel = modelEntry(56);  // bltlt
    for (int i = 0; i < kProjectiles; ++i) {
        Projectile* p = S.projectilePool.alloc();
        p->flight = S.flightPool.alloc();
        p->base_model = prjModel.desc;
        p->body = worldAddObject(prjModel.desc, 24000, 24000, prjModel.object_flags);
        p->body->flags &= u16(~obj3d_flag::kEnabled);
        p->unk_3a[0] = 0xFF;  // +0x3A = 0xFFFF
        p->unk_3a[1] = 0xFF;
        S.projectiles[i] = p;
        ++fx.fxObjectCount;
    }
    fx.aimMarker = worldAddObject(model(0x77F2), 24000, 24000, 1);
    // The original increments the count in the loop condition (9 times for 8 flashes).
    for (int i = 0; i < 8; ++i) {
        ++fx.fxObjectCount;
        fx.flash.push_back(worldAddObject(model(0x95F0), 24000, 24000, 0));
    }
    ++fx.fxObjectCount;
    for (int i = 0; i < 16; ++i) {
        fx.groundFx.push_back(worldAddObject(model(0xBAAE), 24000, 24000, 0));
        ++fx.fxObjectCount;
    }
    for (int i = 0; i < 4; ++i) {
        fx.teamMarker.push_back(worldAddObject(modelEntry(87).desc, 24000, 24000, 0));
        ++fx.fxObjectCount;
    }
    fx.probe = worldAddObject(modelEntry(51).desc, 24000, 24000, 0x1100);
    ++fx.fxObjectCount;
    for (int i = 0; i < 4; ++i) {
        ++fx.fxObjectCount;
        fx.unitMarkers.push_back(worldAddObject(model(0x5D1C), 24000, 24000, 1));
    }
    ++fx.fxObjectCount;
    fxMarkersHide(true);  // fx_markers_hide(1) (1000:592D)
    for (int i = 0; i < 80; ++i) {
        fx.vegetation.push_back(worldAddObject(modelEntry(7).desc, 24000, 24000, 0));
        ++fx.fxObjectCount;
    }
    for (int i = 0; i < 8; ++i) {
        fx.trees.push_back(worldAddObject(modelEntry(81).desc, 24000, 24000, 0));
        ++fx.fxObjectCount;
    }
    S.demoExplodeTime = 0;
}

void registerUnitBody(Unit* u) {
    if (u && u->body) wl().bodyToUnit[u->body] = u;
}

Unit* unitOfBody(const Obj3D* o) {
    auto it = wl().bodyToUnit.find(o);
    return it == wl().bodyToUnit.end() ? nullptr : it->second;
}

void worldFree() {
    WorldLink& w = wl();
    if (render::world().open) {
        render::wldFree();
        render::worldFree();  // runs onWorldFree -> losFreeTrees
    }
    w.dynamicList.clear();
    w.staticList.clear();
    w.dynamicDirty = true;
    w.bodyToWorldObject.clear();
    w.bodyToUnit.clear();
    w.fx = FxPools{};
}

} // namespace st::game::mission
