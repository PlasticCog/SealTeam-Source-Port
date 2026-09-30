// The 3D world: model registry, object arena, world grid and the objects of a
// <world>.w file. Port of segment 2255 section 7 (world_begin / world_add_object
// / world_end / world_free, object lists, grid) and 1000:3EFD (wld_load);
// docs/re/seg_2255.md 6-7, docs/re/seg_1000.md 12, docs/re/STRUCTURES.md.
//
// This header is the stable interface other modules (mission simulation, line
// of sight 4FF8, effects) use to create and query world objects. The renderer
// itself is in render/r3d.h.
//
// Object records are the canonical game::Obj3D (types.h): position in world
// units (24.8: game/map units << 8), heading/pitch/roll in 1/8 degree, model
// descriptor pointer, flags (obj3d_flag::*), `next` link of the list the
// object is in. Game code may modify position, angles, flags and model of an
// object at any time (exactly like the original did); the renderer reads them
// every frame.
//
// Typical mission set-up (1000:4580 mis_load_world):
//     render::worldBegin();                 // 2255:01E7
//     render::wldLoad(worldIndex);          // 1000:3EFD .w/.wd + 126 spare objects
//     ... fx pools, units: render::worldAddObject(model, x, z, flags) ...
//     render::worldEnd();                   // 2255:02B0 (then los_init)
//     ... per frame: render::renderView(...) (render/r3d.h) ...
//     render::worldFree();                  // 2255:01CF (after los_free)
#pragma once

#include "core/common.h"
#include "game/types.h"

#include <string>
#include <vector>

