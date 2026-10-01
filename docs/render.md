# The 3D world and renderer (`src/render/`)

Port of the author's 3D library in segment 2255 (models, object arena, world
grid, `r3d_render_view` with everything it calls), the drawing half of the
time-of-day code and the procedural scenery of segment 1000, and the
billboard sprite code of segment 348e. Reverse-engineering notes:
`docs/re/seg_2255.md` 5-10, `seg_1000.md` 5, 12, 15, 16, `seg_2dbd.md` 4-5.

All game data is read at run time: model table, descriptors, LOD models,
primitive records and BSP trees from `st.exe`'s DGROUP and far segments,
vertex data from `point.lib`, worlds from `worlds.lib`, sprites from the
unit libraries, gradients and remap tables from `main.lib`.

## Files

| File | Contents |
|---|---|
| `world.h/.cpp` | **Public world API**: model registry (`modelsLoad`, `modelByDs`, `modelTable`), object arena (`worldBegin/AddObject/End/Free`, arena offsets), world record and grid, `.w/.wd` loader (`wldLoad`, `worldObjects`, spare objects) |
| `model.h/.cpp` | DS image, LOD models, `.pnt` loading, `mdl_compile` replacement (signed-digit tables), transform tables, model hooks' data, time-of-day model colours |
| `r3dmath.h/.cpp` | Table sine/cosine/atan (from st.exe), sqrt, 16.16 mul/div, 1.14 matrices, `math_rotate_2d`, headings |
| `r3d.h/.cpp` | **Public renderer API** (`renderView`, `project`, `RenderContext`) and the port of `r3d_render_view` |
| `r3dhires.h/.cpp` | The Enhanced preset's high-resolution frame (any layer size, wide view, page-space geometry) |
| `sky.h/.cpp` | `tod_palette_index`, `tod_draw_sky_ground`, gradients, remap tables |
| `sprites.h/.cpp` | `spr_draw_billboard_cb` (348e:0C52): soldier frame selection and drawing, scenery billboards (jungle, bush, muzzle), sprite banks |
| `veg.h/.cpp` | `veg_*` ground cover, camera scatter, ambient flyer placement |
| `viewer.cpp` | Dev commands `--view-world`, `--view-model` |

`src/gfx` (render targets, coverage) and `src/platform/video` (high-resolution
layers of any size, composition, presentation, screenshots) were extended for
the Enhanced preset.

## Using it from the mission code

```
render::modelsLoad(); render::skyLoad(); render::spritesLoad();   // start-up
render::setUnitLookup(...); setEffectPainter(...); setAnimUpdate(...);
render::setPointMan(...); render::setBlockerProbe(...);            // hooks

render::worldBegin();                     // mis_load_world 1000:4580
render::wldLoad(world);                   // .w objects + 126 spares
... fx pools (hand the veg pools over with vegSetPools), units ...
render::worldEnd();                       // + setWorldHooks(los_init, los_free)
render::modelsSetTimeOfDay(hour);         // hud_reset_mission
pal = render::todPaletteIndex(hour, minute);

per frame (view_render_frame 1000:1F92):
    fill render::renderContext() (frame ticks, time, hour, view mode, detail,
        reinsertion point, reticle target)
    render::vegUpdate(camera, force)                 // veg_update
    gfx().setClip(camera rect); render::todDrawSkyGround(camera, detail, g_time, hour)
    r = render::renderView(camera)                   // 1 / 2 = the fatal "too small" errors
    reticle: renderContext().reticleX/Y
render::worldFree();                                 // also drops the high-res layers
```

Objects are `game::Obj3D` records; game code changes position, angles,
flags and model at any time, exactly as the original did. `objOffset()` gives
the original 16-bit arena offset (first object 2, 0x18-byte records) that the
game uses to derive indices.

## Faithfulness (Original preset)

The Original preset reproduces the integer arithmetic of the original:

* **Vertex transform**: the generated x86 code of `mdl_compile` is replaced by
  per-vertex signed-digit tables (`mdl_signed_digits` + the run recoding of
  A6C8, window W = floor(log2 max|coordinate|)) evaluated against the halved
  transform tables T[row + K] with 16-bit wrap-around (32-bit for PNT flag 8
  models, which keep the row-14 fraction bits; 16-bit models keep stale low
  words in the vertex slots). Articulated parts rebuild the tables only when
  their angles are non-zero, so a zero-angle part after a rotated part uses
  the rotated part's tables.
