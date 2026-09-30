// Weapons, shots, hit rolls, damage, wounds and medical aid (19ac:6CCA..89C3,
// docs/re/seg_19ac.md 12), the weapon/tool keys' helpers (19ac:0934..17A0),
// damage application and targeting of segment 1000 (1000:521C dmg_apply,
// 1000:6225 tgt_acquire, 1000:5114 / 69E7 scripted explosions, 1000:8B64
// inventory), and the player's fire / grenade actions (19ac:1A79 6.4-6.5).
//
// Implementation files: combat.cpp and combat_player.cpp (tools, turning and
// the player's actions).
//
// Target pointers: like the original, a target / victim pointer is either a
// Unit or a WorldObject and the kind says which (TargetRec::target union,
// ShotRec::target, Projectile::target). Where the port's field or parameter
// is typed Unit*, a structure target (kind 1) is passed as its WorldObject*
// cast to Unit*; only kind 0 targets are ever dereferenced as units.
#pragma once

#include "game/types.h"

namespace st::game::mission {

void combatReset();

// ---- Weapon slots (19ac) ----------------------------------------------------
int wpnNextRof(const WeaponNode* w);                 // 19ac:0C21 (returns the new rate; the caller stores it)
void wpnSelectNextWeapon(Unit* u);                   // 19ac:16B5
void wpnSelectNextGrenade(Unit* u);                  // 19ac:16FD
bool wpnInRange(int dist, const WeaponNode* w);      // 19ac:6CCA
int wpnBestRange(const Unit* u);                     // 19ac:6CE8
bool wpnBestRangeBelow(const Unit* u, int n);        // 19ac:6D43
bool wpnHasRange(const Unit* u, int n);              // 19ac:6D61
void wpnSelectForRange(Unit* u, int n);              // 19ac:6DAC
bool wpnSelectLongest(Unit* u);                      // 19ac:6E20
WeaponNode* wpnNextItem(const Unit* u);              // 19ac:6EEA
void wpnReload(WeaponNode* w);                       // 19ac:6F51
void wpnReloadBoth(Unit* u);                         // 19ac:6FA5
// 19ac:6FD8: rounds -= n (floor 0); `rof` is the rate the shot used (8 on the
// M203 counts a launcher grenade in the slot's +6 counter).
void wpnSpendRounds(WeaponNode* w, int n, int rof = 0);
int wpnAiPickRof(const WeaponNode* w);               // 19ac:7005 (draws rng for modes 3, 6, 7, 0xF)
int wpnGetMags(const WeaponNode* w);                 // 19ac:70AB
int wpnGetRounds(const WeaponNode* w);               // 19ac:70C8
int wpnRangeBand(const WeaponNode* w, int range);    // 1000:0D0B
// 1000:18A1: end = now + (Reload Time timed ? weapon reload ticks : 0x40) if reloads remain.
void wpnSetReloadTimer(const WeaponNode* w, Ticks& end);

// ---- Shots (19ac) ------------------------------------------------------------
int shotFindFree();                                  // 19ac:70E6
// 19ac:7124 shot_fire(shooter, target unit or NULL (kind 1: the WorldObject
// cast to Unit*), target position, cover (0 for the player), weapon slot,
// range, target kind).
void shotFire(Unit* shooter, Unit* target, const Vec3& pos, int cover, WeaponNode* w, int range, int targetKind);
int aiTargetPriority(const Unit* shooter, const Unit* target, int cover);   // 19ac:746D
// 19ac:7BA8 aim correction for thrown / area weapons. The original writes
// src + offset to a separate destination (y copied); here in place.
void shotScatter(Vec3& pos, const Unit* u);
void combatRandomWound(Unit* u);                     // 19ac:7F58 (pit trap)
void shotUpdateAll(int elapsed);                     // 19ac:8556 every 0x40 ticks

// ---- Wounds and medical (19ac) ---------------------------------------------
int medTreat(Unit* medic, Unit* patient);            // 19ac:86B3 treatments 0..2
int medWoundLevel(const Status* s);                  // 19ac:87B8
int medStatus(const Status* s);                      // 19ac:888C
int medLegLevel(const Status* s);                    // 19ac:88C6 (original argument: the unit)
int medArmCount(const Status* s);                    // 19ac:893B (original argument: the unit)
int medHeadCount(const Status* s);                   // 19ac:8971 (original argument: the unit)
void medBleedTick(int elapsed);                      // 19ac:89C3 every 0x400 ticks

// ---- Damage (1000) -------------------------------------------------------------
// 1000:521C dmg_apply(target, hit bits, damage, what): what 0 = unit
// (hitBits -1 = miss sound only, 0 = near miss), 1 = world object (damage),
// 2 = craft (nothing).
void dmgApply(Unit* unit, WorldObject* obj, int hitBits, int damage, int what);
const ModelDesc* wldDestroyedShape(const WorldObject* w);    // 1000:50B7
void fxPhantomExplosion(Unit* target);               // 1000:5114 (target NULL: at its own position)
void fxDestroyStructure(WorldObject* w);             // 1000:69E7
void wldHideHighlight(WorldObject* w);               // 1000:51F3

// ---- Inventory and tools ---------------------------------------------------------
int invCountItems(const Unit* u, int type);          // 1000:8B64
void invUseItems(Unit* u, int type, int n);          // 1000:8BAA
void itemUseTool(Unit* u);                           // 19ac:0934 '['
void itemCycleTool(Unit* u);                         // 19ac:0A3A ']'
void unit1UseTool();                                 // 1000:6990 second SEAL fires his tool (flare) at himself

// ---- Targeting ----------------------------------------------------------------------
// 1000:6225 tgt_acquire(shooter, record, skip): 0 unit, 1 structure, -1 none.
int tgtAcquire(Unit* shooter, TargetRec& rec, bool skip);
void tgtClear();                                     // 19ac:1685
bool wldOpenHutAhead(Unit* u);                       // 1000:6163 'x' expose trap
// 19ac:17A0: turns a live unit by d degrees and the loop's view heading
// (g_view_heading DS:CEAC) by the same amount through the hook below.
void unitTurn(Unit* u, int d);
// The mission loop owns g_view_heading; it registers the function that adds
// a delta (1/8 degree, wrap it like math_angle_add) to it. Default: none.
void setViewHeadingTurnHook(void (*fn)(int delta8));

// ---- Player actions (19ac:1A79 field_view_keys) ---------------------------------------
// Prologue of field_view_keys, run every frame in the field views before the
// key is handled: auto-target refresh (every 0x100 ticks or when the pointer
// moved horizontally, dx != 0) via tgt_acquire into ms().playerTarget, then
// the target data the fire/grenade keys of this frame use (no target: aim
// point 150 units ahead of the Point Man in ms().playerTarget.pos).
void playerViewPrologue(int dx);
// Enter (6.4), complete key: nothing while extracting; while the grenade box
// is shown it is cancelled (playerCancelGrenadeAim); else fire the current
// weapon at the target / aim point, reload, and the teammates' "Fire at
// Target" volley (every 0x200 ticks).
void playerFire();
// 'g' (6.5), complete key: nothing while extracting; with no target the first
// call shows the aim box, the next one throws at the box.
void playerThrowGrenade();
// Grenade aim box adjustments while ms().grenadeAiming (no-op otherwise), one
// 6-unit step per call: dSide < 0 left (Left key / pointer dx < 0), > 0
// right; dDist > 0 farther (Up / pointer dy < 0, max 300), < 0 nearer (Down:
// min 60; pointer dy > 0: min 75); the side is re-clamped to +-dist/2.
void playerAdjustGrenadeAim(int dSide, int dDist, bool pointer = false);
void playerCancelGrenadeAim();

} // namespace st::game::mission
