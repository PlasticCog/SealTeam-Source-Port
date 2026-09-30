// Module combat, player side: the tool keys (19ac:0934 item_use_tool, 0A3A
// item_cycle_tool), unit_turn (19ac:17A0) and the fire / grenade handling of
// field_view_keys (19ac:1A79, docs/re/seg_19ac.md 6.1-6.5) incl. the grenade
// aim box adjustments of field_pointer_motion (19ac:1816, 6.2).
#include "game/mission/combat.h"

#include "game/mission/craft.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/mission/world.h"

namespace st::game::mission {

namespace {

constexpr u16 kMsgRadioDamaged = 0x290;  // "Radio damaged."
constexpr u16 kMsgUsingThe = 0x29F;      // "Using the "
constexpr u16 kMsgUsingEnd = 0x2AA;      // "."
constexpr u16 kMsgOutOfRange = 0x503;    // "Out of Range."
constexpr int kWeaponM203 = 10;
constexpr int kWeaponSatchel = 12;
constexpr int kAimPointRange = 0x96;     // 150 units
constexpr s32 kAimPointDist = 0x9600;
constexpr int kHideBox = 3000;           // fx_place_aim_marker(side, dist - 3000) hides the box

void (*g_viewHeadingHook)(int) = nullptr;

// Target data of the current field_view_keys call (its locals at BP-0x1C,
// BP-0x14, BP-0x0E), set by playerViewPrologue.
struct FieldTarget {
    Unit* unit = nullptr;              // 5149:000E after the refresh (kind 0)
    WorldObject* structure = nullptr;  // same when it is a world object (kind 1)
    s16 range = kAimPointRange;        // 5149:0018, or 150 for the aim point
    s16 kind = 3;                      // 5149:0012 read before the refresh; 3 = aim point
};
FieldTarget g_ft;

int wtype(const WeaponNode* w) { return u8(w->type); }
bool alive(const Unit* u) { return !(u->status && (u->status->hit_mask & hit_bit::kKilled)); }

bool hasTarget() { return g_ft.unit || g_ft.structure; }
Unit* targetPtr() { return g_ft.unit ? g_ft.unit : reinterpret_cast<Unit*>(g_ft.structure); }
const Vec3& targetPos() { return g_ft.unit ? g_ft.unit->body->pos : g_ft.structure->body->pos; }

// Kind passed to shot_fire. Original quirk (19ac:1AA4): the kind is read
// before this frame's auto-target refresh, the pointer and range after it.
// When the refresh replaced a unit by a structure or back in the same frame,
// the original treats the record as the wrong type (memory corruption); the
// port then uses the kind the pointer really has.
int shotKind() {
    if (g_ft.unit && g_ft.kind == 1) return 0;
    if (g_ft.structure && g_ft.kind == 0) return 1;
    return g_ft.kind;
}

void hideAimBox() {
    MissionState& S = ms();
    fxPlaceAimMarker(S.grenadeAimSide, s16(S.grenadeAimDist - kHideBox));
}

void clampAimSide() {
    MissionState& S = ms();
    const s16 half = s16(S.grenadeAimDist >> 1);
    s16 v = S.grenadeAimSide;
    if (v < s16(-half)) v = s16(-half);
    if (half < v) v = half;
    S.grenadeAimSide = v;
}

} // namespace

void combatReset() {
    g_ft = FieldTarget{};
    // ai_reset_shot_slots (4a37:0035) also frees them at mission start.
    for (ShotRec& s : ms().shots) s.state = 1;
}

void setViewHeadingTurnHook(void (*fn)(int delta8)) { g_viewHeadingHook = fn; }

// ---------------------------------------------------------------------------
// Tools (19ac:0934, 0A3A)
// ---------------------------------------------------------------------------

void itemUseTool(Unit* u) {
    MissionState& S = ms();
    ItemNode* it = u ? u->items : nullptr;
    if (!it) return;
    bool used = false;
    switch (s8(it->type)) {
    case s8(ItemType::Radio):
        if (S.radioDamaged) msgShowDs(kMsgRadioDamaged, 0x200);
        break;
    case s8(ItemType::MedicalKit): {
        Unit* pm = pointMan();  // the kit always treats the Point Man himself
        used = medTreat(pm, pm) != 0;
        break;
    }
    case s8(ItemType::Phk):
        used = evtTakePrisonerPhk();
        break;
    default:
        break;
    }
    if (used) msgShow(dsText(kMsgUsingThe) + itemDef(u8(it->type)).long_name + dsText(kMsgUsingEnd), 0x100);
}

void itemCycleTool(Unit* u) {
    ItemNode* head = u->items;
    if (!head || !head->next) return;
    ItemNode* tail = head;
    while (tail->next) tail = tail->next;
    u->items = head->next;
    head->next = nullptr;
    tail->next = head;
}

// ---------------------------------------------------------------------------
// Turning (19ac:17A0)
// ---------------------------------------------------------------------------

void unitTurn(Unit* u, int d) {
    if (!u || !alive(u)) return;
    const s16 delta = s16(d << 3);
    u->body->heading = s16(angleWrap(s16(u->body->heading + delta)));
    if (u->mover) u->mover->heading = s16(u->body->heading >> 3);
    // g_view_heading (DS:CEAC) += the same delta, wrapped (owned by the loop).
    if (g_viewHeadingHook) g_viewHeadingHook(delta);
}

// ---------------------------------------------------------------------------
// field_view_keys (19ac:1A79)
// ---------------------------------------------------------------------------

void playerViewPrologue(int dx) {
    MissionState& S = ms();
    Unit* pm = pointMan();
    if (!pm) return;
    const TargetKind before = S.playerTarget.kind;
    if (S.autoTarget) {
        Ticks mark = 0;
        if (wrapSub(S.time, S.autotargetTime) >= 0x100) {
            S.autotargetTime = S.time;
            mark = S.time;
        }
        // Original quirk (19ac:1AEA): the periodic refresh tests the stored
        // time value, so it does not happen when g_time is 0.
        if (mark != 0 || dx != 0) tgtAcquire(pm, S.playerTarget, false);
    }
    g_ft = FieldTarget{};
    if (before == TargetKind::None) {
        // Aim point: 150 units ahead of the Point Man (written to 5149:0002).
        S.playerTarget.pos = pm->body->pos;
        posMovePolar(kAimPointDist, 0, pm->body->heading, S.playerTarget.pos);
        g_ft.range = kAimPointRange;
        g_ft.kind = 3;
        return;
    }
    if (S.playerTarget.kind == TargetKind::Unit) g_ft.unit = S.playerTarget.target.unit;
    else if (S.playerTarget.kind == TargetKind::Structure) g_ft.structure = S.playerTarget.target.structure;
    g_ft.range = S.playerTarget.range;
    g_ft.kind = s16(before);
}

void playerFire() {
    MissionState& S = ms();
    if (S.extracting) return;
    if (S.grenadeAiming) {
        playerCancelGrenadeAim();
        return;
    }
    Unit* pm = pointMan();
    if (!pm) return;
    WeaponNode* w = pm->loadout ? pm->loadout->primary : nullptr;
    hideAimBox();
    S.weaponInfoTime = wrapAdd(S.time, 0x200);
    const int kind = shotKind();
    if (w && w->rounds != 0 && S.time >= S.weaponReadyTime &&
        !(wtype(w) == kWeaponM203 && u16(w->m203_count) >= 3 && w->fire_mode == fire_mode::kLauncher)) {
        if (alive(pm)) {
            const Vec3& pos = hasTarget() ? targetPos() : S.playerTarget.pos;
            shotFire(pm, targetPtr(), pos, 0, w, g_ft.range, kind);
        }
        if (w->rounds == 0 || (wtype(w) == kWeaponM203 && w->m203_count == 3)) {
            evtStartReload(pm);
            wpnSetReloadTimer(w, S.weaponReadyTime);
            wpnReload(w);
        }
    }
    // Teammates join in "Fire at Target" (every 0x200 ticks).
    Ticks mark = 0;
    if (wrapSub(S.time, S.teamFireTime) >= 0x200) {
        S.teamFireTime = S.time;
        mark = S.time;
    }
    Team* t0 = team(0);
    if (mark == 0 || !t0) return;
    if (t0->fire_order == FireOrder::CeaseFire || t0->fire_order != FireOrder::AtTarget) return;
    if (!w || wtype(w) == kWeaponSatchel || !hasTarget()) return;
    const Vec3& pos = targetPos();
    for (int i = 1; i < 4; ++i) {
        // Original: members 1..3 are read without a NULL check (a smaller
        // team reads through NULL); the port skips missing members.
        Unit* m = t0->members[i];
        WeaponNode* mw = (m && m->loadout) ? m->loadout->primary : nullptr;
        if (!mw) continue;
        if (alive(m) && wtype(mw) != kWeaponSatchel && mw->rounds > 0)
            shotFire(m, targetPtr(), pos, 0, mw, g_ft.range, kind);
        // Original quirk (19ac:1FB1): also for dead members.
        if (mw->rounds == 0 && mw->reloads != 0) {
            evtStartReload(m);
            wpnReloadBoth(m);
        }
    }
}

void playerThrowGrenade() {
    MissionState& S = ms();
    if (S.extracting) return;
    Unit* pm = pointMan();
    if (!pm) return;
    S.grenadeInfoTime = wrapAdd(S.time, 0x200);
    // Original: the grenade slot is not checked; without one the original
    // reads through a NULL pointer. The port treats it as empty.
    WeaponNode* g = pm->loadout ? pm->loadout->secondary : nullptr;
    if (!hasTarget() && !S.grenadeAiming) {
        if (g && g->rounds > 0 && S.time >= S.grenadeReadyTime) {
            fxPlaceAimMarker(S.grenadeAimSide, S.grenadeAimDist);
            S.grenadeAiming = true;
        }
        return;
    }
    if (g && g->rounds != 0 && S.time >= S.grenadeReadyTime) {
        const int range = hasTarget() ? g_ft.range : S.grenadeAimDist;
        if (!wpnInRange(range, g)) {
            msgShowDs(kMsgOutOfRange, 0x100);
        } else if (alive(pm)) {
            const Vec3* pos;
            if (hasTarget()) pos = &targetPos();
            else if (S.grenadeAiming) pos = &fxPools().aimMarker->pos;
            else pos = &S.playerTarget.pos;
            shotFire(pm, targetPtr(), *pos, 0, g, range, shotKind());
            wpnSetReloadTimer(g, S.grenadeReadyTime);
            wpnReload(g);
        }
    }
    hideAimBox();
    S.grenadeAiming = false;
}

void playerAdjustGrenadeAim(int dSide, int dDist, bool pointer) {
    MissionState& S = ms();
    if (!S.grenadeAiming) return;
    if (dSide < 0) {
        const s16 lim = s16(-(S.grenadeAimDist >> 1));
        const s16 v = s16(S.grenadeAimSide - 6);
        S.grenadeAimSide = lim >= v ? lim : v;
        fxPlaceAimMarker(S.grenadeAimSide, S.grenadeAimDist);
    } else if (dSide > 0) {
        const s16 lim = s16(S.grenadeAimDist >> 1);
        const s16 v = s16(S.grenadeAimSide + 6);
        S.grenadeAimSide = v <= lim ? v : lim;
        fxPlaceAimMarker(S.grenadeAimSide, S.grenadeAimDist);
    }
    if (dDist > 0) {
        S.grenadeAimDist = s16(S.grenadeAimDist + 6);
        if (S.grenadeAimDist > 300) S.grenadeAimDist = 300;
        if (pointer) clampAimSide();  // 19ac:1A46 (a no-op when farther)
        fxPlaceAimMarker(S.grenadeAimSide, S.grenadeAimDist);
    } else if (dDist < 0) {
        const s16 lo = pointer ? 0x4B : 0x3C;  // pointer 19ac:1A3E, Down key 19ac:2381
        S.grenadeAimDist = s16(S.grenadeAimDist - 6);
        if (S.grenadeAimDist < lo) S.grenadeAimDist = lo;
        clampAimSide();
        fxPlaceAimMarker(S.grenadeAimSide, S.grenadeAimDist);
    }
}

void playerCancelGrenadeAim() {
    hideAimBox();
    ms().grenadeAiming = false;
}

} // namespace st::game::mission
