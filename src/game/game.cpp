#include "game/game.h"

#include "data/ealib.h"
#include "data/exeimage.h"
#include "gfx/image.h"
#include "platform/system.h"

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

// Temporary front end until the original flow (docs/re/seg_1000.md) is
// ported: show the title picture and wait for a key.
void showTitle() {
    Image title;
    Palette pal;
    if (!loadPicture("title.pic", title) || !loadPalette("ST.PAL", pal)) fatal("Couldn't load title screen");
    Video& v = sys().video();
    blitToPage(v.page(0), title, 0, 0);
    v.setDisplayStart(0);
    v.setPalette(pal.data(), 0, 256);
    sys().input().flushKeys();
    for (;;) {
        sys().waitRetrace();
        if (sys().input().keyAvailable() || sys().input().mouse().buttons) break;
    }
}

} // namespace

int run(const std::vector<std::string>& args) {
    openLibraries();
    showTitle();
    return 0;
}

} // namespace st::game
