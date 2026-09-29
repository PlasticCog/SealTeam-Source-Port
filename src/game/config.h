// Settings files of the original: s.cnf (campaign slots) and st1.dfr
// (difficulty), docs/re/seg_365e_a.md 1.6 / 1.7.
#pragma once

#include "core/common.h"

#include <array>
#include <string>

namespace st::game {

// s.cnf, 232 bytes: image of far 5178:0060..0147.
struct SlotConfig {
    s8 lastSlot = -1;                     // +03: last used campaign slot, -1 none
    std::array<u8, 8> slotUsed{};         // +04
    std::array<std::string, 8> names;     // +0C: 8 x char[26]
};

bool cfgLoadSCnf(SlotConfig& out);        // cfg_load_s_cnf (365e:4897); missing file: lastSlot -1
bool cfgSaveSCnf(const SlotConfig& cfg);  // cfg_save_s_cnf

SlotConfig& slotConfig();

} // namespace st::game
