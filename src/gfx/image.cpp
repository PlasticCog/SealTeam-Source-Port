#include "gfx/image.h"

#include "data/ealib.h"
#include "platform/video.h"

#include <algorithm>
#include <cstring>

namespace st {

bool decodePxpk(const std::vector<u8>& data, Image& out) {
    if (data.size() < 20 || std::memcmp(data.data(), "PXPK", 4) != 0) return false;
    const int w = rd16(&data[6]);
    const int strideWords = rd16(&data[8]);
    const int h = rd16(&data[10]);
    const u32 len = rd32(&data[16]);
    const int stride = strideWords * 2;
    if (w <= 0 || h <= 0 || stride < w || data.size() < 20 + size_t(len) || size_t(stride) * h > len) return false;
    out.w = w;
    out.h = h;
    out.pixels.resize(size_t(w) * h);
    for (int y = 0; y < h; ++y) std::memcpy(out.row(y), &data[20 + size_t(y) * stride], size_t(w));
    return true;
}

bool loadPicture(std::string_view name, Image& out) {
    std::vector<u8> data;
    if (!resources().read(name, data)) {
        logWarn("picture %.*s not found", int(name.size()), name.data());
        return false;
    }
    if (!decodePxpk(data, out)) {
        logWarn("picture %.*s: bad PXPK data", int(name.size()), name.data());
        return false;
    }
    return true;
}

bool loadPalette(std::string_view name, Palette& out) {
    std::vector<u8> data;
    if (!resources().read(name, data) || data.size() < out.size()) {
        logWarn("palette %.*s not found", int(name.size()), name.data());
        return false;
    }
    std::copy_n(data.begin(), out.size(), out.begin());
    return true;
}

void blitToPage(u8* page, const Image& img, int x, int y) {
    const int x0 = std::max(0, x), x1 = std::min(kScreenW, x + img.w);
    const int y0 = std::max(0, y), y1 = std::min(kScreenH, y + img.h);
    if (x0 >= x1) return;
    for (int yy = y0; yy < y1; ++yy)
        std::memcpy(page + yy * kScreenW + x0, img.row(yy - y) + (x0 - x), size_t(x1 - x0));
}

} // namespace st
