// The game's sound layer (segment 4592, docs/re/seg_libs.md 12) on top of the
// AIL-compatible audio engine in src/audio: music (XMIDI sequences) and the
// sound-effect channels (6 channels, one digital voice with priorities,
// FM effects as XMIDI sequences id+10 of the effects bank, positional volume).
//
// Port settings honoured here (they model the player's sound hardware and
// mixer, not game logic): settings().musicDevice selects OPL2 + SAMPLE.AD or
// OPL3 + SAMPLE.OPL; musicVolume / sfxVolume scale the game's own volumes;
// digitalSfx = false (or the command-line 'd' flag) leaves only FM effects.
//
// Effect positions are 3 x int32 24.8 world coordinates (x, altitude, z)
// passed as pointers to the first element, so this layer needs no game types.
#pragma once

#include "core/common.h"

namespace st::engine {

constexpr int kSfxCount = 53;    // effect ids 1..53
constexpr int kSfxChannels = 6;  // far 53BA:258C

class Sound {
public:
    // snd_init: music device, timbre bank, digital availability.
    void init();
    void shutdown();

    bool musicEnabled() const { return musicEnabled_; }
    void setMusicEnabled(bool on) { musicEnabled_ = on; }

    // snd_load_music(base, id): mscNN.xmi with NN = base + id (base + 1 if id == 0).
    bool loadMusic(int base, int id);
    // snd_music_play(n): start sequence n of the loaded file at the music volume
    // (only with music enabled; then n becomes the current track).
    void play(int sequence);
    // g_music_current (DS:EFD8): sequence last started by play().
    int currentTrack() const { return currentTrack_; }
    void stop();                               // snd_music_stop
    // snd_music_done: true once a sequence started by play() has ended
    // (false before any sequence was started since the last load).
    bool done() const;
    // snd_music_start_once(n): only once per load and only while not faded.
    void startOnce(int sequence);
    void unload();

    // ---- Sound effects (4592:0135..11E0) ---------------------------------
    // snd_load_sfx_bank (4592:079E): the effects bank msc01.xmi becomes the
    // current music file (it also holds the mission music), then the
    // effects 53..1 are loaded (sfxNN.voc for digital versions).
    void loadSfxBank();
    // snd_unload_sfx_bank (4592:07D5): stop channels, forget the effects,
    // stop the music and release its sequences.
    void unloadSfxBank();
    // snd_reset_channels (4592:051F).
    void resetChannels();
    // snd_play_sfx (4592:0135): returns the channel 0..5 or -1.
    int playSfx(int id, s32 lifetime, const s32* pos, int fmParam, const s32* follow);
    void stopChannel(int ch);      // 4592:0461
    void stopAllSfx();             // 4592:04FF
    // snd_update (4592:05E2), once per rendered frame.
    void updateSfx();
    // snd_set_listener_pos (4592:000D); nullptr = origin.
    void setListener(const s32* pos);

    // Game state the effect layer reads: g_time and the time-compression
    // flag (DS:D7ED; no new effects while it is set).
    void setGameClock(s32 now, bool timeCompression) { now_ = now; timeCompression_ = timeCompression; }
    // g_snd_enabled (DS:45C0, Alt-S).
    void setSfxEnabled(bool on) { sfxEnabled_ = on; }
    bool sfxEnabled() const { return sfxEnabled_; }
    // Keep the channel and music bookkeeping but send nothing to the audio
    // device (headless simulation runs).
    void setMuted(bool on) { muted_ = on; }

    struct ChannelInfo {
        int id = 0;
        u16 flags = 0;      // bit0 active, bit1 from descriptor, bit3 unpositioned, bit4 digital voice
        s32 start = 0;
        s32 lifetime = 0;
        s32 pos[3] = {};
    };
    ChannelInfo channel(int ch) const;

private:
    struct Impl;
    int channelVolume(int ch);
    void applyVolume(int ch, int vol);

    bool ready_ = false;
    bool musicEnabled_ = true;
    bool startedSinceLoad_ = false;
    int loadedNumber_ = -1;
    bool sfxEnabled_ = true;
    bool muted_ = false;
    int currentTrack_ = 0;
    bool musicStarted_ = false;
    bool timeCompression_ = false;
    s32 now_ = 0;
    Impl* impl_ = nullptr;
};

Sound& sound();

} // namespace st::engine
