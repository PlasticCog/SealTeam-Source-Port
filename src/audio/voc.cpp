#include "audio/voc.h"

#include <cstring>

namespace st::audio {

namespace {

constexpr char kMagic[] = "Creative Voice File\x1a";
constexpr size_t kMagicLen = 20;

// Rate of an SB DSP "time constant" (DSP command 0x40): 1 MHz / (256 - tc).
double rateFromTimeConstant(u8 tc, int channels) {
    return 1000000.0 / double(256 - tc) / double(channels);
}

struct Format {
    double rate = 0.0;
    int channels = 1;
    int bits = 8;
    bool ok = false;
};

} // namespace

bool decodeVoc(const u8* data, size_t size, PcmSound& out) {
    out = PcmSound{};
    if (size < 26 || std::memcmp(data, kMagic, kMagicLen) != 0) return false;
    size_t pos = rd16(data + 20);
    if (pos < 26 || pos > size) return false;

    Format cur;               // format of the last sound block (for type 2)
    Format ext;               // pending type 8 parameters, apply to the next type 1
    bool haveExt = false;
    bool first = true;
    size_t repeatFrame = 0;   // frame index at the repeat start
    size_t repeatBytes = 0;   // output sample count at the repeat start
    int repeatCount = -1;     // -1 none, 0xFFFF endless

    auto appendPcm = [&](const u8* p, size_t n, const Format& f) {
        if (!f.ok) return;
        if (first) {
            out.rate = f.rate;
            out.channels = f.channels;
            first = false;
        } else if (f.channels != out.channels) {
            logWarn("VOC: channel count changes between blocks, block skipped");
            return;
        } else if (f.rate != out.rate) {
            logWarn("VOC: sample rate changes between blocks (%.0f -> %.0f Hz)", out.rate, f.rate);
        }
        if (f.bits == 8) {
            for (size_t i = 0; i < n; ++i) out.samples.push_back(s16((int(p[i]) - 128) << 8));
        } else {
            for (size_t i = 0; i + 1 < n; i += 2) out.samples.push_back(rds16(p + i));
        }
    };

    while (pos < size) {
        const u8 type = data[pos];
        if (type == 0) break;  // terminator
        if (pos + 4 > size) break;
        size_t len = size_t(data[pos + 1]) | (size_t(data[pos + 2]) << 8) | (size_t(data[pos + 3]) << 16);
        const u8* b = data + pos + 4;
        if (pos + 4 + len > size) len = size - pos - 4;  // tolerate truncated files
        pos += 4 + len;

        switch (type) {
        case 1: {  // sound data: time constant, pack, samples
            if (len < 2) break;
            Format f;
            if (haveExt) {
                f = ext;
                haveExt = false;
            } else {
                f.channels = 1;
                f.rate = rateFromTimeConstant(b[0], 1);
                f.bits = 8;
                f.ok = b[1] == 0;
                if (!f.ok) logWarn("VOC: compressed data (pack %d) not supported", b[1]);
            }
            cur = f;
            appendPcm(b + 2, len - 2, cur);
            break;
        }
        case 2:  // continuation of the previous sound block
            appendPcm(b, len, cur);
            break;
        case 3: {  // silence: length - 1, time constant
            if (len < 3) break;
            const size_t n = size_t(rd16(b)) + 1;
            if (first) {
                out.rate = rateFromTimeConstant(b[2], 1);
                out.channels = 1;
                first = false;
            }
            out.samples.insert(out.samples.end(), n * size_t(out.channels), 0);
            break;
        }
        case 6:  // repeat start
            if (len < 2) break;
            repeatCount = rd16(b);
            repeatBytes = out.samples.size();
            repeatFrame = out.channels ? repeatBytes / size_t(out.channels) : 0;
            break;
        case 7:  // repeat end
            if (repeatCount == 0xffff) {
                out.loopStart = repeatFrame;
                out.loopEnd = out.frames();
            } else if (repeatCount > 0) {
                // The block plays once and is then repeated `count` times.
                const std::vector<s16> body(out.samples.begin() + std::ptrdiff_t(repeatBytes), out.samples.end());
                for (int i = 0; i < repeatCount; ++i) out.samples.insert(out.samples.end(), body.begin(), body.end());
            }
            repeatCount = -1;
            break;
        case 8: {  // extended: 16-bit time constant, pack, mode (0 mono, 1 stereo)
            if (len < 4) break;
            const u16 tc = rd16(b);
            ext.channels = b[3] ? 2 : 1;
            // tc = 65536 - 256000000 / (channels * rate)
            ext.rate = 256000000.0 / double(65536 - tc) / double(ext.channels);
            ext.bits = 8;
            ext.ok = b[2] == 0;
            if (!ext.ok) logWarn("VOC: compressed data (pack %d) not supported", b[2]);
            haveExt = true;
            break;
        }
        case 9: {  // new format: rate, bits, channels, format code
            if (len < 12) break;
            Format f;
            f.rate = double(rd32(b));
            f.bits = b[4];
            f.channels = b[5] ? b[5] : 1;
            const u16 codec = rd16(b + 6);
            f.ok = (codec == 0 && f.bits == 8) || (codec == 4 && f.bits == 16);
            if (!f.ok) logWarn("VOC: format %d/%d-bit not supported", codec, f.bits);
            cur = f;
            appendPcm(b + 12, len - 12, cur);
            break;
        }
        default:  // 4 marker, 5 text, unknown: skip
            break;
        }
    }
    return !first && out.rate > 0.0 && !out.samples.empty();
}

} // namespace st::audio
