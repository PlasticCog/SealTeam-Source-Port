// Settings files of the original: s.cnf (campaign slots) and st1.dfr
// (difficulty), docs/re/seg_365e_a.md 1.6 / 1.7, and where the port writes
// the files the game saves (s.cnf, st1.dfr, cN.cmp).
#pragma once

#include "core/common.h"
#include "game/types.h"

#include <array>
#include <string>
#include <vector>

namespace st::game {

// s.cnf, 232 bytes: image of far 5178:0060..0147. `raw` keeps the whole
// record (including the bytes without a reader) so re-saves are byte-exact;
// the named members mirror it.
struct SlotConfig {
    s8 lastSlot = -1;                     // +03: last used campaign slot, -1 none
    std::array<u8, 8> slotUsed{};         // +04
    std::array<std::string, 8> names;     // +0C: 8 x char[26]
    ConfigFile raw{};                     // full record (unk bytes kept)
};

bool cfgLoadSCnf(SlotConfig& out);        // cfg_load_s_cnf (365e:4897); missing file: lastSlot -1
bool cfgSaveSCnf(const SlotConfig& cfg);  // cfg_save_s_cnf (365e:487B)
SlotConfig& slotConfig();

// st1.dfr (far 5178:0148, 16 bytes).
DifficultyOptions& difficulty();
bool cfgLoadDifficulty();                 // cfg_load_difficulty (365e:48F1); keeps the defaults if missing
bool cfgSaveDifficulty();                 // diff_handle_input 's' (365e:E7EC)

// Files the game writes go to the Game folder like the original, unless a
// save directory is set (tests: --save-dir, or the SEALTEAM_SAVE_DIR
// environment variable). Reads look in the save directory first.
void setSaveDir(const std::string& dir);
const std::string& saveDir();
bool saveFileRead(const std::string& dosName, std::vector<u8>& out);
bool saveFileWrite(const std::string& dosName, const std::vector<u8>& data);

} // namespace st::game
