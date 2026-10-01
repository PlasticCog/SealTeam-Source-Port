// Port addition: the Enhanced "Modern gameplay" simulation rules (modern.h,
// docs/mission.md "Modern gameplay"). No function here draws from
// engine::rng(); every choice is made from the unit's own state.
#include "game/mission/modern.h"

#include "core/settings.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/people.h"
#include "game/mission/state.h"
#include "game/mission/wquery.h"

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

} // namespace st::game::mission