* **Model data in place**: primitives, BSP nodes, facing caches, colour words
  and part angles are read from a mutable copy of DGROUP, so the model hooks
  (UH-1/Cobra rotors, boats, ripples, tunnel, pit) and `mdl_time_of_day_colors`
  write the same bytes as the original. Out-of-range sine reads (the Cobra
  rotor angle grows without a modulo) read the same memory as the original
  would (DGROUP copy / st.exe image).
* **Gathering**: 1/N scan of the cell and dynamic lists (N = 2), persistent
  view list of 200, the 5000-byte work buffer accounting (status 1/2), world
  boxes rebuilt every 20 frames or on 15-degree turns and translated in 0x6600
  steps, cull/clip skip flags (one-frame time slicing), Manhattan LOD
  distance, sphere test against the power-of-two widened frustum.
* **Draw order**: layer then distance comparison, the sort-if-unordered
  quicksort (first-element pivot, recursion on the smaller part) and the list
  merge of 2E96.
* **Projection**: the generated routine's zoom shift (for zoom >= 6 the low
  byte is not cleared: `(y << 8) | (y & 0xFF)`), divide-overflow fix-ups of
  the INT 0 handler (0x7F00, and 10000 / centre for z = 0), the precise
  48-bit division (7047/6F24) for large objects with the camera inside their
  radius, the projection cache (25 frames, 2 degrees) with its odd
  "outside the radius" test.
* **Clipping**: vertical planes on the ring with the plane shift, the top/
  bottom order test that reads ring[1].x instead of y, rounded 16-bit
  division intersections with the degenerate-edge rules, bisection
  callbacks in precise mode (only for edges crossing z = 0 on top/bottom),
  and **the left-plane clipper called twice instead of the right plane**
  (8BD4). Lines clip only against top/bottom (`clip_line_frustum`,
  `clip_line_bisect`).
* **Sky/ground**: the rolled horizon via the pitch/roll matrix, the 23:20
  x stretch, the band rows of `r3d_horizon_rows`, byte-granular row fills.
* **Facing**: normal from halved coordinates on overflow, normalisation by
  4-bit steps then `>> 2` (the note in seg_2255.md said 1), sign of n . P1.
* Aspect ratio: `r3d_set_aspect(20, 23)` gives 0xDE9B (seg_2255.md quoted
  0xDEAA); radius_y and the matrix y rows use it.

* **Polygon filler** (`gfx_fill_polygon` 2255:10FC, in `src/gfx`): chain start
  rules for flat tops, the top clip by integer bisection of the crossing edge
  (140E, the edge restarts from the cut point), per-edge set-up with 32-bit
  halving and IDIV, the pre-step of right-moving right edges that tests
  2r - 2dy instead of the error term, the sign taken from the low word of dx,
  span joining with the previous (joined) span, the unjoined last row.

Verified against the original running in DOSBox (missions 3, 21, 61: huts,
palms, water, mountains, sky gradients at dawn/day/night, ground cover). For a
mission 3 capture the camera was recovered by searching positions against the
captured frame (chase camera behind the Point Man, sky offset +1 for the dawn
brightening tick): every static object, the sky and the ground are pixel-
identical; the remaining differences (about 1200 of 44000 pixels) are the
dynamic objects only: the randomly scattered vegetation, the soldiers and HUD
marks, grass tufts. The exact polygon filler removed the last systematic edge
differences (palm trunks crossing the top of the viewport).

## Enhanced preset

`settings().effectiveRenderScale()` (0 = native, N = a fixed multiple of
320x200), `effectiveWideView()`, `effectiveFullScreen3d()` and
`effectiveDrawDistancePct()` P (`kDrawDistanceMax` = the whole world). The
Enhanced preset always renders through the high-resolution frame of
`r3dhires`, also at 320x200.

