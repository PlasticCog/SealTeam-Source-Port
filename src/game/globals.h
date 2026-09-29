// Game-wide state that the original keeps as DGROUP globals. Each member
// names its original offset (see tools/re/symbols/merged_globals.tsv).
// Record layouts shared by the game modules live in game/types.h.
#pragma once

#include "core/common.h"

namespace st::game {

enum class GameMode : s16 {  // g_game_mode (DS:D7E4)
    Campaign = 1,            // continue a campaign
    Menu = 2,                // main menu / start a new campaign
    Practice = 3,            // practice mission
    Demo = 6,                // mission given on the command line, then exit
};

struct Globals {
    GameMode gameMode = GameMode::Menu;  // D7E4
    bool showTitle = true;               // 022C, cleared by command-line "t"
    bool digitalAllowed = true;          // 45CC, cleared by command-line "d"
    int missionNo = 0;                   // EEBC, 0..79 in demo mode
    bool sfxOn = true;                   // 45C0
    bool musicOn = true;                 // 45C1
    int detailLevel = 5;                 // 02BE, 0..5
    s32 detailMsgTime = 0;               // Alt-D message time
};

Globals& g();

} // namespace st::game
