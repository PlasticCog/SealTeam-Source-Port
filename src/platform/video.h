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
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace st {

constexpr int kScreenW = 320;
constexpr int kScreenH = 200;
constexpr int kPageSize = kScreenW * kScreenH;     // 64000 pixels
constexpr int kVramSize = 256 * 1024;              // 4 planes x 64 KB

// High-resolution layer of a VGA page (Enhanced preset): the 3D view is
// rendered at `scale` x the page resolution into `pixels`; `coverage` marks
// the 320x200 page pixels that show the layer instead of the page (the 3D
// renderer sets it for its viewport, every 2D write through gfx clears it).
struct HiResLayer {
    int scale = 0;             // 0 = inactive
    int w = 0, h = 0;          // 320*scale, 200*scale
    std::vector<u8> pixels;    // w*h palette indices
    std::vector<u8> coverage;  // 320*200, 1 = use the layer
};

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
    // Extra start offset (screen shake writes CRTC start-low on top of the page start).
    void setDisplayOffset(u32 linearPixels) {
        if (displayOffset_ != linearPixels) dirty_ = true;
        displayOffset_ = linearPixels;
    }
    u32 scanoutStart() const { return displayStart_ + displayOffset_; }

    // DAC access, 6-bit components like the hardware (0..63).
    void setPalette(const u8* rgb6, int first, int count);
    void getPalette(u8* rgb6, int first, int count) const;
    const u8* palette() const { return dac_.data(); }

    void markDirty() { dirty_ = true; }
    // Copy the displayed page to the window if anything changed.
    void present(bool force = false);

    void toggleFullscreen();

    // --- high-resolution layers (Enhanced preset), one per page (0 = linear
    // 0x00000, 1 = linear 0x10000). No layer = zero overhead, the output is
    // exactly the 320x200 page.
    HiResLayer* hiResLayer(int page);                    // nullptr if inactive
    HiResLayer& activateHiResLayer(int page, int scale); // (re)allocate, coverage cleared
    void copyHiResLayer(int src, int dst);               // with gfx page copies
    void dropHiResLayers();

    // Save what the window shows (the composed high-resolution frame when a
    // layer is displayed) as an 8-bit BMP.
    bool saveScreenshot(const std::string& path);

    SDL_Renderer* renderer() const { return renderer_; }

private:
    int composeScale() const;  // scale of the displayed page's layer, 1 if none
    void compose(u8* out, int scale) const;  // indexed pixels of the shown frame

    std::array<u8, kVramSize> vram_{};
    HiResLayer layers_[2];
    int textureScale_ = 1;
    std::array<u8, 768> dac_{};
    u32 displayStart_ = 0;
    u32 displayOffset_ = 0;
    bool dirty_ = true;
    bool fullscreen_ = false;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
};

} // namespace st