**The layer** — `platform/video` keeps a high-resolution layer per VGA page
(`HiResLayer`): `w x h` palette indices, a 320x200 coverage mask and the
placement of the page inside the layer. At a fixed scale N the layer is
320N x 200N and the page fills it; at native resolution the layer has the
window's pixel size (queried every frame, so resizing and Alt+Enter just
reallocate it) and the page occupies the centred 4:3 rectangle (8:5 with
square pixels), so a layer pixel is a screen pixel. Page pixel (x, y) covers
layer columns `colStart[x] .. colStart[x+1]-1` and rows `rowStart[y] ..`;
the scales `kx = pw/320`, `ky = ph/200` differ (4:3 pixels are not square)
and need not be integers. `--window WxH` starts with a given window size;
the viewer's `size WxH` renders native frames at a size other than the
window's (a 4K frame on a smaller monitor).

`renderView` activates the draw page's layer, points `gfx` at it (render
target) with the clip set to the viewport's layer rectangle, and draws the
same camera-space pipeline with:

* projection in double precision around the original projection centre:
  `sx = ox + (cx + 0.5) kx - 0.5 + kx * x * 2^zoom / z`, `sy` likewise with
  `ky` (original pixel p covers layer pixels `ox + p kx ..`; the original
  zoom is kept, so the world is never stretched, and the non-square 4:3
  pixel geometry of the page is reproduced by `kx != ky`);
* a floating-point vertex transform (object centre recomputed without the
  16-bit truncation, parts with their own matrices);
* lines `round(min(kx, ky))` pixels thick, points as squares of that size,
  discs/sprites scaled by the projection, sprites' 8.8 scale factors in
  layer pixels (`frameScaleX/Y()`);
* the horizon geometry of `r3d_draw_sky_ground` computed in page pixels and
  rasterised per layer row; the time-of-day gradient rows are drawn into
  both the page and the layer (across the widened view).

**Wide view** (`wideView`, native resolution only) — a viewport that touches
the page border is extended to the window border on that side
(`HiResLayer::rectOf`): the mission's main view (0, 8, 320, 171) fills a
16:9 window's full width between the page rows 8 and 178, the map view (x 8,
w 204) stays inside the page. The frustum normals, world boxes, sphere test
and clip-plane shifts then use the "virtual" page rectangle that covers the
extended viewport (`HiFrame::cvx/cvy/cvw/cvh`, e.g. 427 x 171 for 16:9)
while the projection centre stays the camera rect's, so the field of view
widens horizontally (Hor+) and nothing moves. The 2D page (HUD, text,
cursor, map, menus) is always shown in its 4:3 rectangle, so HUD overlays
land where they do in the original. With `4:3` the sides stay black.

