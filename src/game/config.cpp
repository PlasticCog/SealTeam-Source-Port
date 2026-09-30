#include "game/config.h"

#include "data/gamefs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace st::game {

namespace {

constexpr size_t kNameLen = 26;

std::string& saveDirRef() {
    static std::string dir = [] {
        const char* env = std::getenv("SEALTEAM_SAVE_DIR");
        return std::string(env ? env : "");
    }();
    return dir;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Case-insensitive lookup in the save directory (DOS names).
std::string resolveInSaveDir(const std::string& dosName) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path direct = fs::path(saveDirRef()) / dosName;
    if (fs::exists(direct, ec)) return direct.string();
    const std::string want = lower(dosName);
    for (const auto& e : fs::directory_iterator(saveDirRef(), ec))
        if (lower(e.path().filename().string()) == want) return e.path().string();
    return {};
}

void mirrorFromRaw(SlotConfig& c) {
    c.lastSlot = c.raw.last_slot;
    for (size_t i = 0; i < 8; ++i) {
        c.slotUsed[i] = c.raw.slot_used[i];
        c.names[i].assign(c.raw.names[i], strnlen(c.raw.names[i], kNameLen));
    }
}

void mirrorToRaw(const SlotConfig& c, ConfigFile& raw) {
    raw.last_slot = c.lastSlot;
    for (size_t i = 0; i < 8; ++i) {
        raw.slot_used[i] = c.slotUsed[i];
        // The original copies names with strcpy into the 26-byte field:
        // bytes after the terminator keep their old contents.
        const size_t n = std::min(c.names[i].size(), kNameLen - 1);
        std::memcpy(raw.names[i], c.names[i].data(), n);
        raw.names[i][n] = 0;
    }
}

} // namespace

// ------------------------------------------------------------ file formats

bool parseConfig(const u8* data, std::size_t size, ConfigFile& out) {
    if (size < kConfigSize) return false;
    std::memcpy(out.unk_00, data, 3);
    out.last_slot = s8(data[3]);
    std::memcpy(out.slot_used, data + 4, 8);
    for (size_t i = 0; i < 8; ++i) std::memcpy(out.names[i], data + 0x0c + i * kNameLen, kNameLen);
    std::memcpy(out.unk_dc, data + 0xdc, 12);
    return true;
}

void serializeConfig(const ConfigFile& in, std::vector<u8>& out) {
    out.assign(kConfigSize, 0);
    std::memcpy(out.data(), in.unk_00, 3);
    out[3] = u8(in.last_slot);
    std::memcpy(out.data() + 4, in.slot_used, 8);
    for (size_t i = 0; i < 8; ++i) std::memcpy(out.data() + 0x0c + i * kNameLen, in.names[i], kNameLen);
    std::memcpy(out.data() + 0xdc, in.unk_dc, 12);
}

bool parseDifficulty(const u8* data, std::size_t size, DifficultyOptions& out) {
    if (size < kDifficultySize) return false;
    u16* f[8] = {&out.ammo, &out.enemy_wounds, &out.intelligence, &out.player_wounds,
                 &out.reload_time, &out.team_size, &out.weapons, &out.map};
    for (int i = 0; i < 8; ++i) *f[i] = rd16(data + 2 * i);
    return true;
}

void serializeDifficulty(const DifficultyOptions& in, std::vector<u8>& out) {
    const u16 v[8] = {in.ammo, in.enemy_wounds, in.intelligence, in.player_wounds,
                      in.reload_time, in.team_size, in.weapons, in.map};
    out.resize(kDifficultySize);
    for (int i = 0; i < 8; ++i) {
        out[size_t(2 * i)] = u8(v[i]);
        out[size_t(2 * i + 1)] = u8(v[i] >> 8);
    }
}

// ------------------------------------------------------------ save location

void setSaveDir(const std::string& dir) { saveDirRef() = dir; }
const std::string& saveDir() { return saveDirRef(); }

bool saveFileRead(const std::string& dosName, std::vector<u8>& out) {
    if (!saveDirRef().empty()) {
        const std::string p = resolveInSaveDir(dosName);
        if (!p.empty()) {
            std::ifstream f(p, std::ios::binary);
            if (!f) return false;
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            return true;
        }
    }
    return gameFS().readFile(dosName, out);
}

bool saveFileWrite(const std::string& dosName, const std::vector<u8>& data) {
    if (saveDirRef().empty()) return gameFS().writeFile(dosName, data);
    std::string p = resolveInSaveDir(dosName);
    if (p.empty()) p = (std::filesystem::path(saveDirRef()) / dosName).string();
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(f);
}

// ------------------------------------------------------------ s.cnf

SlotConfig& slotConfig() {
    static SlotConfig instance;
    return instance;
}

bool cfgLoadSCnf(SlotConfig& out) {
    std::vector<u8> d;
    if (!saveFileRead("s.cnf", d) || !parseConfig(d.data(), d.size(), out.raw)) {
        // Missing file: only the last slot is set (to -1); the rest of the
        // far block keeps its previous contents.
        out.lastSlot = -1;
        out.raw.last_slot = -1;
        return false;
    }
    mirrorFromRaw(out);
    return true;
}

bool cfgSaveSCnf(const SlotConfig& cfg) {
    ConfigFile raw = cfg.raw;
    mirrorToRaw(cfg, raw);
    std::vector<u8> d;
    serializeConfig(raw, d);
    return saveFileWrite("s.cnf", d);
}

// ------------------------------------------------------------ st1.dfr

DifficultyOptions& difficulty() {
    // Defaults = the shipped st1.dfr (everything "Real").
    static DifficultyOptions instance{1, 2, 2, 2, 1, 1, 1, 1};
    return instance;
}

bool cfgLoadDifficulty() {
    std::vector<u8> d;
    if (!saveFileRead("st1.dfr", d)) return false;
    return parseDifficulty(d.data(), d.size(), difficulty());
}

bool cfgSaveDifficulty() {
    std::vector<u8> d;
    serializeDifficulty(difficulty(), d);
    return saveFileWrite("st1.dfr", d);
}

} // namespace st::game
