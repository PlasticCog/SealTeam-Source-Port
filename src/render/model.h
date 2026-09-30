// Renderer-internal model data (segment 2255 section 6).
//
// The primitive records, BSP trees and LOD model headers are interpreted in
// place from a private, mutable copy of st.exe's DGROUP (the "DS image"),
// exactly as the original walks them with DS offsets: the model hooks and
// the time-of-day code write colour words, part angles and BSP leaf flags
// into that memory, and the facing test caches its result in primitive
// byte +5. Vertex coordinates come from the .pnt files; mdl_compile's
// generated x86 code is replaced by the equivalent signed-digit tables
// (compiled once per LOD model) evaluated by xfTransform().
#pragma once

#include "core/common.h"
#include "game/types.h"
#include "render/r3dmath.h"

#include <vector>

namespace st::render {

// ---------------------------------------------------------------- DS image

u8* dsImage();  // 64 KB copy of DGROUP (valid after modelsLoad)
inline u8 ds8(u16 off) { return dsImage()[off]; }
inline u16 ds16(u16 off) { return u16(dsImage()[off] | (dsImage()[u16(off + 1)] << 8)); }
inline s16 dsS16(u16 off) { return s16(ds16(off)); }
inline void dsW8(u16 off, u8 v) { dsImage()[off] = v; }
inline void dsW16(u16 off, u16 v) {
    dsImage()[off] = u8(v);
    dsImage()[u16(off + 1)] = u8(v >> 8);
}
// Word of the original far memory seg:off (Ghidra segment): DGROUP reads the
// live DS image, anything else st.exe's initial image (used to reproduce
// out-of-range table reads).
u16 farWord(u16 seg, u16 off);

// ---------------------------------------------------------------- models

// Far code pointers stored in the model data, as load-relative seg:off.
enum class Hook : u8 { None, Uh1Rotor, Mike2, Lssc, Ripples, Rippleb, Tunnel, Cobra, Pit, Unknown };

struct CompiledVertex {
    u8 parent = 0xFF;          // 0xFF: relative to the object centre
    s8 digit[3][15]{};         // signed digits of dx, dy, dz (rows 0..14)
    s16 delta[3]{};            // dx, dy, dz themselves (high-resolution path)
};

struct ModelPart {             // articulated sub-object (BSP leaf with flag 4)
    u16 angles = 0;            // DS offset of s16 a0, a1, a2
    u8 first = 0, last = 0;    // vertex range
};

struct LodModel {
    u16 ds = 0;                // DS offset of the LOD model record
    int np = 0, nv = 0;
    Hook hook = Hook::None;
    u16 root = 0;              // BSP root (DS offset)
    int nFacing = 0;           // leading type-0 records (facing caches reset per object)
    u8 flags = 0;              // bit 0 custom draw, bit 1 compiled, bit 2 7-bit table
    bool compiled = false;
    bool wide = false;         // PNT flag bit 3: 32-bit transform
    int window = 0;            // W of the signed-digit decomposition
    std::vector<CompiledVertex> verts;
    std::vector<ModelPart> parts;  // tree order (mdl_find_subobjects)
    int bodyLast = -1;             // body = vertices 0..bodyLast
};

struct ModelInfo {
    u16 ds = 0;                // descriptor DS offset
    int tableIndex = -1;
    game::ModelDesc desc{};
    LodModel lod[3];
    int lodCount = 0;
};

const ModelInfo* modelInfo(const game::ModelDesc* desc);
const LodModel* lodModelAt(u16 ds);  // LOD model by its DS offset

// mdl_time_of_day_colors (2255:000A): pit and tunnel colours for the hour.
void modelsSetTimeOfDay(int hour);

// ---------------------------------------------------------------- transform

// Camera-space vertex slot (DS:CF8A + 16 i): s32 x, y, z (the 16-bit code
// writes only the high words), projected sx/sy (0x7FFE = not projected).
struct VertexSlot {
    s32 x = 0, y = 0, z = 0;
    s16 sx = 0x7FFE, sy = 0x7FFE;
};
constexpr int kVertexSlots = 0x80;
VertexSlot* vertexSlots();

// Transform table state (xf_build_tables / xf_tables_current).
struct XformState {
    Mat3 cam;                        // F452 camera matrix (y row aspect scaled)
    s16 camAngles[3]{};              // F43E..F442
    Mat3 obj;                        // F62C object matrix
    s16 objAngles[3]{};              // F626..F62A angles of the object being drawn
    s16 objMatAngles[3]{0, 0, 0};    // 54FC..5500 angles F62C was built for
    s32 table[48][3]{};              // T_X rows 0..15, T_Y 16..31, T_Z 32..47 (x, y, z)
    s16 key[6]{-1, -1, -1, -1, -1, -1};  // 5502..550C
    s32 centre[3]{};                 // DS:5B98 object centre (high words = rec x, y, z)
    int K = 0;                       // F61A scale shift
};
XformState& xform();
void xfInvalidate();  // forget the current tables (e.g. new camera)

// Run the transform of `lod` for the current object (the generated routine
// of mdl_compile): centre and K from xform(), results into vertexSlots().
void xfTransform(const LodModel& lod);

}  // namespace st::render