**Full-screen 3D** (`fullScreen3d`, cfg `full_screen_3d`, Setup "Full-screen
3D", native resolution only, default off) — the original's field cameras
leave black bands for the HUD: the main camera rect is (0, 8, 320, 171), the
chase / support / insertion camera rect (0, 24, 320, 139) (seg_1000.md 7.1),
and the HUD text lines, the message line, the banners and the clock box are
drawn at fixed rows in the bands. With the option on, the mission loop sets
`RenderContext::fullScreen3d` for the field views (point man, chase, support
and target views, insertion / extraction; never for the map, the briefing /
debriefing fly-overs, the cut-scenes or any 2D screen) and `HiFrame::begin`
makes the layer viewport that of the whole page, rows 0..199, with the
sides as in wide view (`rectOf(0, 0, 319, 199)`: the whole window with
`Fill`, the 4:3 page area with `4:3`). The camera rect keeps the projection
centre (row 93 for both cameras) and the zoom, so the world is not stretched
and nothing moves; the virtual page clip `cv*` is made symmetric around the
centre (the frustum, the world boxes and the clip shifts work with half
widths / heights: 213 rows for the main camera, 8 above and 21 below it
being the bands) and the vertical field of view grows like the horizontal
one does in wide view. `todDrawSkyGround` draws its gradient rows across
the whole layer height (200 rows either side of the horizon; the tables
saturate) while the page keeps the clipped rows of the original; the
point-sampled page copy and the coverage of `HiFrame::end` then cover the
whole page, which hides the black band fills (`gx.clear(0)` at a full
redraw) without touching them (`todDrawSkyGround` widens the page clip to
the page height around its `layerTargetBegin`, whose layer clip is derived
from the page clip, i.e. the camera rect).

The HUD stays where it is and is drawn on top: every 2D write uncovers the
page pixels it touches (transparent text now uncovers only the set pixels
of a glyph, not its box), so what the original drew on black now lies on
the scene. To keep it readable, `hud.cpp` gives every element one
treatment: a dark backing strip (the glyph rows plus the shadow row, one
page pixel to either side, so the strips of consecutive rows join without
overlapping), darkened into the layer by `render::hudBackingRect` (the nearest palette colour of 45 % of
each pixel's colour, the table rebuilt when the DAC changes; the page
pixels under the strip are re-covered first), and 4x6 text a 1-pixel black
drop shadow (the shadow is drawn through `Gfx::draw4x6String` because
`text4x6` aligns x to 4). This covers the text lines (weapon, grenade,
item, reloading, DEMO, detail level), the message line and the hand-signal
lines (the icon has its own opaque frame), the compass tape (one strip x
0x40..0xFF over the label row, the tape and the heading mark, the
objective marker's rows adjacent below it; in the chase view, without a
tape, only the marker arrow itself is backed), the target diamond's range and
name labels, the "Esc to skip - R to Reinsert" line and the clock box of
the time compression (`hudDrawClockBox` replaces its black box by the strip;
since time compression redraws the clock without rendering, the strip is
darkened once per rendered frame, `HiResLayer::hudOnce`, and re-covering
the box wipes the previous digits; `mapDrawClock` gets the shadow through
its `shadow` argument). Proportional-font messages use `drawTextShadow`.
The "Insertion" / "Extraction" title tabs are opaque and only need to be
redrawn after every render (the original draws them once at a full redraw
into the band, which the full-height view now overdraws). Screenshots
(`--shot`, F12) save the full-height frame like any composed frame. In the
Original preset and at a fixed render scale nothing changes
(`effectiveFullScreen3d()` is false).

Verified with `--enhanced --window 1920x1080 / 3840x2160 --play-mission 1`
in the insertion view, the chase view, the first-person view with the
weapon lines and with a hand signal plus message (`--keys
Enter@mission+1,F1@mission+2,d@mission+3.5`), against the same frames with
the option off (`re/scratch_full/`), and the Original preset's
`--view-world 3` and `--play-mission 1` frames bit-identical to the
previous build's.

After the 3D view the viewport's coverage is set, the rows' extension flags
(`extRows`) and the extended rectangle (`ext*`) are recorded, and a
point-sampled copy of the layer is written into the page (so page copies and
blits see the 3D view). Every 2D write through `gfx` (spans, pixels, lines,
text, sprites, images, blits) clears the coverage of the pixels it touches,
so HUD text, cursor and messages appear on top; a write across the full page
width also clears the row's extension (a 2D screen has replaced the view
there). `copyPage` carries the layer along; page-to-page blits at the same
position copy the layer rectangle, any other blit uncovers the destination.
`Video::present` composes the displayed page into an ARGB texture of the
layer size, row by row on a few threads: covered page pixels take the
layer, others the (nearest-neighbour upscaled) page, the area outside the
page rectangle the layer inside the extension or black (display page and
screen-shake offset respected). A native layer is shown pixel for pixel, a
fixed-scale layer and the plain page are stretched over the page rectangle
(SDL's logical presentation is not used; mouse positions are mapped through
`Video::pageArea()`). Screenshots (`--shot`, F12) save the composed frame at
the layer's size, i.e. the full window at native resolution. In the Original
preset no layer exists and nothing changes. `worldFree()` drops the layers.

