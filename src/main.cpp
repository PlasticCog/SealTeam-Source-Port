// SEAL Team source port - entry point.
//
// Usage: sealteam [--data DIR] [--scale N] [--fullscreen] [--no-aspect] [original options]
// Original options (see st.exe "Command Line Options"): ? / h help, d digital
// sound off, t title screen off, NN start mission NN.

#include "core/common.h"
#include "data/ealib.h"
#include "data/exeimage.h"
#include "data/gamefs.h"
#include "game/game.h"
#include "platform/system.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace st;

namespace {

void usage() {
    std::printf(
        "SEAL Team source port\n"
        "usage: sealteam [--data DIR] [--scale N] [--fullscreen] [--no-aspect] [options]\n"
        "  --data DIR    directory containing the original game (st.exe and *.lib)\n"
        "  --scale N     window scale factor (default 3)\n"
        "  --fullscreen  start in fullscreen (Alt+Enter toggles)\n"
        "  --no-aspect   show square pixels instead of 4:3\n"
        "Original game options are passed through: ? h (help) d (no digital sound)\n"
        "t (no title screen) and a mission number.\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string dataDir;
    VideoConfig vcfg;
    std::vector<std::string> gameArgs{"st"};
    std::string shotPath;
    double shotAfter = 2.0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dataDir = argv[++i];
        else if (a == "--scale" && i + 1 < argc) vcfg.scale = std::max(1, std::atoi(argv[++i]));
        else if (a == "--fullscreen") vcfg.fullscreen = true;
        else if (a == "--no-aspect") vcfg.aspectCorrect = false;
        else if (a == "--shot" && i + 1 < argc) shotPath = argv[++i];
        else if (a == "--shot-after" && i + 1 < argc) shotAfter = std::atof(argv[++i]);
        else if (a == "--help") { usage(); return 0; }
        else gameArgs.push_back(a);
    }

    if (!gameFS().init(dataDir)) {
        std::fprintf(stderr, "Could not find the SEAL Team game files (st.exe). Use --data DIR.\n");
        return 1;
    }
    if (!exe().load("st.exe")) {
        std::fprintf(stderr, "Could not read st.exe\n");
        return 1;
    }
    if (!exe().looksLikeSealTeam()) {
        std::fprintf(stderr, "st.exe is not the SEAL Team V1.0 executable this port supports.\n");
        return 1;
    }

    int rc = 0;
    if (!sys().init(vcfg)) return 1;
    if (!shotPath.empty()) sys().scheduleScreenshot(shotPath, shotAfter);
    try {
        rc = game::run(gameArgs);
    } catch (const QuitRequested&) {
        rc = 0;
    } catch (const FatalError& e) {
        logError("%s", e.what());
        if (shotPath.empty()) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SEAL Team", e.what(), nullptr);
        rc = 1;
    }
    sys().shutdown();
    return rc;
}
