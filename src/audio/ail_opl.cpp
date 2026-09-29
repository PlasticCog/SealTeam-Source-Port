#include "audio/ail_opl.h"

#include "ymfm_opl.h"

#include <cmath>
#include <cstring>

namespace st::audio {

namespace {

constexpr u8 kPercussionBank = 127;
constexpr int kPercussionChannel = 9;  // MIDI channel 10

// Update flags: which register groups of a slot's voice must be rewritten.
enum : u8 {
    kUpdAvekm = 0x80,   // 0x20: AM/VIB/EG/KSR/MULT
    kUpdLevel = 0x40,   // 0x40: KSL/TL (volume)
    kUpdEnv = 0x20,     // 0x60/0x80: envelopes
    kUpdWave = 0x10,    // 0xE0: waveform
    kUpdFbc = 0x08,     // 0xC0: feedback/connection (+ OPL3 output bits)
    kUpdFreq = 0x01,    // 0xA0/0xB0: frequency and key on/off
    kUpdAll = 0xf9,
};

// Register values written by the driver's hardware reset (both OPL3 banks
// use the same operator defaults; bank 1 also sets the OPL3 NEW bit).
u8 resetValue(int bank, int reg) {
    if (bank == 1 && reg == 0x05) return 1;
    if (bank == 0 && reg == 0x01) return 0x20;  // waveform select enable
    if (bank == 0 && reg == 0x04) return 0x60;  // timers masked
    if (bank == 0 && reg == 0xbd) return 0xc0;  // deep AM and deep vibrato
    if (reg >= 0x20 && reg <= 0x35) return 0x01;
    if (reg >= 0x40 && reg <= 0x55) return 0x3f;
    if (reg >= 0x60 && reg <= 0x75) return 0xff;
    if (reg >= 0x80 && reg <= 0x95) return 0x0f;
    return 0;
}

// The driver's volume product: high byte of 2*a*b, plus one when non-zero.
u8 scaleVolume(u8 a, u8 b) {
    const unsigned r = ((unsigned(a) * b) << 1 & 0xffff) >> 8;
    return r ? u8(r + 1) : 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Tables

const AilOplSynth::FreqEntry* AilOplSynth::freqTable() {
    // F-numbers for C4 = 261.63 Hz upwards in 1/16 semitone steps at block 3
    // (49716 Hz chip rate); values that no longer fit 10 bits are stored
    // halved for the next block. This reproduces the driver table exactly.
    struct Table {
        FreqEntry e[192];
        Table() {
            for (int i = 0; i < 192; ++i) {
                const double v = 261.63 * std::pow(2.0, i / 192.0) * 131072.0 / 49716.0;
                if (v < 1023.5) e[i] = {u16(std::lround(v)), false};
                else e[i] = {u16(std::lround(v / 2.0)), true};
            }
        }
    };
    static const Table table;  // thread-safe initialisation
    return table.e;
}

u8 AilOplSynth::resetRegister(int bank, int reg) {
    return resetValue(bank, reg);
}

u8 AilOplSynth::velocityGraph(int index) {
    // Velocity >> 3 -> 82..127 in steps of 3.
    return u8(82 + 3 * (index & 15));
}

// ---------------------------------------------------------------------------
// Global Timbre Library

bool TimbreBank::parse(std::vector<u8> data) {
    data_ = std::move(data);
    entries_.clear();
    for (size_t p = 0; p + 6 <= data_.size(); p += 6) {
        const Entry e{data_[p], data_[p + 1], rd32(&data_[p + 2])};
        if (e.bank == 0xff) break;
        entries_.push_back(e);
    }
    return !entries_.empty();
}

const u8* TimbreBank::find(u8 bank, u8 patch, size_t& size) const {
    for (const auto& e : entries_) {
        if (e.bank != bank || e.patch != patch) continue;
        if (size_t(e.offset) + 2 > data_.size()) return nullptr;
        size = rd16(&data_[e.offset]);
        if (size == 0) size = 2;  // the game's loader treats 0 as 2
        if (size_t(e.offset) + size > data_.size()) return nullptr;
        return &data_[e.offset];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Chip access

struct AilOplSynth::Chip {
    ymfm::ymfm_interface intf;
    std::unique_ptr<ymfm::ym3812> opl2;
    std::unique_ptr<ymfm::ymf262> opl3;
};

AilOplSynth::AilOplSynth(OplMode mode) : mode_(mode), chip_(new Chip) {
    if (mode_ == OplMode::Opl3) chip_->opl3.reset(new ymfm::ymf262(chip_->intf));
    else chip_->opl2.reset(new ymfm::ym3812(chip_->intf));
    reset();
}

AilOplSynth::~AilOplSynth() = default;

void AilOplSynth::writeReg(int bank, u8 reg, u8 value) {
    if (writeHook_) writeHook_(bank, reg, value);
    if (chip_->opl3) {
        if (bank) chip_->opl3->write_address_hi(reg);
        else chip_->opl3->write_address(reg);
        chip_->opl3->write_data(value);
    } else if (bank == 0) {
        chip_->opl2->write_address(reg);
        chip_->opl2->write_data(value);
    }
}

// Operator register of a voice: op 0 modulator, 1 carrier.
void AilOplSynth::writeOp(int voice, int op, u8 base, u8 value) {
    const int w = voice % 9;
    writeReg(voice / 9, u8(base + (w / 3) * 8 + w % 3 + op * 3), value);
}

void AilOplSynth::writeVoice(int voice, u8 base, u8 value) {
    writeReg(voice / 9, u8(base + voice % 9), value);
}

void AilOplSynth::generate(s32* out, int frames) {
    if (chip_->opl3) {
        ymfm::ymf262::output_data o;
        for (int i = 0; i < frames; ++i) {
            chip_->opl3->generate(&o, 1);
            // Output A is the left channel, B the right one (C/D unused).
            // Halved: ymfm's OPL3 output is twice its OPL2 scale.
            out[2 * i] = (swapStereo_ ? o.data[1] : o.data[0]) / 2;
            out[2 * i + 1] = (swapStereo_ ? o.data[0] : o.data[1]) / 2;
        }
    } else {
        ymfm::ym3812::output_data o;
        for (int i = 0; i < frames; ++i) {
            chip_->opl2->generate(&o, 1);
            out[2 * i] = out[2 * i + 1] = o.data[0];
        }
    }
}

int AilOplSynth::activeVoices() const {
    int n = 0;
    for (int v = 0; v < numVoices(); ++v)
        if (voiceOwner_[v] != 0xff) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Reset and timbre cache

void AilOplSynth::reset() {
    if (chip_->opl3) {
        chip_->opl3->reset();
        writeReg(1, 0x05, 1);
        writeReg(1, 0x04, 0);
    } else {
        chip_->opl2->reset();
    }
    for (int reg = 1; reg <= 0xf5; ++reg) writeReg(0, u8(reg), resetValue(0, reg));
    if (chip_->opl3)
        for (int reg = 1; reg <= 0xf5; ++reg) writeReg(1, u8(reg), resetValue(1, reg));

    for (auto& t : cache_) t = Timbre{};
    cacheUsed_ = 0;
    stamp_ = 0;
    for (auto& s : slots_) s = Slot{};
    for (auto& c : chans_) c = Channel{};
    std::memset(voiceOwner_, 0xff, sizeof voiceOwner_);
    std::memset(percTimbre_, 0xff, sizeof percTimbre_);
    nextVoice_ = -1;
}

int AilOplSynth::findTimbre(u8 bank, u8 patch) const {
    for (int i = 0; i < kCacheEntries; ++i)
        if (cache_[i].used && cache_[i].bank == bank && cache_[i].patch == patch) return i;
    return -1;
}

// Drops the least recently used unprotected timbre; notes using it are cut.
void AilOplSynth::evictTimbre() {
    int victim = -1;
    u32 oldest = 0xffffffffu;
    for (int i = 0; i < kCacheEntries; ++i) {
        if (!cache_[i].used || cache_[i].protect) continue;
        if (cache_[i].stamp <= oldest) {
            oldest = cache_[i].stamp;
            victim = i;
        }
    }
    if (victim < 0) return;
    cacheUsed_ -= int(cache_[victim].data.size());
    cache_[victim] = Timbre{};
    for (auto& c : chans_)
        if (c.timbre == victim) c.timbre = 0xff;
    for (auto& p : percTimbre_)
        if (p == victim) p = 0xff;
    for (int i = 0; i < numSlots(); ++i) {
        if (slots_[i].status == 0 || slots_[i].timbre != victim) continue;
        releaseVoice(i);
        slots_[i].status = 0;
    }
}

void AilOplSynth::installTimbre(u8 bank, u8 patch, const u8* record, size_t size) {
    int idx = findTimbre(bank, patch);
    if (idx < 0) {
        if (!record || size < 2) return;
        const size_t need = rd16(record) ? rd16(record) : size;
        for (int tries = 0; tries <= kCacheEntries; ++tries) {
            idx = 0;
            while (idx < kCacheEntries && cache_[idx].used) ++idx;
            if (idx < kCacheEntries && cacheUsed_ + int(need) <= kCacheBytes) break;
            idx = -1;
            evictTimbre();
        }
        if (idx < 0) return;
        Timbre& t = cache_[idx];
        t.used = true;
        t.protect = false;
        t.bank = bank;
        t.patch = patch;
        t.stamp = stamp_++;
        t.data.assign(record, record + std::min(need, size));
        t.data.resize(need, 0);
        cacheUsed_ += int(need);
    }
    // Channels waiting for this bank/patch pick it up immediately.
    for (auto& c : chans_)
        if (c.patch == patch && c.bank == bank) c.timbre = u8(idx);
}

// ---------------------------------------------------------------------------
// MIDI input

void AilOplSynth::midiMessage(u8 status, u8 d1, u8 d2) {
    const int ch = status & 0x0f;
    switch (status & 0xf0) {
    case 0x90:
        if (ch < 1 || ch > 9) return;  // the driver only plays MIDI channels 2-10
        if (d2 == 0) noteOff(ch, d1);
        else noteOn(ch, d1, d2);
        break;
    case 0x80:
        noteOff(ch, d1);
        break;
    case 0xe0:
        chans_[ch].pitchL = d1;
        chans_[ch].pitchH = d2;
        updateChannel(ch, kUpdFreq);
        break;
    case 0xb0:
        controller(ch, d1, d2);
        break;
    case 0xc0: {
        Channel& c = chans_[ch];
        c.patch = d1;
        const int t = findTimbre(c.bank, d1);
        c.timbre = t < 0 ? 0xff : u8(t);
        break;
    }
    default:  // aftertouch, channel pressure: ignored
        break;
    }
}

void AilOplSynth::controller(int ch, u8 ctrl, u8 value) {
    Channel& c = chans_[ch];
    switch (ctrl) {
    case 114: c.bank = value; break;                 // XMIDI patch bank select
    case 112: c.protect = value; break;              // XMIDI voice protection
    case 113:                                        // XMIDI timbre protection
        if (c.timbre != 0xff) cache_[c.timbre].protect = s8(value) >= 64;
        break;
    case 1: c.modulation = value; updateChannel(ch, kUpdAvekm); break;
    case 7: c.volume = value; updateChannel(ch, kUpdLevel); break;
    case 11: c.expression = value; updateChannel(ch, kUpdLevel); break;
    case 10:  // OPL2: only stored (no audible effect); OPL3: output bits
        c.pan = value;
        updateChannel(ch, mode_ == OplMode::Opl3 ? kUpdFbc : kUpdLevel);
        break;
    case 64:
        c.sustain = value;
        if (s8(value) < 64) sustainOff(ch);
        break;
    case 121:  // reset all controllers
        c.sustain = 0;
        sustainOff(ch);
        c.modulation = 0;
        c.expression = 127;
        c.pitchL = 0;
        c.pitchH = 0x40;
        updateChannel(ch, kUpdAvekm | kUpdLevel | kUpdFreq);
        break;
    case 123:
        allNotesOff(ch);
        break;
    default:
        break;
    }
}

void AilOplSynth::updateChannel(int ch, u8 flags) {
    for (int i = 0; i < numSlots(); ++i) {
        Slot& s = slots_[i];
        if (s.status == 0 || s.channel != ch) continue;
        s.flags |= flags;
        updateSlot(s);
    }
}

void AilOplSynth::noteOn(int ch, u8 key, u8 vel) {
    int t = chans_[ch].timbre == 0xff ? -1 : chans_[ch].timbre;
    if (ch == kPercussionChannel) {
        // Percussion: the key selects a timbre in bank 127, cached per key.
        if (key > 127) return;
        if (percTimbre_[key] == 0xff) {
            const int f = findTimbre(kPercussionBank, key);
            percTimbre_[key] = f < 0 ? 0xff : u8(f);
        }
        t = percTimbre_[key] == 0xff ? -1 : percTimbre_[key];
    }
    if (t < 0) return;
    cache_[t].stamp = ++stamp_;

    int i = 0;
    while (i < numSlots() && slots_[i].status != 0) ++i;
    if (i == numSlots()) return;  // all note slots busy: note dropped
    Slot& s = slots_[i];
    const std::vector<u8>& rec = cache_[t].data;
    s.channel = u8(ch);
    s.key = key;
    if (ch == kPercussionChannel) {
        s.note = rec.size() > 2 ? rec[2] : 60;  // percussion timbres carry their own key
        s.transpose = 0;
    } else {
        s.note = key;
        s.transpose = rec.size() > 2 ? s8(rec[2]) : 0;
    }
    s.velocity = velocityGraph(vel >> 3);
    s.timbre = t;
    s.sustained = false;
    s.voice = 0xff;
    if (rec.size() < 14 || rd16(rec.data()) != 0x0e) return;  // 4-op (OPL3) timbres not supported
    s.status = 1;
    loadTimbre(s);
    allocateVoice(i);
}

void AilOplSynth::noteOff(int ch, u8 key) {
    for (int i = 0; i < numSlots(); ++i) {
        Slot& s = slots_[i];
        if (s.status != 1 || s.key != key || s.channel != ch) continue;
        if (s8(chans_[ch].sustain) >= 64) {
            s.sustained = true;
        } else {
            releaseVoice(i);
            s.status = 0;
        }
    }
}

void AilOplSynth::sustainOff(int ch) {
    for (int i = 0; i < numSlots(); ++i) {
        const Slot& s = slots_[i];
        if (s.status != 0 && s.channel == ch && s.sustained) noteOff(ch, s.note);
    }
}

void AilOplSynth::allNotesOff(int ch) {
    for (int i = 0; i < numSlots(); ++i) {
        const Slot& s = slots_[i];
        if (s.status == 1 && s.channel == ch) noteOff(ch, s.note);
    }
}

// Copies a 2-operator timbre record into the slot's register images.
void AilOplSynth::loadTimbre(Slot& s) {
    const u8* r = cache_[s.timbre].data.data();
    s.keyOn = 0x20;
    s.priority = 0x7fff;
    for (int op = 0; op < 2; ++op) {
        const u8* o = r + 3 + op * 6;  // AVEKM, KSLTL, AD, SR, WS (FB/C sits between)
        s.avekm[op] = o[0];
        s.ksl[op] = o[1] & 0xc0;
        s.level[op] = ~o[1] & 0x3f;
        s.ad[op] = o[2];
        s.sr[op] = o[3];
        s.ws[op] = o[4];
    }
    s.fbc = r[8];
    s.scaleMask = u8((r[8] & 1) | 2);
    s.flags = kUpdAll;
}

// Round-robin search for a free voice starting after the last one tried;
// if none is free, voices are redistributed by priority.
void AilOplSynth::allocateVoice(int slot) {
    Slot& s = slots_[slot];
    for (int tries = 0; tries < numVoices(); ++tries) {
        if (++nextVoice_ >= numVoices()) nextVoice_ = 0;
        if (voiceOwner_[nextVoice_] != 0xff) continue;
        s.voice = u8(nextVoice_);
        ++chans_[s.channel].voices;
        voiceOwner_[nextVoice_] = s.channel;
        s.flags = kUpdAll;
        updateSlot(s);
        return;
    }
    reassignVoices();
}

// Key-off and free the slot's voice; the (2-op) slot itself is freed too.
void AilOplSynth::releaseVoice(int slot) {
    Slot& s = slots_[slot];
    if (s.voice == 0xff) return;
    s.keyOn = 0;
    s.flags |= kUpdFreq;
    updateSlot(s);
    --chans_[s.channel].voices;
    voiceOwner_[s.voice] = 0xff;
    s.voice = 0xff;
    s.status = 0;
}

// Priority = 0x7fff (0xffff on voice-protected channels) minus the number of
// voices the channel already holds. While a waiting slot's priority is at
// least that of the weakest sounding slot, the weakest note is cut and its
// voice handed over.
void AilOplSynth::reassignVoices() {
    u16 prio[20] = {};
    int active = 0;
    for (int i = 0; i < numSlots(); ++i) {
        const Slot& s = slots_[i];
        if (s.status == 0) continue;
        ++active;
        const Channel& c = chans_[s.channel];
        const unsigned base = s8(c.protect) >= 64 ? 0xffffu : s.priority;
        prio[i] = base >= c.voices ? u16(base - c.voices) : 0;
    }
    while (active > 0) {
        u16 needMax = 0, haveMin = 0xffff;
        int need = -1, victim = -1;
        for (int i = 0; i < numSlots(); ++i) {
            const Slot& s = slots_[i];
            if (s.status == 0) continue;
            if (s.voice == 0xff) {
                if (prio[i] >= needMax) { needMax = prio[i]; need = i; }
            } else if (prio[i] <= haveMin) {
                haveMin = prio[i];
                victim = i;
            }
        }
        if (needMax < haveMin || needMax == 0 || need < 0 || victim < 0) return;
        const u8 v = slots_[victim].voice;
        releaseVoice(victim);
        Slot& n = slots_[need];
        n.voice = v;
        ++chans_[n.channel].voices;
        voiceOwner_[v] = n.channel;
        n.flags = kUpdAll;
        updateSlot(n);
        --active;
    }
}

// F-number/block for the slot's note, transpose and the channel's pitch bend
// (fixed +-12 semitone range), in 1/16 semitone resolution.
u16 AilOplSynth::pitchRegisters(const Slot& s) const {
    const Channel& c = chans_[s.channel];
    const int bend = ((c.pitchH << 7) | c.pitchL) - 0x2000;
    s16 pitch = s16((bend >> 5) * 12);                     // 1/256 semitones
    int n = int(s.note) + int(s.transpose) - 24;
    do n += 12; while (n < 0);                             // fold into 0..95
    n += 12;
    do n -= 12; while (n > 95);
    pitch = s16(pitch + n * 256);
    int p = s16(pitch + 8) >> 4;                           // 1/16 semitones
    p -= 0xc0;
    do p += 0xc0; while (p < 0);
    p += 0xc0;
    do p -= 0xc0; while (p > 0x5ff);
    const int semi = p >> 4;
    const FreqEntry& e = freqTable()[(semi % 12) * 16 + (p & 15)];
    int block = semi / 12 - 1;
    unsigned fnum = e.fnum;
    if (e.upper) {
        ++block;
    } else if (block < 0) {
        block = 0;
        fnum >>= 1;
    }
    return u16(((block << 2 | (fnum >> 8 & 3)) << 8) | (fnum & 0xff));
}

// Writes the register groups flagged in s.flags to the slot's voice.
void AilOplSynth::updateSlot(Slot& s) {
    if (s.voice == 0xff) return;
    const Channel& c = chans_[s.channel];
    const int v = s.voice;
    u8 vol = 0;
    if (s.flags & kUpdLevel) vol = scaleVolume(scaleVolume(c.volume, c.expression), s.velocity);

    if (s.flags & kUpdAvekm) {
        const u8 vib = s8(c.modulation) >= 64 ? 0x40 : 0;  // mod wheel switches vibrato on
        writeOp(v, 0, 0x20, s.avekm[0] | vib);
        writeOp(v, 1, 0x20, s.avekm[1] | vib);
    }
    if (s.flags & kUpdLevel) {
        for (int op = 0; op < 2; ++op) {
            unsigned lvl = s.level[op];  // 63 - TL
            if (s.scaleMask & (1 << op)) lvl = lvl * vol / 127;
            writeOp(v, op, 0x40, u8((~lvl & 0x3f) | s.ksl[op]));
        }
    }
    if (s.flags & kUpdEnv) {
        writeOp(v, 0, 0x60, s.ad[0]);
        writeOp(v, 1, 0x60, s.ad[1]);
        writeOp(v, 0, 0x80, s.sr[0]);
        writeOp(v, 1, 0x80, s.sr[1]);
    }
    if (s.flags & kUpdWave) {
        writeOp(v, 1, 0xe0, s.ws[1]);
        writeOp(v, 0, 0xe0, s.ws[0]);
    }
    if (s.flags & kUpdFbc) {
        u8 fbc = u8((s.fbc & 0x0e) | (s.fbc & 1));
        if (mode_ == OplMode::Opl3) {
            // Hard panning only: pan <= 27 drops output A, pan >= 100 drops B.
            fbc |= 0x30;
            if (c.pan <= 27) fbc &= 0xef;
            else if (c.pan >= 100) fbc &= 0xdf;
        }
        writeVoice(v, 0xc0, fbc);
    }
    if (s.flags & kUpdFreq) {
        if (!s.keyOn) {
            writeVoice(v, 0xb0, s.lastB0 & 0xdf);
        } else {
            const u16 regs = pitchRegisters(s);
            writeVoice(v, 0xa0, u8(regs & 0xff));
            s.lastB0 = u8((regs >> 8) | s.keyOn);
            writeVoice(v, 0xb0, s.lastB0);
        }
    }
    s.flags = 0;
}

} // namespace st::audio