**Impact effects** (`impactFx`, cfg `impact_fx`, Setup "Impact effects",
default on; `render/impactfx.h`) — the original marks where a bullet hits
with one "impc" puff frame in fixed colours (frame by `Projectile::hit`:
0 ground, 1 unit, 2 obstacle, 4 craft; `spr_draw_explosion` 1000:6DD5)
and shows a puff on only one human hit in three. With the option on, the
ordnance code (`prjUpdate`) classifies the surface of every impact site
and stores it in the port-only `Projectile::impact_surface`
(`game::ImpactSurface`): the ground is Water if `wld_probe_kind` finds a
water / shallow / deep-water object under the point, else Dust; a human
is Blood, a craft Metal; a solid world object by its terrain kind and
model name — vegetation, trees and brush Foliage, the "rock" prop Stone
and the other props Wood, the stone structures (bldgston, well, church,
pagoda) Stone, the bunker Dust and the other structures Wood, a dock Wood,
a helicopter pad Stone, anything else Dust. The original's puffs are then
drawn through a colour remap of the surface: a table per surface is built at run time from the current base
palette (rebuilt when its 768 bytes change), mapping every source index by
its luminance Y to the palette entry nearest to the surface's tint (blood
0.9Y+8 / 0.12Y / 0.12Y, sparks 1.3Y+16 / 1.15Y+10 / 0.5Y, wood 0.75Y+6 /
0.5Y+3 / 0.25Y, stone Y / Y / Y, foliage 0.35Y / 0.8Y+4 / 0.3Y, water
0.55Y+8 / 0.75Y+10 / 1.1Y+16, 6-bit RGB, nearest by squared distance over
all 256 entries); Dust keeps the original colours. The frame choice is the
original's.

A human hit shows a puff every time instead of one time in three. The
original's roll (`rng_range(3)`, repeated every frame while the stopped
round keeps probing its victim) is left exactly as it is, because
`prj_start_impact` also resets the round's lifetime and timer (which
decides when its slot is free again) and plays a sound: the headless
`--sim-mission` logs diverged when the puff was started on the first hit
frame. Every human hit therefore gets a *visual-only* puff, whatever the
roll: the unit frame through the Blood remap, kept in a pool of 16 by
`render/impactfx` and drawn by the post-draw hook with
`spr_draw_explosion`'s growth rule (8.8 scale `e = 4 * elapsed`, at least
0x40, shown while `e < 3 * (billboard scale / 4)`, the billboard scale
being the projected width of 0x60 model units of the burst shape at the
puff's depth). It is placed at the victim's torso for his posture
(`torsoHeight`: 9 / 5 / 2 world units standing / crouching / prone, between
the game's eye heights {15, 9, 3} and muzzle heights {12, 7, 0}) rather
than at the round's altitude, which sits above a prone man's head; the
original's own puff, when its roll succeeds, still starts (sound, lifetime)
but is not drawn for a Blood surface (`drawExplosion`). Nothing of the
projectile changes (port-only `Projectile::impact_shown` keeps the effect
to one per round).

Every impact that starts, and every visual-only puff, also spawns 6-10 particles at the impact point
(world space, an upward and outward velocity, gravity, 0x60-0xA0 ticks of
life, a pool of 64 whose oldest entries are replaced), drawn from a private
linear congruential generator seeded with the impact time, never from the
game RNG. The mission loop advances them with its frame ticks before every
render (`viewRenderFrame`, so the pause and time compression behave) and
draws them through the renderer's post-draw hook (`setPostDrawHook`,
called at the end of `renderView` after the objects while the frame's
render target and clip are active, only in a high-resolution frame) as
filled squares of about `frameScaleX() / 2` layer pixels (at least 1) in
one of three shades of the surface (white, yellow and grey through the
remap; a sandy tone for dust), projected with `projectWorld` (the camera
matrix in double precision, no depth test). The hook is set for the field
views only (never for the map, and the briefing, debriefing and cut-scene
views of the front end never see it). In the Original preset and with the
option off (`effectiveImpactFx()`) nothing changes: no classification, the
original puff colours and the one-in-three rule. The simulation is the
same either way: `--sim-mission 1 --ticks 30000 --script walk` logs are
identical with `--original` and `--enhanced`.

