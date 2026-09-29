#include "game/game.h"

#include "data/ealib.h"
#include "data/exeimage.h"
#include "engine/palette_fade.h"
#include "engine/ticker.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "gfx/image.h"
#include "platform/system.h"

#include <algorithm>
#include <cstring>

namespace st::game {

namespace {

// The archive list lives in a far data segment of st.exe at 5124:0000: 41
// records of 14 bytes, each a 10-char name (not always NUL terminated)
// followed by a 32-bit field used at runtime.
constexpr u16 kLibTableSeg = 0x5124;
constexpr int kLibCount = 41;
constexpr int kLibRecordSize = 14;
constexpr int kLibNameLen = 10;

void openLibraries() {
    for (int i = 0; i < kLibCount; ++i) {
        const u8* p = exe().at(kLibTableSeg, u16(i * kLibRecordSize));
        if (!p) fatal("library table missing from st.exe");
        const std::string name(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), kLibNameLen));
        if (!resources().openLib(name)) fatal("Couldn't open %s", name.c_str());
    }
}

void waitForInput() {
    sys().input().flushKeys();
    for (;;) {
        sys().waitRetrace();
        if (sys().input().keyAvailable() || sys().input().mouse().buttons) break;
    }
}

// Temporary front end until the original flow (docs/re/seg_19ac.md,
// main_game_loop) is ported: show the title picture and wait for a key.
void showTitle() {
    Image title;
    Palette pal;
    if (!loadPicture("title.pic", title) || !loadPalette("ST.PAL", pal)) fatal("Couldn't load title screen");
    Gfx& g = gfx();
    g.setDrawPage(0);
    g.clipFull();
    g.drawImage(title, 0, 0);
    g.setDisplayPage(0);
    engine::paletteFade().setPalette(pal);
    engine::paletteFade().setLevel(0);
    waitForInput();
}

// Developer check of the engine layer: fonts, masked cursor, primitives,
// scaled RLE sprites.
void selfTestGfx() {
    Image title, cursor;
    Mask cursorMask;
    Palette pal, missionPal;
    if (!loadPicture("title.pic", title) || !loadPalette("ST.PAL", pal) || !loadPalette("PAL2.PAL", missionPal) ||
        !loadPicture("cursor.pic", cursor) || !loadMask("cursor.msk", cursor.w, cursor.h, cursorMask))
        fatal("selftest: missing data");
    Font fonts[4];
    const char* names[4] = {"4x6.fnt", "memo.fnt", "prop.fnt", "propbold.fnt"};
    for (int i = 0; i < 4; ++i)
        if (!fonts[i].load(names[i])) fatal("selftest: cannot load %s", names[i]);

    Gfx& g = gfx();
    g.setDrawPage(0);
    g.clipFull();
    g.clear(0);
    g.setTextColors(15, 0);
    g.draw4x6String(fonts[0], "4X6 FONT 0123456789", 4, 4);
    int y = 12;
    for (int i = 1; i < 4; ++i) {
        g.setFont(&fonts[i]);
        g.drawString("The quick brown fox 1993", 4, y);
        y += fonts[i].height() + 2;
    }
    // Primitives: lines fan, circles, polygon, dithered rectangle.
    for (int a = 0; a < 16; ++a) g.line(80, 120, 80 + (a - 8) * 9, 60 + (a & 1) * 110, u16(Gfx::kSolid | (32 + a)));
    g.fillCircle(170, 90, 25, u16(Gfx::kSolid | 40));
    g.fillCircle(170, 90, 12, u16(0x5a00 | 15));  // dithered
    const s16 tri[] = {230, 60, 300, 110, 210, 150};
    g.fillPolygon(tri, 3, u16(Gfx::kSolid | 12));
    const s16 quad[] = {200, 160, 250, 160, 260, 190, 190, 190};
    g.fillPolygon(quad, 4, u16(Gfx::kSolid | 9));
    g.fillRect(4, 150, 60, 40, u16(0xa500 | 14));
    g.rect(2, 148, 64, 44, u16(Gfx::kSolid | 15));
    g.setClip(100, 140, 80, 50);
    g.line(90, 130, 200, 200, u16(Gfx::kSolid | 10));  // clipped line
    g.clipFull();
    for (int i = 0; i < 6; ++i) g.drawImageMasked(cursor, cursorMask, 200 + i * 18, 4);

    // Soldier sprite scaled at several sizes (mission palette on page 1 check).
    std::vector<u8> spr;
    if (resources().read("USF1R1.RLE", spr)) {
        const int sw = rd16(&spr[0]), sh = rd16(&spr[2]);
        g.spriteScaled(270, 120, sw, sh, spr.data());
        g.spriteScaled(290, 120, sw / 2, sh / 2, spr.data());
        g.spriteScaled(300, 150, sw * 2, sh * 2, spr.data());  // clipped at the right edge
    }
    g.setDisplayPage(0);
    engine::paletteFade().setPalette(pal);
    engine::paletteFade().setLevel(0);
    waitForInput();
}

} // namespace

int run(const std::vector<std::string>& args) {
    openLibraries();
    engine::ticker().install();
    gfx().init();
    if (std::find(args.begin(), args.end(), "--selftest-gfx") != args.end()) {
        selfTestGfx();
        return 0;
    }
    showTitle();
    return 0;
}

} // namespace st::game
