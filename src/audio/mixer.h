// SDL-free audio engine: one AIL XMIDI driver instance (sequencer + OPL synth)
// shared by the music and any FM effect sequences, plus the digital voices,
// rendered at the OPL output rate. The driver is serviced the way AIL's timer
// multiplexing calls it (see docs/audio.md). Not thread safe: audio.cpp
// serialises access; offline tools use it directly.
#pragma once

#include "audio/ail_opl.h"
#include "audio/voc.h"
#include "audio/xmidi.h"

#include <memory>
#include <vector>

namespace st::audio {

enum class MusicDevice { None, Opl2, Opl3 };

// How the digital driver applies playback volume and pan.
enum class DigitalVolume {
    SoundBlaster,     // SB 1.x/2.0 (SBDIG.ADV): no mixer, volume and pan are ignored
    SoundBlasterPro,  // SB Pro (SBPDIG.ADV): 4-bit mixer level per side
    Linear,           // plain linear gain and pan (0 = left)
};

struct MixerConfig {
    int digitalVoices = 1;                      // the game used exactly one
    DigitalVolume digitalVolume = DigitalVolume::SoundBlasterPro;
    int timerHz = 256;                          // fastest other AIL timer (the game's tick), 0 = none
    float musicGain = 1.0f;
    float digitalGain = 1.0f;
    bool swapStereo = false;                    // OPL3: swap outputs A and B
};

class Mixer {
public:
    static constexpr double kRate = AilOplSynth::kChipRate;

    explicit Mixer(const MixerConfig& cfg = {});
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    // Changing the device stops all sequences and resets the driver.
    void setMusicDevice(MusicDevice dev);
    MusicDevice musicDevice() const { return device_; }
    // Global Timbre Library for the current device (SAMPLE.AD / SAMPLE.OPL).
    bool loadTimbreBank(std::vector<u8> data);
    // Installs the timbres of sequences 0..count-1 (the game does this when it
    // loads a music file).
    void preloadTimbres(const std::shared_ptr<const XmiFile>& file, int count);

    // XMIDI sequences on the shared driver. Volume 0-127 is applied as AIL
    // relative volume volume*100/127 %. Returns a handle > 0, or 0.
    int playSequence(std::shared_ptr<const XmiFile> file, int index, bool loop, int volume,
                     std::vector<u8> indirectControllers = {});
    void stopSequence(int h);
    bool sequencePlaying(int h) const;
    void setSequenceVolume(int h, int volume);
    void setSequenceRelativeVolume(int h, int percent, int ms);

    // The music is one such sequence.
    bool playMusic(std::shared_ptr<const XmiFile> file, int index, bool loop);
    void stopMusic();
    bool musicPlaying() const { return sequencePlaying(music_); }
    void setMusicVolume(int volume);

    // Digital voices. Returns a handle > 0, or 0.
    int playSample(std::shared_ptr<const PcmSound> pcm, int volume, int pan);
    void stopSample(int h);
    bool samplePlaying(int h) const;
    void setSampleVolume(int h, int volume, int pan);
    void setDigitalEnabled(bool on);
    bool digitalEnabled() const { return digitalOn_; }

    // Interleaved stereo frames at kRate.
    void render(s16* out, int frames);

    XmiSequencer* sequencer() { return seq_.get(); }
    AilOplSynth* synth() { return synth_.get(); }

private:
    struct SeqInfo {
        int handle = 0;       // API handle, 0 = unused
        bool loop = false;
        int percent = 100;
    };
    struct Voice {
        std::shared_ptr<const PcmSound> pcm;
        double pos = 0.0, step = 0.0;
        float gainL = 0.0f, gainR = 0.0f;
        int handle = 0;
        bool active = false;
    };

    int slotOf(int h) const;
    void installTimbres(const XmiFile::Sequence* info);
    void pitTick();
    void service();
    void renderChunk(s16* out, int frames);
    void computeGains(Voice& v, int volume, int pan) const;

    MixerConfig cfg_;
    MusicDevice device_ = MusicDevice::None;
    std::unique_ptr<AilOplSynth> synth_;
    std::unique_ptr<XmiSequencer> seq_;
    TimbreBank bank_;
    SeqInfo seqInfo_[XmiSequencer::kMaxSequences];
    int nextSeqSerial_ = 1;
    int music_ = 0;
    int musicVolume_ = 127;

    // AIL timer emulation: PIT ticks at the fastest timer's period; the
    // driver's 120 Hz service fires from an accumulator.
    u32 pitPeriodUs_ = 0;
    u32 serviceAcc_ = 0;
    double samplesPerPit_ = 0.0;
    double pitCountdown_ = 0.0;

    std::vector<Voice> voices_;
    int nextSampleHandle_ = 1;
    bool digitalOn_ = true;

    std::vector<s32> fmBuf_;
    std::vector<float> digBuf_;
};

} // namespace st::audio
