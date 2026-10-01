// AI noise events of segment 1000 (1000:841A..892E, docs/re/seg_1000.md 14):
// 20 events at far 53BA:26DA (ms().noise), levels by type from far 525C
// (gd().noiseLevel). Created by moving teams (every 0x400 ticks), weapons and
// explosions; heard by the AI through noise_hear.
#include "game/mission/ai.h"

#include "engine/rng.h"
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/modern.h"
#include "game/mission/sfx.h"
#include "game/mission/state.h"
#include "game/mission/wquery.h"

namespace st::game::mission {

namespace {

// noise_from_team's type local (bp-2) is not initialised: a foot team that
// stands still keeps the value of the previous call (the calls come from the
// same noise_update frame). Emulated by this variable, cleared by noiseReset.
s16 g_lastNoiseType = 0;

int rng(int n) { return engine::rng().range(n); }

s16 levelOf(int type) { return gd().noiseLevel[(type >= 0 && type < 0x14) ? type : 0]; }

NoiseEvent* newEvent(NoiseType type) {
    const int idx = noiseAlloc();
    if (idx < 0) return nullptr;
    NoiseEvent& e = ms().noise[idx];
    e.free = 0;
    e.heard = 0;
    e.type = type;
    return &e;
}

} // namespace

void noiseReset() {
    for (NoiseEvent& e : ms().noise) e.free = 1;
    g_lastNoiseType = 0;
}

int noiseAlloc() {
    for (int i = 0; i < kNoiseEvents; ++i)
        if (ms().noise[i].free & 0xFF) return i;
    return -1;
}

void noiseFromTeam(Team* t) {
    if (!t || !t->members[0]) return;
    // Port (Modern gameplay): the parked Phantom flight is off-map; it makes
    // no noise event (and takes no roll) until it is called in.
    if (modernIsPhantomTeam(t) && modernPhantomPhase() == 0) return;
    bool moving = false;
    Unit* leader = t->members[0];
    s16 level = 0;
    s16& type = g_lastNoiseType;
    Vec3 pos = leader->body->pos;
    const int ty = int(t->type);
    if (ty != 0 && ty < 4) {
        // Craft engines.
        if (ty == 1) type = s16(NoiseType::Boat);
        if (ty == 2) type = s16(NoiseType::Helicopter);
        if (ty == 3) type = s16(NoiseType::Aircraft);
        level = 100;
    } else {
        // wld_ground_type probes clear pos.y (quirk of 1000:3757), which the
        // event keeps.
        if (wldGroundType(&pos) == 1) {
            if (leader->mover->move_mode == MoveMode::Run) {
                type = s16(NoiseType::Running);
                level = 15;
                moving = true;
                if (t != pointMan()->team) sfxPlay(0x19, 0x100, &pos, 1, leader);
            } else if (leader->mover->move_mode != MoveMode::Stop) {
                type = s16(NoiseType::Walking);
                moving = true;
                level = 10;
            }
        }
        if (wldGroundType(&pos) == 2 && leader->mover->move_mode != MoveMode::Stop) {
            type = s16(NoiseType::Wading);
            sfxPlay(0x18, 0x100, &pos, 1, leader);
            moving = true;
            level = 5;
        }
        const Status* s = leader->status;
        const int agi = s->agility / 30;
        const int size = s->size / 30;
        const int exp = s->experience / 30;
        level = s16(level - 5 * (agi - size + exp));
    }
    // Enemy radio chatter while engaging.
    if (ty == 4 || ty == 5) {
        bool radio = false;
        for (int i = 0; i < 8 && t->members[i]; ++i)
            if (invCountItems(t->members[i], int(ItemType::Radio)) > 0) radio = true;
        const Brain* b = leader->brain;
        if (b && (b->flags & brain_flag::kEngaging) && radio) {
            type = s16(NoiseType::EnemyRadio);
            level = 99;
        }
    }
    if (rng(100) > level) return;
    if (rng(100) <= 5 && moving) {
        if (rng(100) <= 0x4B) {
            type = s16(NoiseType::Cough);
            sfxPlay(0x21, 0x100, &pos, 1, leader);
        } else {
            type = s16(NoiseType::Sneeze);
            sfxPlay(0x22, 0x100, &pos, 1, leader);
        }
    }
    NoiseEvent* e = newEvent(NoiseType(type));
    if (!e) return;
    e->level = levelOf(type);
    e->source = t;
    e->pos = pos;
}

void noiseUpdate() {
    MissionState& S = ms();
    for (int i = 0; i < kMaxTeams && S.teams[i]; ++i) noiseFromTeam(S.teams[i]);
    // Events live for one to two updates.
    for (NoiseEvent& e : S.noise) {
        if (e.free & 0xFF) continue;
        if (!(e.heard & 0xFF)) e.heard = 1;
        else e.free = 1;
    }
}

// 1000:87BF noise_from_weapon(slot, fire mode, shooter position, shooter team).
void noiseFromWeapon(const Unit* shooter, const WeaponNode* w) {
    if (!shooter) return;
    NoiseEvent* e = newEvent(NoiseType::Weapon);
    if (!e) return;
    const int fireMode = w ? w->fire_mode : 0;
    e->level = fireMode == 0 ? 6 : s16(u16(weaponDef(u8(w->type)).noise) << 2);
    e->source = shooter->team;
    e->pos = shooter->body->pos;
}

// 1000:888A noise_from_explosion(pos, source team): no caller in st.exe; the
// port's declaration has no source team (the event is then ignored by
// noiseHear, which follows the source team's leader).
void noiseFromExplosion(const Vec3& pos) {
    NoiseEvent* e = newEvent(NoiseType::Explosion);
    if (!e) return;
    e->level = levelOf(int(NoiseType::Explosion));
    e->source = nullptr;
    e->pos = pos;
}

bool noiseHear(const Unit* listener, Vec3& outPos) {
    if (!listener || !listener->brain || !listener->brain->team) return false;
    const MissionState& S = ms();
    const Team* lt = listener->brain->team;
    const int ltt = int(lt->type);
    s16 best = 0;
    for (const NoiseEvent& e : S.noise) {
        if (e.free & 0xFF) continue;
        if (e.source == lt) continue;
        if (!e.source || !e.source->members[0]) continue;
        // Original: the level falls off with the distance to the source
        // team's leader now, not to the event position.
        const Vec3 lpos = listener->body->pos;
        const Vec3 spos = e.source->members[0]->body->pos;
        const int stt = int(e.source->type);
        bool opposed;
        if (ltt > 3 && ltt < 6) opposed = stt <= 3 || stt == 7;
        else opposed = stt >= 4 && stt <= 5;
        if (!opposed && e.type != NoiseType::Weapon) continue;
        s16 lvl = s16(e.level - geoDistance(lpos, spos));
        if (lvl < 0) lvl = 0;
        const bool enemyListener = ltt == 4 || ltt == 5;
        if (S.opt.intelligence == 0 && enemyListener && lvl <= 90) lvl = 0;
        if (S.opt.intelligence == 1 && enemyListener && lvl <= 30) lvl = 0;
        if (lt->ai && lt->ai->behaviour == ai_behaviour::kWary && entGroupIsPassiveNpc(e.source)) lvl = -1;
        if (lvl > best) {
            best = lvl;
            outPos = e.source->members[0]->body->pos;
        }
    }
    return best > 0;
}

} // namespace st::game::mission
