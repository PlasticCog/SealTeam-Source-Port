#include "audio/mixer.h"

#include <algorithm>
#include <cmath>

namespace st::audio {

namespace {

constexpr u32 kServicePeriodUs = 1000000 / 120;  // the drivers' service rate

// AIL programs the PIT for the shortest timer period: divisor = us * 10000 / 8380.
double pitHzForPeriod(u32 us) {
    const u32 divisor = std::max<u32>(1, us * 10000u / 8380u);
    return 1193182.0 / double(divisor);
}

int percentOf(int volume) {
    return std::clamp(volume, 0, 127) * 100 / 127;
}

// SB Pro mixer voice level (4-bit field) to gain: 0 mutes, roughly 2 dB per step.
float sbProGain(int nibble) {
    if (nibble <= 0) return 0.0f;
    return float(std::pow(10.0, -2.0 * (15 - std::min(nibble, 15)) / 20.0));
}

} // namespace

Mixer::Mixer(const MixerConfig& cfg) : cfg_(cfg) {
    cfg_.digitalVoices = std::max(1, cfg_.digitalVoices);
    voices_.resize(size_t(cfg_.digitalVoices));
    pitPeriodUs_ = kServicePeriodUs;
    if (cfg_.timerHz > 0) pitPeriodUs_ = std::min<u32>(pitPeriodUs_, 1000000u / u32(cfg_.timerHz));
    samplesPerPit_ = kRate / pitHzForPeriod(pitPeriodUs_);
}

Mixer::~Mixer() = default;

// ---------------------------------------------------------------------------
// Music driver

void Mixer::setMusicDevice(MusicDevice dev) {
    for (auto& s : seqInfo_) s = SeqInfo{};
    music_ = 0;
    if (seq_) seq_->shutdown();
    seq_.reset();
    synth_.reset();
    device_ = dev;
    if (dev == MusicDevice::None) return;
    synth_.reset(new AilOplSynth(dev == MusicDevice::Opl3 ? OplMode::Opl3 : OplMode::Opl2));
    synth_->setSwapStereo(cfg_.swapStereo);
    seq_.reset(new XmiSequencer(*synth_));
    seq_->init();
    serviceAcc_ = 0;
    pitCountdown_ = 0.0;
}

bool Mixer::loadTimbreBank(std::vector<u8> data) {
    return bank_.parse(std::move(data));
}

int Mixer::slotOf(int h) const {
    if (h <= 0) return -1;
    const int slot = h % XmiSequencer::kMaxSequences;
    return seqInfo_[slot].handle == h ? slot : -1;
}

// The application side of AIL: answer the driver's timbre requests from the
// Global Timbre Library until every timbre the sequence lists is installed.
void Mixer::installTimbres(const XmiFile::Sequence* info) {
    if (!synth_ || !info) return;
    for (int guard = 0; guard < 512; ++guard) {
        const XmiFile::Timbre* missing = nullptr;
        for (const auto& t : info->timbres) {
            size_t size = 0;
            if (synth_->timbreInstalled(t.bank, t.patch) || !bank_.find(t.bank, t.patch, size)) continue;
            missing = &t;  // (a timbre absent from the library would hang the DOS game)
            break;
        }
        if (!missing) break;
        size_t size = 0;
        const u8* rec = bank_.find(missing->bank, missing->patch, size);
        synth_->installTimbre(missing->bank, missing->patch, rec, size);
    }
    int absent = 0;
    for (const auto& t : info->timbres) {
        size_t size = 0;
        if (!bank_.find(t.bank, t.patch, size) && absent++ == 0)
            logWarn("audio: timbre bank %d patch %d not in the timbre library", t.bank, t.patch);
    }
    if (absent > 1) logWarn("audio: %d timbres of the sequence are missing in total", absent);
}

void Mixer::preloadTimbres(const std::shared_ptr<const XmiFile>& file, int count) {
    if (!file) return;
    for (int i = 0; i < count && i < file->count(); ++i) installTimbres(file->sequence(i));
}

int Mixer::playSequence(std::shared_ptr<const XmiFile> file, int index, bool loop, int volume,
                        std::vector<u8> indirectControllers) {
    if (!seq_ || !file) return 0;
    const int slot = seq_->registerSequence(std::move(file), index, std::move(indirectControllers));
    if (slot < 0) return 0;
    installTimbres(seq_->sequenceInfo(slot));
    seq_->start(slot);
    SeqInfo& si = seqInfo_[slot];
    si.handle = nextSeqSerial_++ * XmiSequencer::kMaxSequences + slot;
    si.loop = loop;
    si.percent = percentOf(volume);
    seq_->setRelativeVolume(slot, si.percent, 0);
    return si.handle;
}

void Mixer::stopSequence(int h) {
    const int slot = slotOf(h);
    if (slot < 0) return;
    seq_->stop(slot);
    seq_->releaseSequence(slot);
    seqInfo_[slot] = SeqInfo{};
    if (h == music_) music_ = 0;
}

bool Mixer::sequencePlaying(int h) const {
    const int slot = slotOf(h);
    return slot >= 0 && seq_->status(slot) == XmiSequencer::Playing;
}

void Mixer::setSequenceVolume(int h, int volume) {
    setSequenceRelativeVolume(h, percentOf(volume), 0);
}

void Mixer::setSequenceRelativeVolume(int h, int percent, int ms) {
    const int slot = slotOf(h);
    if (slot < 0) return;
    seqInfo_[slot].percent = percent;
    seq_->setRelativeVolume(slot, percent, ms);
}

bool Mixer::playMusic(std::shared_ptr<const XmiFile> file, int index, bool loop) {
    stopMusic();
    music_ = playSequence(std::move(file), index, loop, musicVolume_);
    return music_ != 0;
}

void Mixer::stopMusic() {
    if (music_) stopSequence(music_);
    music_ = 0;
}

void Mixer::setMusicVolume(int volume) {
    musicVolume_ = std::clamp(volume, 0, 127);
    if (music_) setSequenceVolume(music_, musicVolume_);
}

// One PIT interrupt: every AIL timer's accumulator grows by the programmed
// period; the driver's service runs when its accumulator reaches 1/120 s.
void Mixer::pitTick() {
    serviceAcc_ += pitPeriodUs_;
    if (serviceAcc_ >= kServicePeriodUs) {
        serviceAcc_ -= kServicePeriodUs;
        service();
    }
}

void Mixer::service() {
    seq_->service();
    for (int slot = 0; slot < XmiSequencer::kMaxSequences; ++slot) {
        SeqInfo& si = seqInfo_[slot];
        if (!si.handle || seq_->status(slot) != XmiSequencer::Done) continue;
        if (si.loop) {  // restart as a polling application would
            seq_->start(slot);
            seq_->setRelativeVolume(slot, si.percent, 0);
        } else {
            seq_->releaseSequence(slot);
            if (si.handle == music_) music_ = 0;
            si = SeqInfo{};
        }
    }
}

// ---------------------------------------------------------------------------
// Digital voices

void Mixer::computeGains(Voice& v, int volume, int pan) const {
    volume = std::clamp(volume, 0, 127);
    pan = std::clamp(pan, 0, 127);
    switch (cfg_.digitalVolume) {
    case DigitalVolume::SoundBlaster:
        v.gainL = v.gainR = 1.0f;
        break;
    case DigitalVolume::SoundBlasterPro: {
        // SBPDIG: level table min(2*i, 127); the left nibble follows `pan`,
        // the right one 127 - pan (AIL's pan runs right to left).
        auto t = [](int i) { return std::min(2 * i, 127); };
        v.gainL = sbProGain((t(pan) * volume) >> 10);
        v.gainR = sbProGain((t(127 - pan) * volume) >> 10);
        break;
    }
    case DigitalVolume::Linear:
        v.gainL = float(volume) / 127.0f * std::min(1.0f, float(127 - pan) / 63.5f);
        v.gainR = float(volume) / 127.0f * std::min(1.0f, float(pan) / 63.5f);
        break;
    }
}

int Mixer::playSample(std::shared_ptr<const PcmSound> pcm, int volume, int pan) {
    if (!digitalOn_ || !pcm || pcm->frames() == 0 || pcm->rate <= 0.0) return 0;
    Voice* v = nullptr;
    for (auto& c : voices_)
        if (!c.active) { v = &c; break; }
    if (!v) {  // all busy: the oldest sample gives way (one voice: replaced)
        v = &voices_[0];
        for (auto& c : voices_)
            if (c.handle < v->handle) v = &c;
    }
    v->pcm = std::move(pcm);
    v->pos = 0.0;
    v->step = v->pcm->rate / kRate;
    v->handle = nextSampleHandle_++;
    v->active = true;
    computeGains(*v, volume, pan);
    return v->handle;
}

void Mixer::stopSample(int h) {
    for (auto& v : voices_)
        if (v.active && v.handle == h) v = Voice{};
}

bool Mixer::samplePlaying(int h) const {
    for (const auto& v : voices_)
        if (v.active && v.handle == h) return true;
    return false;
}

void Mixer::setSampleVolume(int h, int volume, int pan) {
    for (auto& v : voices_)
        if (v.active && v.handle == h) computeGains(v, volume, pan);
}

void Mixer::setDigitalEnabled(bool on) {
    digitalOn_ = on;
    if (!on)
        for (auto& v : voices_) v = Voice{};
}

// ---------------------------------------------------------------------------
// Rendering

void Mixer::render(s16* out, int frames) {
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        if (seq_) {
            if (pitCountdown_ <= 0.0) {
                pitTick();
                pitCountdown_ += samplesPerPit_;
                continue;
            }
            n = std::min(n, std::max(1, int(std::ceil(pitCountdown_))));
        }
        renderChunk(out + 2 * done, n);
        pitCountdown_ -= n;
        done += n;
    }
}

