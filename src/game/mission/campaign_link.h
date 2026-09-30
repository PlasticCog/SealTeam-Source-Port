// Glue between the mission simulation and the campaign data of the front end
// (game/campaign.h, game/config.h): the mission set-up from the current
// campaign state and the end-of-mission hand-over (casualty recording in the
// roster, statistics, score_mission via awd_evaluate_mission 19ac:00B6).
#pragma once

#include "game/mission/sim.h"

namespace st::game::mission {

// Set-up of the current mission (g().missionNo, campaign year, game mode,
// st1.dfr options, marching-order loadout, roster lookup).
MissionSetup missionSetupFromCampaign();

// Hooks that route cmp_mission_won and awd_evaluate_mission to the campaign
// module; merge them into the loop's own SimHooks (view callbacks etc.).
void addCampaignHooks(SimHooks& hooks);

// The mission's contribution to msn_tally_casualties (365e:1D1F) and the
// scoring input of score_mission, published to campaign::stats()/result().
void publishMissionResults();

} // namespace st::game::mission
