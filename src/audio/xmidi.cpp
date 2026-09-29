#include "audio/xmidi.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace st::audio {

namespace {

// Controllers whose values the driver keeps per channel (restored when a
// locked channel is released or a sequence resumes), with init_driver defaults.
constexpr int kSavedCtrlNum[9] = {7, 1, 10, 11, 64, 114, 110, 111, 112};
constexpr u8 kSavedCtrlDefault[9] = {127, 0, 64, 127, 0, 0, 0, 0, 0};
// Programs init_driver selects on channels 1-8 (0-based): the Roland MT-32
// power-on part assignments.
constexpr u8 kDefaultProgram[16] = {0xff, 68, 48, 95, 78, 41, 3, 110, 122, 0xff,
                                    0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// Beat counter: the accumulator runs in 1/16 microseconds. One service tick
// (1/120 s) adds 16e6/120; a quarter note at the default 500000 us tempo is 8e6.
constexpr u32 kBeatStep = 16000000 / 120;
constexpr u32 kDefaultTempo16 = 500000u * 16;

enum : int {
    kCtrlLock = 110, kCtrlLockProtect = 111, kCtrlIndirect = 115, kCtrlFor = 116,
    kCtrlNext = 117, kCtrlClearBeat = 118, kCtrlCallback = 119,
};

u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]); }
bool idIs(const u8* p, const char* id) { return std::memcmp(p, id, 4) == 0; }

// Signed 32-bit >= as the driver does it for the beat accumulator: signed
// compare of the high words, then (quirk) a signed compare of the low words.
bool driverGe(u32 a, u32 b) {
    const s16 ah = s16(a >> 16), bh = s16(b >> 16);
    if (ah != bh) return ah > bh;
    return s16(a & 0xffff) >= s16(b & 0xffff);
}

} // namespace

// ---------------------------------------------------------------------------
// XMIDI file

bool XmiFile::parse(std::vector<u8> data) {
    data_ = std::move(data);
    seqs_.clear();
    const size_t n = data_.size();
    // Chunks are walked without IFF pad bytes, like the driver; an odd size
    // followed by a zero pad is tolerated.
    auto next = [&](size_t p, u32 size) {
        size_t q = p + 8 + size;
        if ((size & 1) && q < n && !std::isalpha(data_[q])) ++q;
        return q;
    };
    size_t pos = 0;
    while (pos + 12 <= n) {
        const u8* c = &data_[pos];
        const bool form = idIs(c, "FORM"), cat = idIs(c, "CAT ");
        if (!form && !cat) break;  // the driver gives up on anything else
        const u32 size = be32(c + 4);
        const size_t end = std::min(n, pos + 8 + size_t(size));
        if (idIs(c + 8, "XMID")) {
            if (form) {
                parseForm(pos, end);
            } else {
                size_t p = pos + 12;
                while (p + 12 <= end) {
                    const u32 csize = be32(&data_[p + 4]);
                    if (idIs(&data_[p + 8], "XMID")) parseForm(p, std::min(end, p + 8 + size_t(csize)));
                    p = next(p, csize);
                }
            }
            break;
        }
        pos = next(pos, size);
    }
    return !seqs_.empty();
}

bool XmiFile::parseForm(size_t pos, size_t end) {
    Sequence s;
    size_t p = pos + 12;
    while (p + 8 <= end) {
        const u8* c = &data_[p];
        const u32 size = be32(c + 4);
        const size_t body = p + 8;
        const size_t bodyEnd = std::min(end, body + size_t(size));
        if (idIs(c, "TIMB") && body + 2 <= bodyEnd) {
            const unsigned cnt = rd16(&data_[body]);
            for (unsigned i = 0; i < cnt && body + 4 + 2 * i <= bodyEnd; ++i)
                s.timbres.push_back({data_[body + 2 + 2 * i], data_[body + 3 + 2 * i]});
        } else if (idIs(c, "RBRN") && body + 2 <= bodyEnd) {
            const unsigned cnt = rd16(&data_[body]);
            for (unsigned i = 0; i < cnt && body + 8 + 6 * i <= bodyEnd; ++i)
                s.branches.push_back({data_[body + 2 + 6 * i], rd32(&data_[body + 4 + 6 * i])});
        } else if (idIs(c, "EVNT")) {
            s.events = body;
            s.eventsSize = bodyEnd - body;
            break;
        }
        p = p + 8 + size;
    }
    // Keep sequences without events so indices still match the driver's.
    seqs_.push_back(std::move(s));
    return seqs_.back().eventsSize > 0;
}

