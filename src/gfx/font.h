// DeluxeFont (.fnt) bitmap fonts: 4x6.fnt, memo.fnt, prop.fnt, propbold.fnt.
// Format in docs/re/seg_libs.md section 8. Fonts are loose files in the game
// directory, not archive entries.
#pragma once

#include "core/common.h"

#include <string>
#include <string_view>
#include <vector>

namespace st {

class Font {
public:
    bool load(const std::string& dosName);
    bool loaded() const { return height_ > 0; }

    int height() const { return height_; }
    // Width of glyph c in pixels, including its built-in spacing column.
    int glyphWidth(u8 c) const { return loc_[c + 1] - loc_[c]; }
    int textWidth(std::string_view s) const;
    // Whether pixel (x, y) of glyph c is set.
    bool pixel(u8 c, int x, int y) const;

    // Header words whose use is not yet known (likely line spacing and
    // nominal/space width), kept for when the callers are ported.
    int field20() const { return field20_; }
    int field26() const { return field26_; }

private:
    int stride_ = 0;
    int height_ = 0;
    int field20_ = 0, field26_ = 0;
    std::vector<u16> loc_ = std::vector<u16>(257, 0);
    std::vector<u8> bitmap_;
};

} // namespace st
