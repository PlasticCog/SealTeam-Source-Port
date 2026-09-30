// Team behaviour of segment 2dbd part 2 (docs/re/seg_2dbd_teams_ai.md,
// docs/re/seg_2dbd.md 3.6): split-team special orders, medic announcements,
// search and prisoners, buddy following, formations, the player's speed steps
// and the per-frame execution of AI commands.
//
// Tables of st.exe read at run time: DS:07DC formation slots (s16 x/y pairs,
// 5 per member row; the code indexes them from DS:07C8 / DS:07B4), DS:0868
// buddy follow offsets, DS:0ADE formation scale per team type, DS:1C94 short
// role names, DS:1D9C move-mode names and the message texts.
#include "game/mission/teams.h"

#include "data/exeimage.h"
#include "engine/rng.h"
#include "game/mission/ai.h"
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/sfx.h"
#include "game/mission/state.h"
#include "game/mission/world.h"
#include "game/mission/wquery.h"

#include <string>

namespace st::game::mission {

namespace {

constexpr u16 kSlotBase = 0x07C8;       // DS:07DC formation table - one member row (0x14 bytes)
constexpr u16 kSlotPrevBase = 0x07B4;   // the same table one row earlier (previous member)
constexpr u16 kSlotFirst = 0x07DC;      // g_formation_slot_tbl, row of member 1
constexpr u16 kFollowOffsets = 0x0868;  // g_follow_offset_tbl s16[6][2]
constexpr u16 kFormationScale = 0x0ADE; // g_team_formation_scale u8[8]
constexpr u16 kRoleShortNames = 0x1C94; // char* [] ("Point", "OIC", "Medic", "Rear", ...)
constexpr u16 kMoveModeNames = 0x1D9C;  // char* [4] ("Stop", "Slow", "Run ", "Back")

constexpr int kSatchel = 0x0C;          // weapon DEMO
constexpr int kSilencedPistol = 0x04;   // weapon M39 Hushpuppy

// crt_aFlshl on a sign-extended 16-bit value: v << 8 as a 32-bit value.
s32 shl8(int v) { return s32(u32(s32(s16(v))) << 8); }

int teamType(const Team* t) { return int(t->type); }
bool isEnemyType(int type) { return type == 4 || type == 5; }
int moveMode(const Unit* u) { return int(u->mover->move_mode); }
int posture(const Unit* u) { return int(u->mover->posture); }

// Brain flag 0x08 or ai_mind_has_contacts (evaluated in this order).
bool unitInContact(const Unit* u) {
    if (!u->brain) return false;  // port guard (the original reads through the pointer)
    if (u->brain->flags & brain_flag::kContact) return true;
    return aiMindHasContacts(u->brain);
}

bool pointManAboard() {
    const Unit* pm = pointMan();
    return pm && (pm->mover->flags & mover_flag::kAboard);
}

// A buddy link of an escort/carrier whose buddy is dead.
bool carriesDeadBuddy(const Unit* u) {
    return (u->mover->flags & mover_flag::kEscort) && unitDead(u->buddy);
}

void setPostureAnim(Unit* u, int p) {
    evtSetPosture(u, p);
    sprSetAnim(u, p);
}

} // namespace

void teamsReset() {
    // All state of this module lives in MissionState (search counters, ticks,
    // split waypoints, medic assignments); nothing module-private to clear.
}

// ---------------------------------------------------------------------------
// Split teams (2dbd:3684) and medic announcements (2dbd:3D52)
// ---------------------------------------------------------------------------

// 2dbd:3684 evt_split_teams_update (every 0x400 ticks and after "Team split.").
void evtSplitTeamsUpdate() {
    MissionState& S = ms();
    for (int idx = 1; idx < kMaxTeams && S.teams[idx]; ++idx) {
        Team* t = S.teams[idx];
        if (t->type != TeamType::Seal) continue;
        Unit* l = t->members[0];
        if (!l || unitDead(l)) continue;
        Mover* mv = l->mover;
        // Waypoint B only for the last team of the table while two teams are split.
        const bool useB = (u8(S.teamCount) - idx - 1) == 0 && u8(S.splitGroups) > 1;
        const Vec3& wp = useB ? S.wpSplitB : S.wpSplitA;
        mv->desired_heading = s16(geoBearing(l->body->pos, wp));

        if (moveMode(l) == 0) {
            if (!unitInContact(l)) {
                if (t->order == 1) evtSetMoveMode(l, 2);
                if (t->order == 2) {
                    evtSetMoveMode(l, 1);
                    setPostureAnim(l, 1);
                }
            } else {
                // Original quirk (2dbd:37D5): a "leader moving slowly" branch follows here
                // (run on ASAP orders) but it is unreachable: the mode is always 0 on this path.
                if (t->order == 0) evtSetMoveMode(l, 0);
                if ((t->order == 0 && t->fire_order == FireOrder::Snipe) || unitInContact(l)) setPostureAnim(l, 2);
            }
        }

        int d = geoDistance(l->body->pos, wp);
        if (d < 0xB4) {
            evtSetMoveMode(l, 0);
            setPostureAnim(l, 1);
        }
        if (moveMode(l) == 1) {
            if (posture(l) == 1 && engine::rng().range(3) == 0) setPostureAnim(l, 2);
            else setPostureAnim(l, 1);
        }
        if (d >= 0x4B0) continue;

        if (t->fire_order == FireOrder::Demolish && S.hasDemoObjective) {
            const int obj = msnFindObjectiveTarget(3, &l->body->pos);
            if (obj == -1) continue;
            std::vector<WorldObject*>& objs = worldObjects();
            if (obj < 0 || size_t(obj) >= objs.size() || !objs[size_t(obj)]) continue;  // port guard
            WorldObject* s = objs[size_t(obj)];
            d = geoDistance(l->body->pos, s->body->pos);
            WeaponNode* w = evtUnitFindWeapon(l, kSatchel);
            if (!w) continue;
            if (d < 0x258 && w->rounds != 0) {
                mv->desired_heading = s16(geoBearing(l->body->pos, s->body->pos));
                if (s16(objRadiusOrDefault(s->model) + 0x5A) > d && w->rounds != 0) {
                    // The structure goes into the shot's target slot with target kind 1.
                    shotFire(l, reinterpret_cast<Unit*>(s), s->body->pos, 0, w, d, 1);
                    msgShowDs(0x0A90, 0x300);  // "Performing Demolition."
                    // Original quirk (2dbd:3A7D): with unlimited reloads the charge is
                    // refilled at once and placed again on the next update.
                    wpnReload(w);
                }
                if (w->rounds != 0) {
                    if (moveMode(l) == 0) evtSetMoveMode(l, 1);
                } else {
                    mv->desired_heading = s16(geoBearing(s->body->pos, l->body->pos));  // away from it
                    evtSetMoveMode(l, 2);
                }
            } else if (d > 0x4B0) {
                if (w->rounds == 0 && s->hit_points > 0) {
                    evtSetMoveMode(l, 0);
                    setPostureAnim(l, 1);
                    mv->desired_heading = s16(geoBearing(l->body->pos, s->body->pos));
                }
            } else if (w->rounds == 0 && s->hit_points > 0) {
                evtSetMoveMode(l, 2);
                mv->desired_heading = s16(geoBearing(s->body->pos, l->body->pos));  // run away
            }
            continue;
        }

        if (t->fire_order == FireOrder::Snipe && S.hasAmbushObjective) {
            const int ti = msnFindObjectiveTarget(2, &l->body->pos);
            if (ti == -1) continue;
            WeaponNode* w = evtUnitFindWeapon(l, kSilencedPistol);
            if (!w || w->rounds == 0) continue;
            Team* tt = team(ti);
            if (!tt || !tt->members[0]) continue;  // port guard
            // Original quirk (2dbd:3C2E): only the target team's first member is sniped.
            Unit* tgt = tt->members[0];
            d = geoDistance(l->body->pos, tgt->body->pos);
            if (d < 0x4B0) {
                mv->desired_heading = s16(geoBearing(l->body->pos, tgt->body->pos));
                setPostureAnim(l, 2);
            }
            if (unitDead(tgt)) continue;
            if (d < 0x168) {
                setPostureAnim(l, 2);
                evtSetMoveMode(l, 0);
                if (w->rounds != 0) {
                    // Original quirk (2dbd:3D06): the unit target is passed with target
                    // kind 1 (structure), as for the demolition charge.
                    shotFire(l, tgt, tgt->body->pos, 0, w, d, 1);
                    msgShowDs(0x0AA7, 0x300);  // "Performing Snipe."
                }
            }
        }
    }
}

// 2dbd:3D52 evt_announce_medic_assignments (every frame).
void evtAnnounceMedicAssignments() {
    MissionState& S = ms();
    Team* t0 = team(0);
    for (int slot = 1; slot < 4; ++slot) {
        const int p = S.medicTarget[slot];
        if (p != -1 && t0) {
            Unit* medic = t0->members[slot];
            Unit* patient = (p >= 0 && p < 8) ? t0->members[p] : nullptr;
            if (medic && patient && p != slot) {
                std::string text = dsTextPtr(u16(kRoleShortNames + 2 * slot));
                text += dsText(0x0AB9);  // " "
                text += unitName(medic);
                text += dsText(0x0ABB);  // ": \""
                text += dsText(0x0ABF);  // "Medical aid to "
                text += unitName(patient);
                text += dsText(0x0ACF);  // ".\""
                msgShow(text, 0x300);
            }
        }
        S.medicTarget[slot] = -1;
    }
}

// ---------------------------------------------------------------------------
// Search and prisoners (2dbd:4E56..5327)
// ---------------------------------------------------------------------------

// 2dbd:4E56 evt_search_begin (the original takes a team index).
bool evtSearchBegin(Team* t) {
    MissionState& S = ms();
    if (S.searchTeam || !t || t->order == s16(TeamOrder::Search)) return false;
    S.searchRecCount = 0;
    S.searchDocuments = 0;
    S.searchWeapons = 0;
    S.searchPrisoners = 0;
    t->order = s16(TeamOrder::Search);
    if (t->members[0]) evtFindNearestSearchable(t->members[0], true);
    S.searchTeam = t;
    return true;
}

// 2dbd:4ECA evt_take_prisoner_phk.
// Original quirk: neither the kit count nor whether the target is alive or
// already captured is checked.
bool evtTakePrisonerPhk() {
    MissionState& S = ms();
    TargetRec& r = S.playerTarget;
    if (r.kind != TargetKind::Unit) return false;
    Unit* u = r.target.unit;
    Unit* pm = pointMan();
    const bool enemy = u && u->team && isEnemyTeam(u->team);
    if (enemy && r.range < 0x78 && pm && !pm->buddy) {
        u->mover->flags |= mover_flag::kSearched | mover_flag::kSecured;
        u->buddy = pm;
        pm->mover->flags |= mover_flag::kEscort;
        pm->buddy = u;
        S.misPrisoners = s16(S.misPrisoners + 1);
        if (u->brain) u->brain->surrendered = 1;
        evtSetMoveMode(u, 1);
        msgShowDs(0x0AEE, 0x400);  // "Taking a prisoner."
        return true;
    }
    msgShowDs(0x0B01, 0x400);  // "Prisoner not allowed."
    return false;
}

// 2dbd:4FCC evt_search_body: the body of search record 0.
void evtSearchBody() {
    MissionState& S = ms();
    if (S.searchRecCount >= 8) return;
    TargetRec& r0 = S.searchLog[0];
    if (r0.kind != TargetKind::Unit) return;
    Unit* u = r0.target.unit;
    if (!u || !u->mover || (u->mover->flags & mover_flag::kSearched)) return;
    u->mover->flags |= mover_flag::kSearched;
    if (!unitDead(u)) {
        u->mover->flags |= mover_flag::kSecured;
        u->buddy = evtAssignHelper(u, S.searchTeam);
        if (u->buddy) u->buddy->buddy = u;
        if (isEnemyTeam(u->team)) {
            if (u->buddy) S.searchPrisoners = s16(S.searchPrisoners + 1);
            if (u->brain) u->brain->surrendered = 1;
        } else if (u->team->type == TeamType::Friendly) {
            if (u->brain) u->brain->surrendered = 0;
        }
        if (medStatus(u->status) == 2 && u->buddy && medTreat(u->buddy, u) != 0)
            msgShowDs(0x0B17, 0x300);  // "Applying Medical aid."
    }
    if (isEnemyTeam(u->team)) {
        S.searchWeapons = s16(S.searchWeapons + 2);
        S.searchDocuments = s16(S.searchDocuments + 1);
    }
    S.searchLog[S.searchRecCount + 1] = r0;
    S.searchRecCount = s16(S.searchRecCount + 1);
}

// 2dbd:516B evt_search_end_report.
void evtSearchEndReport() {
    MissionState& S = ms();
    S.misWeapons = s16(S.misWeapons + S.searchWeapons);
    S.misDocuments = s16(S.misDocuments + S.searchDocuments);
    S.misPrisoners = s16(S.misPrisoners + S.searchPrisoners);
    const Unit* pm = pointMan();
    if (pm && pm->team == S.searchTeam) {
        // str_utoa: unsigned decimal.
        msgShow(dsText(0x0B2D) + std::to_string(unsigned(u16(S.searchPrisoners))) + dsText(0x0B43), 0x200);
        msgShow(dsText(0x0B46) + std::to_string(unsigned(u16(S.searchWeapons))) + dsText(0x0B5D), 0x200);
        msgShow(dsText(0x0B60) + std::to_string(unsigned(u16(S.searchDocuments))) + dsText(0x0B79), 0x200);
    }
    S.searchTeam = nullptr;
}

// 2dbd:5327 evt_find_nearest_searchable(unit, out): the original's out
// pointer is either NULL or search record 0's position (fill).
int evtFindNearestSearchable(Unit* u, bool fill) {
    MissionState& S = ms();
    if (!u) return 300;  // port guard
    if (fill) S.searchLog[0].pos = u->body->pos;
    int best = 300;
    Unit* found = nullptr;
    const int ut = teamType(u->team);
    for (int i = 0; i < kMaxTeams && S.teams[i]; ++i) {
        Team* t = S.teams[i];
        if (t == u->team) continue;
        const int tt = teamType(t);
        // Hostile sides: VC/NVA against SEALs, craft and friendlies; friendlies
        // are always scanned; civilians only by VC/NVA searchers.
        bool include;
        if (tt > 3 && tt < 6) include = ut <= 3 || ut == 7;
        else include = ut >= 4 && ut <= 5;
        if (!include && tt != 7) continue;
        for (int k = 0; k < 8 && t->members[k]; ++k) {
            Unit* m = t->members[k];
            if (m->mover->flags & mover_flag::kSearched) continue;
            const int d = geoDistance(u->body->pos, m->body->pos);
            if (d < best) {
                found = m;
                best = d;
            }
        }
    }
    if (best != 300 && fill && found) {
        TargetRec& r = S.searchLog[0];
        r.pos = found->body->pos;
        r.target.unit = found;
        r.kind = TargetKind::Unit;
        r.range = s16(best);
    }
    return best;
}

// ---------------------------------------------------------------------------
// Buddies and formations (2dbd:54F8..6371)
// ---------------------------------------------------------------------------

// 2dbd:54F8 evt_follow_buddy(A escort/carrier, B linked unit): true when B
// was handled.
bool evtFollowBuddy(Unit* a, Unit* b) {
    if (!a || !b) return false;
    Mover* bm = b->mover;
    if ((bm->flags & mover_flag::kSecured) && unitDead(b)) {
        b->buddy = nullptr;
        a->buddy = nullptr;
        a->mover->flags &= u8(~mover_flag::kEscort);
        // "The rescued friendly is dead." / "The prisoner is dead."
        msgShowDs(b->team->type == TeamType::Friendly ? 0x0B7C : 0x0B9A, 0x400);
        return false;
    }
    if (bm->flags & mover_flag::kEscort) return false;

    const s16 aTargetSpeed = a->mover->target_speed;
    const int aPosture = posture(a);
    const int aHeading = a->body->heading;
    const Vec3 ap = a->body->pos;

    // Link kind: 0 enemy prisoner, 1 friendly, 2 seriously wounded, 3 dead,
    // 4 fetch-buddy, 5 nothing to do.
    int kind;
    if ((bm->flags & mover_flag::kSecured) && isEnemyTeam(b->team)) kind = 0;
    else if (b->team->type == TeamType::Friendly) kind = 1;
    else if (unitDead(b)) kind = 3;
    else if (b->status->heavy_wounds != 0) kind = 2;
    else kind = (bm->flags2 & mover_flag2::kFetchBuddy) ? 4 : 5;
    if (kind == 5) return false;

    s16 ox = s16(exe().dgShort(u16(kFollowOffsets + kind * 4)) * 3);
    s16 oy = s16(exe().dgShort(u16(kFollowOffsets + kind * 4 + 2)) * 3);
    mathRotate2d(ox, oy, 0, 0, aHeading);
    bm->destination.x = wrapAdd(shl8(ox), ap.x);
    bm->destination.z = wrapAdd(shl8(oy), ap.z);
    const int dist = geoDistance(b->body->pos, bm->destination);

    if (kind == 0 && dist > 6) bm->desired_heading = s16(geoBearing(b->body->pos, bm->destination));
    else if (kind != 3 || dist < 6) bm->desired_heading = s16(aHeading >> 3);

    if ((dist > 0x18 || a->team->order == s16(TeamOrder::Search)) && (kind == 3 || kind == 2)) {
        // Wounded / dead buddies far away (or while searching) stay where they are;
        // a dead one makes the carrier walk back for it.
        if (kind != 2 && a->team->order != s16(TeamOrder::Search)) a->mover->flags2 |= mover_flag2::kFetchBuddy;
        return true;
    }
    a->mover->flags2 &= u8(~mover_flag2::kFetchBuddy);
    if (kind == 3) {
        if (aPosture == 0) setPostureAnim(a, 1);  // carrying a dead buddy: crouch
    } else {
        if (kind != 0 || dist < 0x0C) {
            if (moveMode(b) != moveMode(a)) evtSetMoveMode(b, moveMode(a));
            if (bm->target_speed != aTargetSpeed) bm->target_speed = aTargetSpeed;
        }
        if (posture(b) != aPosture) setPostureAnim(b, aPosture);
    }
    if (!b->body) return true;
    // A prisoner more than 12 units from the player walks to the slot; every
    // other buddy is placed on it (the whole destination vector, altitude too).
    if (kind == 0 && dist >= 0x0C && a == pointMan()) return true;
    b->body->pos = bm->destination;
    return true;
}

// 2dbd:58C1 evt_search_member_step.
void evtSearchMemberStep(Team* t, Unit* member, Unit* leader, bool retarget, int idx) {
    MissionState& S = ms();
    if (retarget) {
        if (evtFindNearestSearchable(leader, true) == 300) {
            msgShowDs(0x0BB0, 0x200);  // 'Security: "Search complete."'
            msgHandSignal(member, 7);
            t->order = s16(TeamOrder::None);
            evtSearchEndReport();
            return;
        }
        // Member offsets 0, 0, -2, +2, ... across, 3 units behind the body.
        const int s = (idx & 1) ? idx - 1 : -idx;
        const TargetRec& r0 = S.searchLog[0];
        Vec3& dst = member->mover->destination;
        dst.x = wrapAdd(shl8(s * 3), r0.pos.x);
        dst.z = wrapAdd(r0.pos.z, 0x300);
        dst.x = wrapAdd(dst.x, shl8((engine::rng().range(2) - 1) * 3));
        dst.z = wrapAdd(dst.z, shl8((engine::rng().range(2) - 1) * 3));
        return;
    }
    if (geoDistance(member->body->pos, S.searchLog[0].pos) < 0x0F) {
        if (posture(member) == 0) setPostureAnim(member, 1);
        evtSearchBody();
    }
}

// 2dbd:5A25 evt_member_watch_heading (the original updates *heading in place).
int evtMemberWatchHeading(int headingDeg, int slot, const Team* t) {
    const int fo = int(t->fire_order);
    if (fo != 0 && fo != 2 && fo != 3) return headingDeg;
    const int h = s16(headingDeg);
    // Original quirk (2dbd:5A89): C remainder, so h - 90 can give a negative heading.
    if (t->formation == Formation::InLine) {
        if (slot == 1 || slot == 2) return s16(h % 360);
        if (slot == 3) return s16((h + 180) % 360);
        return headingDeg;
    }
    if (slot == 1) return s16((h - 90) % 360);
    if (slot == 2) return s16((h + 90) % 360);
    if (slot == 3) return s16((h + 180) % 360);
    return headingDeg;
}

// 2dbd:5A91 evt_team_formation_update (every frame for every team).
void evtTeamFormationUpdate(int teamIndex) {
    MissionState& S = ms();
    Team* t = team(teamIndex);
    if (!t || !t->members[0]) return;  // port guard
    const int form = int(t->formation);
    // Read live: a member's search step can end the search in the middle of the loop.
    auto searching = [t] { return t->type == TeamType::Seal && t->order == s16(TeamOrder::Search); };

    // Tick A: search retarget (0x400) for a searching SEAL team, else the
    // player's team movement tick (0x200). Tick B: player's team idle (0x900).
    // Neither fires at g_time == 0.
    bool tickA = false, tickB = false;
    if (searching()) {
        if (wrapSub(S.time, S.searchTick) >= 0x400) {
            S.searchTick = S.time;
            tickA = S.time != 0;
        }
    } else if (S.sealTeam == teamIndex) {
        if (wrapSub(S.time, S.teamMoveTick) >= 0x200) {
            S.teamMoveTick = S.time;
            tickA = S.time != 0;
        }
    }
    if (S.sealTeam == teamIndex) {
        if (wrapSub(S.time, S.teamIdleTick) >= 0x900) {
            S.teamIdleTick = S.time;
            tickB = S.time != 0;
        }
    }

    Unit* leader = t->members[0];
    // Members after the first are placed relative to the previously processed
    // member: its position and target speed.
    Vec3 base = leader->body->pos;
    s16 baseTargetSpeed = leader->mover->target_speed;
    leader->mover->aim_heading = s16(leader->body->heading >> 3);

    for (int k = 0; k < 8 && t->members[k]; ++k) {
        Unit* m = t->members[k];
        if (k == 0 && !(m->mover->flags & mover_flag::kSecured)) {
            // The leader only when captured, or in a searching split team.
            if (teamIndex == 0 || !searching()) continue;
        }
        if (evtFollowBuddy(m->buddy, m)) continue;
        if (unitDead(m) && m->team->type != TeamType::Helicopter) continue;

        const int heading = leader->body->heading;
        Mover* mv = m->mover;
        if (entAnyCraftMoving() && pointManAboard()) {
            mv->destination.x = leader->body->pos.x;
            mv->destination.z = leader->body->pos.z;
        } else if (searching() && (k < 3 || grpCountAlive(t) <= 2)) {
            evtSearchMemberStep(t, m, leader, tickA, k);
        } else if (mv->flags2 & mover_flag2::kFetchBuddy) {
            if (m->buddy) mv->destination = m->buddy->body->pos;  // port guard on a broken link
        } else {
            const int scale = exe().dgByte(u16(kFormationScale + teamType(t)));
            // Original quirk (2dbd:5DB7): the table is indexed from DS:07C8 (member 0
            // reads bytes before the table, e.g. for a captured leader).
            const u16 cur = u16(kSlotBase + (5 * k + form) * 4);
            const u16 prev = u16(kSlotPrevBase + (5 * k + form) * 4);
            s16 ox, oy;
            if (k == 1 || (t->order == s16(TeamOrder::Search) && k == 3)) {
                if (k == 3) {
                    base = leader->body->pos;
                    baseTargetSpeed = leader->mover->target_speed;
                }
                ox = exe().dgShort(cur);
                oy = exe().dgShort(u16(cur + 2));
            } else {
                ox = s16(exe().dgShort(cur) - exe().dgShort(prev));
                oy = s16(exe().dgShort(u16(cur + 2)) - exe().dgShort(u16(prev + 2)));
            }
            const s16 f = s16(scale * 3);
            ox = s16(f * ox);
            oy = s16(f * oy);
            mathRotate2d(ox, oy, 0, 0, heading);
            mv->destination.x = wrapAdd(shl8(ox), base.x);
            mv->destination.z = wrapAdd(shl8(oy), base.z);
        }

        const int dist = geoDistance(m->body->pos, mv->destination);
        const int brg = geoBearing(m->body->pos, mv->destination);
        const int type = teamType(t);
        if (type == 0) {
            const Unit* pm = pointMan();
            bool steer = true;
            if (!pointManAboard() && pm && m->team == pm->team && moveMode(m) != 2) steer = false;
            if (steer && !(mv->flags & mover_flag::kBlocked)) mv->desired_heading = s16(brg);
        } else if (isEnemyType(type)) {
            if (moveMode(m) != 0) mv->desired_heading = s16(brg);
        } else {
            mv->desired_heading = s16(brg);
        }

        // Enemy teams in water (sampans) are teleported to their slots.
        if (isEnemyType(type) && (leader->mover->flags & mover_flag::kInWater)) {
            m->body->pos = mv->destination;
            mv->desired_heading = s16(heading >> 3);
            sprSetAnim(m, 1);
            evtSetMoveMode(m, 0);
        }
        // Support craft align with the leader when nearly facing it.
        if (type != 0 && type < 4) {
            const int toLeader = geoBearing(m->body->pos, leader->body->pos);
            if (evtHeadingDiff(brg << 3, toLeader << 3) < 8) mv->desired_heading = s16(heading >> 3);
        }

        if (type == 0) {
            // Movement mode of SEAL members.
            int mode = -1;
            if (moveMode(leader) == 0 && !(mv->flags2 & mover_flag2::kFetchBuddy) &&
                t->order != s16(TeamOrder::Search) && !pointManAboard() && dist <= 0x54) {
                mode = 0;
            } else if (mv->flags & mover_flag::kBlocked) {
                // detour active: leave the mode alone
            } else if (dist >= 0x24) {
                int mm;
                if (posture(leader) != 0 && (!pointManAboard() || wldIsCamp())) mm = 1;
                else if (carriesDeadBuddy(m)) mm = 1;
                else mm = 2;
                evtSetMoveMode(m, mm);
                mv->desired_heading = s16(geoBearing(m->body->pos, mv->destination));
            } else if (dist < 0x0C || t->order == 0) {
                if (moveMode(m) != 0) evtSetMoveMode(m, moveMode(leader));
                if (mv->target_speed > baseTargetSpeed) mv->target_speed = baseTargetSpeed;
            } else if (pointManAboard()) {
                mode = 2;
            } else if (moveMode(leader) != 2) {
                mode = 1;
            } else if (tickA && !carriesDeadBuddy(m)) {
                mode = 2;
            }
            if (mode >= 0) evtSetMoveMode(m, mode);

            if (t->order != s16(TeamOrder::Search) && !pointManAboard()) {
                if (tickB && moveMode(m) != 2 && dist < 0x24) evtSetMoveMode(m, 0);
                if (moveMode(m) == 0) mv->desired_heading = mv->aim_heading;
            }
        }

        if (S.sealTeam == teamIndex && !(leader->mover->flags & mover_flag::kAboard) && !wldIsCamp() && tickA &&
            moveMode(m) != 2) {
            // Copy the leader's posture and watch outward.
            int p = posture(leader);
            if (carriesDeadBuddy(m) && p < 1) p = 1;
            setPostureAnim(m, p);
            if (mv->fired_until < S.time) mv->aim_heading = s16(evtMemberWatchHeading(heading >> 3, k, t));
        } else if (S.sealTeam != teamIndex && !searching()) {
            evtSetMoveMode(m, moveMode(leader));
            mv->aim_heading = s16(heading >> 3);
            if (mv->target_speed > baseTargetSpeed) mv->target_speed = baseTargetSpeed;
        }

        base = m->body->pos;
        baseTargetSpeed = mv->target_speed;
    }
}

// 2dbd:6371 evt_team_snap_formation: members placed at their slots, offsets
// always from the leader (spawn time; works before the first frame).
void evtTeamSnapFormation(int teamIndex) {
    Team* t = team(teamIndex);
    if (!t || !t->members[0]) return;  // port guard
    Unit* leader = t->members[0];
    const Vec3 lp = leader->body->pos;
    u16 p = u16(kSlotFirst + int(t->formation) * 4);
    for (int k = 1; k < 8 && t->members[k]; ++k, p = u16(p + 0x14)) {
        Unit* m = t->members[k];
        const int heading = leader->body->heading;
        const int scale = exe().dgByte(u16(kFormationScale + teamType(t)));
        s16 ox = s16(s16(scale * exe().dgShort(p)) * 3);
        s16 oy = s16(s16(exe().dgShort(u16(p + 2)) * scale) * 3);
        mathRotate2d(ox, oy, 0, 0, heading);
        m->body->pos.x = wrapAdd(shl8(ox), lp.x);
        m->body->pos.z = wrapAdd(shl8(oy), lp.z);
    }
}

// 2dbd:6487 evt_team_is_engaged.
bool evtTeamIsEngaged(int teamIndex) {
    const Team* t = team(teamIndex);
    if (!t) return false;
    if (t->order == 6) return false;                               // ceasing the attack
    if (t->type != TeamType::Boat && t->order == 4) return false;  // loitering
    for (int k = 0; k < 8 && t->members[k]; ++k)
        if (unitInContact(t->members[k])) return true;
    return false;
}

// 2dbd:651D evt_player_speed_step(dir): +: faster, -: slower, 0: stop.
void evtPlayerSpeedStep(int dir) {
    Unit* p = pointMan();
    if (!p || unitDead(p)) return;
    const int old = moveMode(p);
    int mode = old;
    if (dir > 0) {
        if (old == 3) mode = 0;
        else if (old != 2) mode = old + 1;
    } else if (dir == 0) {
        mode = 0;
    } else {
        if (old == 0 || old == 3) mode = 3;
        else mode = old - 1;
    }
    evtSetMoveMode(p, mode);
    if (old != mode) msgShow(dsTextPtr(u16(kMoveModeNames + 2 * moveMode(p))), 0x80);
    Team* t0 = team(0);
    if (mode == 0 && t0->order == 0) {
        t0->order = 0;
        const int ap = p->anim ? p->anim->posture : 0;
        sprSetAnim(p, ap == 0 ? 0x0B : ap == 1 ? 0x0E : 0x12);
    }
    if (mode != 0) {
        if (t0->order == s16(TeamOrder::Search)) {
            msgShowDs(0x0BCD, 0x100);  // 'Security: "Search aborted."'
            evtSearchEndReport();
        }
        t0->order = s16(TeamOrder::None);
    }
}

// ---------------------------------------------------------------------------
// AI commands (2dbd:6653)
// ---------------------------------------------------------------------------

// 2dbd:6653 evt_execute_ai_commands (every frame).
void evtExecuteAiCommands() {
    MissionState& S = ms();
    for (int ti = 0; ti < kMaxTeams && S.teams[ti]; ++ti) {
        Team* t = S.teams[ti];
        const int type = teamType(t);
        const bool npc = type == 4 || type == 5 || type == 6;
        for (int k = 0; k < 8 && t->members[k]; ++k) {
            Unit* m = t->members[k];
            if (unitDead(m)) continue;
            if (ti == 0 && k == 0) continue;  // the player
            Mover* mv = m->mover;
            Command* cmd = nullptr;
            if (m->brain) cmd = aiMemberCurrentCommand(m);
            if (!S.aiEnabled || !m->brain || !cmd) continue;

            if (npc && cmd->type == CommandType::Heading) {
                mv->desired_heading = cmd->heading;
                continue;
            }

            if (k == 0 && npc && cmd->type == CommandType::MoveMode) {
                // Move mode, group leaders only.
                const int a = cmd->arg;
                if (a == 0) {
                    evtSetMoveMode(m, 0);
                    if (m->anim) m->anim->move_mode = 0;
                    // Enemies halting in contact shout (30 %).
                    if (isEnemyType(type) && (m->brain->flags & brain_flag::kContact) && unitIndexInGroup(m) == 0 &&
                        engine::rng().range(100) < 30)
                        sfxPlay(0x28, 0x200, &m->body->pos, 1, m);
                } else if (a >= 1 && a <= 3) {
                    // Panicking civilians starting to run shout.
                    if (a == 2 && type == 6 && (m->brain->flags & brain_flag::kFleeing) && unitIndexInGroup(m) == 0 &&
                        moveMode(m) != 2)
                        sfxPlay(0x2B, 0x200, &m->body->pos, 1, m);
                    evtSetMoveMode(m, a);
                    if (m->anim) m->anim->move_mode = s16(a);
                }
                continue;
            }

            if (type != 6 && type != 7 && cmd->type == CommandType::Fire) {
                if (!(S.time > S.insertionClearTime)) continue;  // hold fire after insertion
                if (m->anim && m->anim->state == AnimState::Dead) continue;
                const int ci = aiMindBestContact(m->brain);
                if (ci == -1) continue;
                if (!(cmd->arg & 1) && !(cmd->arg & 2)) continue;
                if (!m->loadout || ci < 0 || ci > 2) continue;  // port guard
                WeaponNode* w = (cmd->arg & 1) ? m->loadout->primary : m->loadout->secondary;
                TargetRec& c = m->brain->contacts[ci];
                Unit* tgt = c.target.unit;
                const Vec3* pos = &c.pos;
                int cover = c.cover;
                int range = c.range;
                if (type == 2 || type == 3) {
                    if (cover < 0x50) cover = 0x50;
                    // Attacking aircraft fire at their destination when the contact is
                    // more than 120 degrees off their heading.
                    if (t->order == 5) {
                        const int b = geoBearing(m->body->pos, c.pos);
                        if (evtHeadingDiff(mv->heading << 3, b << 3) > 0x78) {
                            tgt = nullptr;
                            pos = &mv->destination;
                            range = geoDistance(m->body->pos, mv->destination);
                        }
                    }
                }
                if (!w) continue;

                if (type != 0 && type < 4) {
                    // Support craft: in range (boat) or airborne (helicopter, aircraft).
                    if (t->order == 6) continue;
                    bool fire = false;
                    if (type == 1 && range < 0x4B0) fire = true;
                    else if (t->order == 4) continue;
                    else if (type == 1) fire = true;
                    else if ((mv->height_high >> 2) < mv->height) fire = true;
                    if (fire) shotFire(m, tgt, *pos, cover, w, range, 0);
                    continue;
                }
                if (m->team->type != TeamType::Seal) {
                    if (w->rounds == 0) continue;
                    if (!tgt || !tgt->team) continue;  // port guard
                    if (tgt->team->type == TeamType::Helicopter && tgt->team->order == 4) continue;
                    const int tt = teamType(tgt->team);
                    shotFire(m, tgt, *pos, cover, w, range, (tt != 0 && tt < 4) ? 2 : 0);
                    continue;
                }
                const Unit* pm = pointMan();
                if (!pm || m->team != pm->team) {
                    // Split SEAL team.
                    if (t->fire_order == FireOrder::Snipe) continue;
                    if (int(w->type) == kSatchel) continue;
                    if (m->team->fire_order == FireOrder::CeaseFire) continue;
                } else {
                    // The player's squad: not after extraction, not under Cease Fire,
                    // not with satchels, not beyond 90 degrees off its heading.
                    if (S.extracting) continue;
                    if (m->team->fire_order == FireOrder::CeaseFire) continue;
                    if (int(w->type) == kSatchel) continue;
                    const int b = geoBearing(m->body->pos, *pos);
                    if (evtHeadingDiff(mv->heading << 3, b << 3) > 0x5A) continue;
                }
                shotFire(m, tgt, *pos, cover, w, range, 0);
                continue;
            }

            if (npc && cmd->type == CommandType::Posture) {
                const int a = cmd->arg;
                if (a <= 2) setPostureAnim(m, a);
                continue;
            }
            if ((type == 0 || type >= 4) && cmd->type == CommandType::Reload) {
                evtStartReload(m);
                continue;
            }
            if ((type == 0 || type >= 4) && cmd->type == CommandType::Surrender) {
                setPostureAnim(m, 0);
                if (isEnemyType(type)) sfxPlay(0x2A, 0x200, &m->body->pos, 1, m);
            }
        }
        aiGroupCommandsConsumed(t);
    }
}

} // namespace st::game::mission