const XmiFile::Sequence* XmiFile::sequence(int index) const {
    if (index < 0 || index >= int(seqs_.size())) return nullptr;
    return &seqs_[size_t(index)];
}

// ---------------------------------------------------------------------------
// Sequencer

int XmiSequencer::savedSlot(int ctrl) {
    for (int i = 0; i < kSavedCtrls; ++i)
        if (kSavedCtrlNum[i] == ctrl) return i;
    return -1;
}

XmiSequencer::Seq* XmiSequencer::seq(int h) {
    if (h < 0 || h >= kMaxSequences || !seqs_[h].used) return nullptr;
    return &seqs_[h];
}

const XmiSequencer::Seq* XmiSequencer::seq(int h) const {
    if (h < 0 || h >= kMaxSequences || !seqs_[h].used) return nullptr;
    return &seqs_[h];
}

void XmiSequencer::init() {
    for (auto& s : seqs_) s = Seq{};
    registered_ = 0;
    busy_ = false;
    std::memset(ctrl_, 0xff, sizeof ctrl_);
    std::memset(program_, 0xff, sizeof program_);
    std::memset(pitchL_, 0xff, sizeof pitchL_);
    std::memset(pitchH_, 0xff, sizeof pitchH_);
    std::memset(notes_, 0, sizeof notes_);
    std::memset(lock_, 0, sizeof lock_);
    // Defaults go to channels 2-10 only (0-based 1..9), controller by controller.
    for (int i = 0; i < kSavedCtrls; ++i) {
        for (int ch = 1; ch <= 9; ++ch) {
            ctrl_[i][ch] = kSavedCtrlDefault[i];
            send(u8(0xb0 | ch), u8(kSavedCtrlNum[i]), kSavedCtrlDefault[i]);
        }
    }
    for (int ch = 1; ch <= 9; ++ch) {
        pitchL_[ch] = 0;
        pitchH_[ch] = 0x40;
        send(u8(0xe0 | ch), 0, 0x40);
        if (kDefaultProgram[ch] != 0xff) {
            program_[ch] = kDefaultProgram[ch];
            send(u8(0xc0 | ch), kDefaultProgram[ch], 0);
        }
    }
}

void XmiSequencer::shutdown() {
    for (int h = 0; h < kMaxSequences; ++h) {
        if (!seqs_[h].used) continue;
        stop(h);
        releaseSequence(h);
    }
}

void XmiSequencer::resetState(Seq& s) {
    for (auto& c : s.forCount) c = 0xffff;
    for (auto& p : s.forPos) p = 0;
    for (int i = 0; i < 16; ++i) s.chanMap[i] = u8(i);
    std::memset(s.program, 0xff, sizeof s.program);
    std::memset(s.pitchL, 0xff, sizeof s.pitchL);
    std::memset(s.pitchH, 0xff, sizeof s.pitchH);
    std::memset(s.indirect, 0xff, sizeof s.indirect);
    std::memset(s.saved, 0xff, sizeof s.saved);
    std::memset(s.qChan, 0xff, sizeof s.qChan);
    std::memset(s.qKey, 0, sizeof s.qKey);
    std::memset(s.qDur, 0, sizeof s.qDur);
    s.callbackValue = -1;
    s.delay = 0;
    s.noteCount = 0;
    s.volume = s.volumeTarget = 100;
    s.tempo = s.tempoTarget = 100;
    s.tickAcc = 0;
    s.beat = s.measure = 0;
    s.beatsPerBar = 4;
    s.beatStep = kBeatStep;
    s.beatAcc = kBeatStep;
    s.tempo16 = kDefaultTempo16;
}

