// High-resolution frame of the Enhanced preset (renderer internal).
//
// The camera-space pipeline of r3d.cpp is shared with the Original preset;
// this class supplies what differs in the Enhanced preset: double-precision
// projection into the page's high-resolution layer (platform/video: a fixed
// multiple of 320x200, or the window's own pixels), a floating-point vertex
// transform, thick lines and points, the sky/ground split in layer pixels,
// and the set-up of the gfx render target and the layer coverage.
//
// Geometry: page pixel p covers layer pixels ox + p*kx .. ox + (p+1)*kx - 1
// (kx, ky layer pixels per page pixel, independent because the page's
// pixels are not square on a 4:3 screen). The projection keeps the original
// zoom (the world is not stretched): a wide window shows more of it when
// the viewport is extended to the window border (Settings::wideView), in
// which case the clip used for culling and clipping is the wider "virtual"
// page rectangle cv*.
#pragma once

#include "core/common.h"
#include "gfx/gfx.h"
#include "platform/video.h"

namespace st::render {

struct LodModel;

struct HiFrame {
    bool on = false;
    int vx = 0, vy = 0, vw = 320, vh = 200;  // viewport in page pixels (the camera rect)
    int X0 = 0, Y0 = 0, X1 = 319, Y1 = 199;  // viewport in layer pixels (inclusive, extended)
    int cvx = 0, cvy = 0, cvw = 320, cvh = 200;  // virtual page clip covering the layer viewport
    double kx = 1, ky = 1;                   // layer pixels per page pixel
    double ox = 0, oy = 0;                   // layer position of page pixel (0, 0)
    int cx = 0, cy = 0;                      // projection centre in layer pixels (rounded)
    double cxd = 0, cyd = 0;
    int clipCx = 159, clipCy = 99;           // the original projection centre (page pixels)
    int thick = 1;                           // line / point thickness
    int zoom = 8;
    s32 camX = 0, camY = 0, camZ = 0;
    int page = 0;
    HiResLayer* layer = nullptr;
    Bitmap bmp;

    // Prepare a frame (Enhanced preset; clipCx/Cy: the original projection
    // centre of the viewport). `on` stays false in the Original preset.
    void begin(int rx, int ry, int rw, int rh, int clipCx, int clipCy, int zoomShift, s32 cx32, s32 cy32,
               s32 cz32);
    void beginDraw();  // gfx draws into the layer from here
    void end();        // back to the page; the viewport shows the layer
    void project(s32 x, s32 y, s32 z, s16& sx, s16& sy) const;
    void line(int x0, int y0, int x1, int y1, u16 c) const;
    void point(int x, int y, u16 c) const;
    void transform(const LodModel& L) const;
    void skyGround(const s32 P[3], u8 ground, u8 sky, s16 roll, s16 pitch) const;

    // Layer <-> page coordinates (continuous, pixel-centre convention).
    double toLayerX(double px) const { return ox + (px + 0.5) * kx - 0.5; }
    double toLayerY(double py) const { return oy + (py + 0.5) * ky - 0.5; }
    double toPageX(double X) const { return (X - ox + 0.5) / kx - 0.5; }
    double toPageY(double Y) const { return (Y - oy + 0.5) / ky - 0.5; }
};

// Make the draw page's layer available for 2D drawing of the 3D background
// (the sky gradient of 1000:1AFC). Returns nullptr in the Original preset.
HiResLayer* hiResLayerForDrawPage();

// Point gfx at the layer with the clip set to the layer rectangle of the
// current page clip (extended to the window border in wide view); returns
// false (nothing changed) without a layer. Undo with layerTargetEnd().
bool layerTargetBegin(HiResLayer& l, Bitmap& bmp);
void layerTargetEnd();

}  // namespace st::render
