// Developer command --sim-mission: build a mission world headlessly and run
// the simulation with a scripted player, printing a readable event log.
//
//   sealteam --sim-mission <1..80> [--ticks N] [--seed-skip K] [--dt D]
//            [--script idle|walk|hold|attack] [--craft b|u|a|g|G] [--summary]
//            [--log FILE] [--quiet-snapshots]
//
// The log lists the spawned teams and units, then every change the frame
// loop observes: messages, sound effects, AI flag / order changes, postures,
// wounds and deaths, shots and their resolution, objectives and the end of
// the mission. Two runs with the same arguments must give identical logs.
// The hold and attack scripts (and --summary) add "hit:" lines naming the
// shooter of every wound and a summary at the end; walk and idle do not, so
// their logs stay comparable across builds. "--craft g" (attack script,
// Modern gameplay) orders Phantom strikes at the nearest enemy team until the
// flight is Winchester and logs the flight's path every 0x400 ticks, the
// bomb releases and their impacts; "--craft G" orders them 400 units ahead
// of the running squad instead, so that the danger-close rule holds the
// releases.
#include "engine/rng.h"
#include "engine/sound.h"
#include "game/campaign.h"
#include "game/devtools.h"
#include "game/mission/ai.h"
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/modern.h"
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/sfx.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/mission/world.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace st::game::mission {

namespace {

FILE* g_out = stdout;

void logf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fprintf(g_out, "[%6d %02d:%02d:%02d] %s\n", ms().time, ms().todHour, ms().todMinute, ms().todSecond, buf);
}

const char* teamTypeName(TeamType t) {
    static const char* n[] = {"SEAL", "Boat", "Helo", "Aircraft", "VC", "NVA", "Civilian", "Friendly"};
    const int i = int(t);
    return i >= 0 && i < 8 ? n[i] : "?";
}

// Readable unit label "T<team>.<member>".
std::string label(const Unit* u) {
    if (!u) return "-";
    char buf[32];
    std::snprintf(buf, sizeof buf, "T%d.%d", grpIndex(u->team), unitIndexInGroup(u));
    return buf;
}

std::string posStr(const Vec3& p) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "(%d,%d,%d)", p.x >> 8, p.y >> 8, p.z >> 8);
    return buf;
}

// ---- Default team for the test (a simplified loadout_build_team) ---------

struct TestRoster {
    std::vector<RosterEntry> entries;
    std::vector<SeRecord> se;
};

TestRoster g_roster;

void buildTestTeam(MissionSetup& setup) {
    static const int kSe[4] = {0, 9, 7, 1};  // point man, OIC, corpsman, rear security (sealNN.se)
    g_roster.entries.assign(4, RosterEntry{});
    g_roster.se.assign(4, SeRecord{});
    const MciHeader& h = ms().mci;
    bool demo = false, snatch = false, ambush = false;
    for (const MciObjective& o : h.objective) {
        demo |= o.kind == ObjectiveKind::Demolition;
        snatch |= o.kind == ObjectiveKind::Snatch;
        ambush |= o.kind == ObjectiveKind::Ambush;
    }
    for (int k = 0; k < 4; ++k) {
        char name[32];
        std::snprintf(name, sizeof name, "seal%02d.se", kSe[k] + 1);
        SeRecord& se = g_roster.se[size_t(k)];
        if (!loadSeFile(name, se)) logWarn("sim: %s missing", name);
        RosterEntry& r = g_roster.entries[size_t(k)];
        r.se_id = u8(kSe[k]);
        r.rank = se.rank;
        for (int i = 0; i < 8; ++i) r.skill[i] = se.skill[i];
        r.flags = roster_flag::kInTeam;
        r.se = &se;
        LoadoutRecord& l = setup.loadout[size_t(k)];
        l.se_id = s8(kSe[k]);
        l.camouflage = se.camouflage;
        for (int i = 0; i < 4; ++i) l.weapons[i] = se.weapons[i];
        l.tools[0] = 1;
        l.tools[1] = 1;
        if (k == 0) {
            if (demo) l.weapons[3] = 12;
            if (snatch) l.tools[0] = 2;
            l.tools[1] = 0;
        } else if (k == 1) {
            l.weapons[1] = 16;
            if (demo) l.weapons[2] = 12;
            l.weapons[3] = 4;
        } else {
            if (demo) l.weapons[3] = 12;
            if (ambush) l.weapons[2] = 4;
            if (k == 2) l.tools[0] = 2;
        }
        for (int i = 0; i < 4; ++i)
            l.reloads[i] = l.weapons[i] >= 0 ? campaign::sealWeaponReloads(l.weapons[i]) : 0;
    }
    setup.rosterFind = [](int se) -> RosterEntry* {
        for (RosterEntry& r : g_roster.entries)
            if (r.se_id == se) return &r;
        return nullptr;
    };
}

