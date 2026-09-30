// The mission's view of the 3D world: model descriptors, the object arena
// (2255:0234 world_add_object), the world/structure object table loaded from
// <world>.w (1000:3EFD wld_load) and the effect object pools (1000:4291
// fx_create_pools).
//
// This is the small interface the simulation uses; world.cpp implements it on
// top of the renderer's world (src/render/world.h), so the simulation and the
// renderer share one object arena. Object creation order matters: the LOS
// module walks the dynamic list newest-first and the static list in
// reverse creation order, exactly as the original grid/list code does.
#pragma once

#include "game/types.h"

#include <vector>

namespace st::game::mission {

constexpr int kModelCount = 97;      // far 514B:0000
constexpr int kSpareObjects = 126;   // g_spare_objects DS:31BA

// Optional heightmap of a model (descriptor +0x48, used by the LOS test).
struct Heightmap {
    const u8* bytes = nullptr;  // +0 far pointer to width*depth height bytes
    s16 width = 0;              // +4
    s16 depth = 0;              // +6
    s16 xoff = 0;               // +8
    s16 zoff = 0;               // +A
    s16 cellShift = 0;          // +C
    s16 heightShift = 0;        // +E
};

// ---- Models ---------------------------------------------------------------
// Model descriptor by its DGROUP offset (e.g. 0x862A human); nullptr for 0.
const ModelDesc* model(u16 dsOffset);
// DGROUP offset of a descriptor returned by model() (0 for nullptr).
u16 modelOffset(const ModelDesc* desc);
// Model table entry i (0..96) of far 514B:0000.
const ModelTableEntry& modelEntry(int i);
// Heightmap of a model (nullptr if none).
const Heightmap* modelHeightmap(const ModelDesc* desc);
// Base name of the model's .pnt file (HUD target name), "" if none.
const char* modelName(const ModelDesc* desc);

// ---- Object arena (2255:0234, 2255:01E7 world_begin, 2255:02B0 world_end) --
void worldBegin();
// Build the static grid and the LOS trees (calls losInit()).
void worldEnd();
// world_add_object(desc, x, z, flags): x/z in game units; altitude =
// desc->default_height; angles 0. Flag 0x4000 = static grid object,
// otherwise the object is pushed at the front of the dynamic list.
Obj3D* worldAddObject(const ModelDesc* desc, s32 x, s32 z, u16 flags);
// Arena record index k of an object (hit id = 2 + 0x18 * k), -1 if unknown.
int objIndex(const Obj3D* o);
Obj3D* objByIndex(int k);
int objCount();
// Dynamic list (DS:07B0) in list order: newest object first.
const std::vector<Obj3D*>& dynamicObjects();
// Static grid objects in cell-list order (reverse creation order).
const std::vector<Obj3D*>& staticObjects();

// ---- World objects (g_world_objects DS:26F4, count DS:ECB6) --------------
std::vector<WorldObject*>& worldObjects();
WorldObject* spareObject(int i);  // g_spare_objects DS:31BA
// 1000:3EFD: load <world>.w and <world>.wd, create the objects and the 126
// spare ground-cover objects. Returns false if the world file is missing.
bool wldLoad(int world);
// The WorldObject whose body is `o` (wld_object_from_hit 1000:3D05), or nullptr.
WorldObject* worldObjectOfBody(const Obj3D* o);

// ---- Effect object pools (1000:4291 fx_create_pools) ----------------------
struct FxPools {
    Obj3D* aimMarker = nullptr;               // DS:2F28 grenade target box (crosshc)
    std::vector<Obj3D*> flash;                // DS:2F70 muzzle flashes (8)
    std::vector<Obj3D*> groundFx;             // DS:2F2C shadows/wakes (16)
    std::vector<Obj3D*> teamMarker;           // DS:2F94 sampans (4)
    Obj3D* probe = nullptr;                   // DS:2FA8 collision probe (human model, flags 0x1100)
    std::vector<Obj3D*> unitMarkers;          // DS:2FAC map unit markers (4)
    std::vector<Obj3D*> vegetation;           // DS:2FBC camera-scatter vegetation (80)
    std::vector<Obj3D*> trees;                // DS:3100 distant trees / ambient flyers (8)
    int fxObjectCount = 0;                    // DS:ECF8
};
FxPools& fxPools();
// Creates the 32 projectiles (ms().projectiles) and the pools above.
void fxCreatePools();

// Units owning a body (19ac:61FA grp_seglist_add / 622C grp_from_object_id).
void registerUnitBody(Unit* u);
Unit* unitOfBody(const Obj3D* o);

// Release everything (1000:45A6 mis_free_world).
void worldFree();

} // namespace st::game::mission
