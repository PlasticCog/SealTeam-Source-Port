// Module combat: weapon slots, shots, hit rolls, damage and wounds, medical
// aid (19ac:0C21, 16B5..16FD, 6CCA..89C3), damage application, scripted
// explosions, targeting and inventory of segment 1000 (1000:0D0B, 18A1,
// 50B7..521C, 6163, 6225, 6990, 69E7, 8B64, 8BAA).
// docs/re/seg_19ac.md 6.6 and 12, docs/re/seg_1000.md 9.3, 9.4, 13, 18.
//
// The tools, turning and the player's field-view actions are in
// combat_player.cpp.
#include "game/mission/combat.h"

#include "engine/rng.h"
#include "game/mission/ai.h"
#include "game/mission/build.h"
#include "game/mission/craft.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/sfx.h"
#include "game/mission/state.h"
#include "game/mission/world.h"
#include "game/mission/wquery.h"

#include <array>

namespace st::game::mission {

namespace {

constexpr int kVictims = 16;                 // stack array of shot_update_all
constexpr u16 kMsgRadioDamagedLight = 0x33FC;// "The radio is damaged."
constexpr u16 kMsgRadioDamagedHeavy = 0x3412;// same text, second copy
constexpr int kWeaponM16 = 1;                // M203 rifle mode borrows the M16's ranges
constexpr int kWeaponM203 = 10;
constexpr int kWeaponSatchel = 12;

using Victims = std::array<ShotVictim, kVictims>;

int rnd(int n) { return engine::rng().range(n); }

// Weapon slot type as the original indexes the table (unsigned byte).
int wtype(const WeaponNode* w) { return u8(w->type); }
const WeaponDef& wdef(const WeaponNode* w) { return weaponDef(wtype(w)); }
// Low byte of the table's fire-mode word (the code compares bytes).
u8 wmodes(const WeaponNode* w) { return u8(wdef(w).fire_modes); }

int teamType(const Team* t) { return t ? int(t->type) : -1; }
bool craftType(int tt) { return tt == 1 || tt == 2 || tt == 3; }
bool enemyType(int tt) { return tt == 4 || tt == 5; }
// Team type through the unit's AI record (unit+0x12 -> +0x68), as the combat
// code reads it. Original quirk: the Point Man has no AI record; the original
// then reads the interrupt vector table through a NULL pointer (almost
// certainly not an enemy/craft type); the port answers "no team" (-1).
int brainTeamType(const Unit* u) { return (u && u->brain) ? teamType(u->brain->team) : -1; }
bool isDead(const Unit* u) { return u->status && (u->status->hit_mask & hit_bit::kKilled); }

// 16-bit helpers.
s16 abs16(s16 v) { return s16(v < 0 ? -v : v); }
s32 shl8(int v) { return s32(u32(s32(s16(v))) << 8); }

// The original keeps units and world objects in the same far-pointer fields.
// A kind-1 pointer is only used as a WorldObject when it really is one of the
// world objects (see shotAimedHitRoll).
WorldObject* findWorldObject(const void* p) {
    if (!p) return nullptr;
    for (WorldObject* w : worldObjects())
        if (static_cast<const void*>(w) == p) return w;
    return nullptr;
}

Obj3D* recBody(const TargetRec& r) {
    if (r.kind == TargetKind::Unit) return r.target.unit ? r.target.unit->body : nullptr;
    if (r.kind == TargetKind::Structure) return r.target.structure ? r.target.structure->body : nullptr;
    return nullptr;
}

const void* recPtr(const TargetRec& r) { return r.target.unit; }

// ---- Shot resolution helpers (19ac:7604..7F57, 84CD, 8524) -----------------------

// 19ac:7604 shot_throw_bonus: range bonuses of thrown grenades (rate 0x10) by
// the thrower's personnel byte +9.
void shotThrowBonus(const ShotRec& s, int& a, int& b) {
    a = 0;
    b = 0;
    if (s.fire_mode != fire_mode::kThrow) return;
    const u8 v = s.shooter->status->strength;
    if (v >= 0x3C) {
        a = 0x15;
        b = 0x36;
    } else if (v > 0x1D) {
        a = 9;
        b = 0x1B;
    }
}

// 19ac:7660 shot_range_bands: short and medium range (+ bonuses); the M203
// in rifle mode uses the M16's medium / maximum ranges (no bonus).
void shotRangeBands(const ShotRec& s, int& shortR, int& mediumR, int a, int b) {
    if (s.weapon == kWeaponM203 && s.fire_mode != fire_mode::kLauncher) {
        mediumR = weaponDef(kWeaponM16).range_max;
        shortR = weaponDef(kWeaponM16).range_medium;
    } else {
        const WeaponDef& d = weaponDef(s.weapon);
        mediumR = s16(d.range_medium + b);
        shortR = s16(d.range_short + a);
    }
}

// 19ac:7C8F shot_shooter_skill: personnel byte selected by the weapon class
// (jump table 19ac:7CB2, classes 0..19; others 0).
int shotShooterSkill(const ShotRec& s) {
    const u16 cls = u16(weaponDef(s.weapon).wclass);
    const u8* sk = s.shooter->status->skill;
    switch (cls) {
    case 0: case 18: return sk[1];
    case 1: case 2: case 4: case 5: case 9: return sk[0];
    case 3: case 6: case 7: case 8: case 17: case 19: return sk[4];
    case 15: return sk[3];
    case 16: return 5;
    default: return 0;  // 10..14 and > 19
    }
}

// 19ac:7D31 shot_aimed_hit_roll: fills the victim entry (the obstacle hit on
// the way, else the target) and rolls the hit.
bool shotAimedHitRoll(ShotRec& s, ShotVictim& v) {
    int chance = shotShooterSkill(s);
    int a, b, shortR, mediumR;
    shotThrowBonus(s, a, b);
    shotRangeBands(s, shortR, mediumR, a, b);
    if (s.range >= s32(s16(mediumR))) {
        chance -= 20;
        v.band = 2;
    } else if (s.range >= s32(s16(shortR))) {
        chance -= 10;
        v.band = 1;
    } else {
        v.band = 0;
    }
    // Original: the projectile pointer is not checked (a failed prj_fire leaves
    // NULL and the test reads the interrupt vector table); the port then uses
    // the target.
    const Projectile* p = s.projectile;
    const void* obstacle = p ? static_cast<const void*>(p->last_hit.unit) : nullptr;
    if (obstacle && obstacle != static_cast<const void*>(s.target) && (p->hit & prj_hit::kObstacle)) {
        v.who.structure = findWorldObject(obstacle);
        v.kind = u8(TargetKind::Structure);
    } else if (s.target_kind == TargetKind::Structure) {
        // Original quirk (2dbd snipe order): a unit fired at with kind 1 is
        // then damaged as a world object, which corrupts the unit's record in
        // the original. The port applies structure damage only to real world
        // objects; a unit given kind 1 takes nothing.
        v.who.structure = findWorldObject(s.target);
        v.kind = u8(TargetKind::Structure);
    } else {
        v.who.unit = s.target;
        v.kind = u8(s16(s.target_kind));
    }
    const int leg = medLegLevel(s.shooter->status);
    if (leg == 2) chance -= 10;
    if (leg == 1) chance -= 5;
    if (s.cover >= 0x4B) chance -= 30;
    else if (s.cover >= 0x32) chance -= 10;
    else if (s.cover >= 0x19) chance -= 5;
    if (s.fire_mode == fire_mode::kSingle) chance -= 5;
    else if (s.fire_mode == fire_mode::kFull) chance += 15;
    if (s.shooter_posture == 2) chance += 10;
    else if (s.shooter_posture == 1) chance += 5;
    if (s.shooter_move == 1) chance -= 5;
    else if (s.shooter_move == 2) chance -= 10;
    if (s.target_move == 1) chance -= 2;
    else if (s.target_move == 2) chance -= 5;
    if (s.target_posture == 2) chance -= 10;
    else if (s.target_posture == 1) chance += 5;
    const u8 size = s.shooter->status->size;
    if (size >= 0x3C) chance += 30;
    else if (size >= 0x1E) chance += 10;
    if (chance > 0x5A) chance = 0x5A;
    if (chance < 0) chance = 0;
    return rnd(100) <= chance;
}

// 19ac:76A8 shot_blast_victims: members of teams whose leader is within 16 R
// of the impact that lie within R (band 1 beyond R/2; kind 2 for craft), then
// the world objects whose |distance - radius| <= R (kind 1, band 0).
bool shotBlastVictims(const ShotRec& s, Victims& v) {
    MissionState& S = ms();
    const s16 r = weaponDef(s.weapon).blast_radius;
    int n = 0;
    for (int ti = 0; n < kVictims && ti < kMaxTeams && S.teams[ti]; ++ti) {
        Team* t = S.teams[ti];
        if (!t->members[0]) continue;
        if (geoDistance(s.target_pos, t->members[0]->body->pos) > s16(r << 4)) continue;
        for (int m = 0; n < kVictims && m < 8 && t->members[m]; ++m) {
            Unit* u = t->members[m];
            const int d = geoDistance(s.target_pos, u->body->pos);
            if (d > r) continue;
            v[n].who.unit = u;
            v[n].kind = craftType(teamType(t)) ? u8(TargetKind::Craft) : u8(TargetKind::Unit);
            v[n].band = (r / 2 < d) ? 1 : 0;
            ++n;
        }
    }
    for (WorldObject* w : worldObjects()) {
        if (n >= kVictims) break;
        const int radius = objRadiusOrDefault(w->model);
        const int d = geoDistance(s.target_pos, w->body->pos);
        if (abs16(s16(d - radius)) > r) continue;
        v[n].who.structure = w;
        v[n].band = 0;
        v[n].kind = u8(TargetKind::Structure);
        ++n;
    }
    return n > 0;
}

// 19ac:78B8 shot_line_victims: untargeted bullet. Members of teams whose
// leader is within the maximum range of the aim point, inside the 8-degree
// cone of the shooter, with line of fire (cover < 90) and not farther than
// the last accepted one are appended (band 2); then the world objects.
bool shotLineVictims(const ShotRec& s, Victims& v) {
    MissionState& S = ms();
    const s16 maxR = weaponDef(s.weapon).range_max;
    const int heading = abs16(s16(s.shooter->body->heading >> 3));
    s16 limit = s16(maxR + 1);
    int n = 0;
    // Original quirk: the structure pass compares the last member distance
    // computed by the team pass instead of the object's own distance; when no
    // member distance was computed that local is uninitialised (the port: 0).
    s16 lastD = 0;
    for (int ti = 0; n < kVictims && ti < kMaxTeams && S.teams[ti]; ++ti) {
        Team* t = S.teams[ti];
        if (!t->members[0]) continue;
        if (maxR < geoDistance(s.target_pos, t->members[0]->body->pos)) continue;
        for (int m = 0; n < kVictims && m < 8 && t->members[m]; ++m) {
            Unit* u = t->members[m];
            lastD = s16(geoDistance(s.target_pos, u->body->pos));
            if (maxR < lastD) continue;
            if (u == s.shooter) continue;
            if (!geoInFov(&s.shooter_pos, 8, heading, &u->body->pos)) continue;
            if (lastD > limit) continue;
            if (wldLosCover(s.shooter, u->body->pos) >= 0x5A) continue;
            v[n].who.unit = u;
            v[n].kind = craftType(teamType(t)) ? u8(TargetKind::Craft) : u8(TargetKind::Unit);
            v[n].band = 2;
            ++n;
            limit = lastD;
        }
    }
    limit = s16(maxR + 1);
    for (WorldObject* w : worldObjects()) {
        if (n >= kVictims) break;
        if (maxR < geoDistance(s.target_pos, w->body->pos)) continue;
        if (!geoInFov(&s.shooter_pos, 8, heading, &w->body->pos)) continue;
        if (lastD > limit) continue;
        v[n].who.structure = w;
        v[n].kind = u8(TargetKind::Structure);
        v[n].band = 2;
        ++n;
        limit = lastD;
    }
    return n > 0;
}

bool bulletShot(const ShotRec& s) {
    return weaponDef(s.weapon).blast_radius <= 0 || (s.weapon == kWeaponM203 && s.fire_mode != fire_mode::kLauncher);
}

// 19ac:84CD shot_collect_victims.
bool shotCollectVictims(ShotRec& s, Victims& v) {
    if (!bulletShot(s)) return shotBlastVictims(s, v);
    if (!s.target) return shotLineVictims(s, v);
    return shotAimedHitRoll(s, v[0]);
}

// 19ac:8524 shot_clear_victims.
void shotClearVictims(Victims& v) {
    for (ShotVictim& e : v) {
        e.who.unit = nullptr;
        e.kind = u8(TargetKind::AimPoint);
    }
}

// Damage to a non-person victim entry (world object or craft).
void damageOther(const ShotVictim& e, int damage) {
    if (e.kind == u8(TargetKind::Structure)) dmgApply(nullptr, e.who.structure, 0, damage, 1);
    else dmgApply(e.who.unit, nullptr, 0, damage, e.kind);
}

// 19ac:7FFC shot_apply_damage.
void shotApplyDamage(const ShotRec& s, Victims& v) {
    MissionState& S = ms();
    const DifficultyOptions& o = S.opt;
    const Unit* pm = pointMan();
    const bool sealShooter = teamType(s.shooter->team) == 0;
    if (bulletShot(s)) {
        for (ShotVictim& e : v) {
            if (e.kind == u8(TargetKind::Unit) && e.who.unit) {
                Unit* u = e.who.unit;
                int roll = rnd(100);
                if (u == pm && o.player_wounds == 1) {
                    roll -= 15;
                    if (roll < 0) roll = 0;
                }
                const bool enemy = enemyType(brainTeamType(u));
                if (enemy && s.fire_mode == fire_mode::kFull) {
                    roll += 0x28;
                    if (roll > 100) roll = 100;
                }
                if (enemy && o.enemy_wounds == 1) {
                    roll += 15;
                    if (roll > 100) roll = 100;
                }
                int wound = s16(woundScan(e.band * 0x11, roll));
                if (u == pm && o.player_wounds == 0 && wound != -1) wound = 0;
                if (enemy && o.enemy_wounds == 0) wound = 0x4000;
                if (wound != -1 && wound > 0 && sealShooter && s.rounds != 0 && !isDead(u))
                    S.stats.roundsHit = s16(S.stats.roundsHit + rnd(s.rounds) + 1);
                dmgApply(u, nullptr, wound, 0, e.kind);
            } else if (e.kind != u8(TargetKind::AimPoint) && e.who.unit) {
                damageOther(e, s.rounds * 2);
            }
        }
        return;
    }
    const int wclass = int(weaponDef(s.weapon).wclass);
    for (ShotVictim& e : v) {
        if (e.kind == u8(TargetKind::Unit) && e.who.unit) {
            Unit* u = e.who.unit;
            int roll = rnd(100);
            if (u == pm && o.player_wounds == 1) {
                roll -= 15;
                if (roll < 0) roll = 0;
            }
            const bool enemy = enemyType(brainTeamType(u));
            if (enemy && o.enemy_wounds == 1) roll += 0x19;
            if (enemy) roll += 10;
            if (roll > 100) roll = 100;
            if (wclass == 12) roll = 0;
            if (wclass == 11) roll = 0;
            if (wclass == 14 || wclass == 13) roll = 1;
            int wound = s16(woundScan(kWoundBlast0 + e.band * 0x11, roll));
            if (wound != -1 && wound > 0 && sealShooter && s.fire_mode != fire_mode::kLauncher &&
                s.weapon != kWeaponSatchel)
                S.stats.grenadeHits = s16(S.stats.grenadeHits + 1);
            if (u == pm && o.player_wounds == 0 && wound != -1) wound = 0;
            if (enemy && o.enemy_wounds == 0) wound = 0x4000;
            dmgApply(u, nullptr, wound, 0, e.kind);
        } else if (e.kind != u8(TargetKind::AimPoint) && e.who.unit) {
            damageOther(e, u8(weaponDef(s.weapon).structure_damage));
        }
    }
}

// Shared part of dmg_apply's alertness updates (AI record +0xCE, flag 0x10).
void suppress(Unit* u, int base) {
    Brain* b = u->brain;
    const s16 add = s16(rnd(0xA00) + base);
    b->suppress_time = s16(b->suppress_time + add);
    b->flags |= brain_flag::kSuppressed;
}

void radioDamage(Unit* u, int chance, s32 lifetime, u16 msgOff) {
    MissionState& S = ms();
    S.radioDamaged = rnd(100) < chance;
    if (!S.radioDamaged) return;
    sfxPlay(0x1D, lifetime, &u->body->pos, 1, nullptr);
    msgShowDs(msgOff, 0x200);
    S.radioRepairTime = wrapAdd(S.time, 0x1E00);
}

void dmgApplyUnit(Unit* u, int hitBits) {
    MissionState& S = ms();
    if (!u || !u->status) return;
    Status* st = u->status;
    if (st->hit_mask & hit_bit::kKilled) return;
    const s16 bits = s16(hitBits);
    if (bits != -1) st->hit_mask = u16(st->hit_mask | u16(bits));
    const bool isPm = u == pointMan();
    if (bits == 0) {
        if (u->brain) suppress(u, 0x500);
    } else if (bits > 0) {
        switch (bits) {
        case 0x0001: case 0x0002: case 0x0010: case 0x0020: case 0x0100:
            st->light_wounds = u8(st->light_wounds + 1);
            // Original quirk (1000:5302): a random number is drawn and dropped.
            if (st->light_wounds > 4) (void)rnd(100);
            if (isPm && st->heavy_wounds > 1 && !S.radioDamaged) radioDamage(u, 0x19, 0x80, kMsgRadioDamagedLight);
            if (u->brain && enemyType(teamType(u->brain->team))) suppress(u, 0x500);
            break;
        case 0x0004: case 0x0008: case 0x0040: case 0x0080: case 0x0200: case 0x0400: case 0x0800:
        case 0x1000: case 0x2000:
            st->heavy_wounds = u8(st->heavy_wounds + 1);
            if (st->heavy_wounds > 2) {
                // Original quirk (1000:53F2): the draw and (heavy-1)*15 are computed and dropped.
                (void)rnd(100);
            } else {
                if (st->heavy_wounds > 1) {
                    st->bleeding = 1;
                    st->bleed_time = s16(rnd(0x2C00) + 0x2C00);
                }
                if (isPm && st->heavy_wounds > 1 && !S.radioDamaged)
                    radioDamage(u, 0x32, 0x100, kMsgRadioDamagedHeavy);
            }
            if (u->brain && enemyType(teamType(u->brain->team))) suppress(u, 0x1400);
            break;
        default:
            break;  // 0x4000 (killed) and combinations: no counters
        }
    }
    if (bits == -1) {
        sfxPlay(0x0C, 0x100, &u->body->pos, 1, nullptr);
        return;
    }
    if (!(st->hit_mask & hit_bit::kKilled)) {
        if (bits == 0) {
            sfxPlay(0x0C, 0x100, &u->body->pos, 1, nullptr);
            evtUnitSuppressed(u);
        } else {
            sfxPlay(0x0B, 0x100, &u->body->pos, 1, nullptr);
            evtUnitWounded(u);
        }
        return;
    }
    evtUnitKilled(u);
    // Original: the target is dropped only for units with an AI record (not
    // for the Point Man).
    if (!u->brain) return;
    aiMemberDeactivate(u);
    tgtClear();
}

void dmgApplyObject(WorldObject* w, int damage) {
    MissionState& S = ms();
    if (!w || w->cover == 0 || w->hit_points < 1) return;
    w->hit_points = s16(w->hit_points - damage);
    if (w->hit_points > 0) return;
    if (S.hasSpecialObject && msnIsObjectiveStructure(w)) {
        // Tunnel world: the destroyed objective releases a reserve group at
        // the nearest tunnel (kind 0x12).
        const int n = wldNearestObject(w->body->pos, 0x12);
        const int g = aiFindReserveGroup();
        if (n == -1 || g == -1) return;
        WorldObject* tunnel = worldObjects()[size_t(n)];
        tunnel->body->pitch = 1;
        Team* t = team(g);
        aiGroupDeploy(t, &tunnel->body->pos);
        if (t && t->members[0] && t->members[0]->mover) t->members[0]->mover->height = -12;
        w->body->model = model(0x7A40);
        w->cover = 0x19;
        w->height = 0x1F;
        return;
    }
    // The destroyed shape replaces the model of the 3D instance (+2 -> +0);
    // the world object's own model word is left alone.
    w->body->model = wldDestroyedShape(w);
    w->cover = 0x19;
    w->height = 0x1F;
    sfxPlay(0x11, 0x100, &w->body->pos, 1, nullptr);
    if (w->kind == TerrainKind::Structure) fxDestroyStructure(w);
    tgtClear();
}

} // namespace

// ---------------------------------------------------------------------------
// Weapon slots (19ac)
// ---------------------------------------------------------------------------

int wpnNextRof(const WeaponNode* w) {
    const u8 allowed = wmodes(w);
    const u16 next = u16(u16(w->fire_mode) << 1);
    if (next & allowed) return u8(next);
    u8 r = allowed & fire_mode::kSingle;
    if (!r) r = allowed & fire_mode::kSemi;
    if (!r) r = allowed & fire_mode::kFull;
    if (!r) r = allowed & fire_mode::kLauncher;
    return r;
}

void wpnSelectNextWeapon(Unit* u) {
    if (!u->loadout) return;
    // Original (19ac:16B5) loops until a non-grenade item (or the satchel) is
    // current and never ends for a list of grenades only; the port stops
    // after one pass over the list.
    int len = 0;
    for (WeaponNode* n = u->loadout->list; n; n = n->next) ++len;
    for (int i = 0; i <= len; ++i) {
        WeaponNode* p = wpnNextItem(u);
        u->loadout->primary = p;
        if (!p) return;
        if (wmodes(p) != fire_mode::kThrow) return;
        if (wtype(p) == kWeaponSatchel) return;
    }
}

void wpnSelectNextGrenade(Unit* u) {
    if (!u->loadout) return;
    Loadout* l = u->loadout;
    WeaponNode* saved = l->primary;
    l->primary = l->secondary;
    int len = 0;
    for (WeaponNode* n = l->list; n; n = n->next) ++len;
    WeaponNode* p = nullptr;
    // Ends at the next grenade-class item (not the satchel) or back at the
    // old one; the pass limit only matters when the old slot is not in the
    // list (endless in the original).
    for (int i = 0; i <= len; ++i) {
        p = wpnNextItem(u);
        l->primary = p;
        if (l->secondary == p || !p) break;
        if (wmodes(p) == fire_mode::kThrow && wtype(p) != kWeaponSatchel) break;
    }
    l->secondary = p;
    l->primary = saved;
}

bool wpnInRange(int dist, const WeaponNode* w) { return !(wdef(w).range_max < dist); }

int wpnBestRange(const Unit* u) {
    // Original: no NULL checks; a unit without a grenade slot reads the weapon
    // type through a NULL far pointer (interrupt vector table). The port
    // uses the slot that exists.
    const WeaponNode* a = u->loadout ? u->loadout->primary : nullptr;
    const WeaponNode* b = u->loadout ? u->loadout->secondary : nullptr;
    if (!a && !b) return 0;
    if (!b) return wdef(a).range_max;
    if (!a) return wdef(b).range_max;
    return (wdef(a).range_max < wdef(b).range_max) ? wdef(b).range_max : wdef(a).range_max;
}

bool wpnBestRangeBelow(const Unit* u, int n) { return wpnBestRange(u) < n; }

bool wpnHasRange(const Unit* u, int n) {
    for (const WeaponNode* w = u->loadout ? u->loadout->list : nullptr; w; w = w->next)
        if (n <= wdef(w).range_max) return true;
    return false;
}

void wpnSelectForRange(Unit* u, int n) {
    for (WeaponNode* w = u->loadout ? u->loadout->list : nullptr; w; w = w->next) {
        if (wmodes(w) != fire_mode::kThrow && n <= wdef(w).range_max && s16(w->rounds + w->reloads) != 0)
            u->loadout->primary = w;
    }
}

bool wpnSelectLongest(Unit* u) {
    if (!u->loadout || !u->loadout->list) return false;
    int best = 0;
    WeaponNode* sel = nullptr;
    for (WeaponNode* w = u->loadout->list; w; w = w->next) {
        if (s16(w->rounds + w->reloads) != 0 && best < wdef(w).range_max && w != u->loadout->secondary) {
            best = wdef(w).range_max;
            sel = w;
        }
    }
    if (!sel) return false;
    u->loadout->primary = sel;
    return true;
}

WeaponNode* wpnNextItem(const Unit* u) {
    const Loadout* l = u->loadout;
    if (!l || !l->list || !l->primary) return nullptr;
    return l->primary->next ? l->primary->next : l->list;
}

void wpnReload(WeaponNode* w) {
    if (!w) return;
    if (ms().opt.ammo == 1) {
        if (w->reloads == 0) return;
        w->rounds = wdef(w).magazine;
        w->reloads = s16(w->reloads - 1);
    } else {
        w->rounds = wdef(w).magazine;
    }
    w->m203_count = 0;
}

void wpnReloadBoth(Unit* u) {
    if (!u->loadout) return;
    wpnReload(u->loadout->primary);
    wpnReload(u->loadout->secondary);
}

void wpnSpendRounds(WeaponNode* w, int n, int rof) {
    if (u16(w->rounds) < u16(n)) w->rounds = 0;
    else w->rounds = s16(w->rounds - n);
    if (u8(rof) == fire_mode::kLauncher && wtype(w) == kWeaponM203) w->m203_count = s16(w->m203_count + 1);
}

int wpnAiPickRof(const WeaponNode* w) {
    const u8 m = wmodes(w);
    switch (m) {
    case 0x20: return 0x20;
    case 1: return 1;
    case 3: return rnd(20) >= 0x11 ? 1 : 2;
    case 4: return 4;
    case 6: return rnd(20) >= 0x11 ? 4 : 2;
    case 7: {
        const int r = rnd(20);
        if (r < 3) return 4;
        return r >= 0x11 ? 1 : 2;
    }
    case 8: return 8;
    case 0xF: {
        const int r = rnd(20);
        if (r < 3) return 4;
        if (r < 0xB) return 2;
        if (r >= 0x11) return 1;
        return u16(w->m203_count) >= 3 ? 2 : 8;
    }
    case 0x10: return 0x10;
    default: return 1;  // also every value above 0x20
    }
}

int wpnGetMags(const WeaponNode* w) { return w ? w->reloads : 0; }
int wpnGetRounds(const WeaponNode* w) { return w ? w->rounds : 0; }

int wpnRangeBand(const WeaponNode* w, int range) {
    const WeaponDef& d = wdef(w);
    const s16 t[3] = {d.range_short, d.range_medium, d.range_max};
    int n = 0;
    while (n < 3 && t[n] <= range) ++n;
    return n;
}

void wpnSetReloadTimer(const WeaponNode* w, Ticks& end) {
    if (w->reloads == 0) return;
    const MissionState& S = ms();
    end = wrapAdd(S.time, S.opt.reload_time == 1 ? s32(wdef(w).reload_ticks) : 0x40);
}

// ---------------------------------------------------------------------------
// Shots (19ac)
// ---------------------------------------------------------------------------

int shotFindFree() {
    int i = 0;
    while (i < kShots && ms().shots[i].state == 0) ++i;
    return i == kShots ? -1 : i;
}

void shotFire(Unit* shooter, Unit* target, const Vec3& pos, int cover, WeaponNode* w, int range, int targetKind) {
    MissionState& S = ms();
    const int slot = shotFindFree();
    if (slot == -1) return;
    if (!shooter || !w) return;  // the original has no check (garbage read)
    ShotRec& s = S.shots[slot];
    const int r = rnd(100);
    int jam = u8(wdef(w).jam) >> 1;
    if (jam < 1) jam = 1;
    if (r <= jam) {
        // Jammed: only the (dud) trajectory is recorded; the record stays free.
        w->jammed = 1;
        wpnSpendRounds(w, 1, w->fire_mode);
        s.projectile = prjFire(shooter, w, pos, target, -1, range);
    } else {
        const u8 rof = (shooter == pointMan()) ? w->fire_mode : u8(wpnAiPickRof(w));
        int n;
        if (rof >= 0x20 || rof == fire_mode::kSingle) n = 1;
        else if (rof == fire_mode::kSemi) n = 3;
        else if (rof == fire_mode::kFull) n = 0x14;
        else n = 1;
        if (u16(w->rounds) < u16(n)) n = u16(w->rounds);
        if (teamType(shooter->team) == 0) {
            // Original quirk: satchel charges, launcher grenades (rate 8) and
            // everything fired from the M203 count as rounds, not explosives.
            const int t = wtype(w);
            if (wdef(w).blast_radius <= 0 || t == kWeaponSatchel || rof == fire_mode::kLauncher || t == kWeaponM203)
                S.stats.roundsFired = s16(S.stats.roundsFired + n);
            else
                S.stats.grenadesThrown = s16(S.stats.grenadesThrown + 1);
        }
        s.rounds = u8(n);
        s.fire_mode = rof;
        wpnSpendRounds(w, n, rof);
        s.state = 0;
        s.shooter = shooter;
        s.shooter_move = shooter->mover ? u8(shooter->mover->move_mode) : 0;
        s.shooter_posture = shooter->mover ? u8(shooter->mover->posture) : 0;
        s.target = target;
        s.target_kind = TargetKind(s16(targetKind));
        if (targetKind == 0) {
            // Original: read through the pointer even when it is NULL
            // (unit1_use_tool, fx_phantom_explosion); the port gives 0.
            const Mover* m = target ? target->mover : nullptr;
            s.target_move = m ? u8(m->move_mode) : 0;
            s.target_posture = m ? u8(m->posture) : 0;
        } else {
            s.target_move = 0;
            s.target_posture = 0;
        }
        s.shooter_pos = shooter->body->pos;
        s.unk_20 = 0;
        s.weapon = s16(wtype(w));
        s.range = s16(range);
        s.cover = s16(cover);
        s.target_pos = pos;
        s.projectile = prjFire(shooter, w, pos, target, targetKind, range);
    }
    noiseFromWeapon(shooter, w);  // 19ac:744E tail: shooter position, weapon, fire mode, team
}

int aiTargetPriority(const Unit* shooter, const Unit* target, int cover) {
    MissionState& S = ms();
    const int d = geoDistance(shooter->body->pos, target->body->pos);
    int p = (-(d - 900) / 0x32) * 0x1E + 10;
    if (p > 0x50) p = 0x50;
    if (s16(cover) < 0x5F) p -= s16(cover);
    else p = 0;
    if (p < 0) p = 0;
    if (craftType(brainTeamType(target))) p += 0x14;
    const Team* grp = (target == pointMan()) ? team(0) : (target->brain ? target->brain->team : nullptr);
    const int size = teamMemberCount(grp);
    if (size == 1) p -= 5;
    else p += (size - 1) * 5;
    p += rnd(20);
    if (recPtr(S.playerTarget) == static_cast<const void*>(target) && shooter->team &&
        shooter->team->fire_order == FireOrder::AtTarget)
        p = 0x7F;
    if (teamType(shooter->team) == 0 && shooter->team->fire_order == FireOrder::CoverFire && target->brain &&
        (target->brain->flags & brain_flag::kSuppressed))
        p = 0;
    if (p < 0) p = 0;
    if (p > 0x7F) p = 0x7E;
    return u8(p);
}

void shotScatter(Vec3& pos, const Unit* u) {
    int n = 0x13 - u8(u->status->skill[u8(Skill::Throw)]) / 5;
    const int leg = medLegLevel(u->status);
    if (leg == 2) n += 10;
    if (leg == 1) n += 5;
    if (n < 1) n = 1;
    int ox = rnd(n) * 3;
    int oz = rnd(n) * 3;
    if (rnd(100) < 0x32) ox = -ox;
    if (rnd(100) < 0x32) oz = -oz;
    pos.x = wrapAdd(pos.x, shl8(ox));
    pos.z = wrapAdd(pos.z, shl8(oz));
}

void combatRandomWound(Unit* u) {
    int roll = rnd(100);
    // Original quirk: low rolls become 21/23, which band 1 turns into misses.
    if (roll < 0x10) roll = (roll & 1) ? 0x15 : 0x17;
    int wound = s16(woundScan(kWoundBullet1, roll));
    if (u == pointMan() && ms().opt.player_wounds == 0 && wound != -1) wound = 0;
    dmgApply(u, nullptr, wound, 0, 0);
}

void shotUpdateAll(int elapsed) {
    (void)elapsed;  // the original takes no argument
    for (ShotRec& s : ms().shots) {
        if (s.state != 0) continue;
        if (!unitProjectileIdle(&s)) continue;
        Victims v{};
        shotClearVictims(v);
        const bool any = s.shooter && shotCollectVictims(s, v);
        if (any) shotApplyDamage(s, v);
        unitProjectileStop(&s);  // (the original passes 3 / 0 for hit / nothing; 1000:5790 ignores it)
        s.state = 1;
    }
}

// ---------------------------------------------------------------------------
// Wounds and medical (19ac)
// ---------------------------------------------------------------------------

int medTreat(Unit* medic, Unit* patient) {
    if (!medic || !patient) return 0;
    if (invCountItems(medic, 1) < 1) return 0;
    Status* st = patient->status;
    int n = 0;
    if (st->bleeding == 1) {
        st->bleeding = 0;
        n = 1;
    }
    if (st->heavy_wounds >= 2) {
        st->heavy_wounds = u8(st->heavy_wounds - 2);
        ++n;
    } else if (st->heavy_wounds == 1) {
        st->heavy_wounds = 0;
        if (st->light_wounds != 0) st->light_wounds = u8(st->light_wounds - 1);
        ++n;
    }
    if (n == 0) {
        if (st->light_wounds >= 2) {
            st->light_wounds = u8(st->light_wounds - 2);
            ++n;
        } else if (st->light_wounds == 1) {
            st->light_wounds = 0;
            ++n;
        }
    }
    if (n == 0) return 0;
    invUseItems(medic, 1, 1);
    return n;
}

int medWoundLevel(const Status* s) {
    if (s->hit_mask & hit_bit::kKilled) return 0;
    if (s->bleeding == 1) return 2;
    const u8 lo = u8(s->hit_mask), hi = u8(s->hit_mask >> 8);
    int n = (hi & 0x20) ? 1 : 0;
    if (hi & 0x10) ++n;
    if (hi & 0x08) ++n;
    if (hi & 0x04) ++n;
    if (hi & 0x02) n += 2;
    if (lo & 0x80) n += 2;
    if (lo & 0x40) n += 2;
    if (lo & 0x04) n += 2;
    if (lo & 0x08) n += 2;
    if (hi & 0x01) ++n;
    if (lo & 0x20) ++n;
    if (lo & 0x10) ++n;
    if (lo & 0x01) ++n;
    if (lo & 0x02) ++n;
    return n > 2 ? 2 : n;
}

int medStatus(const Status* s) {
    if (s->hit_mask & hit_bit::kKilled) return 0;
    if (s->bleeding == 1 || s->heavy_wounds != 0) return 2;
    return s->light_wounds != 0 ? 1 : 0;
}

int medLegLevel(const Status* s) {
    const u8 lo = u8(s->hit_mask);
    if (lo & 0xC0) return 2;
    if ((lo & 0x30) && s->light_wounds > 1) return 2;
    return (lo & 0x30) ? 1 : 0;
}

int medArmCount(const Status* s) {
    const u8 hi = u8(s->hit_mask >> 8);
    return ((hi & 0x08) ? 1 : 0) + ((hi & 0x10) ? 1 : 0);
}

int medHeadCount(const Status* s) {
    const u8 lo = u8(s->hit_mask);
    return ((lo & 0x05) ? 1 : 0) + ((lo & 0x0A) ? 1 : 0);
}

void medBleedTick(int elapsed) {
    MissionState& S = ms();
    for (int ti = 0; ti < kMaxTeams && S.teams[ti]; ++ti) {
        Team* t = S.teams[ti];
        for (int m = 0; m < 8 && t->members[m]; ++m) {
            Unit* u = t->members[m];
            Status* st = u->status;
            if (!st || (st->hit_mask & hit_bit::kKilled) || st->bleeding != 1) continue;
            if (s16(elapsed) < st->bleed_time) {
                st->bleed_time = s16(st->bleed_time - s16(elapsed));
                continue;
            }
            evtUnitBledToDeath(u);
            if (u->brain) aiMemberDeactivate(u);
            st->hit_mask |= hit_bit::kKilled;
        }
    }
}

// ---------------------------------------------------------------------------
// Damage and scripted explosions (1000)
// ---------------------------------------------------------------------------

void dmgApply(Unit* unit, WorldObject* obj, int hitBits, int damage, int what) {
    if (what == 0) {
        dmgApplyUnit(unit, hitBits);
        return;
    }
    if (what == 2) return;
    dmgApplyObject(obj, damage);
}

const ModelDesc* wldDestroyedShape(const WorldObject* w) {
    if (!w) return model(0x79B0);
    if (modelOffset(w->body->model) == 0x6D16) {
        w->body->pos.y = 0;
        return model(0x791A);
    }
    switch (w->kind) {
    case TerrainKind::Vegetation: return model(0x79B0);
    case TerrainKind::Tree: return model(0x8800);
    case TerrainKind::Structure: return model(0x786A);
    default: return model(0x7A40);
    }
}

void fxPhantomExplosion(Unit* target) {
    Unit* ph = ms().proxyGrenadier;
    if (!ph) return;
    // The explosion point is the target's live position, or the phantom's own
    // (after its 3-unit shift below).
    const Vec3& pos = (target ? target : ph)->body->pos;
    evtSetPosture(ph, 2);
    if (target) ph->body->pos = pos;
    ph->body->pos.z = wrapSub(ph->body->pos.z, 0x300);
    ph->body->heading = 0;
    WeaponNode* g = ph->loadout ? ph->loadout->secondary : nullptr;
    if (g) {
        shotFire(ph, target, pos, 0, g, 0, 0);
        g->rounds = s16(g->rounds + 1);
    }
    ph->body->pos.z = wrapSub(ph->body->pos.z, 0x177000);
    unitHideChain(ph);
}

void fxDestroyStructure(WorldObject* w) {
    Unit* ph = ms().proxyGrenadier;
    if (!ph || !ph->loadout || !ph->loadout->secondary) return;
    WeaponNode* g = ph->loadout->secondary;
    const WeaponId saved = g->type;
    ph->body->pos = w->body->pos;
    g->type = WeaponId::MkIIllum;   // 0x21
    fxPhantomExplosion(nullptr);
    ph->body->pos = w->body->pos;
    g->type = WeaponId::Sg1Smoke;   // 0x1A
    fxPhantomExplosion(nullptr);
    g->type = saved;
}

void wldHideHighlight(WorldObject* w) {
    w->body->flags &= u16(~obj3d_flag::kLosTreeB);
    if (w->kind == TerrainKind::PitTrap) w->body->pitch = 1;
}

// ---------------------------------------------------------------------------
// Inventory (1000)
// ---------------------------------------------------------------------------

int invCountItems(const Unit* u, int type) {
    int n = 0;
    for (const ItemNode* it = u->items; it; it = it->next)
        if (s8(it->type) == s8(type)) n = s16(n + it->quantity);
    return n;
}

void invUseItems(Unit* u, int type, int n) {
    for (ItemNode* it = u->items; it && n != 0; it = it->next) {
        if (s8(it->type) == s8(type) && it->quantity > 0) {
            it->quantity = s16(it->quantity - n);
            n = 0;
        }
    }
}

void unit1UseTool() {
    Team* t0 = team(0);
    Unit* u = t0 ? t0->members[1] : nullptr;
    if (!u) return;
    WeaponNode* g = u->loadout ? u->loadout->secondary : nullptr;
    if (!g) return;  // the original has no check
    shotFire(u, nullptr, u->body->pos, 0, g, 0x3C, 0);
}

// ---------------------------------------------------------------------------
// Targeting (1000)
// ---------------------------------------------------------------------------

int tgtAcquire(Unit* shooter, TargetRec& rec, bool skip) {
    MissionState& S = ms();
    int best = 3000;
    const void* old = recPtr(rec);
    const bool lock = !(rec.kind == TargetKind::None || skip);
    const Vec3& sp = shooter->body->pos;
    const int hdg = shooter->body->heading >> 3;
    auto clearOld = [&]() {
        if (rec.kind == TargetKind::None) return;
        if (Obj3D* b = recBody(rec)) b->flags &= u16(~obj3d_flag::kTargeted);
    };
    if (S.targetStructures) {
        for (WorldObject* w : worldObjects()) {
            if (skip) {
                if (static_cast<const void*>(w) == old) skip = false;
                continue;
            }
            if (w->cover != 100 || !geoInFov(&sp, 10, hdg, &w->body->pos)) continue;
            const int d = geoDistance(w->body->pos, sp);
            if (d >= 0x4B1 || (lock && static_cast<const void*>(w) != old)) continue;
            const int los = wldLosCover(shooter, w->body->pos);
            best = d;
            const bool trap = int(w->kind) == 0xA || int(w->kind) == 0xB;  // exposed with 'x'
            if (los < 100 && (!trap || d < 0x79)) {
                clearOld();
                rec.target.structure = w;
                rec.kind = TargetKind::Structure;
                rec.range = s16(d);
                rec.cover = s16(los);
            }
            break;  // the walk stops at the first candidate either way
        }
    }
    const Unit* pm = pointMan();
    const Team* ownTeam = pm ? pm->team : nullptr;
    for (int ti = 0; ti < kMaxTeams && S.teams[ti]; ++ti) {
        Team* t = S.teams[ti];
        if (!isFootTeam(t) || t == ownTeam) continue;
        for (int m = 0; m < 8 && t->members[m]; ++m) {
            Unit* u = t->members[m];
            if (skip) {
                if (static_cast<const void*>(u) == old) skip = false;
                continue;
            }
            if (!geoInFov(&sp, 0x10, hdg, &u->body->pos)) continue;
            if (isDead(u) || (u->mover && (u->mover->flags & mover_flag::kSecured))) continue;
            if (!u->brain || !(u->brain->flags & brain_flag::kActive)) continue;
            const int d = geoDistance(u->body->pos, sp);
            if (lock && static_cast<const void*>(u) != old) continue;
            if (d >= 0x475 || d >= best) continue;
            const int los = wldLosCover(shooter, u->body->pos);
            if (los >= 100) continue;
            clearOld();
            rec.range = s16(d);
            rec.cover = s16(los);
            rec.target.unit = u;
            u->body->flags |= obj3d_flag::kTargeted;
            rec.kind = TargetKind::Unit;
            return 0;
        }
    }
    if (best < 3000) return 1;
    clearOld();
    rec.target.unit = nullptr;
    rec.kind = TargetKind::None;
    return -1;
}

void tgtClear() {
    TargetRec& r = ms().playerTarget;
    if (r.kind == TargetKind::Unit && r.target.unit && r.target.unit->body)
        r.target.unit->body->flags &= u16(~obj3d_flag::kTargeted);
    r.kind = TargetKind::None;
    r.target.unit = nullptr;
}

bool wldOpenHutAhead(Unit* u) {
    for (WorldObject* w : worldObjects()) {
        if (w->kind != TerrainKind::TripWire && w->kind != TerrainKind::PitTrap) continue;
        if (!geoInFov(&u->body->pos, 10, u->body->heading >> 3, &w->body->pos)) continue;
        if (geoDistance(w->body->pos, u->body->pos) < 0x79) {
            w->cover = 100;
            return true;
        }
    }
    return false;
}

} // namespace st::game::mission
