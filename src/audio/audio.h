// Public audio API: SDL3 output of the AIL-style FM music driver (OPL2/OPL3
// emulation) and the digital sound effects. Thread safe; independent of game
// logic (which effect or music plays when is decided by the game modules).
#pragma once

#include "audio/mixer.h"

#include <memory>
#include <vector>

namespace st::audio {

struct Config {
    MusicDevice musicDevice = MusicDevice::Opl2;
    bool digitalEnabled = true;
    int digitalVoices = 1;                       // the game used one digital voice
    DigitalVolume digitalVolume = DigitalVolume::SoundBlasterPro;
    int timerHz = 256;                           // the game's AIL timer (shares the PIT with the driver)
    float musicGain = 1.0f;
    float digitalGain = 1.0f;
    bool swapStereo = false;                     // OPL3: swap output channels
    int bufferFrames = 1024;                     // device buffer hint
};

// Opens the audio subsystem and device. Without a device the API still works
// but nothing is rendered.
bool init(const Config& cfg = {});
void shutdown();

void setMusicDevice(MusicDevice dev);
MusicDevice musicDevice();
// Global Timbre Library for the device: SAMPLE.AD (OPL2) or SAMPLE.OPL (OPL3).
bool loadTimbreBank(const std::vector<u8>& data);

// Pre-installs the timbres of sequences 0..count-1, as the game does when it
// loads a music file (11 or 16 sequences).
void preloadTimbres(std::shared_ptr<const XmiFile> xmi, int count);

// Parsed data that can be shared between many play calls.
std::shared_ptr<const XmiFile> loadXmi(const std::vector<u8>& data);
std::shared_ptr<const PcmSound> loadVoc(const std::vector<u8>& data);

// Music: one XMIDI sequence; `loop` restarts it when it ends (most tracks
// also loop internally with XMIDI FOR/NEXT).
bool playMusic(const std::vector<u8>& xmiData, int sequence, bool loop);
bool playMusic(std::shared_ptr<const XmiFile> xmi, int sequence, bool loop);
void stopMusic();
bool musicPlaying();
void setMusicVolume(int volume);  // 0-127 (AIL relative volume volume*100/127 %; the game's 96 % = 122)

// Further XMIDI sequences on the same driver, e.g. FM sound effects. The
// indirect controller table is read by XMIDI controller 115. Handle 0 = failed.
using SequenceHandle = int;
SequenceHandle playSequence(std::shared_ptr<const XmiFile> xmi, int sequence, bool loop, int volume,
                            const std::vector<u8>& indirectControllers = {});
void stopSequence(SequenceHandle h);
bool sequencePlaying(SequenceHandle h);
void setSequenceVolume(SequenceHandle h, int volume);  // 0-127
// Exact AIL relative volume in percent, reached over `ms` milliseconds.
void setSequenceRelativeVolume(SequenceHandle h, int percent, int ms = 0);

// Digital effects. Handle 0 = not played.
using SampleHandle = int;
SampleHandle playSample(const std::vector<u8>& vocData, int volume = 127, int pan = 64);
SampleHandle playSample(std::shared_ptr<const PcmSound> pcm, int volume = 127, int pan = 64);
void stopSample(SampleHandle h);
bool samplePlaying(SampleHandle h);
void setSampleVolume(SampleHandle h, int volume, int pan = 64);
void setDigitalEnabled(bool on);

} // namespace st::audio
