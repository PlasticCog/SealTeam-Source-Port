// main_game_loop (19ac:016D). The step tables were read from the jump table
// at 19ac:0309 (docs/re/seg_19ac.md 2.2):
//
//   practice (mode 3): 0 intel, 1 random point man + roster + team + c8.cmp +
//                      briefing, 2 mission, 3 debriefing -> main menu
//   campaign (1/2) and demo (6): 0 recruit (mode 2 only), 1 campaign screen,
//                      2 intel, 3 team + autosave + briefing, 4 bull session,
//                      5 insertion cut-scene, 6 mission, 7 extraction
//                      cut-scene, 8 debriefing (mode 2 becomes 1)
//
// Screens return 0 = quit, 1 = back, 2 = next.
#include "game/main_loop.h"

#include "engine/rng.h"
#include "game/campaign.h"
#include "game/config.h"
#include "game/front/front.h"
#include "game/globals.h"
#include "game/menu.h"
#include "game/mission/mission.h"
#include "game/screens.h"
#include "game/title.h"

namespace st::game {

namespace {

// ui_screen_transition(0, 1), "One Moment Please ...", mission (1000:01DE);
// returns true if the player quit the game.
bool playMission() {
    screenTransition(PalMission, true);
    front::showPleaseWait();
    if (mission::run() == 1) return true;
    mission::unload();  // mis_cleanup (1000:0790)
    return false;
}

// Practice sequence; returns true to quit the game.
bool runPractice(int& step) {
    Globals& gs = g();
    for (;;) {
        logInfo("main loop: practice step %d", step);
        switch (step) {
        case 0: {
            const int r = front::intelScreen();
            if (r == 0) return true;
            if (r == 1) {
                step = 0;
                gs.gameMode = GameMode::Menu;
                return false;
            }
            ++step;
            break;
        }
        case 1: {
            // Random point man from the pool of the chosen year (odd years:
            // SEAL 35..42 with pool 25.., even: SEAL 11..18 with pool 0..).
            CampaignHeader& h = campaign::header();
            int pool;
            if (h.year & 1) {
                h.point_man = s16(engine::rng().range(8) + 0x22);
                pool = 0x19;
            } else {
                h.point_man = s16(engine::rng().range(8) + 0x0a);
                pool = 0;
            }
            campaign::rosterNew(pool);
            campaign::loadoutBuildTeam();
            campaign::save(8);
            const int r = front::briefingScreen();
            if (r == 0) return true;
            if (r == 1) {
                step = 0;
                campaign::rosterFree();
                break;
            }
            ++step;
            break;
        }
        case 2:
            if (playMission()) return true;
            ++step;
            break;
        case 3:
            if (front::debriefScreen(true) == 0) return true;
            step = 0;
            campaign::rosterFree();
            gs.gameMode = GameMode::Menu;
            return false;
        default:
            step = 0;
            return true;
        }
    }
}

// Campaign / demo sequence; returns true to quit the game, false to go back
// to the outer loop (main menu).
bool runCampaign(int& step) {
    Globals& gs = g();
    const auto demo = [&gs] { return gs.gameMode == GameMode::Demo; };
    for (;;) {
        if (step == 0 && gs.gameMode != GameMode::Menu) step = 1;
        logInfo("main loop: campaign step %d (mode %d)", step, int(gs.gameMode));
        switch (step) {
        case 0: {
            const int r = front::recruitScreen();
            if (r == 0) return true;
            if (r == 1) {
                step = 0;
                gs.gameMode = GameMode::Menu;
                campaign::rosterFree();
                return false;
            }
            ++step;
            break;
        }
        case 1: {
            if (demo()) {
                ++step;
                break;
            }
            const int r = front::campaignScreen();
            if (r == 0) return true;
            if (r == 1) {
                step = 0;
                gs.gameMode = GameMode::Menu;
                campaign::rosterFree();
                campaign::fromMission() = false;
                return false;
            }
            ++step;
            break;
        }
        case 2: {
            if (demo()) {
                campaign::missionLoad();
                campaign::missionFreeMtm();
                ++step;
                break;
            }
            const int r = front::intelScreen();
            if (r == 0) return true;
            if (r == 1) {
                step = 0;
                campaign::rosterFree();
                break;
            }
            ++step;
            break;
        }
        case 3: {
            campaign::loadoutBuildTeam();
            campaign::saveAutosave();
            if (demo()) {
                ++step;
                break;
            }
            const int r = front::briefingScreen();
            if (r == 0) return true;
            if (r == 1) {
                --step;
                campaign::loadoutReleaseTeam();
                break;
            }
            ++step;
            break;
        }
        case 4:
        case 5: {
            if (demo()) {
                ++step;
                break;
            }
            const int r = step == 4 ? front::bullScreen() : front::insertionScreen();
            if (r == 0) return true;
            if (r == 1) --step;
            else ++step;
            break;
        }
        case 6:
            if (playMission()) return true;
            ++step;
            break;
        case 7:
            if (!demo() && front::extractionScreen() == 0) return true;
            ++step;
            break;
        case 8:
            if (demo()) return true;  // the demo ends after the mission
            if (front::debriefScreen(true) == 0) return true;
            if (gs.gameMode == GameMode::Menu) gs.gameMode = GameMode::Campaign;
            campaign::fromMission() = true;
            step = 0;
            campaign::rosterFree();
            break;
        default:
            step = 0;
            return true;
        }
    }
}

} // namespace

int mainLoop() {
    Globals& gs = g();
    cfgLoadDifficulty();  // cfg_load_difficulty runs at start-up in the original
    campaign::stateResetForMode();
    int step = 0;
    for (;;) {
        if (gs.showTitle) {
            if (gs.gameMode != GameMode::Demo && titleScreen() == 0) return 0;
            gs.showTitle = false;
        }
        if (gs.gameMode == GameMode::Menu) {
            if (menuRun() == 0) return 0;
            campaign::stateResetForMode();
        }
        const bool quit = gs.gameMode == GameMode::Practice ? runPractice(step) : runCampaign(step);
        if (quit) return 0;
    }
}

} // namespace st::game
