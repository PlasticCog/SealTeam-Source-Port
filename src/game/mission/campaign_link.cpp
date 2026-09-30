#include "game/mission/campaign_link.h"

#include "game/campaign.h"
#include "game/config.h"
#include "game/globals.h"
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/entity.h"
#include "game/mission/state.h"
#include "game/mission/wquery.h"

namespace st::game::mission {

namespace {

// score_eval_objective (365e:DC11, docs/re/seg_365e_b.md 12) for objective i.
bool scoreEvalObjective(int i) {
    const MissionState& S = ms();
    const MciObjective& o = S.mci.objective[i];
    const int teamIdx = S.firstMtmGroup + o.target_team;
    switch (int(u16(o.kind))) {
    case 1: return wldObjectFlag0(o.target_structure);
    case 2: {
        const Team* t = team(teamIdx);
        if (!t) return false;
        int total = 0, n = 0;
        for (int m = 0; m < 8 && t->members[m]; ++m) {
            ++total;
            const Unit* u = t->members[m];
            // 365e:DC98: dead (hit_mask 0x4000) or mover flag 0x20 (secured).
            if (unitDead(u) || (u->mover->flags & mover_flag::kSecured)) ++n;
        }
        return total > 0 && 100 * n / total > 65;
    }
    case 3: return wldObjectDestroyed(o.target_structure);
    case 4: return wldObjectiveDone(o.target_structure);
    case 5:
    case 6: return entGroupHasSecuredMember(teamIdx);
    case 7: return wldObjectFlag0(o.target_structure);
    default: return false;
    }
}

} // namespace

MissionSetup missionSetupFromCampaign() {
    MissionSetup s;
    s.missionNo = g().missionNo;
    s.year = campaign::header().year;
    s.gameMode = g().gameMode;
    s.options = difficulty();
    const LoadoutRecord* l = campaign::loadout();
    for (int k = 0; k < 4; ++k) s.loadout[size_t(k)] = l[k];
    s.rosterFind = [](int se) { return campaign::rosterFind(se); };
    s.detailLevel = g().detailLevel;
    s.handSignals = true;
    s.headless = false;
    return s;
}

// The original counts into the far 53BA statistics block in place; the
// debriefing reads it whether the mission ended by extraction or by an
// Esc+Y abort. Mirror the mission counters into it at every exit.
void copyMissionStats() {
    const MissionState& S = ms();
    campaign::MissionStats& st = campaign::stats();
    st.roundsFired = u16(S.stats.roundsFired);
    st.roundsHit = u16(S.stats.roundsHit);
    st.grenadesThrown = u16(S.stats.grenadesThrown);
    st.grenadesHit = u16(S.stats.grenadeHits);
    st.bonus = u16(S.stats.bonusCounter);
}

void publishMissionResults() {
    MissionState& S = ms();
    // msn_tally_casualties: roster part (roster_clear_wounded, roster_record_casualty).
    campaign::rosterClearWounded();
    const Team* t0 = team(0);
    for (int m = 0; t0 && m < 8 && t0->members[m]; ++m) {
        Unit* u = t0->members[m];
        if (u->roster) campaign::rosterRecordCasualty(*u->roster, m == 0, u->status->hit_mask);
    }
    copyMissionStats();
    campaign::MissionResult& r = campaign::result();
    r.enemyKia = S.enemyKia;
    r.sealKia = S.sealKia;
    r.sealWia = S.sealWia;
    r.minutes = S.missionMinutes;
    r.extractionType = s8(S.extractionType);
    r.weaponsCaptured = S.misWeapons;
    r.documentsCaptured = S.misDocuments;
    r.enemyCaptured = S.misPrisoners;

    campaign::ScoreInput in;
    for (int m = 0; t0 && m < 8 && t0->members[m]; ++m) {
        const Unit* u = t0->members[m];
        campaign::ScoreUnit su;
        su.roster = u->roster;
        su.dead = unitDead(u);
        su.wounded = medWoundLevel(u->status) != 0;
        in.team.push_back(su);
    }
    in.noCraftLost = statNoCraftLost();
    in.deadVc = statCountDead(4);
    in.deadNva = statCountDead(5);
    in.deadCivilians = statCountDead(6);
    for (int i = 0; i < 3; ++i) {
        in.objective[i] = scoreEvalObjective(i);
        const Team* tt = team(S.firstMtmGroup + S.mci.objective[i].target_team);
        int dead = 0;
        for (int m = 0; tt && m < 8 && tt->members[m]; ++m)
            if (unitDead(tt->members[m])) ++dead;
        in.snatchTargetsDead[i] = dead;
    }
    in.missionMinutes = todMissionMinutes();
    in.destroyedObjects = wldCountDestroyed();
    in.capturedEnemies = statCountCapturedEnemies();
    in.teamAllExtracted = entTeamAllExtracted();
    campaign::awardEvaluateMission(in);
    S.stats.missionScore = campaign::stats().score;
    S.stats.teamSize = s16(campaign::stats().teamSize);
}

void addCampaignHooks(SimHooks& hooks) {
    hooks.missionWon = [](s16 score) { return campaign::missionWon(score); };
    hooks.evaluateMission = [] { publishMissionResults(); };
}

} // namespace st::game::mission
