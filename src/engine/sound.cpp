#include "engine/sound.h"

#include "audio/audio.h"
#include "core/settings.h"
#include "data/ealib.h"
#include "data/exeimage.h"
#include "engine/palette_fade.h"
#include "game/globals.h"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace st::engine {

namespace {
// g_music_volume: 96 % AIL relative volume.
constexpr int kMusicVolume = 122;
// Sound-effect table in st.exe: far 520D:0000 listener, descriptors at +000C + id*0x16.
constexpr u16 kSfxSeg = 0x520D;
constexpr u16 kSfxDescBase = 0x000C;
constexpr int kFmMaxVol = 0x7F;    // g_fm_max_vol DS:45D0
constexpr int kDigiMaxVol = 0x50;  // g_digi_max_vol DS:45CF
constexpr int kSndBank = 1;        // g_snd_bank DS:45CE: 1 = msc01.xmi (FM drivers)

enum : u16 { kChActive = 0x01, kChDesc = 0x02, kChUnpositioned = 0x08, kChDigital = 0x10 };

// 1000:321F geo_distance on two int32[3] positions (x, y, z; y ignored).
int groundDistance(const s32* a, const s32* b) {
    auto absd = [](s32 v) { return v < 0 ? s32(0u - u32(v)) : v; };
    const s32 dz = absd(s32(u32(b[2]) - u32(a[2])));
    const s32 dx = absd(s32(u32(b[0]) - u32(a[0])));
    const s32 mn = dz > dx ? dx : dz, mx = dz > dx ? dz : dx;
    const s32 v = s32(u32((mn * 3) >> 3) + u32(mx)) >> 8;
    const s16 r = s16(v);
    return r < 0 ? 0x7FFF : r;
}
} // namespace

struct Sound::Impl {
    std::shared_ptr<const audio::XmiFile> xmi;  // current music file (also the effects bank)

    struct Desc {
        u16 distance = 0;
        u16 flags = 0;
        bool fm = false;
        bool digital = false;
        u16 priority = 0;
        std::shared_ptr<const audio::PcmSound> sample;  // loaded digital version
        bool fmReady = false;                           // bank has sequence id+10
    };
    Desc desc[kSfxCount + 1];
    bool descLoaded = false;

    struct Channel {
        int id = 0;
        u16 flags = 0;
        int fmParam = 0;
        s32 start = 0;
        s32 lifetime = 0;
        s32 pos[3] = {};
        const s32* follow = nullptr;
        audio::SequenceHandle fmSeq = 0;  // 0 = none (-1 in the original)
        audio::SampleHandle sample = 0;
    };
    Channel ch[kSfxChannels];
    s32 listener[3] = {};
    bool digiOk = false;   // g_digi_ok
    bool fmOk = false;     // g_fm_sfx_ok
    bool digiBusy = false; // g_digi_busy
    int digiChannel = -1;  // g_digi_channel
};

Sound& sound() {
    static Sound instance;
    return instance;
}

void Sound::init() {
    impl_ = new Impl;
    const bool opl3 = settings().musicDevice == MusicDevice::SoundBlasterPro2;
    audio::Config cfg;
    cfg.musicDevice = opl3 ? audio::MusicDevice::Opl3 : audio::MusicDevice::Opl2;
    cfg.digitalGain = float(std::clamp(settings().sfxVolume, 0, 100)) / 100.0f;
    const bool audioOk = audio::init(cfg);
    if (!audioOk) logWarn("audio: no output device, continuing silently");
    // snd_init step 4: the Global Timbre Library SAMPLE.<driver suffix>.
    std::vector<u8> gtl;
    const char* bank = opl3 ? "SAMPLE.OPL" : "SAMPLE.AD";
    if (resources().read(bank, gtl) && audio::loadTimbreBank(gtl)) {
        ready_ = true;
    } else {
        logWarn("audio: %s timbre bank not available, music disabled", bank);
        musicEnabled_ = false;
    }
    audio::setMusicVolume(kMusicVolume * std::clamp(settings().musicVolume, 0, 100) / 100);
    impl_->fmOk = ready_;
    // snd_init step 3: the digital driver only when wanted (g_digi_wanted,
    // cleared by the 'd' option) and, in the port, when the effects setting allows it.
    impl_->digiOk = game::g().digitalAllowed && settings().digitalSfx;
    audio::setDigitalEnabled(impl_->digiOk);
    resetChannels();
}

void Sound::shutdown() {
    if (impl_) {
        stopAllSfx();
    }
    audio::shutdown();
    delete impl_;
    impl_ = nullptr;
    ready_ = false;
}

