#include "game/screens.h"

#include "data/ealib.h"
#include "data/exeimage.h"
#include "engine/palette_fade.h"
#include "engine/ticker.h"
#include "game/globals.h"
#include "gfx/gfx.h"
#include "platform/system.h"

#include <algorithm>
#include <cstring>

namespace st::game {

Globals& g() {
    static Globals instance;
    return instance;
}

namespace {

constexpr u16 kPalNameTable = 0x0102;  // 17 near pointers to palette names
constexpr int kPalCount = 17;

// pic_load builds the file name from DGROUP strings chosen by a jump table;
// these are the string offsets per picture number (0 = none).
constexpr u16 kPicNameOffsets[21] = {
    0x05a5, 0x05ac, 0x05b5, 0x05b9, 0x05bd, 0x05c3, 0,      0x05e1, 0,      0x05c9, 0x05ce,
    0x05d2, 0x05d7, 0x05dc, 0x05e7, 0x05eb, 0x05ef, 0x05f3, 0x05f7, 0x05fb, 0x05b2,
};

Palette g_current;
Image g_picture;
Font g_fonts[3];
Font g_font4x6;
const Font* g_selected = nullptr;

} // namespace

void palLoad(int n) {
    if (n < 0 || n >= kPalCount) fatal("bad palette number %d", n);
    const std::string name = exe().dgStringPtr(u16(kPalNameTable + 2 * n)) + ".pal";
    if (!loadPalette(name, g_current)) fatal("Couldn't load %s", name.c_str());
    engine::paletteFade().setPalette(g_current);
}

void palApply() { engine::paletteFade().upload(); }

bool picLoad(int n) {
    if (n < 0 || n > 20 || kPicNameOffsets[n] == 0) return false;
    const std::string name = exe().dgString(kPicNameOffsets[n]) + ".pic";
    return loadPicture(name, g_picture);
}

const Image& picture() { return g_picture; }

void picBlitToScreen() {
    Gfx& gx = gfx();
    gx.clipFull();
    gx.drawImage(g_picture, 0, 0);
}

void present() {
    Gfx& gx = gfx();
    gx.clipFull();
    gx.flip(true, true);
    engine::ticker().frameLimitReset();
}

void screenTransition(int pal, bool wait) {
    auto& fade = engine::paletteFade();
    auto& clock = engine::ticker();
    if (wait) {
        clock.resetClock();
        while (clock.time() < 0x180) {
            sys().idle();
            clock.updateGameTime();
            fade.request(0x700, clock.frameDt());
        }
    }
    for (int i = 0; i < (wait ? 2 : 1); ++i) {
        gfx().clear(0);
        present();
    }
    palLoad(pal);
    if (wait) fade.setLevel(0x100);
    palApply();
    if (wait) fade.setLevel(0x100);
}

void loadFonts() {
    // font_load_all (365e:921E): memo -> clipboard, propbold -> title,
    // prop -> dialog; then propbold is selected.
    const char* files[3] = {"memo.fnt", "propbold.fnt", "prop.fnt"};
    for (int i = 0; i < 3; ++i)
        if (!g_fonts[i].load(files[i])) fatal("Couldn't load %s", files[i]);
    if (!g_font4x6.load("4x6.fnt")) fatal("Couldn't load 4x6.fnt");
    fontSelect(FontId::Title);
}

void fontSelect(FontId f) {
    g_selected = &g_fonts[int(f)];
    gfx().setFont(g_selected);
}

const Font& font4x6() { return g_font4x6; }

int textWidth(std::string_view s) { return g_selected ? g_selected->textWidth(s) : 0; }

void drawText(int x, int y, std::string_view s, u8 color) {
    Gfx& gx = gfx();
    gx.setTextColors(color, 0);
    gx.drawString(s, x, y);
}

void drawTextShadow(int x, int y, std::string_view s, u8 shadow, u8 color) {
    Gfx& gx = gfx();
    const int cx0 = gx.clipX0(), cy0 = gx.clipY0();
    const int cw = gx.clipX1() - cx0 + 1, ch = gx.clipY1() - cy0 + 1;
    gx.setClip(cx0, cy0 + 1, cw, ch);  // g_clip_y0++, g_clip_y1++
    drawText(x + 1, y + 1, s, shadow);
    gx.setClip(cx0, cy0, cw, ch);
    drawText(x, y, s, color);
}

bool loadText(const std::string& name, std::vector<u8>& out) {
    return resources().read(name + ".s", out);
}

std::string textLine(const std::vector<u8>& text, size_t offset, size_t len) {
    if (offset >= text.size()) return {};
    len = std::min(len, text.size() - offset);
    const char* p = reinterpret_cast<const char*>(text.data() + offset);
    return std::string(p, strnlen(p, len));
}

} // namespace st::game
