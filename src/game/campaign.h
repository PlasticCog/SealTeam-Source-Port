// Campaign data model: campaign header, roster and SE records, team loadout,
// the tables the front end reads from st.exe (weapons, tools, campaign flow,
// promotions, Medal of Honor), cN.cmp save/load, the current mission header
// (msn_load), mission statistics and the end-of-mission scoring.
//
// Original code: segment 365e, docs/re/seg_365e_a.md sections 1-2 and 8,
// docs/re/seg_365e_b.md sections 4 and 12. Record layouts are the canonical
// ones of game/types.h (docs/re/STRUCTURES.md).
//
// For the mission code (src/game/mission):
//   * the SEAL team to spawn is loadout() (4 records, point man first; the
//     list ends at the first record with se_id == -1); each member's roster
//     entry is rosterFind(se_id) and its SE record rosterFind(se_id)->se;
//   * difficulty() holds the st1.dfr options (game/config.h loads/saves it);
//   * missionLoad() is msn_load (365e:237E): it loads mci() and mtm() for
//     g().missionNo; mission::run() may call it again, as 1000:01DE does;
//   * stats() is the far-53BA statistics block the mission counts into
//     (scoreResetStats() is 365e:DBE6, called at mission start);
//   * at mission end fill a ScoreInput and call scoreMission() (365e:DEB0),
//     or awardEvaluateMission() which is 19ac:00B6 including the history
//     lookups; the casualty tally writes result() and calls
//     rosterRecordCasualty() for every SEAL.
#pragma once

#include "core/common.h"
#include "game/types.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace st::game::campaign {

// ---------------------------------------------------------------------------
// Tables read from st.exe
// ---------------------------------------------------------------------------

constexpr int kWeaponCount = 34;    // DS:48FE
constexpr int kToolCount = 12;      // far 52E3:0000
constexpr int kYears = 4;           // 1966..1969
constexpr int kMissionsPerYear = 20;
constexpr int kPoolSize = 24;       // SEALs per pool (SE 0..23 / 24..47)

const WeaponDef& weapon(int id);            // 0..33
// Byte W[id]+0x20 exactly as the original reads it (DS:491E + id*0x22), also
// for id == -1 (empty slot), where it reads the byte before the table.
u8 weaponReloadsByte(int id);
// Default and maximum magazines of a SEAL loadout slot: weaponReloadsByte,
// or the "Modern gameplay" loadout (docs/mission.md) when that option is on.
u8 sealWeaponReloads(int id);
const ItemDef& tool(int id);                // 0..11
const FlowEntry& flow(int year, int mission);   // g_campaign_flow (DS:3C5A -> far 5275)
u16 promotionThreshold(int rank);           // far 52BB:0002, rank 0..9 (10 = none)
const std::vector<MohRecord>& mohRecords(); // far 52BB:001A, 9 records

// ---------------------------------------------------------------------------
// Campaign header (g_campaign_state DS:3B0E; g_campaign always points to it)
// ---------------------------------------------------------------------------

CampaignHeader& header();
void stateInit(s8 slot, s16 pointMan);      // cmp_state_init (365e:378B)
void stateResetForMode();                   // cmp_state_reset_for_mode (365e:37BE)
bool missionWon(s16 score);                 // cmp_mission_won (365e:3FAE)
void recordMission(s16 score);              // cmp_record_mission (365e:3FD0)

// Set by the main loop: 1 after a campaign debriefing, 0 when the campaign
// screen returns "back" (g_from_mission, DS:022D).
bool& fromMission();

// ---------------------------------------------------------------------------
// Roster (g_roster DS:3A6A, up to 40 entries) and SE personnel records
// ---------------------------------------------------------------------------

// sealNN.se for SE id 0..47 (roster_load_seal_se 365e:384F); nullptr if missing.
std::unique_ptr<SeRecord> loadSealSe(int seId);
// NPC personnel (npc_se_load 365e:391E): 0..20 vcNN, 21 civ01, 22..30 frndNN,
// others vc01; cached until npcSeCacheFree().
SeRecord* npcSe(int index);
void npcSeCacheFree();

