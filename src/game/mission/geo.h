// Fixed-point geometry used by the simulation: the engine math of segment
// 2255 (docs/re/seg_2255.md 5.1), the polar move 4e87:0006 and the position
// helpers of segment 1000 (1000:31FD..3C84). All results are bit exact with
// the original, including its rounding and table quirks.
//
// Angles: "a8" = 1/8 degree (0..2879, 2880 = 0xB40 per turn); "deg" = whole
// degrees. Positions: Vec3 24.8 fixed point (map units = value >> 8).
#pragma once

#include "game/types.h"

namespace st::game::mission {

constexpr int kFullTurn8 = 0xB40;  // 360 degrees in 1/8 degree

// 2255:613F / 618C: table sine/cosine, value range -16383..16383 (1.14).
// Input must be 0..2879 like the original.
int mathSin(int a8);
int mathCos(int a8);
// 2255:61E7 / 61F6: the same times 4 as a 16.16 value.
s32 mathSinFx(int a8);
s32 mathCosFx(int a8);
// 2255:6205: wrap into 0..2879.
int angleWrap(int a8);
// 2255:4481: table arctangent; 0 along +a, 720 along +b.
int mathAtan2(int a, int b);
// 2255:63D7: heading (a8) from (x0,z0) to (x1,z1); 0 = +z, 720 = -x.
int mathHeading(s32 x0, s32 z0, s32 x1, s32 z1);
// 2255:64FD: pitch (a8) from a to b; 0 level, 720 straight up, 2880-t down.
int mathPitch(const Vec3& a, const Vec3& b);
// 2255:6368: square root of a 32-bit value, 16.16 result.
s32 mathSqrtLong(s32 v);
// 2255:62D0: rotate (x, y) about (cx, cy) by a8, 16-bit arithmetic with
// round-half-up of the 1.14 products.
void mathRotate2d(s16& x, s16& y, s16 cx, s16 cy, int a8);

// 4e87:0006 pos_move_polar: move pos by d (24.8 units) at pitch/heading (a8).
// Nothing happens for d <= 0; d > 0x4000 is computed on d >> 8.
void posMovePolar(s32 d, int pitch8, int heading8, Vec3& pos);

// 1000:321F: ground distance in map units, (max + 3*min/8) >> 8 of |dx|,|dz|;
// values that do not fit a positive int16 give 0x7FFF.
int geoDistance(const Vec3& a, const Vec3& b);
// 1000:3300: bearing in whole degrees 0..359 from a to b (0 for the same object).
int geoBearing(const Vec3& a, const Vec3& b);
// 1000:334B: pitch in whole degrees from a to b.
int geoPitch(const Vec3& a, const Vec3& b);
// 1000:3392: ((180 - a8/8) mod 360 + b8/8) mod 360.
int geoRelativeBearing(int a8, int b8);
// 1000:3C84: bearing a->b within +-half degrees (|half| capped at 180) of heading deg.
bool geoInFov(const Vec3* a, int halfDeg, int headingDeg, const Vec3* b);
// 1000:37FF: out = a + (b - a) / 2 per axis (arithmetic shift); returns the
// distance from a to b.
int geoMidpoint(const Vec3& a, const Vec3& b, Vec3& out);
// 1000:389C: out.x = pos.x + (d << 8), out.z = pos.z + (d << 8) (y untouched).
void geoOffsetXz(const Vec3& pos, int d, Vec3& out);

// Helpers for 32-bit wrap-around arithmetic like the 8086 code.
inline s32 wrapAdd(s32 a, s32 b) { return s32(u32(a) + u32(b)); }
inline s32 wrapSub(s32 a, s32 b) { return s32(u32(a) - u32(b)); }

} // namespace st::game::mission
