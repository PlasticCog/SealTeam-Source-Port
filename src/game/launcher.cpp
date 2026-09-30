#include "game/launcher.h"

#include "core/settings.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/ticker.h"
#include "game/devtools.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/gfx.h"
#include "platform/system.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace st::game {

namespace {

enum class Result { None, Play, Quit, Back };

u16 colour(u8 index) { return u16(0xff00 | index); }

// Panel in the style of the game's text panels.
void drawPanel(int x, int y, int w, int h) {
    Gfx& gx = gfx();
    gx.fillRect(x - 4, y - 3, w + 8, h + 6, colour(0x18));
    gx.fillRect(x - 3, y - 2, w + 6, h + 4, colour(0x16));
    gx.fillRect(x - 2, y - 1, w + 4, h + 2, colour(0x14));
}

void drawHelp(const std::string& text) {
    fontSelect(FontId::Dialog);
    drawPanel(20, 170, 280, 12);
    drawTextShadow(160 - textWidth(text) / 2, 172, text, 0x0f, 0x00);
}

// Generic screen loop for a button list: redraws, pointer, press/release
// countdown and hot keys, like the game's own screens.
template <typename Draw, typename Handle>
Result runScreen(ButtonList& list, Draw draw, Handle handle) {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    UiState& s = ui();
    in.resetRepeatTimers();
    in.flushKeyboard();
    in.setMode(engine::InputMode::Menu);
    Result result = Result::None;
    while (result == Result::None) {
        engine::paletteFade().request(0, clock.frameDt());
        // Redraw every frame: the game's two-frame redraw leaves the two video
        // pages with different help text when the focus changes between them
        // (the cursor then sits on one button while the page shows another's
        // text). This screen is cheap enough to draw fully each frame.
        clock.frameLimitWait();
        picBlitToScreen();
        draw();
        uiDrawButtons(list);
        gfx().clipFull();
        clock.frameLimitWait();
        cursorDraw();
        present();
        clock.updateGameTime();

        int key = in.getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        if (dx || dy) uiPointerUpdate(dx, dy, list);
        uiButtonRelease(key);
        if (s.releaseCount != 0 && --s.releaseCount == 0 && s.focus != -1) {
            key = list[size_t(s.focus)].key;
        }
        switch (key) {
        case 0: break;
        case engine::key::Up: uiPointerUpdate(0, -5, list); break;
        case engine::key::Down: uiPointerUpdate(0, 5, list); break;
        case engine::key::Left: uiPointerUpdate(-8, 0, list); break;
        case engine::key::Right: uiPointerUpdate(8, 0, list); break;
        case engine::key::Enter:
            if (s.focus != -1) uiButtonPress(list[size_t(s.focus)].key);
            break;
        default:
            result = handle(key);
            break;
        }
    }
    return result;
}

Button makeButton(int x, int y, int w, u16 key, const std::string& label, bool underline) {
    Button b;
    b.x = s16(x);
    b.y = s16(y);
    b.w = s16(w);
    b.h = 16;
    b.key = key;
    b.hotIndex = underline ? 0 : -1;
    b.flags = btn::Clickable;
    b.label = label;
    return b;
}

template <size_t N>
int cycle(int v, const int (&values)[N]) {
    for (size_t i = 0; i < N; ++i)
        if (values[i] == v) return values[(i + 1) % N];
    return values[0];
}

std::string onOff(bool v) { return v ? "On" : "Off"; }

// ------------------------------------------------------------------ options

Result runOptions() {
    Settings& st = settings();
    const bool wasFullscreen = st.fullscreen;
    ButtonList list;
    const char* help = "Click an option to change it.";
    auto rebuild = [&] {
        char buf[64];
        list.clear();
        std::snprintf(buf, sizeof buf, "Window size: %dx", st.windowScale);
        list.push_back(makeButton(6, 32, 150, 'a', buf, false));
        list.push_back(makeButton(6, 52, 150, 'b', "Fullscreen: " + onOff(st.fullscreen), false));
        list.push_back(makeButton(6, 72, 150, 'c', std::string("Aspect: ") + (st.aspectCorrect ? "4:3 (CRT)" : "Square pixels"), false));
        list.push_back(makeButton(6, 92, 150, 'd', "Smooth scaling: " + onOff(st.smoothScaling), false));
        list.push_back(makeButton(6, 112, 150, 'e', std::string("Show this menu: ") + (st.skipLauncher ? "No" : "Yes"), false));
        std::snprintf(buf, sizeof buf, "3D resolution: %dx%d", 320 * st.renderScale, 200 * st.renderScale);
        list.push_back(makeButton(164, 32, 150, 'f', buf, false));
        std::snprintf(buf, sizeof buf, "Draw distance: %d%%", st.drawDistancePct);
        list.push_back(makeButton(164, 52, 150, 'g', buf, false));
        list.push_back(makeButton(164, 72, 150, 'h', std::string("Music: ") + (st.musicDevice == MusicDevice::AdLib ? "AdLib" : "SB Pro 2"), false));
        std::snprintf(buf, sizeof buf, "Music volume: %d%%", st.musicVolume);
        list.push_back(makeButton(164, 92, 150, 'i', buf, false));
        std::snprintf(buf, sizeof buf, "Effects: %s %d%%", st.digitalSfx ? "Digital" : "FM", st.sfxVolume);
        list.push_back(makeButton(164, 112, 150, 'j', buf, false));
        list.push_back(makeButton(112, 140, 96, engine::key::Esc, "Back", false));
    };
    rebuild();
    const Result r = runScreen(
        list,
        [&] {
            uiDrawTitleTab(" Setup  ", 136, 4);
            drawHelp(help);
        },
        [&](int key) {
            switch (key) {
            case 'a': st.windowScale = st.windowScale % 6 + 1; help = "Window size takes effect at the next start."; break;
            case 'b': st.fullscreen = !st.fullscreen; help = "Alt+Enter also switches fullscreen."; break;
            case 'c': st.aspectCorrect = !st.aspectCorrect; help = "Takes effect at the next start."; break;
            case 'd': st.smoothScaling = !st.smoothScaling; help = "Takes effect at the next start."; break;
            case 'e': st.skipLauncher = !st.skipLauncher; help = "Start with --launcher to see this menu again."; break;
            case 'f': st.renderScale = cycle(st.renderScale, kRenderScales); help = "Enhanced game only: resolution of the 3D view."; break;
            case 'g': st.drawDistancePct = cycle(st.drawDistancePct, kDrawDistances); help = "Enhanced game only: how far you can see."; break;
            case 'h':
                st.musicDevice = st.musicDevice == MusicDevice::AdLib ? MusicDevice::SoundBlasterPro2 : MusicDevice::AdLib;
                help = "AdLib (OPL2) or Sound Blaster Pro 2 (OPL3) music.";
                break;
            case 'i': st.musicVolume = (st.musicVolume + 10) % 110; help = "Music volume."; break;
            case 'j':
                // Cycle: digital 100/75/50/25 %, then FM effects, then back.
                if (st.digitalSfx && st.sfxVolume > 25) st.sfxVolume -= 25;
                else if (st.digitalSfx) { st.digitalSfx = false; st.sfxVolume = 100; }
                else st.digitalSfx = true;
                help = "Digital (Sound Blaster) or FM sound effects.";
                break;
            case engine::key::Esc: return Result::Back;
            case engine::key::AltX: return Result::Quit;
            default: return Result::None;
            }
            rebuild();
            return Result::None;
        });
    if (st.fullscreen != wasFullscreen) sys().video().toggleFullscreen();
    saveSettings();
    return r;
}

const DevCommand kSetupScreen("--setup-screen", "open the start menu's Setup page", [](const DevArgs&) {
    screenTransition(PalMenu, false);
    picLoad(PicMainMenu);
    engine::paletteFade().setLevel(0);
    return runOptions() == Result::Quit ? 0 : 0;
});

} // namespace

