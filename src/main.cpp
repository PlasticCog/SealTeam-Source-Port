// SEAL Team source port - entry point.
//
// The original game files must be copied into a folder named "Game" next to
// the executable (or in the working directory).
//
// Usage: sealteam [--data DIR] [--scale N] [--fullscreen] [--no-aspect] [original options]
// Original options (see st.exe "Command Line Options"): ? / h help, d digital
// sound off, t title screen off, NN start mission NN.

#include "core/common.h"
#include "core/settings.h"
#include "platform/crash.h"
#include "data/ealib.h"
#include "data/exeimage.h"
#include "data/gamefs.h"
#include "game/game.h"
#include "platform/system.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace st;

namespace {

bool g_interactive = true;  // false for scripted --shot runs: no modal dialogs

void startupError(const char* msg) {
    std::fprintf(stderr, "%s\n", msg);
    if (g_interactive) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SEAL Team", msg, nullptr);
}

void usage() {
    std::printf(
        "SEAL Team source port\n"
        "usage: sealteam [--original | --enhanced | --launcher] [--data DIR] [--scale N | --window WxH]\n"
        "                [--fullscreen] [--no-aspect] [options]\n"
        "  --original    play 1:1 like the DOS game (skips the start menu)\n"
        "  --enhanced    play with the enhancements chosen in Setup (skips the start menu)\n"
        "  --launcher    show the start menu even if it was switched off\n"
        "  --data DIR    use DIR instead of the Game folder for the original files\n"
        "  --scale N     window scale factor (default from Setup, 3)\n"
        "  --window WxH  window size in pixels instead of the scale factor\n"
        "  --fullscreen  start in fullscreen (Alt+Enter toggles)\n"
        "  --no-aspect   show square pixels instead of 4:3\n"
        "Settings are stored in sealteam.cfg next to the program.\n"
        "Original game options are passed through: ? h (help) d (no digital sound)\n"
        "t (no title screen) and a mission number.\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string exeDir;
    if (const char* base = SDL_GetBasePath()) exeDir = base;  // owned by SDL
    setSettingsDir(exeDir);
    installCrashHandler(exeDir);
    loadSettings();
    Settings& st = settings();

    std::string dataDir;
    VideoConfig vcfg;
    vcfg.scale = st.windowScale;
    vcfg.fullscreen = st.fullscreen;
    vcfg.aspectCorrect = st.aspectCorrect;
    vcfg.smooth = st.smoothScaling;
    std::vector<std::string> gameArgs{"st"};
    std::string shotPath;
    double shotAfter = 2.0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dataDir = argv[++i];
        else if (a == "--scale" && i + 1 < argc) vcfg.scale = std::max(1, std::atoi(argv[++i]));
        else if (a == "--window" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &vcfg.width, &vcfg.height) != 2) vcfg.width = vcfg.height = 0;
        }
        else if (a == "--fullscreen") vcfg.fullscreen = true;
        else if (a == "--no-aspect") vcfg.aspectCorrect = false;
        else if (a == "--shot" && i + 1 < argc) shotPath = argv[++i];
        else if (a == "--shot-after" && i + 1 < argc) shotAfter = std::atof(argv[++i]);
        else if (a == "--help") { usage(); return 0; }
        else gameArgs.push_back(a);
    }

    g_interactive = shotPath.empty();
    st.scriptedRun = !g_interactive;
    if (!gameFS().init(dataDir, exeDir)) {
        startupError(dataDir.empty()
            ? "Could not find the original SEAL Team files.\n\n"
              "Copy all files from your SEAL Team installation (st.exe, *.lib, *.fnt, ...) "
              "into a folder named \"Game\" next to sealteam.exe."
            : "Could not find st.exe in the directory given with --data.");
        return 1;
    }
    if (!exe().load("st.exe")) {
        startupError("Could not read st.exe from the Game folder.");
        return 1;
    }
    if (!exe().looksLikeSealTeam()) {
        startupError("Game/st.exe is not the SEAL Team V1.0 executable this port supports.");
        return 1;
    }

    int rc = 0;
    if (!sys().init(vcfg)) return 1;
    sys().setScreenshotDir(exeDir);
    if (!shotPath.empty()) sys().scheduleScreenshot(shotPath, shotAfter);
    try {
        rc = game::run(gameArgs);
    } catch (const QuitRequested&) {
        rc = 0;
    } catch (const FatalError& e) {
        logError("%s", e.what());
        if (g_interactive) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SEAL Team", e.what(), nullptr);
        rc = 1;
    }
    sys().shutdown();
    return rc;
}
