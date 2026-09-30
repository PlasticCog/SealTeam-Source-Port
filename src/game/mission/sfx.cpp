#include "game/mission/sfx.h"

#include "engine/sound.h"
#include "game/mission/state.h"

namespace st::game::mission {

namespace {
void (*g_observer)(int, s32, const Vec3*, int) = nullptr;
} // namespace

void setSfxObserver(void (*fn)(int id, s32 lifetime, const Vec3* pos, int channel)) { g_observer = fn; }

int sfxPlay(int id, s32 lifetime, const Vec3* pos, int fmParam, const Unit* attached) {
    engine::Sound& snd = engine::sound();
    snd.setGameClock(ms().time, ms().tcState != 0);
    const s32* p = pos ? &pos->x : nullptr;
    const s32* follow = (attached && attached->body) ? &attached->body->pos.x : nullptr;
    const int ch = snd.playSfx(id, lifetime, p, fmParam, follow);
    if (g_observer) g_observer(id, lifetime, pos, ch);
    return ch;
}

void sfxRadioAck(int id) {
    Unit* pm = pointMan();
    sfxPlay(id, 0x200, pm ? &pm->body->pos : nullptr, 1, nullptr);
}

void sfxStopAll() { engine::sound().stopAllSfx(); }

void sfxStopChannel(int ch) { engine::sound().stopChannel(ch); }

} // namespace st::game::mission