bool runLauncher() {
    Settings& st = settings();
    screenTransition(PalMenu, false);
    picLoad(PicMainMenu);
    engine::paletteFade().setLevel(0);
    ButtonList list = {
        makeButton(96, 64, 128, 'o', "Original Game", true),
        makeButton(96, 88, 128, 'e', "Enhanced Game", true),
        makeButton(96, 112, 128, 's', "Setup", true),
        makeButton(96, 136, 128, 'q', "Quit to Desktop", true),
    };
    ui().focus = st.original() ? 0 : 1;
    uiCursorToButton(list[size_t(ui().focus)]);
    for (;;) {
        const Result r = runScreen(
            list,
            [&] {
                uiDrawTitleTab(" SEAL Team  ", 124, 4);
                static const char* kHelp[] = {
                    "Plays exactly like the 1993 DOS original.",
                    "Higher 3D resolution and longer draw distance.",
                    "Display, enhancement and sound options.",
                    "Leave the game.",
                };
                const int f = ui().focus;
                drawHelp(f >= 0 && f < 4 ? kHelp[f] : "Source port start menu.");
            },
            [&](int key) {
                switch (key) {
                case 'o': st.preset = Preset::Original; return Result::Play;
                case 'e': st.preset = Preset::Enhanced; return Result::Play;
                case 's': return Result::Back;  // opens Setup below
                case 'q':
                case engine::key::Esc:
                case engine::key::AltX: return Result::Quit;
                default: return Result::None;
                }
            });
        if (r == Result::Back) {
            if (runOptions() == Result::Quit) return false;
            ui().focus = 2;
            uiCursorToButton(list[2]);
            continue;
        }
        cursorReset();
        if (r == Result::Quit) return false;
        saveSettings();
        return true;
    }
}

} // namespace st::game
