// XMIDI ("Extended MIDI", Miles Design) files and the AIL 2.x XMIDI sequencer,
// i.e. the device-independent half of the AIL .ADV music drivers: event
// interpretation, note queue, FOR/NEXT loops, channel locking, relative
// volume/tempo and the controller state that is restored when a channel is
// released. It emits plain MIDI channel messages to a MidiSink (the synth
// half, see ail_opl.h). Behaviour notes in docs/audio.md.
#pragma once

#include "core/common.h"

#include <functional>
#include <memory>
#include <vector>

namespace st::audio {

class XmiFile {
public:
    struct Timbre { u8 patch = 0; u8 bank = 0; };
    struct Branch { u8 marker = 0; u32 offset = 0; };  // offset into the EVNT data
    struct Sequence {
        std::vector<Timbre> timbres;   // TIMB: timbres the sequence needs
        std::vector<Branch> branches;  // RBRN: branch targets
        size_t events = 0;             // offset of the EVNT data in bytes()
        size_t eventsSize = 0;
    };

    // Accepts FORM XDIR + CAT XMID (a collection) or a bare FORM XMID.
    bool parse(std::vector<u8> data);
    int count() const { return int(seqs_.size()); }
    const Sequence* sequence(int index) const;
    const std::vector<u8>& bytes() const { return data_; }

private:
    bool parseForm(size_t pos, size_t end);
    std::vector<u8> data_;
    std::vector<Sequence> seqs_;
};

// Receiver of the MIDI channel messages produced by the sequencer.
class MidiSink {
public:
    virtual ~MidiSink() = default;
    virtual void midiMessage(u8 status, u8 data1, u8 data2) = 0;
};

class XmiSequencer {
public:
    static constexpr int kMaxSequences = 8;  // sequence handles per driver
    static constexpr int kQueueSize = 32;    // pending note-offs per sequence
    static constexpr int kForNest = 4;       // FOR/NEXT nesting depth
    // The driver's service rate (from its descriptor): AIL programs the PIT
    // with 1193182 / 120 = 9943.
    static constexpr double kServiceHz = 1193181.666 / 9943.0;

    enum Status { Stopped = 0, Playing = 1, Done = 2 };
    using Callback = std::function<void(int handle, int value)>;

    explicit XmiSequencer(MidiSink& sink) : sink_(sink) {}

    // init_driver: default controllers, pitch and MT-32 style default
    // programs on channels 2-10.
    void init();
    // Stops and releases all sequences.
    void shutdown();

    // Returns a handle or -1 (no free handle / sequence not found). The
    // controller table is the application array read by indirect controllers
    // (XMIDI controller 115).
    int registerSequence(std::shared_ptr<const XmiFile> file, int index, std::vector<u8> controllerTable = {});
    // Frees the handle; if the sequence is still playing it is freed when it ends.
    void releaseSequence(int h);
    const XmiFile::Sequence* sequenceInfo(int h) const;

    void start(int h);
    void stop(int h);
    void resume(int h);
    int status(int h) const;

    // Percent (100 = unchanged) reached linearly over `ms` milliseconds.
    void setRelativeVolume(int h, int percent, int ms);
    int relativeVolume(int h) const;
    void setRelativeTempo(int h, int percent, int ms);
    int relativeTempo(int h) const;
    int beatCount(int h) const;
    int measureCount(int h) const;
    void branchIndex(int h, int marker);

    // Channels are 0-based here.
    int controllerValue(int h, int channel, int controller) const;
    void setControllerValue(int h, int channel, int controller, int value);
    int channelNotes(int h, int channel) const;
    void setIndirectController(int h, int index, u8 value);

    int lockChannel();              // 1-based physical channel, 0 if none
    void releaseChannel(int ch1);   // 1-based
    void mapSequenceChannel(int h, int seqCh1, int physCh1);
    int trueSequenceChannel(int h, int seqCh1) const;

    // Called from service() for XMIDI controller 119.
    void setCallback(Callback cb) { callback_ = std::move(cb); }

    // One timer interrupt of the driver (kServiceHz).
    void service();

private:
    static constexpr int kSavedCtrls = 9;  // 7, 1, 10, 11, 64, 114, 110, 111, 112

    struct Seq {
        bool used = false;
        std::shared_ptr<const XmiFile> file;
        const XmiFile::Sequence* info = nullptr;
        std::vector<u8> ctrlTable;
        int status = Stopped;
        bool started = false;
        bool releasePending = false;
        size_t pos = 0;           // event pointer, offset into the EVNT data
        s32 delay = 0;            // ticks until the next event group
        int noteCount = 0;
        u8 qChan[kQueueSize];     // 0xff = free entry
        u8 qKey[kQueueSize];
        s32 qDur[kQueueSize];     // remaining ticks - 1
        u16 forCount[kForNest];   // 0xffff = free, 0 = endless
        size_t forPos[kForNest];
        u8 chanMap[16];
        u8 program[16], pitchL[16], pitchH[16], indirect[16];
        u8 saved[kSavedCtrls][16];
        int callbackValue = -1;
        int volume = 100, volumeTarget = 100;
        u32 volumeAcc = 0, volumePeriod = 1;
        int tempo = 100, tempoTarget = 100;
        u32 tempoRampAcc = 0, tempoPeriod = 1;
        int tickAcc = 0;          // relative tempo accumulator (100 per tick)
        int beat = 0, measure = 0, beatsPerBar = 4;
        u32 beatStep = 0, beatAcc = 0, tempo16 = 0;
    };

    Seq* seq(int h);
    const Seq* seq(int h) const;
    const u8* events(const Seq& s) const { return s.file->bytes().data() + s.info->events; }
    size_t eventsSize(const Seq& s) const { return s.info->eventsSize; }
    u8 evByte(const Seq& s, size_t at) const { return at < eventsSize(s) ? events(s)[at] : 0; }

    void send(u8 status, u8 d1, u8 d2) { sink_.midiMessage(status, d1, d2); }
    void resetState(Seq& s);
    void flushNotes(Seq& s);
    void flushChannelNotes(int ch);
    void cleanup(Seq& s);
    void restoreState(Seq& s);
    void applyVolume(Seq& s);
    int controller(Seq& s, int h, int ch, int ctrl, int value);
    size_t noteEvent(Seq& s);
    size_t metaEvent(Seq& s, int h);
    void processEvents(Seq& s, int h);
    void rampVolume(Seq& s);
    void rampTempo(Seq& s);
    static int savedSlot(int ctrl);

    MidiSink& sink_;
    Callback callback_;
    Seq seqs_[kMaxSequences];
    int registered_ = 0;
    bool busy_ = false;
    // Driver-global channel state, indexed by (sequence) channel number.
    u8 ctrl_[kSavedCtrls][16];
    u8 program_[16], pitchL_[16], pitchH_[16];
    u8 notes_[16];   // queued notes per physical channel
    u8 lock_[16];    // 0x80 locked, 0x40 lock-protected
};

} // namespace st::audio
