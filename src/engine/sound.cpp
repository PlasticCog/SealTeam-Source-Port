#include "engine/sound.h"

#include "audio/audio.h"
#include "data/ealib.h"
#include "engine/palette_fade.h"

#include <cstdio>
#include <memory>

namespace st::engine {

namespace {
// g_music_volume: 96 % AIL relative volume.
constexpr int kMusicVolume = 122;
} // namespace

struct Sound::Impl {
    std::shared_ptr<const audio::XmiFile> xmi;
};

Sound& sound() {
    static Sound instance;
    return instance;
}

void Sound::init() {
    impl_ = new Impl;
    audio::Config cfg;
    cfg.musicDevice = audio::MusicDevice::Opl2;
    if (!audio::init(cfg)) {
        logWarn("audio: no output device, continuing silently");
    }
    std::vector<u8> gtl;
    if (resources().read("SAMPLE.AD", gtl) && audio::loadTimbreBank(gtl)) {
        ready_ = true;
    } else {
        logWarn("audio: SAMPLE.AD timbre bank not available, music disabled");
        musicEnabled_ = false;
    }
    audio::setMusicVolume(kMusicVolume);
}

void Sound::shutdown() {
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
    return true;
}

void Sound::play(int sequence) {
    if (!ready_ || !musicEnabled_ || !impl_ || !impl_->xmi) return;
    audio::playMusic(impl_->xmi, sequence, false);
    audio::setMusicVolume(kMusicVolume);
}

void Sound::stop() { audio::stopMusic(); }

bool Sound::done() const { return !audio::musicPlaying(); }

void Sound::startOnce(int sequence) {
    if (startedSinceLoad_ || paletteFade().level() != 0) return;
    startedSinceLoad_ = true;
    play(sequence);
}

void Sound::unload() {
    stop();
    if (impl_) impl_->xmi.reset();
    loadedNumber_ = -1;
}

} // namespace st::engine
