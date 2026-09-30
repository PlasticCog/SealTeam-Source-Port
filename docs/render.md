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
| `r3dhires.h/.cpp` | The Enhanced preset's high-resolution frame |
| `sky.h/.cpp` | `tod_palette_index`, `tod_draw_sky_ground`, gradients, remap tables |
| `sprites.h/.cpp` | `spr_draw_billboard_cb` (348e:0C52): soldier frame selection and drawing, scenery billboards (jungle, bush, muzzle), sprite banks |
| `veg.h/.cpp` | `veg_*` ground cover, camera scatter, ambient flyer placement |
| `viewer.cpp` | Dev commands `--view-world`, `--view-model` |

`src/gfx` (render targets, coverage) and `src/platform/video` (high-resolution
layers, composition, screenshots) were extended for the Enhanced preset.

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

`settings().effectiveRenderScale()` N and `effectiveDrawDistancePct()` P.

**Render scale (N > 1)** — `platform/video` keeps a high-resolution layer per
VGA page: (320N)x(200N) palette indices plus a 320x200 coverage mask.
`renderView` activates the draw page's layer, points `gfx` at it (render
target) with the viewport scaled by N, and draws the same camera-space
pipeline with:

* projection in double precision: `sx = N*cx + (N-1)/2 + N * x * 2^zoom / z`
  (original pixel p covers layer pixels pN..pN+N-1);
* a floating-point vertex transform (object centre recomputed without the
  16-bit truncation, parts with their own matrices);
* lines N pixels thick, points as NxN squares, discs/sprites scaled by the
  projection, sprites' 8.8 scale factors in layer pixels;
* sky/ground split computed per layer row; the time-of-day gradient is drawn
  into both the page and the layer.

After the 3D view the viewport's coverage is set and a point-sampled copy of
the layer is written into the page (so page copies and blits see the 3D
view). Every 2D write through `gfx` (spans, pixels, lines, text, sprites,
images, blits) clears the coverage of the pixels it touches, so HUD text,
cursor and messages appear on top. Page-to-page blits and `copyPage` carry the
layer along. `Video::present` composes the displayed page: covered pixels from
the layer, others upscaled from the page (display page and screen-shake
offset respected); screenshots save the composed frame. With N = 1 or the
Original preset no layer exists and nothing changes. `worldFree()` drops the
layers.

**Draw distance (P > 100)** — LOD thresholds are scaled by P/100 (capped at
0xFFFF), the world-box size class of every object is raised by 1 (P > 100) or
2 (P >= 400) so the view-pyramid boxes reach 2x/4x deeper, and the work
buffer and view list are made 64x larger so gathering never fails.

**Differences from the original in Enhanced only** (clear bugs):

| Original | Enhanced |
|---|---|
| clip bit 3 (right plane) clips against the left plane again (8BD4) | clipped against the right plane x <= z |
| Cobra rotor angle grows without modulo and indexes outside the sine table (0470) | wrapped to 0..2879 |
| projection cache (static far objects keep their projected shape up to 25 frames / 2 degrees) | disabled (exact projection every frame) |
| a zero-angle articulated part after a rotated part uses the rotated part's tables | uses the object matrix |
| zoom shift keeps the low byte, 16-bit vertex precision | double-precision transform/projection |

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
sealteam --view-world <1..80> [x y z heading pitch] [enhanced [N [dist%]]] [detail D] [hour H]
sealteam --view-model <0..96> [stand|walk|run|crouch|crawl|prone|dead] [enhanced [N]]
```
Mission n's world is loaded from `cYmNN.mci` (world index, start time,
insertion point); the camera starts at the insertion point, 24 units up,
facing the primary objective. Keys: arrows turn/move, PgUp/PgDn height,
Home/End pitch, +/- speed, 1..6 detail, `e` toggles Original/Enhanced, `h`
HUD, `r` rotates the model, Esc quits. Add `--shot FILE --shot-after S` for a
screenshot (the composed high-resolution frame in Enhanced).

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
  are applied at layer resolution (finer, same coverage).
