// Re-implementation of the synthesizer half of the AIL 2.x Yamaha FM drivers:
// ADLIB.ADV / SBFM.ADV (OPL2, 9 voices) and SBP2FM.ADV / PASOPL.ADV (OPL3,
// 18 voices, stereo). Timbre cache, note slots, voice allocation and stealing,
// and the MIDI -> OPL register mapping follow the driver code; the chip itself
// is emulated by ymfm. Behaviour notes in docs/audio.md.
#pragma once

#include "audio/xmidi.h"

#include <functional>
#include <memory>
#include <vector>

namespace st::audio {

enum class OplMode { Opl2, Opl3 };

// Global Timbre Library (SAMPLE.AD / SAMPLE.OPL): a directory of
// (patch, bank, u32 offset) entries ending with bank 0xFF; each timbre record
// starts with its u16 size.
class TimbreBank {
public:
    bool parse(std::vector<u8> data);
    bool empty() const { return entries_.empty(); }
    // Record (size word included) or nullptr.
    const u8* find(u8 bank, u8 patch, size_t& size) const;

private:
    struct Entry { u8 patch, bank; u32 offset; };
    std::vector<u8> data_;
    std::vector<Entry> entries_;
};

class AilOplSynth final : public MidiSink {
public:
    // Native output rate: 3.579545 MHz / 72 (OPL2) = 14.31818 MHz / 288 (OPL3).
    static constexpr double kChipRate = 3579545.0 / 72.0;
    static constexpr int kCacheEntries = 192;   // timbre cache directory size
    static constexpr int kCacheBytes = 0xe00;   // default timbre cache size

    explicit AilOplSynth(OplMode mode);
    ~AilOplSynth() override;
    AilOplSynth(const AilOplSynth&) = delete;
    AilOplSynth& operator=(const AilOplSynth&) = delete;

    OplMode mode() const { return mode_; }

    // Chip reset with the driver's register defaults and an empty synth state.
    void reset();
    void midiMessage(u8 status, u8 data1, u8 data2) override;

    // Timbre cache (AIL_timbre_status / AIL_install_timbre / protection).
    bool timbreInstalled(u8 bank, u8 patch) const { return findTimbre(bank, patch) >= 0; }
    void installTimbre(u8 bank, u8 patch, const u8* record, size_t size);

    // Swap the OPL3 output channels (A/B) when mixing.
    void setSwapStereo(bool on) { swapStereo_ = on; }
    // Observer of every chip register write (bank, register, value); for tests.
    void setWriteHook(std::function<void(int, int, int)> hook) { writeHook_ = std::move(hook); }

    // Renders interleaved stereo frames at kChipRate.
    void generate(s32* out, int frames);

    // Number of voices currently keyed on (diagnostics).
    int activeVoices() const;

    // The driver's F-number table (12 semitones x 16 steps from C); entries
    // with `upper` set belong to the next block. Exposed for verification.
    struct FreqEntry { u16 fnum; bool upper; };
    static const FreqEntry* freqTable();
    static u8 velocityGraph(int index);
    // Value the driver's hardware reset writes to a register (bank 0/1).
    static u8 resetRegister(int bank, int reg);

private:
    struct Timbre {
        bool used = false;
        bool protect = false;
        u8 bank = 0, patch = 0;
        u32 stamp = 0;            // LRU stamp
        std::vector<u8> data;     // record including its size word
    };
    struct Slot {
        u8 status = 0;            // 0 free, 1 key down
        u8 voice = 0xff;          // 0xff: waiting for a voice
        u8 channel = 0;
        u8 key = 0;               // key as received (note-off matching)
        u8 note = 0;              // key to play (percussion: from the timbre)
        s8 transpose = 0;
        u8 velocity = 0;          // velocity graph value
        bool sustained = false;
        u8 flags = 0;             // pending register updates
        u8 keyOn = 0;             // 0x20 while the note sounds
        u8 lastB0 = 0;
        int timbre = -1;
        u16 priority = 0x7fff;
        u8 avekm[2]{}, ksl[2]{}, level[2]{}, ad[2]{}, sr[2]{}, ws[2]{};  // [0] modulator, [1] carrier
        u8 fbc = 0;
        u8 scaleMask = 0;         // bit 0 modulator, bit 1 carrier follow the volume
    };
    struct Channel {
        u8 timbre = 0xff, bank = 0, patch = 0xff, voices = 0;
        u8 volume = 0, pan = 0, pitchL = 0, pitchH = 0, expression = 0, modulation = 0;
        u8 sustain = 0, protect = 0;
    };
    struct Chip;

    int numVoices() const { return mode_ == OplMode::Opl3 ? 18 : 9; }
    int numSlots() const { return mode_ == OplMode::Opl3 ? 20 : 16; }
    int findTimbre(u8 bank, u8 patch) const;
    void evictTimbre();

    void writeReg(int bank, u8 reg, u8 value);
    void writeOp(int voice, int op, u8 base, u8 value);
    void writeVoice(int voice, u8 base, u8 value);

    void noteOn(int ch, u8 key, u8 vel);
    void noteOff(int ch, u8 key);
    void sustainOff(int ch);
    void allNotesOff(int ch);
    void controller(int ch, u8 ctrl, u8 value);
    void updateChannel(int ch, u8 flags);
    void loadTimbre(Slot& s);
    void allocateVoice(int slot);
    void releaseVoice(int slot);
    void reassignVoices();
    void updateSlot(Slot& s);
    u16 pitchRegisters(const Slot& s) const;

    OplMode mode_;
    std::unique_ptr<Chip> chip_;
    bool swapStereo_ = false;
    std::function<void(int, int, int)> writeHook_;
    Timbre cache_[kCacheEntries];
    int cacheUsed_ = 0;
    u32 stamp_ = 0;
    Slot slots_[20];
    Channel chans_[16];
    u8 voiceOwner_[18];
    int nextVoice_ = -1;
    u8 percTimbre_[128];
};

} // namespace st::audio
