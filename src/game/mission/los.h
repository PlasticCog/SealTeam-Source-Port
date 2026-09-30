// Line-of-sight and collision queries (segment 4ff8, docs/re/seg_libs.md 15):
// two static quadtrees over the grid objects (mask 0x1000 = sight-blocking,
// mask 0x0100 = terrain probes) plus the dynamic object list, and a precise
// segment-versus-model-box test with optional heightmap.
#pragma once

#include "game/types.h"

namespace st::game::mission {

// 4ff8:000C los_init: build tree A (mask 0x1000) and tree B (mask 0x0100)
// from the static grid objects (world.h staticObjects()).
void losInit();
// 4ff8:006C
void losFreeTrees();

// Result of a ground hit (0xFFFF in the original).
Obj3D* losGroundHit();
inline bool isGroundHit(const Obj3D* o) { return o && o == losGroundHit(); }

// 4ff8:0148 los_trace: segment `from`..`to` grown by `radius` (24.8 units)
// against the two endpoint leaves of the tree selected by `mask` (0x1000 or
// 0x0100), then the dynamic list (testDynamic) and the ground (testGround).
// Objects must have (flags & (mask|1)) == (mask|1) and differ from `ignore`.
// Returns the first object hit in list order, losGroundHit(), or nullptr.
// The hit point is written to *hitOut when an object is hit (the callers
// pass ms().losHit).
Obj3D* losTrace(const Obj3D* ignore, const Vec3& from, const Vec3& to, s32 radius, Vec3* hitOut, u16 mask,
                bool includeFlagged, bool testGround, bool testDynamic);

} // namespace st::game::mission
