// sealteam_audiotest - offline checks of the audio subsystem: renders XMIDI
// music through the AIL OPL driver re-implementation to a WAV file, converts
// VOC effects to WAV, lists/traces sequences and verifies the built-in driver
// tables against the user's ADLIB.ADV. Not part of the game executable.

#include "audio/audio.h"
#include "audio/mixer.h"
#include "core/common.h"
#include "data/ealib.h"
#include "data/gamefs.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace st;
using namespace st::audio;

namespace {

void usage() {
    std::printf(
        "usage: sealteam_audiotest [--data DIR] [options]\n"
        "  --xmi NAME        XMIDI file: entry of sound.lib or a host path\n"
        "  --seq N           sequence index (default 0)\n"
        "  --device D        opl2 | opl3 (default opl2)\n"
        "  --seconds S       length to render (default 30)\n"
        "  --out FILE        output WAV (music: 49716 Hz stereo; VOC: its own rate)\n"
        "  --volume V        music volume 0-127 (default 127)\n"
        "  --no-loop         do not restart the sequence when it ends\n"
        "  --fx N@T[:R]      also start sequence N of the XMI at T seconds, indirect\n"
        "                    controller 0 = R (repeat count, default 1), volume 127\n"
        "  --voc NAME        VOC effect: converted to WAV (with --out and no --xmi)\n"
        "  --voc-at T        with --xmi: play the VOC through the mixer at T seconds\n"
        "  --list            list the sequences of the XMI\n"
        "  --trace           print the MIDI messages the sequencer sends\n"
        "  --verify-driver   compare the built-in tables with ADLIB.ADV\n"
        "  --play            also play live through the audio device\n"
        "  --regdump FILE    write every OPL register write (service tick, bank,\n"
        "                    register, value) for comparison with the real driver\n");
}

bool readInput(const std::string& name, std::vector<u8>& out) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(name, ec)) {
        std::ifstream f(name, std::ios::binary);
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !out.empty();
    }
    return resources().read(name, out);
}

