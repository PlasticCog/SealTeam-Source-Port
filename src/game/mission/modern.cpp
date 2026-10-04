// Port addition: the Enhanced "Modern gameplay" simulation rules (modern.h,
// docs/mission.md "Modern gameplay"). No function here draws from
// engine::rng(); every choice is made from the unit's own state (the
// Phantom crews' scatter comes from a private generator).
#include "game/mission/modern.h"

#include "core/settings.h"
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/craft.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/sfx.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/mission/world.h"
#include "game/mission/wquery.h"

#include <cstdio>

namespace st::game::mission {

namespace {

constexpr int kHoldBlastMargin = 60;     // units added to the blast radius around the target
constexpr int kHoldLineDistance = 90;    // units from the line of fire
constexpr int kDetourBounces = 3;        // bounces against one obstacle that start a detour
constexpr int kDetourBounceWindow = 0x500;
constexpr int kDetourTurn = 90;          // degrees off the impact bearing
constexpr int kDetourClearance = 20;     // units beyond the obstacle radius that end it
constexpr int kDetourMaxTicks = 0x300;
constexpr int kGrenadeInterval = 0x400;  // ticks between two throws of one unit

int wrapDeg(int d) {
    d %= 360;
    return d < 0 ? d + 360 : d;
}

bool thrownItem(const WeaponNode* w) { return (u8(weaponDef(u8(w->type)).fire_modes) & fire_mode::kThrow) != 0; }

PortUnitState* findState(const Unit* u) {
    auto it = ms().portUnits.find(u);
    return it == ms().portUnits.end() ? nullptr : &it->second;
}

} // namespace

bool modernGameplayOn() { return settings().effectiveModernGameplay(); }

// ---------------------------------------------------------------------------
// Rule 3: support craft hold fire near friendlies
// ---------------------------------------------------------------------------

bool modernCraftHoldsFire(const Unit* craft, const Vec3& target, const WeaponNode* w) {
    if (!modernGameplayOn() || !craft || !w) return false;
    const MissionState& S = ms();
    const Vec3& from = craft->body->pos;
    const int radius = weaponDef(u8(w->type)).blast_radius;
    const int safe = (radius > 0 ? radius : 0) + kHoldBlastMargin;
    const int lineLen = geoDistance(from, target);
    const int lineBrg = geoBearing(from, target);
    for (int ti = 0; ti < kMaxTeams && S.teams[ti]; ++ti) {
        const Team* t = S.teams[ti];
        if (t->type != TeamType::Seal && t->type != TeamType::Friendly) continue;
        for (int mi = 0; mi < 8 && t->members[mi]; ++mi) {
            const Unit* u = t->members[mi];
            if (unitDead(u) || !u->mover || (u->mover->flags & mover_flag::kAboard)) continue;
            const Vec3& p = u->body->pos;
            if (geoDistance(target, p) <= safe) return true;
            // Perpendicular distance from the segment craft -> target: the
            // unit must lie ahead of the craft and not beyond the target
            // (the circle above covers the far end).
            const int d = geoDistance(from, p);
            if (d > lineLen) continue;
            int off = geoBearing(from, p) - lineBrg;
            if (off > 180) off -= 360;
            if (off < -180) off += 360;
            if (off < 0) off = -off;
            if (off >= 90) continue;
            const int perp = (d * mathSin(off << 3)) >> 14;  // 1.14 sine
            if (perp <= kHoldLineDistance) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Rule 4: squad mates work around obstacles
// ---------------------------------------------------------------------------

void modernNoteBounce(Unit* u, const WorldObject* obstacle) {
    if (!modernGameplayOn() || !u || !obstacle || !u->team || u->team->type != TeamType::Seal) return;
    MissionState& S = ms();
    PortUnitState& st = S.portUnits[u];
    Mover* m = u->mover;
    if (st.detour) {
        // Bounced again while walking around it. The impact bearing points
        // at the object's centre, not at the face the 6-unit probe hit, so a
        // detour heading can lead straight into the outline: the bounce then
        // resets the unit to the same spot every few frames ("pinned"). After
        // three such bounces on the detour heading turn 90 degrees to the
        // side that leads away from the obstacle.
        const bool onHeading = evtHeadingDiff(m->heading << 3, st.detour_heading << 3) <= 10;
        const bool pinned = onHeading && geoDistance(st.detour_last_pos, u->body->pos) <= 1;
        st.detour_last_pos = u->body->pos;
        st.detour_pinned = pinned ? st.detour_pinned + 1 : 0;
        if (st.detour_pinned >= kDetourBounces) {
            const int a = wrapDeg(st.detour_heading + kDetourTurn);
            const int b = wrapDeg(st.detour_heading - kDetourTurn);
            const int ib = m->impact_bearing;
            st.detour_heading = s16(evtHeadingDiff(a << 3, ib << 3) >= evtHeadingDiff(b << 3, ib << 3) ? a : b);
            st.detour_pinned = 0;
            st.detour_start = S.time;
        }
        m->desired_heading = st.detour_heading;
        return;
    }
    if (st.bounce_obj == obstacle && wrapSub(S.time, st.bounce_time) <= kDetourBounceWindow) {
        ++st.bounce_count;
    } else {
        st.bounce_obj = obstacle;
        st.bounce_count = 1;
    }
    st.bounce_time = S.time;
    if (st.bounce_count < kDetourBounces) return;
    // Walk around it: perpendicular to the impact bearing, on the side
    // nearer the formation slot / destination (ties go right).
    const int ib = m->impact_bearing;
    const int want = geoBearing(u->body->pos, m->destination);
    const int right = wrapDeg(ib + kDetourTurn);
    const int left = wrapDeg(ib - kDetourTurn);
    const int h = evtHeadingDiff(right << 3, want << 3) <= evtHeadingDiff(left << 3, want << 3) ? right : left;
    st.detour = true;
    st.detour_heading = s16(h);
    st.detour_start = S.time;
    st.detour_obj = obstacle;
    st.detour_last_pos = u->body->pos;
    st.detour_pinned = 0;
    st.bounce_obj = nullptr;
    st.bounce_count = 0;
    m->desired_heading = s16(h);
    ++S.portStats.detours;
}

void modernDetourUpdate(Unit* u) {
    if (!modernGameplayOn() || !u) return;
    PortUnitState* st = findState(u);
    if (!st || !st->detour) return;
    MissionState& S = ms();
    const WorldObject* w = st->detour_obj;
    const bool clear = !w || geoDistance(u->body->pos, w->body->pos) >= objRadiusOrDefault(w->model) + kDetourClearance;
    if (clear || wrapSub(S.time, st->detour_start) >= kDetourMaxTicks || unitDead(u)) {
        st->detour = false;
        st->detour_obj = nullptr;
        return;
    }
    u->mover->desired_heading = st->detour_heading;
}

bool modernDetourActive(const Unit* u) {
    if (!modernGameplayOn() || !u) return false;
    const PortUnitState* st = findState(u);
    return st && st->detour;
}

// ---------------------------------------------------------------------------
// Rule 5: enemy grenade discipline
// ---------------------------------------------------------------------------

bool modernGrenadeAllowed(Unit* u, const WeaponNode* w, int dist, bool secondaryRoll) {
    if (!modernGameplayOn() || !u || !w || !thrownItem(w)) return true;
    MissionState& S = ms();
    PortUnitState& st = S.portUnits[u];
    if (!secondaryRoll) {
        // The same range rule as the secondary roll (a quarter of the maximum
        // range) and one throw per 4 seconds.
        const bool inRange = weaponDef(u8(w->type)).range_max >= s16(u16(dist) << 2);
        const bool rested = !st.thrown || wrapSub(S.time, st.last_throw) >= kGrenadeInterval;
        if (!inRange || !rested) {
            ++S.portStats.grenadeHolds;
            return false;
        }
    }
    st.thrown = true;
    st.last_throw = S.time;
    return true;
}

bool modernSkipThrownForLongest(const WeaponNode* w) { return modernGameplayOn() && w && thrownItem(w); }

// ---------------------------------------------------------------------------
// Rule 8: a demolition charge explodes where it lies
// ---------------------------------------------------------------------------

void modernSatchelBlastAtCharge(ShotRec& s) {
    if (!modernGameplayOn() || s.weapon != kSatchelWeapon) return;
    const Projectile* p = s.projectile;
    // The charge is still in its slot when the shot resolves (the slot is
    // released after shot_update_all marks it resolved).
    if (!p || !(p->state & prj_state::kInUse) || p->weapon != kSatchelWeapon || p->owner != s.shooter) return;
    Vec3 at = p->body->pos;
    at.y = 0;
    s.target_pos = at;
    ++ms().portStats.satchelsRecentred;
}

// ---------------------------------------------------------------------------
// Rule 6: the snatch target is marked (presentation only)
// ---------------------------------------------------------------------------

bool modernIsSnatchTarget(const Unit* u) {
    if (!modernGameplayOn() || !u) return false;
    MissionState& S = ms();
    if (!S.snatchTargetsValid) {
        // The target team of a Snatch objective is MTM team `target_team`
        // (msn_check_objective, msn_find_objective_target); its leader is
        // the unit the objective's distance test and camera refer to. Not
        // before the MTM teams exist (the camp scenes have none: 0xFF).
        if (S.firstMtmGroup == 0xFF || S.teamCount <= S.firstMtmGroup) return false;
        for (int i = 0; i < 3; ++i) {
            const MciObjective& o = S.mci.objective[i];
            S.snatchTargets[i] = nullptr;
            if (o.kind != ObjectiveKind::Snatch || o.target_team < 0) continue;
            const Team* t = team(S.firstMtmGroup + o.target_team);
            if (t && t->members[0] && (isEnemyTeam(t) || t->type == TeamType::Civilian)) S.snatchTargets[i] = t->members[0];
        }
        S.snatchTargetsValid = true;
    }
    for (const Unit* t : S.snatchTargets)
        if (t && t == u) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Rule 7: callable F-4 Phantom air strikes
// ---------------------------------------------------------------------------

namespace {

using Phase = PortPhantomState::Phase;

constexpr u16 kF4Model = 0x7C72;          // model table entry 0x37 "f4" (the ambient fly-over)
constexpr u16 kBombModel = 0xB552;        // "rckt": the bomb's shape in flight
constexpr int kBombWeapon = 0x1C;         // T.31 mortar round: class 20, blast 180, structure damage 40, sound 0x10
constexpr s16 kPhantomSpeed = 0x5A0;      // twice the OV-10's 0x2D0 (1440 units/s)
constexpr s16 kPhantomTurnRate = 0x30;    // deg/s (the OV-10 turns 0x20)
constexpr s16 kPhantomHighAlt = 0x5DC;    // 1500 units: the ambient F-4's altitude (0x5DC00 >> 8)
constexpr s16 kPhantomRunAlt = 0x1E0;     // 480 units: the OV-10's cruise altitude, over the marker
constexpr s16 kPhantomClimbRate = 0x100;  // units/s up and down
constexpr int kPhantomParkDist = 9000;    // units behind the insertion point (away from objective 1)
constexpr int kPhantomReleaseDist = 150;  // release within this distance of the aim point
constexpr int kPhantomPassDist = 600;     // ... or at the closest approach inside this distance
constexpr int kPhantomScatter = 30;       // units of scatter per axis (a skilled crew)
constexpr int kPhantomDangerMargin = 90;  // units added to the blast radius around the impact point
constexpr s16 kBombSpeed = 0x20;          // forward speed of the falling bomb (a retarded bomb: 128 units in its 4 s fall)
constexpr int kBombGravity = 0x3C;        // the ordnance update's gravity (units/s per second)
constexpr Ticks kBombLifetime = 0xA00;    // well beyond the fall (the shot resolves on landing)
constexpr int kPhantomRunAhead = 6000;    // units past the marker the egress heads for
constexpr int kPhantomEgressDist = 1500;  // units from the marker before the run counts as over ...
constexpr Ticks kPhantomEgressTicks = 0x600;   // ... and ticks for the bombs to land
constexpr Ticks kPhantomRunTimeout = 0x4000;   // a run that never reaches its aim point ends after 64 s

const std::string kMsgInbound = "Phantom flight inbound.";
const std::string kMsgWinchester = "Phantoms are Winchester.";
const std::string kMsgComplete = "Strike complete.";
const std::string kMsgAbort = "Danger close, Phantoms abort.";
const std::string kMsgNegative = "Danger close, negative strike.";
const std::string kMsgBusy = "Phantoms off target, stand by.";

std::function<void(const std::string&)> g_phantomObserver;

void note(const std::string& s) {
    if (g_phantomObserver) g_phantomObserver(s);
}

std::string posText(const Vec3& p) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "(%d,%d,%d)", p.x >> 8, p.y >> 8, p.z >> 8);
    return buf;
}

s32 shl8u(s32 v) { return s32(u32(v) << 8); }

Team* phantomTeam() { return modernPhantomAvailable() ? team(ms().portPhantomGroup) : nullptr; }

// A living member of a SEAL or Friendly team (not aboard a craft) within
// `radius` units of p: the hold rule of the bombs (rule 3's circle test).
bool friendlyWithin(const Vec3& p, int radius) {
    const MissionState& S = ms();
    for (int ti = 0; ti < kMaxTeams && S.teams[ti]; ++ti) {
        const Team* t = S.teams[ti];
        if (t->type != TeamType::Seal && t->type != TeamType::Friendly) continue;
        for (int mi = 0; mi < 8 && t->members[mi]; ++mi) {
            const Unit* u = t->members[mi];
            if (unitDead(u) || !u->mover || (u->mover->flags & mover_flag::kAboard)) continue;
            if (geoDistance(p, u->body->pos) <= radius) return true;
        }
    }
    return false;
}

int dangerRadius() {
    const int r = weaponDef(kBombWeapon).blast_radius;
    return (r > 0 ? r : 0) + kPhantomDangerMargin;
}

// Private generator (no engine::rng() draw): a scatter offset in units.
s32 scatterUnits(PortPhantomState& st) {
    st.lcg = st.lcg * 1664525u + 1013904223u;
    return s32(int((st.lcg >> 16) % u32(2 * kPhantomScatter + 1)) - kPhantomScatter);
}

// Forward travel of a bomb released from `height` units: the fall takes
// sqrt(2 h / g) seconds at kBombSpeed (128 units from the run altitude).
int bombForward(int height) {
    if (height < 1) height = 1;
    const s32 v = s32(height) * 2 * 256 / kBombGravity;  // (16 x seconds)^2
    s32 t16 = 0;                                          // integer square root
    for (s32 bit = 1 << 15; bit; bit >>= 1)
        if ((t16 + bit) * (t16 + bit) <= v) t16 += bit;
    return int((s32(kBombSpeed) * t16) >> 4);
}

void setAltitude(Team* t, s16 target) {
    for (int k = 0; k < 8 && t->members[k]; ++k) t->members[k]->mover->target_height = target;
}

// One bomb from aircraft k: a T.31 round through the original's shot_fire /
// prj_fire (shot record, projectile, firing sound, noise), then its flight
// record turned into a level release at the aircraft's heading that falls
// under the original's gravity and explodes on landing.
void releaseBomb(Unit* u, int k, const Vec3& impact) {
    MissionState& S = ms();
    PortPhantomState& st = S.portPhantom;
    WeaponNode* w = st.bomb[k];
    if (!w) return;
    w->rounds = 1;  // the node is private: one bomb per run, spent by shot_fire
    Vec3 aim = st.aim[k];
    aim.y = 0;
    shotFire(u, nullptr, aim, 0, w, geoDistance(u->body->pos, aim), int(TargetKind::AimPoint));
    Projectile* p = nullptr;
    for (const ShotRec& s : S.shots)
        if (s.state == 0 && s.shooter == u && s.projectile && s.projectile->owner == u && s.projectile->launch_time == S.time)
            p = s.projectile;
    if (p) {
        p->muzzle_phase = 0;
        p->lifetime = kBombLifetime;
        FlightRec* f = p->flight;
        f->speed = f->target_speed = kBombSpeed;
        f->vertical_speed = 0;
        f->gravity = 1;
        f->bounces = 0;
        p->body->heading = u->body->heading;
        p->body->pitch = 0;
        p->body->roll = 0;
        if (const ModelDesc* m = model(kBombModel)) {
            p->body->model = m;
            p->base_model = m;
        }
    }
    st.round[k] = p;
    st.retargeted[k] = false;
    ++S.portStats.phantomBombs;
    note("release by T" + std::to_string(S.portPhantomGroup) + "." + std::to_string(k) + " at " + posText(u->body->pos) +
         " aim " + posText(aim) + " expected impact " + posText(impact) + (p ? "" : " (dud)"));
}

// The bombs in flight: once one has come down, its shot's blast centre moves
// to the impact point (shot_blast_victims measures from the record's target
// position), and a round stopped by a solid feature above the ground gets
// its explosion there.
void trackBombs(Team* t) {
    MissionState& S = ms();
    PortPhantomState& st = S.portPhantom;
    for (int k = 0; k < 2; ++k) {
        Projectile* p = st.round[k];
        if (!p) continue;
        if (!(p->state & prj_state::kInUse) || !t->members[k] || p->owner != t->members[k]) {
            st.round[k] = nullptr;
            continue;
        }
        if (st.retargeted[k] || !(p->hit & (prj_hit::kLanded | prj_hit::kObstacle | prj_hit::kUnit))) continue;
        if ((p->hit & prj_hit::kObstacle) && !p->fx_explosion) prjStartBurst(p, 0);
        Vec3 at = p->body->pos;
        at.y = 0;
        for (ShotRec& s : S.shots)
            if (s.state == 0 && s.projectile == p) s.target_pos = at;
        st.retargeted[k] = true;
        note("impact of T" + std::to_string(S.portPhantomGroup) + "." + std::to_string(k) + "'s bomb at " + posText(at));
    }
}

} // namespace

bool modernPhantomAvailable() { return modernGameplayOn() && ms().portPhantomGroup != 0xFF; }

bool modernIsPhantomGroup(int teamIndex) { return modernPhantomAvailable() && teamIndex == ms().portPhantomGroup; }
bool modernIsPhantomTeam(const Team* t) { return t && modernPhantomAvailable() && t == team(ms().portPhantomGroup); }

void modernSpawnPhantomFlight(s32 ix, s32 iz) {
    if (!modernGameplayOn()) return;
    MissionState& S = ms();
    PortPhantomState& st = S.portPhantom;
    st = PortPhantomState{};
    st.strikesLeft = kPhantomStrikes;
    // Like ent_spawn_support_craft(1), but in Column (the wingman trails the
    // leader over the marker), without an AI record and, below, without
    // brains or a loadout: the AI never sees the flight, the original's
    // craft update only steers and moves it.
    Team* t = entGroupCreate(TeamType::Aircraft, int(Formation::Column), int(FireOrder::AtWill),
                             int(CraftOrder::PortPhantom), 0);
    S.portPhantomGroup = S.teamCount - 1;
    const ModelDesc* f4 = model(kF4Model);
    for (int k = 0; k < 2; ++k) {
        Unit* u = entGroupAddUnit(t, 0x03, UnitClass::Aircraft, -1, ix + 600 * k, iz);
        if (f4) {
            u->model = f4;
            u->body->model = f4;
        }
        Mover* mv = u->mover;
        // Run mode like a cruising craft: the formation update copies the
        // leader's mode to the wingman and prj_fire re-applies the shooter's
        // every time, so the mode must carry the speed.
        mv->base_speed = kPhantomSpeed;
        evtSetMoveMode(u, int(MoveMode::Run));
        mv->speed = mv->target_speed;
        mv->turn_rate = kPhantomTurnRate;
        mv->height_high = kPhantomHighAlt;
        mv->height_low = kPhantomRunAlt;
        mv->height = mv->target_height = kPhantomHighAlt;
        mv->climb_rate = mv->descent_rate = kPhantomClimbRate;
        st.bomb[k] = S.weaponPool.alloc();
        entWeaponInit(st.bomb[k], kBombWeapon, 0);
    }
    // Parked far behind the insertion point (away from objective 1, like the
    // OV-10's push-back) at the ambient flyers' altitude, facing the objective.
    Unit* lead = t->members[0];
    entUnitPushBackFromObjective(lead, kPhantomParkDist);
    const int h = geoBearing(lead->body->pos, S.mci.objective[0].pos);
    st.parking = lead->body->pos;
    st.parking.y = shl8u(kPhantomHighAlt);
    for (int k = 0; k < 2 && t->members[k]; ++k) {
        Mover* mv = t->members[k]->mover;
        mv->heading = mv->desired_heading = s16(h);
        t->members[k]->body->heading = s16(h << 3);
        mv->destination = st.parking;
    }
    evtTeamSnapFormation(S.portPhantomGroup);
    for (int k = 0; k < 2 && t->members[k]; ++k) t->members[k]->body->pos.y = shl8u(kPhantomHighAlt);
}

int modernPhantomStrikeOrder() {
    Team* t = phantomTeam();
    if (!t || !t->members[0]) return -1;
    MissionState& S = ms();
    PortPhantomState& st = S.portPhantom;
    const int button = S.mapSelTeam == S.portPhantomGroup ? kPhantomButton : -1;
    if (st.strikesLeft <= 0) {
        msgShow(kMsgWinchester, 0x200);
        sfxRadioAck(0x2C);
        note("order refused: Winchester");
        return -1;
    }
    if (st.phase == Phase::Egress) {
        msgShow(kMsgBusy, 0x200);
        sfxRadioAck(0x2C);
        note("order refused: off target");
        return -1;
    }
    Vec3 marker = S.wpSupport;
    marker.y = 0;
    if (friendlyWithin(marker, dangerRadius())) {
        msgShow(kMsgNegative, 0x200);
        sfxRadioAck(0x2C);
        note("order refused: danger close at " + posText(marker));
        return -1;
    }
    // Like the aircraft attack: a failed call stops the order only with a
    // damaged radio (for an aircraft the call cannot fail otherwise).
    if (!radioCall(kMsgInbound, S.portPhantomGroup) && S.radioDamaged) return -1;
    st.phase = Phase::Inbound;
    st.marker = marker;
    st.aborted = false;
    st.counted = false;
    st.runStart = S.time;
    for (int k = 0; k < 2; ++k) {
        st.aim[k] = marker;
        st.aim[k].x = wrapAdd(st.aim[k].x, shl8u(scatterUnits(st)));
        st.aim[k].z = wrapAdd(st.aim[k].z, shl8u(scatterUnits(st)));
        st.passed[k] = false;
        st.lastDist[k] = -1;
        st.round[k] = nullptr;
        st.retargeted[k] = false;
    }
    t->members[0]->mover->destination = st.aim[0];
    setAltitude(t, kPhantomRunAlt);
    sfxRadioAck(0x31);
    evtForceCraftEngineSound();  // the jets' engine sound starts with the run (restarts every craft's, like leaving the map)
    note("strike ordered at " + posText(marker) + ", aim " + posText(st.aim[0]) + " / " + posText(st.aim[1]) +
         ", strikes left " + std::to_string(st.strikesLeft));
    return button;
}

void modernPhantomUpdate() {
    Team* t = phantomTeam();
    if (!t || !t->members[0]) return;
    MissionState& S = ms();
    PortPhantomState& st = S.portPhantom;
    Unit* lead = t->members[0];
    trackBombs(t);
    switch (st.phase) {
    case Phase::Idle:
        // Circling the parking point (the craft update steers at the destination).
        lead->mover->destination = st.parking;
        break;
    case Phase::Inbound: {
        lead->mover->destination = st.aim[0];
        for (int k = 0; k < 2 && t->members[k]; ++k) {
            Unit* u = t->members[k];
            if (st.passed[k]) continue;
            const int d = geoDistance(u->body->pos, st.aim[k]);
            const bool closest = st.lastDist[k] >= 0 && d > st.lastDist[k] && d < kPhantomPassDist;
            st.lastDist[k] = d;
            if (d > kPhantomReleaseDist && !closest) continue;
            st.passed[k] = true;
            if (st.aborted) continue;  // the leader held: the wingman keeps its bomb too
            Vec3 impact = u->body->pos;
            impact.y = 0;
            posMovePolar(shl8u(bombForward(u->mover->height)), 0, u->body->heading, impact);
            if (friendlyWithin(impact, dangerRadius())) {
                // Rule 3 for bombs: no release with friendlies near the impact point.
                st.aborted = true;
                ++S.portStats.phantomHolds;
                msgShow(kMsgAbort, 0x200);
                note("release held by T" + std::to_string(S.portPhantomGroup) + "." + std::to_string(k) +
                     ": friendlies within " + std::to_string(dangerRadius()) + " of " + posText(impact));
                continue;
            }
            if (!st.counted) {
                st.counted = true;
                st.firstRelease = S.time;
                --st.strikesLeft;
            }
            releaseBomb(u, k, impact);
        }
        const bool both = st.passed[0] && (st.passed[1] || !t->members[1]);
        if (both || wrapSub(S.time, st.runStart) > kPhantomRunTimeout) {
            st.phase = Phase::Egress;
            st.egressStart = S.time;
            Vec3 ahead = st.marker;
            posMovePolar(shl8u(kPhantomRunAhead), 0, lead->body->heading, ahead);
            lead->mover->destination = ahead;
            setAltitude(t, kPhantomHighAlt);
            note(std::string("egress") + (st.aborted ? " (aborted)" : "") + ", strikes left " + std::to_string(st.strikesLeft));
        }
        break;
    }
    case Phase::Egress: {
        // Over once the flight is clear of the marker and the bombs are down.
        const bool bombsDown = (!st.round[0] || st.retargeted[0]) && (!st.round[1] || st.retargeted[1]);
        if (geoDistance(lead->body->pos, st.marker) >= kPhantomEgressDist && wrapSub(S.time, st.egressStart) >= kPhantomEgressTicks &&
            (bombsDown || wrapSub(S.time, st.egressStart) >= kPhantomRunTimeout)) {
            st.phase = Phase::Idle;
            lead->mover->destination = st.parking;
            if (!st.aborted) msgShow(kMsgComplete, 0x200);
            note(std::string(st.aborted ? "run aborted" : "strike complete") + ", strikes left " + std::to_string(st.strikesLeft));
        }
        break;
    }
    }
}

int modernPhantomStrikesLeft() { return modernPhantomAvailable() ? ms().portPhantom.strikesLeft : 0; }
int modernPhantomPhase() { return modernPhantomAvailable() ? int(ms().portPhantom.phase) : 0; }

void setPhantomObserver(std::function<void(const std::string&)> fn) { g_phantomObserver = std::move(fn); }

} // namespace st::game::mission
