// EALIB archives (*.lib) and their LZSS compression. Format notes in
// docs/formats.md.
#pragma once

#include "core/common.h"

#include <string>
#include <string_view>
#include <vector>

namespace st {

// Okumura-style LZSS as used by the game: 4096-byte ring pre-filled with
// spaces, writing from 0xFEE, LSB-first flag bits (1 = literal), references are
// 12-bit ring position + 4-bit (length - 3). Decodes exactly outLen bytes.
bool lzssDecode(const u8* src, size_t srcLen, u8* out, size_t outLen);

class EALib {
public:
    enum Method : u8 { Stored = 0, Lzss = 1, Picture = 3 };

    struct Entry {
        std::string name;   // as stored (8.3, original case)
        u8 method = 0;
        u32 offset = 0;
        u32 size = 0;       // stored (compressed) size
    };

    bool open(const std::string& dosName);
    const std::string& name() const { return name_; }
    const std::vector<Entry>& entries() const { return entries_; }

    int find(std::string_view entryName) const;  // case-insensitive, -1 if missing

    // Decoded entry data. Picture entries keep their 20-byte PXPK header
    // followed by the unpacked pixels.
    bool read(int index, std::vector<u8>& out) const;
    bool read(std::string_view entryName, std::vector<u8>& out) const;

private:
    std::string name_;
    std::vector<u8> data_;
    std::vector<Entry> entries_;
};

// All archives the game opens at start-up, searched in order.
class Resources {
public:
    bool openLib(const std::string& dosName);
    bool read(std::string_view entryName, std::vector<u8>& out) const;
    bool exists(std::string_view entryName) const;
    const EALib* findLib(std::string_view libName) const;

private:
    std::vector<EALib> libs_;
};

Resources& resources();

} // namespace st
