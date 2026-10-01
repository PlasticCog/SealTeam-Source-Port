// Mission simulation state: the DGROUP globals and far data blocks of the
// original mission code (segments 1000, 19ac, 2dbd, 348e, 365e, 4a37, 4592)
// gathered in one object. Every member names its original DS offset or far
// address. Records use the canonical layouts of game/types.h.
//
// Ownership: all run-time records (teams, units and their components, weapon
// and item nodes, waypoints, projectiles, ...) are allocated from the pools of
// the MissionState and live until the next resetMission(). The original freed
// some of them earlier (merged teams, waypoint lists); the port simply stops
// referencing them, which is equivalent.
//
// Module-private state that no other module reads lives in the module's own
// .cpp file and is cleared by its reset function (called from resetMission).
#pragma once

#include "game/types.h"

#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace st::game::mission {

constexpr int kMaxTeams = 25;        // g_groups DS:12E2..1345 (26 far pointers incl. the NULL end)
constexpr int kProjectiles = 32;     // g_fx_pool DS:2670
constexpr int kShots = 32;           // far 53BA:28BA
constexpr int kNoiseEvents = 20;     // far 53BA:26DA
constexpr int kSearchLog = 9;        // far 53BA:0000
constexpr int kSoundChannels = 6;    // far 53BA:258C (engine/sound)
constexpr int kNpcSeCache = 31;      // DS:3C6A
constexpr int kFlyers = 8;           // DS:3100 (area-effect / ambient flyer list)

// Stable-address object pool (deque never moves its elements). alloc()
// returns a value-initialised (all zero) record, like mem_far_calloc.
template <typename T>
class Pool {
public:
    T* alloc() { items_.emplace_back(); return &items_.back(); }
    void clear() { items_.clear(); }
    size_t size() const { return items_.size(); }
private:
    std::deque<T> items_;
};

// Statistics block of far segment 53BA read by the debriefing (365e:DBE6
// score_reset_stats clears it at mission start).
struct MissionStats {
    s16 roundsFired = 0;        // 53BA:303A bullets fired by SEALs
    s16 roundsHit = 0;          // 53BA:303C bullet hits by SEALs
    s16 grenadesThrown = 0;     // 53BA:303E explosives fired by SEALs
    s16 grenadeHits = 0;        // 53BA:3040 explosive hits by SEALs
    s16 bonusCounter = 0;       // 53BA:3042
    s16 missionScore = 0;       // 53BA:3044
    s16 teamSize = 0;           // 53BA:3046
};

// Port-only per-unit state of the Enhanced "Modern gameplay" rules
// (modern.h, docs/mission.md): obstacle detours and grenade timing. Written
// only while the option is on.
struct PortUnitState {
    const WorldObject* bounce_obj = nullptr;  // obstacle of the last bounce
    int bounce_count = 0;                     // consecutive bounces against it
    Ticks bounce_time = 0;                    // g_time of the last bounce
    bool detour = false;                      // detour heading in force
    s16 detour_heading = 0;                   // deg
    Ticks detour_start = 0;
    const WorldObject* detour_obj = nullptr;  // obstacle being walked around
    Vec3 detour_last_pos{};                   // position at the last bounce of the detour
    int detour_pinned = 0;                    // bounces of the detour without progress
    bool thrown = false;                      // has thrown a grenade (last_throw valid)
    Ticks last_throw = 0;
};

// Port-only counters read by the --sim-mission summary (no effect on the
// simulation; the bounce count is kept with the option off too).
struct PortStats {
    int bounces = 0;       // evt_unit_bounce_off_obstacle calls of foot units
    int detours = 0;       // detours started (Modern gameplay)
    int craftHolds = 0;    // support-craft shots held near friendlies (Modern gameplay)
    int grenadeHolds = 0;  // enemy grenade throws held by the discipline rule (Modern gameplay)
};

struct MissionState {
    // ---- Pools ------------------------------------------------------------
    Pool<Team> teamPool;
    Pool<TeamAi> teamAiPool;
    Pool<Unit> unitPool;
    Pool<Status> statusPool;
    Pool<Mover> moverPool;
    Pool<Anim> animPool;
    Pool<Brain> brainPool;
    Pool<Loadout> loadoutPool;
    Pool<WeaponNode> weaponPool;
    Pool<ItemNode> itemPool;
    Pool<WaypointNode> waypointPool;
    Pool<ScriptStep> scriptPool;
    Pool<Projectile> projectilePool;
    Pool<FlightRec> flightPool;
    Pool<WorldObject> worldObjectPool;
    Pool<SeRecord> sePool;

