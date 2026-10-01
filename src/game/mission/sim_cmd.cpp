// Developer command --sim-mission: build a mission world headlessly and run
// the simulation with a scripted player, printing a readable event log.
//
//   sealteam --sim-mission <1..80> [--ticks N] [--seed-skip K] [--dt D]
//            [--script idle|walk] [--log FILE] [--quiet-snapshots]
//
// The log lists the spawned teams and units, then every change the frame
// loop observes: messages, sound effects, AI flag / order changes, postures,
// wounds and deaths, shots and their resolution, objectives and the end of
// the mission. Two runs with the same arguments must give identical logs.
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
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/sfx.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/mission/world.h"

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
    logf("world objects %d, teams %d (first MTM team %d, insertion %d, extraction %d, emergency %d, fire support %d)",
         int(worldObjects().size()), S.teamCount, S.firstMtmGroup, S.insertionGroup, S.extractionGroup, S.emergencyGroup,
         S.fireSupportGroup);
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
        std::printf("usage: --sim-mission <1..80> [--ticks N] [--seed-skip K] [--dt D] [--script idle|walk] [--log FILE]\n");
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
    bool walk = false;
    bool snapshots = true;
    std::string logFile;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--ticks") ticks = std::atoi(next().c_str());
        else if (a == "--seed-skip") seedSkip = std::atoi(next().c_str());
        else if (a == "--dt") dt = std::max(1, std::atoi(next().c_str()));
        else if (a == "--script") walk = next() == "walk";
        else if (a == "--log") logFile = next();
        else if (a == "--quiet-snapshots") snapshots = false;
    }
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

    if (!simInit(setup)) return 1;
    dumpWorld();
    trackChanges();

    int stepsSinceTurn = 0;
    bool running = false;
    Ticks now = 0;
    Ticks nextSnap = 0x800;
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
                const int want = geoBearing(pm->body->pos, S.mci.objective[0].pos);
                const int have = (pm->body->heading >> 3);
                int diff = want - have;
                while (diff > 180) diff -= 360;
                while (diff < -180) diff += 360;
                if (++stepsSinceTurn >= 4 && (diff > 3 || diff < -3)) {
                    playerTurn(diff > 0 ? 3 : -3);
                    stepsSinceTurn = 0;
                }
                if (!running) {
                    evtPlayerSpeedStep(0xC00);
                    evtPlayerSpeedStep(0xC00);
                    running = true;
                }
            }
        }
        simFrameUpdate();
        simMsgTick();
        trackChanges();
        if (snapshots && S.time >= nextSnap) {
            nextSnap += 0x800;
            snapshot();
        }
    }
    logf("end: mission done %d, extraction end %d, SEALs alive %d, enemy dead %d, rounds fired %d hit %d",
         ms().misDone ? 1 : 0, ms().extractionEnd, sealCountAlive(), statCountDead(4) + statCountDead(5),
         ms().stats.roundsFired, ms().stats.roundsHit);
    simShutdown();
    setMessageObserver(nullptr);
    setSfxObserver(nullptr);
    if (g_out != stdout) std::fclose(g_out);
    g_out = stdout;
    return 0;
}

const DevCommand g_cmd("--sim-mission", "<1..80> [--ticks N] [--seed-skip K] [--dt D] [--script idle|walk]: headless mission simulation log",
                       simMissionCommand);

} // namespace

} // namespace st::game::mission
