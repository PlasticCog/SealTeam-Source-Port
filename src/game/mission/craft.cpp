// Support craft, extraction pickup, craft engine sounds and ambient flyers
// (segment 2dbd part 4, docs/re/seg_2dbd_craft_ordnance.md). The ordnance
// flight and the projectile / effect objects of segment 1000 are in
// ordnance.cpp.
#include "game/mission/craft.h"

#include "data/exeimage.h"
#include "engine/rng.h"
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/entity.h"
#include "game/mission/geo.h"
#include "game/mission/modern.h"
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/sfx.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/mission/world.h"
#include "game/mission/wquery.h"

namespace st::game::mission {

namespace {

constexpr u16 kCraftFarDist = 0x0A68;     // g_craft_far_dist (1200)
constexpr u16 kCraftNearDist = 0x0A6A;    // g_craft_near_dist (180)
constexpr u16 kCraftArriveDist = 0x0A6C;  // g_craft_arrive_dist (30)
constexpr u16 kMsgOutOfAmmo = 0x0A76;     // "Support Team Out of Ammo."

// Position of the current camera (g_cur_camera DS:D8AE, camera +0x00). Set by
// the mission loop; presentation state, so craftReset() leaves it alone.
const Vec3* g_viewer = nullptr;

const Vec3& viewerPos() {
    static const Vec3 kOrigin{};
    if (g_viewer) return *g_viewer;
    const Unit* pm = pointMan();
    return (pm && pm->body) ? pm->body->pos : kOrigin;
}

// speed * g_frame_ticks as the original computes it (16x16 -> 32 bit product,
// shifted left and right by 8: a sign-extended 24-bit value).
s32 frameStep(int speed) {
    const s32 v = s32(s16(speed)) * s32(ms().frameTicks);
    return s32(u32(v) << 8) >> 8;
}

s32 shl8(s32 v) { return s32(u32(v) << 8); }

// River alignment: the feature's heading or its reverse, whichever lies
// within 90 degrees of the desired heading (2dbd:2C62 / 2CF9).
void alignWithRiver(Mover* mv, const WorldObject* w) {
    if (!w || !w->body) return;
    const int fh = w->body->heading;
    const int reverse = angleWrap(fh + 0x5A0) >> 3;
    const int forward = s16(fh) >> 3;
    const int d = evtHeadingDiff(angleWrap(s16(mv->desired_heading << 3)), s16(forward << 3));
    mv->desired_heading = s16(d < 0x5A ? forward : reverse);
}

bool isAirTeam(const Team* t) { return t && (t->type == TeamType::Helicopter || t->type == TeamType::Aircraft); }

} // namespace

void craftReset() {
    // No module-private mission state: the engine-sound timer DS:0A6E is
    // ms().minuteJobTime and the extraction flags live in ms().
}

void craftSetViewerPos(const Vec3* pos) { g_viewer = pos; }

// ---------------------------------------------------------------------------
// Support craft (2dbd:297D)
// ---------------------------------------------------------------------------

void evtUpdateSupportCraft() {
    MissionState& S = ms();
    const s16 farDist = exe().dgShort(kCraftFarDist);
    const s16 nearDist = exe().dgShort(kCraftNearDist);
    const s16 arriveDist = exe().dgShort(kCraftArriveDist);
    WorldObject* water = nullptr;  // [BP-0x0C] feature under the boat
    bool onWater = false;          // [BP-0x2A] only set for boats
    for (int ti = 0; ti < kMaxTeams && S.teams[ti]; ++ti) {
        Team* t = S.teams[ti];
        const int type = int(t->type);
        if (type != 2 && type != 3 && type != 1) continue;
        for (int mi = 0; mi < 8 && t->members[mi]; ++mi) {
            Unit* u = t->members[mi];
            Mover* mv = u->mover;
            Obj3D* body = u->body;
            int dist = geoDistance(body->pos, mv->destination);

            // 1. Boat on water.
            if (type == 1) {
                water = wldProbeSolid(body);
                if (water && (water->kind == TerrainKind::Water || water->kind == TerrainKind::DeepWater)) {
                    onWater = true;
                    if (!(mv->flags & mover_flag::kInWater) && dist < 0x78) mv->flags |= mover_flag::kInWater;
                } else {
                    onWater = false;
                    mv->flags &= u8(~mover_flag::kInWater);
                    evtSetAltitudeLevel(u, 2);
                    evtSetMoveMode(u, mv->heading == mv->desired_heading ? 2 : 0);
                }
            }

            // 2. Collision with another craft (1000:3E4C).
            if (type != 0 && type < 4) {
                Unit* other = wldProbeUnit(body);
                if (other && other->team && isCraftTeam(other->team)) {
                    mv->desired_heading = s16(other->body->heading >> 3);
                    if (isAirTeam(u->team) && isAirTeam(other->team)) {
                        const s16 step = s16((s32(0x24) * ms().frameTicks) >> 8);
                        if (other->mover->height <= mv->height) {
                            mv->height = s16(mv->height + step);
                        } else if (mv->height >= 0x24) {
                            mv->height = s16(mv->height - step);
                        }
                    }
                    if (u->team->type == TeamType::Boat && u->team->members[0] != u) evtSetMoveMode(u, 0);
                }
            }

            // 3./4. Steering.
            if (type == 1 && t->order == 4 && (mv->flags & mover_flag::kInWater)) {
                water = evtSetDestToTerrainObj(u, water);
                dist = geoDistance(body->pos, mv->destination);
                const Vec3& goal = (team(S.extractionGroup) == t) ? S.extractMarker : mv->destination;
                mv->desired_heading = s16(geoBearing(body->pos, goal));
                alignWithRiver(mv, water);
            } else if (t->order != 5 || dist >= nearDist) {
                mv->desired_heading = s16(geoBearing(body->pos, mv->destination));
                if (type == 1) {
                    if (onWater && dist > 0x384) {
                        alignWithRiver(mv, water);
                    } else if (!onWater) {
                        water = evtSetDestToTerrainObj(u, nullptr);
                        mv->desired_heading = s16(geoBearing(body->pos, mv->destination));
                    }
                }
            }

            // 5. Orders (leader only; the switch compares unsigned, so -1 does nothing).
            if (mi == 0) {
                switch (u16(t->order)) {
                case 0:
                case 1:
                case 2:
                case 3: {
                    int mode;
                    if (dist < arriveDist) {
                        evtSetAltitudeLevel(u, 2);
                        mode = 0;
                    } else if (dist < nearDist) {
                        evtSetAltitudeLevel(u, 1);
                        mode = 1;
                    } else {
                        evtSetAltitudeLevel(u, 0);
                        mode = 2;
                    }
                    evtSetMoveMode(u, mode);
                    // Everybody aboard: fly away 3000 units east.
                    if (entTeamAllExtracted()) mv->destination.x = wrapAdd(body->pos.x, 0xBB800);
                    break;
                }
                case 4:
                    if (type == 1 && dist < 0x12C) {
                        evtSetAltitudeLevel(u, 2);
                        evtSetMoveMode(u, 0);
                    } else if (type != 1 && dist < nearDist) {
                        if (type == 2) evtSetMoveMode(u, 0);  // helicopter hovers, aircraft keeps circling
                    } else {
                        evtSetAltitudeLevel(u, 0);
                        evtSetMoveMode(u, 2);
                    }
                    break;
                case 5:
                    if (dist >= nearDist) {
                        evtSetAltitudeLevel(u, 0);
                        evtSetMoveMode(u, 2);
                    } else {
                        evtSetAltitudeLevel(u, 1);
                        evtSetMoveMode(u, 1);
                    }
                    break;
                case 6:
                    if (dist < farDist) {
                        mv->desired_heading = s16(mv->desired_heading + 0xB4);
                        // Original quirk (2dbd:2EBD): "> 360", so 360 itself is kept.
                        if (mv->desired_heading > 0x168) mv->desired_heading = s16(mv->desired_heading - 0x168);
                        evtSetMoveMode(u, 2);
                    } else {
                        t->order = 4;
                    }
                    break;
                default:
                    break;
                }
            }

            // 6. Out of ammunition during an attack (every member).
            if (t->order == 5 && u->loadout && u->loadout->primary && u->loadout->secondary) {
                const WeaponNode* a = u->loadout->primary;
                const WeaponNode* b = u->loadout->secondary;
                if (a->reloads == 0 && a->rounds == 0 && b->reloads == 0 && b->rounds == 0) {
                    msgShowDs(kMsgOutOfAmmo, 0x200);
                    t->order = 6;
                    geoRandomOffsetDry(mv->destination, s16(nearDist * 3), farDist);
                }
            }

            // 7. Speed, heading, movement, altitude.
            if (mv->speed != mv->target_speed) {
                if (mv->target_speed == 0) evtApproachZero(mv->speed, mv->target_speed, mv->decel);
                else evtApproachValue(mv->speed, mv->target_speed, mv->accel);
            }
            if (mv->heading != mv->desired_heading) {
                evtApproachAngle(mv->heading, mv->desired_heading, mv->turn_rate);
                body->heading = s16(mv->heading << 3);
            }
            const int h = body->heading;
            if (mv->speed > 0) {
                posMovePolar(frameStep(mv->speed), 0, h, body->pos);
            } else if (mv->speed < 0) {
                const int back = angleWrap(h + 0x5A0);
                posMovePolar(frameStep(s16(-mv->speed)), 0, back, body->pos);
            }
            if (mv->height != mv->target_height) {
                if (mv->target_height == 0) evtApproachZero(mv->height, mv->target_height, mv->descent_rate);
                else evtApproachValue(mv->height, mv->target_height, mv->climb_rate);
                body->pos.y = shl8(mv->height);
            }

            // 8. Attitude.
            if (type == 1) {
                int n = mv->speed >> 5;
                if (n < 1) n = 1;
                const int r = engine::rng().range(n);
                int k = mv->speed >> 2;
                if (k > 4) k = 4;
                body->pitch = s16((r + k) << 3);
            }
            if (type == 2 || type == 3) {
                const int limit = type == 2 ? 12 : 8;
                if (type == 2) {
                    int p = mv->speed >> 2;
                    if (p > 12) p = 12;
                    body->pitch = s16(angleWrap(s16(-(p << 3))));
                }
                const int h8 = s16(mv->heading << 3), d8 = s16(mv->desired_heading << 3);
                int bank = evtHeadingDiff(h8, d8);
                if (bank > limit) bank = limit;
                const int sign = evtIsPositiveTurn(d8, h8) ? -1 : 1;
                body->roll = s16(angleWrap(s16(s16(sign * bank) << 3)));
            }
        }
    }
}

// 2dbd:31EC
void evtTeamSetMoverFlags(Team* t, u8 bits) {
    if (!t) return;
    for (int i = 0; i < 8 && t->members[i]; ++i) t->members[i]->mover->flags |= bits;
}

// ---------------------------------------------------------------------------
// Extraction pickup (2dbd:3232)
// ---------------------------------------------------------------------------

void evtUpdateExtractionPickup() {
    MissionState& S = ms();
    if (entTeamAllExtracted()) {
        // The squad rides with the extraction craft (team index DS:EC92).
        const Team* ct = team(S.extractingGroup);
        const Unit* craft = ct ? ct->members[0] : nullptr;
        Team* squad = S.teams[0];
        if (craft && squad)
            for (int i = 0; i < 8 && squad->members[i]; ++i) squad->members[i]->body->pos = craft->body->pos;
    }
    Unit* pm = pointMan();
    if (!pm) return;
    for (int ti = 0; ti < kMaxTeams && S.teams[ti]; ++ti) {
        Team* t = S.teams[ti];
        if (t->type != TeamType::Helicopter && t->type != TeamType::Boat) continue;
        Unit* leader = t->members[0];
        if (t->order != 2 && t->order != 3) continue;
        if (!leader) continue;
        if (geoDistance(pm->body->pos, leader->body->pos) >= 0x2A) continue;
        if (wrapSub(leader->body->pos.y, pm->body->pos.y) >= 0x3C00) continue;

        leader->mover->desired_heading = s16(pm->body->heading >> 3);
        for (int ui = 0; ui < kMaxTeams && S.teams[ui]; ++ui) {
            Team* g = S.teams[ui];
            if (g->type != TeamType::Seal && g->type != TeamType::Friendly) continue;
            // Team 0's member list can grow while it is walked (365e:17B1 merges the split teams).
            for (int idx = 0; idx < 8 && g->members[idx]; ++idx) {
                Unit* m = g->members[idx];
                int nearFlag = 0;
                bool board = unitDead(m) || medStatus(m->status) == 2;
                if (!board) {
                    nearFlag = geoDistance(m->body->pos, leader->body->pos) < 0x3C ? 1 : 0;
                    board = nearFlag != 0;
                }
                if (!board) continue;
                bool placeOnCraft = idx != 0;
                if (!placeOnCraft) {
                    if (ui == 0) {
                        entTeamRejoin();
                        g->order = -1;
                    }
                    if (m->mover->flags & mover_flag::kAboard) {
                        placeOnCraft = true;
                    } else if (nearFlag) {
                        // Original quirk (2dbd:34AC): moves by (1 >> 2) << 8 = 0, a no-op.
                        posMovePolar(shl8(nearFlag >> 2), 0, m->body->heading, m->body->pos);
                    }
                }
                if (placeOnCraft) m->body->pos = leader->body->pos;
                m->mover->flags |= mover_flag::kAboard;
                evtSetMoveMode(m, 0);
                unitHideChain(m);
            }
            g->formation = Formation::Column;
            g->order = -1;
        }
        misStartExtraction();
        return;
    }
}

// ---------------------------------------------------------------------------
// Engine sounds (2dbd:3517 / 352C)
// ---------------------------------------------------------------------------

void evtForceCraftEngineSound() {
    MissionState& S = ms();
    S.minuteJobTime = wrapSub(S.time, 0x3C00);
}

void evtPlayCraftEngineSounds() {
    MissionState& S = ms();
    for (int ti = 0; ti < kMaxTeams && S.teams[ti]; ++ti) {
        const Team* t = S.teams[ti];
        const Unit* leader = t->members[0];
        if (!leader) continue;
        // Port (Modern gameplay): the parked Phantom flight is off-map and
        // silent (its engine sound would only hold one of the six channels).
        if (modernIsPhantomGroup(ti) && modernPhantomPhase() == 0) continue;
        int id = -1;
        if (t->type == TeamType::Helicopter) {
            id = 0x17;
        } else if (t->type == TeamType::Aircraft) {
            id = 0x16;
        } else if (t->type == TeamType::Boat) {
            const MoveMode mode = leader->mover->move_mode;
            if (mode != MoveMode::Stop) id = mode == MoveMode::Run ? 0x15 : 0x14;
        }
        if (id >= 0) sfxPlay(id, 0x3C00, &leader->body->pos, 1, leader);
    }
}

// ---------------------------------------------------------------------------
// Ambient flyers (2dbd:4C74)
// ---------------------------------------------------------------------------

void evtUpdateAmbientFlyer(Obj3D* o) {
    if (!o) return;
    MissionState& S = ms();
    if (o->pos.y < 0x12C00) {
        // Low: birds. Heading bit 3 marks the flapping variant.
        int speed, k;
        if (o->heading & 8) {
            speed = 0x30;
            k = 8;
        } else {
            speed = 0x48;
            k = 3;
        }
        posMovePolar(frameStep(speed), o->pitch, o->heading, o->pos);
        const int up = angleWrap(0x2D0);
        const int rise = engine::rng().range(k);
        posMovePolar(frameStep(s16(rise * 3)), up, o->heading, o->pos);
        const int down = angleWrap(-0x2D0);
        const int sink = engine::rng().range(k - 2);
        posMovePolar(frameStep(s16(sink * 3)), down, o->heading, o->pos);
        if (o->pos.y < 0x1E00) {
            o->roll = s16(o->roll == 0xF0 ? -0xF0 : 0xF0);
        } else if (o->heading & 8) {
            const int r = engine::rng().range(2);
            o->roll = s16(angleWrap((r == 0 ? -1 : 1) * 0xF0));
        } else {
            o->roll = 0;
        }
        if (o->pos.y <= 0) o->pos.y = 0x300;
        if (geoDistance(o->pos, viewerPos()) > 0x384) o->flags &= u16(~obj3d_flag::kEnabled);
    } else {
        // High: aircraft in level flight.
        o->pitch = 0;
        o->roll = 0;
        const s32 d = s32(u32(s32(S.frameTicks)) * 0x61800u) >> 8;
        posMovePolar(d, 0, o->heading, o->pos);
        if (geoDistance(o->pos, viewerPos()) > 0x7530) o->flags &= u16(~obj3d_flag::kEnabled);
    }
}

} // namespace st::game::mission