void Mixer::renderChunk(s16* out, int frames) {
    const size_t n2 = size_t(frames) * 2;
    if (fmBuf_.size() < n2) fmBuf_.resize(n2);
    if (digBuf_.size() < n2) digBuf_.resize(n2);
    if (synth_) synth_->generate(fmBuf_.data(), frames);
    else std::fill(fmBuf_.begin(), fmBuf_.begin() + std::ptrdiff_t(n2), 0);
    std::fill(digBuf_.begin(), digBuf_.begin() + std::ptrdiff_t(n2), 0.0f);

    for (auto& v : voices_) {
        if (!v.active) continue;
        const PcmSound& p = *v.pcm;
        const size_t total = p.loops() ? p.loopEnd : p.frames();
        const int ch = p.channels;
        for (int f = 0; f < frames; ++f) {
            const size_t i = size_t(v.pos);
            const float frac = float(v.pos - double(i));
            size_t j = i + 1;
            if (j >= total) j = p.loops() ? p.loopStart : i;
            float l = p.samples[i * size_t(ch)];
            float r = ch > 1 ? p.samples[i * size_t(ch) + 1] : l;
            const float l1 = p.samples[j * size_t(ch)];
            const float r1 = ch > 1 ? p.samples[j * size_t(ch) + 1] : l1;
            l += (l1 - l) * frac;
            r += (r1 - r) * frac;
            digBuf_[2 * size_t(f)] += l * v.gainL;
            digBuf_[2 * size_t(f) + 1] += r * v.gainR;
            v.pos += v.step;
            if (v.pos >= double(total)) {
                if (p.loops()) {
                    const double len = double(p.loopEnd - p.loopStart);
                    v.pos = double(p.loopStart) + std::fmod(v.pos - double(p.loopEnd), len);
                } else {
                    v = Voice{};
                    break;
                }
            }
        }
    }

    for (size_t k = 0; k < n2; ++k) {
        const float s = float(fmBuf_[k]) * cfg_.musicGain + digBuf_[k] * cfg_.digitalGain;
        out[k] = s16(std::clamp(std::lround(s), -32768L, 32767L));
    }
}

} // namespace st::audio
