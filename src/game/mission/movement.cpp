// Soldiers and craft as moving bodies (segment 2dbd parts 1 and 3): the rate
// helpers, speed / posture / move-mode rules, mover set-up, diving, fatigue,
// obstacle bounce and the per-frame movement of every foot unit, plus the two
// effect objects that ride with units (1000:5985 sampan, 1000:5D1D shadow and
// wake). docs/re/seg_2dbd.md 3, docs/re/seg_2dbd_movement.md.
#include "game/mission/people.h"

#include "data/exeimage.h"
#include "engine/rng.h"
#include "game/mission/combat.h"
#include "game/mission/craft.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/modern.h"
#include "game/mission/msg.h"
#include "game/mission/sfx.h"
#include "game/mission/state.h"
#include "game/mission/world.h"
#include "game/mission/wquery.h"

#include <utility>

namespace st::game::mission {

namespace {

// DGROUP tables and texts (read from st.exe).
constexpr u16 kWadingHeight = 0x0878;  // s16[] by terrain kind: [7] -9, [8] -3, [9] -15
constexpr u16 kEyeHeight = 0x0880;     // s16[3] {15, 9, 3} by posture
constexpr u16 kLowHeight = 0x0884;     // s16 3 (soldier low height, Point Man eye height reset)
constexpr u16 kPitHeight = 0x0888;     // s16 -3 (height after falling into a pit)
constexpr u16 kMsgDiveQuickly = 0x09ED;  // "Dive Quickly!"
constexpr u16 kMsgOnWater = 0x09FB;      // "On Water"
constexpr u16 kMsgPit = 0x0A04;          // "Boobytrap pit!"
constexpr u16 kMsgTripWire = 0x0A13;     // "Boobytrap grenade tripped!"
constexpr u16 kMsgTree = 0x0A2E;         // "Watch out for that tree."
constexpr u16 kMsgNoGo = 0x0A47;         // "You can't go in there."
constexpr u16 kMsgCollision = 0x0A5E;    // "Collision"

// Model table entries (far 514B:0000, 6-byte entries) used by the effects.
constexpr int kModelShadow = 75;     // 514B:01C2 shadow, +1 shadowc, +2 shadowp, +3 shadowu
constexpr int kModelRipples = 79;    // 514B:01DA ripples (wading)
constexpr int kModelRippleB = 80;    // 514B:01E0 rippleb (running through water)
constexpr int kModelSampanSm = 87;   // 514B:020A sampansm (team of up to 3)
constexpr int kModelSampanLg = 88;   // 514B:0210 sampanlg

std::function<Vec3()> g_cameraHook;

s16 dsShort(u16 off) { return exe().dgShort(off); }

// Team types 0, 4 and 5 ("people" whose speed depends on the body).
bool isPeopleTeam(const Team* t) {
    const int type = int(t->type);
    return type == 0 || type == 4 || type == 5;
}

// 19ac:63B9 unit_load_level on the personnel record's status image (SE +0x74).
int seLoadLevel(const Unit* u) {
    if (!u->se) return 0;
    Status s{};
    s.size = u->se->size;
    s.strength = u->se->strength;
    s.load = u->se->load;
    return unitLoadLevel(&s);
}

// Distance of one frame: (speed * g_frame_ticks) << 8 >> 8 (long shifts).
s32 frameDistance(s16 speed) {
    const s32 d = s32(speed) * s32(ms().frameTicks);
    return s32(u32(d) << 8) >> 8;
}

// Advance a body along heading8 (backwards when the speed is negative).
void moveBody(Obj3D* body, s16 speed, s16 heading8) {
    if (speed > 0) {
        posMovePolar(frameDistance(speed), 0, heading8, body->pos);
    } else if (speed < 0) {
        const int back = angleWrap(s16(heading8 + 0x5A0));
        posMovePolar(frameDistance(s16(-speed)), 0, back, body->pos);
    }
}

// Speed toward the target speed: decelerate to zero at mover.decel, else at mover.accel.
void approachSpeed(Mover* m) {
    if (m->speed == m->target_speed) return;
    if (m->target_speed == 0) evtApproachZero(m->speed, m->target_speed, m->decel);
    else evtApproachValue(m->speed, m->target_speed, m->accel);
}

// Argument of fx_unit_ground_fx: 0 (place) for a drawn body with detail >= 1, else -1.
int groundFxArg(const Obj3D* body) {
    return ((body->flags & obj3d_flag::kEnabled) && u8(ms().detailLevel) >= 1) ? 0 : -1;
}

Vec3 cameraPos(const Unit* u) {
    if (g_cameraHook) return g_cameraHook();
    const Unit* pm = pointMan();
    return pm ? pm->body->pos : u->body->pos;
}

} // namespace

void setGroundFxCameraHook(std::function<Vec3()> fn) { g_cameraHook = std::move(fn); }

// Nothing module-private survives a mission: the movement timers are in
// ms() (fatigueTick DS:0894, playerTerrainFxTime DS:D8B0, contactUntil DS:0898).
void peopleReset() {
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// 2dbd:0006
int evtHeadingDiff(int a8, int b8) {
    s16 hi = s16(s16(a8) >> 3);
    s16 lo = s16(s16(b8) >> 3);
    if (lo > hi) std::swap(hi, lo);
    if (s16(hi - lo) > 0xB4) lo = s16(lo + 0x168);
    const s16 d = s16(hi - lo);
    return s16((d ^ (d >> 15)) - (d >> 15));
}

// 2dbd:2503
void evtApproachValue(s16& value, s16 target, int rate) {
    const s16 diff = s16(value - target);
    const s32 prod = s32(s16(rate)) * s32(ms().frameTicks);  // math_imul16
    const s16 step = prod < 0x100 ? s16(1) : s16(prod >> 8);
    const s16 mag = s16((diff ^ (diff >> 15)) - (diff >> 15));
    if (step < mag) {
        if (diff < 0) value = s16(value + step);
        else value = s16(value - step);
    } else {
        value = target;
    }
}

// 2dbd:2580: the target is shifted by 360 for the call and restored.
void evtApproachAngle(s16& value, s16 target, int rate) {
    const s16 diff = s16(value - target);
    const s16 mag = s16((diff ^ (diff >> 15)) - (diff >> 15));
    if (mag < 0xB4) {
        evtApproachValue(value, target, rate);
    } else if (target > 0xB4) {
        evtApproachValue(value, s16(target - 0x168), rate);
        if (value < 0) value = s16(value + 0x168);
    } else {
        evtApproachValue(value, s16(target + 0x168), rate);
        if (value >= 0x168) value = s16(value - 0x168);
    }
}

// 2dbd:2603 (the original reads the rate at pair + 0x0A).
void evtApproachZero(s16& value, s16& target, int rate) {
    target = 0;
    evtApproachValue(value, target, rate);
}

// 2dbd:2933
bool evtIsPositiveTurn(int a8, int b8) {
    s16 b = s16(s16(b8) >> 3);
    s16 a = s16(s16(a8) >> 3);
    if (s16(a - b) > 0xB4) b = s16(b + 0x168);
    else if (s16(a - b) < -0xB4) a = s16(a + 0x168);
    return s16(a - b) >= 0;
}

// 2dbd:3628: the type byte is compared with the whole argument word.
WeaponNode* evtUnitFindWeapon(const Unit* u, int weaponType) {
    if (!u || !u->loadout) return nullptr;
    for (WeaponNode* n = u->loadout->list; n; n = n->next)
        if (int(u8(n->type)) == int(u16(weaponType))) return n;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Speed, posture, mode
// ---------------------------------------------------------------------------

// 2dbd:01A7
int evtCalcMoveSpeed(Unit* u, int base) {
    Mover* m = u->mover;
    s16 p = 0;
    if (m->flags & mover_flag::kInWater) p = 2;
    if ((m->flags & mover_flag::kDeepWater) || (m->flags & mover_flag::kSlowTerrain)) p = s16(p + 1);
    p = s16(p + seLoadLevel(u) * 2);
    p = s16(p + m->fatigue);
    if (m->flags & mover_flag::kEscort) {
        p = s16(p + 1);
        const Unit* b = u->buddy;
        if (b && unitDead(b)) p = s16(p + 8);
        else if (b && b->status->heavy_wounds) p = s16(p + 4);
    }
    const Ticks now = ms().time;
    if (now < m->fired_until) {
        p = s16(p + 2);
        m->winded = s16(m->winded + 1);
    }
    if (now < m->throw_until) p = s16(p + 2);
    if (now < m->posture_until) p = s16(p + 2);
    if (u8(m->posture) == 2) p = s16(p + 8);
    else if (u8(m->posture) == 1) p = s16(p + 2);
    s16 bonus = s16((int(u->status->size) + int(u->status->strength) - 0x50) / 10);
    if (bonus < 0) bonus = 0;
    const s16 r = s16(bonus - p + s16(base));
    return r < 0 ? 0 : r;
}

// 2dbd:0307
void evtSetPosture(Unit* u, int posture) {
    Mover* m = u->mover;
    if (u16(u8(m->posture)) != u16(posture)) {
        m->posture_until = wrapAdd(ms().time, 0x200);
        // Original quirk (2dbd:0348): the speed is recalculated before the new
        // posture is stored, so it still carries the old posture's penalty.
        evtSetMoveMode(u, u8(m->move_mode));
    }
    m->posture = Posture(u8(posture));
    if (u8(m->move_mode) == 2 && s16(posture) != 0) evtSetMoveMode(u, 1);
    if (u == pointMan()) {
        // Original quirk: running was just cancelled for any posture but
        // upright, so the eye height 0 case can never be taken.
        if (u8(m->move_mode) == 2 && s16(posture) == 2) m->target_height = 0;
        else if (!(u->mover->flags & mover_flag::kInWater)) m->target_height = dsShort(u16(kEyeHeight + posture * 2));
    }
}

// 2dbd:03C5
void evtSetMoveMode(Unit* u, int mode) {
    Mover* m = u->mover;
    const bool people = isPeopleTeam(u->team);
    const s16 v = people ? s16(evtCalcMoveSpeed(u, m->base_speed)) : m->base_speed;
    if (s16(mode) == 0) {
        m->target_speed = 0;
    } else if (s16(mode) == 2) {
        if (people && u8(m->posture) != 0) {
            evtSetPosture(u, 0);
            sprSetAnim(u, 0);
        }
        // Original quirk: v was computed with the posture before standing up.
        m->target_speed = v;
    } else {
        m->target_speed = s16(v >> 2);
        if (s16(mode) == 3) m->target_speed = s16(-m->target_speed);
    }
    m->move_mode = MoveMode(u8(mode));
}

// 2dbd:04A7
void evtSetDesiredHeading(Unit* u, int deg) {
    if (!u || !u->mover) return;
    u->mover->desired_heading = s16(deg);
}

// 2dbd:04D8
void evtStopUnit(Unit* u) {
    Mover* m = u->mover;
    evtSetMoveMode(u, 0);
    m->speed = m->target_speed;
    if (u->anim) u->anim->move_mode = 0;
}

// 2dbd:261E
void evtInitMover(Unit* u, UnitClass kind) {
    // Original bug (2dbd:262C / 2641): with a NULL unit or mover the body
    // altitude and the mover tail are still written through the NULL pointer.
    if (!u || !u->mover) return;
    Mover* m = u->mover;
    const int k = int(kind);
    if (k <= 4 || k >= 8) {
        m->unk_06 = -12;
        m->accel = 0x20;
        m->decel = 0x40;
        m->unk_10 = 0x168;
        m->unk_12 = 0;
        m->turn_rate = 0x70;
        m->target_height = dsShort(kEyeHeight);
        m->height = m->target_height;
        m->height_high = m->target_height;
        m->height_low = dsShort(kLowHeight);
        m->base_speed = 0x18;
        m->climb_rate = 0x18;
        m->descent_rate = 0x48;
    } else if (k == 6) {
        m->unk_06 = -0x20;
        m->accel = 0x1E;
        m->unk_10 = 0x168;
        m->turn_rate = 0x30;
        m->target_speed = 0;
        m->speed = 0;
        m->unk_12 = 0;
        m->desired_heading = 0;
        m->heading = 0;
        m->height = 0xF0;
        m->target_height = 0x10E;
        m->height_high = 0x10E;
        m->height_low = 9;
        m->climb_rate = 0x18;
        m->base_speed = 0x80;
        m->decel = 0x80;
        m->descent_rate = 0x80;
        m->destination = u->body->pos;
    } else if (k == 7) {
        m->unk_06 = 0x18;
        m->base_speed = 0x2D0;
        m->target_speed = 0x2D0;
        m->speed = 0x2D0;
        m->unk_10 = 0x168;
        m->accel = 0x20;
        m->turn_rate = 0x20;
        m->unk_12 = 0;
        m->desired_heading = 0;
        m->heading = 0;
        m->height = 0x1C2;
        m->target_height = 0x1E0;
        m->height_high = 0x1E0;
        m->height_low = 0x78;
        m->climb_rate = 0x40;
        m->decel = 0x80;
        m->descent_rate = 0x80;
        m->destination = u->body->pos;
    } else {  // 5, boat
        m->base_speed = 0x40;
        m->unk_06 = -0x20;
        m->accel = 0x20;
        m->decel = 0x20;
        m->unk_10 = 0x168;
        m->turn_rate = 0x30;
        m->height = 3;
        m->height_high = 3;
        m->target_speed = 0;
        m->speed = 0;
        m->unk_12 = 0;
        m->desired_heading = 0;
        m->heading = 0;
        m->target_height = 0;
        m->height_low = 0;
        m->climb_rate = 4;
        m->descent_rate = 8;
        m->destination = u->body->pos;
    }
    if (k > 4 && k < 8) u->body->pos.y = s32(u32(s32(m->height)) << 8);
    m->fatigue = 0;
    m->winded = 0;
    m->posture_until = 0;
    m->throw_until = 0;
    m->fired_until = 0;
}

// 2dbd:2841
void evtSetAltitudeLevel(Unit* u, int level) {
    Mover* m = u->mover;
    m->posture = Posture(u8(level));
    if (s16(level) == 0) m->target_height = m->height_high;
    else if (s16(level) == 1) m->target_height = s16((m->height_high >> 1) + m->height_low);
    else m->target_height = m->height_low;
}

// 2dbd:287F
WorldObject* evtSetDestToTerrainObj(Unit* u, WorldObject* target) {
    if (!target) {
        const int i = wldNearestObject(u->body->pos, 7);
        // The original also computes geo_distance to the object and discards it.
        if (i >= 0) target = worldObjects()[size_t(i)];
    }
    if (target) u->mover->destination = target->body->pos;
    return target;
}

// ---------------------------------------------------------------------------
// Diving
// ---------------------------------------------------------------------------

// 2dbd:16E6
void evtUnitDive(Unit* u, int heading) {
    if (!u) return;
    if (s16(u->status->unit_class) >= 5) return;
    Mover* m = u->mover;
    evtSetMoveMode(u, 2);
    if (u->anim) u->anim->move_mode = 2;
    m->desired_heading = s16(heading);
    u->body->heading = s16(heading << 3);
    m->heading = s16(heading);
    evtSetPosture(u, 2);
    sprSetAnim(u, 2);
    m->speed = s16(m->base_speed << 1);
    m->height = m->height_high;
    m->target_height = m->height_high;
    // Original: the mode byte is cleared, the target speed of the (walk) mode stays.
    m->move_mode = MoveMode::Stop;
    posMovePolar(0x2D00, 0, u->body->heading, u->body->pos);
}

// 2dbd:17C8 (the 'q' key's rate limit DS:D80E is the caller's).
void evtTeamDiveQuickly(Team* t) {
    if (!t) return;
    Unit* leader = t->members[0];
    // Far pointer to the leader's live position: the members scatter away
    // from where the leader lands.
    const Vec3& lp = leader->body->pos;
    if (unitAlive(leader)) {
        evtUnitDive(leader, s16(leader->body->heading) >> 3);
        msgShowDs(kMsgDiveQuickly, 0x300);
    }
    for (int i = 1; i < 8 && t->members[i]; ++i) {
        Unit* u = t->members[i];
        const s16 b8 = s16(geoBearing(lp, u->body->pos) << 3);
        if (unitAlive(u)) evtUnitDive(u, b8 >> 3);
    }
}

// ---------------------------------------------------------------------------
// Fatigue and obstacles
// ---------------------------------------------------------------------------

namespace {

// Serious wounds force the posture one step lower (not below prone).
void woundPostureStep(Unit* u) {
    const u8 p = u8(u->mover->posture);
    if (p >= 2) return;
    evtSetPosture(u, p + 1);
    sprSetAnim(u, u8(u->mover->posture));
}

} // namespace

// 2dbd:18B2 (every 0x500 ticks from the movement update)
void evtUpdateUnitFatigue(Unit* u) {
    Mover* m = u->mover;
    const Status* s = u->status;
    const s16 winded0 = m->winded;
    if (u8(m->move_mode) == 2) {
        m->fatigue = s16(m->fatigue + 1);
        if (s->light_wounds) m->fatigue = s16(m->fatigue + 2);
        if (s->heavy_wounds) {
            woundPostureStep(u);
            m->fatigue = s16(m->fatigue + 2);
        }
        m->winded = s16(m->winded + 2);
    } else if (u8(m->move_mode) != 0) {
        if (s->light_wounds) m->fatigue = s16(m->fatigue + 1);
        if (s->heavy_wounds) {
            m->fatigue = s16(m->fatigue + 1);
            woundPostureStep(u);
        }
        if (u->team->type == TeamType::Seal) {
            const s16 f = s16(m->fatigue - 1);
            m->fatigue = f < 0 ? 0 : f;
        }
        const s16 w = s16(m->winded - 1);
        m->winded = w < 0 ? 0 : w;
    }
    // The mode is read again: a wound posture step may have ended the run.
    if (u8(m->move_mode) == 0) {
        const s16 f = s16(m->fatigue - 1);
        m->fatigue = f < 0 ? 0 : f;
        const s16 w = s16(m->winded - 2);
        m->winded = w < 0 ? 0 : w;
    }
    if (m->fatigue > 8) m->fatigue = 8;
    if (m->winded > 8) m->winded = 8;

    if (u != pointMan()) return;
    if (m->winded == winded0 && m->winded != 0) return;
    if (m->winded < 2) {
        if (engine::rng().range(2) != 0) sfxPlay(0x20, 0x400, &u->body->pos, 1, nullptr);
        else sfxPlay(0x1E, 0x300, &u->body->pos, 1, nullptr);
    } else if (m->winded < 4) {
        sfxPlay(0x1F, 0x600, &u->body->pos, 1, nullptr);
    }
}

// 2dbd:1B05
void evtUnitBounceOffObstacle(Unit* u) {
    MissionState& S = ms();
    Obj3D* body = u->body;
    body->pos.x = S.losHit.x;  // contact point of the last sweep (DS:ECFE / ED06)
    body->pos.z = S.losHit.z;
    Mover* m = u->mover;
    const s16 ib = m->impact_bearing;
    posMovePolar(0x400, 0, angleWrap(s16(s16(ib + 0xB4) << 3)), body->pos);
    if (evtHeadingDiff(s16(m->heading << 3), s16(ib << 3)) >= 100) return;
    // Turn 100 degrees off the obstacle, to the side nearer the current heading.
    int h = angleWrap(s16(s16(ib + 100) << 3));
    if (evtHeadingDiff(s16(m->heading << 3), h) > 100) h = angleWrap(s16(s16(ib - 100) << 3));
    m->desired_heading = s16(s16(h) >> 3);
    if (u == pointMan()) u->team->view_heading = s16(angleWrap(h + 0x5A0));
}

// ---------------------------------------------------------------------------
// Per-frame movement (2dbd:1C04)
// ---------------------------------------------------------------------------

namespace {

// Pass 1 for one unit (every foot unit except the Point Man). `changed` is
// the function-wide "terrain left" flag of the original (consumed here).
void moveOtherUnit(Team* t, int slot, Unit* u, bool tick, bool& changed) {
    Obj3D* body = u->body;
    Mover* m = u->mover;
    const int type = int(t->type);
    if (!(m->flags2 & mover_flag2::kInBoat)) fxUnitGroundFx(u, groundFxArg(body));
    approachSpeed(m);
    if (m->height != m->target_height) evtApproachValue(m->height, m->target_height, m->climb_rate);
    modernDetourUpdate(u);  // port: Modern gameplay detour heading (no-op while off)
    // Living units turn, and dead ones that are being carried.
    if ((unitAlive(u) || u->buddy) && m->heading != m->desired_heading)
        evtApproachAngle(m->heading, m->desired_heading, s16(m->turn_rate >> (u8(m->posture) & 0x1F)));
    const s16 h8 = s16(m->heading << 3);
    body->heading = h8;
    // The original computes geo_distance(unit, Point Man) here and discards it.
    moveBody(body, m->speed, h8);
    if (unitDead(u)) return;

    if (tick) evtUpdateUnitFatigue(u);
    WorldObject* w = wldProbeSolid(body);
    const int k = w ? int(w->kind) : -1;
    if (!w || k == 5 || k == 0xE || k == 0xF || k == 0x11) {
        // NPCs walk over clearings, pads and brush and never trigger traps.
        if (m->flags & 0x0F) {
            if ((type == 4 || type == 5) && slot == 0 && (m->flags2 & mover_flag2::kInBoat)) fxTeamMarker(u, -1);
            if (!(u->mover->flags & mover_flag::kBlocked) || tick) {
                m->flags &= 0xF0;   // word AND 0xFDF0: terrain bits and the sampan bit
                m->flags2 &= 0xFD;
            }
            changed = true;
        }
    } else {
        m->impact_bearing = s16(geoBearing(body->pos, w->body->pos));
        if (k == 7 || k == 9 || k == 8) {
            m->flags |= mover_flag::kInWater;
            if (k == 9) m->flags |= mover_flag::kDeepWater;
            m->target_height = dsShort(u16(kWadingHeight + k * 2));
            if ((type == 4 || type == 5) && slot == 0 && (u->mover->flags2 & mover_flag2::kInBoat) &&
                !(u->mover->flags & mover_flag::kSecured)) {
                fxTeamMarker(u, 0);
                t->formation = Formation::Column;
            }
        } else {
            m->flags &= 0xF3;
            m->flags |= mover_flag::kBlocked;
            evtUnitBounceOffObstacle(u);
            ++ms().portStats.bounces;  // port: --sim-mission statistics only
            modernNoteBounce(u, w);    // port: Modern gameplay detours (no-op while off)
        }
    }
    if (tick || changed) {
        evtSetMoveMode(u, u8(m->move_mode));
        if (changed) changed = false;
        if (!(u->mover->flags & mover_flag::kInWater)) m->target_height = 0;
    }
}

} // namespace

void evtUpdateUnitMovement() {
    MissionState& S = ms();
    bool blocked = false;  // [bp-0x20]: the Point Man bounced or tripped a wire
    bool changed = false;  // [bp-0x22]: terrain entered/left, re-apply the mode
    bool tick = false;     // [bp-0x24]: the 0x500 fatigue tick (DS:0894)
    if (wrapSub(S.time, S.fatigueTick) >= 0x500) {
        S.fatigueTick = S.time;
        // Original quirk: the tick is "now != 0", so it never fires at g_time 0.
        tick = S.time != 0;
    }

    // Pass 1: every unit of the foot teams (types 0 and >= 4) but the Point Man.
    for (int ti = 0; ti < kMaxTeams && S.teams[ti]; ++ti) {
        Team* t = S.teams[ti];
        const int type = int(t->type);
        if (type != 0 && type < 4) continue;
        for (int mi = 0; mi < 8 && t->members[mi]; ++mi) {
            if (ti == 0 && mi == 0) continue;
            moveOtherUnit(t, mi, t->members[mi], tick, changed);
        }
    }

    // Pass 2: the Point Man.
    Unit* pm = pointMan();
    if (!pm) return;
    Obj3D* body = pm->body;
    Mover* m = pm->mover;
    const s16 h8 = body->heading;  // the player's heading is turned by the controls
    approachSpeed(m);
    if (m->height != m->target_height) {
        if (m->target_height == 0) evtApproachZero(m->height, m->target_height, m->descent_rate);
        else evtApproachValue(m->height, m->target_height, m->climb_rate);
        if (m->height == 0) m->target_height = dsShort(kLowHeight);
    }
    // view_set_eye_height (1000:2462) belongs to the mission loop.
    if (!blocked) moveBody(body, m->speed, h8);  // blocked is always clear here

    if (unitAlive(pm)) {
        if (tick) evtUpdateUnitFatigue(pm);
        WorldObject* w = wldProbeSolid(body);
        if (!w) {
            if (m->flags & 0x0F) {
                m->flags &= 0xF0;
                changed = true;
            }
        } else {
            const int k = int(w->kind);
            m->impact_bearing = s16(geoBearing(body->pos, w->body->pos));
            if (k == 7 || k == 9 || k == 8) {
                if (!(pm->mover->flags & mover_flag::kInWater)) {
                    msgShowDs(kMsgOnWater, 0x200);
                    changed = true;
                }
                m->flags |= mover_flag::kInWater;
                if (k == 9) m->flags |= mover_flag::kDeepWater;
                m->target_height = dsShort(u16(kWadingHeight + k * 2));
            } else if (k == 0xB && unitAlive(pm)) {
                msgShowDs(kMsgPit, 0x300);
                combatRandomWound(pm);
                body->pos = w->body->pos;  // into the pit
                m->height = dsShort(kPitHeight);
                wldHideHighlight(w);
                m->target_speed = 0;
            } else if (k == 0xA && u8(m->posture) < 2) {
                msgShowDs(kMsgTripWire, 0x100);
                fxPhantomExplosion(pm);
                blocked = true;
                w->body->flags &= u16(~obj3d_flag::kEnabled);  // wire disarmed
                m->speed = 0;
                m->target_speed = 0;
            } else if (k == 0x11) {
                // Brush rustles while moving, at most every 0x200 ticks.
                if (u8(pm->mover->move_mode) != 0 && S.time > S.playerTerrainFxTime) {
                    S.playerTerrainFxTime = wrapAdd(S.time, 0x200);
                    sfxPlay(0x1A, 0x100, &body->pos, 1, pm);
                }
            } else {
                // Anything else blocks (also a prone player on a trip wire).
                blocked = true;
                if (S.time > S.playerTerrainFxTime) {
                    S.playerTerrainFxTime = wrapAdd(S.time, 0x300);
                    if (k == 6) msgShowDs(kMsgTree, 0x100);
                    else if (k == 0) msgShowDs(kMsgNoGo, 0x100);
                    else msgShowDs(kMsgCollision, 0x80);
                    sfxPlay(0x0B, 0x100, &body->pos, 1, pm);
                }
                evtUnitBounceOffObstacle(pm);
            }
        }
        if (tick || changed) {
            evtSetMoveMode(pm, u8(m->move_mode));
            if (!(pm->mover->flags & mover_flag::kInWater)) {
                evtSetPosture(pm, u8(m->posture));
                sprSetAnim(pm, u8(pm->mover->posture));
            }
        }
        // The player's heading follows the mover only on the camp maps or
        // after a bounce; otherwise the controls turn the body directly.
        if (wldIsCamp() || blocked) {
            if (m->heading != m->desired_heading) evtApproachAngle(m->heading, m->desired_heading, m->turn_rate);
            body->heading = s16(m->heading << 3);
        }
    }
    fxUnitGroundFx(pm, groundFxArg(body));
}

// ---------------------------------------------------------------------------
// Effects riding with units (segment 1000)
// ---------------------------------------------------------------------------

// 1000:5985: sampan of an enemy team travelling on water (pool DS:2F94).
void fxTeamMarker(Unit* u, int on) {
    if (s16(on) < 0) {
        // Original: only this unit's pointer is dropped; the sampan stays
        // visible where the team landed and the members keep pointing at it.
        u->attached_fx = nullptr;
        return;
    }
    Obj3D* fx = u->attached_fx;
    if (!fx) {
        fx = fxPoolAlloc(fxPools().teamMarker);
        u->attached_fx = fx;
        if (fx) {
            fx->model = modelEntry(grpCountAlive(u->team) > 3 ? kModelSampanLg : kModelSampanSm).desc;
            for (int i = 1; i < 8 && u->team->members[i]; ++i) u->team->members[i]->attached_fx = fx;
        }
    }
    if (!fx) return;
    fx->flags |= obj3d_flag::kEnabled;
    const int h = angleWrap(s16(u->body->heading + 0x5A0));
    const s16 n = s16(grpCountAlive(u->team));
    s16 ox = 0;
    s16 oz = s16(0x12 * n - 0x1E);
    mathRotate2d(ox, oz, 0, 0, h);
    fx->pos.x = wrapAdd(u->body->pos.x, s32(ox) * 256);
    fx->pos.z = wrapAdd(u->body->pos.z, s32(oz) * 256);
    fx->pos.y = -0x200;
    fx->heading = s16(h);
}

// 1000:5D1D: shadow (on land) or ripples (in water) under a unit (pool DS:2F2C).
void fxUnitGroundFx(Unit* u, int on) {
    const int d = geoDistance(u->body->pos, cameraPos(u));
    if (s16(on) < 0 || d > 0x4B0) {
        if (u->attached_fx) {
            u->attached_fx->flags &= u16(~obj3d_flag::kEnabled);
            u->attached_fx = nullptr;
        }
        return;
    }
    Obj3D* fx = u->attached_fx;
    if (!fx) {
        fx = fxPoolAlloc(fxPools().groundFx);
        u->attached_fx = fx;
    }
    if (!fx) return;
    const Mover* m = u->mover;
    const ModelDesc* rippleB = modelEntry(kModelRippleB).desc;
    if ((m->flags & mover_flag::kInWater) && unitAlive(u)) {
        int h;
        if (u8(m->move_mode) != 2) {
            fx->model = modelEntry(kModelRipples).desc;
            h = s16(engine::rng().range(8) + u->body->heading);
        } else {
            fx->model = rippleB;
            h = u->body->heading;
        }
        fx->heading = s16(angleWrap(h));
        fx->pitch = (ms().time & 0x100) ? 1 : 0;  // ripple animation frame
    } else {
        // Shadow by posture: upright/crouch/prone, 3 = lying (dead, no timed animation).
        const Anim* a = u->anim;
        const s16 state = a ? s16(a->state) : 0;
        const s16 posture = a ? a->posture : 0;
        int idx = state > 3 ? 2 : 3;
        if (idx > posture) idx = posture;
        fx->model = modelEntry(kModelShadow + idx).desc;
        const s16 r = s16(engine::rng().range(15));
        const s16 turn = s16(s16(r * u8(m->move_mode)) << 3);
        fx->heading = s16(angleWrap(s16(turn + u->body->heading)));
        fx->pitch = 0;
    }
    fx->flags |= obj3d_flag::kEnabled;
    fx->pos.x = u->body->pos.x;
    fx->pos.z = u->body->pos.z;
    fx->pos.y = 0;
    if (fx->model == rippleB) {
        // Original bug (1000:5EEC): the heading for this move is read after ES
        // was switched to the model table segment (514B:obj+0x12, unrelated
        // data); the port uses the object's heading as intended.
        posMovePolar(0x900, 0, angleWrap(s16(fx->heading + 0x5A0)), fx->pos);
        fx->heading = s16(angleWrap(s16(fx->heading + 0x5A0)));
    }
}

} // namespace st::game::mission
