#include "game/mission/exedata.h"

#include "data/exeimage.h"

namespace st::game::mission {

namespace {

GameData g_data;

constexpr u16 kWeaponTable = 0x48FE;
constexpr u16 kItemSeg = 0x52E3;
constexpr u16 kWoundSeg = 0x525F;
constexpr u16 kNoiseSeg = 0x525C;
constexpr u16 kSinSeg = 0x5327, kSinOff = 0x000A;
constexpr u16 kAtanSeg = 0x52E7, kAtanOff = 0x0008;

const u8* need(u16 seg, u16 off) {
    const u8* p = exe().at(seg, off);
    if (!p) fatal("st.exe table %04X:%04X missing", seg, off);
    return p;
}

} // namespace

const GameData& gd() { return g_data; }

void loadGameData() {
    if (g_data.loaded) return;
    GameData& d = g_data;
    for (int i = 0; i < kWeaponCount; ++i) {
        const u8* p = need(ExeImage::kDGroup, u16(kWeaponTable + i * 0x22));
        d.weaponShort[i] = exe().dgString(rd16(p + 0x00));
        d.weaponLong[i] = exe().dgString(rd16(p + 0x02));
        WeaponDef& w = d.weapons[i];
        w.wclass = WeaponClass(rds16(p + 0x04));
        w.availability = rd16(p + 0x06);
        w.range_short = rds16(p + 0x08);
        w.range_medium = rds16(p + 0x0A);
        w.range_max = rds16(p + 0x0C);
        w.magazine = rds16(p + 0x0E);
        w.fire_modes = rd16(p + 0x10);
        w.blast_radius = rds16(p + 0x12);
        w.structure_damage = rds16(p + 0x14);
        w.noise = rds16(p + 0x16);
        w.weight = rds16(p + 0x18);
        w.reload_weight = rds16(p + 0x1A);
        w.jam = rds16(p + 0x1C);
        w.reload_ticks = rds16(p + 0x1E);
        w.default_reloads = rds16(p + 0x20);
    }
    for (int i = 0; i < kWeaponCount; ++i) {
        d.weapons[i].short_name = d.weaponShort[i].c_str();
        d.weapons[i].long_name = d.weaponLong[i].c_str();
    }
    for (int i = 0; i < kItemCount; ++i) {
        const u8* p = need(kItemSeg, u16(i * 6));
        d.itemShort[i] = exe().dgString(rd16(p + 0));
        d.itemLong[i] = exe().dgString(rd16(p + 2));
        d.items[i].weight = rds16(p + 4);
    }
    for (int i = 0; i < kItemCount; ++i) {
        d.items[i].short_name = d.itemShort[i].c_str();
        d.items[i].long_name = d.itemLong[i].c_str();
    }
    for (int i = 0; i < kWoundEntries; ++i) {
        const u8* p = need(kWoundSeg, u16(i * 4));
        d.wounds[i] = WoundTableEntry{p[0], p[1], rd16(p + 2)};
    }
    for (int i = 0; i < 0x14; ++i) d.noiseLevel[i] = rds16(need(kNoiseSeg, u16(i * 2)));
    for (int i = 0; i <= 720; ++i) d.sinTable[i] = rds16(need(kSinSeg, u16(kSinOff + i * 2)));
    for (int i = 0; i <= 512; ++i) d.atanTable[i] = rds16(need(kAtanSeg, u16(kAtanOff + i * 2)));
    d.loaded = true;
}

u16 woundScan(int firstEntry, int roll) {
    for (int i = firstEntry; i < kWoundEntries; ++i) {
        const WoundTableEntry& e = g_data.wounds[i];
        if (e.lo <= roll && roll <= e.hi) return e.bits;
    }
    return 0;
}

std::string dsText(u16 offset) { return exe().dgString(offset); }
std::string dsTextPtr(u16 offset) { return exe().dgStringPtr(offset); }

} // namespace st::game::mission
