// Orders given on the map screen and at insertion (19ac:52A1 map_screen_keys,
// 19ac:5C8E insert_keys, docs/re/seg_19ac.md 7.5 and 8): waypoints, support
// craft attack / cease / loiter / extract / emergency extraction, and the
// re-insertion. Only the simulation side; pointer, focus and drawing belong
// to the map screen.
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/modern.h"
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/sfx.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/mission/wquery.h"

namespace st::game::mission {

namespace {

// 19ac:5280 map_extract_busy.
bool mapExtractBusy() {
    if (entAnyCraftMoving()) {
        msgShowDs(0x107D, 0x200);  // "Extract already in progress."
        return true;
    }
    return false;
}

bool onWater(Vec3& pos) { return wldProbeKind(pos, 7) || wldProbeKind(pos, 9); }

// Attack order of a boat / helicopter / aircraft ('b' / 'u' / 'a').
int craftAttack(TeamType type, u16 radioMsg, u16 failMsg, int failAck, u16 hotMsg, int button) {
    MissionState& S = ms();
    Team* sel = team(S.mapSelTeam);
    if (!sel || sel->type != type) return -1;
    if (!radioCall(dsText(radioMsg), S.mapSelTeam)) {
        if (S.radioDamaged) return -1;
        if (failMsg) {
            // Original: boats and helicopters give up; the aircraft carries on.
            msgShowDs(failMsg, 0x200);
            sfxRadioAck(failAck);
            return -1;
        }
    }
    if (evtTeamIsEngaged(S.mapSelTeam)) {
        msgShowDs(hotMsg, 0x200);  // "Hang on, Section hot!"
        sfxRadioAck(0x2C);
        return -1;
    }
    sel->order = s16(CraftOrder::Attack);
    sel->members[0]->mover->destination = S.wpSupport;
    sfxRadioAck(0x31);
    return button;
}

} // namespace

void mapSetWaypoint(s32 x, s32 z) {
    MissionState& S = ms();
    Vec3* wp = &S.wpSeal;
    if (S.mapSelTeam != 0) {
        const Team* sel = team(S.mapSelTeam);
        if (!sel) return;
        const int t = int(sel->type);
        if (t != 0 && t < 4) {
            wp = &S.wpSupport;
        } else if (t == 0) {
            wp = (S.teamCount - S.mapSelTeam == 1 && S.splitGroups > 1) ? &S.wpSplitB : &S.wpSplitA;
        } else if (t != 4 && t != 5) {
            return;
        }
    }
    // map_screen_to_world writes x and z only.
    wp->x = x;
    wp->z = z;
}

int mapOrderKey(int key) {
    MissionState& S = ms();
    Team* sel = team(S.mapSelTeam);
    switch (key) {
    case 'a':
        // Port (Modern gameplay): with the Phantom flight selected its attack
        // button / key is the strike.
        if (modernIsPhantomGroup(S.mapSelTeam)) return modernPhantomStrikeOrder();
        return craftAttack(TeamType::Aircraft, 0x1110, 0, 0, 0x1123, 0x19);
    case 'g':  // port only (Modern gameplay): Phantom strike at the support waypoint
        return modernPhantomStrikeOrder();
    case 'b': return craftAttack(TeamType::Boat, 0x109A, 0x10A7, 0x2E, 0x10BB, 0x17);
    case 'u': return craftAttack(TeamType::Helicopter, 0x10D1, 0x10E1, 0x2D, 0x10FA, 0x18);
    case 'k': {
        if (S.mapSelTeam == 0 || S.mapSelTeam >= S.firstMtmGroup || !sel || modernIsPhantomGroup(S.mapSelTeam)) return -1;
        if (!radioCall(dsText(0x1139), S.mapSelTeam)) return -1;  // "Attack Ceased."
        sel->order = s16(CraftOrder::CeaseAttack);
        Vec3 d = S.wpSupport;
        d.x = wrapAdd(d.x, 0x5DC00);
        sel->members[0]->mover->destination = d;
        sfxRadioAck(0x30);
        return 0x1A;
    }
    case 'o': {
        if (S.mapSelTeam == 0 || S.mapSelTeam >= S.firstMtmGroup || !sel || modernIsPhantomGroup(S.mapSelTeam)) return -1;
        if (!radioCall(dsText(0x11F8), S.mapSelTeam)) {  // "Loitering."
            if (!S.radioDamaged) {
                msgShowDs(0x1203, 0x200);  // "Engine malfunction!"
                sfxRadioAck(0x2E);
                sel->order = s16(CraftOrder::Loiter);
            }
            return -1;
        }
        sel->order = s16(CraftOrder::Loiter);
        Unit* l = sel->members[0];
        if (sel->type == TeamType::Boat) {
            // The boat is moved to the waypoint to find the water there, then put back.
            const Vec3 saved = l->body->pos;
            l->body->pos = S.wpSupport;
            evtSetDestToTerrainObj(l, nullptr);
            l->body->pos = saved;
        } else {
            l->mover->destination = S.wpSupport;
        }
        sfxRadioAck(0x30);
        return 0x1C;
    }
    case 'e': {
        if (mapExtractBusy()) return -1;
        if (!sel || (sel->type != TeamType::Boat && sel->type != TeamType::Helicopter)) return -1;
        if (sel->type == TeamType::Boat) {
            if (!onWater(S.wpSupport)) {
                msgShowDs(0x1148, 0x200);  // "Extraction point must be on water!"
                return -1;
            }
            if (!radioCall(dsText(0x116B), S.mapSelTeam)) {
                if (S.radioDamaged) return -1;
                msgShowDs(0x117C, 0x200);  // "Engine malfunction!"
                sfxRadioAck(0x2E);
                return -1;
            }
        } else if (!radioCall(dsText(0x1190), S.mapSelTeam)) {
            if (S.radioDamaged) return -1;
            msgShowDs(0x11A1, 0x200);  // "Rotor damage!"
            sfxRadioAck(0x2F);
            return -1;
        }
        S.extractingGroup = S.mapSelTeam;
        sel->order = s16(CraftOrder::Extract);
        S.extractMarker = S.wpSupport;
        sel->members[0]->mover->destination = S.extractMarker;
        entTeamRejoin();
        sfxRadioAck(0x32);
        return 0x1B;
    }
    case 'y': {
        if (mapExtractBusy()) return -1;
        Unit* pm = pointMan();
        Team* em = team(S.emergencyGroup);
        if (!pm || !em) return -1;
        S.wpSupport = pm->body->pos;
        u16 msg = 0x11E7;  // "Helo Extracting."
        if (em->type == TeamType::Boat) {
            if (!onWater(S.wpSupport)) {
                msgShowDs(0x11AF, 0x200);  // "Extraction point must be on the water!"
                return -1;
            }
            msg = 0x11D6;  // "Boat Extracting."
        }
        // Original: the call goes to the team selected on the map.
        if (!radioCall(dsText(msg), S.mapSelTeam) && S.radioDamaged) return -1;
        S.extractingGroup = S.emergencyGroup;
        em->order = s16(CraftOrder::EmergencyExtract);
        S.extractMarker = S.wpSupport;
        em->members[0]->mover->destination = S.extractMarker;
        unit1UseTool();
        entTeamRejoin();
        sfxRadioAck(0x31);
        return 0x1D;
    }
    case 'f':
        team(0)->fire_order = FireOrder::FieldOfFire;
        return 0x0B;
    case 't':
        team(0)->fire_order = FireOrder::AtTarget;
        return 0x0C;
    case 'w':
        team(0)->fire_order = FireOrder::AtWill;
        return 0x0D;
    default:
        return -1;
    }
}

void reinsertBegin() {
    MissionState& S = ms();
    simSetViewMode(0x0C);
    msgQueueClear();
    msgShowDs(0x1217, 0x7800);  // "Press R to Reinsert."
    S.mapSelTeam = S.insertionGroup;
}

bool reinsertConfirm() {
    MissionState& S = ms();
    Team* craft = team(S.insertionGroup);
    if (!craft) return false;
    if (craft->type == TeamType::Boat && !onWater(S.wpSupport)) {
        msgQueueClear();
        msgShowDs(0x122C, 0x200);   // "Insertion point must be on the water."
        msgShowDs(0x1252, 0x7800);  // "Set New Insertion point, Press R key to Reinsert."
        return false;
    }
    Unit* l = craft->members[0];
    l->body->pos = S.wpSupport;
    pointMan()->body->pos = S.wpSupport;
    if (craft->type == TeamType::Boat) {
        evtSetDestToTerrainObj(l, nullptr);
        posMovePolar(0xB400, 0, l->body->heading, l->body->pos);
    } else if (craft->type == TeamType::Helicopter) {
        l->mover->height = 6;
    }
    entUnitFaceObjective(pointMan());
    evtTeamSnapFormation(0);
    misStartInsertion();
    // clk_reset (1000:30C9): g_time, g_ticks and the saved time are cleared,
    // frame ticks = 1. Original quirk: g_prev_time (DS:ECAC) is not reset, so
    // the next clk_update sees one big (16-bit wrapped) frame.
    S.time = 0;
    S.frameTicks = 1;
    S.mapSelTeam = S.sealTeam;
    return true;
}

} // namespace st::game::mission