// ---- State tracking for the log -------------------------------------------

struct UnitSnap {
    u16 hit = 0;
    u8 posture = 0xFF, mode = 0xFF, brainFlags = 0, surrendered = 0, mflags = 0;
    bool visible = false;
};
struct TeamSnap {
    int order = -999, fireOrder = -999, formation = -999;
    int aiOrder = -1, behaviour = -1;
};

std::map<const Unit*, UnitSnap> g_units;
std::map<const Team*, TeamSnap> g_teams;
ShotRec g_shots[kShots];
bool g_objDone[3];

// Extended log (hold / attack scripts, --summary): hit attribution lines and
// the end summary. Off for walk / idle so their logs stay identical.
bool g_extended = false;
struct Tally {
    int enemyBullets = 0, enemyThrown = 0, enemyOther = 0;  // enemy shots by weapon class
    int craftShots = 0;                                     // shots fired by support craft
    int sealWounds[8] = {};                                 // SEAL wounds by shooter team type
    int sealKia[9] = {};                                    // SEAL deaths by the last shooter's team type (8 = none)
};
Tally g_tally;
std::map<const Unit*, int> g_lastShooterType;  // last team type that wounded a unit

bool isEnemyTeam(const Team* t) { return t && (t->type == TeamType::VietCong || t->type == TeamType::NvArmy); }
bool leaderPresent(const Team* t) {
    return t && t->members[0] && unitAlive(t->members[0]) && (t->members[0]->body->flags & 1);
}

// First enemy (VC / NVA) team of the mission table with a living, visible leader.
int firstEnemyTeam() {
    const MissionState& S = ms();
    for (int i = S.firstMtmGroup; i < kMaxTeams && S.teams[i]; ++i)
        if (isEnemyTeam(S.teams[i]) && leaderPresent(S.teams[i])) return i;
    return -1;
}

