// World queries of segment 1000 (1000:314C..3E85, 6648..671C, 695A) built on
// the LOS module: ray/probe traces, terrain kind probes, cover sums, nearest
// objects, objective structure state.
#pragma once

#include "game/types.h"

namespace st::game::mission {

// 1000:314C geo_ray_to_object: los_trace(from, from->pos, to, 0x300,
// &losHit, 0x1000, include 0, ground 1, dynamic 1); a ground hit gives nullptr.
Obj3D* geoRayToObject(const Obj3D* from, const Vec3& to);
// 1000:318E geo_probe_at: los_trace(o, o->pos, o->pos + (0x600, 0, 0x600),
// 0x600, &losHit, 0x0100, 0, 1, 1); may return losGroundHit().
Obj3D* geoProbeAt(const Obj3D* o);
// 1000:3D05: world object whose body is `hit` (index < world object count).
WorldObject* wldObjectFromHit(const Obj3D* hit);
// 1000:33CD: objective structure (kind 3 slots) within 3000 of the unit, last
// match wins; else world object 0 (returns false).
bool wldFindMissionPoint(const Unit* u, WorldObject*& out);
// 1000:3503: same but the object must have hit points > 0; the fallback is
// the world object nearest to the Point Man.
bool wldFindLiveMissionPoint(const Unit* u, WorldObject*& out);
// 1000:3677: index of the nearest world object of `kind` (-1 any, 7 also
// matches 9) closer than 24000, or -1.
int wldNearestObject(const Vec3& pos, int kind);
// 1000:370C: set/clear flag 0x0100 (tree B) of every kind-5 world object.
void wldSetKind5Visible(bool on);
// 1000:3757: move the probe object to pos, probe, and test the hit's kind (-1
// any; 5 includes kind-5 objects). Quirk kept: clears pos.y of the caller.
bool wldProbeKind(Vec3& pos, int kind);
// 1000:38E2: random x/z offset of lo..hi units (two rng draws), pulled back
// off water (kinds 7/9).
void geoRandomOffsetDry(Vec3& pos, int lo, int hi);
// 1000:3A4C: 2 on water (kinds 7, 8, 9), else 1 (1 for NULL).
int wldGroundType(Vec3* pos);
// 1000:3D40: sum of the cover bytes of the world objects between the unit
// and target (< 100 = visible). Increments ms().losCalls.
int wldLosCover(const Unit* u, const Vec3& target);
// 1000:3E4C: unit whose body lies under the object's probe, or nullptr.
Unit* wldProbeUnit(const Obj3D* o);
// 1000:3E85: world object under the object if solid (height >= 0x7F) or of
// kind 7, 8, 9, 0xA, 0xB, 0x11; else nullptr.
WorldObject* wldProbeSolid(const Obj3D* o);
// 1000:6648 / 6665 / 669B / 66D9 / 671C.
bool wldObjectDestroyed(int n);
bool wldObjectiveDone(int n);
bool wldObjectFlag0(int n);
int wldCountDestroyed();
bool wldIsCamp();
// 1000:66BB: minutes since the mission start (+1).
int todMissionMinutes();
// 1000:695A: radius (world units >> 8) of a model, 150 without one.
int objRadiusOrDefault(const ModelDesc* m);

} // namespace st::game::mission
