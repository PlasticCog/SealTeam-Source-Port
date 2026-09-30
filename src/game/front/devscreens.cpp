// Developer entry points of the front end (see game/devtools.h):
//   --screen NAME [options]   open one screen with plausible state
//   --main-loop [options]     run the whole game flow (main_game_loop)
//   --cmp-roundtrip DIR       load/re-save c0..c8.cmp, s.cnf, st1.dfr into DIR and compare
// Options: --slot N (campaign file to load, default 0), --mission N (0..79),
// --practice, --save-dir DIR (where the game writes files; recommended for
// every test), --keys SPEC (scripted keys, see front/common.h), --kind K
// (speech kind), --won 0/1, --kia N (SEALs killed last mission), --score N,
// --sample (plausible mission results for the debriefing).
#include "data/gamefs.h"
#include "engine/rng.h"
#include "game/campaign.h"
#include "game/config.h"
#include "game/devtools.h"
#include "game/front/common.h"
#include "game/front/front.h"
#include "game/globals.h"
#include "game/main_loop.h"
#include "game/screens.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace st::game::front {

namespace {

struct Options {
    int slot = 0;
    int mission = -1;
    bool practice = false;
    int kind = 0;
    int won = -1;
    int kia = -1;
    int score = -9999;
    bool sample = false;
    std::string saveDir, keys;
};

Options parseOptions(const DevArgs& args, size_t first) {
    Options o;
    for (size_t i = first; i < args.size(); ++i) {
        const std::string& a = args[i];
        const bool more = i + 1 < args.size();
        if (a == "--slot" && more) o.slot = std::atoi(args[++i].c_str());
        else if (a == "--mission" && more) o.mission = std::atoi(args[++i].c_str());
        else if (a == "--practice") o.practice = true;
        else if (a == "--kind" && more) o.kind = std::atoi(args[++i].c_str());
        else if (a == "--won" && more) o.won = std::atoi(args[++i].c_str());
        else if (a == "--kia" && more) o.kia = std::atoi(args[++i].c_str());
        else if (a == "--score" && more) o.score = std::atoi(args[++i].c_str());
        else if (a == "--sample") o.sample = true;
        else if (a == "--save-dir" && more) o.saveDir = args[++i];
        else if (a == "--keys" && more) o.keys = args[++i];
    }
    return o;
}

void applyCommon(const Options& o) {
    if (!o.saveDir.empty()) {
        std::filesystem::create_directories(o.saveDir);
        setSaveDir(o.saveDir);
    }
    cfgLoadSCnf(slotConfig());
    cfgLoadDifficulty();
    if (!o.keys.empty()) setKeyScript(o.keys);
}

// A campaign state from a saved slot, or a practice team for a mission.
void prepareState(const Options& o) {
    Globals& gs = g();
    CampaignHeader& h = campaign::header();
    if (o.practice) {
        gs.gameMode = GameMode::Practice;
        campaign::stateResetForMode();
        const int m = o.mission >= 0 ? o.mission : 0;
        h.year = u8(m / 20);
        h.mission = u8(m % 20);
        gs.missionNo = m;
        h.point_man = s16((h.year & 1) ? 0x22 : 0x0a);
        campaign::rosterNew((h.year & 1) ? 0x19 : 0);
    } else {
        gs.gameMode = GameMode::Campaign;
        campaign::stateResetForMode();
        if (!campaign::load(o.slot)) logWarn("c%d.cmp not found", o.slot);
        h.slot = s8(o.slot);
        if (o.mission >= 0) {
            h.year = u8(o.mission / 20);
            h.mission = u8(o.mission % 20);
        }
        gs.missionNo = h.mission == 0xff ? h.year * 20 : h.year * 20 + h.mission;
    }
    if (o.sample) {
        // Plausible end-of-mission numbers for the debriefing screens.
        campaign::MissionResult& r = campaign::result();
        r.success[0] = 1;
        r.success[1] = 1;
        r.enemyKia = 7;
        r.enemyCaptured = 1;
        r.weaponsCaptured = 3;
        r.documentsCaptured = 0;
        r.sealWia = 1;
        r.minutes = 97;
        r.extractionType = 1;
        campaign::MissionStats& st = campaign::stats();
        st.roundsFired = 412;
        st.roundsHit = 38;
        st.grenadesThrown = 4;
        st.grenadesHit = 2;
        st.score = 1234;
    }
    if (o.score != -9999) campaign::stats().score = s16(o.score);
    if (o.won >= 0) campaign::result().success[0] = u8(o.won);
    if (o.kia >= 0) campaign::result().sealKia = s16(o.kia);
}

int runScreen(const DevArgs& args) {
    if (args.empty()) {
        std::printf("screens: difficulty recruit campaign intel briefing patrol marching bull insertion extraction "
                    "debrief report historic speech\n");
        return 1;
    }
    const std::string name = args[0];
    const Options o = parseOptions(args, 1);
    applyCommon(o);
    Globals& gs = g();
    gs.showTitle = false;
    int rc = -1;
    if (name == "difficulty") {
        screenTransition(PalMenu, false);
        rc = difficultyScreen();
    } else if (name == "recruit") {
        gs.gameMode = GameMode::Menu;
        campaign::stateResetForMode();
        rc = recruitScreen();
    } else if (name == "campaign") {
        prepareState(o);
        campaign::save(8);  // the screen loads the slot again
        rc = campaignScreen();
    } else if (name == "intel") {
        prepareState(o);
        rc = intelScreen();
    } else if (name == "briefing" || name == "patrol" || name == "marching") {
        prepareState(o);
        campaign::missionLoad();
        campaign::loadoutBuildTeam();
        rc = briefingScreen();
    } else if (name == "bull") {
        prepareState(o);
        campaign::missionLoad();
        campaign::loadoutBuildTeam();
        rc = bullScreen();
    } else if (name == "insertion" || name == "extraction") {
        prepareState(o);
        campaign::missionLoad();
        campaign::loadoutBuildTeam();
        rc = name == "insertion" ? insertionScreen() : extractionScreen();
    } else if (name == "debrief" || name == "report" || name == "historic") {
        prepareState(o);
        campaign::missionLoad();
        campaign::loadoutBuildTeam();
        if (o.keys.empty() && name != "debrief") setKeyScript(name == "report" ? "p@4.5" : "h@4.5");
        rc = debriefScreen(true);
    } else if (name == "speech") {
        prepareState(o);
        rc = speechScreen(o.kind);
    } else {
        std::printf("unknown screen %s\n", name.c_str());
        return 1;
    }
    logInfo("screen %s returned %d", name.c_str(), rc);
    return 0;
}

bool readFile(const std::string& path, std::vector<u8>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Loads every shipped campaign file through the campaign model and saves it
// into DIR; the bytes must be identical.
int cmpRoundTrip(const DevArgs& args) {
    if (args.empty()) {
        std::printf("usage: --cmp-roundtrip DIR\n");
        return 1;
    }
    const std::string dir = args[0];
    std::filesystem::create_directories(dir);
    int failures = 0;
    cfgLoadSCnf(slotConfig());  // from the Game folder
    for (int slot = 0; slot <= 8; ++slot) {
        char name[16];
        std::snprintf(name, sizeof name, "c%d.cmp", slot);
        std::vector<u8> original;
        if (!gameFS().readFile(name, original)) {
            std::printf("%s: missing\n", name);
            continue;
        }
        setSaveDir("");
        g().gameMode = GameMode::Campaign;
        if (!campaign::load(slot)) {
            std::printf("%s: load failed\n", name);
            ++failures;
            continue;
        }
        setSaveDir(dir);
        campaign::save(slot);
        std::vector<u8> saved;
        readFile((std::filesystem::path(dir) / name).string(), saved);
        const bool same = saved == original;
        std::printf("%s: %zu bytes, year %d mission %d pm %d score %d roster %zu -> %s\n", name, original.size(),
                    campaign::header().year, campaign::header().mission, campaign::header().point_man,
                    int(campaign::header().total_score), campaign::roster().size(), same ? "identical" : "DIFFERENT");
        if (!same) ++failures;
        campaign::rosterFree();
    }
    // s.cnf and st1.dfr
    setSaveDir(dir);
    for (const char* name : {"s.cnf", "st1.dfr"}) {
        std::vector<u8> original, saved;
        gameFS().readFile(name, original);
        if (std::strcmp(name, "s.cnf") == 0) {
            setSaveDir("");
            cfgLoadSCnf(slotConfig());
            setSaveDir(dir);
            cfgSaveSCnf(slotConfig());
        } else {
            setSaveDir("");
            cfgLoadDifficulty();
            setSaveDir(dir);
            cfgSaveDifficulty();
        }
        readFile((std::filesystem::path(dir) / name).string(), saved);
        const bool same = saved == original;
        std::printf("%s: %zu bytes -> %s\n", name, original.size(), same ? "identical" : "DIFFERENT");
        if (!same) ++failures;
    }
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace

const DevCommand kScreenCommand("--screen", "open one front-end screen: --screen NAME [--slot N] [--mission N] "
                                            "[--practice] [--save-dir DIR] [--keys SPEC]",
                                runScreen);

const DevCommand kMainLoopCommand("--main-loop", "run main_game_loop (title, menu, campaign flow) [--save-dir DIR] "
                                                 "[--keys SPEC]",
                                  [](const DevArgs& args) {
                                      const Options o = parseOptions(args, 0);
                                      applyCommon(o);
                                      return mainLoop();
                                  });

const DevCommand kCmpRoundTrip("--cmp-roundtrip", "load and re-save c0..c8.cmp, s.cnf, st1.dfr into DIR and compare",
                               cmpRoundTrip);

} // namespace st::game::front
