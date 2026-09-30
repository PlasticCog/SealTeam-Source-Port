#include "game/title.h"

#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/sound.h"
#include "engine/ticker.h"
#include "game/front/common.h"
#include "game/globals.h"
#include "game/screens.h"
#include "gfx/gfx.h"
#include "platform/system.h"

#include <vector>

namespace st::game {

namespace {

constexpr int kLogoTicks = 0x500;       // 5 s
constexpr int kMusicDelay = 0x300;      // 3 s before the title music starts
constexpr int kTitleTimeout = 0x5400;   // 84 s
constexpr int kScrollStep = 0x10;       // one pixel every 1/16 s
constexpr int kLinesPerPage = 2;
constexpr int kLineLen = 80;
constexpr int kPageCount = 10;          // page 9 is left blank

// The recurring "has `period` elapsed since `start`" test: restarts the
// period and reports true (the original returns the time, so it also reads
// false at time 0).
bool elapsed(s32& start, int period) {
    const s32 now = engine::ticker().time();
    if (now - start < period) return false;
    start = now;
    return now != 0;
}

} // namespace

bool inputToggleKeys(int key) {
    Globals& gs = g();
    switch (key) {
    case engine::key::AltS:
        gs.sfxOn = !gs.sfxOn;
        // TODO(hud): stop all channels when turning off; "Sound Effects On/Off" message.
        return true;
    case engine::key::AltD:
        gs.detailMsgTime = engine::ticker().time() + 0x100;
        gs.detailLevel = (gs.detailLevel + 1) % 6;
        // TODO(world): level 0 removes detail objects, level 5 respawns them.
        return true;
    case engine::key::AltM:
        if (!engine::sound().musicEnabled()) {
            engine::sound().setMusicEnabled(true);
            engine::sound().play(0);
        } else {
            engine::sound().stop();
            engine::sound().setMusicEnabled(false);
        }
        // TODO(hud): "Music On" / "Music Off" message.
        return true;
    default:
        return false;
    }
}

void titleShowLogo() {
    Gfx& gx = gfx();
    auto& clock = engine::ticker();
    gx.clear(0);
    present();
    engine::paletteFade().setLevel(0x100);
    screenTransition(PalLogo, true);
    picLoad(PicLogo);
    clock.resetClock();
    engine::input().resetRepeatTimers();
    int frames = 0;
    while (clock.time() < kLogoTicks) {
        clock.updateGameTime();
        engine::paletteFade().request(0, clock.frameDt());
        if (frames < 2) {
            picBlitToScreen();
            present();
            ++frames;
        } else {
            sys().idle();
        }
    }
}

int titleScreen() {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    titleShowLogo();
    screenTransition(PalTitle, true);
    picLoad(PicTitle);
    clock.resetClock();

    std::vector<u8> credits;
    loadText("c", credits);  // 10 pages of two 80-character lines
    fontSelect(FontId::Title);
    engine::sound().loadMusic(4, 0);
    engine::paletteFade().request(0, clock.frameDt());
    clock.wait(kMusicDelay);
    engine::sound().play(0);
    in.resetRepeatTimers();
    in.flushKeyboard();
    in.setMode(engine::InputMode::Menu);

    s32 counter = 1;
    int page = 0;
    s32 scrollTimer = 0, timeoutTimer = 0;
    int result = 1;
    Gfx& gx = gfx();
    for (;;) {
        picBlitToScreen();
        gx.setClip(0, 150, 320, 40);
        if (page < 9) {
            engine::ticker().frameLimitWait();
            const size_t base = size_t(page) * kLinesPerPage * kLineLen;
            const int y = 190 - int(counter % 64);
            const std::string line1 = textLine(credits, base, kLineLen);
            drawTextShadow(160 - textWidth(line1) / 2, y, line1, 0, 0x0f);
            const std::string line2 = textLine(credits, base + kLineLen, kLineLen);
            if (!line2.empty()) drawTextShadow(160 - textWidth(line2) / 2, y + 12, line2, 0, 0x0f);
        }
        present();
        clock.updateGameTime();
        engine::paletteFade().request(0, clock.frameDt());
        if (elapsed(scrollTimer, kScrollStep)) {
            ++counter;
            if (counter % 50 == 0) {
                counter = 1;
                if (++page == kPageCount) page = 0;
            }
        }
        const int key = front::getKey();
        if (inputToggleKeys(key)) continue;
        if (key == engine::key::Enter) break;
        if (elapsed(timeoutTimer, kTitleTimeout)) break;
        if (key == engine::key::Esc) break;
        if (key == engine::key::AltX) {
            result = 0;
            break;
        }
    }
    engine::sound().unload();
    return result;
}

} // namespace st::game