int XmiSequencer::registerSequence(std::shared_ptr<const XmiFile> file, int index, std::vector<u8> controllerTable) {
    if (!file) return -1;
    int h = 0;
    while (h < kMaxSequences && seqs_[h].used) ++h;
    if (h == kMaxSequences) return -1;
    const XmiFile::Sequence* info = file->sequence(index);
    if (!info || info->eventsSize == 0) return -1;
    Seq& s = seqs_[h];
    s = Seq{};
    s.used = true;
    s.file = std::move(file);
    s.info = info;
    s.ctrlTable = std::move(controllerTable);
    ++registered_;
    resetState(s);
    return h;
}

void XmiSequencer::releaseSequence(int h) {
    Seq* s = seq(h);
    if (!s) return;
    if (s->status == Playing) {
        s->releasePending = true;
        return;
    }
    *s = Seq{};
    --registered_;
}

const XmiFile::Sequence* XmiSequencer::sequenceInfo(int h) const {
    const Seq* s = seq(h);
    return s ? s->info : nullptr;
}

void XmiSequencer::start(int h) {
    Seq* s = seq(h);
    if (!s) return;
    if (s->status == Playing) stop(h);
    resetState(*s);
    s->pos = 0;
    s->status = Playing;
    s->started = true;
}

void XmiSequencer::stop(int h) {
    Seq* s = seq(h);
    if (!s || s->status != Playing) return;
    flushNotes(*s);
    cleanup(*s);
    s->status = Stopped;
}

void XmiSequencer::resume(int h) {
    Seq* s = seq(h);
    if (!s || s->status != Stopped || !s->started) return;
    restoreState(*s);
    s->status = Playing;
}

int XmiSequencer::status(int h) const {
    const Seq* s = seq(h);
    return s ? s->status : Stopped;
}

// Sends note-offs for everything in the sequence's note queue.
void XmiSequencer::flushNotes(Seq& s) {
    for (int i = 0; i < kQueueSize; ++i) {
        if (s.qChan[i] == 0xff) continue;
        const u8 ch = s.qChan[i];
        s.qChan[i] = 0xff;
        const u8 p = s.chanMap[ch];
        --notes_[p];
        send(u8(0x80 | p), s.qKey[i], 0);
    }
    s.noteCount = 0;
}

// Note-offs for queued notes on sequence channel `ch`, in every sequence.
void XmiSequencer::flushChannelNotes(int ch) {
    for (auto& s : seqs_) {
        if (!s.used || s.noteCount == 0) continue;
        for (int i = 0; i < kQueueSize; ++i) {
            if (s.qChan[i] != ch) continue;
            s.qChan[i] = 0xff;
            const u8 p = s.chanMap[ch];
            --notes_[p];
            send(u8(0x80 | p), s.qKey[i], 0);
            --s.noteCount;
        }
    }
}

// End of sequence / stop: undo sustain, channel locks, lock and voice protection.
void XmiSequencer::cleanup(Seq& s) {
    for (int ch = 0; ch < 16; ++ch) {
        if (s8(s.saved[4][ch]) >= 64) {
            ctrl_[4][ch] = 0;
            send(u8(0xb0 | ch), 64, 0);
        }
        if (s8(s.saved[6][ch]) >= 64) {
            flushChannelNotes(ch);
            releaseChannel(s.chanMap[ch] + 1);
            s.chanMap[ch] = u8(ch);
        }
        if (s8(s.saved[7][ch]) >= 64) lock_[ch] &= u8(~0x40);
        if (s8(s.saved[8][ch]) >= 64) send(u8(0xb0 | ch), 112, 0);
    }
}

// Resume: re-lock channels and re-send the saved controller/program/pitch state.
void XmiSequencer::restoreState(Seq& s) {
    const int h = int(&s - seqs_);
    for (int ch = 0; ch < 16; ++ch) {
        const u8 v = s.saved[6][ch];
        if (v != 0xff && s8(v) >= 64) {
            const int r = lockChannel();
            s.chanMap[ch] = u8(r ? r - 1 : ch);
        }
    }
    for (int i = 0; i < kSavedCtrls; ++i) {
        if (kSavedCtrlNum[i] == kCtrlLock) continue;
        for (int ch = 0; ch < 16; ++ch)
            if (s.saved[i][ch] != 0xff) controller(s, h, ch, kSavedCtrlNum[i], s.saved[i][ch]);
    }
    for (int ch = 0; ch < 16; ++ch) {
        if (s.pitchL[ch] != 0xff && s.pitchH[ch] != 0xff)
            send(u8(0xe0 | s.chanMap[ch]), s.pitchL[ch], s.pitchH[ch]);
        if (s.program[ch] != 0xff) send(u8(0xc0 | s.chanMap[ch]), s.program[ch], 0);
    }
}

