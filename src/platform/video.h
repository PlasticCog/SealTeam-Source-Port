// VGA emulation.
//
// The original runs in "mode X": BIOS mode 13h reprogrammed to unchained,
// four-plane 320x200 with 256 KB of video memory and page flipping through the
// CRTC start address. Planar memory maps exactly onto a linear buffer where
//     pixel index = planeOffset * 4 + plane
// so VRAM is kept as 256 KB of linear pixels. A page that starts at planar
// offset S begins at linear pixel S*4, and a latched 4-plane copy moves four
// consecutive pixels. Chained mode 13h is the same buffer with start 0.
#pragma once

#include "core/common.h"

#include <array>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace st {

constexpr int kScreenW = 320;
constexpr int kScreenH = 200;
constexpr int kPageSize = kScreenW * kScreenH;     // 64000 pixels
constexpr int kVramSize = 256 * 1024;              // 4 planes x 64 KB

struct VideoConfig {
    int scale = 3;               // window scale factor
    bool fullscreen = false;
    bool aspectCorrect = true;   // show 320x200 as 4:3 like a CRT did
};

class Video {
public:
    bool init(const VideoConfig& cfg);
    void shutdown();

    u8* vram() { return vram_.data(); }
    u8* page(int n) { return vram_.data() + n * kPageSize; }

    // Linear pixel index of the displayed page (CRTC start address * 4).
    void setDisplayStart(u32 linearPixel) { displayStart_ = linearPixel; dirty_ = true; }
    u32 displayStart() const { return displayStart_; }

    // DAC access, 6-bit components like the hardware (0..63).
    void setPalette(const u8* rgb6, int first, int count);
    void getPalette(u8* rgb6, int first, int count) const;
    const u8* palette() const { return dac_.data(); }

    void markDirty() { dirty_ = true; }
    // Copy the displayed page to the window if anything changed.
    void present(bool force = false);

    void toggleFullscreen();

private:
    std::array<u8, kVramSize> vram_{};
    std::array<u8, 768> dac_{};
    u32 displayStart_ = 0;
    bool dirty_ = true;
    bool fullscreen_ = false;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
};

} // namespace st
