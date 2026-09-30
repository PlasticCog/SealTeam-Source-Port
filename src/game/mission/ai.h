// Group and soldier AI (segment 4a37, docs/re/seg_libs.md 14) and the AI
// noise events of segment 1000 (1000:841A..892E, docs/re/seg_1000.md 14).
//
// Implementation files: ai.cpp, noise.cpp.
#pragma once

#include "game/types.h"

namespace st::game::mission {

void aiReset();

// ---- Set-up (4a37:0000..066E) ----------------------------------------------
void aiInit();                                       // 4a37:0000 (also noise_reset)
void aiResetShotSlots();                             // 4a37:0035
void aiBuildScriptSequences();                       // 4a37:005C
void aiShutdown();                                   // 4a37:01BC
void aiMemberStartScript(Unit* u, int idx);          // 4a37:02AB
void aiMindReset(Brain* b);                          // 4a37:02E7
void aiGroupOrdersReset(Team* t);                    // 4a37:0372
// 4a37:0425: from the MTM record; index = team index, first = first MTM team.
void aiGroupOrdersFromMission(Team* t, const MtmTeam* rec, int index, int first);
void aiMindAttach(Unit* u, Team* t);                 // 4a37:061C

// ---- Commands (4a37:06A1..0EBD) -----------------------------------------------
Command* aiMemberCurrentCommand(Unit* u);            // 4a37:06A1
Command* aiMemberLastCommand(Unit* u);               // 4a37:071D
void aiGroupCommandsConsumed(Team* t);               // 4a37:0762
void aiMindPushCommand(Brain* b, const Command& c);  // 4a37:0D65
void aiGroupBroadcastCommand(Team* t, const Command& c);  // 4a37:0DEC
void aiGroupClearPending(Team* t);                   // 4a37:0EBD

// ---- Orders and movement ----------------------------------------------------------
void aiGroupSetDestination(Team* t, const Vec3& pos);  // 4a37:07AD
int aiFindReserveGroup();                            // 4a37:07D6 team index or -1
bool aiGroupAtDestination(const Team* t);            // 4a37:0842
bool aiGroupHasMoveOrders(const Team* t);            // 4a37:08A8
void aiGroupNextDestination(Team* t);                // 4a37:0923
// 4a37:0F38: deploy a reserve group (pos NULL: 720..1200 units from the player).
void aiGroupDeploy(Team* t, const Vec3* pos);
bool aiGroupReinforcementTick(Team* t, int dt);      // 4a37:1156
void aiSquadAutoFirstAid(Team* t);                   // 4a37:12AE
bool aiGroupRunScripts(Team* t, int dt);             // 4a37:14D8

// ---- Main tick (every 0x140 ticks) ---------------------------------------------------
void aiUpdate(int dt);                               // 4a37:175C

// ---- Alerts, contacts, perception, combat -----------------------------------------------
void aiGroupClearAlert(Team* t);                     // 4a37:1E1E
void aiGroupAlert(Team* t);                          // 4a37:1E6B
void aiMemberDeactivate(Unit* u);                    // 4a37:1ED6
void aiMindExpireContacts(Unit* u);                  // 4a37:201B
bool aiMindTracksTarget(const Brain* b, const Unit* target);  // 4a37:219F
int aiMindBestContactScore(const Brain* b);          // 4a37:21E6
bool aiGroupScan(Team* t);                           // 4a37:2237
bool aiMindHasContacts(const Brain* b);              // 4a37:289E
int aiMindBestContact(const Brain* b);               // 4a37:28D0 contact index or -1
int aiMindBorrowContact(Unit* u);                    // 4a37:2949 0 or -1
int aiMemberWeaponsInRange(const Unit* u, int dist); // 4a37:2AA7 bit0 primary, bit1 secondary
Obj3D* aiNearestEnemyLeader(const Vec3& pos);        // 4a37:2B25
void aiGroupSupportFire(Team* t);                    // 4a37:2BCD
void aiGroupCombat(Team* t);                         // 4a37:3080
void aiGroupTickSuppression(Team* t, int dt);        // 4a37:37A1

// ---- Noise events (1000) ---------------------------------------------------------------------
void noiseReset();                                   // 1000:841A
int noiseAlloc();                                    // 1000:8441
void noiseFromTeam(Team* t);                         // 1000:847A
void noiseUpdate();                                  // 1000:875E every 0x400 ticks
void noiseFromWeapon(const Unit* shooter, const WeaponNode* w);  // 1000:87BF
void noiseFromExplosion(const Vec3& pos);            // 1000:888A
bool noiseHear(const Unit* listener, Vec3& outPos);  // 1000:892E

} // namespace st::game::mission