// Re-sends controller 7 of every channel scaled by the relative volume.
void XmiSequencer::applyVolume(Seq& s) {
    for (int ch = 0; ch < 16; ++ch) {
        const u8 v = s.saved[0][ch];
        if (v == 0xff) continue;
        unsigned x = unsigned(v) * unsigned(s.volume) / 100;
        if (x >= 127) x = 127;
        ctrl_[0][ch] = u8(x);
        if (!(lock_[ch] & 0x80)) send(u8(0xb0 | s.chanMap[ch]), 7, u8(x));
    }
}

int XmiSequencer::controller(Seq& s, int h, int ch, int ctrl, int value) {
    if (s.indirect[ch] != 0xff) {
        const u8 idx = s.indirect[ch];
        s.indirect[ch] = 0xff;
        value = idx < s.ctrlTable.size() ? s.ctrlTable[idx] : 0;
    }
    const int slot = savedSlot(ctrl);
    if (slot >= 0) {
        ctrl_[slot][ch] = u8(value);
        s.saved[slot][ch] = u8(value);
    }
    switch (ctrl) {
    case 7:
        if (s.volume != 100) {
            unsigned x = unsigned(value & 0xff) * unsigned(s.volume) / 100;
            if (x >= 127) x = 127;
            value = int(x);
            ctrl_[0][ch] = u8(x);
        }
        break;
    case kCtrlClearBeat:
        s.beat = s.measure = 0;
        s.beatAcc = s.beatStep;
        return 3;
    case kCtrlCallback:
        s.callbackValue = value;
        if (callback_) callback_(h, value);
        return 3;
    case kCtrlFor:
        for (int i = 0; i < kForNest; ++i) {
            if (s.forCount[i] != 0xffff) continue;
            s.forCount[i] = u16(value);
            s.forPos[i] = s.pos;  // this event; the caller then steps past it
            break;
        }
        return 3;
    case kCtrlNext: {
        if (s8(value) < 64) return 3;
        int i = kForNest - 1;
        while (i >= 0 && s.forCount[i] == 0xffff) --i;
        if (i < 0) return 3;
        if (s.forCount[i] != 0 && --s.forCount[i] == 0) {
            s.forCount[i] = 0xffff;  // last pass done
            return 3;
        }
        s.pos = s.forPos[i];
        return 3;
    }
    case kCtrlLockProtect:
        lock_[ch] |= 0x40;
        if (s8(value) < 64) lock_[ch] &= u8(~0x40);
        return 3;
    case kCtrlLock:
        if (s8(value) >= 64) {
            const int r = lockChannel();
            s.chanMap[ch] = u8(r ? r - 1 : ch);
        } else {
            flushChannelNotes(ch);
            releaseChannel(s.chanMap[ch] + 1);
            s.chanMap[ch] = u8(ch);
        }
        return 3;
    case kCtrlIndirect:
        s.indirect[ch] = u8(value);
        return 3;
    default:
        break;
    }
    if (!(lock_[ch] & 0x80)) send(u8(0xb0 | s.chanMap[ch]), u8(ctrl), u8(value));
    return 3;
}

namespace {
u32 readVlq(const u8* p, size_t size, size_t at, size_t& len) {
    u32 v = 0;
    len = 0;
    while (at + len < size) {
        const u8 c = p[at + len++];
        v = (v << 7) | (c & 0x7f);
        if (!(c & 0x80)) break;
    }
    return v;
}
} // namespace

