#include "audio/audio.h"

#include <SDL3/SDL.h>

#include <mutex>
#include <utility>

namespace st::audio {

namespace {

struct State {
    std::mutex lock;
    std::unique_ptr<Mixer> mixer;
    SDL_AudioStream* stream = nullptr;
    bool subsystem = false;
    std::vector<s16> buffer;  // audio thread only
};

State& state() {
    static State s;
    return s;
}

// SDL pulls data: render at the OPL rate, SDL resamples to the device.
void SDLCALL feed(void*, SDL_AudioStream* stream, int additional, int) {
    if (additional <= 0) return;
    State& st = state();
    const int frames = (additional + 3) / 4;
    st.buffer.resize(size_t(frames) * 2);
    {
        std::lock_guard<std::mutex> g(st.lock);
        if (!st.mixer) return;
        st.mixer->render(st.buffer.data(), frames);
    }
    SDL_PutAudioStreamData(stream, st.buffer.data(), frames * 4);
}

} // namespace

bool init(const Config& cfg) {
    shutdown();
    State& st = state();
    MixerConfig mc;
    mc.digitalVoices = cfg.digitalVoices;
    mc.digitalVolume = cfg.digitalVolume;
    mc.timerHz = cfg.timerHz;
    mc.musicGain = cfg.musicGain;
    mc.digitalGain = cfg.digitalGain;
    mc.swapStereo = cfg.swapStereo;
    {
        std::lock_guard<std::mutex> g(st.lock);
        st.mixer.reset(new Mixer(mc));
        st.mixer->setMusicDevice(cfg.musicDevice);
        st.mixer->setDigitalEnabled(cfg.digitalEnabled);
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        logWarn("audio: SDL audio init failed: %s", SDL_GetError());
        return false;
    }
    st.subsystem = true;
    char frames[16];
    SDL_snprintf(frames, sizeof frames, "%d", cfg.bufferFrames);
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames);
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = int(Mixer::kRate + 0.5);
    st.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, nullptr);
    if (!st.stream) {
        logWarn("audio: cannot open audio device: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(st.stream);
    logInfo("audio: %d Hz output, music %s", spec.freq,
            cfg.musicDevice == MusicDevice::Opl3 ? "OPL3" : cfg.musicDevice == MusicDevice::Opl2 ? "OPL2" : "off");
    return true;
}

void shutdown() {
    State& st = state();
    if (st.stream) {
        SDL_DestroyAudioStream(st.stream);  // also closes the device
        st.stream = nullptr;
    }
    if (st.subsystem) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        st.subsystem = false;
    }
    std::lock_guard<std::mutex> g(st.lock);
    st.mixer.reset();
}

namespace {
template <typename F>
auto withMixer(F&& f, decltype(f(std::declval<Mixer&>())) fallback) {
    State& st = state();
    std::lock_guard<std::mutex> g(st.lock);
    return st.mixer ? f(*st.mixer) : fallback;
}
} // namespace

void setMusicDevice(MusicDevice dev) {
    withMixer([&](Mixer& m) { m.setMusicDevice(dev); return 0; }, 0);
}

MusicDevice musicDevice() {
    return withMixer([](Mixer& m) { return m.musicDevice(); }, MusicDevice::None);
}

bool loadTimbreBank(const std::vector<u8>& data) {
    return withMixer([&](Mixer& m) { return m.loadTimbreBank(data); }, false);
}

void preloadTimbres(std::shared_ptr<const XmiFile> xmi, int count) {
    withMixer([&](Mixer& m) { m.preloadTimbres(xmi, count); return 0; }, 0);
}

std::shared_ptr<const XmiFile> loadXmi(const std::vector<u8>& data) {
    auto f = std::make_shared<XmiFile>();
    if (!f->parse(data)) return nullptr;
    return f;
}

std::shared_ptr<const PcmSound> loadVoc(const std::vector<u8>& data) {
    auto p = std::make_shared<PcmSound>();
    if (!decodeVoc(data.data(), data.size(), *p)) return nullptr;
    return p;
}

bool playMusic(const std::vector<u8>& xmiData, int sequence, bool loop) {
    return playMusic(loadXmi(xmiData), sequence, loop);
}

bool playMusic(std::shared_ptr<const XmiFile> xmi, int sequence, bool loop) {
    if (!xmi) return false;
    return withMixer([&](Mixer& m) { return m.playMusic(xmi, sequence, loop); }, false);
}

void stopMusic() {
    withMixer([](Mixer& m) { m.stopMusic(); return 0; }, 0);
}

bool musicPlaying() {
    return withMixer([](Mixer& m) { return m.musicPlaying(); }, false);
}

void setMusicVolume(int volume) {
    withMixer([&](Mixer& m) { m.setMusicVolume(volume); return 0; }, 0);
}

SequenceHandle playSequence(std::shared_ptr<const XmiFile> xmi, int sequence, bool loop, int volume,
                            const std::vector<u8>& indirectControllers) {
    if (!xmi) return 0;
    return withMixer([&](Mixer& m) { return m.playSequence(xmi, sequence, loop, volume, indirectControllers); }, 0);
}

void stopSequence(SequenceHandle h) {
    withMixer([&](Mixer& m) { m.stopSequence(h); return 0; }, 0);
}

bool sequencePlaying(SequenceHandle h) {
    return withMixer([&](Mixer& m) { return m.sequencePlaying(h); }, false);
}

void setSequenceVolume(SequenceHandle h, int volume) {
    withMixer([&](Mixer& m) { m.setSequenceVolume(h, volume); return 0; }, 0);
}

void setSequenceRelativeVolume(SequenceHandle h, int percent, int ms) {
    withMixer([&](Mixer& m) { m.setSequenceRelativeVolume(h, percent, ms); return 0; }, 0);
}

SampleHandle playSample(const std::vector<u8>& vocData, int volume, int pan) {
    return playSample(loadVoc(vocData), volume, pan);
}

SampleHandle playSample(std::shared_ptr<const PcmSound> pcm, int volume, int pan) {
    if (!pcm) return 0;
    return withMixer([&](Mixer& m) { return m.playSample(pcm, volume, pan); }, 0);
}

void stopSample(SampleHandle h) {
    withMixer([&](Mixer& m) { m.stopSample(h); return 0; }, 0);
}

bool samplePlaying(SampleHandle h) {
    return withMixer([&](Mixer& m) { return m.samplePlaying(h); }, false);
}

void setSampleVolume(SampleHandle h, int volume, int pan) {
    withMixer([&](Mixer& m) { m.setSampleVolume(h, volume, pan); return 0; }, 0);
}

void setDigitalEnabled(bool on) {
    withMixer([&](Mixer& m) { m.setDigitalEnabled(on); return 0; }, 0);
}

} // namespace st::audio