bool writeWav(const std::string& path, const std::vector<s16>& samples, int channels, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto u32le = [&](u32 v) { char b[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; f.write(b, 4); };
    auto u16le = [&](u16 v) { char b[2] = {char(v), char(v >> 8)}; f.write(b, 2); };
    const u32 bytes = u32(samples.size() * 2);
    f.write("RIFF", 4); u32le(36 + bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32le(16); u16le(1); u16le(u16(channels)); u32le(u32(rate));
    u32le(u32(rate * channels * 2)); u16le(u16(channels * 2)); u16le(16);
    f.write("data", 4); u32le(bytes);
    for (s16 s : samples) u16le(u16(s));
    return bool(f);
}

// Prints every message the sequencer emits (no synthesis).
class TraceSink : public MidiSink {
public:
    int tick = 0;
    bool print = true;
    void midiMessage(u8 status, u8 d1, u8 d2) override {
        if (print) std::printf("%7d %6.2fs  %02X %3d %3d\n", tick, tick / XmiSequencer::kServiceHz, status, d1, d2);
    }
};

// Runs a sequence without output and reports how long it plays.
void listSequences(const std::shared_ptr<const XmiFile>& xmi) {
    for (int i = 0; i < xmi->count(); ++i) {
        const auto* s = xmi->sequence(i);
        TraceSink sink;
        sink.print = false;
        XmiSequencer seq(sink);
        seq.init();
        const int h = seq.registerSequence(xmi, i);
        std::printf("seq %2d: %3zu timbres, %5zu event bytes", i, s->timbres.size(), s->eventsSize);
        if (h < 0) { std::printf("  (no events)\n"); continue; }
        seq.start(h);
        const int limit = int(XmiSequencer::kServiceHz * 600);
        int t = 0;
        while (t < limit && seq.status(h) == XmiSequencer::Playing) { seq.service(); ++t; }
        if (seq.status(h) == XmiSequencer::Playing) std::printf(", loops (still playing after 600 s)\n");
        else std::printf(", ends after %d ticks (%.2f s)\n", t, t / XmiSequencer::kServiceHz);
    }
}

size_t findBytes(const std::vector<u8>& hay, const u8* needle, size_t n) {
    for (size_t i = 0; i + n <= hay.size(); ++i)
        if (std::memcmp(&hay[i], needle, n) == 0) return i;
    return size_t(-1);
}

// Checks that the formula-built tables equal the ones inside the driver.
int verifyDriver(const char* name, bool opl3) {
    std::vector<u8> drv;
    if (!gameFS().readFile(name, drv)) {
        std::fprintf(stderr, "%s not found\n", name);
        return 1;
    }
    std::printf("%s:\n", name);
    int bad = 0;
    // F-number table: 192 words; upper-range entries are stored as 0xFE00 | (fnum & 0x1FF).
    std::vector<u8> ft;
    for (int i = 0; i < 192; ++i) {
        const auto& e = AilOplSynth::freqTable()[i];
        const u16 w = e.upper ? u16(0xfe00 | (e.fnum & 0x1ff)) : e.fnum;
        ft.push_back(u8(w));
        ft.push_back(u8(w >> 8));
    }
    const size_t fpos = findBytes(drv, ft.data(), ft.size());
    std::printf("F-number table (192 entries): %s\n", fpos != size_t(-1) ? "matches" : "MISMATCH");
    bad += fpos == size_t(-1);
    u8 vg[16];
    for (int i = 0; i < 16; ++i) vg[i] = AilOplSynth::velocityGraph(i);
    const bool velOk = findBytes(drv, vg, 16) != size_t(-1);
    std::printf("velocity graph: %s\n", velOk ? "matches" : "MISMATCH");
    bad += !velOk;
    // Register reset table: indexed by register number, starting at register 1.
    u8 rt[0xf5];
    for (int r = 1; r <= 0xf5; ++r) rt[r - 1] = AilOplSynth::resetRegister(0, r);
    const bool resetOk = findBytes(drv, rt, sizeof rt) != size_t(-1);
    std::printf("register reset table: %s\n", resetOk ? "matches" : "MISMATCH");
    bad += !resetOk;
    if (opl3) {  // second register bank, registers 0x101..0x1F5
        for (int r = 1; r <= 0xf5; ++r) rt[r - 1] = AilOplSynth::resetRegister(1, r);
        const bool ok = findBytes(drv, rt, sizeof rt) != size_t(-1);
        std::printf("register reset table, bank 1: %s\n", ok ? "matches" : "MISMATCH");
        bad += !ok;
    }
    const u8 defaults[] = {7, 1, 10, 11, 64, 114, 110, 111, 112, 127, 0, 64, 127, 0, 0, 0, 0, 0,
                           68, 48, 95, 78, 41, 3, 110, 122, 255};
    const bool defOk = findBytes(drv, defaults, sizeof defaults) != size_t(-1);
    std::printf("controller/program defaults: %s\n", defOk ? "matches" : "MISMATCH");
    bad += !defOk;
    if (drv.size() > 0xdb) {
        const size_t desc = rd16(&drv[0]);
        size_t p = desc;
        while (p + 4 <= drv.size() && rd16(&drv[p]) != 0xffff) p += 4;
        p += 2;
        if (p + 0x16 <= drv.size())
            std::printf("driver descriptor: type %d, suffix %.3s, service rate %d Hz\n", rd16(&drv[p + 2]),
                        reinterpret_cast<const char*>(&drv[p + 4]), rd16(&drv[p + 0x14]));
    }
    return bad ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string dataDir, xmiName, vocName, outPath, regdumpPath, device = "opl2";
    int seqIndex = 0, volume = 127;
    double seconds = 30.0, vocAt = -1.0;
    bool loop = true, list = false, trace = false, verify = false, play = false;
    struct Fx { int seq; double at; int repeat; bool started = false; };
    std::vector<Fx> fx;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") dataDir = next();
        else if (a == "--xmi") xmiName = next();
        else if (a == "--seq") seqIndex = std::atoi(next().c_str());
        else if (a == "--device") device = next();
        else if (a == "--seconds") seconds = std::atof(next().c_str());
        else if (a == "--out") outPath = next();
        else if (a == "--volume") volume = std::atoi(next().c_str());
        else if (a == "--no-loop") loop = false;
        else if (a == "--voc") vocName = next();
        else if (a == "--voc-at") vocAt = std::atof(next().c_str());
        else if (a == "--list") list = true;
        else if (a == "--trace") trace = true;
        else if (a == "--verify-driver") verify = true;
        else if (a == "--play") play = true;
        else if (a == "--regdump") regdumpPath = next();
        else if (a == "--fx") {
            const std::string v = next();
            Fx f{0, 0.0, 1};
            std::sscanf(v.c_str(), "%d@%lf:%d", &f.seq, &f.at, &f.repeat);
            fx.push_back(f);
        } else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
    }

    if (!gameFS().init(dataDir, "")) {
        std::fprintf(stderr, "Game files not found (use --data DIR)\n");
        return 1;
    }
    if (verify) return verifyDriver("adlib.adv", false) | verifyDriver("sbp2fm.adv", true);
    resources().openLib("sound.lib");

    // VOC conversion only.
    if (!vocName.empty() && xmiName.empty()) {
        std::vector<u8> data;
        PcmSound pcm;
        if (!readInput(vocName, data) || !decodeVoc(data.data(), data.size(), pcm)) {
            std::fprintf(stderr, "cannot decode %s\n", vocName.c_str());
            return 1;
        }
        std::printf("%s: %zu frames, %d ch, %.2f Hz, %.3f s\n", vocName.c_str(), pcm.frames(), pcm.channels,
                    pcm.rate, pcm.frames() / pcm.rate);
        if (!outPath.empty() && !writeWav(outPath, pcm.samples, pcm.channels, int(pcm.rate + 0.5))) return 1;
        return 0;
    }

    if (xmiName.empty()) { usage(); return 1; }
    std::vector<u8> data;
    if (!readInput(xmiName, data)) { std::fprintf(stderr, "cannot read %s\n", xmiName.c_str()); return 1; }
    auto xmi = loadXmi(data);
    if (!xmi) { std::fprintf(stderr, "%s is not an XMIDI file\n", xmiName.c_str()); return 1; }
    if (list) { listSequences(xmi); return 0; }

    if (trace) {
        TraceSink sink;
        XmiSequencer seq(sink);
        seq.init();
        const int h = seq.registerSequence(xmi, seqIndex);
        if (h < 0) return 1;
        seq.start(h);
        const int ticks = int(seconds * XmiSequencer::kServiceHz);
        for (sink.tick = 0; sink.tick < ticks && seq.status(h) == XmiSequencer::Playing; ++sink.tick) {
            for (auto& f : fx) {
                if (f.started || sink.tick < f.at * XmiSequencer::kServiceHz) continue;
                f.started = true;
                const int e = seq.registerSequence(xmi, f.seq, {u8(f.repeat), 0x7f});
                std::printf("---- effect sequence %d -> handle %d\n", f.seq, e);
                if (e >= 0) seq.start(e);
            }
            seq.service();
        }
        return 0;
    }

    const MusicDevice dev = device == "opl3" ? MusicDevice::Opl3 : MusicDevice::Opl2;
    std::vector<u8> gtl;
    if (!resources().read(dev == MusicDevice::Opl3 ? "SAMPLE.OPL" : "SAMPLE.AD", gtl))
        std::fprintf(stderr, "warning: timbre library not found\n");
    std::shared_ptr<const PcmSound> voc;
    if (!vocName.empty()) {
        std::vector<u8> vd;
        if (readInput(vocName, vd)) voc = loadVoc(vd);
    }

    if (!regdumpPath.empty()) {  // driver-level register log, one service call per tick
        FILE* f = std::fopen(regdumpPath.c_str(), "w");
        if (!f) return 1;
        int tick = 0;
        AilOplSynth synth(dev == MusicDevice::Opl3 ? OplMode::Opl3 : OplMode::Opl2);
        synth.setWriteHook([&](int b, int r, int v) { std::fprintf(f, "%d %d %02x %02x\n", tick, b, r, v); });
        synth.reset();
        XmiSequencer seq(synth);
        seq.init();
        TimbreBank bank;
        bank.parse(gtl);
        auto install = [&](int h) {  // the application's timbre request loop
            for (const auto& t : seq.sequenceInfo(h)->timbres) {
                size_t size = 0;
                const u8* rec = bank.find(t.bank, t.patch, size);
                if (!rec) break;  // (the game would hang; the emulator harness stops here)
                if (!synth.timbreInstalled(t.bank, t.patch)) synth.installTimbre(t.bank, t.patch, rec, size);
            }
        };
        const int h = seq.registerSequence(xmi, seqIndex);
        if (h < 0) return 1;
        install(h);
        seq.start(h);
        if (volume != 127) seq.setRelativeVolume(h, volume * 100 / 127, 0);
        const int ticks = int(seconds * XmiSequencer::kServiceHz);
        for (tick = 1; tick <= ticks; ++tick) {
            for (auto& fx1 : fx) {
                if (fx1.started || tick < fx1.at * XmiSequencer::kServiceHz) continue;
                fx1.started = true;
                const int e = seq.registerSequence(xmi, fx1.seq, {u8(fx1.repeat), 0x7f});
                if (e < 0) continue;
                install(e);
                seq.start(e);
            }
            seq.service();
        }
        std::fclose(f);
        return 0;
    }

    if (play) {  // live through SDL, in real time
        Config cfg;
        cfg.musicDevice = dev;
        if (!init(cfg)) return 1;
        loadTimbreBank(gtl);
        setMusicVolume(volume);
        playMusic(xmi, seqIndex, loop);
        const Uint64 start = SDL_GetTicks();
        while (SDL_GetTicks() - start < Uint64(seconds * 1000)) {
            const double t = (SDL_GetTicks() - start) / 1000.0;
            for (auto& f : fx)
                if (!f.started && t >= f.at) { f.started = true; playSequence(xmi, f.seq, false, 127, {u8(f.repeat), 0x7f}); }
            if (voc && vocAt >= 0 && t >= vocAt) { playSample(voc); vocAt = -1; }
            SDL_Delay(10);
        }
        std::printf("played %.1f s live, music %s\n", seconds, musicPlaying() ? "still playing" : "ended");
        shutdown();
        return 0;
    }

    Mixer mixer;
    mixer.setMusicDevice(dev);
    mixer.loadTimbreBank(gtl);
    mixer.setMusicVolume(volume);
    if (!mixer.playMusic(xmi, seqIndex, loop)) { std::fprintf(stderr, "cannot start sequence %d\n", seqIndex); return 1; }

    const int total = int(seconds * Mixer::kRate);
    const int block = 512;
    std::vector<s16> out(size_t(total) * 2);
    int maxVoices = 0;
    bool wasPlaying = true;
    for (int pos = 0; pos < total; pos += block) {
        const double t = pos / Mixer::kRate;
        for (auto& f : fx) {
            if (f.started || t < f.at) continue;
            f.started = true;
            if (!mixer.playSequence(xmi, f.seq, false, 127, {u8(f.repeat), 0x7f}))
                std::fprintf(stderr, "cannot start effect sequence %d\n", f.seq);
        }
        if (voc && vocAt >= 0 && t >= vocAt) { mixer.playSample(voc, 127, 64); vocAt = -1; }
        mixer.render(&out[size_t(pos) * 2], std::min(block, total - pos));
        maxVoices = std::max(maxVoices, mixer.synth()->activeVoices());
        if (wasPlaying && !mixer.musicPlaying()) {
            std::printf("music ended at %.3f s\n", (pos + block) / Mixer::kRate);
            wasPlaying = false;
        }
    }
    std::printf("rendered %.1f s, music %s, max voices %d\n", seconds,
                mixer.musicPlaying() ? "still playing" : "ended", maxVoices);
    if (!outPath.empty() && !writeWav(outPath, out, 2, int(Mixer::kRate + 0.5))) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }
    return 0;
}
