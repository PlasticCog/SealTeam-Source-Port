// The game's sound layer (segment 4592, docs/re/seg_libs.md 12) on top of the
// AIL-compatible audio engine in src/audio. Music part; effects follow with
// the mission code.
#pragma once

#include "core/common.h"

namespace st::engine {

class Sound {
public:
    // snd_init: music device and timbre bank (SAMPLE.AD for AdLib/SB OPL2).
    void init();
    void shutdown();

    bool musicEnabled() const { return musicEnabled_; }
    void setMusicEnabled(bool on) { musicEnabled_ = on; }

    // snd_load_music(base, id): mscNN.xmi with NN = base + id (base + 1 if id == 0).
    bool loadMusic(int base, int id);
    // snd_music_play(n): start sequence n of the loaded file at the music volume.
    void play(int sequence);
    void stop();                               // snd_music_stop
    bool done() const;                         // snd_music_done
    // snd_music_start_once(n): only once per load and only while not faded.
    void startOnce(int sequence);
    void unload();

private:
    bool ready_ = false;
    bool musicEnabled_ = true;
    bool startedSinceLoad_ = false;
    int loadedNumber_ = -1;
    struct Impl;
    Impl* impl_ = nullptr;
};

Sound& sound();

} // namespace st::engine
