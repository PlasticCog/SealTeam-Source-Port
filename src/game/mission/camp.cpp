// World set-ups outside the mission loop: the mission world alone (briefing
// fly-over, debriefing) and the SEAL camp cut-scenes (19ac:64D4 camp_load_scene,
// 663A camp_build_teams, 6945 camp_place_at_insertion, 6B07
// camp_place_after_extraction; docs/re/seg_19ac.md 10).
#include "data/exeimage.h"
#include "engine/sound.h"
#include "game/mission/ai.h"
#include "game/mission/build.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/people.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/mission/world.h"
#include "game/mission/wquery.h"

namespace st::game::mission {

namespace {

constexpr u16 kAreaCampFrame = 0x3178;  // s16 per world: camp world = value + 0x1C

void applySetup(const MissionSetup& setup) {
    loadGameData();
    resetMission();
    MissionState& S = ms();
    S.missionNo = setup.missionNo;
    S.year = setup.year;
    S.gameMode = setup.gameMode;
    S.opt = setup.options;
    S.loadout = setup.loadout;
    S.detailLevel = setup.detailLevel;
    S.handSignalGfx = setup.handSignals;
    setRosterLookup(setup.rosterFind);
}

s32 mapUnits(s32 v) { return v >> 8; }

WorldObject* worldObjectAt(int n) {
    auto& v = worldObjects();
    return (n >= 0 && n < int(v.size())) ? v[size_t(n)] : nullptr;
}

// Turn the Point Man toward `target` and push him `dist` along that heading.
void faceAndStep(Unit* pm, const Vec3& target, s32 dist) {
    const int h = geoBearing(pm->body->pos, target);
    pm->mover->heading = s16(h);
    pm->mover->desired_heading = s16(h);
    pm->body->heading = Angle8(h << 3);
    posMovePolar(dist, 0, pm->body->heading, pm->body->pos);
}

} // namespace

bool simBuildWorld(const MissionSetup& setup) {
    applySetup(setup);
    if (!msnLoad(setup.missionNo)) return false;
    // mis_load_world (1000:4580)
    worldBegin();
    if (!wldLoad(ms().mci.world)) return false;
    fxCreatePools();
    msnBuildWorld();
    worldEnd();
    misScanSpecials();
    mapInitMarkers();
    return true;
}

// 19ac:663A camp_build_teams.
void campBuildTeams() {
    MissionState& S = ms();
    S.mapSelTeam = 0;
    S.teamCount = 0;
    S.firstMtmGroup = 0xFF;
    S.breakContactGroup = S.fireSupportGroup = S.emergencyGroup = S.extractionGroup = S.insertionGroup = 0xFF;
    S.boatGroup = S.airGroup = S.heloGroup = 0xFF;
    S.sealTeam = 0;
    Team* t = entGroupCreate(TeamType::Seal, 0, 1, -1, 8);
    const WorldObject* w0 = worldObjectAt(0);
    const s32 x = w0 ? mapUnits(w0->body->pos.x) : 0;
    const s32 z = w0 ? mapUnits(w0->body->pos.z) + 0x1E : 0;
    for (int k = 0; k < 4; ++k)
        entGroupAddUnit(t, k == 0 ? 0x17 : 0x1F, UnitClass::Seal, S.loadout[size_t(k)].se_id, x, z);
    evtTeamSnapFormation(S.sealTeam);
    const MciHeader& h = S.mci;
    const Unit* pm = pointMan();
    entSpawnInsertionCraft(int(u16(h.insertion_method)), mapUnits(pm->body->pos.x), mapUnits(pm->body->pos.z) + 0x12C);
    S.insertionGroup = S.teamCount - 1;
    int last = S.insertionGroup;  // the group whose status the next call sets
    grpSetCraftStatus(last, 4);
    evtTeamSnapFormation(S.insertionGroup);
    int craft = 1;
    const s32 ix = mapUnits(h.insertion.x), iz = mapUnits(h.insertion.z);
    if (h.insertion_method != h.extraction_method) {
        ++craft;
        entSpawnInsertionCraft(int(u16(h.extraction_method)), ix + 0x708, iz + 0x708);
        last = S.teamCount - 1;
        grpSetCraftStatus(last, 4);
        evtTeamSnapFormation(S.extractionGroup);
    }
    if (u16(h.fire_support) != 0) {
        entSpawnSupportCraft(int(u16(h.fire_support)), ix + 0x708, iz - 0x708);
        S.fireSupportGroup = S.teamCount - 1;
        last = S.fireSupportGroup;
        grpSetCraftStatus(last, 4);
        evtTeamSnapFormation(S.fireSupportGroup);
        ++craft;
    }
    if (craft < 3 && u16(h.break_contact) != 0) {
        entSpawnSupportCraft(int(u16(h.break_contact)), ix + 0x4B0, iz + 0x4B0);
        S.breakContactGroup = S.teamCount - 1;
        // Original quirk (19ac:68CC): the new group is not stored, the status
        // goes to the previously created craft group again.
        grpSetCraftStatus(last, 4);
        evtTeamSnapFormation(S.breakContactGroup);
    }
    if (S.emergencyGroup == 0xFF) {
        entSpawnInsertionCraft(8, mapUnits(h.extraction.x), mapUnits(h.extraction.z));
        grpSetCraftStatus(S.teamCount - 1, 4);
    }
    if (S.fireSupportGroup == 0xFF) S.fireSupportGroup = S.emergencyGroup;
}

// 19ac:64D4 camp_load_scene(area): the camp world of the mission's area.
bool campLoadScene(const MissionSetup& setup, int area) {
    // DS:EC96 (dead SEALs) and DS:EC92 (extracting craft) keep their values
    // from the mission: camp_place_after_extraction reads them.
    const s16 kia = ms().sealKia;
    const int extracting = ms().extractingGroup;
    applySetup(setup);
    ms().sealKia = kia;
    ms().extractingGroup = extracting;
    if (!msnLoad(setup.missionNo)) return false;
    worldBegin();
    if (!wldLoad(exe().dgShort(u16(kAreaCampFrame + area * 2)) + 0x1C)) return false;
    fxCreatePools();
    campBuildTeams();
    worldEnd();
    return true;
}

// 19ac:6945 camp_place_at_insertion.
void campPlaceAtInsertion() {
    MissionState& S = ms();
    S.extractionGroup = S.insertionGroup;
    Team* craft = team(S.insertionGroup);
    Unit* pm = pointMan();
    if (!craft || !pm) return;
    grpSetCraftStatus(S.insertionGroup, 0);
    evtTeamSnapFormation(S.insertionGroup);
    pm->body->pos = craft->members[0]->body->pos;
    craft->order = s16(CraftOrder::Extract);
    int idx = wldNearestObject(pm->body->pos, craft->type == TeamType::Helicopter ? 0xF : 0xE);
    if (idx != -1) pm->body->pos = worldObjectAt(idx)->body->pos;
    idx = wldNearestObject(pm->body->pos, 5);
    if (idx != -1) pm->body->pos = worldObjectAt(idx)->body->pos;
    faceAndStep(pm, craft->members[0]->body->pos, 0x3C00);
    evtSetMoveMode(pm, 1);
    evtTeamSnapFormation(S.sealTeam);
}

// 19ac:6B07 camp_place_after_extraction: dead SEALs (ms().sealKia) are hidden
// from the last slot down.
void campPlaceAfterExtraction() {
    MissionState& S = ms();
    int ci = S.extractionGroup;
    if (S.extractingGroup != 0xFF) {
        S.extractionGroup = S.extractingGroup;
        ci = S.extractingGroup;
    }
    Team* craft = team(ci);
    Unit* pm = pointMan();
    if (!craft || !pm) return;
    grpSetCraftStatus(ci, 0);
    evtSetMoveMode(craft->members[0], 0);
    int kia = S.sealKia;
    for (int slot = 3; slot >= 0; --slot) {
        if (kia > 0) {
            unitHideChain(pm->team->members[slot]);
            --kia;
        }
    }
    int idx = wldNearestObject(craft->members[0]->body->pos, craft->type == TeamType::Helicopter ? 0xF : 0xE);
    if (idx != -1) pm->body->pos = worldObjectAt(idx)->body->pos;
    idx = wldNearestObject(pm->body->pos, 5);
    if (idx != -1) faceAndStep(pm, worldObjectAt(idx)->body->pos, 0x1E00);
    evtSetMoveMode(pm, 1);
    evtTeamSnapFormation(S.sealTeam);
}

} // namespace st::game::mission
