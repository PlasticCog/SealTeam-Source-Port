// Team behaviour of segment 2dbd part 2 (docs/re/seg_2dbd_teams_ai.md): search
// and prisoners, buddy following, formations, split-team special orders,
// medic announcements, the player's speed steps and the per-frame execution
// of AI commands.
//
// Implementation file: teams.cpp.
#pragma once

#include "game/types.h"

namespace st::game::mission {

void teamsReset();

void evtSplitTeamsUpdate();                          // 2dbd:3684 every 0x400 ticks (and after "Team split.")
void evtAnnounceMedicAssignments();                  // 2dbd:3D52 every frame
bool evtSearchBegin(Team* t);                        // 2dbd:4E56
bool evtTakePrisonerPhk();                           // 2dbd:4ECA (PHK tool on the reticle target; true = taken)
void evtSearchBody();                                // 2dbd:4FCC
void evtSearchEndReport();                           // 2dbd:516B
// 2dbd:5327: distance of the nearest searchable body (300 = nothing); with
// fill, search record 0 is primed with it.
int evtFindNearestSearchable(Unit* u, bool fill);
bool evtFollowBuddy(Unit* a, Unit* b);               // 2dbd:54F8
void evtSearchMemberStep(Team* t, Unit* member, Unit* leader, bool retarget, int idx);  // 2dbd:58C1
int evtMemberWatchHeading(int headingDeg, int slot, const Team* t);  // 2dbd:5A25
void evtTeamFormationUpdate(int teamIndex);          // 2dbd:5A91 every frame for every team
void evtTeamSnapFormation(int teamIndex);            // 2dbd:6371
bool evtTeamIsEngaged(int teamIndex);                // 2dbd:6487
void evtPlayerSpeedStep(int dir);                    // 2dbd:651D (+0xC00 faster, -0xC00 slower, 0 stop)
void evtExecuteAiCommands();                         // 2dbd:6653 every frame

} // namespace st::game::mission
