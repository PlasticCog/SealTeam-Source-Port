#include "gfx/font.h"

#include "data/gamefs.h"

#include <cstring>

namespace st {

namespace {
constexpr size_t kLocOffset = 0x2a;
constexpr size_t kBitmapOffset = 0x22c;  // 0x2a + 257 * 2
} // namespace

bool Font::load(const std::string& dosName) {
    std::vector<u8> d;
    if (!gameFS().readFile(dosName, d)) return false;
    if (d.size() < kBitmapOffset || std::memcmp(d.data(), "[DeluxeFont]", 12) != 0 || d[0x0d] != 2) {
        logError("%s: not a DeluxeFont v2 file", dosName.c_str());
        return false;
    }
    field20_ = rd16(&d[0x20]);
    stride_ = rd16(&d[0x22]);
    height_ = rd16(&d[0x24]);
    field26_ = rd16(&d[0x26]);
    for (int i = 0; i < 257; ++i) loc_[size_t(i)] = rd16(&d[kLocOffset + size_t(i) * 2]);
    const size_t bytes = size_t(stride_) * size_t(height_);
    if (d.size() < kBitmapOffset + bytes) {
        height_ = 0;
        return false;
    }
    bitmap_.assign(d.begin() + kBitmapOffset, d.begin() + long(kBitmapOffset + bytes));
    return true;
}

int Font::textWidth(std::string_view s) const {
    int w = 0;
    for (char ch : s) w += glyphWidth(u8(ch));
    return w;
}

bool Font::pixel(u8 c, int x, int y) const {
    const int col = loc_[c] + x;
    if (x < 0 || x >= glyphWidth(c) || y < 0 || y >= height_) return false;
    const u8 byte = bitmap_[size_t(y) * size_t(stride_) + size_t(col >> 3)];
    return (byte >> (7 - (col & 7))) & 1;
}

} // namespace st
