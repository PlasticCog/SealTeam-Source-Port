// Entities: allocation of units, teams and their components (4511), team and
// unit construction (365e:000C..05A3), NPC personnel records (365e:391E) and
// the small unit/team helpers of segment 19ac (19ac:612A..642C, 85FA, 8617).
#pragma once

#include "game/types.h"

#include <functional>

namespace st::game::mission {

// ---- Allocation (4511) ----------------------------------------------------
// 4511:019E ent_alloc: unit with components by flag bits 0x01 status, 0x02
// mover, 0x04 anim, 0x08 brain (0x10 unused).
Unit* entAlloc(unsigned flags);
// 4511:03D0 ent2_alloc: team, with TeamAi when bit 3 is set.
Team* ent2Alloc(unsigned flags);
// 4511:0554 ent_add_waypoint / 4511:0480 ent_clear_waypoints.
void entAddWaypoint(Team* t, const Vec3& pos);
void entClearWaypoints(Team* t);

// ---- Construction (365e) ---------------------------------------------------
// 365e:05A3 ent_group_create(type, formation (+0x20), fire order (+0x22),
// order (+0x24), flags): appended to ms().teams; AI record from the MTM record
// when ms().firstMtmGroup != 0xFF (ai_group_orders_from_mission), else reset.
Team* entGroupCreate(TeamType type, int formation, int fireOrder, int order, unsigned flags);
// 365e:0459 ent_group_add_unit(team, flags, class, se, x, z): x/z game units.
Unit* entGroupAddUnit(Team* t, unsigned flags, UnitClass cls, int se, s32 x, s32 z);
// 365e:000C / 00CC: weapon node (reloads -1 -> 8); the second weapon added
// also becomes the secondary slot.
void entWeaponInit(WeaponNode* n, int weapon, int reloads);
WeaponNode* entUnitAddWeapon(Unit* u, int weapon, int reloads);
// 365e:01E5 / 0217: item node (quantity -1 -> 8).
void entItemInit(ItemNode* n, int item, int quantity);
ItemNode* entUnitAddItem(Unit* u, int item, int quantity);
// 365e:02E0 ent_unit_init_personnel(unit, class, se).
void entUnitInitPersonnel(Unit* u, UnitClass cls, int se);

// Roster lookup used for SEALs (class < 3): 365e roster_find(se). Set by the
// mission set-up (campaign API); returns nullptr when the SE has no entry.
void setRosterLookup(std::function<RosterEntry*(int se)> fn);
RosterEntry* rosterFind(int se);

// 365e:391E npc_se_load(i): 0..20 vcNN, 21 civ01, 22..30 frndNN, else vc01
// (cached in ms().npcSeCache). 365e:38B5 npc_se_cache_clear.
SeRecord* npcSeLoad(int i);
void npcSeCacheClear();
// Reads a 0x90-byte SE file (sealNN.se, vcNN.se, ...) from the archives.
bool loadSeFile(const char* name, SeRecord& out);
void decodeSe(const u8* p, SeRecord& out);

// ---- Unit and team helpers (19ac) ------------------------------------------
void unitSetClassWord(Unit* u, UnitClass cls, int se);   // 19ac:612A
void unitShow(Unit* u);                                   // 19ac:618D flags |= 0x0101
void unitHideChain(Unit* u);                              // 19ac:61A7 clears 0x0101, recursively unlinks buddy
int unitIndexInGroup(const Unit* u);                      // 19ac:6279
int grpIndex(const Team* t);                              // 19ac:62D1
int grpCountAlive(const Team* t);                         // 19ac:6317 (-1 for NULL)
int sealCountAlive();                                     // 19ac:6375
int unitLoadLevel(const Status* s);                       // 19ac:63B9
void unitAddLoad(Status* s, int n);                       // 19ac:640C
int grpNearestEnemyDist(const Team* t, const Vec3& pos);  // 19ac:642C
void unitAddExperience(Status* s, int n);                 // 19ac:85FA
bool unitInitBody(Unit* u, UnitClass cls);                // 19ac:8617

// 4a37:066E ai_list_count on a team's member list / the team table.
int teamMemberCount(const Team* t);
int teamTableCount();

// Small predicates used everywhere.
inline bool unitDead(const Unit* u) { return u && u->status && (u->status->hit_mask & hit_bit::kKilled); }
inline bool unitAlive(const Unit* u) { return u && !unitDead(u); }
inline bool isFootTeam(const Team* t) { return t && (t->type == TeamType::Seal || int(t->type) > 3); }
inline bool isCraftTeam(const Team* t) { return t && int(t->type) >= 1 && int(t->type) <= 3; }
inline bool isEnemyTeam(const Team* t) { return t && (t->type == TeamType::VietCong || t->type == TeamType::NvArmy); }

} // namespace st::game::mission
