// Port addition: the Enhanced "Modern gameplay" rules that change the
// simulation itself (docs/mission.md "Modern gameplay"): support craft hold
// their fire near friendlies, squad mates work around obstacles and enemy
// soldiers throw grenades with discipline; and the one presentation rule
// under the same option, the marked snatch target. Every entry point does
// nothing while the option is off, so the original code paths stay
// byte-for-byte; with it on none of them draws from engine::rng()
// (deterministic choices only), so enabling the option changes just what it
// is meant to change.
//
// Per-unit state lives in ms().portUnits (released by resetMission()).
#pragma once

#include "game/types.h"

namespace st::game::mission {

// settings().effectiveModernGameplay().
bool modernGameplayOn();

// Rule 3 (evt_execute_ai_commands, craft branch): true when a living member
// of a SEAL or Friendly team that is not aboard the craft lies within the
// weapon's blast radius + 60 units of the target or within 90 units of the
// line of fire craft -> target. False while the option is off.
bool modernCraftHoldsFire(const Unit* craft, const Vec3& target, const WeaponNode* w);

// Rule 4 (evt_update_unit_movement): called after evt_unit_bounce_off_obstacle
// for a foot unit; three bounces against the same obstacle within 0x500 ticks
// start a detour (impact bearing +-90 degrees, the side nearer the unit's
// destination) that modernDetourUpdate keeps until the unit is radius + 20
// units from the obstacle or 0x300 ticks passed. A detour heading that makes
// no progress (three bounces at the same spot) turns 90 degrees away from
// the obstacle. SEAL teams only.
void modernNoteBounce(Unit* u, const WorldObject* obstacle);
// Every frame before the unit turns: holds the detour heading / ends the detour.
void modernDetourUpdate(Unit* u);
// The formation and split-team updates leave the heading alone while this is set.
bool modernDetourActive(const Unit* u);

// Rule 5 (ai_group_combat): whether a unit may throw `w` (a thrown item) at a
// contact `dist` units away as its primary: only within a quarter of the
// item's maximum range and 0x400 ticks after its last throw. `secondaryRoll`
// = the item was chosen by the secondary roll, which is not restricted. A
// true result records the throw. Always true while the option is off.
bool modernGrenadeAllowed(Unit* u, const WeaponNode* w, int dist, bool secondaryRoll);
// wpn_select_longest skips thrown items while the option is on.
bool modernSkipThrownForLongest(const WeaponNode* w);

// Rule 6 (presentation only): `u` is the leader of the target team of a
// Snatch objective of the current mission. The targets are looked up once
// from the MCI objectives after the teams are built and cached in
// ms().snatchTargets (cleared with the mission). False while the option is
// off, so the sprite, map and HUD code that reads it draws the original
// picture.
bool modernIsSnatchTarget(const Unit* u);

} // namespace st::game::mission