// XMIDI note-on: status, key, velocity, VLQ duration in ticks.
size_t XmiSequencer::noteEvent(Seq& s) {
    const u8* ev = events(s);
    const u8 ch = ev[s.pos] & 0x0f;
    const u8 key = evByte(s, s.pos + 1);
    const u8 vel = evByte(s, s.pos + 2);
    size_t n = 0;
    const u32 dur = readVlq(ev, eventsSize(s), s.pos + 3, n);
    if (!(lock_[ch] & 0x80)) {
        int idx = 0;
        while (idx < kQueueSize && s.qChan[idx] != 0xff) ++idx;
        if (idx == kQueueSize) idx = 0;  // queue full: entry 0 is overwritten
        else ++s.noteCount;
        s.qChan[idx] = ch;
        s.qKey[idx] = key;
        s.qDur[idx] = s32(dur - 1);
        const u8 p = s.chanMap[ch];
        ++notes_[p];
        send(u8(0x90 | p), key, vel);
    }
    return 3 + n;
}

size_t XmiSequencer::metaEvent(Seq& s, int h) {
    const u8* ev = events(s);
    const u8 type = evByte(s, s.pos + 1);
    size_t n = 0;
    const u32 len = readVlq(ev, eventsSize(s), s.pos + 2, n);
    const size_t d = s.pos + 2 + n;
    switch (type) {
    case 0x2f:  // end of track
        cleanup(s);
        s.status = Done;
        if (s.releasePending) releaseSequence(h);
        break;
    case 0x58: {  // time signature: numerator, log2 denominator
        s.beatsPerBar = evByte(s, d);
        const int dd = evByte(s, d + 1);
        // 32-bit arithmetic like the driver; its 16-bit multiplier saturates at 2^16.
        s.beatStep = dd >= 2 ? kBeatStep << std::min(dd - 2, 16) : kBeatStep >> (2 - dd);
        s.beatAcc = s.beatStep;
        break;
    }
    case 0x51:  // tempo, microseconds per quarter note
        s.tempo16 = ((u32(evByte(s, d)) << 16) | (u32(evByte(s, d + 1)) << 8) | evByte(s, d + 2)) << 4;
        break;
    default:
        break;
    }
    return 2 + n + len;
}

void XmiSequencer::processEvents(Seq& s, int h) {
    const u8* ev = events(s);
    for (;;) {
        if (s.pos >= eventsSize(s)) {  // no end-of-track event: stop cleanly
            cleanup(s);
            s.status = Done;
            if (s.releasePending) releaseSequence(h);
            return;
        }
        const u8 b = ev[s.pos];
        if (b < 0x80) {  // interval byte: ticks until the next events
            ++s.pos;
            s.delay = b;
            return;
        }
        const u8 hi = b & 0xf0, ch = b & 0x0f;
        const u8 d1 = evByte(s, s.pos + 1), d2 = evByte(s, s.pos + 2);
        size_t len = 3;
        bool pass = false;  // forward to the synth unless the channel is locked
        switch (hi) {
        case 0xf0:
            if (b == 0xff) {
                len = metaEvent(s, h);
            } else {  // sysex: VLQ length + data, not sent to the FM synth
                size_t n = 0;
                const u32 l = readVlq(ev, eventsSize(s), s.pos + 1, n);
                len = 1 + n + l;
            }
            break;
        case 0xe0:
            s.pitchL[ch] = d1;
            s.pitchH[ch] = d2;
            pitchL_[ch] = d1;
            pitchH_[ch] = d2;
            pass = true;
            break;
        case 0xd0:
            len = 2;
            pass = true;
            break;
        case 0xc0:
            s.program[ch] = d1;
            program_[ch] = d1;
            len = 2;
            pass = true;
            break;
        case 0xb0:
            controller(s, h, ch, d1, d2);
            break;
        case 0xa0:
            pass = true;
            break;
        default:  // 0x80 and 0x90 are both XMIDI note-ons with a duration
            len = noteEvent(s);
            break;
        }
        if (pass && !(lock_[ch] & 0x80)) send(u8(hi | s.chanMap[ch]), d1, d2);
        if (!s.used) return;  // released at end of track
        s.pos += len;
        if (s.status != Playing) return;
    }
}