    // ---- Clock (1000:2FEA..314B) -----------------------------------------
    Ticks time = 0;              // g_time DS:ECA6
    s16 frameTicks = 1;          // g_frame_ticks DS:ECAA (>= 1)
    Ticks prevTime = 0;          // g_prev_time DS:ECAC
    s16 elapsed = 0;             // g_elapsed DS:ECB4 (argument of the periodic updates)
    u8 tcState = 0;              // g_time_compress DS:D7ED: 0 off, 3/2 switching on, 1 compressed

    // Time of day (1000:1964..19EE).
    s8 todHour = 0;              // DS:EC58
    s8 todMinute = 0;            // DS:EC57
    s8 todSecond = 0;            // DS:EC59
    Ticks tMinute = 0;           // DS:EC4E

    // ---- Periodic timestamps (all set to g_time at mission start) --------
    Ticks tUnits = 0;            // DS:D7FC 0x40 shot resolution
    Ticks tAi = 0;               // DS:D7F8 0x140 AI/music/alerts
    Ticks t4s = 0;               // DS:D800 0x400 noise/bleeding/objectives
    Ticks t9s = 0;               // DS:D804 0x900 scenery
    Ticks alertUntil = 0;        // DS:D808 "Enemy sighted." alert (music mood 1)
    Ticks diveReadyTime = 0;     // DS:D80E next 'q' dive
    Ticks contactUntil = 0;      // DS:0898 under-fire signal / combat music (mood 2)
    Ticks teamMoveTick = 0;      // DS:088C formation 0x200/0x400 tick A
    Ticks teamIdleTick = 0;      // DS:0890 formation 0x900 tick B
    Ticks fatigueTick = 0;       // DS:0894 movement 0x500 tick
    Ticks minuteJobTime = 0;     // DS:0A6E 0x3C00 craft engine sounds
    Ticks job4sTime = 0;         // DS:0A72 0x400 split-team update
    Ticks searchTick = 0;        // DS:0AEA search retarget
    Ticks teamFireTime = 0;      // DS:02B2 teammates join "Fire at Target"
    Ticks autotargetTime = 0;    // DS:02B6 auto-target refresh
    Ticks playerTerrainFxTime = 0;  // DS:D8B0 collision message / brush sound throttle

    // ---- Mission data ----------------------------------------------------
    int missionNo = 0;           // g_mission_no DS:EEBC (0..79)
    int year = 0;                // campaign year 0..3 (g_campaign+1)
    GameMode gameMode = GameMode::Demo;  // g_game_mode DS:D7E4
    MciHeader mci{};             // g_mci DS:ED32
    std::vector<MtmTeam> mtm;    // g_mission_groups DS:39CE (modified in place by Team Size)
    DifficultyOptions opt{1, 2, 2, 2, 1, 1, 1, 1};  // far 5178:0148
    std::array<LoadoutRecord, 4> loadout{};         // far 5178:0000 marching order
    int worldIndex = 0;          // g_world_index DS:ED16
    std::string areaName;        // g_area_name DS:ECB9

    // ---- Teams (365e) -----------------------------------------------------
    Team* teams[kMaxTeams + 1] = {};  // g_groups DS:12E2, NULL terminated
    int teamCount = 0;           // DS:EC87
    int mapSelTeam = 0;          // DS:EC86 team selected on the map
    int sealTeam = 0;            // DS:EC88
    int boatGroup = 0xFF;        // DS:EC89
    int heloGroup = 0xFF;        // DS:EC8A
    int airGroup = 0xFF;         // DS:EC8B
    int insertionGroup = 0xFF;   // DS:EC8C insertion craft (views F3..F6 use +0..+3)
    int extractionGroup = 0xFF;  // DS:EC8D
    int emergencyGroup = 0xFF;   // DS:EC8E
    int firstMtmGroup = 0xFF;    // DS:EC8F first enemy/other team
    int fireSupportGroup = 0xFF; // DS:EC90
    int breakContactGroup = 0xFF;// DS:EC91
    int extractingGroup = 0xFF;  // DS:EC92 craft ordered to extract
    int extractionType = 0;      // DS:EC93 type of the extraction group (4 none)
    int splitGroups = 0;         // DS:EC9C split SEAL teams 0..2
    Unit* proxyGrenadier = nullptr;  // g_proxy_grenadier DS:134A
    std::array<SeRecord*, kNpcSeCache> npcSeCache{};  // DS:3C6A

    // ---- Waypoints and markers (19ac) -------------------------------------
    Vec3 wpSeal{};               // DS:0E94 (also the objective marker position)
    Vec3 wpSupport{};            // DS:0EA0 support waypoint / extraction / re-insertion point
    Vec3 extractMarker{};        // DS:0EAC extraction point actually ordered
    Vec3 wpSplitA{};             // DS:0EB8
    Vec3 wpSplitB{};             // DS:0EC4
    Vec3 routeNodes[4] = {};     // DS:0ED0 (20-byte entries; only the position matters)