bool Sound::loadMusic(int base, int id) {
    if (!impl_) return false;
    const int n = id == 0 ? base + 1 : base + id;
    char name[16];
    std::snprintf(name, sizeof name, "msc%02d.xmi", n);
    std::vector<u8> data;
    if (!resources().read(name, data)) {
        logWarn("audio: %s not found", name);
        return false;
    }
    impl_->xmi = audio::loadXmi(data);
    if (!impl_->xmi) return false;
    audio::preloadTimbres(impl_->xmi, id != 0 ? 11 : 16);
    loadedNumber_ = n;
    startedSinceLoad_ = false;
    musicStarted_ = false;
    return true;
}

void Sound::play(int sequence) {
    if (!ready_ || !musicEnabled_ || !impl_ || !impl_->xmi) return;
    currentTrack_ = sequence;
    musicStarted_ = true;
    if (muted_) return;
    audio::playMusic(impl_->xmi, sequence, false);
    audio::setMusicVolume(kMusicVolume * std::clamp(settings().musicVolume, 0, 100) / 100);
}

void Sound::stop() {
    if (!muted_) audio::stopMusic();
}

bool Sound::done() const {
    if (!musicStarted_) return false;
    return muted_ || !audio::musicPlaying();
}

void Sound::startOnce(int sequence) {
    if (startedSinceLoad_ || paletteFade().level() != 0) return;
    startedSinceLoad_ = true;
    play(sequence);
}

void Sound::unload() {
    stop();
    if (impl_) impl_->xmi.reset();
    loadedNumber_ = -1;
    musicStarted_ = false;
}

// ---------------------------------------------------------------------------
// Sound effects
// ---------------------------------------------------------------------------

void Sound::loadSfxBank() {
    if (!impl_) return;
    // Static descriptor fields from st.exe (distance, flags, fm, digital, priority).
    for (int id = 1; id <= kSfxCount; ++id) {
        const u8* p = exe().at(kSfxSeg, u16(kSfxDescBase + id * 0x16));
        if (!p) fatal("sound effect table missing from st.exe");
        Impl::Desc& d = impl_->desc[id];
        d.distance = rd16(p + 0x00);
        d.flags = rd16(p + 0x02);
        d.fm = p[0x04] != 0;
        d.digital = p[0x05] != 0;
        d.priority = rd16(p + 0x12);
        d.sample.reset();
        d.fmReady = false;
    }
    impl_->descLoaded = true;
    // g_music_xmi = snd_load_music(0, g_snd_bank): msc01.xmi.
    loadMusic(0, kSndBank);
    for (int id = kSfxCount; id > 0; --id) {  // snd_load_sfx(53..1)
        Impl::Desc& d = impl_->desc[id];
        if (impl_->digiOk && d.digital) {
            char name[16];
            std::snprintf(name, sizeof name, "sfx%02d.voc", id);
            std::vector<u8> data;
            if (resources().read(name, data)) d.sample = audio::loadVoc(data);
        }
        // FM version: sequence id+10 of the bank (all priorities on FM drivers).
        if (d.fm && impl_->xmi) d.fmReady = true;
    }
}

void Sound::unloadSfxBank() {
    if (!impl_) return;
    stopAllSfx();
    for (int id = 1; id <= kSfxCount; ++id) {
        impl_->desc[id].sample.reset();
        impl_->desc[id].fmReady = false;
    }
    stop();
}

void Sound::resetChannels() {
    if (!impl_) return;
    for (auto& c : impl_->ch) c = Impl::Channel{};
    impl_->digiBusy = false;
    impl_->digiChannel = -1;
}

void Sound::setListener(const s32* pos) {
    if (!impl_) return;
    for (int i = 0; i < 3; ++i) impl_->listener[i] = pos ? pos[i] : 0;
}

// 4592:005B snd_channel_volume.
int Sound::channelVolume(int n) {
    Impl::Channel& c = impl_->ch[n];
    if (c.follow)
        for (int i = 0; i < 3; ++i) c.pos[i] = c.follow[i];
    const int dist = (c.flags & kChUnpositioned) ? -1 : groundDistance(c.pos, impl_->listener);
    const int maxDist = s16(impl_->desc[c.id].distance);
    if (maxDist < dist) return 0;
    if (dist == -1) return 0x7F;
    const int maxVol = (c.flags & kChDigital) ? kDigiMaxVol : kFmMaxVol;
    int v = maxVol - s8(s32(dist) * maxVol / maxDist);
    return v < 0 ? 0 : v;
}

// 4592:0563 snd_apply_channel_volume.
void Sound::applyVolume(int n, int vol) {
    Impl::Channel& c = impl_->ch[n];
    if (muted_) return;
    const Impl::Desc& d = impl_->desc[c.id];
    if (impl_->digiOk && d.digital && (c.flags & kChDigital)) {
        if (c.sample) audio::setSampleVolume(c.sample, vol);
        return;
    }
    if (d.fm && c.fmSeq) {
        // snd_fm_set_volume: percent = vol * 100 / 127, capped at 90.
        int pct = (s8(vol) * 100) / 0x7F;
        if (pct > 0x5A) pct = 0x5A;
        pct = pct * std::clamp(settings().sfxVolume, 0, 100) / 100;
        audio::setSequenceRelativeVolume(c.fmSeq, pct, 0);
    }
}

