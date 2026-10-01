// Port settings: the Original preset reproduces the DOS game 1:1; the
// Enhanced preset enables optional improvements. Game code must query the
// effective*() accessors, which always return the original behaviour when
// the Original preset is active. Stored in sealteam.cfg (key = value lines).
#pragma once

#include "core/common.h"

#include <map>
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
    int renderScale = 0;          // 3D view resolution: 0 = native (the window's pixels), N = 320N x 200N
    bool wideView = true;         // native only: widen the field of view to fill a wide window (else 4:3)
    bool fullScreen3d = false;    // native only: the field views fill the window height, the HUD drawn over the scene
    int drawDistancePct = 400;    // view distance in percent of the original; kDrawDistanceMax = whole world
    bool impactFx = true;         // surface-coloured impact puffs and a particle burst where shots hit
    // Unlike the options above, this one changes gameplay rules, not the
    // presentation: realistic SEAL loadouts and gameplay fixes (docs/mission.md,
    // "Modern gameplay"). The simulation is the original's while it is off.
    bool modernGameplay = false;

    // Audio.
    MusicDevice musicDevice = MusicDevice::AdLib;
    bool digitalSfx = true;
    int musicVolume = 100;        // percent of the game's own music level
    int sfxVolume = 100;

    // Launcher.
    bool skipLauncher = false;    // start the game directly with the saved preset

    // Runtime only (not saved): automated run (--shot), no launcher or dialogs.
    bool scriptedRun = false;

    // --- Game controller (engine/controller.cpp, docs/controller.md) -----
    // Kept as one block: the mapping layer owns the meaning of the values.
    bool padEnabled = true;
    int padDeadZonePct = 20;      // stick dead zone, percent of full deflection
    int padSensitivityPct = 100;  // stick response scale (turn / pointer speed)
    // Action id -> input name ("bind_<action> = <input>" lines); an absent
    // action uses the default Xbox layout.
    std::map<std::string, std::string> padBindings;
    // --- end controller block ------------------------------------------

    bool original() const { return preset == Preset::Original; }
    // 0 = native resolution (Enhanced only), else the fixed multiple of 320x200.
    int effectiveRenderScale() const { return original() ? 1 : renderScale; }
    bool effectiveWideView() const { return !original() && renderScale == 0 && wideView; }
    // The mission's field views (not the map, briefing or cut-scenes) fill
    // the window height; the HUD bands are drawn over the scene.
    bool effectiveFullScreen3d() const { return !original() && renderScale == 0 && fullScreen3d; }
    // Percent of the original view distance; kDrawDistanceMax = the whole world.
    int effectiveDrawDistancePct() const { return original() ? 100 : drawDistancePct; }
    // Impact sprites remapped by the surface hit plus a particle burst
    // (render/impactfx.h); the Original preset keeps the plain puffs.
    bool effectiveImpactFx() const { return !original() && impactFx; }
    // Gameplay rules of docs/mission.md "Modern gameplay" (loadouts, fixes).
    bool effectiveModernGameplay() const { return !original() && modernGameplay; }
};

Settings& settings();

// Settings file next to the executable (falls back to the user's pref dir).
void setSettingsDir(const std::string& exeDir);
bool loadSettings();
bool saveSettings();

// Allowed values for the menus.
constexpr int kRenderScaleNative = 0;
constexpr int kRenderScales[] = {kRenderScaleNative, 1, 2, 3, 4, 6};
constexpr int kDrawDistanceMax = 0;  // "Max": every object of the world is drawn
constexpr int kDrawDistances[] = {100, 150, 200, 300, 400, 800, 1600, kDrawDistanceMax};
// Menu / command-line text of a value.
const char* renderScaleName(int scale);      // "Native", "320x200", ...
const char* drawDistanceName(int pct);       // "100%", ..., "Max"

} // namespace st
