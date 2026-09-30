// Soldiers and craft as moving bodies: segment 2dbd parts 1 and 3 (movement
// modes, posture, speed, fatigue, terrain and booby traps, diving, obstacle
// bounce, mover setup, casualty handlers, helper assignment, reload
// animations) and the segment 348e animation state machine.
// docs/re/seg_2dbd.md 3 and 4.2, docs/re/seg_2dbd_movement.md.
//
// Implementation files: movement.cpp, casualty.cpp, anim.cpp.
#pragma once

#include "game/types.h"

#include <functional>

namespace st::game::mission {

// Module reset (module-private timers and state), called by resetMission().
void peopleReset();

// Position of the current camera (g_cur_camera DS:D8AE, camera +0x00) read by
// fx_unit_ground_fx: shadows and wakes exist only within 0x4B0 units of it and
// draw random numbers there. Installed by the mission loop (the camera is the
// renderer's); without a hook the Point Man's position is used, which is the
// first-person camera's x/z (headless runs).
void setGroundFxCameraHook(std::function<Vec3()> fn);

// ---- Helpers ----------------------------------------------------------------
int evtHeadingDiff(int a8, int b8);                  // 2dbd:0006 |a-b| in whole degrees 0..180
// 2dbd:2503: value moves toward target by max(1, rate*dt >> 8), snapping.
void evtApproachValue(s16& value, s16 target, int rate);
// 2dbd:2580: same for degrees 0..359 along the shorter arc.
void evtApproachAngle(s16& value, s16 target, int rate);
// 2dbd:2603: target = 0, then approach at `rate` (the pair's +0x0A member:
// mover decel for the speed, descent rate for the height).
void evtApproachZero(s16& value, s16& target, int rate);
bool evtIsPositiveTurn(int a8, int b8);               // 2dbd:2933
WeaponNode* evtUnitFindWeapon(const Unit* u, int weaponType);  // 2dbd:3628

// ---- Speed, posture, mode ------------------------------------------------
int evtCalcMoveSpeed(Unit* u, int base);             // 2dbd:01A7 (side effect: winded += 1 while firing)
void evtSetPosture(Unit* u, int posture);            // 2dbd:0307
void evtSetMoveMode(Unit* u, int mode);              // 2dbd:03C5
void evtSetDesiredHeading(Unit* u, int deg);         // 2dbd:04A7
void evtStopUnit(Unit* u);                           // 2dbd:04D8
void evtInitMover(Unit* u, UnitClass kind);          // 2dbd:261E
void evtSetAltitudeLevel(Unit* u, int level);        // 2dbd:2841
// 2dbd:287F: destination = target's position (nullptr: nearest water object
// of kind 7/9 within 24000); returns the target used.
WorldObject* evtSetDestToTerrainObj(Unit* u, WorldObject* target);

// ---- Casualties -----------------------------------------------------------
Unit* evtAssignHelper(Unit* u, Team* t);             // 2dbd:004E
void evtCheckPrisonerEscape(Unit* u);                // 2dbd:0520
void evtCheckEmergencyExtraction(Team* t);           // 2dbd:0632
void evtUnitKilled(Unit* u);                         // 2dbd:0684
void evtUnitBledToDeath(Unit* u);                    // 2dbd:0A9D
void evtUnitWounded(Unit* u);                        // 2dbd:0D6F
void evtUnitSuppressed(Unit* u);                     // 2dbd:12E7

// ---- Weapons handling animations ------------------------------------------
void evtStartReload(Unit* u);                        // 2dbd:13C1
void evtCrouchToFireLauncher(Unit* u);               // 2dbd:161E
void evtCrouchToPlaceCharge(Unit* u);                // 2dbd:168B

// ---- Movement -------------------------------------------------------------
void evtUnitDive(Unit* u, int heading);              // 2dbd:16E6 (heading in whole degrees)
void evtTeamDiveQuickly(Team* t);                    // 2dbd:17C8
void evtUpdateUnitFatigue(Unit* u);                  // 2dbd:18B2
void evtUnitBounceOffObstacle(Unit* u);              // 2dbd:1B05
void evtUpdateUnitMovement();                        // 2dbd:1C04 (every frame, from the mission tick)

// ---- Effects riding with units (segment 1000) -------------------------------
// `on` >= 0 places the object, < 0 removes it (the movement passes 0 or -1).
void fxUnitGroundFx(Unit* u, int on);                // 1000:5D1D shadow / wake
void fxTeamMarker(Unit* u, int on);                  // 1000:5985 sampan of an enemy team on water

// ---- Animation state machine (segment 348e) -------------------------------
void sprAdvanceAnimClocks();                         // 348e:000E (every frame)
void sprStartThrowAnim(Unit* u);                     // 348e:0085
void sprSetAnim(Unit* u, int state);                 // 348e:00C8
int sprUpdateAnim(Unit* u);                          // 348e:0302 (1 while a timed animation plays)
void sprInitUnitAnim(Unit* u, int kind);             // 348e:158C

} // namespace st::game::mission