// Linear ramps: 83 units per service tick against a period of 10*ms/|delta|.
void XmiSequencer::rampVolume(Seq& s) {
    const bool up = s.volume < s.volumeTarget;
    s32 acc = s32(s.volumeAcc + 83);
    int steps = 0;
    s.volumeAcc = u32(acc);
    while (acc - s32(s.volumePeriod) >= 0) {
        acc -= s32(s.volumePeriod);
        s.volumeAcc = u32(acc);
        ++steps;
    }
    if (steps == 0) return;
    int v = up ? s.volume + steps : s.volume - steps;
    if (up ? v > s.volumeTarget : v < s.volumeTarget) v = s.volumeTarget;
    s.volume = v;
    applyVolume(s);
}

void XmiSequencer::rampTempo(Seq& s) {
    const bool up = s.tempo < s.tempoTarget;
    s32 acc = s32(s.tempoRampAcc + 83);
    int steps = 0;
    s.tempoRampAcc = u32(acc);
    while (acc - s32(s.tempoPeriod) >= 0) {
        acc -= s32(s.tempoPeriod);
        s.tempoRampAcc = u32(acc);
        ++steps;
    }
    if (steps == 0) return;
    int v = up ? s.tempo + steps : s.tempo - steps;
    if (up ? v > s.tempoTarget : v < s.tempoTarget) v = s.tempoTarget;
    s.tempo = v;
}

void XmiSequencer::service() {
    if (busy_) return;
    busy_ = true;
    int remaining = registered_;
    for (int h = 0; h < kMaxSequences && remaining > 0; ++h) {
        Seq& s = seqs_[h];
        if (!s.used) continue;
        --remaining;
        if (s.status != Playing) continue;
        // Relative tempo: one sequencer tick per 100 accumulated percent.
        s.tickAcc += s.tempo;
        int acc = s.tickAcc - 100;
        bool left = false;
        while (acc >= 0) {
            s.tickAcc = acc;
            u32 b = s.beatAcc + s.beatStep;
            if (driverGe(b, s.tempo16)) {
                b -= s.tempo16;
                if (++s.beat >= s.beatsPerBar) {
                    s.beat = 0;
                    ++s.measure;
                }
            }
            s.beatAcc = b;
            if (s.noteCount != 0) {  // note queue: durations run down, expired notes are released
                for (int i = 0; i < kQueueSize; ++i) {
                    if (s.qChan[i] == 0xff || --s.qDur[i] >= 0) continue;
                    const u8 ch = s.qChan[i];
                    s.qChan[i] = 0xff;
                    const u8 p = s.chanMap[ch];
                    --notes_[p];
                    send(u8(0x80 | p), s.qKey[i], 0);
                    if (--s.noteCount == 0) break;
                }
            }
            if (--s.delay <= 0) {
                processEvents(s, h);
                if (!s.used || s.status != Playing) {
                    left = true;
                    break;
                }
            }
            acc = s.tickAcc - 100;
        }
        if (left) continue;
        if (s.tempo != s.tempoTarget) rampTempo(s);
        if (s.volume != s.volumeTarget) rampVolume(s);
    }
    busy_ = false;
}

void XmiSequencer::setRelativeVolume(int h, int percent, int ms) {
    Seq* s = seq(h);
    if (!s) return;
    s->volumeTarget = percent;
    if (ms == 0) {
        s->volume = percent;
        applyVolume(*s);
        return;
    }
    const int delta = percent > s->volume ? percent - s->volume : s->volume - percent;
    if (delta == 0) return;
    u32 period = u32(10) * u32(u16(ms)) / u32(delta);
    s->volumePeriod = period ? period : 1;
    s->volumeAcc = 0;
}

int XmiSequencer::relativeVolume(int h) const {
    const Seq* s = seq(h);
    return s ? s->volume : -1;
}

void XmiSequencer::setRelativeTempo(int h, int percent, int ms) {
    Seq* s = seq(h);
    if (!s) return;
    s->tempoTarget = percent;
    if (ms == 0) {
        s->tempo = percent;
        return;
    }
    const int delta = percent > s->tempo ? percent - s->tempo : s->tempo - percent;
    if (delta == 0) return;
    u32 period = u32(10) * u32(u16(ms)) / u32(delta);
    s->tempoPeriod = period ? period : 1;
    s->tempoRampAcc = 0;
}

int XmiSequencer::relativeTempo(int h) const {
    const Seq* s = seq(h);
    return s ? s->tempo : -1;
}

