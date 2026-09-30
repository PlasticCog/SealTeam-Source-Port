// Soldier animation state machine of segment 348e (docs/re/seg_2dbd.md 4.2):
// the per-frame animation clocks, posture transitions and timed animations
// (falls, throws, reloads). Frame selection and drawing belong to the renderer.
#include "game/mission/people.h"

#include "engine/rng.h"
#include "game/mission/geo.h"
#include "game/mission/state.h"

namespace st::game::mission {

// 348e:000E: the clocks of the foot teams (types 0 and >= 4) run with the game.
void sprAdvanceAnimClocks() {
    MissionState& S = ms();
    for (int i = 0; i < kMaxTeams && S.teams[i]; ++i) {
        const Team* t = S.teams[i];
        if (t->type != TeamType::Seal && int(t->type) < 4) continue;
        for (int m = 0; m < 8 && t->members[m]; ++m) {
            Anim* a = t->members[m]->anim;
            if (a) a->clock = wrapAdd(a->clock, s32(S.frameTicks));
        }
    }
}

// 348e:0085: throw by posture (ua / ca / pa).
void sprStartThrowAnim(Unit* u) {
    if (!u->anim) return;
    switch (u->anim->posture) {
    case 0: sprSetAnim(u, int(AnimState::ThrowUp)); break;
    case 1: sprSetAnim(u, int(AnimState::ThrowCrouch)); break;
    case 2: sprSetAnim(u, int(AnimState::ThrowProne)); break;
    default: break;
    }
}

// 348e:00C8
void sprSetAnim(Unit* u, int state) {
    Anim* a = u->anim;
    if (!a) return;
    const s16 s = s16(state);
    const s16 p = a->posture;
    // Timed animation: state, return state, duration and start = clock.
    auto timed = [a](int st, int ret, int duration) {
        a->state = AnimState(st);
        a->return_state = AnimState(ret);
        a->duration = s16(duration);
        a->start = a->clock;
    };
    // Posture transition of states 0..3: the duration stays unchanged when
    // no transition is needed.
    auto transition = [a, s](int st, int duration) {
        a->state = AnimState(st);
        if (duration) a->duration = s16(duration);
        a->start = a->clock;
        a->return_state = AnimState(s);
    };
    switch (u16(s) > 0x13 ? -1 : s) {
    case 0:  // upright
        if (p == 1) transition(5, 0x80);        // uc reversed
        else if (p == 2) transition(8, 0x80);   // cp reversed (then 5, see sprUpdateAnim)
        else transition(s, 0);
        break;
    case 1:  // crouch
        if (p == 0) transition(4, 0x80);        // uc
        else if (p == 2) transition(8, 0x80);   // cp reversed
        else transition(s, 0);
        break;
    case 2:  // prone
        if (p == 0) transition(6, 0x100);       // up (dive)
        else if (p == 1) transition(7, 0x80);   // cp
        else transition(s, 0);
        break;
    case 3:  // dead
        if (p == 0) transition(9, 0x200);       // uf
        else transition(13, 0x100);             // cf
        break;
    case 9:
        if (p == 0) timed(9, 3, 0x100);
        break;
    case 11:
    case 12:
        if (p == 0) timed(s, 0, 0x100);
        break;
    case 13:
        if (p == 1) timed(13, 3, 0x100);
        break;
    case 14:
    case 15:
        if (p == 1) timed(s, 1, 0x100);
        break;
    case 16:
    case 17:
        if (p == 1) timed(s, 1, 0x300);
        break;
    case 18:
    case 19:
        if (p == 2) timed(s, 2, 0x100);
        break;
    default:  // 4..8, 10 and anything above 19: no timing
        a->state = AnimState(s);
        a->return_state = AnimState(s);
        break;
    }
    if (s <= 3) {
        a->posture = s;
        a->posture_copy = s;
    }
}

// 348e:0302: 1 while a timed animation plays.
int sprUpdateAnim(Unit* u) {
    Anim* a = u->anim;
    if (!a) return 0;
    if (a->state == a->return_state) return 0;
    Ticks finished = 0;
    if (wrapSub(a->clock, a->start) >= s32(a->duration)) {
        a->start = a->clock;
        finished = a->clock;
    }
    // Original quirk (348e:0368): "finished" is the clock value, so an
    // animation that ends while the clock is exactly 0 plays on.
    if (finished == 0) return 1;
    if (a->state == AnimState::ProneToCrouch && a->posture == 0) {
        // Prone to upright continues with crouch to upright.
        a->posture = 1;
        a->posture_copy = 1;
        sprSetAnim(u, 0);
        return 1;
    }
    a->state = a->return_state;
    return 0;
}

// 348e:158C
void sprInitUnitAnim(Unit* u, int kind) {
    if (!u || !u->anim) return;
    Anim* a = u->anim;
    a->sets = nullptr;  // 53BA:015E soldier sprite sets: owned by the renderer
    a->created = ms().time;
    a->zero_00 = 0;
    a->kind = u8(kind);
    a->posture = 0;
    a->posture_copy = 0;
    a->return_state = AnimState::Upright;
    a->state = AnimState::Upright;
    a->clock = engine::rng().range(0x100);  // random phase
}

} // namespace st::game::mission
