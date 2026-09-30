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
// rendered into `pixels` (w x h palette indices) instead of the page.
//
// The 320x200 page is placed inside the layer: page pixel (x, y) covers
// layer columns colStart[x] .. colStart[x+1]-1 and rows rowStart[y] ..
// rowStart[y+1]-1 (the page area `ox, oy, pw, ph`). With a fixed render
// scale N the layer is 320N x 200N and the page fills it; at native
// resolution the layer is the window's pixel size and the page area is the
// 4:3 (or 8:5) rectangle centred in it, so a layer pixel is a screen pixel.
//
// `coverage` marks the page pixels that show the layer instead of the page
// (the 3D renderer sets it for its viewport, every 2D write through gfx
// clears it). Layer pixels outside the page area (a wide window) show the
// layer inside the extension rectangle `ext*` (the 3D viewport widened to
// the window border) while `extRows` of the page row is set; a 2D write
// across the full page width clears the row (a 2D screen replacing the view).
struct HiResLayer {
    bool active = false;
    int scale = 0;                 // 0 = native (window size), N = 320N x 200N
    int w = 0, h = 0;              // layer size
    int ox = 0, oy = 0, pw = 0, ph = 0;  // page area inside the layer
    bool fill = false;             // 3D viewports touching the page border extend to the layer border
    std::vector<int> colStart;     // 321 entries: first layer column of page column x
    std::vector<int> rowStart;     // 201 entries
    std::vector<s16> colPage;      // w entries: page column of a layer column (-1 outside)
    std::vector<s16> rowPage;      // h entries
    std::vector<u8> pixels;        // w*h palette indices
    std::vector<u8> coverage;      // 320*200, 1 = use the layer
    int extX0 = 0, extY0 = 0, extX1 = -1, extY1 = -1;  // inclusive, empty if x1 < x0
    std::vector<u8> extRows;       // 200
    // Full-screen 3D: HUD backings already darkened into this frame of the
    // layer (bit 0: the clock box), cleared by every 3D render. Elements that
    // are redrawn without a new frame (time compression) darken only once.
    u8 hudOnce = 0;

    double scaleX() const { return pw / double(kScreenW); }  // layer pixels per page pixel
    double scaleY() const { return ph / double(kScreenH); }
    bool sameGeometry(const HiResLayer& o) const {
        return w == o.w && h == o.h && ox == o.ox && oy == o.oy && pw == o.pw && ph == o.ph;
    }
    // Layer rectangle (inclusive) of the page rectangle x0..x1 / y0..y1; with
    // `extend` (and `fill`) sides on the page border reach the layer border.
    void rectOf(int x0, int y0, int x1, int y1, bool extend, int& X0, int& Y0, int& X1, int& Y1) const;
    // Page column / row of a layer position (clamped to the page).
    int pageX(int X) const;
    int pageY(int Y) const;
};

struct VideoConfig {
    int scale = 3;               // window scale factor
    int width = 0, height = 0;   // explicit window size instead of the scale (0 = use the scale)
    bool fullscreen = false;
    bool aspectCorrect = true;   // show 320x200 as 4:3 like a CRT did
    bool smooth = false;         // linear filtering of the 320x200 page (never of a layer)
};

struct PixelRect {
    int x = 0, y = 0, w = 0, h = 0;
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
    // While set, pump() does not present on its own: the game shows a new
    // frame exactly once per page flip (setDisplayStart), like the CRTC.
    void setExplicitPresent(bool on) { explicitPresent_ = on; }
    bool explicitPresent() const { return explicitPresent_; }
    // Copy the displayed page to the window if anything changed.
    void present(bool force = false);

    void toggleFullscreen();
    void setWindowSize(int w, int h);

    // Confine the (hidden) OS cursor to the window and deliver raw motion,
    // like the DOS mouse driver the game re-centres every frame. Released
    // when the window loses focus, re-acquired on the next click.
    void captureMouse(bool on);
    bool mouseCaptured() const { return mouseCaptured_; }

    // Size of the window in pixels (the renderer's output), and the rectangle
    // of it that shows the 320x200 page (4:3 or 8:5, centred): mouse
    // positions are mapped through it.
    void outputSize(int& w, int& h) const;
    PixelRect pageArea() const;
    // Testing aid: render native layers at this size instead of the window's
    // (a 4K frame on a smaller monitor); 0 = off.
    void setOutputSizeOverride(int w, int h) { overrideW_ = w; overrideH_ = h; }

    // --- high-resolution layers (Enhanced preset), one per page (0 = linear
    // 0x00000, 1 = linear 0x10000). No layer = zero overhead, the output is
    // exactly the 320x200 page.
    HiResLayer* hiResLayer(int page);                    // nullptr if inactive
    // (Re)allocate the layer of a page: scale 0 = native (the current
    // output size, page area centred), N = 320N x 200N. Coverage is cleared
    // when the geometry changes. `fill` widens viewports to the window.
    HiResLayer& activateHiResLayer(int page, int scale, bool fill);
    void copyHiResLayer(int src, int dst);               // with gfx page copies
    void dropHiResLayers();

    // Save what the window shows (the composed high-resolution frame when a
    // layer is displayed) as an 8-bit BMP.
    bool saveScreenshot(const std::string& path);

    SDL_Renderer* renderer() const { return renderer_; }

    // Time spent by present() in its phases since the last reset (profiling).
    struct PresentStats {
        int frames = 0;
        double lock = 0, lockMax = 0;        // SDL_LockTexture (may wait for the GPU)
        double compose = 0, composeMax = 0;  // composition + palette conversion into the texture
        double upload = 0, uploadMax = 0;    // texture unlock (upload) + render + present
    };
    const PresentStats& presentStats() const { return stats_; }
    void resetPresentStats() { stats_ = PresentStats{}; }

private:
    const HiResLayer* displayedLayer() const;  // layer of the displayed page, if active
    // Palette indices of row Y of the frame the CRT would show (layer size).
    void composeRow(const HiResLayer& l, int Y, u8* out) const;
    void composeIndexed(const HiResLayer& l, u8* out) const;
    PixelRect pageAreaIn(int w, int h) const;

    std::array<u8, kVramSize> vram_{};
    HiResLayer layers_[2];
    int texW_ = 0, texH_ = 0;
    std::array<u8, 768> dac_{};
    u32 displayStart_ = 0;
    u32 displayOffset_ = 0;
    bool dirty_ = true;
    bool fullscreen_ = false;
    bool aspectCorrect_ = true;
    bool smooth_ = false;
    bool mouseCaptured_ = false;
    bool explicitPresent_ = false;
    int overrideW_ = 0, overrideH_ = 0;
    PresentStats stats_;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
};

} // namespace st