int XmiSequencer::beatCount(int h) const {
    const Seq* s = seq(h);
    return s ? s->beat : -1;
}

int XmiSequencer::measureCount(int h) const {
    const Seq* s = seq(h);
    return s ? s->measure : -1;
}

void XmiSequencer::branchIndex(int h, int marker) {
    Seq* s = seq(h);
    if (!s) return;
    for (const auto& b : s->info->branches) {
        if (b.marker != u8(marker)) continue;
        s->pos = b.offset;
        s->delay = 0;
        flushNotes(*s);
        for (auto& c : s->forCount) c = 0xffff;
        return;
    }
}

int XmiSequencer::controllerValue(int h, int channel, int ctrl) const {
    const Seq* s = seq(h);
    if (!s || channel < 0 || channel > 15) return -1;
    if (ctrl == kCtrlCallback) return s->callbackValue;
    const int slot = savedSlot(ctrl);
    return slot < 0 ? -1 : int(s8(s->saved[slot][channel]));
}

void XmiSequencer::setControllerValue(int h, int channel, int ctrl, int value) {
    Seq* s = seq(h);
    if (!s || channel < 0 || channel > 15) return;
    controller(*s, h, channel, ctrl, value);
}

int XmiSequencer::channelNotes(int h, int channel) const {
    const Seq* s = seq(h);
    if (!s) return -1;
    int n = 0;
    for (int i = 0; i < kQueueSize; ++i)
        if (s->qChan[i] == channel) ++n;
    return n;
}

void XmiSequencer::setIndirectController(int h, int index, u8 value) {
    Seq* s = seq(h);
    if (!s || index < 0 || index > 255) return;
    if (size_t(index) >= s->ctrlTable.size()) s->ctrlTable.resize(size_t(index) + 1, 0);
    s->ctrlTable[size_t(index)] = value;
}

// Picks the physical channel 2-9 (never the percussion channel) with the
// fewest queued notes that is neither locked nor lock-protected; if none,
// lock protection is ignored. Ties go to the higher channel.
int XmiSequencer::lockChannel() {
    int best = -1;
    u8 fewest = 0xff;
    for (u8 mask : {u8(0xc0), u8(0x80)}) {
        for (int p = 8; p >= 1; --p) {
            if (!(lock_[p] & mask) && notes_[p] < fewest) {
                fewest = notes_[p];
                best = p;
            }
        }
        if (best >= 0) break;
    }
    if (best < 0) return 0;
    send(u8(0xb0 | best), 64, 0);
    flushChannelNotes(best);
    notes_[best] = 0;
    lock_[best] |= 0x80;
    return best + 1;
}

// Unlocks and restores the channel's controllers, program and pitch bend
// from the driver-global state (what the music last set on it).
void XmiSequencer::releaseChannel(int ch1) {
    const int p = ch1 - 1;
    if (p < 0 || p > 15 || !(lock_[p] & 0x80)) return;
    lock_[p] &= 0x7f;
    notes_[p] = 0;
    send(u8(0xb0 | p), 64, 0);
    send(u8(0xb0 | p), 123, 0);
    for (int i = 0; i < kSavedCtrls; ++i)
        if (ctrl_[i][p] != 0xff) send(u8(0xb0 | p), u8(kSavedCtrlNum[i]), ctrl_[i][p]);
    if (program_[p] != 0xff) send(u8(0xc0 | p), program_[p], 0);
    if (pitchL_[p] != 0xff && pitchH_[p] != 0xff) send(u8(0xe0 | p), pitchL_[p], pitchH_[p]);
}

void XmiSequencer::mapSequenceChannel(int h, int seqCh1, int physCh1) {
    Seq* s = seq(h);
    if (!s || seqCh1 < 1 || seqCh1 > 16) return;
    s->chanMap[seqCh1 - 1] = u8((physCh1 - 1) & 0x0f);
}

int XmiSequencer::trueSequenceChannel(int h, int seqCh1) const {
    const Seq* s = seq(h);
    if (!s || seqCh1 < 1 || seqCh1 > 16) return -1;
    return s->chanMap[seqCh1 - 1] + 1;
}

} // namespace st::audio
