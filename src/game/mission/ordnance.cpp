// Ordnance: launch velocity, flight set-up and the per-frame flight of the 32
// projectiles (segment 2dbd part 4, docs/re/seg_2dbd_craft_ordnance.md), and
// the projectile / effect-object code of segment 1000 (1000:45B9..60FA,
// docs/re/seg_1000.md 13): allocation, firing, burst / impact / cloud
// effects, muzzle flashes and the grenade aim box and map unit markers.
//
// The "effects" of a projectile (explosion, cloud, impact puff) are its own
// body switched to the burst model 0x7C02; only the muzzle flash is a separate
// object (pool DS:2F70).
#include "game/mission/craft.h"

#include "core/settings.h"
#include "data/exeimage.h"
#include "engine/rng.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/sfx.h"
#include "game/mission/state.h"
#include "game/mission/world.h"
#include "game/mission/wquery.h"
#include "render/impactfx.h"

#include <cstring>

namespace st::game::mission {

namespace {

constexpr u16 kMuzzleFwdByPosture = 0x0AD2;  // s16[3] {15, 15, 21}: muzzle flash distance ahead
constexpr u16 kMsgDud = 0x0AD8;              // "Dud."
constexpr u16 kMuzzleHeight = 0x3124;        // s32[4] by posture {12, 7, 0, 0}
constexpr u16 kAimPitch = 0x3134;            // s32[4] by posture, low word = launch pitch
constexpr u16 kMsgJammed = 0x33BF;           // "Weapon jammed."
constexpr u16 kMsgDemoFuse = 0x33CE;         // "Demolition Charge will explode in 60 seconds."
constexpr u16 kSfxSeg = 0x520D;              // far sound data segment (slot DS:CD8A)
constexpr u16 kFireSfxTable = 0x04B0;        // s16[class] firing sound
constexpr u16 kBurstModel = 0x7C02;          // explosion / puff / cloud shape
constexpr u16 k40mmModel = 0x832E;           // 40 mm grenade (M79 / M203)
constexpr u16 kAimMarkerY = 0xFE00;          // marker altitude 0xFFFFFE00

// speed * g_frame_ticks: 16x16 -> 32 bit product, shifted left and right by 8.
s32 frameStep(int speed) {
    const s32 v = s32(s16(speed)) * s32(ms().frameTicks);
    return s32(u32(v) << 8) >> 8;
}

s32 shl8(s32 v) { return s32(u32(v) << 8); }

// "if now - base >= dur then base = now": true unless now == 0 (the original
// tests the stored value).
bool timerElapsed(Ticks& base, Ticks dur) {
    const Ticks now = ms().time;
    if (wrapSub(now, base) >= dur) {
        base = now;
        return now != 0;
    }
    return false;
}

bool isM203Grenade(const Projectile* p) { return p->weapon_node && p->weapon_node->fire_mode == 8; }

// Classes that explode on landing / at the end of their life (2dbd:48B0, 4BE8).
bool explodesOnLanding(const Projectile* p) {
    const int c = int(p->wclass);
    return c == 0xF || c == 4 || (c == 5 && isM203Grenade(p)) || c == 0x10 || c == 0x14;
}

s16 fireSound(int wclass) {
    const u8* t = exe().at(kSfxSeg, u16(kFireSfxTable + 2 * wclass));
    return t ? rds16(t) : 0;
}

s32 muzzleHeight(int posture) { return s32(exe().dgDword(u16(kMuzzleHeight + (posture & 0xFF) * 4))); }
s16 aimPitch(int posture) { return exe().dgShort(u16(kAimPitch + (posture & 0xFF) * 4)); }

Obj3D* burstShapeOn(Obj3D* o) {
    if (o) {
        o->flags |= obj3d_flag::kEnabled;
        o->model = model(kBurstModel);
    }
    return o;
}

// ---------------------------------------------------------------------------
// Port: Enhanced "Impact effects" (render/impactfx.h). The surface of an
// impact site is classified here, next to the original's puff-kind flags,
// and kept in Projectile::impact_surface for the painter. Only called with
// settings().effectiveImpactFx(); none of it touches the game RNG.
// ---------------------------------------------------------------------------

bool impactFxOn() { return settings().effectiveImpactFx(); }

// Ground contact: water if a water / shallow / deep-water object covers the
// point (wld_probe_kind clears the altitude of its argument: pass a copy).
ImpactSurface groundSurface(const Vec3& at) {
    for (const int kind : {int(TerrainKind::Water), int(TerrainKind::Shallow), int(TerrainKind::DeepWater)}) {
        Vec3 probe = at;
        if (wldProbeKind(probe, kind)) return ImpactSurface::Water;
    }
    return ImpactSurface::Dust;
}

// A solid world object by its terrain kind and model.
ImpactSurface obstacleSurface(const WorldObject* w) {
    if (!w) return ImpactSurface::Dust;
    const char* name = modelName(w->model);
    auto is = [&](const char* n) { return std::strcmp(name, n) == 0; };
    switch (w->kind) {
    case TerrainKind::Vegetation:
    case TerrainKind::Tree:
    case TerrainKind::Brush:
        return ImpactSurface::Foliage;
    case TerrainKind::Prop:
        return is("rock") ? ImpactSurface::Stone : ImpactSurface::Wood;
    case TerrainKind::Structure:
        if (is("bldgston") || is("well") || is("church") || is("pagoda")) return ImpactSurface::Stone;
        if (is("bunker")) return ImpactSurface::Dust;
        return ImpactSurface::Wood;
    case TerrainKind::BoatPad:
        return ImpactSurface::Wood;
    case TerrainKind::HeloPad:
        return ImpactSurface::Stone;
    default:
        return ImpactSurface::Dust;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Flight (2dbd)
// ---------------------------------------------------------------------------

// 2dbd:4088: initial vertical speed that makes a lobbed round come down at
// height 0 after `range`; also raises the lifetime to cover the flight.
int evtCalcLaunchVelocity(Projectile* p, int range) {
    if (!p || !p->flight) return 0;
    FlightRec* f = p->flight;
    const s16 launchHeight = s16(u32(p->body->pos.y) >> 8);  // word at body +0x0B
    s16 r = s16(range);
    if (r <= 0) r = 6;
    if ((p->fire_modes & fire_mode::kThrow) && f->speed > r) f->speed = r;
    if (r > 0xF0) f->speed = s16(f->speed + (r >> 5));
    r = s16(r - (r >> 2));
    r = s16(r - (r >> 2));
    const s16 t = f->speed > 0 ? s16((s32(r) << 8) / s32(f->speed)) : s16(0x100);
    s16 v = s16((s32(t) * 30) >> 8);
    if (t > 0) v = s16(v - s16(shl8(launchHeight) / s32(t)));
    const s32 n = s16(f->speed > 0x12C ? t << 2 : t << 1);
    if (p->lifetime < n) p->lifetime = n;
    return s16(-v);
}

// 2dbd:41A4: motion record by ordnance class.
void evtProjectileInitMotion(Projectile* p, int range) {
    if (!p || !p->flight) return;
    FlightRec* f = p->flight;
    f->unk_10 = 0x168;
    f->turn_rate = 0x20;
    f->unk_1a = 9;
    f->vertical_speed = 9;
    f->unk_1c = -0x10;
    f->unk_12 = 0;
    f->unk_0e = 0;
    f->heading = 0;
    f->launch_height = 0;
    f->bounces = 0;
    f->gravity = 0;
    int cls = int(p->wclass);
    if (cls == 5 && p->weapon_node && p->weapon_node->fire_mode != 8) cls = 3;  // M203 rifle mode
    auto bullet = [f](s16 pitchConst) {
        f->max_speed = 0x3E7;
        f->accel = 0x20;
        f->unk_06 = 0;
        f->unk_0a = 0;
        f->speed = f->target_speed = 0x168;
        f->gravity = 1;
        f->unk_1c = pitchConst;
    };
    auto set = [f](s16 speed, s16 bounces, s16 pitchConst) {
        f->speed = f->target_speed = speed;
        f->bounces = bounces;
        f->gravity = 1;
        f->unk_1c = pitchConst;
    };
    switch (cls) {
    case 0: case 1: case 2: case 3: case 6: case 9: case 18: case 19:
        bullet(-0x30);
        break;
    case 7: case 8: case 17:
        bullet(-0x24);
        break;
    case 4:
        set(0x168, 0, -0x1E);
        break;
    case 5:
        set(0x1E0, 0, -0x1E);
        break;
    case 10: case 21:
        set(0x5A, 1, 0xF0);
        f->launch_height = 3;
        if (p->weapon == 0xC) {  // satchel charge: placed, no launch velocity
            f->speed = 0xC;
            f->vertical_speed = 0x15;
            return;
        }
        break;
    case 11: case 13:
        set(0x5A, 1, 0xD2);
        f->launch_height = 3;
        break;
    case 12:
        set(0x60, 1, 0x96);
        f->launch_height = 3;
        break;
    case 14:
        set(0x48, 1, 0xB4);
        f->launch_height = 3;
        break;
    case 15:
        set(0x258, 0, 0xF0);
        break;
    case 16:
        set(0x21C, 0, 0xF0);
        f->launch_height = 6;
        break;
    case 20:
        set(0x3C, 0, 0xF0);
        break;
    default:
        // Unknown classes: default speed, no launch velocity.
        f->accel = 0x20;
        f->unk_06 = 0;
        f->unk_0a = 0;
        f->speed = f->target_speed = 0x168;
        return;
    }
    f->vertical_speed = s16(evtCalcLaunchVelocity(p, range));
}

// 2dbd:4429 (every frame for each slot in use).
void evtUpdateOrdnance(Projectile* p) {
    if (!p) return;
    MissionState& S = ms();
    FlightRec* f = p->flight;
    Obj3D* b = p->body;
    const int cls = int(p->wclass);

    if (p->muzzle_phase != 0) {
        // Muzzle / hand phase: the round rides with its carrier.
        if (p->fire_modes & 0x0F) {
            int fwd = -1;
            if (p->launch_time == p->timer_base) {
                // Original quirk (2dbd:4475): craft have no anim block; the original then reads the
                // posture from the interrupt vector table (a segment value > 2, i.e. the entry for 2).
                int k = (p->owner && p->owner->anim) ? int(p->owner->anim->posture) : 2;
                if (k > 2) k = 2;
                fwd = exe().dgShort(u16(kMuzzleFwdByPosture + 2 * k));
            }
            fxMuzzleFlash(p, 0, fwd);
        }
        if (timerElapsed(p->timer_base, p->muzzle_phase)) {
            p->muzzle_phase = 0;
        } else if (cls == 0xF || cls == 0x10) {
            // Rockets stay hidden for their first second.
            if (wrapSub(S.time, p->timer_base) < 0x100) {
                b->flags &= u16(~obj3d_flag::kEnabled);
                fxMuzzleFlash(p, 0, -1);
            } else {
                b->flags |= obj3d_flag::kEnabled;
            }
        }
        const s16 carrierSpeed = (p->owner && p->owner->mover) ? p->owner->mover->speed : 0;
        posMovePolar(frameStep(carrierSpeed), b->pitch, b->heading, b->pos);
    } else {
        // In flight.
        if (p->fire_modes & 0x0F) fxMuzzleFlash(p, 0, -1);
        if (cls == 0xF || cls == 0x10) b->flags |= obj3d_flag::kEnabled;
        posMovePolar(frameStep(f->speed), b->pitch, b->heading, b->pos);
        if (f->vertical_speed > 0) {
            const int down = angleWrap(-0x2D0);
            posMovePolar(frameStep(f->vertical_speed), down, b->heading, b->pos);
        } else {
            const int up = angleWrap(0x2D0);
            posMovePolar(frameStep(s16(-f->vertical_speed)), up, b->heading, b->pos);
        }

        // Timed detonation: 40 mm grenades and thrown items.
        const bool timed = (p->weapon == 10 && p->base_model == model(k40mmModel)) ||
                           (p->weapon != 10 && p->weapon_node && (p->weapon_node->fire_mode & fire_mode::kThrow));
        if (timed && p->detonate_time <= S.time) {
            if (p->state & prj_state::kDud) {
                f->vertical_speed = 0;
                f->speed = 0;
            } else {
                if (cls != 0xC && cls != 0xE) prjStartBurst(p, 0);
                else prjShowBurst(p, 0);  // smoke / gas cloud
                f->vertical_speed = 0;
                f->speed = 0;
                p->hit |= prj_hit::kLanded;
            }
        }
        if (f->speed <= 0xC) {
            f->speed = 0;
            f->vertical_speed = 0;
        }
        if (b->pos.y > 0 && f->gravity != 0) f->vertical_speed = s16(f->vertical_speed + s16((s32(0x3C) * S.frameTicks) >> 8));
        // Bullets still falling high above the ground are culled.
        if (f->vertical_speed > 0 && f->bounces == 0 && b->pos.y > 0x9600 && cls <= 3 && !(p->state & prj_state::kDud))
            p->state |= prj_state::kExpired;

        // Lifetime.
        if (p->state & prj_state::kExpired) {
            if (timerElapsed(p->timer_base, wrapAdd(p->lifetime, p->lifetime))) p->state |= prj_state::kResolved;
        } else if (timerElapsed(p->timer_base, p->lifetime)) {
            p->state |= prj_state::kExpired;
            if ((p->state & prj_state::kDud) && p->owner == pointMan()) msgShowDs(kMsgDud, 0x100);
        }

        if (b->pos.y < 0) {
            // Ground contact.
            if (f->bounces == 0) {
                b->pos.y = 0;
                f->speed = 0;
                p->impact_time = S.time;
                p->hit |= prj_hit::kLanded;
                if (!(p->state & prj_state::kDud) && explodesOnLanding(p)) {
                    prjStartBurst(p, 0);
                } else {
                    if (impactFxOn()) p->impact_surface = u8(groundSurface(b->pos));
                    prjStartImpact(p, 0, 0x10);
                    p->state |= prj_state::kExpired;
                }
            } else {
                const int k = f->speed > 0x3C ? 4 : 0x10;
                s16 h = b->heading;
                if (f->speed <= 0x78) {
                    const int r = engine::rng().range(k * 2);
                    h = s16(angleWrap(h + s16((r - k) << 3)));  // 2255:621E math_angle_add
                }
                b->heading = h;
                b->pos.y = 0;
                const int r2 = engine::rng().range(0x30);
                b->heading = s16(angleWrap(s16(s16((r2 - 0x18) << 3) + b->heading)));
                if (f->speed != 0) {
                    f->vertical_speed = s16(-(f->vertical_speed >> 2));
                    f->speed = s16(f->speed >> 1);
                }
            }
        } else if (Obj3D* hit = geoProbeAt(b)) {
            // Collision probe (1000:318E).
            Unit* victim = unitOfBody(hit);
            p->last_hit.unit = victim;
            if (victim && victim != p->owner && victim == p->target) {
                // Launched and thrown rounds pass through their target.
                if (!(p->fire_modes & 0x18)) {
                    p->hit |= prj_hit::kUnit;
                    f->speed = 0;
                    p->impact_time = S.time;
                    b->pos.x = victim->body->pos.x;
                    b->pos.z = victim->body->pos.z;
                    const Team* vt = victim->team;
                    if (vt && int(vt->type) != 0 && int(vt->type) < 4) {
                        if (impactFxOn()) p->impact_surface = u8(ImpactSurface::Metal);
                        prjStartImpact(p, 0, 0x80);
                    } else {
                        // One puff in three on a human (the round keeps probing
                        // its victim every frame, so the roll repeats until it
                        // succeeds or the round expires).
                        if (impactFxOn()) p->impact_surface = u8(ImpactSurface::Blood);
                        if (engine::rng().range(3) == 0) {
                            prjStartImpact(p, 0, 0x20);
                        } else if (impactFxOn() && !p->fx_impact && !p->impact_shown) {
                            // Port: the Enhanced impact effects show every hit.
                            // Starting the puff here would reset the round's
                            // lifetime (prj_start_impact), so a visual-only
                            // puff and the particles are drawn by render/impactfx
                            // instead; the game state is the original's.
                            p->impact_shown = 1;
                            render::impactPuffAdd(b->pos, 1, ImpactSurface::Blood, S.time);
                            render::impactParticlesSpawn(b->pos, ImpactSurface::Blood, S.time);
                        }
                    }
                    victim->mover->impact_bearing = s16(b->heading >> 3);
                }
            } else {
                WorldObject* w = wldObjectFromHit(hit);
                p->last_hit.structure = w;
                if (w && w->height == 0x7F) {
                    if (f->bounces == 0) {
                        p->impact_time = S.time;
                        f->speed = 0;
                        if (impactFxOn()) p->impact_surface = u8(obstacleSurface(w));
                        prjStartImpact(p, 0, 0x40);
                        p->hit |= prj_hit::kObstacle;
                    } else if (p->weapon != 0xC) {
                        // Ricochet (satchel charges excepted).
                        b->heading = s16(angleWrap(b->heading + 0x5A0));
                        posMovePolar(frameStep(f->speed), b->pitch, b->heading, b->pos);
                        f->speed = s16(f->speed >> 1);
                    }
                }
            }
        }
    }

    // Final: expired and resolved -> release the effects and the slot.
    if ((p->state & prj_state::kExpired) && (p->state & prj_state::kResolved)) {
        if (!(p->state & prj_state::kDud)) {
            if ((p->fire_modes & 0x18) || explodesOnLanding(p)) prjStartBurst(p, -1);
            if (p->fire_modes & 0x18) prjShowBurst(p, -1);
            prjStartImpact(p, -1, 0);
        }
        prjDeactivate(p);
    }
}

// ---------------------------------------------------------------------------
// Projectile pool (1000:45B9..488E)
// ---------------------------------------------------------------------------

void prjResetAll() {
    for (Projectile* p : ms().projectiles) {
        if (!p) continue;
        p->hit = 0;
        p->impact_surface = p->impact_shown = 0;  // port: Enhanced impact effects
        p->state = 0;
        p->owner = nullptr;
        p->weapon_node = nullptr;
        if (p->flight) evtProjectileInitMotion(p, 0x96);
    }
}

Projectile* prjFindByEffect(const Obj3D* fx) {
    if (!fx) return nullptr;
    for (Projectile* p : ms().projectiles) {
        if (!p) return nullptr;
        if ((p->state & prj_state::kInUse) &&
            (p->fx_muzzle == fx || p->fx_explosion == fx || p->fx_cloud == fx || p->fx_impact == fx))
            return p;
    }
    return nullptr;
}

Obj3D* fxPoolAlloc(const std::vector<Obj3D*>& pool) {
    for (Obj3D* o : pool) {
        if (!o) return nullptr;
        if (!(o->flags & obj3d_flag::kEnabled)) return o;
    }
    return nullptr;
}

void fxRelease(Obj3D* o) {
    if (o) o->flags &= u16(~obj3d_flag::kEnabled);
}

int prjAlloc(int wclass, int fireMode) {
    const u8 mode = u8(fireMode);
    if (wclass == 5 && mode != 8) wclass = 3;
    u16 shape;
    switch (wclass) {
    case 0: case 2: case 18: case 19:
        shape = mode == 1 ? 0x68E8 : mode == 4 ? 0x66FA : 0x6522;
        break;
    case 1:
        shape = mode == 1 ? 0x69CA : mode == 4 ? 0x67F2 : 0x6606;
        break;
    case 3: case 6: case 8:
        shape = mode == 1 ? 0x6876 : mode == 4 ? 0x6676 : 0x64B2;
        break;
    case 4: case 5:
        shape = k40mmModel;
        break;
    case 7: case 17:
        shape = mode == 1 ? 0x6958 : mode == 4 ? 0x677C : 0x6592;
        break;
    case 9:
        shape = 0xBD84;
        break;
    case 10: case 11: case 12: case 13: case 14:
        shape = mode == 8 ? 0xB7BE : 0x820E;
        break;
    case 15:
        shape = 0xB628;
        break;
    case 16:
        shape = 0xB552;
        break;
    case 20:
        shape = 0x9238;
        break;
    case 21:
        shape = 0xB7BE;
        break;
    default:
        shape = 0x6606;
        break;
    }
    int slot = 0;
    for (; slot < kProjectiles; ++slot) {
        Projectile* p = ms().projectiles[slot];
        if (!p || (p->state & prj_state::kInUse)) continue;
        const ModelDesc* m = model(shape);
        p->body->model = m;
        p->base_model = m;
        break;
    }
    return slot;
}

// 1000:488E
Projectile* prjFire(Unit* shooter, WeaponNode* w, const Vec3& targetPos, Unit* targetUnit, int mode, int speed) {
    MissionState& S = ms();
    if (!shooter || !w) return nullptr;
    const int type = u8(w->type);
    const WeaponDef& def = weaponDef(type);
    const int cls = int(def.wclass);

    // Launch animation.
    if (type == 0xC) evtCrouchToPlaceCharge(shooter);
    else if (u8(def.fire_modes) == fire_mode::kThrow) sprStartThrowAnim(shooter);
    else if (cls == 0xF || cls == 0x10) evtCrouchToFireLauncher(shooter);

    // Jammed direct shot.
    if (mode == -1 && !(w->fire_mode & 0x18) && w->jammed) {
        Unit* pm = pointMan();
        if (shooter == pm) {
            msgShowDs(kMsgJammed, 0x80);
            sfxPlay(0x1C, 0x100, &pm->body->pos, 1, nullptr);
        }
        w->jammed = 0;
        return nullptr;
    }

    const int fm = type == 0xC ? 8 : w->fire_mode;
    const int slot = prjAlloc(cls, u8(fm));
    if (slot >= kProjectiles) return nullptr;
    Projectile* p = S.projectiles[slot];
    p->hit = 0;
    p->impact_surface = p->impact_shown = 0;  // port: Enhanced impact effects
    p->state = prj_state::kInUse;
    if (w->jammed) {
        p->state |= prj_state::kDud;
        w->jammed = 0;
    }
    p->owner = shooter;
    p->weapon_node = w;
    p->wclass = def.wclass;
    p->weapon = s16(type);
    p->fire_modes = u8(def.fire_modes);

    // Firing sound with its repeat count.
    int single = 0;
    u16 count;
    if (w->fire_mode & fire_mode::kSemi) {
        count = u16(w->rounds);
        if (count > 3) count = 3;
        if (count < 1) count = 1;
    } else if (w->fire_mode & fire_mode::kFull) {
        count = u16(w->rounds);
        if (count > 20) count = 20;
        if (count < 1) count = 1;
    } else {
        if ((w->fire_mode & fire_mode::kSingle) && cls != 0xF && cls != 0x10 && cls != 0x14 && cls != 0x15) single = 1;
        count = 1;
    }
    const int sfxClass = (type == 10 && !(w->fire_mode & fire_mode::kLauncher)) ? 3 : cls;  // M203 rifle mode
    sfxPlay(s16(fireSound(sfxClass) + single), 0x80, &shooter->body->pos, count, nullptr);

    p->target = targetUnit;
    const Vec3 tgt = targetPos;
    p->last_hit.unit = nullptr;
    p->timer_base = p->launch_time = S.time;
    if (Mover* mv = shooter->mover) {
        mv->fired_until = wrapAdd(p->launch_time, 0x100);
        if (p->fire_modes & fire_mode::kThrow) mv->throw_until = wrapAdd(p->launch_time, 0x200);
        evtSetMoveMode(shooter, int(mv->move_mode));
    }

    // Lifetime, muzzle phase and fuse.
    if (mode == -1 && p->weapon != 0xC) {
        p->muzzle_phase = 0;
        p->lifetime = 0x700;
        p->detonate_time = wrapAdd(p->launch_time, 0x700);
    } else {
        const bool flare = p->weapon == 0x21 || p->weapon == 0x1A;  // Mk I illumination, SG1
        if (cls != 0xC && cls != 0xE) {
            p->lifetime = 0x600;
        } else {
            p->lifetime = 0x3C00;
            if (flare) p->lifetime = 0xF000 + s32(s16(engine::rng().range(0x3C) << 8));
        }
        if (p->fire_modes & fire_mode::kBuckshot) p->muzzle_phase = 0x20;
        else if (cls == 0xF || cls == 0x10 || cls == 0x14) p->muzzle_phase = 0x180;
        else p->muzzle_phase = 0x40;
        if (p->weapon == 0xC) {
            // Satchel charge: 60 s fuse.
            if (S.time > S.demoExplodeTime) S.demoExplodeTime = wrapAdd(S.time, 0x3C00);
            p->detonate_time = wrapAdd(p->launch_time, 0x3C00);
            msgShowDs(kMsgDemoFuse, 0x200);
            p->lifetime = mode == -1 ? 0x4000 : 0x5000;
        } else {
            p->detonate_time = wrapAdd(p->launch_time, 0x400);
            if (flare) p->detonate_time = wrapSub(p->detonate_time, s32(s16(engine::rng().range(4) << 8)));
        }
    }

    Obj3D* b = p->body;
    if (!(mode == -1 && !(p->fire_modes & 0x18)) && p->weapon != 0x21 && p->weapon != 0x1A) b->flags |= obj3d_flag::kEnabled;
    if (p->weapon == 0xC) speed = 0;
    // Original quirk (1000:4DAB): the flight is set up before the body is moved to the shooter, so
    // the launch height of evt_calc_launch_velocity is the altitude where the slot's last round ended.
    evtProjectileInitMotion(p, speed);

    // Original quirk (1000:4DB3): this first bearing starts from where the slot's body was left;
    // it is overwritten below and only reaches the shooter's aim heading for a moment.
    b->heading = s16(geoBearing(b->pos, tgt) << 3);
    if (shooter->mover) shooter->mover->aim_heading = s16(b->heading >> 3);
    b->pos = shooter->body->pos;  // 1000:31FD geo_copy_pos

    const int stype = shooter->team ? int(shooter->team->type) : 0;
    const int posture = shooter->mover ? int(u8(shooter->mover->posture)) : 0;
    const bool footShooter = stype == 0 || stype == 4 || stype == 5;
    if (footShooter) {
        // Start at the muzzle height of the posture (replaces the altitude), then ahead of the shooter.
        b->pos.y = wrapAdd(shl8(p->flight->launch_height), shl8(muzzleHeight(posture)));
        if (p->fire_modes == fire_mode::kThrow) {
            const int side = angleWrap(shooter->body->heading + 0x2D0);
            posMovePolar(0x300, 0, side, b->pos);
        } else {
            posMovePolar(0x1800, 0, shooter->body->heading, b->pos);
        }
    }

    b->heading = s16(geoBearing(b->pos, tgt) << 3);
    bool aim;
    if (targetUnit) {
        const int ttype = targetUnit->team ? int(targetUnit->team->type) : 0;
        if (stype == 0) aim = false;
        else if (stype == 4 || stype == 5) aim = ttype == 2 || ttype == 3;  // enemies aim at aircraft
        else aim = true;
        b->pitch = aim ? s16(geoPitch(b->pos, tgt) << 3) : aimPitch(posture);
    } else if (footShooter) {
        b->pitch = p->flight->gravity != 0 ? s16(0) : aimPitch(posture);
    } else {
        b->pitch = s16(geoPitch(b->pos, tgt) << 3);
    }
    if (shooter->mover) shooter->mover->aim_heading = s16(b->heading >> 3);
    return p;
}

// 1000:5068
void prjDeactivate(Projectile* p) {
    if (!p) return;
    // Only the muzzle flash and the explosion are released; the pointers are kept.
    if (p->fx_muzzle) fxRelease(p->fx_muzzle);
    if (p->fx_explosion) fxRelease(p->fx_explosion);
    p->hit = 0;
    p->state = 0;
    p->body->flags &= u16(~obj3d_flag::kEnabled);
}

// 1000:575A: true when the shot's round is done (none, hit / landed, or expired).
bool unitProjectileIdle(const ShotRec* s) {
    const Projectile* p = s ? s->projectile : nullptr;
    if (!p) return true;
    return (p->hit & (prj_hit::kUnit | prj_hit::kObstacle | prj_hit::kLanded)) || (p->state & prj_state::kExpired);
}

// 1000:5790
void unitProjectileStop(ShotRec* s) {
    Projectile* p = s ? s->projectile : nullptr;
    if (p && (p->state & prj_state::kInUse)) p->state |= prj_state::kResolved;
}

// ---------------------------------------------------------------------------
// Markers (1000:57BE..592D)
// ---------------------------------------------------------------------------

// Grenade aim box `side` units right and `dist` units ahead of the Point Man
// (hidden for dist < 0, but still placed).
void fxPlaceAimMarker(int side, int dist) {
    Obj3D* m = fxPools().aimMarker;
    const Unit* pm = pointMan();
    if (!m || !pm) return;
    if (s16(dist) < 0) m->flags &= u16(~obj3d_flag::kEnabled);
    else m->flags |= obj3d_flag::kEnabled;
    s16 x = s16(side), z = s16(dist);
    mathRotate2d(x, z, 0, 0, pm->body->heading);  // centre DS:54F6 = (0, 0)
    m->pos.x = wrapAdd(shl8(x), pm->body->pos.x);
    m->pos.z = wrapAdd(shl8(z), pm->body->pos.z);
    m->pos.y = s32(0xFFFF0000u | kAimMarkerY);
}

// Map unit marker n (0..3) 12 units in front of the unit.
void fxPlaceUnitMarker(Unit* u, int n) {
    // The original only rejects n >= 4 (a negative n indexes the pool before DS:2FAC).
    if (!u || n >= 4 || n < 0 || n >= int(fxPools().unitMarkers.size())) return;
    Obj3D* m = fxPools().unitMarkers[size_t(n)];
    m->flags |= obj3d_flag::kEnabled;
    const s16 h = u->body->heading;
    s16 x = 0, z = 12;
    mathRotate2d(x, z, 0, 0, h);
    m->pos.x = wrapAdd(shl8(x), u->body->pos.x);
    m->pos.z = wrapAdd(shl8(z), u->body->pos.z);
    m->pos.y = s32(0xFFFF0000u | kAimMarkerY);
    m->heading = h;
}

// on = hide; the markers are parked at (0x5DC000, 0x5DC000) either way.
void fxMarkersHide(bool on) {
    for (Obj3D* m : fxPools().unitMarkers) {
        if (!m) continue;
        if (on) m->flags &= u16(~obj3d_flag::kEnabled);
        else m->flags |= obj3d_flag::kEnabled;
        m->pos.x = 0x5DC000;
        m->pos.z = 0x5DC000;
    }
}

// ---------------------------------------------------------------------------
// Muzzle flash and projectile effects (1000:5B2F, 5F4E, 604F, 60FA)
// ---------------------------------------------------------------------------

// Flash `fwd` units ahead (and `side` units to the side) of the owner along the
// round's heading (reversed for rockets / mortar); fwd < 0 removes it.
void fxMuzzleFlash(Projectile* p, int side, int fwd) {
    if (!p) return;
    if (s16(fwd) < 0) {
        fxRelease(p->fx_muzzle);
        p->fx_muzzle = nullptr;
        return;
    }
    Obj3D* f = p->fx_muzzle;
    if (!f) {
        f = fxPoolAlloc(fxPools().flash);
        p->fx_muzzle = f;
    }
    if (!f) return;
    f->flags |= obj3d_flag::kEnabled;
    int h = p->body->heading;
    const int cls = int(p->wclass);
    if (cls == 0xF || cls == 0x10 || cls == 0x14) h = angleWrap(h + 0x5A0);
    f->heading = s16(h);
    s16 x = s16(side), z = s16(fwd);
    mathRotate2d(x, z, 0, 0, h);
    const Unit* o = p->owner;
    if (!o) return;
    f->pos.x = wrapAdd(shl8(x), o->body->pos.x);
    f->pos.z = wrapAdd(shl8(z), o->body->pos.z);
    if (o == pointMan()) {
        const Mover* mv = o->mover;
        f->pos.y = shl8(s16((mv->posture == Posture::Crouch ? -1 : -3) + mv->height));
        if (mv->flags & mover_flag::kInWater) f->pos.y = wrapSub(f->pos.y, shl8(mv->height));
    } else if (o->anim) {
        f->pos.y = shl8(muzzleHeight(int(u8(o->mover->posture))));
    } else {
        f->pos.y = o->body->pos.y;
    }
}

// Explosion: the body becomes the burst shape (first call plays sound 0xF, or
// 0x10 for LAAW / mortar); n < 0 restores the normal shape.
void prjStartBurst(Projectile* p, int n) {
    if (!p) return;
    if (n < 0) {
        p->fx_explosion = nullptr;
        p->body->model = p->base_model;
        return;
    }
    Obj3D* e = p->fx_explosion;
    if (!e) {
        e = p->body;
        p->fx_explosion = e;
        const int cls = int(p->wclass);
        sfxPlay(cls == 0xF || cls == 0x14 ? 0x10 : 0xF, 0x100, &e->pos, 1, nullptr);
    }
    if (burstShapeOn(e)) {
        const int c = int(p->wclass);
        if (c == 0xF || c == 0x10 || c == 4 || (c == 5 && isM203Grenade(p)) || c == 0x15 || c == 0x14)
            p->detonate_time = p->impact_time;
    }
}

// Impact puff (once per use): burst shape, hit flags, 0x100 ticks of life, sound 10.
void prjStartImpact(Projectile* p, int n, u8 flags) {
    if (!p) return;
    if (n < 0) {
        p->fx_impact = nullptr;
        p->body->model = p->base_model;
        return;
    }
    if (p->fx_impact) return;
    Obj3D* s = p->body;
    p->fx_impact = s;
    burstShapeOn(s);
    p->detonate_time = p->impact_time;
    p->hit |= flags;
    p->lifetime = 0x100;
    p->timer_base = ms().time;
    sfxPlay(0xA, 0x80, &s->pos, 1, nullptr);
    // Port: the particle burst of the Enhanced impact effects (its own RNG;
    // not again for a human hit whose visual-only puff already spawned one).
    if (impactFxOn() && !p->impact_shown)
        render::impactParticlesSpawn(s->pos, ImpactSurface(p->impact_surface), p->impact_time);
}

// Smoke / gas cloud: burst shape without sound; n < 0 restores the normal shape.
void prjShowBurst(Projectile* p, int n) {
    if (!p) return;
    if (n < 0) {
        p->fx_cloud = nullptr;
        p->body->model = p->base_model;
        return;
    }
    Obj3D* c = p->fx_cloud;
    if (!c) {
        c = p->body;
        p->fx_cloud = c;
    }
    burstShapeOn(c);
}

} // namespace st::game::mission
