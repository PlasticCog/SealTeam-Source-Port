// 8-bit indexed images and the game's picture/palette formats.
#pragma once

#include "core/common.h"

#include <array>
#include <string_view>
#include <vector>

namespace st {

struct Image {
    int w = 0, h = 0;
    std::vector<u8> pixels;  // w*h, row-major

    u8* row(int y) { return pixels.data() + size_t(y) * size_t(w); }
    const u8* row(int y) const { return pixels.data() + size_t(y) * size_t(w); }
};

// 256-entry VGA palette with 6-bit components (the .PAL file layout).
using Palette = std::array<u8, 768>;

// PXPK picture: 'PXPK', u16 depth (0x100 = 256 colours), u16 width,
// u16 row stride in 16-bit words, u16 height, 6 reserved bytes, u32 pixel byte
// count, then LZSS data (already unpacked by EALib::read).
bool decodePxpk(const std::vector<u8>& data, Image& out);

bool loadPicture(std::string_view name, Image& out);   // from the open archives
bool loadPalette(std::string_view name, Palette& out); // 768-byte .PAL entry

// Copy an image into a 320-wide VGA page at (x, y), clipped to 320x200.
void blitToPage(u8* page, const Image& img, int x, int y);

} // namespace st
