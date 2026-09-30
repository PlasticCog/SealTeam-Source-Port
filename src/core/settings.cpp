#include "core/settings.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace st {

namespace {

std::string g_dir;

std::string path() { return (g_dir.empty() ? std::string() : g_dir) + "sealteam.cfg"; }

template <size_t N>
int nearestAllowed(int v, const int (&allowed)[N]) {
    int best = allowed[0];
    for (int a : allowed)
        if (std::abs(a - v) < std::abs(best - v)) best = a;
    return best;
}

bool parseBool(const std::string& v) { return v == "1" || v == "true" || v == "yes" || v == "on"; }

} // namespace

Settings& settings() {
    static Settings instance;
    return instance;
}

void setSettingsDir(const std::string& exeDir) {
    g_dir = exeDir;
    if (!g_dir.empty() && g_dir.back() != '/' && g_dir.back() != '\\') g_dir += '/';
}

bool loadSettings() {
    std::ifstream f(path());
    if (!f) return false;
    Settings& s = settings();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string x) {
            x.erase(0, x.find_first_not_of(" \t\r"));
            x.erase(x.find_last_not_of(" \t\r") + 1);
            return x;
        };
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        const int n = std::atoi(v.c_str());
        if (k == "preset") s.preset = (v == "enhanced") ? Preset::Enhanced : Preset::Original;
        else if (k == "window_scale") s.windowScale = std::clamp(n, 1, 8);
        else if (k == "fullscreen") s.fullscreen = parseBool(v);
        else if (k == "aspect_correct") s.aspectCorrect = parseBool(v);
        else if (k == "smooth_scaling") s.smoothScaling = parseBool(v);
        else if (k == "render_scale") s.renderScale = nearestAllowed(n, kRenderScales);
        else if (k == "draw_distance") s.drawDistancePct = nearestAllowed(n, kDrawDistances);
        else if (k == "music_device") s.musicDevice = (v == "opl3") ? MusicDevice::SoundBlasterPro2 : MusicDevice::AdLib;
        else if (k == "digital_sfx") s.digitalSfx = parseBool(v);
        else if (k == "music_volume") s.musicVolume = std::clamp(n, 0, 100);
        else if (k == "sfx_volume") s.sfxVolume = std::clamp(n, 0, 100);
        else if (k == "skip_launcher") s.skipLauncher = parseBool(v);
    }
    return true;
}

bool saveSettings() {
    const Settings& s = settings();
    std::ofstream f(path(), std::ios::trunc);
    if (!f) return false;
    f << "# SEAL Team source port settings\n"
      << "preset = " << (s.original() ? "original" : "enhanced") << "\n"
      << "window_scale = " << s.windowScale << "\n"
      << "fullscreen = " << s.fullscreen << "\n"
      << "aspect_correct = " << s.aspectCorrect << "\n"
      << "smooth_scaling = " << s.smoothScaling << "\n"
      << "render_scale = " << s.renderScale << "\n"
      << "draw_distance = " << s.drawDistancePct << "\n"
      << "music_device = " << (s.musicDevice == MusicDevice::SoundBlasterPro2 ? "opl3" : "opl2") << "\n"
      << "digital_sfx = " << s.digitalSfx << "\n"
      << "music_volume = " << s.musicVolume << "\n"
      << "sfx_volume = " << s.sfxVolume << "\n"
      << "skip_launcher = " << s.skipLauncher << "\n";
    return bool(f);
}

} // namespace st