`--impact-test [hour H]` draws the four non-empty "impc" frames at 3x
through every surface remap with the mission palette of the hour into a
960x600 layer (labels on the page), and `b` in `--view-world` fires a fake
impact of each surface in turn 40 units ahead of the camera every half
second (a visual-only puff and particles; `--keys b@1` scripts it).
Verified with `re/scratch_impact/` captures: the day (hour 12) and night
(hour 2) grids, puffs and particles mid-flight in `--view-world 1 enhanced
native max fill size 1920x1080`, the Original preset's `--view-world 3`
frame and the Enhanced `--view-world 1` frame bit-identical to the
previous build's, identical `--sim-mission` logs, and `--enhanced
--window 1920x1080 --play-mission 1` (the preset flag goes before the dev
command) running normally with and without the option.

**Draw distance** — the original renderer is 16-bit in three places that
limit how far it can see: the LOD thresholds (`u16`, units of 65536 world
units), the world-box size class (the view-pyramid boxes reach depth
`2^(c+8)` world units), and `r3d_obj_to_camera`, whose magnitude test
`m = |x| | |y| | |z| + radius + 1` and scale shift K (0..13) wrap for an
object farther than 2^15 model units and cannot represent one whose
camera-space centre needs more than 14 bits. The Enhanced preset widens
all of them (Original keeps the exact 16-bit behaviour):

* LOD thresholds scaled by P/100 in 32 bits; at Max every threshold is
  infinite (the distance `D = Manhattan >> 16` never exceeds ~300 in a
  24000-unit world).
* The size class is raised by `ceil(log2(P/100))` (1600 % = +4); at Max
  every object passes the world-box test (class >= 20).
* `objToCameraWide`: the magnitude is computed in 64 bits and
  `K = 13 - floor(log2 m)` may be **negative** (camera units of 2^-K model
  units), so the rotated centre always fits 14 bits; `cullSphereWide`
  does the frustum test in 64 bits. A negative K is honoured by the
  double-precision transform, the disc radius, the sprite depth (the width
  is scaled instead) and the render record (`K` is `s8`). The work buffer
  and view list are 64x the original's (12300 / 12800 records).

The remaining limits are the original's polygon pipeline: clipped vertices
are rounded to whole camera units (2^-K model units, sub-pixel at those
distances) and projected coordinates are clamped to +-32000 layer pixels.
The flat "horizon" of the time-of-day gradient is the middle row of the
viewport whatever the pitch (original design), so with a long draw distance
a camera pitched down sees distant objects above the gradient's ground rows;
the game's field cameras look level.

Frame times of `--bench-view 3 enhanced native max fill size ...` (mission 3,
Tay Khanh village, the camera making a full turn, 150-200 objects drawn) on
a 2024 desktop CPU, RelWithDebInfo: 3D render 2 ms at 4K (1 ms at 1080p);
composition + palette conversion 2 ms at 4K (8 threads); the 33 MB texture
upload and present 1-10 ms depending on the GPU driver's state; 6-14 ms per
frame at 4K, 6-7 ms at 1080p, i.e. within the game's own 5-tick (19.5 ms)
frame limiter. The `--view-world` viewer at 640x400 / 200 % takes 2 ms.

**Differences from the original in Enhanced only** (clear bugs):

| Original | Enhanced |
|---|---|
| clip bit 3 (right plane) clips against the left plane again (8BD4) | clipped against the right plane x <= z |
| Cobra rotor angle grows without modulo and indexes outside the sine table (0470) | wrapped to 0..2879 |
| projection cache (static far objects keep their projected shape up to 25 frames / 2 degrees) | disabled (exact projection every frame) |
| a zero-angle articulated part after a rotated part uses the rotated part's tables | uses the object matrix |
| zoom shift keeps the low byte, 16-bit vertex precision | double-precision transform/projection |
| 16-bit distances, LOD and world-box culling | 32/64-bit, draw distance up to the whole world |

## Billboards (348e)

`spr_draw_billboard_cb` is the only type-4 routine; it gets the unit through
`setUnitLookup` (19ac:622C in the game), computes the scale from the
projected width of 0x60 model units at the object's depth, the view rotation
(`geo_relative_bearing`), the colour remap by anim kind (kind 7 uses the sky
gradient as in the original), the size scaling, and selects set/frame exactly
as 348e (timed animations by state, running/standing/crouch/prone walk
cycles, surrendered sets). Body, headgear (Point Man beret, VC conical, NVA
pith, SEAL camouflage hats), the ground-line clip, sinking and the grass tuft
(which draws from the game RNG, as the original) are ported. Without a unit
the scenery branch draws Jungle.RLE (detail >= 4, never enlarged, x snapped
to 8) and Bush.RLE (detail >= 1); effect objects of projectiles are passed to
the mission's `setEffectPainter` hook; lone muzzle objects draw fxmu with
remap4. `spr_update_anim` belongs to the mission code and is called through
`setAnimUpdate`.

Port only: `setUnitMark` is the mission code's "draw this unit marked"
predicate (the snatch target of the Enhanced "Modern gameplay" option,
docs/mission.md). For a unit it accepts, `drawSoldierFrame` draws the
headgear through a red remap of the current base palette (built like the
impact remaps of `impactfx.cpp`: luminance -> red, nearest entry, 0 and 255
pinned) and `drawMarkBand` draws the body frame once more through the same
remap with the clip box reduced (`setClipTop` / `setClipBottom`) to a band
of rows at the bottom edge of the headgear frame, two sprite pixels scaled
with the sprite and at least one page pixel, so only the body's own pixels
change colour and the silhouette is the original's. Without the hook, or
with it false (the option off, every other unit), the draw calls are
exactly the original's.

## Procedural scenery (veg)

Ground cover from the 126 spare world objects on the 0x31/0x62-cell pattern
of DS:3560 around the Point Man (256-unit grid, 128-unit match, random type
cascade of 1000:3AC1), camera scatter from the 80-object pool on the 49
offsets of DS:348C rotated by the camera heading snapped to 90 degrees, and
ambient flyer placement (4 offsets of DS:3550) — all with the game RNG in the
original order. `wld_probe_kind(pos, 5)` is a hook (`setBlockerProbe`) for the
line-of-sight code; the flyer type choice uses mission state
(`grp_nearest_enemy_dist`, Point Man exhaustion) that is not available here,
so the bird branch is only reached through the mission code (open issue).

## Dev commands

```
sealteam --view-world <1..80> [x y z heading pitch] [enhanced [native|N [dist%|max]]]
                     [fill|4:3] [size WxH] [detail D] [hour H] [chase]
