// Creative Voice File (.VOC) decoding for the digital sound effects. Format
// notes in docs/audio.md.
#pragma once

#include "core/common.h"

#include <cstddef>
#include <vector>

namespace st::audio {

// Decoded PCM, signed 16-bit, interleaved when stereo.
struct PcmSound {
    std::vector<s16> samples;
    int channels = 1;
    double rate = 0.0;                   // Hz; SB time constants give fractional rates
    size_t loopStart = size_t(-1);       // frame range repeated forever (VOC repeat 0xFFFF)
    size_t loopEnd = 0;

    size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
    bool loops() const { return loopStart < loopEnd; }
};

// Handles block types 1 (sound), 2 (continuation), 3 (silence), 4/5 (marker,
// text; ignored), 6/7 (repeat), 8 (extended rate/stereo) and 9 (new format).
// Sample formats: 8-bit unsigned and 16-bit signed PCM (Creative ADPCM is not
// supported). Returns false if the data is not a usable VOC file.
bool decodeVoc(const u8* data, size_t size, PcmSound& out);

} // namespace st::audio
