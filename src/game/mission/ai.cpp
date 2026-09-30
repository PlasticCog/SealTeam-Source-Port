// Group and soldier AI of segment 4a37 (docs/re/seg_libs.md 14): set-up,
// member command rings, script sequences, orders and patrols, reserve
// deployment, automatic first aid, perception, alerts and combat.
//
// The noise events of segment 1000 are in noise.cpp.
//
// Not ported (no near or far references in st.exe): 4a37:0248
// ai_group_start_script, 4a37:110E ai_tick_reinforcement_timers, 4a37:1242
// ai_group_turn_aside and 4a37:1435 ai_group_check_blocked.
#include "game/mission/ai.h"

#include "data/exeimage.h"
#include "engine/rng.h"
#include "game/mission/combat.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/mission/wquery.h"

#include <cstdlib>

namespace st::game::mission {

namespace {

using namespace brain_flag;

// Reaction delay tables of ai_update (DGROUP, read from st.exe): value
// thresholds A and results B of ai_delay_table_lookup.
constexpr u16 kDelayExpA = 0x46AC, kDelayExpB = 0x46B0;
constexpr u16 kDelayIntA = 0x46B6, kDelayIntB = 0x46BA;
constexpr u16 kDelayObsA = 0x46C0, kDelayObsB = 0x46C4;

int rng(int n) { return engine::rng().range(n); }

bool isPlayer(const Unit* u) { return u && u == pointMan(); }

// Flags of a unit's mind; the Point Man has no mind (the original reads
// through its NULL pointer; the port reads 0).
u8 mindFlags(const Brain* b) { return b ? b->flags : 0; }
u8 unitFlags(const Unit* u) { return u ? mindFlags(u->brain) : 0; }

u16 cmdWord(const Command& c) { return u16(c.arg | (u16(u8(c.type)) << 8)); }

void setCmdWord(Command& c, u16 w) {
    c.arg = u8(w);
    c.type = CommandType(u8(w >> 8));
}

// Low / high word of TeamAi::think_timer: ai_update uses the low word as the
// reaction delay, the group-script code the high word as the step delay.
s16 thinkLo(const TeamAi* ai) { return s16(u32(ai->think_timer) & 0xFFFF); }
s16 thinkHi(const TeamAi* ai) { return s16(u32(ai->think_timer) >> 16); }
void setThinkLo(TeamAi* ai, int v) { ai->think_timer = s32((u32(ai->think_timer) & 0xFFFF0000u) | u16(v)); }
void setThinkHi(TeamAi* ai, int v) { ai->think_timer = s32((u32(ai->think_timer) & 0xFFFFu) | (u32(u16(v)) << 16)); }

// Heading of a unit's body in whole degrees: abs(heading8 >> 3).
int bodyHeadingDeg(const Unit* u) { return std::abs(int(s16(u->body->heading) >> 3)); }

WeaponNode* primaryOf(const Unit* u) { return u->loadout ? u->loadout->primary : nullptr; }
WeaponNode* secondaryOf(const Unit* u) { return u->loadout ? u->loadout->secondary : nullptr; }

// 19ac:70C8 / 70AB return 0 for NULL slots.
int roundsOf(const WeaponNode* w) { return w ? wpnGetRounds(w) : 0; }
int magsOf(const WeaponNode* w) { return w ? wpnGetMags(w) : 0; }

// Live position of a contact's target (the original stores the target's 3D
// object and copies its position with geo_copy_pos 1000:31FD).
Vec3 contactLivePos(const TargetRec& c) { return c.target_pos ? *c.target_pos : c.pos; }

// Member count of the team of a contact's target (ai_list_count).
int contactTeamSize(const TargetRec& c) {
    return (c.target.unit && c.target.unit->team) ? teamMemberCount(c.target.unit->team) : 0;
}

// Aim correction of weapon classes 10, 12, 13, 14 with a blast radius.
bool needsScatter(const WeaponNode* w) {
    const WeaponDef& wd = weaponDef(u8(w->type));
    if (wd.blast_radius <= 0) return false;
    const int c = int(wd.wclass);
    return c == 10 || c == 13 || c == 14 || c == 12;
}

// 4a37:0D3B ai_delay_table_lookup(value, A, B, n).
// Original quirk (4a37:0D3B): the search loop never compares the value with
// the thresholds A; it always stops at i = n and returns the low byte of B[n]
// (with the three tables of ai_update: 30 + 30 + 77).
int delayTableLookup(int value, u16 tableA, u16 tableB, int n) {
    (void)value;
    (void)tableA;
    u8 i = 0;
    while (i < u8(n)) ++i;
    return exe().dgWord(u16(tableB + 2 * i)) & 0xFF;
}

// 4a37:1EFD ai_sight_range_leader_mod: observe skill (status +6).
void sightRangeLeaderMod(const Unit* u, int& range) {
    const int v = u->status->skill[6];
    if (v >= 60) range = s16(range + (v - 15) * 3);
    if (v <= 45) range = s16(range + (v - 55) * 3);
}

// 4a37:1F54 ai_sight_range_stub: empty.
void sightRangeStub(int& range) { (void)range; }

// 4a37:1F55 ai_sight_range_target_mod: big targets are easier to see.
void sightRangeTargetMod(const Status* s, int& range) {
    if (s->size > 75) range = s16(range + 60);
    if (s->size > 90) range = s16(range + 300);
}

// 4a37:1F91 ai_sight_range_prone_mod.
void sightRangeProneMod(const Unit* observer, const Unit* target, int& range) {
    if (target->mover->posture == Posture::Prone && !(unitFlags(observer) & kEngaging)) range = 135;
}

// 4a37:1FD0 ai_watch_sector_angle: rotates the scan angle per member slot
// (the result is not used by ai_group_scan).
void watchSectorAngle(int& angle, int slot, int mode) {
    if (!(mode == 0 || mode == 2 || mode == 3)) return;
    int a;
    if (slot == 1) a = s16(angle + 90);
    else if (slot == 2) a = s16(angle - 90);
    else if (slot == 3) a = s16(angle + 180);
    else return;
    angle = a % 360;
}

// 4a37:210C ai_mind_store_contact.
void storeContact(Brain* b, const Vec3& pos, const Vec3* livePos, int range, u8 score, u8 sighted, int slot,
                  int cover, Unit* target, TargetKind kind) {
    TargetRec& c = b->contacts[slot];
    c.in_use = 1;
    c.target.unit = target;
    c.kind = kind;
    c.pos = pos;
    c.target_pos = livePos;
    c.range = s16(range);
    c.score = score;
    c.sighted = sighted;
    c.cover = s16(cover);
    b->flags |= kContact;
}

// 4a37:2949 ai_mind_borrow_contact(group, mind, member index): copies the
// best contact of the first groupmate that has one into slot 0.
int borrowContact(Team* t, Brain* b, int idx) {
    const Vec3 mpos = t->members[idx]->body->pos;
    for (int j = 0; j < 8 && t->members[j]; ++j) {
        Unit* m = t->members[j];
        if (isPlayer(m) || !m->brain) continue;
        const int k = aiMindBestContact(m->brain);
        if (k < 0) continue;
        const TargetRec& src = m->brain->contacts[k];
        TargetRec& dst = b->contacts[0];
        // Original quirk (4a37:29D3): target, kind and cover of slot 0 are
        // not copied (they keep whatever the slot held before).
        dst.in_use = 1;
        dst.pos = src.pos;
        dst.target_pos = src.target_pos;
        const Vec3 live = contactLivePos(src);
        dst.range = s16(geoDistance(live, mpos));
        dst.sighted = 1;
        dst.score = 25;
        return 0;
    }
    return -1;
}

// 4a37:2237 ai_group_scan(group, angle, mode). `angle` is the scan angle the
// original rotates per member (unused); the mode argument is not read.
bool groupScan(Team* t, int angle) {
    MissionState& S = ms();
    bool res = false;
    bool any = false;
    // "Observer": the leader first, then the last member iterated (quirk).
    Unit* obs;
    if (isPlayer(t->members[0])) {
        if (!t->members[1]) return false;
        obs = t->members[1];
    } else {
        obs = t->members[0];
    }
    Brain* obsBrain = obs->brain;
    TeamAi* ai = t->ai;

    for (int i = 0; i < 8 && t->members[i]; ++i)
        if (!isPlayer(t->members[i])) aiMindExpireContacts(t->members[i]);

    const u8 bestScore = u8(aiMindBestContactScore(obs->brain));
    if (bestScore) any = res = true;

    int range = 495;
    int blind = 25;
    const u8 exp = obs->status->experience;
    if (exp > 40) range = ((exp - 40) / 5 + 165) * 3;
    if (unitFlags(obs) & kAlerted) {
        range += 60;
        blind -= 5;
    }
    sightRangeLeaderMod(obs, range);
    sightRangeStub(range);
    if (range < 90) range = 90;

    for (int gi = 0; gi < kMaxTeams && S.teams[gi]; ++gi) {
        Team* other = S.teams[gi];
        const int tt = int(t->type);
        const int ot = int(other->type);
        bool hostile;
        if (tt > 3 && tt < 6) hostile = ot <= 3 || ot == 7;
        else hostile = ot >= 4 && ot <= 5;
        if (!hostile) {
            if (obs->team->type != TeamType::Civilian) continue;
            if (ot == 4 || ot == 5 || ot == 6) continue;
        }
        for (int j = 0; j < 8 && other->members[j]; ++j) {
            Unit* tg = other->members[j];
            if (unitDead(tg)) continue;
            if (tg->mover->flags & mover_flag::kSecured) continue;
            if (tg->brain && tg->brain->surrendered) continue;
            if (tg->team->type == TeamType::Helicopter && tg->team->order == 4) continue;
            if (tg->brain && !(tg->brain->flags & kActive)) continue;
            // Original quirk (4a37:23FB, 24B3): the tracked-target and range
            // pre-checks use the last member iterated by the observer loop
            // below (the leader only for the first target that passes).
            if (aiMindTracksTarget(obsBrain, tg)) continue;
            int r = range;
            const Vec3 tpos = tg->body->pos;
            const int dist = geoDistance(obs->body->pos, tpos);
            sightRangeTargetMod(tg->status, r);
            sightRangeProneMod(obs, tg, r);
            if (t->fire_order == FireOrder::AtTarget && t->type == TeamType::Seal) r = wpnBestRange(obs);
            if (dist > s16(r)) continue;

            for (int mi = 0; mi < 8 && t->members[mi]; ++mi) {
                Unit* m = t->members[mi];
                if (isPlayer(m)) continue;
                obs = m;
                obsBrain = m->brain;
                watchSectorAngle(angle, mi, int(t->fire_order));
                const Vec3 mpos = m->body->pos;
                const int rear = (bodyHeadingDeg(m) + 180) % 360;
                if (geoInFov(&mpos, blind, rear, &tpos) && t->fire_order != FireOrder::AtTarget && dist >= 90)
                    continue;
                if (u8(aiTargetPriority(m, tg, 0)) <= bestScore) continue;
                const int cover = s16(wldLosCover(m, tpos));
                if (cover >= 100 && t->fire_order != FireOrder::AtTarget) continue;
                if (!(mindFlags(obsBrain) & kAlerted)) aiGroupAlert(t);
                Team* mt = m->team;
                for (int q = 0; q < 8 && mt->members[q]; ++q) {
                    Unit* u = mt->members[q];
                    if (isPlayer(u) || mt->type == TeamType::Civilian || !u->brain) continue;
                    u->brain->flags |= kEngaging;
                }
                res = any = true;
                if (!obsBrain) continue;
                int worst = -1;
                int freeSlot = -1;
                const u8 prio = u8(aiTargetPriority(m, tg, cover));
                u8 minScore = prio;
                for (int k = 0; k < 3; ++k) {
                    if (!obsBrain->contacts[k].in_use) freeSlot = k;
                    if (obsBrain->contacts[k].score < minScore) {
                        minScore = obsBrain->contacts[k].score;
                        worst = k;
                    }
                }
                const int slot = freeSlot != -1 ? freeSlot : worst;
                if (slot != -1)
                    storeContact(obsBrain, tpos, &tg->body->pos, dist, prio, 1, slot, cover, tg, TargetKind::Unit);
                obsBrain->flags |= kEngaging;
                ai->behaviour = ai_behaviour::kHunt;
            }
        }
    }
    if (!res) {
        for (int i = 0; i < 8 && t->members[i]; ++i) {
            Unit* m = t->members[i];
            if (isPlayer(m) || !m->brain) continue;
            m->brain->flags &= u8(~(kEngaging | kContact));
        }
    }
    return any;
}

// Push one command into a member's ring (shared by 4a37:0D65 and 0DEC).
void pushCommand(Brain* b, const Command& c) {
    const int slot = b->cmd_index;
    b->cmd_index = u8(u8(b->cmd_index + 1) % 5);
    b->cmd_pending = 1;
    Command& d = b->commands[slot];
    // +0x03 and +0x11 are not copied.
    d.arg = c.arg;
    d.type = c.type;
    d.flag = c.flag;
    d.pos = c.pos;
    d.ai_issued = c.ai_issued;
    d.heading = c.heading;
}

void memberFlee(Unit* m) {
    aiMemberStartScript(m, 0);
    m->brain->flags |= kFleeing;
}

void memberSurrender(Unit* m, Command& cmd) {
    setCmdWord(cmd, 0x900);
    m->brain->surrendered = 1;
    pushCommand(m->brain, cmd);
}

void memberReload(Unit* m, Command& cmd) {
    setCmdWord(cmd, 0x500);
    wpnReloadBoth(m);
    pushCommand(m->brain, cmd);
}

enum class GroupResult { Skip, Decided };

// One group of ai_update (4a37:176E..1E0D). `cmd` is ai_update's command
// local (kept across groups like the original stack frame).
GroupResult updateGroup(int gi, int dt, Command& cmd) {
    MissionState& S = ms();
    Team* t = S.teams[gi];
    TeamAi* ai = t->ai;
    if (!ai) return GroupResult::Skip;
    Unit* leader;
    if (gi == 0) {
        if (teamMemberCount(t) == 1) return GroupResult::Skip;
        leader = t->members[1];
    } else {
        leader = t->members[0];
    }
    Brain* b = leader->brain;
    if (!b) return GroupResult::Skip;
    const Vec3 lpos = leader->body->pos;

    // 1. Reserves.
    if (t->type != TeamType::Seal) {
        if (!S.hasSpecialObject) {
            if (aiGroupReinforcementTick(t, dt)) return GroupResult::Skip;
        } else if (!(b->flags & kActive)) {
            return GroupResult::Skip;
        }
    }
    // 2. Suppression, first aid.
    aiGroupTickSuppression(t, dt);
    if (t->type == TeamType::Seal) aiSquadAutoFirstAid(t);
    // 3. Captives are released when the player's squad is gone or far away.
    if (t->type != TeamType::Friendly && b->surrendered) {
        Unit* pm = pointMan();
        const Vec3 ppos = pm->body->pos;
        if (grpCountAlive(pm->team) <= 0 || geoDistance(ppos, lpos) >= 225) {
            for (int i = 0; i < 8 && t->members[i]; ++i) {
                Unit* m = t->members[i];
                if (!(m->mover->flags & mover_flag::kSecured) && m->brain) m->brain->surrendered = 0;
            }
        }
    }
    // 4. Perception.
    const bool seen = groupScan(t, cmd.heading);
    if (seen && t->type == TeamType::Civilian) {
        for (int i = 0; i < 8 && t->members[i]; ++i) {
            Unit* m = t->members[i];
            if (!m->brain || (m->brain->flags & kScript)) continue;
            aiMemberStartScript(m, 0);
            m->brain->flags |= kFleeing;
        }
    }
    if (seen && !(b->flags & kAlerted)) {
        aiGroupAlert(t);
        aiGroupCombat(t);
        return GroupResult::Skip;
    }
    if (!seen && (b->flags & kEngaging)) {
        for (int i = 0; i < 8 && t->members[i]; ++i) {
            Unit* m = t->members[i];
            if (isPlayer(m) || !m->brain) continue;
            m->brain->flags &= u8(~kEngaging);
            m->brain->flags &= u8(~kAlerted);
        }
    }
    // 5. Scripts.
    if (t->type != TeamType::Seal && aiGroupRunScripts(t, dt)) return GroupResult::Skip;
    // 6. Noise.
    Vec3 npos{};
    if (noiseHear(leader, npos) && !(b->flags & kEngaging) && !(b->flags & kFleeing) && !(b->flags & kContact)) {
        aiGroupAlert(t);
        if (geoDistance(lpos, npos) <= 10) {
            setCmdWord(cmd, 0x800);
            cmd.heading = s16(geoBearing(lpos, npos));
            groupScan(t, cmd.heading);
            aiGroupBroadcastCommand(t, cmd);
            return GroupResult::Decided;
        }
        if (t->type != TeamType::Seal) {
            bool go;
            if (!aiGroupHasMoveOrders(t)) {
                go = ai->behaviour == ai_behaviour::kHunt;
                if (go) ai->order_type = AiOrderType::Reserve;
            } else {
                const int dn = geoDistance(lpos, npos);
                const int dd = geoDistance(lpos, S.teams[gi]->ai->destination);
                go = dd > dn;
            }
            if (go) {
                aiGroupSetDestination(t, npos);
                setCmdWord(cmd, 0x100);
                aiGroupBroadcastCommand(t, cmd);
                return GroupResult::Decided;
            }
        }
    }
    // 7. Reaction delay.
    if (thinkLo(ai) != 0) {
        s16 v = thinkLo(ai);
        if (v > s16(dt)) v = s16(v - dt);
        else v = 0;
        setThinkLo(ai, v);
        aiGroupClearPending(t);
        return GroupResult::Skip;
    }
    // 8. Every 5th decision while travelling: look around.
    if (ai->look_counter == 0) {
        if (!seen && !aiGroupAtDestination(t)) {
            setCmdWord(cmd, 0x800);
            cmd.heading = s16(bodyHeadingDeg(leader));
            aiGroupBroadcastCommand(t, cmd);
            ai->look_counter = 4;
            return GroupResult::Decided;
        }
        ai->look_counter = 1;
    } else {
        ai->look_counter--;
    }
    // 9. Combat.
    const int ty = int(t->type);
    bool combat = false;
    if ((b->flags & kEngaging) || (ty != 0 && ty < 4 && t->order == 5)) {
        if (ty == 0 || ty >= 4) combat = true;
        else if (t->order != 6) combat = true;
    }
    if (combat) {
        aiGroupCombat(t);
        return GroupResult::Decided;
    }
    // 10. Movement.
    if (aiGroupHasMoveOrders(t)) {
        if (leader->mover->move_mode == MoveMode::Stop) {
            // The original reads the command word through the NULL returned
            // for a suppressed leader; the port treats it as "no turn".
            const Command* last = aiMemberLastCommand(leader);
            if (last && cmdWord(*last) == 0x200) {
                setCmdWord(cmd, (b->flags & kAlerted) ? 0x102 : 0x101);
            } else {
                const int brg = geoBearing(lpos, ai->destination);
                setCmdWord(cmd, 0x200);
                cmd.heading = s16(brg);
                cmd.ai_issued = 1;
            }
            aiGroupBroadcastCommand(t, cmd);
        } else if (aiGroupAtDestination(t)) {
            aiGroupNextDestination(t);
            setCmdWord(cmd, 0x100);
            aiGroupBroadcastCommand(t, cmd);
        }
    }
    return GroupResult::Decided;
}

// 11. Reaction delay after a decision (4a37:1D24).
void finishDecision(int gi, Command& cmd) {
    Team* t = ms().teams[gi];
    TeamAi* ai = t->ai;
    Unit* leader = gi == 0 ? t->members[1] : t->members[0];
    setThinkLo(ai, 0x180);
    // Never true: ai_update itself never issues 0x500.
    if (cmdWord(cmd) == 0x500) {
        if (const WeaponNode* w = primaryOf(leader)) setThinkLo(ai, weaponDef(u8(w->type)).reload_ticks);
    }
    if (ai->blocked) setThinkLo(ai, 0x400);
    const Status* s = leader->status;
    setThinkLo(ai, thinkLo(ai) + delayTableLookup(s->experience, kDelayExpA, kDelayExpB, 3));
    setThinkLo(ai, thinkLo(ai) + delayTableLookup(s->intelligence, kDelayIntA, kDelayIntB, 3));
    setThinkLo(ai, thinkLo(ai) + delayTableLookup(s->skill[6], kDelayObsA, kDelayObsB, 3));
    if (unitFlags(leader) & kEngaging) setThinkLo(ai, 0);
    if (gi == 0) setThinkLo(ai, 0);
}

} // namespace

void aiReset() {
}

// ---------------------------------------------------------------------------
// Set-up
// ---------------------------------------------------------------------------

void aiInit() {
    for (s16& p : ms().medicTarget) p = -1;
    aiBuildScriptSequences();
    aiResetShotSlots();
    noiseReset();
}

void aiResetShotSlots() {
    for (ShotRec& s : ms().shots) s.state = 1;
}

void aiBuildScriptSequences() {
    MissionState& S = ms();
    // Sequence 0 (flee): stand up, turn to 180, run.
    ScriptStep* h = S.scriptPool.alloc();
    setCmdWord(h->cmd, 0x300);
    h->delay = 0x100;
    ScriptStep* n = S.scriptPool.alloc();
    setCmdWord(n->cmd, 0x200);
    n->cmd.heading = 0xB4;
    n->cmd.ai_issued = 0;
    n->delay = 0x100;
    h->next = n;
    ScriptStep* n2 = S.scriptPool.alloc();
    n->next = n2;
    setCmdWord(n2->cmd, 0x102);
    n2->delay = s16(0xFF00);
    n2->next = nullptr;
    S.scriptHeads[0] = h;
    // Sequence 1: stop, kneel.
    h = S.scriptPool.alloc();
    setCmdWord(h->cmd, 0x100);
    h->delay = 0x100;
    n = S.scriptPool.alloc();
    h->next = n;
    setCmdWord(n->cmd, 0x301);
    n->next = nullptr;
    n->delay = 0x100;
    S.scriptHeads[1] = h;
    // Sequence 2: stop, go prone.
    h = S.scriptPool.alloc();
    setCmdWord(h->cmd, 0x100);
    h->cmd.ai_issued = 0;
    h->delay = 0x100;
    n = S.scriptPool.alloc();
    h->next = n;
    setCmdWord(n->cmd, 0x302);
    n->delay = 0x100;
    n->next = nullptr;
    S.scriptHeads[2] = h;
}

void aiShutdown() {
    // The script nodes live in ms().scriptPool, freed with the mission state.
}

void aiMemberStartScript(Unit* u, int idx) {
    Brain* b = u ? u->brain : nullptr;
    const int i = idx & 0xFF;
    if (!b || i > 2) return;
    ScriptStep* head = ms().scriptHeads[i];
    b->flags |= kScript;
    b->script = head;
    b->script_delay = head ? head->delay : 0;
}

void aiMindReset(Brain* b) {
    if (!b) return;
    for (Command& c : b->commands) {
        c.arg = 0;
        c.type = CommandType::None;
    }
    for (TargetRec& c : b->contacts) c.in_use = 0;
    b->team = nullptr;
    b->cmd_index = 0;
    b->flags = 0;
    b->cmd_pending = 0;
    b->surrendered = 0;
    b->script = nullptr;
    b->script_delay = 0;
    b->suppress_time = 0;
}

void aiGroupOrdersReset(Team* t) {
    TeamAi* ai = t ? t->ai : nullptr;
    if (!ai) return;
    ai->look_counter = 0;
    ai->patrol_dir = 1;
    ai->cur_waypoint = nullptr;
    ai->waypoints = nullptr;
    ai->destination = Vec3{0, 0, 0};
    ai->deploy_timer = 0;
    ai->script = nullptr;
    ai->script_active = 0;
    ai->cmd_pending = 0;
    ai->unk_80 = 0;
    ai->blocked = 0;
    ai->think_timer = 0;
    for (Command& c : ai->commands) {
        c.arg = 0;
        c.type = CommandType::None;
    }
    ai->cmd_index = 0;
    ai->order_type = AiOrderType::None;
    ai->behaviour = 0;
}

void aiGroupOrdersFromMission(Team* t, const MtmTeam* rec, int index, int first) {
    if (!t || !t->ai) return;
    TeamAi* ai = t->ai;
    aiGroupOrdersReset(t);
    // Groups created after the last MTM record read an unused (zero) record.
    static const MtmTeam kZero{};
    if (!rec) rec = &kZero;
    ai->behaviour = rec->behaviour;
    const s16 slot = s16(s16(index - first) % 5);
    ai->think_timer = s32(u32(u16(u16(slot * 0x180) + 0x100)));
    ai->look_counter = u8(6 - slot);
    ai->order_type = rec->order_type;
    if (rec->order_type == AiOrderType::Patrol) {
        ai->patrol_dir = 1;
        ai->destination.x = rec->waypoints[0].x;
        ai->destination.y = 0;
        ai->destination.z = rec->waypoints[0].z;
        for (int i = 0; i < 5; ++i) {
            const Vec3& w = rec->waypoints[i];
            if (w.x == 0 && w.z == 0) continue;
            // y is an uninitialised stack word in the original.
            entAddWaypoint(t, Vec3{w.x, 0, w.z});
        }
    } else {
        ai->destination = Vec3{0, 0, 0};
    }
    if (rec->order_type != AiOrderType::Reserve) return;
    // Deployment delay (switch at 4a37:0570, jump table in st.exe).
    s32 v;
    switch (rec->deploy_delay) {
    case 1: v = rng(10); break;
    case 2: v = rng(20) + 10; break;
    case 3: v = rng(30) + 30; break;
    case 4: v = rng(60) + 60; break;
    case 5: v = rng(120) + 120; break;
    case 6: v = rng(240) + 240; break;
    case 7: v = rng(240) + 480; break;
    case 8: v = rng(240) + 720; break;
    case 9: v = rng(240) + 960; break;
    default: {
        // Original quirk (4a37:0582): other codes compute AH = rand(240) - 0x40
        // without the << 8 of the long shift (sign-extended 16-bit value).
        const u16 w = u16(u8(rng(0xF0) - 0x40) << 8);
        ai->deploy_timer = s32(s16(w));
        return;
    }
    }
    ai->deploy_timer = s32(u32(s32(s16(v))) << 8);
}

void aiMindAttach(Unit* u, Team* t) {
    if (!u || !u->brain) return;
    aiMindReset(u->brain);
    u->brain->team = t;
    const bool reserve = t && t->ai && t->ai->order_type == AiOrderType::Reserve;
    u->brain->flags = reserve ? 0 : kActive;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

Command* aiMemberCurrentCommand(Unit* u) {
    Brain* b = u ? u->brain : nullptr;
    if (!b) return nullptr;
    if (b->cmd_pending) return &b->commands[(b->cmd_index + 4) % 5];
    if (!(b->flags & kSuppressed)) {
        TeamAi* ai = b->team ? b->team->ai : nullptr;
        if (ai && ai->cmd_pending) return &ai->commands[(ai->cmd_index + 4) % 5];
    }
    return nullptr;
}

Command* aiMemberLastCommand(Unit* u) {
    Brain* b = u ? u->brain : nullptr;
    if (!b) return nullptr;
    if (b->flags & kSuppressed) return nullptr;
    return &b->commands[(b->cmd_index + 4) % 5];
}

void aiGroupCommandsConsumed(Team* t) {
    if (!t) return;
    if (t->ai) t->ai->cmd_pending = 0;
    for (int i = 0; i < 8 && t->members[i]; ++i)
        if (Brain* b = t->members[i]->brain) b->cmd_pending = 0;
}

void aiMindPushCommand(Brain* b, const Command& c) {
    if (!b) return;
    pushCommand(b, c);
}

void aiGroupBroadcastCommand(Team* t, const Command& c) {
    for (int i = 0; i < 8 && t->members[i]; ++i) {
        Unit* m = t->members[i];
        if (isPlayer(m)) continue;
        Brain* b = m->brain;
        if (!b || (b->flags & kScript)) continue;
        pushCommand(b, c);
    }
}

void aiGroupClearPending(Team* t) {
    for (int i = 0; i < 8 && t->members[i]; ++i) {
        Unit* m = t->members[i];
        if (isPlayer(m)) continue;
        Brain* b = m->brain;
        if (!b || (b->flags & kScript)) continue;
        b->cmd_pending = 0;
    }
    if (!t->ai->script_active) t->ai->cmd_pending = 0;
}

// ---------------------------------------------------------------------------
// Orders and movement
// ---------------------------------------------------------------------------

void aiGroupSetDestination(Team* t, const Vec3& pos) {
    t->ai->destination = pos;
}

int aiFindReserveGroup() {
    MissionState& S = ms();
    for (int i = S.firstMtmGroup; i < kMaxTeams && S.teams[i]; ++i) {
        const Team* g = S.teams[i];
        if (unitFlags(g->members[0]) & kActive) continue;
        if (g->type == TeamType::VietCong || g->type == TeamType::NvArmy) return i;
    }
    return -1;
}

bool aiGroupAtDestination(const Team* t) {
    const Unit* leader = t->members[0];
    const Vec3 pos = leader->body->pos;
    const int limit = leader->mover->move_mode == MoveMode::Run ? 150 : 30;
    return geoDistance(pos, t->ai->destination) <= limit;
}

bool aiGroupHasMoveOrders(const Team* t) {
    // For the player's squad the leader slot is the Point Man, who has no mind
    // (the original reads the flags through NULL; the port reads 0).
    const bool alerted = unitFlags(t->members[0]) & kAlerted;
    if (alerted && t->ai->behaviour != ai_behaviour::kGuard) return true;
    if (t->ai->order_type == AiOrderType::Patrol) return true;
    if (t->ai->order_type == AiOrderType::Reserve) return true;
    return false;
}

// 4a37:0923 ai_group_next_destination(group, out): out is always the
// group's own destination.
void aiGroupNextDestination(Team* t) {
    TeamAi* ai = t->ai;
    Unit* leader = t->members[0];
    Brain* b = leader->brain;
    Vec3& out = ai->destination;
    WaypointNode* cur = ai->cur_waypoint;
    const Vec3 lpos = leader->body->pos;

    if (mindFlags(b) & kAlerted) {
        if (aiMindHasContacts(b)) {
            if (b->flags & kEngaging) return;
            const int k = aiMindBestContact(b);
            // The original indexes slot -1 when every contact's target is
            // dead (garbage position); the port keeps the destination.
            if (k < 0) return;
            const Vec3 cpos = contactLivePos(b->contacts[k]);
            geoMidpoint(lpos, cpos, out);
            return;
        }
        aiGroupClearAlert(t);
        WorldObject* obj = nullptr;
        if (ai->behaviour == ai_behaviour::kGuard && wldFindMissionPoint(leader, obj) && obj) {
            const Vec3 opos = obj->body->pos;
            if (geoDistance(lpos, opos) < 50) {
                geoOffsetXz(opos, 50, out);
                return;
            }
        }
        if (wldFindLiveMissionPoint(leader, obj) && obj) {
            // New square patrol around the objective.
            ai->order_type = AiOrderType::Patrol;
            const int d = s16(rng(6) * 300);
            const Vec3 opos = obj->body->pos;
            geoOffsetXz(opos, d, out);
            entClearWaypoints(t);
            entAddWaypoint(t, out);
            // y is an uninitialised stack word in the original.
            Vec3 w{out.x, out.y, out.z};
            const s32 dd = s32(u32(s32(s16(d))) << 8);
            w.z = wrapSub(w.z, dd);
            entAddWaypoint(t, w);
            w.x = wrapSub(w.x, dd);
            entAddWaypoint(t, w);
            w.z = out.z;
            entAddWaypoint(t, w);
            entAddWaypoint(t, out);
            ai->patrol_dir = 1;
            return;
        }
        geoRandomOffsetDry(out, 600, 900);
        return;
    }

    // Ping-pong along the patrol waypoints. The original reads through NULL
    // when there is no next/previous node; the port leaves the destination.
    if (!cur) return;
    if (ai->patrol_dir != 0) {
        if (cur->next) {
            ai->cur_waypoint = cur->next;
            out = cur->next->pos;
        } else {
            ai->cur_waypoint = cur->prev;
            if (cur->prev) out = cur->prev->pos;
            ai->patrol_dir = 0;
        }
    } else {
        if (cur->prev) {
            ai->cur_waypoint = cur->prev;
            out = cur->prev->pos;
        } else {
            ai->cur_waypoint = cur->next;
            if (cur->next) out = cur->next->pos;
            ai->patrol_dir = 1;
        }
    }
}

// 4a37:0F38 ai_group_deploy(ai, leader, mind, group, index, pos).
void aiGroupDeploy(Team* t, const Vec3* pos) {
    if (!t || !t->ai) return;
    TeamAi* ai = t->ai;
    const int index = grpIndex(t);
    Vec3 a{};
    Vec3 b{};
    if (!pos) {
        a = pointMan()->body->pos;
        b = a;
        geoRandomOffsetDry(a, 720, 1200);
    } else {
        a = *pos;
        b = *pos;
    }
    t->members[0]->body->pos = a;
    evtTeamSnapFormation(index);
    // Only the list head is cleared; cur_waypoint is reset by the first add.
    ai->waypoints = nullptr;
    ai->patrol_dir = s8(-1);
    a = b;
    geoRandomOffsetDry(b, 120, 600);
    aiGroupSetDestination(t, b);
    b = a;
    geoRandomOffsetDry(b, 120, 600);
    entAddWaypoint(t, b);
    b = a;
    geoRandomOffsetDry(b, 120, 600);
    entAddWaypoint(t, b);
    aiGroupAlert(t);
    for (int i = 0; i < 8 && t->members[i]; ++i) {
        Unit* m = t->members[i];
        if (m->brain) m->brain->flags = kActive | kAlerted;
        unitShow(m);
    }
    ai->behaviour = ai_behaviour::kHunt;
}

// 4a37:1156 ai_group_reinforcement_tick(group, dt, index).
bool aiGroupReinforcementTick(Team* t, int dt) {
    TeamAi* ai = t->ai;
    Unit* leader = t->members[0];
    if (isPlayer(leader)) return false;
    if (unitFlags(leader) & kActive) return false;
    if (!unitDead(leader)) {
        const s32 d = s32(s16(dt));
        if (ai->deploy_timer >= d) ai->deploy_timer = wrapSub(ai->deploy_timer, d);
        else ai->deploy_timer = 0;
        if (ai->deploy_timer <= 0) {
            aiGroupDeploy(t, nullptr);
            return false;
        }
    }
    aiGroupClearPending(t);
    return true;
}

void aiSquadAutoFirstAid(Team* t) {
    MissionState& S = ms();
    u8 level[8] = {};
    for (int i = 0; i < 8 && t->members[i]; ++i) {
        Unit* m = t->members[i];
        if (isPlayer(m) || unitDead(m)) continue;
        if (invCountItems(m, int(ItemType::MedicalKit)) <= 0) continue;
        int best = -1;
        int bestLevel = 0;
        for (int j = 0; j < 8 && t->members[j]; ++j) {
            Unit* q = t->members[j];
            if (isPlayer(q)) continue;
            level[j] = u8(medStatus(q->status));
            bool listed = false;
            for (int k = 0; k < 4; ++k)
                if (S.medicTarget[k] == j) listed = true;
            if (level[j] >= 2 && int(level[j]) > bestLevel && !listed) {
                bestLevel = level[j];
                best = j;
            }
        }
        if (best < 0) continue;
        medTreat(m, t->members[best]);
        // The original writes 525B:0000[i] for any member slot i (slots 4..7
        // would run past the 4 entries); the port keeps it within the array.
        if (t == S.teams[0] && i < 4) S.medicTarget[i] = s16(best);
    }
}

bool aiGroupRunScripts(Team* t, int dt) {
    TeamAi* ai = t->ai;
    if (ai->script_active) {
        // Group script: only started by the unreferenced 4a37:0248, so never
        // active. Its delay is the high word of the think timer.
        s16 d = thinkHi(ai);
        if (d > s16(dt)) d = s16(d - dt);
        else d = 0;
        setThinkHi(ai, d);
        if (d != 0) {
            aiGroupClearPending(t);
            return true;
        }
        ai->script = ai->script ? ai->script->next : nullptr;
        if (!ai->script) {
            for (int i = 0; i < 8 && t->members[i]; ++i)
                if (Brain* b = t->members[i]->brain) b->flags &= u8(~kScript);
            return true;
        }
        // The original takes the command word from an uninitialised mind
        // pointer; the port uses the step's own command.
        aiGroupBroadcastCommand(t, ai->script->cmd);
        ai->script = ai->script->next;
        setThinkHi(ai, ai->script ? ai->script->delay : 0);
        return true;
    }
    // Only the first scripted member advances per tick.
    for (int i = 0; i < 8 && t->members[i]; ++i) {
        Brain* b = t->members[i]->brain;
        if (!b || !(b->flags & kScript)) continue;
        if (b->script_delay > s16(dt)) b->script_delay = s16(b->script_delay - dt);
        else b->script_delay = 0;
        if (b->script_delay != 0) {
            aiGroupClearPending(t);
            return true;
        }
        if (!b->script) {
            b->flags &= u8(~kScript);
            return true;
        }
        pushCommand(b, b->script->cmd);
        b->script = b->script->next;
        // Original quirk (4a37:1734): at the end of a chain the delay is read
        // through the NULL pointer (a BIOS vector word, negative in practice);
        // 0 gives the same result: the script ends on the next tick.
        b->script_delay = b->script ? b->script->delay : 0;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Main tick
// ---------------------------------------------------------------------------

void aiUpdate(int dt) {
    MissionState& S = ms();
    // ai_update's command local (bp-0x18): kept across the groups; its
    // position/flag bytes are never written (stack contents in the original).
    Command cmd{};
    for (int gi = 0; gi < kMaxTeams && S.teams[gi]; ++gi)
        if (updateGroup(gi, dt, cmd) == GroupResult::Decided) finishDecision(gi, cmd);
}

// ---------------------------------------------------------------------------
// Alerts, contacts, perception
// ---------------------------------------------------------------------------

void aiGroupClearAlert(Team* t) {
    for (int i = 0; i < 8 && t->members[i]; ++i) {
        Unit* m = t->members[i];
        if (isPlayer(m) || !m->brain) continue;
        m->brain->flags &= u8(~kAlerted);
    }
}

void aiGroupAlert(Team* t) {
    for (int i = 0; i < 8 && t->members[i]; ++i) {
        Unit* m = t->members[i];
        if (isPlayer(m) || !m->brain) continue;
        m->brain->flags |= kAlerted;
        if (!t->ai->blocked) setThinkLo(t->ai, 0);
    }
}

void aiMemberDeactivate(Unit* u) {
    if (!u || isPlayer(u) || !u->brain) return;
    u->brain->flags &= u8(~kActive);
}

void aiMindExpireContacts(Unit* u) {
    Brain* b = u ? u->brain : nullptr;
    if (!b) return;
    for (TargetRec& c : b->contacts) {
        if (!c.in_use) continue;
        const Unit* tg = c.target.unit;
        // A slot that never held a target (borrowed contact) is read through
        // NULL in the original; its garbage position is dropped there.
        if (!tg) {
            c.in_use = 0;
            continue;
        }
        bool drop = false;
        if (unitDead(tg)) drop = true;
        if (tg->brain && tg->brain->surrendered) drop = true;
        if ((unitFlags(tg) & kSuppressed) && b->team && b->team->fire_order == FireOrder::CoverFire) drop = true;
        const Vec3 tpos = tg->body->pos;
        if (geoDistance(tpos, c.pos) > 150) drop = true;
        if (drop) c.in_use = 0;
    }
}

bool aiMindTracksTarget(const Brain* b, const Unit* target) {
    if (!b) return false;
    // Original: tests the target field of all three slots, used or not.
    for (const TargetRec& c : b->contacts)
        if (c.target.unit == target) return true;
    return false;
}

int aiMindBestContactScore(const Brain* b) {
    u8 best = 0;
    if (!b) return 0;
    for (const TargetRec& c : b->contacts)
        if (c.in_use && c.score > best) best = c.score;
    return best;
}

bool aiGroupScan(Team* t) {
    return groupScan(t, 0);
}

bool aiMindHasContacts(const Brain* b) {
    if (!b) return false;
    for (const TargetRec& c : b->contacts)
        if (c.in_use) return true;
    return false;
}

int aiMindBestContact(const Brain* b) {
    int out = -1;
    if (!b) return out;
    u8 best = 0;
    for (int i = 0; i < 3; ++i) {
        const TargetRec& c = b->contacts[i];
        if (!c.in_use || c.score < best) continue;
        if (c.kind == TargetKind::Unit && unitDead(c.target.unit)) continue;
        best = c.score;
        out = i;
    }
    return out;
}

int aiMindBorrowContact(Unit* u) {
    if (!u || !u->brain || !u->team) return -1;
    const int idx = unitIndexInGroup(u);
    if (idx < 0) return -1;
    return borrowContact(u->team, u->brain, idx);
}

int aiMemberWeaponsInRange(const Unit* u, int dist) {
    const WeaponNode* p = primaryOf(u);
    const WeaponNode* s = secondaryOf(u);
    const bool inP = p && wpnInRange(dist, p);
    const bool inS = s && wpnInRange(s16(u16(dist) << 2), s);
    if (inP && inS) return 3;
    if (inP && !inS) return 1;
    if (!inP && inS) return 2;
    return 0;
}

Obj3D* aiNearestEnemyLeader(const Vec3& pos) {
    MissionState& S = ms();
    u16 best = 0xFFFF;
    int found = 0;
    for (int i = S.firstMtmGroup; i < kMaxTeams && S.teams[i]; ++i) {
        const Team* g = S.teams[i];
        if (g->type == TeamType::Civilian) continue;
        const Vec3 lp = g->members[0]->body->pos;
        const u16 d = u16(geoDistance(pos, lp));
        if (d < best) {
            best = d;
            found = i;
        }
    }
    if (found > 0) return S.teams[found]->members[0]->body;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------

void aiGroupSupportFire(Team* t) {
    Unit* pm = pointMan();
    // Locals of the original frame kept across members: the command (bp-0x1a)
    // and the chosen weapon (bp-4) are not reset per member.
    Command cmd{};
    WeaponNode* w = nullptr;
    for (int mi = 0; mi < 8 && t->members[mi]; ++mi) {
        Unit* m = t->members[mi];
        Brain* b = m->brain;
        if (!b || !m->loadout) continue;
        const Vec3 mpos = m->body->pos;
        // 19ac:2C6D grp_get_leader_pos: the leader's movement destination.
        Vec3 aim = t->members[0]->mover->destination;
        aim.y = 0;
        if (std::abs(geoDistance(mpos, aim)) > 2400) continue;
        int ci = aiMindBestContact(b);
        if (ci == -1) {
            ci = borrowContact(t, b, mi);
            if (ci == -1) aiGroupClearAlert(t);
        }
        // Without a contact the original reads slot -1 (garbage position);
        // the port uses the aim point.
        const Vec3 tpos = ci >= 0 ? contactLivePos(b->contacts[ci]) : aim;
        const int mask = aiMemberWeaponsInRange(m, geoDistance(mpos, tpos));
        const int pR = roundsOf(primaryOf(m));
        const int sR = roundsOf(secondaryOf(m));
        const int pM = magsOf(primaryOf(m));
        const int sM = magsOf(secondaryOf(m));
        if (pR == 0 && sR == 0) {
            if (pM != 0 || sM != 0) memberReload(m, cmd);
            continue;
        }
        if (pR == 0 && mask == 1) {
            if (pM != 0) memberReload(m, cmd);
            continue;
        }
        if (sR == 0 && mask == 2) {
            if (sM != 0) memberReload(m, cmd);
            continue;
        }
        if (pR > 0) {
            setCmdWord(cmd, 1);
            w = primaryOf(m);
        }
        if (mask == 3 && secondaryOf(m) && sR > 0) {
            int pct = 30;
            if (ci >= 0 && contactTeamSize(b->contacts[ci]) >= 3) pct = 65;
            if (rng(100) <= pct) {
                setCmdWord(cmd, 2);
                w = secondaryOf(m);
            }
        }
        if (!w) continue;
        if (ci == -1) {
            if (rng(100) >= 50) return;
            // Synthetic contact at the aim point.
            ci = 0;
            TargetRec& c = b->contacts[0];
            c.pos = aim;
            c.in_use = 1;
            c.kind = TargetKind::Unit;
            c.target.unit = pm;
            c.sighted = 0;
            c.score = 50;
            c.cover = s16(wldLosCover(m, aim));
            Obj3D* lead = aiNearestEnemyLeader(aim);
            c.target_pos = lead ? &lead->pos : nullptr;
            c.range = s16(lead ? geoDistance(mpos, aim) : 2400);
        }
        TargetRec& c = b->contacts[ci];
        if (needsScatter(w)) {
            Vec3 p = tpos;
            shotScatter(p, m);
            c.pos = p;
        }
        cmd.type = CommandType(u8(cmd.type) | 4);
        cmd.pos = c.pos;
        cmd.flag = 1;
        b->flags |= kContact;
        pushCommand(b, cmd);
    }
}

void aiGroupCombat(Team* t) {
    const int ty = int(t->type);
    if (ty >= 1 && ty <= 3) {
        aiGroupSupportFire(t);
        return;
    }
    MissionState& S = ms();
    Unit* pm = pointMan();
    // Locals of the original frame kept across members: the command (bp-0x2e)
    // and the chosen weapon (bp-6) are not reset per member.
    Command cmd{};
    WeaponNode* w = nullptr;
    for (int mi = 0; mi < 8 && t->members[mi]; ++mi) {
        Unit* m = t->members[mi];
        if (isPlayer(m)) continue;
        Brain* b = m->brain;
        if (!b) continue;
        if (t->type == TeamType::Civilian) {
            aiMemberStartScript(m, 0);
            continue;
        }
        if (m->mover->flags & mover_flag::kSecured) continue;
        if (unitDead(m) || b->surrendered) continue;
        if (!primaryOf(m) && !secondaryOf(m)) continue;
        const Vec3 mpos = m->body->pos;
        const Vec3 ppos = pm->body->pos;
        // After a reload: 50 % go prone.
        const Command* last = aiMemberLastCommand(m);
        if (last && cmdWord(*last) == 0x500 && rng(100) < 50) {
            aiMemberStartScript(m, 2);
            continue;
        }
        // Surrender / flee rolls of enemy soldiers near the player.
        if (ty == 4 || ty == 5) {
            const int d = geoDistance(mpos, ppos);
            if (d < 180) {
                int chance = d < 150 ? 30 : 20;
                const int extra = teamMemberCount(S.teams[0]) - teamMemberCount(t);
                if (extra > 0) chance += 5 * extra;
                if (S.hasSpecialObject && t->ai->order_type == AiOrderType::Reserve) chance = 10;
                else if (d < 90) chance = 100;
                if (rng(100) < chance) {
                    const int r = rng(100);
                    if (r < 60) {
                        memberSurrender(m, cmd);
                        continue;
                    }
                    if (r < 85) {
                        memberFlee(m);
                        continue;
                    }
                }
            }
        }
        // Standing enemy soldiers take cover.
        if ((ty == 4 || ty == 5) && m->mover->posture != Posture::Prone && m->mover->posture != Posture::Crouch &&
            !(b->flags & kFleeing)) {
            aiMemberStartScript(m, rng(100) < 50 ? 1 : 2);
            continue;
        }
        int ci = aiMindBestContact(b);
        if (ci == -1) {
            ci = borrowContact(t, b, mi);
            if (ci == -1) {
                aiGroupClearAlert(t);
                return;
            }
        }
        // "Fire at Target": SEALs only engage the player's reticle target.
        if (ty == 0 && t->fire_order == FireOrder::AtTarget &&
            b->contacts[ci].target.unit != S.playerTarget.target.unit)
            continue;
        const Vec3 cpos = contactLivePos(b->contacts[ci]);
        const int bearing = geoBearing(mpos, cpos);
        if (ty != 0 && std::abs(bearing - bodyHeadingDeg(m)) > 15) {
            setCmdWord(cmd, 0x200);
            cmd.heading = s16(bearing);
            cmd.ai_issued = 1;
            pushCommand(b, cmd);
            continue;
        }
        const int dist = geoDistance(mpos, cpos);
        const int mask = aiMemberWeaponsInRange(m, dist);
        if (mask == 0) {
            if (!wpnHasRange(m, dist)) {
                aiMemberStartScript(m, 2);
                continue;
            }
            wpnSelectForRange(m, dist);
        }
        // Jam clearing.
        const int jam = (m->status->experience / 3) * 15 + 25;
        if (WeaponNode* p = primaryOf(m); p && p->jammed && rng(100) < jam) p->jammed = 0;
        if (WeaponNode* s = secondaryOf(m); s && s->jammed && rng(100) < jam) s->jammed = 0;
        const int pR = roundsOf(primaryOf(m));
        const int sR = roundsOf(secondaryOf(m));
        const int pM = magsOf(primaryOf(m));
        const int sM = magsOf(secondaryOf(m));
        if (pR == 0 && sR == 0) {
            if (pM != 0 || sM != 0) {
                memberReload(m, cmd);
                continue;
            }
            if (!wpnSelectLongest(m)) {
                if (geoDistance(mpos, ppos) > 210) memberFlee(m);
                else memberSurrender(m, cmd);
                continue;
            }
        }
        if (pR == 0 && mask == 1) {
            if (pM != 0) {
                memberReload(m, cmd);
                continue;
            }
            if (!wpnSelectLongest(m)) {
                memberFlee(m);
                continue;
            }
        }
        if (sR == 0 && mask == 2) {
            if (sM != 0) memberReload(m, cmd);
            else aiMemberStartScript(m, 2);
            continue;
        }
        // Fire.
        if (pR > 0) {
            setCmdWord(cmd, 1);
            w = primaryOf(m);
        }
        if (mask == 3 && secondaryOf(m) && sR > 0) {
            int pct = 40;
            if (contactTeamSize(b->contacts[ci]) >= 3) pct = 65;
            if (rng(100) <= pct) {
                setCmdWord(cmd, 2);
                w = secondaryOf(m);
            }
        }
        if (!w) continue;
        TargetRec& c = b->contacts[ci];
        if (needsScatter(w)) {
            Vec3 p = cpos;
            shotScatter(p, m);
            c.pos = p;
        }
        cmd.type = CommandType(u8(cmd.type) | 4);
        cmd.pos = c.pos;
        cmd.flag = 1;
        b->flags |= kContact;
        pushCommand(b, cmd);
    }
}

void aiGroupTickSuppression(Team* t, int dt) {
    for (int i = 0; i < 8 && t->members[i]; ++i) {
        Unit* m = t->members[i];
        if (isPlayer(m)) continue;
        Brain* b = m->brain;
        if (!b || !(b->flags & kSuppressed)) continue;
        if (b->suppress_time > s16(dt)) b->suppress_time = s16(b->suppress_time - dt);
        else b->suppress_time = 0;
        if (b->suppress_time == 0) b->flags &= u8(~kSuppressed);
    }
}

} // namespace st::game::mission
