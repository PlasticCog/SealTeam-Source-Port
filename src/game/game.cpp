#include "game/game.h"

#include "data/ealib.h"
#include "data/exeimage.h"
#include "engine/palette_fade.h"
#include "engine/ticker.h"
#include "gfx/canvas.h"
#include "gfx/font.h"
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

// Temporary front end until the original flow (docs/re/seg_1000.md) is
// ported: show the title picture and wait for a key.
void showTitle() {
    Image title;
    Palette pal;
    if (!loadPicture("title.pic", title) || !loadPalette("ST.PAL", pal)) fatal("Couldn't load title screen");
    Video& v = sys().video();
    canvas().setTarget(v.page(0));
    canvas().resetClip();
    canvas().blit(title, 0, 0);
    v.setDisplayStart(0);
    engine::paletteFade().setPalette(pal);
    engine::paletteFade().setLevel(0);
    waitForInput();
}

// Developer check of the engine layer: fonts, masked cursor, fades.
void selfTestGfx() {
    Image title, cursor;
    Mask cursorMask;
    Palette pal;
    if (!loadPicture("title.pic", title) || !loadPalette("ST.PAL", pal) || !loadPicture("cursor.pic", cursor) ||
        !loadMask("cursor.msk", cursor.w, cursor.h, cursorMask))
        fatal("selftest: missing data");
    Font fonts[4];
    const char* names[4] = {"4x6.fnt", "memo.fnt", "prop.fnt", "propbold.fnt"};
    for (int i = 0; i < 4; ++i)
        if (!fonts[i].load(names[i])) fatal("selftest: cannot load %s", names[i]);

    Video& v = sys().video();
    Canvas& c = canvas();
    c.setTarget(v.page(0));
    c.resetClip();
    c.blit(title, 0, 0);
    c.fillRect(0, 120, 320, 80, 0);
    c.setTextColors(15, 0);
    c.draw4x6String(fonts[0], "4x6 FONT 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ", 4, 124);
    int y = 132;
    for (int i = 1; i < 4; ++i) {
        c.setFont(&fonts[i]);
        c.drawString("The quick brown fox jumps over the lazy dog 1993", 4, y);
        y += fonts[i].height() + 3;
    }
    for (int i = 0; i < 6; ++i) c.blitMasked(cursor, cursorMask, 200 + i * 18, 180);
    v.setDisplayStart(0);
    engine::paletteFade().setPalette(pal);
    engine::paletteFade().setLevel(0);
    waitForInput();
}

} // namespace

int run(const std::vector<std::string>& args) {
    openLibraries();
    engine::ticker().install();
    if (std::find(args.begin(), args.end(), "--selftest-gfx") != args.end()) {
        selfTestGfx();
        return 0;
    }
    showTitle();
    return 0;
}

} // namespace st::game
