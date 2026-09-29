// Read-only view of the user's original st.exe load image.
//
// Many of the game's text strings and data tables (weapon stats, object
// definitions, UI coordinates...) live inside the executable rather than in
// the .lib archives. The port does not ship copies of them; it reads them from
// the user's own st.exe at runtime through this class.
//
// Addresses use the Ghidra convention from docs/re/CONVENTIONS.md: the image is
// based at segment 0x1000 and DGROUP is segment 0x56bf.
#pragma once

#include "core/common.h"

#include <string>
#include <vector>

namespace st {

class ExeImage {
public:
    static constexpr u16 kLoadSeg = 0x1000;
    static constexpr u16 kDGroup = 0x56bf;

    bool load(const std::string& dosName);

    // Pointer into the image for a Ghidra seg:off address (nullptr if outside).
    const u8* at(u16 seg, u16 off) const;
    const u8* dg(u16 off) const { return at(kDGroup, off); }

    u8 dgByte(u16 off) const;
    u16 dgWord(u16 off) const;
    s16 dgShort(u16 off) const { return s16(dgWord(off)); }
    u32 dgDword(u16 off) const;
    // NUL-terminated string at DS:off (empty if out of range).
    std::string dgString(u16 off) const;
    // DS-relative near pointer stored at DS:off, dereferenced to a string.
    std::string dgStringPtr(u16 off) const { return dgString(dgWord(off)); }

    // Sanity check that this is the build the port was written against.
    bool looksLikeSealTeam() const;

private:
    std::vector<u8> image_;  // load module, header stripped
};

ExeImage& exe();

} // namespace st