namespace st::render {

using game::ModelDesc;
using game::ModelTableEntry;
using game::Obj3D;
using game::Vec3;
using game::WorldObject;

// ===========================================================================
// Models (2255 section 6). Descriptors, LOD models, primitives and BSP trees
// live in st.exe's DGROUP; vertex coordinates come from the .pnt files of
// point.lib. Everything is read from the user's files at run time.
// ===========================================================================

constexpr int kModelCount = 97;  // entries of the model table far 514B:0000

// mdl_load_all (2255:0177): read the model table, descriptors and .pnt
// files, prepare the vertex transforms. Needs the archives and st.exe (call
// after start-up). Safe to call more than once. Returns false (and logs) if
// data is missing.
bool modelsLoad();
bool modelsLoaded();

// Model table entry i (0..96): descriptor, TerrainKind, world_add_object flags.
const ModelTableEntry& modelTable(int index);
const ModelDesc* modelByIndex(int index);
// Descriptor at DGROUP offset `dsOffset` (the value the original stores in
// Obj3D +0 / WorldObject +0 / Unit +0, e.g. 0x862A human, 0x7A40 dmgd);
// nullptr if no model descriptor starts there.
const ModelDesc* modelByDs(u16 dsOffset);
u16 modelDsOffset(const ModelDesc* desc);  // inverse; 0 for nullptr / unknown
int modelIndex(const ModelDesc* desc);     // table index, -1 if unknown
const char* modelName(const ModelDesc* desc);  // .pnt base name ("human"), "" if unknown
// mdl_time_of_day_colors (2255:000A): pit and tunnel colours for the hour;
// the game calls it at mission start (hud_reset_mission 1000:1EB0).
void modelsSetTimeOfDay(int hour);

// ===========================================================================
// Object arena and world (2255 section 7)
// ===========================================================================

// World grid cell (2255:A0A0). The game builds a 24000 x 24000 game-unit
// world, which is always a single 1 x 1 cell holding every static object.
struct GridCell {
    Obj3D* objects = nullptr;                // +0 static objects (list front = last created)
    std::vector<const GridCell*> visible;    // +2 other cells whose objects can be seen from here
};

// World record (DS:07A4).
struct World {
    u8 groundColor = 0xFF;         // +00 flat ground colour for r3d_draw_sky_ground (0xFF = none)
    u8 skyColor = 0xFF;            // +01 flat sky colour
    s16 horizonTerm = 0;           // +02 horizon elevation term
    int gridW = 0, gridH = 0;      // +04 / +06 cells
    std::vector<GridCell> cells;   // +08 row-major, gridW * gridH
    Obj3D* dynamicList = nullptr;  // +0C objects created without kStaticGrid (list front = last created)
    bool open = false;             // between worldBegin and worldFree
    bool built = false;            // worldEnd done (grid valid)
};

World& world();

// world_begin (2255:01E7): reset the default view (object list, projection
// caches), clear the world and open a new object arena.
void worldBegin();

// world_add_object (2255:0234): create an object of model `desc` at game
// units (x, z) (stored << 8); altitude = desc->default_height (world units),
// angles 0, flags as given. flags & kStaticGrid: static scenery, distributed
// into the grid by worldEnd(); otherwise pushed at the front of the dynamic
// list. With obj3d_flag::kStatic the original record has no angles (the port
// keeps the fields but the renderer ignores them). Fatal when the 0x4C90-byte
// arena is full, like the original. Returns the new object.
Obj3D* worldAddObject(const ModelDesc* desc, s32 x, s32 z, u16 flags);

// world_end (2255:02B0): close the arena and build the grid. Then runs the
// hook set with setWorldHooks (the original calls los_init 4FF8:000C here).
void worldEnd();

// world_free (2255:01CF): runs the free hook (los_free_trees), frees the view
// list, the grid and the arena. All Obj3D pointers become invalid. Also
// drops the high-resolution layers of the Enhanced preset.
void worldFree();

// Optional callbacks run at the end of worldEnd() / the start of worldFree()
// (the line-of-sight module builds and frees its quadtrees there).
void setWorldHooks(void (*onEnd)(), void (*onFree)());

// Arena bookkeeping. The original addresses objects by their 16-bit offset in
// the arena segment: the first object is at 2, records take 0x18 bytes
// (0x12 with kStatic) in creation order. Game code derives indices from it
// (e.g. 1000:3D05: world object index = (offset - 2) / 0x18).
u16 objOffset(const Obj3D* obj);   // 0 for nullptr / not an arena object
Obj3D* objAtOffset(u16 offset);    // nullptr if no object starts there
int objCount();                    // objects created since worldBegin (DS:07B4)
Obj3D* objByIndex(int i);          // i-th created object (0-based)

// world_cell_at (2255:42C9): cell containing world position (x, z) (world
// units), clamped to the grid. nullptr before worldEnd().
const GridCell* worldCellAt(s32 x, s32 z);

// world_foreach (2255:421D): fn(obj) for every object of the dynamic list,
// then every object of every cell list (row-major), in list order.
template <class Fn>
void worldForeach(Fn&& fn) {
    World& w = world();
    for (Obj3D* o = w.dynamicList; o; o = o->next) fn(o);
    for (GridCell& c : w.cells)
        for (Obj3D* o = c.objects; o; o = o->next) fn(o);
}

// ===========================================================================
// World objects of a <world>.w file (1000:3EFD wld_load, seg_1000.md 12.1)
// ===========================================================================

constexpr int kWorldFileMaxObjects = 524;
constexpr int kSpareObjects = 126;      // ground-cover spares DS:31BA
constexpr int kWorldNameCount = 33;     // g_world_names (DS:1CD0): mekong1..camp5

// Name of world n (0..32) from st.exe, e.g. "mekong1"; "" if out of range.
std::string worldName(int index);

// wld_load: must be called between worldBegin() and worldEnd(). Reads
// <world>.w (18-byte records, max 524), creates one WorldObject + Obj3D per
// record with the original per-kind overrides, reads <world>.wd, then appends
// the 126 spare ground-cover objects. Returns false if the file is missing.
bool wldLoad(int worldIndex);
// wld_free (1000:44FB): forget the world object table (the Obj3D records go
// with the arena in worldFree()).
void wldFree();

int worldIndex();                         // g_world_index, -1 if none loaded
const game::WorldDesc& worldDesc();       // <world>.wd (area name at +1)
// g_world_objects (DS:26F4): the .w objects followed by the spares.
// Pointers stay valid until wldFree()/wldLoad().
std::vector<WorldObject*>& worldObjects();
int worldFileObjectCount();               // number of .w records loaded (spares follow)
WorldObject* spareObject(int i);          // g_spare_objects[i] (DS:31BA), 0..125

}  // namespace st::render
