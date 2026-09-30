#include "game/mission/wquery.h"

#include "engine/rng.h"
#include "game/mission/geo.h"
#include "game/mission/los.h"
#include "game/mission/state.h"
#include "game/mission/world.h"

namespace st::game::mission {

namespace {

WorldObject* worldObjectAt(int n) {
    auto& v = worldObjects();
    return (n >= 0 && n < int(v.size())) ? v[size_t(n)] : nullptr;
}

// Shared body of 1000:33CD / 1000:3503.
bool findMissionPoint(const Unit* u, WorldObject*& out, bool needAlive) {
    out = nullptr;
    const Vec3& p = u->body->pos;
    for (const MciObjective& o : ms().mci.objective) {
        if (u16(o.kind) != 3) continue;
        WorldObject* w = worldObjectAt(o.target_structure);
        if (!w) continue;
        if (geoDistance(p, w->body->pos) < 3000 && (!needAlive || w->hit_points > 0)) out = w;
    }
    return out != nullptr;
}

} // namespace

Obj3D* geoRayToObject(const Obj3D* from, const Vec3& to) {
    Obj3D* hit = losTrace(from, from->pos, to, 0x300, &ms().losHit, 0x1000, false, true, true);
    return isGroundHit(hit) ? nullptr : hit;
}

Obj3D* geoProbeAt(const Obj3D* o) {
    Vec3 to = o->pos;
    to.x = wrapAdd(to.x, 0x600);
    to.z = wrapAdd(to.z, 0x600);
    return losTrace(o, o->pos, to, 0x600, &ms().losHit, 0x0100, false, true, true);
}

WorldObject* wldObjectFromHit(const Obj3D* hit) {
    if (!hit || isGroundHit(hit)) return nullptr;
    const int k = objIndex(hit);
    if (k < 0 || k >= int(worldObjects().size())) return nullptr;
    return worldObjects()[size_t(k)];
}

bool wldFindMissionPoint(const Unit* u, WorldObject*& out) {
    if (findMissionPoint(u, out, false)) return true;
    out = worldObjectAt(0);
    return false;
}

bool wldFindLiveMissionPoint(const Unit* u, WorldObject*& out) {
    if (findMissionPoint(u, out, true)) return true;
    Unit* pm = pointMan();
    out = pm ? worldObjectAt(wldNearestObject(pm->body->pos, -1)) : nullptr;
    return false;
}

int wldNearestObject(const Vec3& pos, int kind) {
    int best = 24000;
    int found = -1;
    const auto& v = worldObjects();
    for (size_t i = 0; i < v.size(); ++i) {
        const WorldObject* w = v[i];
        const int k = int(w->kind);
        if (kind != -1 && k != kind && !(kind == 7 && k == 9)) continue;
        const int d = geoDistance(pos, w->body->pos);
        if (d < best) {
            best = d;
            found = int(i);
        }
    }
    return found;
}

void wldSetKind5Visible(bool on) {
    for (WorldObject* w : worldObjects()) {
        if (w->kind != TerrainKind::Clearing) continue;
        if (on) w->body->flags |= obj3d_flag::kLosTreeB;
        else w->body->flags &= u16(~obj3d_flag::kLosTreeB);
    }
}

bool wldProbeKind(Vec3& pos, int kind) {
    const bool kind5 = kind == 5;
    if (kind5) kind = -1;
    Obj3D* probe = fxPools().probe;
    probe->pos = pos;
    pos.y = 0;  // original: the caller's altitude is cleared after the copy
    if (kind5) wldSetKind5Visible(true);
    bool result = false;
    Obj3D* hit = geoProbeAt(probe);
    if (hit) {
        WorldObject* w = wldObjectFromHit(hit);
        if (w && (kind == -1 || int(w->kind) == kind)) result = true;
    }
    if (kind5) wldSetKind5Visible(false);
    return result;
}

void geoRandomOffsetDry(Vec3& pos, int lo, int hi) {
    s16 l = s16(lo), h = s16(hi);
    if (l < 1) l = 1;
    if (l < h) {} else h = l;
    auto pick = [&]() -> s16 {
        s16 v = s16(engine::rng().range(s16(h * 2)) - h);
        if (v < 0) {
            if (s16(-l) <= v) v = s16(-l);
        } else if (l >= v) {
            v = l;
        }
        return v;
    };
    const s16 ox = pick();
    pos.x = wrapAdd(pos.x, s32(ox) * 256);
    const s16 oz = pick();
    pos.z = wrapAdd(pos.z, s32(oz) * 256);
    if (wldProbeKind(pos, 7) || wldProbeKind(pos, 9)) {
        pos.x = wrapSub(pos.x, s32(s16(ox * 2)) * 256);
        pos.z = wrapSub(pos.z, s32(s16(oz * 2)) * 256);
        if (wldProbeKind(pos, 7) || wldProbeKind(pos, 9)) {
            // Original: adds (2*ox + oz) and (2*oz + ox), not a clean rotation.
            pos.x = wrapAdd(pos.x, (s32(s16(ox * 2)) + s32(oz)) * 256);
            pos.z = wrapAdd(pos.z, (s32(s16(oz * 2)) + s32(ox)) * 256);
        }
    }
}

int wldGroundType(Vec3* pos) {
    if (!pos) return 1;
    if (!wldProbeKind(*pos, 7) && !wldProbeKind(*pos, 8) && !wldProbeKind(*pos, 9)) return 1;
    return 2;
}

int wldLosCover(const Unit* u, const Vec3& target) {
    ms().losCalls++;
    if (!u || !u->body) return 0;
    int total = 0;
    const Obj3D* from = u->body;
    const Obj3D* last = nullptr;
    while (from && total <= 99) {
        Obj3D* hit = geoRayToObject(from, target);
        if (!hit || hit == last) return total;
        last = hit;
        from = hit;
        WorldObject* w = wldObjectFromHit(hit);
        if (!w) continue;
        if (geoDistance(u->body->pos, target) <= 5) return total;
        if (w->body->pos.x == target.x && w->body->pos.z == target.z) return total;
        total += w->cover;
    }
    return total;
}

Unit* wldProbeUnit(const Obj3D* o) {
    Obj3D* hit = geoProbeAt(o);
    if (!hit || isGroundHit(hit)) return nullptr;
    return unitOfBody(hit);
}

WorldObject* wldProbeSolid(const Obj3D* o) {
    Obj3D* hit = geoProbeAt(o);
    if (!hit) return nullptr;
    WorldObject* w = wldObjectFromHit(hit);
    if (!w) return nullptr;
    const u16 k = u16(w->kind);
    if (w->height < 0x7F && k != 7 && k != 9 && k != 8 && k != 0x11 && k != 10 && k != 0xB) return nullptr;
    return w;
}

bool wldObjectDestroyed(int n) {
    WorldObject* w = worldObjectAt(n);
    return w && w->hit_points < 1;
}

bool wldObjectiveDone(int n) {
    WorldObject* w = worldObjectAt(n);
    return w && (w->flags & 1) && (w->hit_points < 1 || ms().enemyKia > 4);
}

bool wldObjectFlag0(int n) {
    WorldObject* w = worldObjectAt(n);
    return w && (w->flags & 1);
}

int wldCountDestroyed() {
    int n = 0;
    for (const WorldObject* w : worldObjects())
        if (w->kind == TerrainKind::Structure && w->hit_points < 1) ++n;
    return n;
}

bool wldIsCamp() { return ms().worldIndex > 0x1B; }

int todMissionMinutes() {
    const MissionState& S = ms();
    return S.todMinute + ((S.todHour - s16(S.mci.start_hour)) * 60 - s16(S.mci.start_minute)) + 1;
}

int objRadiusOrDefault(const ModelDesc* m) {
    if (!m) return 150;
    return s16(m->radius_world >> 8);
}

} // namespace st::game::mission