sealteam --view-model <0..96> [stand|walk|run|crouch|crawl|prone|dead] [enhanced [native|N]]
sealteam --bench-view <1..80> [enhanced [native|N [dist%|max]]] [fill|4:3] [size WxH] [frames N]
```
Mission n's world is loaded from `cYmNN.mci` (world index, start time,
insertion point); the camera starts at the insertion point, 24 units up,
facing the primary objective. Keys: arrows turn/move, PgUp/PgDn height,
Home/End pitch, +/- speed, 1..6 detail, `e` toggles Original/Enhanced, `h`
HUD, `r` rotates the model, Esc quits. Add `--shot FILE --shot-after S` for a
screenshot (the composed frame at the layer's size in Enhanced). `size WxH`
renders native frames at that size whatever the window (e.g.
`--view-world 3 enhanced native max fill size 3840x2160`). `--bench-view`
turns the camera once around over `frames` frames without the frame limiter
and prints the average and worst render, compose and present times.

## Open issues

* Pixel comparison covers static scenery of one mission-3 frame (see above);
  dynamic objects depend on game state/RNG and were not compared. Remaining
  candidates for subtle differences: the INT 0 behaviour on IDIV quotient
  -32768 (the polygon filler's IDIV can fault on degenerate edges wider than
  0x8000; the port truncates), garbage low words in 16-bit vertex slots
  (emulated) and the out-of-table sine reads (emulated from the initial
  image).
* Group objects (flag 0x20), `r3d_temp_add` slots and the persistent view mode
  are unused by the game and not ported (documented in 2255 §7/8).
* Multi-cell grids: the game always builds a 1x1 grid; a larger grid lists
  every other cell as visible (conservative, never exercised).
* `veg_setup_distant_tree`'s bird branch needs mission state; the flyers'
  movement is the mission's `evt_update_ambient_flyer`.
* Enhanced: sprite and line thickness choices are cosmetic; dither patterns
  are applied at layer resolution (finer, same coverage). The mouse cursor's
  save-under restore is a 2D blit, so on the map screen it leaves a
  page-resolution patch in the 3D map where the cursor was.
* Enhanced, wide view: the extension beyond the 4:3 page is a whole-row
  affair (a row keeps its extension until a full-width 2D write): a 2D
  element narrower than the page cannot uncover the extension next to it.
* Enhanced, Max distance: everything in the world is gathered every frame
  (about 400-500 objects on mission 3); the cost is in the composition and
  texture upload of a 4K frame, not in the 3D pipeline.
