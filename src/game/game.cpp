#include "game/game.h"

#include "data/ealib.h"
#include "data/exeimage.h"
#include "engine/controller.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/rng.h"
#include "engine/sound.h"
#include "engine/ticker.h"
#include "game/config.h"
#include "core/settings.h"
#include "game/devtools.h"
#include "game/globals.h"
#include "game/launcher.h"
#include "game/main_loop.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "gfx/image.h"
#include "platform/system.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
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

// Usage text of the original (1959:04AA), printed from st.exe's strings.
void printUsage() {
    const u16 lines[] = {0x1360, 0x0172, 0x13a8, 0x13d7, 0x13fc, 0x01b2, 0x01c8, 0x01dc, 0x01e7, 0x01f2, 0x0213};
    std::printf("\n");
    for (u16 off : lines) {
        std::string s = exe().dgString(off);
        if (off >= 0x1360) s += "\n";  // version and requirement lines are printed with a newline
        std::fputs(s.c_str(), stdout);
    }
}

// main_parse_cmdline (19ac:04BE). Arguments are scanned from last to first.
void parseCommandLine(const std::vector<std::string>& args) {
    Globals& gs = g();
    int mission = -1;
    int lastLen = 0;
    for (size_t a = args.size(); a-- > 1;) {
        const std::string& arg = args[a];
        lastLen = int(arg.size());
        for (size_t i = 0; i < arg.size(); ++i) {
            const int c = std::tolower(u8(arg[i]));
            if (c == 't') gs.showTitle = false;
            else if (c == 'd') gs.digitalAllowed = false;
            else if (c == '?' || c == 'h') {
                printUsage();
                throw QuitRequested{};
            } else if (c >= '0' && c <= '9') {
                if (i == 0) mission = (c - '0') * 10;
                else if (i == 1) mission += c - '0';
            }
        }
    }
    if (args.size() > 1 && lastLen == 1) mission /= 10;
    mission -= 1;
    if (mission < 0 || mission >= 80) {
        gs.missionNo = 0;
        gs.gameMode = GameMode::Menu;
    } else {
        gs.missionNo = mission;
        gs.gameMode = GameMode::Demo;
    }
}

// sys_load_resources (1959:0265), the parts ported so far.
void initSystems() {
    openLibraries();
    engine::rng().init();
    engine::ticker().install();
    gfx().init();
    gfx().clear(0);
    gfx().initPages();
    palLoad(PalMission);
    palApply();
    loadFonts();
    cursorLoadAll();
    engine::paletteFade().setLevel(0);
    engine::input().init();
    cfgLoadSCnf(slotConfig());
}

void waitForInput() {
    sys().input().flushKeys();
    for (;;) {
        sys().waitRetrace();
        engine::controller().pump(engine::input().mode());  // pad buttons count as keys
        if (sys().input().keyAvailable() || sys().input().mouse().buttons ||
            engine::controller().stickButtons() != 0)
            break;
    }
}

