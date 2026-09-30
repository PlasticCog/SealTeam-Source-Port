// Time of day as far as drawing is concerned (segment 1000, docs/re/seg_1000.md
// 5): palette choice, sky/ground gradients (gradsky1/gradgrn1.bin) and the
// colour remap tables for sprites (remap2..5.bin) loaded by 1959:0265.
#pragma once

#include "core/common.h"
#include "game/types.h"

#include <string>

namespace st::render {

// Load gradsky1.bin, gradgrn1.bin and remap2..5.bin (sys_load_resources).
bool skyLoad();

// Sprite colour remap tables: index 0..3 = remap2..remap5 (DS:00EA..00F9).
const u8* remapTable(int i);
const u8* gradSkyTable();     // gradsky1.bin (DS:00FA)
const u8* gradGroundTable();  // gradgrn1.bin (DS:00FE)

// Gradient state (DS:0530 sky offset, DS:0532 ground offset, DS:EC4A dawn timer).
struct SkyState {
    int skyOfs = 0x80;
    int groundOfs = 0x80;
    s32 dawnTime = 0;
};
SkyState& skyState();

// tod_palette_index (1000:1A88): 0 pal2 (day), 1 paln (night), 2 pals
// (dawn/dusk); sets the gradient offsets for the hour and minute.
int todPaletteIndex(int hour, int minute);
// Palette file name for tod_palette_index's result ("pal2.pal"...), from st.exe.
std::string todPaletteName(int index);

// tod_draw_sky_ground (1000:1AFC): with the clip set to the view rect `cam`,
// fill the view with the sky and ground gradients (detail >= 2) or record
// the flat colours in the world record for r3d (detail < 2), then brighten
// the dawn gradient every 0xF00 ticks. `time` = g_time, `hour` = tod hour.
void todDrawSkyGround(const game::Camera& cam, int detail, s32 time, int hour);

// view_clear_map_ground (1000:1C8A): fill the view rect with gradgrn1[0xB6].
void viewClearMapGround(const game::Camera& cam);

}  // namespace st::render
