// Mission loading and world construction (365e:227C..24DD, 129A), the
// run-time team helpers and statistics (365e:1544..1E8D), objectives
// (365e:251D..29C5) and the mission set-up flags of segment 1000 (1000:68E2).
// docs/re/seg_365e_a.md 4-7.
#pragma once

#include "game/types.h"

#include <string>

namespace st::game::mission {

// 365e:237E msn_load: cYmNN.mci / cYmNN.mtm into ms().mci / ms().mtm.
bool msnLoad(int missionNo);
// 365e:129A msn_build_world (called between wld_load / fx_create_pools and world_end).
void msnBuildWorld();
// 1000:68E2 mis_scan_specials.
void misScanSpecials();

// ---- Spawning (365e) ----------------------------------------------------------
Team* entSpawnSealTeam();                                    // 365e:0D8B
Team* entSpawnMtmTeam(MtmTeam& rec);                         // 365e:0EA6
Team* entSpawnInsertionCraft(int method, s32 x, s32 z);      // 365e:0AA5
Team* entSpawnSupportCraft(int kind, s32 x, s32 z);          // 365e:071C
Team* entSpawnHiddenGrenadier();                             // 365e:11A8
void entUnitPushBackFromObjective(Unit* u, int dist);        // 365e:0693
void entUnitFaceObjective(Unit* u);                          // 365e:24DD

// ---- Run-time team helpers (365e) -----------------------------------------------
bool entGroupIsPassiveNpc(const Team* t);            // 365e:1544
int entGroupSplit(int teamIndex);                    // 365e:1568 new index or -1
int entGroupMerge(int a, int b);                     // 365e:1691 a or -1
void entTeamRejoin();                                // 365e:17B1
void entOrderExtraction(const Vec3& pos);            // 365e:1800
bool entAnyCraftMoving();                            // 365e:1875
void entGroupReplaceLeader(Team* t, Unit* u);        // 365e:1A2F
bool entGroupHasSecuredMember(int teamIndex);        // 365e:1C83
bool entTeamAllExtracted();                          // 365e:1CDA
bool entTeamEnemySighted();                          // 365e:1DFB
void entMarkEnemiesOnStructures();                   // 365e:1E8D

// ---- Statistics (365e) ----------------------------------------------------------------
int statCountCapturedEnemies();                      // 365e:1ACC
int statCountWounded(int teamType);                  // 365e:1B3F
int statCountDead(int teamType);                     // 365e:1BB7
bool statNoCraftLost();                              // 365e:1C4D
void msnTallyCasualties();                           // 365e:1D1F (end of mission)

// ---- Objectives (365e) ----------------------------------------------------------------
bool msnCheckObjective(int i);                       // 365e:251D
void msnCheckObjectives();                           // 365e:2853 every 0x400 ticks
bool msnIsObjectiveStructure(const WorldObject* w);  // 365e:288A
bool msnIsObjectiveTeam(const Team* t);              // 365e:290B
int msnObjectiveDemoWeapon(int i);                   // 365e:2998
int msnFindObjectiveTarget(int kind, const Vec3* pos);  // 365e:29C5

// ---- Map markers (19ac:2B60 map_init_markers) --------------------------------------------
void mapInitMarkers();

// ---- Camp scenes (19ac:651B) ----------------------------------------------------------------
void grpSetCraftStatus(int teamIndex, int status);

} // namespace st::game::mission
