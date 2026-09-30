// High-resolution frame of the Enhanced preset (renderer internal).
//
// The camera-space pipeline of r3d.cpp is shared with the Original preset;
// this class supplies what differs at render scale N > 1: 64-bit projection
// into (320N)x(200N) layer pixels, a floating-point vertex transform, thick
// lines and points, the sky/ground split in layer pixels, and the set-up of
// the gfx render target and the layer coverage.
#pragma once

#include "core/common.h"
#include "gfx/gfx.h"
#include "platform/video.h"

namespace st::render {

struct LodModel;

struct HiFrame {
    bool on = false;
    int N = 1;
    int vx = 0, vy = 0, vw = 320, vh = 200;  // viewport in page pixels
    int cx = 0, cy = 0;                      // projection centre in layer pixels (rounded)
    double cxd = 0, cyd = 0;
    int zoom = 8;
    s32 camX = 0, camY = 0, camZ = 0;
    int page = 0;
    HiResLayer* layer = nullptr;
    Bitmap bmp;

    // Prepare a frame of scale n (n <= 1: off). clipCx/Cy: the original
    // projection centre of the viewport.
    void begin(int n, int rx, int ry, int rw, int rh, int clipCx, int clipCy, int zoomShift, s32 cx32, s32 cy32,
               s32 cz32);
    void beginDraw();  // gfx draws into the layer from here
    void end();        // back to the page; the viewport shows the layer
    void project(s32 x, s32 y, s32 z, s16& sx, s16& sy) const;
    void line(int x0, int y0, int x1, int y1, u16 c) const;
    void point(int x, int y, u16 c) const;
    void transform(const LodModel& L) const;
    void skyGround(const s32 P[3], u8 ground, u8 sky, s16 roll, s16 pitch) const;
};

// Make the draw page's layer available for 2D drawing of the 3D background
// (the sky gradient of 1000:1AFC). Returns nullptr in the Original preset.
HiResLayer* hiResLayerForDrawPage();

}  // namespace st::render
