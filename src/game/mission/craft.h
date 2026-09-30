// Support craft, extraction pickup, engine sounds, ordnance flight and
// ambient flyers (segment 2dbd part 4, docs/re/seg_2dbd_craft_ordnance.md)
// and the projectile / effect-object code of segment 1000 (1000:45B9..60FA,
// docs/re/seg_1000.md 13).
//
// Implementation files: craft.cpp, ordnance.cpp.
#pragma once

#include "game/types.h"

#include <vector>

namespace st::game::mission {

void craftReset();
// Position of the current camera (g_cur_camera DS:D8AE), used to cull the
// ambient flyers. Set by the mission loop (the pointer must stay valid);
// nullptr = the Point Man's position.
void craftSetViewerPos(const Vec3* pos);

// ---- Craft (2dbd) --------------------------------------------------------------
void evtUpdateSupportCraft();                        // 2dbd:297D every frame
void evtTeamSetMoverFlags(Team* t, u8 bits);         // 2dbd:31EC
void evtUpdateExtractionPickup();                    // 2dbd:3232 every frame
void evtForceCraftEngineSound();                     // 2dbd:3517
void evtPlayCraftEngineSounds();                     // 2dbd:352C

// ---- Ordnance flight (2dbd) --------------------------------------------------------
int evtCalcLaunchVelocity(Projectile* p, int range); // 2dbd:4088
void evtProjectileInitMotion(Projectile* p, int range);  // 2dbd:41A4
void evtUpdateOrdnance(Projectile* p);               // 2dbd:4429 every frame per in-use slot
void evtUpdateAmbientFlyer(Obj3D* o);                // 2dbd:4C74 area-effect list DS:3100

// ---- Projectiles and effect objects (1000) ------------------------------------------------
void prjResetAll();                                  // 1000:45B9
Projectile* prjFindByEffect(const Obj3D* fx);        // 1000:461A
Obj3D* fxPoolAlloc(const std::vector<Obj3D*>& pool); // 1000:4692 first hidden object
void fxRelease(Obj3D* o);                            // 1000:46D2
int prjAlloc(int wclass, int fireMode);              // 1000:473C slot or 32
// 1000:488E prj_fire(shooter, weapon slot, target position, target unit,
// mode (-1 = direct shot / dud), speed). Returns the projectile or nullptr.
Projectile* prjFire(Unit* shooter, WeaponNode* w, const Vec3& targetPos, Unit* targetUnit, int mode, int speed);
void prjDeactivate(Projectile* p);                   // 1000:5068
bool unitProjectileIdle(const ShotRec* s);           // 1000:575A (true = done: none, hit/landed or expired)
void unitProjectileStop(ShotRec* s);                 // 1000:5790
void fxPlaceAimMarker(int side, int dist);           // 1000:57BE grenade aim box
void fxPlaceUnitMarker(Unit* u, int n);              // 1000:587A
void fxMarkersHide(bool on);                         // 1000:592D
void fxMuzzleFlash(Projectile* p, int side, int fwd); // 1000:5B2F (fwd < 0 removes the flash)
void prjStartBurst(Projectile* p, int n);            // 1000:5F4E
void prjStartImpact(Projectile* p, int n, u8 flags); // 1000:604F
void prjShowBurst(Projectile* p, int n);             // 1000:60FA

} // namespace st::game::mission