// Roster entries in table order (stable pointers until rosterFree()).
const std::vector<RosterEntry*>& roster();
RosterEntry* rosterFind(int seId);          // roster_find (365e:3F71), nullptr if absent
bool rosterAdd(int seId);                   // roster_add (365e:3EFC)
void rosterNew(int pointMan);               // roster_new (365e:3D99)
void rosterFree();                          // roster_free (365e:3E14)
// roster_entry_init (365e:3E9C) on a caller-owned entry; `se` receives the
// loaded SE record and entry.se points to it.
void rosterEntryInit(RosterEntry& entry, std::unique_ptr<SeRecord>& se, int seId);
int rosterActivateRecruit();                // roster_activate_recruit (365e:4C1D), -1 none
int rosterSkillRating(const RosterEntry& e);  // roster_skill_rating (365e:4B16)
int rosterCountKia();                       // roster_count_kia (365e:4ADA)
void rosterClearWounded();                  // roster_clear_wounded (365e:4A32)
int rosterPickDefaultMember(int k);         // roster_pick_default_member (365e:4124), uses the RNG
int poolBase();                             // 0 if the point man is < 24, else 24

// Unit-level bookkeeping. The original takes a unit and compares it with the
// leader of team 0 (g_groups[0][0]) to decide whether the point-man history
// entry hist[roster.missions] is written; pass that comparison as isLeader.
void rosterAddAwards(RosterEntry& r, bool isLeader, u16 bits);        // 365e:4984
void rosterSetRank(RosterEntry& r, bool isLeader, int rank);          // 365e:49C9
void rosterRecordCasualty(RosterEntry& r, bool isLeader, u16 bits);   // 365e:4A61

// ---------------------------------------------------------------------------
// Save slots (cN.cmp) and s.cnf directory
// ---------------------------------------------------------------------------

bool load(int slot);                        // cmp_load (365e:3A55)
bool save(int slot);                        // cmp_save (365e:3BC4)
void loadAutosave();                        // cmp_load_autosave (365e:3B9B)
void saveAutosave();                        // cmp_save_autosave (365e:3CF0)
bool slotUsed(int slot);                    // cmp_slot_used (365e:3760)
int lastSlot();                             // cmp_last_slot (365e:3781)
void setLastSlot(int slot);                 // cmp_set_last_slot (365e:3D19), writes s.cnf
bool deleteSlot(int slot);                  // cmp_delete_slot (365e:3D2F), writes s.cnf
std::string slotName(int slot);             // s.cnf name of slot 0..7
void setSlotName(int slot, const std::string& name);

// ---------------------------------------------------------------------------
// Team loadout (far 5178:0000, 4 records + 0xFF terminator)
// ---------------------------------------------------------------------------

constexpr int kTeamSize = 4;  // Point Man, Officer-in-Charge, Corpsman, Rear Security
LoadoutRecord* loadout();                   // kTeamSize records
int loadoutNextWeapon(int w, int year, bool up, int cls);   // loadout_next_weapon (365e:4B57)
int loadoutFixWeaponForYear(int w);         // loadout_fix_weapon_for_year (365e:41E0)
void loadoutBuildMember(int k, int seId);   // loadout_build_member (365e:422F); needs mci()
void loadoutBuildTeam();                    // loadout_build_team (365e:45A8)
void loadoutReleaseTeam();                  // loadout_release_team (365e:45F4)
// Carried load of member k in 1/10 lb (loadout_compute_weight 365e:5CEE on SE+0x8C).
int loadoutComputeWeight(int k);

// ---------------------------------------------------------------------------
// Current mission (msn_load 365e:237E / msn_load_text 365e:227C)
// ---------------------------------------------------------------------------