// Developer check of the engine layer: fonts, masked cursor, primitives,
// scaled RLE sprites.
void selfTestGfx() {
    Image cursor;
    Mask cursorMask;
    Palette pal;
    if (!loadPalette("ST.PAL", pal) || !loadPicture("cursor.pic", cursor) ||
        !loadMask("cursor.msk", cursor.w, cursor.h, cursorMask))
        fatal("selftest: missing data");
    Font fonts[4];
    const char* names[4] = {"4x6.fnt", "memo.fnt", "prop.fnt", "propbold.fnt"};
    for (int i = 0; i < 4; ++i)
        if (!fonts[i].load(names[i])) fatal("selftest: cannot load %s", names[i]);

    Gfx& gx = gfx();
    gx.setDrawPage(0);
    gx.clipFull();
    gx.clear(0);
    gx.setTextColors(15, 0);
    gx.draw4x6String(fonts[0], "4X6 FONT 0123456789", 4, 4);
    int y = 12;
    for (int i = 1; i < 4; ++i) {
        gx.setFont(&fonts[i]);
        gx.drawString("The quick brown fox 1993", 4, y);
        y += fonts[i].height() + 2;
    }
    for (int a = 0; a < 16; ++a) gx.line(80, 120, 80 + (a - 8) * 9, 60 + (a & 1) * 110, u16(Gfx::kSolid | (32 + a)));
    gx.fillCircle(170, 90, 25, u16(Gfx::kSolid | 40));
    gx.fillCircle(170, 90, 12, u16(0x5a00 | 15));
    const s16 tri[] = {230, 60, 300, 110, 210, 150};
    gx.fillPolygon(tri, 3, u16(Gfx::kSolid | 12));
    const s16 quad[] = {200, 160, 250, 160, 260, 190, 190, 190};
    gx.fillPolygon(quad, 4, u16(Gfx::kSolid | 9));
    gx.fillRect(4, 150, 60, 40, u16(0xa500 | 14));
    gx.rect(2, 148, 64, 44, u16(Gfx::kSolid | 15));
    gx.setClip(100, 140, 80, 50);
    gx.line(90, 130, 200, 200, u16(Gfx::kSolid | 10));
    gx.clipFull();
    for (int i = 0; i < 6; ++i) gx.drawImageMasked(cursor, cursorMask, 200 + i * 18, 4);
    std::vector<u8> spr;
    if (resources().read("USF1R1.RLE", spr)) {
        const int sw = rd16(&spr[0]), sh = rd16(&spr[2]);
        gx.spriteScaled(270, 120, sw, sh, spr.data());
        gx.spriteScaled(290, 120, sw / 2, sh / 2, spr.data());
        gx.spriteScaled(300, 150, sw * 2, sh * 2, spr.data());
    }
    gx.setDisplayPage(0);
    engine::paletteFade().setPalette(pal);
    engine::paletteFade().setLevel(0);
    waitForInput();
}

} // namespace

const DevCommand kShotHotkeyTest("--test-f12", "exercise the F12 screenshot path", [](const DevArgs&) {
    sys().saveScreenshot(sys().screenshotDir() + "sealteam-shot-test.bmp");
    return 0;
});

const DevCommand kSelfTestGfx("--selftest-gfx", "draw fonts, primitives and sprites for inspection", [](const DevArgs&) {
    selfTestGfx();
    return 0;
});

int run(const std::vector<std::string>& args) {
    // Port-only "--" options are not seen by the original single-letter
    // parser, and neither are the arguments following a developer command.
    const bool devCommand = hasDevCommand(args);
    std::vector<std::string> original{args.empty() ? std::string("st") : args[0]};
    bool presetGiven = false, forceLauncher = false;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--original") { settings().preset = Preset::Original; presetGiven = true; }
        else if (a == "--enhanced") { settings().preset = Preset::Enhanced; presetGiven = true; }
        else if (a == "--launcher") forceLauncher = true;
        if (a.rfind("--", 0) == 0) {
            if (devCommand) break;
            continue;
        }
        original.push_back(a);
    }
    parseCommandLine(original);
    initSystems();
    engine::input().setHelpHook(uiShowKeyReference);  // Ctrl+H on any screen

    int rc = 0;
    if (devCommand) {
        engine::sound().init();
        runDevCommand(args, rc);
    } else {
        const Settings& st = settings();
        const bool showLauncher = forceLauncher || (!presetGiven && !st.skipLauncher && !st.scriptedRun);
        if (showLauncher && !runLauncher()) {
            engine::sound().shutdown();
            return 0;
        }
        logInfo("preset: %s", settings().original() ? "Original" : "Enhanced");
        // Sound starts after the start menu so its device choice applies.
        engine::sound().init();
        rc = mainLoop();
    }
    engine::sound().shutdown();
    return rc;
}

} // namespace st::game