int Sound::playSfx(int id, s32 lifetime, const s32* pos, int fmParam, const s32* follow) {
    if (!impl_ || !impl_->descLoaded) return -1;
    if (timeCompression_ || !sfxEnabled_ || id < 1 || id > kSfxCount) return -1;
    int n = 0;
    while (n < kSfxChannels && impl_->ch[n].flags != 0) ++n;
    if (n == kSfxChannels) return -1;
    Impl::Channel& c = impl_->ch[n];
    const Impl::Desc& d = impl_->desc[id];
    c = Impl::Channel{};
    c.id = id;
    c.flags = u16(d.flags | kChActive);
    c.fmParam = fmParam & 0xFF;
    c.start = now_;
    c.lifetime = s16(lifetime);  // int16 argument, sign extended
    if (!pos) c.flags |= kChUnpositioned;
    else for (int i = 0; i < 3; ++i) c.pos[i] = pos[i];
    c.follow = follow;
    const int vol = channelVolume(n);
    if (!(c.flags & kChDesc) || vol != 0) {
        // Digital voice: free, or the new priority is higher, or equal and >= 0x18.
        const u16 pNew = d.priority;
        bool takeDigital = impl_->digiOk && d.digital;
        if (takeDigital && impl_->digiBusy) {
            const u16 pCur = impl_->desc[impl_->ch[impl_->digiChannel].id].priority;
            takeDigital = pCur <= pNew && (pCur != pNew || pNew > 0x17);
        }
        if (takeDigital) {
            if (impl_->digiBusy) stopChannel(impl_->digiChannel);
            impl_->digiBusy = true;
            impl_->digiChannel = n;
            c.flags |= kChDigital;
            if (d.sample && !muted_) c.sample = audio::playSample(d.sample, vol);
            return n;
        }
        if (impl_->fmOk && d.fm && d.fmReady) {
            // snd_fm_start: sequence id+10 with the controller table {fm_param, 0x7F}.
            if (!muted_) {
                c.fmSeq = audio::playSequence(impl_->xmi, id + 10, false, 127,
                                              {u8(c.fmParam), 0x7F});
                if (!c.fmSeq) {
                    // snd_fm_start frees the channel when the sequence cannot be
                    // registered, but snd_play_sfx still returns its number.
                    c.flags = 0;
                    return n;
                }
                applyVolume(n, vol);
            }
            return n;
        }
    }
    c.flags = 0;
    return -1;
}

void Sound::stopChannel(int n) {
    if (!impl_ || n < 0 || n >= kSfxChannels) return;
    Impl::Channel& c = impl_->ch[n];
    const Impl::Desc& d = impl_->desc[c.id];
    if (impl_->digiOk && d.digital && (c.flags & kChDigital)) {
        impl_->digiBusy = false;
        impl_->digiChannel = -1;
        if (c.sample) audio::stopSample(c.sample);
        c.sample = 0;
    } else if (impl_->fmOk && d.fm) {
        if (c.fmSeq) audio::stopSequence(c.fmSeq);
        c.fmSeq = 0;
    }
    c.flags = 0;
}

void Sound::stopAllSfx() {
    for (int n = 0; n < kSfxChannels; ++n) stopChannel(n);
}

void Sound::updateSfx() {
    if (!impl_) return;
    for (int n = 0; n < kSfxChannels; ++n) {
        Impl::Channel& c = impl_->ch[n];
        if (c.flags == 0) continue;
        bool done = false;
        const Impl::Desc& d = impl_->desc[c.id];
        if (!d.digital && c.fmSeq) {
            done = !audio::sequencePlaying(c.fmSeq);
            // Looping FM effects (descriptor flag bit 1) stay alive while playing.
            if (!done && (c.flags & kChDesc) && s32(u32(c.start) + u32(c.lifetime)) < now_) c.lifetime += 0x100;
        }
        if (!done && now_ <= s32(u32(c.start) + u32(c.lifetime))) {
            applyVolume(n, channelVolume(n));
            continue;
        }
        stopChannel(n);
    }
}

Sound::ChannelInfo Sound::channel(int n) const {
    ChannelInfo info;
    if (!impl_ || n < 0 || n >= kSfxChannels) return info;
    const Impl::Channel& c = impl_->ch[n];
    info.id = c.id;
    info.flags = c.flags;
    info.start = c.start;
    info.lifetime = c.lifetime;
    for (int i = 0; i < 3; ++i) info.pos[i] = c.pos[i];
    return info;
}

} // namespace st::engine
