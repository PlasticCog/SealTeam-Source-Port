// Integer math of the 3D library (segment 2255 section 5, docs/re/seg_2255.md):
// table sine/cosine and arctangent read from st.exe, 1.14 matrices, 16.16
// fixed point, square root, rotations. Bit-exact ports of the original
// routines; the renderer, the camera code and the sprite code share them.
#pragma once

#include "core/common.h"

namespace st::render {

constexpr int kAngleFull = 2880;  // 360 degrees in 1/8 degree units (0xB40)

// Tables from st.exe (far 5327:000A sine 0..90 degrees, 52E7:0008 arctangent).
bool mathInit();

s16 mathSin(int a);   // 613F: a in 0..2879, 1.14 result (max 16383)
s16 mathCos(int a);   // 618C
int angleWrap(int a); // 6205: into 0..2879

// 4481: angle of (a, b) in 1/8 degree, 0 along +a, 720 along +b.
int mathAtan2(int a, int b);
// 63D7: heading from (x0,z0) to (x1,z1) (32-bit world units); 0 = +z, 720 = -x.
int mathHeading(s32 x0, s32 z0, s32 x1, s32 z1);
// 64FD: pitch from point a to point b (0..720 up, 2880-t down).
int mathPitch(s32 ax, s32 ay, s32 az, s32 bx, s32 by, s32 bz);

// 6368: square root of a non-negative 32-bit value as 16.16.
s32 mathSqrtLong(s32 v);
s32 mathFmul(s32 a, s32 b);  // 65B9: signed 16.16 product
s32 mathFdiv(s32 a, s32 b);  // 661B: signed 16.16 quotient (original overflow behaviour)

// 1.14 matrices, row major: m[3*i + j].
struct Mat3 {
    s16 m[9]{};
};
// 3726: R = Ry(a0) Rx(a1) Rz(a2) (object heading, pitch, roll).
Mat3 mathRotMatrix(int a0, int a1, int a2);
// 9704: b := a x b.
void mathMatMul(Mat3& b, const Mat3& a);
// 98A7: v := M x v on the high words of three longs; result (sum << 2) per
// component (high word = sum >> 14, low word = 2 extra fraction bits).
void mathMatVec(s32 v[3], const Mat3& m);
// 62D0: rotate (x, y) about (cx, cy) by angle a with rounded 1.14 products.
void mathRotate2d(s16& x, s16& y, s16 cx, s16 cy, int a);

// Helpers for 16-bit register arithmetic.
inline s16 hi16(s32 v) { return s16(u32(v) >> 16); }
inline u16 lo16(s32 v) { return u16(u32(v)); }
inline s32 mk32(s16 hi, u16 lo) { return s32((u32(u16(hi)) << 16) | lo); }
// (u * v) as the high word of the product shifted left twice (bits 14..29).
inline s16 mul14(s16 u, s16 v) { return hi16(s32(u32(s32(u) * s32(v)) << 2)); }

}  // namespace st::render
