#include "data/gamefs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace st {

GameFS& gameFS() {
    static GameFS instance;
    return instance;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

static bool hasExe(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (lower(e.path().filename().string()) == "st.exe") return true;
    return false;
}

bool GameFS::init(const std::string& explicitDir) {
    std::vector<fs::path> candidates;
    if (!explicitDir.empty()) {
        candidates.emplace_back(explicitDir);
    } else {
        candidates = {"SealTeam-DOS", "Game", ".", "../SealTeam-DOS", "../../SealTeam-DOS"};
    }
    for (const auto& c : candidates) {
        if (hasExe(c)) {
            dir_ = fs::absolute(c).lexically_normal().string();
            logInfo("Game data: %s", dir_.c_str());
            return true;
        }
    }
    return false;
}

std::string GameFS::resolve(const std::string& dosName) const {
    std::error_code ec;
    const fs::path direct = fs::path(dir_) / dosName;
    if (fs::exists(direct, ec)) return direct.string();
    const std::string want = lower(dosName);
    for (const auto& e : fs::directory_iterator(dir_, ec))
        if (lower(e.path().filename().string()) == want) return e.path().string();
    return {};
}

bool GameFS::readFile(const std::string& dosName, std::vector<u8>& out) const {
    const std::string path = resolve(dosName);
    if (path.empty()) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    out.resize(size_t(f.tellg()));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
    return bool(f);
}

bool GameFS::writeFile(const std::string& dosName, const std::vector<u8>& data) const {
    std::string path = resolve(dosName);
    if (path.empty()) path = (fs::path(dir_) / dosName).string();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(f);
}

} // namespace st
