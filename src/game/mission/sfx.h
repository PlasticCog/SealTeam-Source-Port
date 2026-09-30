// Mission-side wrapper of the sound-effect layer (segment 4592, implemented
// in engine/sound.*): snd_play_sfx with game positions and attached units.
#pragma once

#include "game/types.h"

namespace st::game::mission {

// 4592:0135 snd_play_sfx(id, lifetime, pos, fm_param, attached unit).
// `pos` NULL = unpositioned (full volume); an attached unit's body position
// is followed while the effect plays. Returns the channel or -1. The
// original never draws random numbers here.
int sfxPlay(int id, s32 lifetime, const Vec3* pos, int fmParam, const Unit* attached);
// 19ac:5251 snd_radio_ack(n): sfxPlay(n, 0x200, Point Man position, 1, NULL).
void sfxRadioAck(int id);
void sfxStopAll();                // 4592:04FF
void sfxStopChannel(int ch);      // 4592:0461

// Observer for the sim log (called for every successful or refused request).
void setSfxObserver(void (*fn)(int id, s32 lifetime, const Vec3* pos, int channel));

} // namespace st::game::mission
