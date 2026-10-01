// Port only: the Enhanced preset's "Impact effects" (Settings::impactFx).
// Where a bullet hits, the original draws one of the "impc" puff frames in
// its own colours; with the option on the puff is drawn through a colour
// remap of the surface that was hit (blood, sparks, wood chips, stone dust,
// leaves, water) and a few particles fly out of the impact point.
//
// Remap tables are built from the current base palette at run time, one per
// surface: every source index is replaced by the palette entry nearest to
// the surface's tint of the source's luminance, so the puffs keep their
// shading whatever the time-of-day palette. Dust keeps the original colours.
//
// The particles and the visual-only puffs (a human hit whose one-in-three
// roll failed: the original shows nothing, and starting its puff would
// change the projectile's lifetime, so the port draws one that touches no
// game state) are small pools in world space advanced with the mission's
// frame ticks (pause and time compression behave) and drawn by the
// renderer's post-draw hook at layer resolution. Their random numbers come
// from a private generator, never from the game RNG, so the simulation is
// the same with the option on or off. Nothing here is ever used by the
// Original preset.
#pragma once

#include "core/common.h"
#include "game/types.h"

namespace st::render {

// Colour remap of a surface for Gfx::setSpriteRemap; nullptr for None and
// Dust (the original colours). Rebuilt whenever the base palette changes.
const u8* impactRemap(game::ImpactSurface s);

// A bright particle colour of the surface (k selects one of a few shades).
u8 impactParticleColor(game::ImpactSurface s, int k);

// Spawn the particle burst of an impact at `pos` (24.8 world units); `time`
// seeds the private generator. The pool holds 64, the oldest are replaced.
void impactParticlesSpawn(const game::Vec3& pos, game::ImpactSurface s, s32 time);
// Show a visual-only puff: "impc" frame `frame` (0 ground, 1 unit, 2
// obstacle, 4 craft) through the surface remap, growing from `time` like
// spr_draw_explosion's impact (1000:6DD5). Pool of 16, oldest replaced.
void impactPuffAdd(const game::Vec3& pos, int frame, game::ImpactSurface s, s32 time);

// Advance the particles by `ticks` game ticks; `time` is the game clock the
// puffs grow with.
void impactFxUpdate(int ticks, s32 time);
// Draw the puffs and particles into the frame being rendered (post-draw hook).
void impactFxDraw();
// Drop everything (mission start / end).
void impactFxReset();
// Number of live particles (dev viewer).
int impactParticleCount();

const char* impactSurfaceName(game::ImpactSurface s);

}  // namespace st::render