// Such a team with the leader nearest to pos.
int nearestEnemyTeam(const Vec3& pos) {
    const MissionState& S = ms();
    int best = -1, bestD = 0x7FFF;
    for (int i = S.firstMtmGroup; i < kMaxTeams && S.teams[i]; ++i) {
        if (!isEnemyTeam(S.teams[i]) || !leaderPresent(S.teams[i])) continue;
        const int d = geoDistance(pos, S.teams[i]->members[0]->body->pos);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

int firstTeamOfType(TeamType type) {
    const MissionState& S = ms();
    for (int i = 0; i < kMaxTeams && S.teams[i]; ++i)
        if (S.teams[i]->type == type) return i;
    return -1;
}

void logSummary() {
    const MissionState& S = ms();
    const Tally& t = g_tally;
    logf("summary: enemy shots %d (bullets %d, thrown %d, other %d); craft shots %d, held near friendlies %d",
         t.enemyBullets + t.enemyThrown + t.enemyOther, t.enemyBullets, t.enemyThrown, t.enemyOther, t.craftShots,
         S.portStats.craftHolds);
    logf("summary: SEAL wounds by shooter: SEAL %d Boat %d Helo %d Aircraft %d VC %d NVA %d; SEAL KIA by last shooter: "
         "SEAL %d Boat %d Helo %d Aircraft %d VC %d NVA %d none %d",
         t.sealWounds[0], t.sealWounds[1], t.sealWounds[2], t.sealWounds[3], t.sealWounds[4], t.sealWounds[5],
         t.sealKia[0], t.sealKia[1], t.sealKia[2], t.sealKia[3], t.sealKia[4], t.sealKia[5], t.sealKia[8]);
    logf("summary: enemy grenade throws held %d; unit bounces %d, detours %d", S.portStats.grenadeHolds,
         S.portStats.bounces, S.portStats.detours);
    if (modernPhantomAvailable())
        logf("summary: Phantom bombs released %d, releases held near friendlies %d, strikes left %d",
             S.portStats.phantomBombs, S.portStats.phantomHolds, modernPhantomStrikesLeft());
}

void dumpWorld() {
    const MissionState& S = ms();
    const MciHeader& h = S.mci;
    logf("mission %d (c%dm%02d) world %d '%s', start %02d:%02d, insertion %s method %d, extraction %s method %d",
         S.missionNo + 1, S.missionNo / 20 + 1, S.missionNo % 20 + 1, h.world, S.areaName.c_str(), h.start_hour,
         h.start_minute, posStr(h.insertion).c_str(), int(h.insertion_method), posStr(h.extraction).c_str(),
         int(h.extraction_method));
    for (int i = 0; i < 3; ++i) {
        const MciObjective& o = h.objective[i];
        if (o.kind == ObjectiveKind::None) continue;
        logf("objective %d: kind %d at %s team %d structure %d \"%.40s\"", i + 1, int(o.kind), posStr(o.pos).c_str(),
             o.target_team, o.target_structure, o.description);
    }
    logf("world objects %d, teams %d (first MTM team %d, insertion %d, extraction %d, emergency %d, fire support %d)%s",
         int(worldObjects().size()), S.teamCount, S.firstMtmGroup, S.insertionGroup, S.extractionGroup, S.emergencyGroup,
         S.fireSupportGroup,
         modernPhantomAvailable() ? (" Phantom flight " + std::to_string(S.portPhantomGroup)).c_str() : "");
    for (int i = 0; i < kMaxTeams && S.teams[i]; ++i) {
        const Team* t = S.teams[i];
        std::string ai;
        if (t->ai) {
            char buf[96];
            std::snprintf(buf, sizeof buf, " ai order %d behaviour 0x%02X deploy %d", int(t->ai->order_type),
                          t->ai->behaviour, t->ai->deploy_timer);
            ai = buf;
        }
        logf("team %d %s formation %d fire %d order %d%s", i, teamTypeName(t->type), int(t->formation),
             int(t->fire_order), t->order, ai.c_str());
        for (int m = 0; m < 8 && t->members[m]; ++m) {
            const Unit* u = t->members[m];
            std::string weapons;
            for (const WeaponNode* w = u->loadout ? u->loadout->list : nullptr; w; w = w->next) {
                char buf[48];
                std::snprintf(buf, sizeof buf, " %s(%d+%d)", weaponDef(int(w->type)).short_name, w->rounds, w->reloads);
                weapons += buf;
            }
            logf("  %s %s hdg %d name '%s'%s%s", label(u).c_str(), posStr(u->body->pos).c_str(), u->body->heading >> 3,
                 unitName(u).c_str(), weapons.c_str(), (u->body->flags & 1) ? "" : " hidden");
        }
    }
}

void trackChanges() {
    const MissionState& S = ms();
    for (int i = 0; i < kMaxTeams && S.teams[i]; ++i) {
        const Team* t = S.teams[i];
        TeamSnap& ts = g_teams[t];
        const int aiOrder = t->ai ? int(t->ai->order_type) : -1;
        const int beh = t->ai ? t->ai->behaviour : -1;
        if (ts.order != t->order || ts.fireOrder != int(t->fire_order) || ts.formation != int(t->formation) ||
            ts.aiOrder != aiOrder || ts.behaviour != beh) {
            if (ts.order != -999)
                logf("team %d %s: order %d fire %d formation %d ai order %d behaviour 0x%02X", i, teamTypeName(t->type),
                     t->order, int(t->fire_order), int(t->formation), aiOrder, beh & 0xFF);
            ts = TeamSnap{t->order, int(t->fire_order), int(t->formation), aiOrder, beh};
        }
        for (int m = 0; m < 8 && t->members[m]; ++m) {
            const Unit* u = t->members[m];
            UnitSnap& us = g_units[u];
            UnitSnap now;
            now.hit = u->status ? u->status->hit_mask : 0;
            now.posture = u->mover ? u8(u->mover->posture) : 0;
            now.mode = u->mover ? u8(u->mover->move_mode) : 0;
            now.mflags = u->mover ? u->mover->flags : 0;
            now.brainFlags = u->brain ? u->brain->flags : 0;
            now.surrendered = u->brain ? u->brain->surrendered : 0;
            now.visible = (u->body->flags & 1) != 0;
            if (us.posture == 0xFF) {
                us = now;
                continue;
            }
            if (now.hit != us.hit) {
                logf("%s %s hit mask 0x%04X -> 0x%04X%s (light %d heavy %d bleeding %d)", label(u).c_str(),
                     teamTypeName(t->type), us.hit, now.hit, (now.hit & hit_bit::kKilled) ? " KILLED" : "",
                     u->status->light_wounds, u->status->heavy_wounds, u->status->bleeding);
                if (t->type == TeamType::Seal && (now.hit & hit_bit::kKilled) && !(us.hit & hit_bit::kKilled)) {
                    const auto it = g_lastShooterType.find(u);
                    ++g_tally.sealKia[it == g_lastShooterType.end() ? 8 : it->second];
                }
            }
            if (now.brainFlags != us.brainFlags || now.surrendered != us.surrendered)
                logf("%s ai flags 0x%02X -> 0x%02X%s at %s", label(u).c_str(), us.brainFlags, now.brainFlags,
                     now.surrendered ? " (hands up)" : "", posStr(u->body->pos).c_str());
            if (now.mflags != us.mflags)
                logf("%s mover flags 0x%02X -> 0x%02X", label(u).c_str(), us.mflags, now.mflags);
            if (now.visible != us.visible)
                logf("%s %s at %s", label(u).c_str(), now.visible ? "appears" : "disappears", posStr(u->body->pos).c_str());
            if (t->type == TeamType::Seal && (now.posture != us.posture || now.mode != us.mode))
                logf("%s posture %d mode %d speed %d at %s", label(u).c_str(), now.posture, now.mode, u->mover->speed,
                     posStr(u->body->pos).c_str());
            us = now;
        }
    }
    for (int i = 0; i < kShots; ++i) {
        const ShotRec& s = S.shots[i];
        ShotRec& old = g_shots[i];
        if (s.state == 0 && s.shooter && (old.state != 0 || old.shooter != s.shooter || old.weapon != s.weapon)) {
            logf("shot %d: %s fires %s x%d at %s range %d (target %s)", i, label(s.shooter).c_str(),
                 weaponDef(s.weapon).short_name, s.rounds, posStr(s.target_pos).c_str(), s.range,
                 label(s.target).c_str());
            const Team* st = s.shooter->team;
            const WeaponDef& wd = weaponDef(s.weapon);
            if (isEnemyTeam(st)) {
                if (u8(wd.fire_modes) & fire_mode::kThrow) ++g_tally.enemyThrown;
                else if (wd.blast_radius <= 0) ++g_tally.enemyBullets;
                else ++g_tally.enemyOther;
            } else if (st && int(st->type) >= 1 && int(st->type) <= 3) {
                ++g_tally.craftShots;
            }
        } else if (s.state != 0 && old.state == 0 && old.shooter) {
            logf("shot %d resolved", i);
        }
        old = s;
    }
    for (int i = 0; i < 3; ++i) {
        const bool d = objectiveComplete(i);
        if (d && !g_objDone[i]) logf("objective %d complete", i + 1);
        g_objDone[i] = d;
    }
}

void snapshot() {
    const MissionState& S = ms();
    for (int i = 0; i < kMaxTeams && S.teams[i]; ++i) {
        const Team* t = S.teams[i];
        const Unit* l = t->members[0];
        if (!l || !(l->body->flags & 1)) continue;
        logf("  pos team %d %s leader %s hdg %d speed %d alive %d", i, teamTypeName(t->type),
             posStr(l->body->pos).c_str(), l->body->heading >> 3, l->mover ? l->mover->speed : 0, grpCountAlive(t));
    }
}

int simMissionCommand(const DevArgs& args) {
    if (args.empty()) {
        std::printf("usage: --sim-mission <1..80> [--ticks N] [--seed-skip K] [--dt D] [--script idle|walk|hold|attack] "
                    "[--craft b|u|a|g|G] [--summary] [--log FILE]\n");
        return 2;
    }
    const int mission = std::atoi(args[0].c_str());
    if (mission < 1 || mission > 80) {
        std::printf("mission must be 1..80\n");
        return 2;
    }
    int ticks = 0x3C00 * 3;
    int seedSkip = 0;
    int dt = 5;
    enum class Script { Idle, Walk, Hold, Attack };
    Script script = Script::Idle;
    int craftKey = 'b';  // attack script: the map order key of the craft ('b' boat, 'u' helicopter, 'a' aircraft)
    bool summary = false;
    bool snapshots = true;
    std::string logFile;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--ticks") ticks = std::atoi(next().c_str());
        else if (a == "--seed-skip") seedSkip = std::atoi(next().c_str());
        else if (a == "--dt") dt = std::max(1, std::atoi(next().c_str()));
        else if (a == "--script") {
            const std::string s = next();
            script = s == "walk" ? Script::Walk : s == "hold" ? Script::Hold : s == "attack" ? Script::Attack : Script::Idle;
        }
        else if (a == "--craft") {
            const std::string s = next();
            craftKey = s == "u" ? 'u' : s == "a" ? 'a' : s == "g" ? 'g' : s == "G" ? 'G' : 'b';
        }
        else if (a == "--summary") summary = true;
        else if (a == "--log") logFile = next();
        else if (a == "--quiet-snapshots") snapshots = false;
    }
    const bool walk = script == Script::Walk;
    const bool hold = script == Script::Hold || script == Script::Attack;
    g_extended = summary || hold;
    g_tally = Tally{};
    g_lastShooterType.clear();
    if (!logFile.empty()) {
        g_out = std::fopen(logFile.c_str(), "w");
        if (!g_out) {
            std::printf("cannot write %s\n", logFile.c_str());
            return 1;
        }
    }
    for (int i = 0; i < seedSkip; ++i) engine::rng().next();

    MissionSetup setup;
    setup.missionNo = mission - 1;
    setup.year = (mission - 1) / 20;
    setup.gameMode = GameMode::Demo;
    setup.headless = true;
    // The loadout depends on the objectives: load the MCI first.
    loadGameData();
    resetMission();
    if (!msnLoad(setup.missionNo)) return 1;
    buildTestTeam(setup);

    g_units.clear();
    g_teams.clear();
    std::memset(g_shots, 0, sizeof g_shots);
    for (int i = 0; i < kShots; ++i) g_shots[i].state = 1;
    for (bool& b : g_objDone) b = false;
    setMessageObserver([](const Message& m) {
        if (m.style == 3 && m.duration != 0 && m.kind != 4) logf("hand signal %d by '%s'", m.icon, m.text.c_str());
        else if (!m.text.empty() && m.text != " ") logf("message: \"%s\"", m.text.c_str());
    });
    setSfxObserver([](int id, s32 lifetime, const Vec3* pos, int ch) {
        if (ch >= 0) logf("sfx %d (0x%X ticks) at %s ch %d", id, lifetime, pos ? posStr(*pos).c_str() : "-", ch);
    });
    setPhantomObserver([](const std::string& s) { logf("phantom: %s", s.c_str()); });
    setShotHitObserver([](const ShotRec& s, const Unit* u, int wound) {
        const Team* st = s.shooter ? s.shooter->team : nullptr;
        const int stt = st ? int(st->type) : -1;
        if (u->team && u->team->type == TeamType::Seal && stt >= 0 && stt < 8) {
            ++g_tally.sealWounds[stt];
            g_lastShooterType[u] = stt;
        }
        if (g_extended)
            logf("hit: %s %s wounds %s %s with %s, bits 0x%04X%s", label(s.shooter).c_str(),
                 st ? teamTypeName(st->type) : "?", label(u).c_str(), u->team ? teamTypeName(u->team->type) : "?",
                 weaponDef(s.weapon).short_name, wound, (wound & hit_bit::kKilled) ? " KILLED" : "");
    });

    if (!simInit(setup)) return 1;
    dumpWorld();
    trackChanges();

    int stepsSinceTurn = 0;
    bool running = false;
    int holdTeam = -1;        // hold / attack: the enemy team approached
    bool holding = false;     // stopped at ~250 units from it
    bool attackDone = false;  // attack: the craft order was given
    const bool phantomScript = script == Script::Attack && (craftKey == 'g' || craftKey == 'G');
    int phantomOrders = 0;    // attack --craft g: strike orders given (the 4th answers Winchester)
    Ticks nextPhantomOrder = 2000;
    Ticks nextPhantomSnap = 0;
    // attack --craft g: the squad stays outside the bomb's danger-close
    // distance (blast radius 180 + 90) of the enemy it approaches.
    const int holdDist = phantomScript ? 450 : 250;
    Ticks now = 0;
    Ticks nextSnap = 0x800;
    // Turn the Point Man toward a position, 3 degrees every 4th frame.
    auto steerToward = [&](const Unit* pm, const Vec3& goal) {
        const int want = geoBearing(pm->body->pos, goal);
        const int have = (pm->body->heading >> 3);
        int diff = want - have;
        while (diff > 180) diff -= 360;
        while (diff < -180) diff += 360;
        if (++stepsSinceTurn >= 4 && (diff > 3 || diff < -3)) {
            playerTurn(diff > 0 ? 3 : -3);
            stepsSinceTurn = 0;
        }
    };
    while (now < ticks && !ms().misDone) {
        MissionState& S = ms();
        Unit* pm = pointMan();
        // Render phase: sound listener + snd_update.
        engine::sound().setListener(pm ? &pm->body->pos.x : nullptr);
        engine::sound().updateSfx();
        now += dt;
        simFrameClock(now);
        simFrameAnimate();
        // Input phase.
        if (S.viewMode == 7) {
            if (S.insertionClearTime < S.time) misInsertionClear();
        } else {
            playerViewPrologue(0);
            if (walk && pm && unitAlive(pm)) {
                // Face the first objective and run to it.
                steerToward(pm, S.mci.objective[0].pos);
                if (!running) {
                    evtPlayerSpeedStep(0xC00);
                    evtPlayerSpeedStep(0xC00);
                    running = true;
                }
            } else if (hold && pm && unitAlive(pm)) {
                // Run at the first enemy team, stop about 250 units from it and
                // hold there (facing it) so that it engages for a long time.
                if (holdTeam < 0) {
                    holdTeam = firstEnemyTeam();
                    if (holdTeam >= 0)
                        logf("script: approaching team %d at %s", holdTeam,
                             posStr(S.teams[holdTeam]->members[0]->body->pos).c_str());
                }
                const Unit* enemy = holdTeam >= 0 ? S.teams[holdTeam]->members[0] : nullptr;
                if (enemy) {
                    steerToward(pm, enemy->body->pos);
                    const int d = geoDistance(pm->body->pos, enemy->body->pos);
                    if (!holding && d <= holdDist) {
                        evtPlayerSpeedStep(0);
                        holding = true;
                        logf("script: holding %d units from team %d", d, holdTeam);
                    } else if (!holding && !running) {
                        evtPlayerSpeedStep(0xC00);
                        evtPlayerSpeedStep(0xC00);
                        running = true;
                    }
                }
                if (phantomScript) {
                    // Port (Modern gameplay): the map's 'g' order, a Phantom strike at
                    // the nearest enemy team's leader ('G': 400 units ahead of the
                    // running squad, inside the danger-close distance by the time the
                    // flight arrives); one order per run while the flight is parked,
                    // until the fourth is answered with Winchester.
                    if (phantomOrders < 4 && S.time >= nextPhantomOrder && modernPhantomPhase() == 0) {
                        ++phantomOrders;
                        nextPhantomOrder = S.time + 0x1000;
                        const int target = craftKey == 'G' ? 0 : nearestEnemyTeam(pm->body->pos);
                        if (!modernPhantomAvailable() || target < 0) {
                            logf("script: no Phantom strike (flight team %d, enemy team %d)", ms().portPhantomGroup, target);
                            phantomOrders = 4;
                        } else {
                            const int sel = S.mapSelTeam;
                            S.mapSelTeam = ms().portPhantomGroup;
                            if (craftKey == 'G') {
                                S.wpSupport = pm->body->pos;
                                posMovePolar(s32(400) << 8, 0, pm->body->heading, S.wpSupport);
                            } else {
                                S.wpSupport = S.teams[target]->members[0]->body->pos;
                            }
                            const int r = mapOrderKey('g');
                            S.mapSelTeam = sel;
                            logf("script: Phantom strike order %d at %s (team %d): result %d, phase %d, strikes left %d",
                                 phantomOrders, posStr(S.wpSupport).c_str(), target, r, modernPhantomPhase(),
                                 modernPhantomStrikesLeft());
                        }
                    }
                } else if (script == Script::Attack && !attackDone && S.time >= 2000) {
                    // The map's attack order of the support craft at the nearest enemy team.
                    attackDone = true;
                    const TeamType want = craftKey == 'u' ? TeamType::Helicopter
                                        : craftKey == 'a' ? TeamType::Aircraft : TeamType::Boat;
                    const int craft = firstTeamOfType(want);
                    const int target = nearestEnemyTeam(pm->body->pos);
                    if (craft < 0 || target < 0) {
                        logf("script: no attack order (craft team %d, enemy team %d)", craft, target);
                    } else {
                        const int sel = S.mapSelTeam;
                        S.mapSelTeam = craft;
                        S.wpSupport = S.teams[target]->members[0]->body->pos;
                        const int r = mapOrderKey(craftKey);
                        S.mapSelTeam = sel;
                        logf("script: attack order '%c' to team %d %s at %s (team %d): result %d, order %d", craftKey,
                             craft, teamTypeName(S.teams[craft]->type), posStr(S.wpSupport).c_str(), target, r,
                             S.teams[craft]->order);
                    }
                }
            }
        }
        simFrameUpdate();
        simMsgTick();
        trackChanges();
        if (phantomScript && modernPhantomAvailable() && S.time >= nextPhantomSnap) {
            // The flight's path: both aircraft every 0x400 ticks.
            nextPhantomSnap = S.time + 0x400;
            const Team* t = team(ms().portPhantomGroup);
            const Unit* a = t ? t->members[0] : nullptr;
            const Unit* b = t ? t->members[1] : nullptr;
            if (a)
                logf("phantom: phase %d strikes left %d lead %s hdg %d spd %d alt %d%s%s", modernPhantomPhase(),
                     modernPhantomStrikesLeft(), posStr(a->body->pos).c_str(), a->body->heading >> 3, a->mover->speed,
                     a->mover->height, b ? " wing " : "", b ? posStr(b->body->pos).c_str() : "");
        }
        if (snapshots && S.time >= nextSnap) {
            nextSnap += 0x800;
            snapshot();
        }
    }
    logf("end: mission done %d, extraction end %d, SEALs alive %d, enemy dead %d, rounds fired %d hit %d",
         ms().misDone ? 1 : 0, ms().extractionEnd, sealCountAlive(), statCountDead(4) + statCountDead(5),
         ms().stats.roundsFired, ms().stats.roundsHit);
    if (g_extended) logSummary();
    simShutdown();
    setMessageObserver(nullptr);
    setSfxObserver(nullptr);
    setShotHitObserver(nullptr);
    setPhantomObserver(nullptr);
    if (g_out != stdout) std::fclose(g_out);
    g_out = stdout;
    return 0;
}

const DevCommand g_cmd("--sim-mission",
                       "<1..80> [--ticks N] [--seed-skip K] [--dt D] [--script idle|walk|hold|attack] [--craft b|u|a|g|G] "
                       "[--summary]: headless mission simulation log",
                       simMissionCommand);

} // namespace

} // namespace st::game::mission
