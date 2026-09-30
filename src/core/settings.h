// Port settings: the Original preset reproduces the DOS game 1:1; the
// Enhanced preset enables optional improvements. Game code must query the
// effective*() accessors, which always return the original behaviour when
// the Original preset is active. Stored in sealteam.cfg (key = value lines).
#pragma once

#include "core/common.h"

#include <string>

namespace st {

enum class Preset { Original, Enhanced };
enum class MusicDevice { AdLib, SoundBlasterPro2 };  // OPL2 / OPL3 (AIL ADLIB.ADV / SBP2FM.ADV)

struct Settings {
    Preset preset = Preset::Original;

    // Display (presentation only; never changes game behaviour).
    int windowScale = 3;          // window size as a multiple of 320x240 (or 320x200)
    bool fullscreen = false;
    bool aspectCorrect = true;    // show 320x200 as 4:3 like a CRT
    bool smoothScaling = false;   // linear instead of nearest-neighbour upscaling

    // Enhancements (only used with the Enhanced preset).
    int renderScale = 2;          // 3D view resolution multiplier: 1 = 320x200, 2 = 640x400, ...
    int drawDistancePct = 200;    // view distance in percent of the original

    // Audio.
    MusicDevice musicDevice = MusicDevice::AdLib;
    bool digitalSfx = true;
    int musicVolume = 100;        // percent of the game's own music level
    int sfxVolume = 100;

    // Launcher.
    bool skipLauncher = false;    // start the game directly with the saved preset

    // Runtime only (not saved): automated run (--shot), no launcher or dialogs.
    bool scriptedRun = false;

    bool original() const { return preset == Preset::Original; }
    int effectiveRenderScale() const { return original() ? 1 : renderScale; }
    int effectiveDrawDistancePct() const { return original() ? 100 : drawDistancePct; }
};

Settings& settings();

// Settings file next to the executable (falls back to the user's pref dir).
void setSettingsDir(const std::string& exeDir);
bool loadSettings();
bool saveSettings();

// Allowed values for the menus.
constexpr int kRenderScales[] = {1, 2, 3, 4, 6};
constexpr int kDrawDistances[] = {100, 150, 200, 300, 400};

} // namespace st
