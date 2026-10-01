// Developer command --play-mission: start a practice-style mission directly
// with the real loadout_build_team, so screenshots (--shot) can be taken at
// set times with scripted keys (--keys, see front/common.h).
//
//   sealteam [--original|--enhanced] --play-mission <1..80> [--keys SPEC] [--save-dir DIR]
//            [--shot FILE --shot-after S]
#include "engine/rng.h"
#include "game/campaign.h"
#include "game/config.h"
#include "game/devtools.h"
#include "game/front/common.h"
#include "game/front/front.h"
#include "game/globals.h"
#include "game/mission/mission.h"
#include "game/mission/state.h"
#include "game/screens.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace st::game::mission {

namespace {

int playMissionCommand(const DevArgs& args) {
    if (args.empty()) {
        std::printf("usage: --play-mission <1..80> [--keys SPEC] [--save-dir DIR]\n");
        return 2;
    }
    const int mission = std::atoi(args[0].c_str());
    if (mission < 1 || mission > 80) {
        std::printf("mission must be 1..80\n");
        return 2;
    }
    std::string keys, saveDir;
    for (size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == "--keys") keys = args[++i];
        else if (args[i] == "--save-dir") saveDir = args[++i];
    }
    if (!saveDir.empty()) {
        std::filesystem::create_directories(saveDir);
        setSaveDir(saveDir);
    }
    cfgLoadSCnf(slotConfig());
    cfgLoadDifficulty();
    // Practice sequence step 1 of main_game_loop (19ac:016D): random point man
    // of the year's pool, roster, loadout_build_team.
    Globals& gs = g();
    gs.showTitle = false;
    gs.gameMode = GameMode::Practice;
    campaign::stateResetForMode();
    CampaignHeader& h = campaign::header();
    const int m = mission - 1;
    h.year = u8(m / 20);
    h.mission = u8(m % 20);
    gs.missionNo = m;
    int pool;
    if (h.year & 1) {
        h.point_man = s16(engine::rng().range(8) + 0x22);
        pool = 0x19;
    } else {
        h.point_man = s16(engine::rng().range(8) + 0x0A);
        pool = 0;
    }
    campaign::rosterNew(pool);
    campaign::missionLoad();
    campaign::loadoutBuildTeam();
    // The team's loadout (magazines per slot), so a run can be checked.
    for (int k = 0; k < campaign::kTeamSize; ++k) {
        const LoadoutRecord& r = campaign::loadout()[k];
        if (r.se_id == -1) continue;
        std::string slots;
        for (int i = 0; i < 4; ++i)
            if (r.weapons[i] != -1)
                slots += std::string(" ") + campaign::weapon(r.weapons[i]).short_name + " x" + std::to_string(s8(r.reloads[i]));
        logInfo("play-mission: member %d SE %d loadout%s", k, r.se_id, slots.c_str());
    }
    if (!keys.empty()) front::setKeyScript(keys);
    // main loop step 2: fade, "One Moment Please ...", the mission.
    screenTransition(PalMission, true);
    front::showPleaseWait();
    const int rc = run();
    if (rc != 1) unload();
    const MissionState& S = ms();
    logInfo("play-mission: %s, minutes %d, enemy KIA %d, SEAL KIA %d WIA %d, rounds %d/%d, score %d", rc == 1 ? "quit" : "done",
            S.missionMinutes, S.enemyKia, S.sealKia, S.sealWia, S.stats.roundsFired, S.stats.roundsHit, S.stats.missionScore);
    campaign::rosterFree();
    return 0;
}

const DevCommand kPlayMission("--play-mission", "<1..80> [--keys SPEC] [--save-dir DIR]: play a practice-style mission directly",
                              playMissionCommand);

} // namespace

} // namespace st::game::mission
