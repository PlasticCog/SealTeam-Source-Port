// The 3D renderer: port of r3d_render_view (2255:3090) and everything it
// uses (docs/re/seg_2255.md 7-9). Draws the world built with render/world.h
// into the current gfx draw page (Original preset) or into the page's
// high-resolution layer (Enhanced preset).
#pragma once

#include "core/common.h"
#include "game/types.h"

namespace st::render {

// ---------------------------------------------------------------- view record

// view_init (2255:3F6F) with the game's parameters (world_begin): object
// list of 200, subsample 2, projection cache on (25 frames, 16 angle units).
void viewInit();
// view_free (2255:400B): forget the object list and projection caches.
void viewFree();

// ---------------------------------------------------------------- game state

// Game state read by the renderer, the model hooks and the billboard
// callback. The mission code keeps it up to date (all fields default to
// harmless values so a bare world viewer works).
struct RenderContext {
    int frameTicks = 1;             // g_frame_ticks (DS:ECAA): UH-1 rotor speed
    s32 time = 0;                   // g_time (DS:ECA6)
    int todHour = 12;               // g_tod_hour (DS:EC58): pit colour
    game::Vec3 reinsertPoint{};     // DS:0EA0: Mike boat part angle (bearing to it)
    int viewMode = 2;               // g_view_mode (DS:D844): billboards hidden in map modes 1/0xC
    int detail = 5;                 // g_detail (DS:02BE) 0..5
    // tgt_update_reticle (1000:65D3), called by the renderer after drawing:
    // if the body is in the draw list its centre is projected into reticle.
    const game::Obj3D* reticleTarget = nullptr;
    s16 reticleX = 0, reticleY = 0; // DS:0534 / DS:0536 (kept when not found)
    // Camera position used by the billboard code for view bearings (g_cur_camera DS:D8AE).
    game::Vec3 cameraPos{};
};
RenderContext& renderContext();

// ---------------------------------------------------------------- rendering

enum RenderStatus : int {
    kRenderOk = 0,
    kViewBufferTooSmall = 1,    // "View Buffer Too Small" (fatal in the game)
    kObjectListTooSmall = 2,    // "Object List Too Small" (fatal in the game)
};

// r3d_render_view (2255:3090) for the default world (DS:07A4) and view
// (DS:074A) with the game's 5000-byte work buffer. Sets the gfx clip to the
// viewport, draws sky/ground (world colours, see World::groundColor) and the
// objects. `wait` runs the frame limiter before the first pixel is drawn.
int renderView(const game::Camera& cam, bool wait = true);
int renderView(s32 x, s32 y, s32 z, int heading, int pitch, int roll, int rectX, int rectY, int rectW,
               int rectH, int zoom, bool wait = true);

// r3d_project (2255:4328): project a camera-space point (x, y, z as 32-bit
// values whose high words are the coordinates) with the current projection.
// In a high-resolution frame the result is in layer pixels.
void project(const s32 v[3], s16& sx, s16& sy);

// r3d_project with the normal (non-precise) routine in 320x200 page pixels,
// whatever the render scale: for 2D overlays drawn after the frame with the
// projection of the last renderView (the map markers of 19ac:2F47).
void projectPage(const s32 v[3], s16& sx, s16& sy);

// x of the projection centre (the clip centre) in the pixels of the frame
// being drawn (DS:F248).
int projectionCentreX();

// Layer pixels per page pixel of the frame being drawn (1 in the Original
// preset; the Enhanced layer scales x and y independently). frameScale() is
// the rounded-up larger one, for coarse limits. Valid during renderView.
int frameScale();
double frameScaleX();
double frameScaleY();

// State of the object being drawn, for primitive callbacks (type 4).
struct DrawState {
    const game::Obj3D* obj = nullptr;   // DS:F61C
    s16 x = 0, y = 0, z = 0;             // render record +6/+8/+A: camera-space centre
    int K = 0;                           // render record +5: scale shift
    u8 clip = 0;                         // render record +4
};
const DrawState& drawState();

// Primitive callback routine (far pointer in a type-4 record): the game has
// one, 348e:0C52 spr_draw_billboard_cb. Registered by render/sprites.
using PrimCallback = void (*)(u16 prim, int sx, int sy);
void setBillboardCallback(PrimCallback fn);

// Statistics of the last frame (for the dev viewer).
struct RenderStats {
    int tested = 0, drawn = 0, far = 0, out = 0, in = 0, clipped = 0, listed = 0;
};
const RenderStats& renderStats();

}  // namespace st::render
