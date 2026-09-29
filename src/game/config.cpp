#include "game/config.h"

#include "data/gamefs.h"

#include <cstring>
#include <vector>

namespace st::game {

namespace {
constexpr size_t kSCnfSize = 0xe8;
constexpr size_t kNameLen = 26;
} // namespace

SlotConfig& slotConfig() {
    static SlotConfig instance;
    return instance;
}

bool cfgLoadSCnf(SlotConfig& out) {
    out = SlotConfig{};
    std::vector<u8> d;
    if (!gameFS().readFile("s.cnf", d) || d.size() < kSCnfSize) return false;
    out.lastSlot = s8(d[3]);
    for (size_t i = 0; i < 8; ++i) {
        out.slotUsed[i] = d[4 + i];
        const char* p = reinterpret_cast<const char*>(&d[0x0c + i * kNameLen]);
        out.names[i].assign(p, strnlen(p, kNameLen));
    }
    return true;
}

bool cfgSaveSCnf(const SlotConfig& cfg) {
    std::vector<u8> d;
    if (!gameFS().readFile("s.cnf", d) || d.size() < kSCnfSize) d.assign(kSCnfSize, 0);
    d[3] = u8(cfg.lastSlot);
    for (size_t i = 0; i < 8; ++i) {
        d[4 + i] = cfg.slotUsed[i];
        u8* p = &d[0x0c + i * kNameLen];
        std::memset(p, 0, kNameLen);
        std::memcpy(p, cfg.names[i].data(), std::min(cfg.names[i].size(), kNameLen - 1));
    }
    return gameFS().writeFile("s.cnf", d);
}

} // namespace st::game
