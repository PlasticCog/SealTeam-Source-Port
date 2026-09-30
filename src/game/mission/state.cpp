#include "game/mission/state.h"

#include "game/mission/ai.h"
#include "game/mission/combat.h"
#include "game/mission/craft.h"
#include "game/mission/people.h"
#include "game/mission/teams.h"
#include "game/mission/world.h"

#include <memory>

namespace st::game::mission {

namespace {
std::unique_ptr<MissionState> g_state;
} // namespace

MissionState& ms() {
    if (!g_state) g_state.reset(new MissionState);
    return *g_state;
}

void resetMission() {
    worldFree();
    g_state.reset(new MissionState);
    peopleReset();
    combatReset();
    aiReset();
    teamsReset();
    craftReset();
}

} // namespace st::game::mission