std::string missionBaseName(int missionNo);    // "cYmNN"
bool missionLoad();                         // loads mci() and mtm() for g().missionNo; fatal on bad data
MciHeader& mci();                           // g_mci (DS:ED32)
std::vector<MtmTeam>& mtm();                // g_mtm (DS:39CE)
void missionFreeMtm();                      // msn_free_mtm (365e:24BC)
// Fixed-width text resource `<name>.s` split into lines (NUL padded records).
bool loadTextLines(const std::string& name, std::vector<std::string>& lines, size_t lineLen = kTextLineLen);
// cYmNN.s of a mission (24 lines).
bool loadMissionText(int missionNo, std::vector<std::string>& lines);
// sealNN.s Bull Session chatter of a SEAL (msn_load_seal_bio_text 365e:2319).
bool loadSealChatter(int seId, std::vector<SealChatterLine>& lines);
// A far-segment objective search as used by the loadout code:
// msn_find_objective_target(type, no position) (365e:29C5) - index of the last
// objective target of that kind, -1 none (2/6 need a target team, 3 a structure).
int findObjectiveTarget(int kind);

// ---------------------------------------------------------------------------
// Mission statistics and results
// ---------------------------------------------------------------------------

// Statistics block far 53BA:303A..304B.
struct MissionStats {
    u16 roundsFired = 0;     // +303A (SEAL groups only)
    u16 roundsHit = 0;       // +303C
    u16 grenadesThrown = 0;  // +303E
    u16 grenadesHit = 0;     // +3040
    u16 bonus = 0;           // +3042 bonus counter (+250 points each)
    s16 score = 0;           // +3044 mission score
    u16 teamSize = 0;        // +3046 team size at the end of the mission
};
MissionStats& stats();
void scoreResetStats();                     // score_reset_stats (365e:DBE6)

// Mission results in DGROUP written at mission end and read by the debriefing.
struct MissionResult {
    u8 success[3] = {};           // EEC6..EEC8 primary/secondary/tertiary objective achieved
    u16 roundsFired = 0;          // D7E6 (score_mission output)
    u16 roundsHit = 0;            // D7E8
    s16 enemyKia = 0;             // EC94 dead VC + NVA
    s16 sealKia = 0;              // EC96
    s16 sealWia = 0;              // EC98
    s16 minutes = 0;              // 135E elapsed mission minutes
    s8 extractionType = 4;        // EC93 team type of the extraction group (4 none)
    s16 weaponsCaptured = 0;      // EC60
    s16 documentsCaptured = 0;    // EC62
    s16 enemyCaptured = 0;        // EC64
};
MissionResult& result();

// Everything score_mission (365e:DEB0) reads from the mission world, gathered
// by the mission code in the original order of team 0.
struct ScoreUnit {
    RosterEntry* roster = nullptr;  // unit+0x28
    bool dead = false;              // Status+0x0F bit 0x40
    bool wounded = false;           // med_wound_level(status) != 0 (19ac:87B8)
};
struct ScoreInput {
    std::vector<ScoreUnit> team;    // members of g_groups[0] ([0] = leader)
    bool noCraftLost = true;        // stat_no_craft_lost (365e:1C4D)
    int deadVc = 0, deadNva = 0, deadCivilians = 0;
    bool objective[3] = {};         // score_eval_objective (365e:DC11) of objectives 1..3
    int snatchTargetsDead[3] = {};  // dead units of the target team of each objective
    int missionMinutes = 0;         // tod_mission_minutes (1000:66BB)
    int destroyedObjects = 0;       // wld_count_destroyed (1000:66D9)
    int capturedEnemies = 0;        // stat_count_captured_enemies (365e:1ACC)
    bool teamAllExtracted = false;  // ent_team_all_extracted (365e:1CDA)
};
struct ScoreContext {
    s16 prevScore = 0;       // history[max(missions-1, 0)].score (low word)
    int rank = 0;            // point man's roster rank
    s16 totalScore = 0;      // campaign total (low word)
    int year = 0, mission = 0;
    int yearKia = 0;         // always 0 from the original caller
};
// score_mission: writes stats().score/teamSize, result().success and the
// rounds outputs, and gives promotions, awards and skill training.
void scoreMission(const ScoreInput& in, const ScoreContext& ctx);
// awd_evaluate_mission (19ac:00B6) after msn_tally_casualties: clears the
// outputs and calls scoreMission with the campaign's history values.
void awardEvaluateMission(const ScoreInput& in);

} // namespace st::game::campaign
