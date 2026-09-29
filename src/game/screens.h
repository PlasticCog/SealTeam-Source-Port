// Shared screen plumbing used by every front-end screen: palette slots
// (1959), pictures (1000:1CCB), frame present (1000:1E87), screen transition
// (365e:3180), fonts (365e:921E) and text helpers (365e:93BC..9505).
#pragma once

#include "core/common.h"
#include "gfx/font.h"
#include "gfx/image.h"

#include <string>
#include <string_view>
#include <vector>

namespace st::game {

// Palette numbers (names read from st.exe, DS:0102).
enum Pal { PalMission = 0, PalN = 1, PalS = 2, PalTitle = 3, PalLogo = 4, PalMenu = 5, PalNew = 6,
           PalL = 7, PalIntel = 8, PalB = 9, PalOrders = 10, PalCamp0 = 11 };

// Picture numbers for picLoad (names read from st.exe).
enum Pic { PicMap = 0, PicTitle = 1, PicBriefing = 2, PicOrders = 3, PicMainMenu = 4, PicIntel = 5,
           PicDebriefing = 7, PicLoad = 9, PicNew = 10, PicCamp0 = 14, PicLogo = 20 };

void palLoad(int n);                  // pal_load: becomes the current palette
void palApply();                      // pal_apply: upload it (respecting the fade level)
bool picLoad(int n);                  // pic_load into the single picture buffer
const Image& picture();
void picBlitToScreen();               // pic_blit_to_screen (full picture rectangle)
void present();                       // gfx_present: full clip, flip + 5-tick wait, reset limiter
void screenTransition(int pal, bool wait);  // ui_screen_transition

// Fonts: memo (clipboard), propbold (title), prop (dialog) and the 4x6 font.
void loadFonts();
enum class FontId { Clipboard, Title, Dialog };
void fontSelect(FontId f);
const Font& font4x6();
int textWidth(std::string_view s);
void drawText(int x, int y, std::string_view s, u8 color);
// Two passes: shadow colour at (x+1, y+1) with the clip moved down one row,
// then text colour at (x, y).
void drawTextShadow(int x, int y, std::string_view s, u8 shadow, u8 color);

// Fixed-length text resources (.S): name + ".s" from the archives.
bool loadText(const std::string& name, std::vector<u8>& out);
// NUL-terminated line of `len` bytes at `offset` in a text resource.
std::string textLine(const std::vector<u8>& text, size_t offset, size_t len);

} // namespace st::game
