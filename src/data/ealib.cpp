#include "data/ealib.h"

#include "data/gamefs.h"

#include <cctype>
#include <cstring>

namespace st {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(u8(a[i])) != std::tolower(u8(b[i]))) return false;
    return true;
}

constexpr size_t kDirEntrySize = 18;  // name[13], method, u32 offset
constexpr size_t kPxpkHeaderSize = 20;

} // namespace

bool lzssDecode(const u8* src, size_t srcLen, u8* out, size_t outLen) {
    constexpr unsigned N = 4096, F = 18, Threshold = 2;
    u8 ring[N];
    std::memset(ring, ' ', N);
    unsigned r = N - F;
    size_t in = 0, o = 0;
    unsigned flags = 0;
    while (o < outLen) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            if (in >= srcLen) return false;
            flags = src[in++] | 0xff00u;
        }
        if (flags & 1) {
            if (in >= srcLen) return false;
            const u8 c = src[in++];
            out[o++] = c;
            ring[r] = c;
            r = (r + 1) & (N - 1);
        } else {
            if (in + 1 >= srcLen) return false;
            const unsigned lo = src[in++], hi = src[in++];
            const unsigned pos = lo | ((hi & 0xf0) << 4);
            const unsigned len = (hi & 0x0f) + Threshold + 1;
            for (unsigned k = 0; k < len && o < outLen; ++k) {
                const u8 c = ring[(pos + k) & (N - 1)];
                out[o++] = c;
                ring[r] = c;
                r = (r + 1) & (N - 1);
            }
        }
    }
    return true;
}

bool EALib::open(const std::string& dosName) {
    if (!gameFS().readFile(dosName, data_)) return false;
    if (data_.size() < 7 || std::memcmp(data_.data(), "EALIB", 5) != 0) {
        logError("%s: not an EALIB archive", dosName.c_str());
        return false;
    }
    name_ = dosName;
    const unsigned count = rd16(&data_[5]);
    if (7 + (count + 1) * kDirEntrySize > data_.size()) return false;
    entries_.clear();
    entries_.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const u8* p = &data_[7 + i * kDirEntrySize];
        Entry e;
        e.name.assign(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), 13));
        e.method = p[13];
        e.offset = rd32(p + 14);
        u32 next = rd32(p + kDirEntrySize + 14);
        if (next == 0 || next > data_.size()) next = u32(data_.size());
        e.size = next > e.offset ? next - e.offset : 0;
        entries_.push_back(std::move(e));
    }
    return true;
}

int EALib::find(std::string_view entryName) const {
    for (size_t i = 0; i < entries_.size(); ++i)
        if (iequals(entries_[i].name, entryName)) return int(i);
    return -1;
}

bool EALib::read(int index, std::vector<u8>& out) const {
    if (index < 0 || size_t(index) >= entries_.size()) return false;
    const Entry& e = entries_[size_t(index)];
    if (size_t(e.offset) + e.size > data_.size()) return false;
    const u8* raw = &data_[e.offset];
    switch (e.method) {
    case Stored:
        out.assign(raw, raw + e.size);
        return true;
    case Lzss: {
        if (e.size < 4) return false;
        const u32 len = rd32(raw);
        out.resize(len);
        return lzssDecode(raw + 4, e.size - 4, out.data(), len);
    }
    case Picture: {
        if (e.size < kPxpkHeaderSize) return false;
        const u32 len = rd32(raw + 16);
        out.resize(kPxpkHeaderSize + len);
        std::memcpy(out.data(), raw, kPxpkHeaderSize);
        return lzssDecode(raw + kPxpkHeaderSize, e.size - kPxpkHeaderSize, out.data() + kPxpkHeaderSize, len);
    }
    default:
        logError("%s/%s: unknown method %d", name_.c_str(), e.name.c_str(), e.method);
        return false;
    }
}

bool EALib::read(std::string_view entryName, std::vector<u8>& out) const {
    return read(find(entryName), out);
}

Resources& resources() {
    static Resources instance;
    return instance;
}

bool Resources::openLib(const std::string& dosName) {
    EALib lib;
    if (!lib.open(dosName)) return false;
    libs_.push_back(std::move(lib));
    return true;
}

bool Resources::read(std::string_view entryName, std::vector<u8>& out) const {
    for (const auto& lib : libs_) {
        const int i = lib.find(entryName);
        if (i >= 0) return lib.read(i, out);
    }
    return false;
}

bool Resources::exists(std::string_view entryName) const {
    for (const auto& lib : libs_)
        if (lib.find(entryName) >= 0) return true;
    return false;
}

const EALib* Resources::findLib(std::string_view libName) const {
    for (const auto& lib : libs_)
        if (iequals(lib.name(), libName)) return &lib;
    return nullptr;
}

} // namespace st
