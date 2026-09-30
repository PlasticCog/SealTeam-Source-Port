// Procedural scenery (1000:70B3..784D veg_*, docs/re/seg_1000.md 15): ground
// cover around the Point Man from the 126 spare world objects, vegetation
// scattered around the camera, and ambient flyers placed far away.
#pragma once

#include "core/common.h"
#include "game/types.h"

#include <vector>

namespace st::render {

// Pools created by fx_create_pools (1000:4291): the camera-scatter pool
// DS:2FBC (80 objects of model entry 7) and the ambient flyer pool DS:3100
// (8 objects of model entry 0x51). The mission code creates them in the
// original order and hands them over; vegCreatePools() creates them for
// tools that have no mission (must run before worldEnd()).
void vegSetPools(const std::vector<game::Obj3D*>& scatter, const std::vector<game::Obj3D*>& flyers);
void vegCreatePools();

// wld_probe_kind(pos, 5) (1000:3757): true if a kind-5 blocker (clearing,
// path, bridge, cemetery) lies at world position (x, z). Installed by the
// line-of-sight code; without it nothing blocks.
using BlockerProbe = bool (*)(s32 x, s32 z);
void setBlockerProbe(BlockerProbe fn);

// veg_reset (1000:70B3)
void vegReset();
// veg_update (1000:784D). `cam` = current camera; the Point Man position for
// the ground cover defaults to the camera unless set with vegSetPointMan.
void vegUpdate(const game::Camera& cam, bool force);
void vegSetPointMan(const game::Vec3* pos, bool aboardCraft);

}  // namespace st::render
