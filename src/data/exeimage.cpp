#include "data/exeimage.h"

#include "data/gamefs.h"

#include <cstring>

namespace st {

ExeImage& exe() {
    static ExeImage instance;
    return instance;
}

bool ExeImage::load(const std::string& dosName) {
    std::vector<u8> file;
    if (!gameFS().readFile(dosName, file)) return false;
    if (file.size() < 0x1c || file[0] != 'M' || file[1] != 'Z') {
        logError("%s: not an MZ executable", dosName.c_str());
        return false;
    }
    const u32 lastPage = rd16(&file[2]);
    const u32 pages = rd16(&file[4]);
    const u32 headerBytes = u32(rd16(&file[8])) * 16;
    u32 imageEnd = pages * 512 - (lastPage ? 512 - lastPage : 0);
    if (imageEnd > file.size()) imageEnd = u32(file.size());
    if (headerBytes >= imageEnd) return false;
    image_.assign(file.begin() + headerBytes, file.begin() + imageEnd);
    // DGROUP's _BSS and stack follow the load module; pad so near addresses
    // anywhere in the 64 KB segment are addressable (they read as zero).
    const size_t dgEnd = size_t(kDGroup - kLoadSeg) * 16 + 0x10000;
    if (image_.size() < dgEnd) image_.resize(dgEnd, 0);
    return true;
}

const u8* ExeImage::at(u16 seg, u16 off) const {
    if (seg < kLoadSeg) return nullptr;
    const size_t lin = size_t(seg - kLoadSeg) * 16 + off;
    return lin < image_.size() ? &image_[lin] : nullptr;
}

u8 ExeImage::dgByte(u16 off) const {
    const u8* p = dg(off);
    return p ? *p : 0;
}

u16 ExeImage::dgWord(u16 off) const {
    const u8* p = dg(off);
    return p ? rd16(p) : 0;
}

u32 ExeImage::dgDword(u16 off) const {
    const u8* p = dg(off);
    return p ? rd32(p) : 0;
}

std::string ExeImage::dgString(u16 off) const {
    const u8* p = dg(off);
    if (!p) return {};
    const size_t maxLen = image_.size() - size_t(p - image_.data());
    return std::string(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), maxLen));
}

bool ExeImage::looksLikeSealTeam() const {
    // The Microsoft C runtime banner sits at DS:0008 and the game's own
    // version string is in DGROUP; both must be where this port expects them.
    return dgString(0x0008).rfind("MS Run-Time Library", 0) == 0 &&
           dgString(0x1360) == "SEAL Team  V1.0";
}

} // namespace st
