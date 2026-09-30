// Game data tables of the mission code, read from the user's st.exe at run
// time (never embedded in the port). Addresses are Ghidra seg:off / DS
// offsets (docs/re/CONVENTIONS.md).
//
// Modules that need a small table not listed here read it directly with
// st::exe() (data/exeimage.h) next to the code that uses it, citing the
// address; texts are read with dsText(offset).
#pragma once

#include "game/types.h"

#include <string>

namespace st::game::mission {

constexpr int kWeaponCount = 34;   // DS:48FE
constexpr int kItemCount = 12;     // far 52E3:0000
constexpr int kWoundEntries = 0x58;// far 525F:0000.. (5 tables of 17 plus the entries the scan can run into)

struct GameData {
    WeaponDef weapons[kWeaponCount]{};       // names point into the strings below
    std::string weaponShort[kWeaponCount];
    std::string weaponLong[kWeaponCount];
    ItemDef items[kItemCount]{};
    std::string itemShort[kItemCount];
    std::string itemLong[kItemCount];
    WoundTableEntry wounds[kWoundEntries]{};  // far 525F, read linearly
    s16 noiseLevel[0x14]{};                   // far 525C:0000, indexed by NoiseType
    s16 sinTable[721]{};                      // far 5327:000A, sin 0..90 deg in 1/8 deg, max 16383
    s16 atanTable[513]{};                     // far 52E7:0008, 8*atan(r/512) deg
    bool loaded = false;
};

const GameData& gd();
void loadGameData();  // idempotent; called by the mission init

inline const WeaponDef& weaponDef(int id) { return gd().weapons[(id >= 0 && id < kWeaponCount) ? id : 0]; }
inline const ItemDef& itemDef(int id) { return gd().items[(id >= 0 && id < kItemCount) ? id : 0]; }

// Wound table scan (19ac:7ffc / 7f58): starting at the entry index of the
// table (bullet bands 0x00/0x11/0x22, blast bands 0x33/0x44), return the bits
// of the first entry with lo <= roll <= hi. Each table ends with its own
// all-zero entry, so every roll 0..100 stops inside its own table.
u16 woundScan(int firstEntry, int roll);
constexpr int kWoundBullet0 = 0x00, kWoundBullet1 = 0x11, kWoundBullet2 = 0x22;
constexpr int kWoundBlast0 = 0x33, kWoundBlast1 = 0x44;

// NUL-terminated string at DS:offset in st.exe.
std::string dsText(u16 offset);
// String through the near pointer stored at DS:offset (tables of char*).
std::string dsTextPtr(u16 offset);

} // namespace st::game::mission