    // ---- Player / combat --------------------------------------------------
    TargetRec playerTarget{};    // far 5149:0000 reticle target / current contact
    ShotRec shots[kShots] = {};  // far 53BA:28BA
    Projectile* projectiles[kProjectiles] = {};  // g_fx_pool DS:2670
    NoiseEvent noise[kNoiseEvents] = {};         // far 53BA:26DA
    TargetRec searchLog[kSearchLog] = {};        // far 53BA:0000
    s16 medicTarget[4] = {-1, -1, -1, -1};       // far 525B:0000 automatic first-aid patients
    ScriptStep* scriptHeads[3] = {};             // far 53BA:268C
    Ticks weaponReadyTime = 0;   // DS:D8A6 (also the "Reloading" HUD line)
    Ticks grenadeReadyTime = 0;  // DS:D8AA
    Ticks weaponInfoTime = 0;    // DS:D84A HUD weapon line
    Ticks grenadeInfoTime = 0;   // DS:D86A HUD grenade/tool line
    Ticks toolInfoTime = 0;      // DS:EC46 HUD item line
    Ticks demoExplodeTime = 0;   // DS:33B6 satchel countdown
    bool radioDamaged = false;   // DS:051E
    Ticks radioRepairTime = 0;   // DS:0520
    bool autoTarget = true;      // DS:02C1 Alt-U
    bool targetStructures = true;// DS:02BB
    bool grenadeAiming = false;  // DS:02AC
    s16 grenadeAimSide = 0;      // DS:02AE
    s16 grenadeAimDist = 300;    // DS:02B0

    // ---- Search / prisoners (2dbd) ----------------------------------------
    Team* searchTeam = nullptr;  // DS:0AE6
    s16 searchRecCount = 0;      // DS:EC5E
    s16 misWeapons = 0;          // DS:EC60
    s16 misDocuments = 0;        // DS:EC62
    s16 misPrisoners = 0;        // DS:EC64 (g_prisoners_held)
    s16 searchWeapons = 0;       // DS:EC66
    s16 searchDocuments = 0;     // DS:EC68
    s16 searchPrisoners = 0;     // DS:EC6A

    // ---- Mission flow (1000) ----------------------------------------------
    bool misDone = false;        // DS:D7EA (also set by Esc + 'Y': g_mission_aborted is the same byte)
    bool quitGame = false;       // DS:D7EB
    Ticks extractionEnd = 0;     // DS:D7EE mission end time (0 = not started)
    Ticks insertionClearTime = 0;// DS:D7F2 insertion start + 0xA00; the AI command executor also uses it as hold-fire time
    bool extracting = false;     // DS:023A (g_input_locked)
    bool aiEnabled = true;       // DS:02BA
    bool hasSpecialObject = false;  // DS:ECFA world has the tunnel shape 0xC4B2 ("alarm mode" of the AI)
    bool hasDemoObjective = false;  // DS:ECFB
    bool hasAmbushObjective = false;// DS:ECFC
    u8 viewMode = 0;             // g_view_mode DS:D844 (set by the loop; read by the simulation)
    int detailLevel = 5;         // g_detail_level DS:02BE (game/globals.h mirrors it)
    bool handSignalGfx = true;   // DS:348B

    // ---- End-of-mission statistics (365e:1D1F msn_tally_casualties) ------
    s16 enemyKia = 0;            // DS:EC94 (also read by wld_objective_done: > 4)
    s16 sealKia = 0;             // DS:EC96
    s16 sealWia = 0;             // DS:EC98
    s16 missionMinutes = 0;      // DS:135E
    MissionStats stats;

    // ---- LOS (4ff8) -------------------------------------------------------
    Vec3 losHit{};               // g_los_hit DS:ECFE (also the collision contact point)
    s16 losCalls = 0;            // DS:D80C

    // ---- Port only (no original counterpart) ------------------------------
    std::unordered_map<const Unit*, PortUnitState> portUnits;  // Modern gameplay side table
    PortStats portStats;
};

MissionState& ms();

// Clears every pool and global to the state before msn_load and calls the
// reset function of every module.
void resetMission();

// Convenience accessors.
inline Team* team(int i) { return (i >= 0 && i < kMaxTeams) ? ms().teams[i] : nullptr; }
inline Unit* pointMan() { return ms().teams[0] ? ms().teams[0]->members[0] : nullptr; }

} // namespace st::game::mission
