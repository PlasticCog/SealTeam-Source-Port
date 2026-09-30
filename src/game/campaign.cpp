// Campaign data model (segment 365e, docs/re/seg_365e_a.md 2 and 8,
// docs/re/seg_365e_b.md 4 and 12). See campaign.h.
#include "game/campaign.h"

#include "data/ealib.h"
#include "data/exeimage.h"
#include "engine/rng.h"
#include "game/config.h"
#include "game/globals.h"

#include <algorithm>
#include <cstring>

namespace st::game {

// ---------------------------------------------------------------------------
// cN.cmp file format (types.h). The roster record's far SE pointer (+0x14) is
// run-time garbage in the file; it is kept in file_se_ptr so that loading and
// re-saving a file reproduces it byte for byte.
// ---------------------------------------------------------------------------

namespace {

void wr16(u8* p, u16 v) {
    p[0] = u8(v);
    p[1] = u8(v >> 8);
}
void wr32(u8* p, u32 v) {
    for (int i = 0; i < 4; ++i) p[i] = u8(v >> (8 * i));
}

} // namespace

bool parseCampaign(const u8* d, std::size_t size, CampaignHeader& h, std::vector<RosterEntry>& roster) {
    if (size < kCampaignHeaderSize) return false;
    h.slot = s8(d[0]);
    h.year = d[1];
    h.mission = d[2];
    h.last_won = d[3];
    h.point_man = rds16(d + 4);
    h.total_score = s32(rd32(d + 6));
    h.best_score = s32(rd32(d + 0x0a));
    std::memcpy(h.nickname, d + 0x0e, sizeof h.nickname);
    for (int i = 0; i < 24; ++i) {
        const u8* p = d + 0x2a + i * 12;
        CampaignHistory& e = h.history[i];
        e.mission = rd16(p);
        e.score = s32(rd32(p + 2));
        e.awards = rd16(p + 6);
        e.casualties = rd16(p + 8);
        e.rank = p[10];
        e.won = p[11];
    }
    h.pm_weapon_a = s8(d[0x14a]);
    h.pm_weapon_b = s8(d[0x14b]);
    roster.clear();
    const std::size_t n = std::min<std::size_t>((size - kCampaignHeaderSize) / kRosterEntrySize, kRosterMax);
    for (std::size_t i = 0; i < n; ++i) {
        const u8* p = d + kCampaignHeaderSize + i * kRosterEntrySize;
        RosterEntry e{};
        e.se_id = p[0];
        e.missions = p[1];
        e.wins = p[2];
        e.rank = p[3];
        e.awards = rd16(p + 4);
        e.unk_06 = rd16(p + 6);
        e.casualties = rd16(p + 8);
        std::memcpy(e.skill, p + 0x0a, 8);
        e.flags = p[0x12];
        e.unk_13 = p[0x13];
        e.se = nullptr;
        e.file_se_ptr = rd32(p + 0x14);
        roster.push_back(e);
    }
    return true;
}

void serializeCampaign(const CampaignHeader& h, const std::vector<RosterEntry>& roster, std::vector<u8>& out) {
    out.assign(kCampaignHeaderSize + roster.size() * kRosterEntrySize, 0);
    u8* d = out.data();
    d[0] = u8(h.slot);
    d[1] = h.year;
    d[2] = h.mission;
    d[3] = h.last_won;
    wr16(d + 4, u16(h.point_man));
    wr32(d + 6, u32(h.total_score));
    wr32(d + 0x0a, u32(h.best_score));
    std::memcpy(d + 0x0e, h.nickname, sizeof h.nickname);
    for (int i = 0; i < 24; ++i) {
        u8* p = d + 0x2a + i * 12;
        const CampaignHistory& e = h.history[i];
        wr16(p, e.mission);
        wr32(p + 2, u32(e.score));
        wr16(p + 6, e.awards);
        wr16(p + 8, e.casualties);
        p[10] = e.rank;
        p[11] = e.won;
    }
    d[0x14a] = u8(h.pm_weapon_a);
    d[0x14b] = u8(h.pm_weapon_b);
    for (std::size_t i = 0; i < roster.size(); ++i) {
        u8* p = d + kCampaignHeaderSize + i * kRosterEntrySize;
        const RosterEntry& e = roster[i];
        p[0] = e.se_id;
        p[1] = e.missions;
        p[2] = e.wins;
        p[3] = e.rank;
        wr16(p + 4, e.awards);
        wr16(p + 6, e.unk_06);
        wr16(p + 8, e.casualties);
        std::memcpy(p + 0x0a, e.skill, 8);
        p[0x12] = e.flags;
        p[0x13] = e.unk_13;
        wr32(p + 0x14, e.file_se_ptr);
    }
}

namespace campaign {

namespace {

// ---------------------------------------------------------------- st.exe tables

constexpr u16 kWeaponTable = 0x48fe;     // DS, 0x22 bytes per entry
constexpr int kWeaponRecord = 0x22;
constexpr u16 kToolSeg = 0x52e3;         // 6 bytes per entry
constexpr u16 kFlowPtrs = 0x3c5a;        // 4 far pointers
constexpr u16 kAwardSeg = 0x52bb;        // +2 promotions, +0x1a MoH records
constexpr int kMohCount = 9;
constexpr u16 kMissionNameTmpl = 0x1d14; // -> "cNmNN"
constexpr u16 kCampaignFileTmpl = 0x1d18;// -> "cN.cmp"
constexpr u16 kSealSeTmpl = 0x1d24;      // -> "sealNN"
constexpr u16 kVcSeTmpl = 0x1d26;        // -> "vcNN"
constexpr u16 kCivSeTmpl = 0x1d28;       // -> "civNN"
constexpr u16 kFrndSeTmpl = 0x1d2a;      // -> "frndNN"
constexpr u16 kSealSeExt = 0x3cea;       // ".se"
constexpr u16 kNpcSeExt = 0x3cee;        // ".se"
constexpr u16 kMciExt = 0x39c2;          // ".mci"
constexpr u16 kMtmExt = 0x39c7;          // ".mtm"
constexpr u16 kTextExt = 0x39bc;         // ".s"

struct Tables {
    bool loaded = false;
    std::vector<std::string> strings;  // storage for the name pointers
    WeaponDef weapons[kWeaponCount]{};
    ItemDef tools[kToolCount]{};
    FlowEntry flow[kYears][kMissionsPerYear]{};
    std::vector<MohRecord> moh;
};

Tables& tables() {
    static Tables t;
    if (t.loaded) return t;
    t.loaded = true;
    const ExeImage& x = exe();
    // Reserve so the c_str() pointers stay valid.
    t.strings.reserve(2 * (kWeaponCount + kToolCount));
    auto keep = [](std::string s) -> const char* {
        t.strings.push_back(std::move(s));
        return t.strings.back().c_str();
    };
    for (int i = 0; i < kWeaponCount; ++i) {
        const u16 b = u16(kWeaponTable + i * kWeaponRecord);
        WeaponDef& w = t.weapons[i];
        w.short_name = keep(x.dgStringPtr(b));
        w.long_name = keep(x.dgStringPtr(u16(b + 2)));
        w.wclass = WeaponClass(x.dgShort(u16(b + 4)));
        w.availability = x.dgWord(u16(b + 6));
        w.range_short = x.dgShort(u16(b + 8));
        w.range_medium = x.dgShort(u16(b + 0x0a));
        w.range_max = x.dgShort(u16(b + 0x0c));
        w.magazine = x.dgShort(u16(b + 0x0e));
        w.fire_modes = x.dgWord(u16(b + 0x10));
        w.blast_radius = x.dgShort(u16(b + 0x12));
        w.structure_damage = x.dgShort(u16(b + 0x14));
        w.noise = x.dgShort(u16(b + 0x16));
        w.weight = x.dgShort(u16(b + 0x18));
        w.reload_weight = x.dgShort(u16(b + 0x1a));
        w.jam = x.dgShort(u16(b + 0x1c));
        w.reload_ticks = x.dgShort(u16(b + 0x1e));
        w.default_reloads = x.dgShort(u16(b + 0x20));
    }
    for (int i = 0; i < kToolCount; ++i) {
        const u8* p = x.at(kToolSeg, u16(i * 6));
        if (!p) fatal("tool table missing from st.exe");
        ItemDef& it = t.tools[i];
        it.short_name = keep(x.dgString(rd16(p)));
        it.long_name = keep(x.dgString(rd16(p + 2)));
        it.weight = rds16(p + 4);
    }
    for (int y = 0; y < kYears; ++y) {
        const u16 off = x.dgWord(u16(kFlowPtrs + 4 * y));
        const u16 seg = u16(x.dgWord(u16(kFlowPtrs + 4 * y + 2)) + ExeImage::kLoadSeg);
        for (int m = 0; m < kMissionsPerYear; ++m) {
            const u8* p = x.at(seg, u16(off + m * 14));
            if (!p) fatal("campaign flow table missing from st.exe");
            FlowEntry& f = t.flow[y][m];
            f.stage = rd16(p);
            f.month = rd16(p + 2);
            f.day = rd16(p + 4);
            f.time_hhmm = rd16(p + 6);
            f.next_if_lost = rd16(p + 8);
            f.next_if_won = rd16(p + 0x0a);
            f.flags = rd16(p + 0x0c);
        }
    }
    for (int i = 0; i < kMohCount; ++i) {
        const u8* p = x.at(kAwardSeg, u16(0x1a + i * 6));
        if (!p) fatal("award table missing from st.exe");
        t.moh.push_back(MohRecord{rd16(p), rd16(p + 2), rd16(p + 4)});
    }
    return t;
}

// ---------------------------------------------------------------- state

struct RosterSlot {
    RosterEntry entry{};
    std::unique_ptr<SeRecord> se;
};

struct State {
    CampaignHeader header{};
    std::vector<std::unique_ptr<RosterSlot>> roster;
    std::vector<RosterEntry*> rosterView;
    LoadoutRecord loadout[kTeamSize]{};
    u8 loadoutTerminator = 0;  // 5178:0030
    MciHeader mci{};
    std::vector<MtmTeam> mtm;
    std::unique_ptr<SeRecord> npcCache[31];
    MissionStats stats;
    MissionResult result;
    bool fromMission = false;
};

State& st() {
    static State s;
    return s;
}

void rebuildView() {
    State& s = st();
    s.rosterView.clear();
    for (auto& r : s.roster) s.rosterView.push_back(&r->entry);
}

// "cNmNN"-style names: replace the digit placeholders of a template.
std::string exeTemplate(u16 ptr) { return exe().dgStringPtr(ptr); }

void copyField(char* dst, const u8* src, size_t n) { std::memcpy(dst, src, n); }

bool parseSeBytes(const std::vector<u8>& d, SeRecord& se) {
    if (d.size() < kSeSize) return false;
    const u8* p = d.data();
    copyField(se.first_name, p, 12);
    copyField(se.last_name, p + 0x0c, 20);
    copyField(se.nickname, p + 0x20, 26);
    copyField(se.birthplace, p + 0x3a, 32);
    se.height_ft = p[0x5a];
    se.height_in = p[0x5b];
    se.weight_lb = p[0x5c];
    se.buds_class = p[0x5d];
    se.rating = p[0x5e];
    se.age = p[0x5f];
    se.unk_60 = p[0x60];
    se.rank = p[0x61];
    se.camouflage = p[0x62];
    std::memcpy(se.weapons, p + 0x63, 8);
    std::memcpy(se.items, p + 0x6b, 8);
    se.unk_73 = p[0x73];
    std::memcpy(se.skill, p + 0x74, 8);
    se.size = p[0x7c];
    se.strength = p[0x7d];
    se.agility = p[0x7e];
    se.intelligence = p[0x7f];
    se.experience = p[0x80];
    std::memcpy(se.unk_81, p + 0x81, 11);
    se.load = rd16(p + 0x8c);
    se.unk_8e = rd16(p + 0x8e);
    return true;
}

std::unique_ptr<SeRecord> loadSe(const std::string& name) {
    std::vector<u8> d;
    if (!resources().read(name, d)) return nullptr;
    auto se = std::make_unique<SeRecord>();
    if (!parseSeBytes(d, *se)) return nullptr;
    return se;
}

// Writes two decimal digits of n at `pos` of s (s must be long enough).
void putTwoDigits(std::string& s, size_t pos, int n) {
    if (pos + 1 >= s.size()) s.resize(pos + 2, '0');
    s[pos] = char('0' + n / 10);
    s[pos + 1] = char('0' + n % 10);
}

void mciObjective(const u8* p, MciObjective& o, bool full) {
    o.kind = ObjectiveKind(rd16(p));
    o.pos = Vec3{s32(rd32(p + 2)), s32(rd32(p + 6)), s32(rd32(p + 10))};
    o.target_team = rds16(p + 0x0e);
    o.target_structure = rds16(p + 0x10);
    copyField(o.description, p + 0x12, 40);
    if (full) copyField(o.route, p + 0x3a, 40);
    else std::memset(o.route, 0, sizeof o.route);
}

bool parseMciBytes(const std::vector<u8>& d, MciHeader& m) {
    if (d.size() != kMciSize) return false;
    const u8* p = d.data();
    m.start_hour = rd16(p);
    m.start_minute = rd16(p + 2);
    m.world = rd16(p + 4);
    m.insertion_method = InsertionMethod(rd16(p + 6));
    m.extraction_method = InsertionMethod(rd16(p + 8));
    m.insertion = Vec3{s32(rd32(p + 0x0a)), s32(rd32(p + 0x0e)), s32(rd32(p + 0x12))};
    m.extraction = Vec3{s32(rd32(p + 0x16)), s32(rd32(p + 0x1a)), s32(rd32(p + 0x1e))};
    copyField(m.insertion_desc, p + 0x22, 40);
    copyField(m.extraction_desc, p + 0x4a, 40);
    m.fire_support = SupportUnit(rd16(p + 0x72));
    m.break_contact = SupportUnit(rd16(p + 0x74));
    mciObjective(p + 0x76, m.objective[0], true);
    mciObjective(p + 0xd8, m.objective[1], true);
    mciObjective(p + 0x13a, m.objective[2], false);
    m.mtm_count = rd16(p + 0x174);
    return true;
}

bool parseMtmBytes(const std::vector<u8>& d, std::vector<MtmTeam>& out) {
    out.clear();
    const size_t n = d.size() / kMtmTeamSize;
    for (size_t i = 0; i < n; ++i) {
        const u8* p = d.data() + i * kMtmTeamSize;
        MtmTeam t{};
        t.kind = MtmKind(rd16(p));
        t.start = Vec3{s32(rd32(p + 2)), s32(rd32(p + 6)), s32(rd32(p + 10))};
        t.member_count = rd16(p + 0x0e);
        for (int k = 0; k < 8; ++k) t.members[k] = rd16(p + 0x10 + 2 * k);
        t.formation = rd16(p + 0x20);
        t.order_type = AiOrderType(p[0x22]);
        t.behaviour = p[0x23];
        t.deploy_delay = rd16(p + 0x24);
        t.unk_26 = rd16(p + 0x26);
        t.heading = rd16(p + 0x28);
        for (int k = 0; k < 5; ++k) {
            const u8* w = p + 0x2a + 12 * k;
            t.waypoints[k] = Vec3{s32(rd32(w)), s32(rd32(w + 4)), s32(rd32(w + 8))};
        }
        out.push_back(t);
    }
    return n <= kMtmMaxTeams;
}

// History entry the unit-level helpers write for the leader of team 0:
// hist[roster.missions] (the original writes past the table for 24+).
CampaignHistory* historyFor(const RosterEntry& r) {
    if (r.missions >= 24) return nullptr;
    return &st().header.history[r.missions];
}

} // namespace

// ---------------------------------------------------------------- tables

const WeaponDef& weapon(int id) {
    if (id < 0 || id >= kWeaponCount) fatal("bad weapon id %d", id);
    return tables().weapons[id];
}

u8 weaponReloadsByte(int id) { return exe().dgByte(u16(kWeaponTable + 0x20 + id * kWeaponRecord)); }

const ItemDef& tool(int id) {
    if (id < 0 || id >= kToolCount) fatal("bad tool id %d", id);
    return tables().tools[id];
}

const FlowEntry& flow(int year, int mission) {
    return tables().flow[std::clamp(year, 0, kYears - 1)][std::clamp(mission, 0, kMissionsPerYear - 1)];
}

// Indexed by the current rank; rank 10 reads the word after the table (the
// code accepts ranks < 11), as the original does.
u16 promotionThreshold(int rank) {
    const u8* p = exe().at(kAwardSeg, u16(2 + 2 * rank));
    return p ? rd16(p) : 0xffff;
}

const std::vector<MohRecord>& mohRecords() { return tables().moh; }

// ---------------------------------------------------------------- header

CampaignHeader& header() { return st().header; }
bool& fromMission() { return st().fromMission; }

void stateInit(s8 slot, s16 pointMan) {
    CampaignHeader& h = header();
    h.slot = slot;
    h.mission = 0;
    h.year = 0;
    h.last_won = 1;
    h.point_man = pointMan;
    h.total_score = 0;  // only the dword at +6; best score and history are kept
    h.nickname[0] = 0;
}

void stateResetForMode() {
    const Globals& gs = g();
    switch (gs.gameMode) {
    case GameMode::Campaign: {
        int slot = lastSlot();
        if (slot == 8) slot = 0;
        stateInit(s8(slot), 0);
        break;
    }
    case GameMode::Menu:
    case GameMode::Practice:
        stateInit(-1, 0);
        break;
    case GameMode::Demo:
        stateInit(-1, 0);
        header().year = u8(gs.missionNo / 20);
        header().mission = u8(gs.missionNo % 20);
        break;
    }
}

bool missionWon(s16 score) { return result().success[0] != 0 && score > 0; }

void recordMission(s16 score) {
    CampaignHeader& h = header();
    const FlowEntry& f = flow(h.year, h.mission);
    RosterEntry* pm = rosterFind(h.point_man);
    if (!pm) return;
    h.total_score += score;
    const int idx = pm->missions++;
    bool won = missionWon(score);
    if (idx < 24) {
        CampaignHistory& e = h.history[idx];
        e.mission = u16(h.mission + h.year * 20);
        e.score = score;
        e.won = won ? 1 : 0;
    }
    if (score > h.best_score) h.best_score = score;
    if (won) {
        h.mission = u8(f.next_if_won);
        ++pm->wins;
    } else {
        h.mission = u8(f.next_if_lost);
    }
    if (h.mission == 0) {
        if (h.year == 3) h.mission = 0xff;  // campaign complete
        else ++h.year;
    }
    if (pm->flags & roster_flag::kKilled) h.mission = 0xff;
    if (rosterCountKia() > 10) h.mission = 0xff;
    h.last_won = won ? 1 : 0;
}

// ---------------------------------------------------------------- SE records

std::unique_ptr<SeRecord> loadSealSe(int seId) {
    std::string name = exeTemplate(kSealSeTmpl);  // "sealNN"
    putTwoDigits(name, 4, (seId + 1) % 100);
    return loadSe(name + exe().dgString(kSealSeExt));
}

SeRecord* npcSe(int index) {
    State& s = st();
    int slot = index;
    std::string name;
    int number;
    if (index < 21) {
        name = exeTemplate(kVcSeTmpl);   // vcNN
        number = index;
        name = name.substr(0, 2);
    } else if (index < 22) {
        name = exeTemplate(kCivSeTmpl);  // civNN
        number = index - 21;
        name = name.substr(0, 3);
    } else if (index < 31) {
        name = exeTemplate(kFrndSeTmpl); // frndNN
        number = index - 22;
        name = name.substr(0, 4);
    } else {
        name = exeTemplate(kVcSeTmpl).substr(0, 2);
        number = 0;
        slot = 0;  // cached as entry 0
    }
    if (slot < 0 || slot > 30) return nullptr;
    if (s.npcCache[slot]) return s.npcCache[slot].get();
    name.push_back(char('0' + (number + 1) / 10));
    name.push_back(char('0' + (number + 1) % 10));
    s.npcCache[slot] = loadSe(name + exe().dgString(kNpcSeExt));
    return s.npcCache[slot].get();
}

void npcSeCacheFree() {
    for (auto& p : st().npcCache) p.reset();
}

// ---------------------------------------------------------------- roster

const std::vector<RosterEntry*>& roster() { return st().rosterView; }

RosterEntry* rosterFind(int seId) {
    for (auto& r : st().roster)
        if (r->entry.se_id == u8(seId)) return &r->entry;
    return nullptr;
}

void rosterEntryInit(RosterEntry& e, std::unique_ptr<SeRecord>& se, int seId) {
    if (seId >= 0x31) return;
    e = RosterEntry{};
    e.se_id = u8(seId);
    se = loadSealSe(seId);
    e.se = se.get();
    if (!se) return;
    std::memcpy(e.skill, se->skill, 8);
    e.rank = se->rank;
}

// The original allows a 41st entry (count > 40 fails); the port stops at 40.
bool rosterAdd(int seId) {
    State& s = st();
    if (s.roster.size() >= kRosterMax) return false;
    auto slot = std::make_unique<RosterSlot>();
    rosterEntryInit(slot->entry, slot->se, seId);
    s.roster.push_back(std::move(slot));
    rebuildView();
    return true;
}

int poolBase() { return header().point_man < kPoolSize ? 0 : kPoolSize; }

void rosterNew(int pointMan) {
    rosterFree();
    header().slot = -1;
    if (pointMan < 0) return;
    const int base = pointMan < kPoolSize ? 0 : kPoolSize;
    for (int i = 0; i < kPoolSize; ++i) {
        rosterAdd(i + base);
        if (i > 17) {
            if (RosterEntry* e = rosterFind(i + base)) e->flags |= roster_flag::kReserve;
        }
    }
}

void rosterFree() {
    st().roster.clear();
    rebuildView();
}

int rosterActivateRecruit() {
    const int base = poolBase();
    for (int i = 0; i < kPoolSize; ++i) {
        RosterEntry* e = rosterFind(i + base);
        if (e && (e->flags & roster_flag::kReserve)) {
            e->flags = 0;
            return i;
        }
    }
    return -1;
}

int rosterSkillRating(const RosterEntry& e) {
    int sum = 0;
    for (u8 v : e.skill) sum += v;
    return sum >> 3;
}

int rosterCountKia() {
    int n = 0;
    for (auto& r : st().roster)
        if (r->entry.casualties & hit_bit::kKilled) ++n;
    return n;
}

void rosterClearWounded() {
    for (auto& r : st().roster) r->entry.flags &= u8(~roster_flag::kWounded);
}

// 365e:4124: OIC from base+9 (needs rank > 7), Corpsman from base+7, Rear
// Security from base + random(3); busy entries are skipped cyclically.
int rosterPickDefaultMember(int k) {
    const CampaignHeader& h = header();
    const int base = h.point_man < kPoolSize ? 0 : kPoolSize;
    int c;
    if (k == 1) c = base + 9;
    else if (k == 2) c = base + 7;
    else c = engine::rng().range(3) + base;
    int tries = 0;
    RosterEntry* e = rosterFind(c);
    for (;;) {
        if (h.point_man != c) {
            if (!e) return c;
            if (e->flags == 0 && (k != 1 || e->rank > 7)) return c;
        }
        c = (c - base + 1) % kPoolSize + base;
        e = rosterFind(c);
        if (tries + 1 > 23) {
            if (h.point_man != c) return c;
        } else {
            ++tries;
        }
    }
}

void rosterAddAwards(RosterEntry& r, bool isLeader, u16 bits) {
    r.awards |= bits;
    if (isLeader)
        if (CampaignHistory* h = historyFor(r)) h->awards = bits;  // assigned: last award wins
}

void rosterSetRank(RosterEntry& r, bool isLeader, int rank) {
    if (rank < 0 || rank >= 12) return;
    if (rank == 1) rank = 2;
    if (r.se) r.se->rank = u8(rank);
    r.rank = u8(rank);
    if (isLeader)
        if (CampaignHistory* h = historyFor(r)) h->rank = u8(rank);
}

void rosterRecordCasualty(RosterEntry& r, bool isLeader, u16 bits) {
    r.casualties |= bits;
    if (bits & hit_bit::kKilled) {
        // The original also ORs 1 << (year + 8) into this byte, which is
        // always 0 after the truncation (365e:4A61).
        r.flags |= roster_flag::kKilled;
    } else if (bits != 0) {
        r.flags |= roster_flag::kWounded;
    }
    if (isLeader)
        if (CampaignHistory* h = historyFor(r)) h->casualties = bits;
}

// ---------------------------------------------------------------- save slots

namespace {
std::string campaignFileName(int slot) {
    std::string name = exeTemplate(kCampaignFileTmpl);  // "cN.cmp"
    if (name.size() > 1) name[1] = char('0' + slot);
    return name;
}
} // namespace

bool load(int slot) {
    if (slot < 0 || slot > 8) return false;
    std::vector<u8> d;
    if (!saveFileRead(campaignFileName(slot), d)) return false;
    std::vector<RosterEntry> entries;
    CampaignHeader h{};
    if (!parseCampaign(d.data(), d.size(), h, entries)) return false;
    State& s = st();
    s.header = h;
    s.header.slot = s8(slot);
    s.roster.clear();
    for (const RosterEntry& e : entries) {
        auto rs = std::make_unique<RosterSlot>();
        rs->entry = e;
        rs->se = loadSealSe(e.se_id);
        rs->entry.se = rs->se.get();
        if (rs->se) rs->se->rank = e.rank;
        s.roster.push_back(std::move(rs));
    }
    rebuildView();
    return true;
}

bool save(int slot) {
    if (slot < 0 || slot > 8) return false;
    State& s = st();
    s.header.slot = s8(slot);
    std::vector<RosterEntry> entries;
    for (auto& r : s.roster) entries.push_back(r->entry);
    std::vector<u8> d;
    serializeCampaign(s.header, entries, d);
    saveFileWrite(campaignFileName(slot), d);
    if (slot < 8) slotConfig().slotUsed[size_t(slot)] = 1;
    return true;
}

void loadAutosave() {
    const s8 slot = header().slot;
    load(8);
    header().slot = slot;
}

void saveAutosave() {
    const s8 slot = header().slot;
    save(8);
    header().slot = slot;
}

bool slotUsed(int slot) { return slot >= 0 && slot < 8 && slotConfig().slotUsed[size_t(slot)] != 0; }
int lastSlot() { return slotConfig().lastSlot; }

void setLastSlot(int slot) {
    slotConfig().lastSlot = s8(slot);
    cfgSaveSCnf(slotConfig());
}

bool deleteSlot(int slot) {
    if (slot < 0 || slot > 8) return false;
    SlotConfig& c = slotConfig();
    if (slot < 8) c.slotUsed[size_t(slot)] = 0;
    if (c.lastSlot == slot) {
        c.lastSlot = -1;
        for (int i = 0; i < 8; ++i)
            if (c.slotUsed[size_t(i)]) c.lastSlot = s8(i);
    }
    cfgSaveSCnf(c);
    return true;
}

std::string slotName(int slot) {
    if (slot < 0 || slot > 7) return {};
    return slotConfig().names[size_t(slot)];
}

void setSlotName(int slot, const std::string& name) {
    if (slot < 0 || slot > 7) return;
    slotConfig().names[size_t(slot)] = name.substr(0, 25);
}

// ---------------------------------------------------------------- loadout

LoadoutRecord* loadout() { return st().loadout; }

// 365e:4B57. Candidates step from w (exclusive) towards `up`, inside 0..28;
// the first one allowed in the year that matches the slot class wins.
int loadoutNextWeapon(int w, int year, bool up, int cls) {
    u8 mask = 0;
    for (int i = 0; i <= year; ++i) mask = u8(mask | (1 << i));
    int c = w;
    if (up && w <= 27) ++c;
    else if (!up && w >= 0) --c;
    for (;;) {
        if (c < 0 || c > 28) return c == -1 ? -1 : w;
        const WeaponDef& d = weapon(c);
        const bool thrown = u8(d.fire_modes) == fire_mode::kThrow;
        if ((u8(d.availability) & mask) != 0) {
            const bool throwClass = (cls == 2 || cls > 2) && thrown && c != int(WeaponId::Demo);
            const bool gunClass = (cls == 1 || cls > 2) && (!thrown || c == int(WeaponId::Demo));
            if (throwClass || gunClass) return c;
        }
        c += up ? 1 : -1;
    }
}

int loadoutFixWeaponForYear(int w) {
    if (w == -1) return w;
    const int year = header().year;
    return loadoutNextWeapon(loadoutNextWeapon(w, year, false, 3), year, true, 3);
}

int findObjectiveTarget(int kind) {
    int found = -1;
    for (const MciObjective& o : mci().objective) {
        if (int(o.kind) != kind) continue;
        if ((kind == 2 || kind == 6) && o.target_team != -1) found = o.target_team;
        else if (kind == 3 && o.target_structure != -1) found = o.target_structure;
    }
    return found;
}

// 365e:422F. Reload counts come from the weapon table byte even for empty
// slots (-1 reads the byte before the table), and the point man's campaign
// weapons replace slots A/B without updating their reloads - both as in the
// original.
void loadoutBuildMember(int k, int seId) {
    RosterEntry* e = rosterFind(seId);
    if (!e) {
        rosterAdd(seId);
        e = rosterFind(seId);
    }
    if (!e) return;
    e->flags |= roster_flag::kInTeam;
    const SeRecord* se = e->se;
    static const SeRecord kEmpty{};
    if (!se) se = &kEmpty;
    LoadoutRecord& r = loadout()[k];
    r.se_id = s8(seId);
    r.camouflage = se->camouflage;
    r.weapons[0] = se->weapons[0];
    if (k > 0) r.weapons[0] = s8(loadoutFixWeaponForYear(r.weapons[0]));
    r.reloads[0] = weaponReloadsByte(r.weapons[0]);
    r.weapons[1] = s8(loadoutFixWeaponForYear(se->weapons[1]));
    r.reloads[1] = weaponReloadsByte(r.weapons[1]);
    r.tools[1] = 1;  // two Medical Kits
    r.tools[0] = 1;
    r.weapons[2] = se->weapons[2];
    if (k > 0) r.weapons[2] = s8(loadoutFixWeaponForYear(r.weapons[2]));
    r.reloads[2] = weaponReloadsByte(r.weapons[2]);
    r.weapons[3] = se->weapons[3];
    if (k > 0) r.weapons[3] = s8(loadoutFixWeaponForYear(r.weapons[3]));
    r.reloads[3] = weaponReloadsByte(r.weapons[3]);
    const GameMode mode = g().gameMode;
    constexpr s8 kDemo = s8(WeaponId::Demo);
    if (k == 0) {
        if (mode != GameMode::Demo && mode != GameMode::Practice) {
            r.weapons[0] = header().pm_weapon_a;
            r.weapons[1] = header().pm_weapon_b;
        }
        int d = -1;
        for (const MciObjective& o : mci().objective) {
            if (int(o.kind) >= 1 && o.kind == ObjectiveKind(3)) {
                d = kDemo;
                break;
            }
        }
        if (d == -1) d = se->weapons[3];
        r.weapons[3] = s8(d);
        r.reloads[3] = weaponReloadsByte(s8(d));
        if (findObjectiveTarget(6) != -1) r.tools[0] = 2;  // PHK for a Snatch
        r.tools[1] = 0;                                    // PRC25 radio
    } else if (k == 1) {
        r.weapons[1] = 16;  // M18 smoke
        r.reloads[1] = weaponReloadsByte(16);
        if (findObjectiveTarget(3) != -1) {
            r.weapons[2] = kDemo;
            r.reloads[2] = weaponReloadsByte(kDemo);
        }
        r.weapons[3] = 4;   // M39
        r.reloads[3] = weaponReloadsByte(4);
    } else {
        if (findObjectiveTarget(3) != -1) {
            r.weapons[3] = kDemo;
            r.reloads[3] = weaponReloadsByte(kDemo);
        }
        if (findObjectiveTarget(2) != -1) {
            r.weapons[2] = 4;  // M39
            r.reloads[2] = weaponReloadsByte(4);
        }
        if (k == 2) r.tools[0] = 2;  // Corpsman: PHK
    }
}

void loadoutBuildTeam() {
    for (int k = 0; k < kTeamSize; ++k) {
        const int id = k == 0 ? header().point_man : rosterPickDefaultMember(k);
        loadoutBuildMember(k, id);
    }
    st().loadoutTerminator = 0xff;
}

void loadoutReleaseTeam() {
    for (int k = 0; k < kTeamSize; ++k) {
        LoadoutRecord& r = loadout()[k];
        if (RosterEntry* e = rosterFind(r.se_id)) e->flags &= u8(~roster_flag::kInTeam);
        r.se_id = -1;
        r.weapons[0] = -1;
    }
}

// 365e:5CEE: adds weapon, reload and tool weights of record k to the member's
// load (SE+0x8C); each addition is clamped at 0 (unit_add_load 19ac:640C).
int loadoutComputeWeight(int k) {
    const LoadoutRecord& r = loadout()[k];
    RosterEntry* e = rosterFind(r.se_id);
    if (!e || !e->se) return 0;
    SeRecord& se = *e->se;
    auto add = [&se](int v) {
        const int n = s16(se.load) + v;
        se.load = u16(n < 0 ? 0 : n);
    };
    for (int i = 0; i < 4; ++i) {
        const int w = r.weapons[i];
        if (w != -1) {
            const WeaponDef& d = weapon(w);
            add(d.weight);
            const int reloads = s8(r.reloads[i]);  // read as a signed byte
            add(reloads * (d.reload_weight == 0 ? d.weight : d.reload_weight));
        }
        if (i < 2 && r.tools[i] != -1) add(tool(r.tools[i]).weight);
    }
    return se.load;
}

// ---------------------------------------------------------------- mission

std::string missionBaseName(int missionNo) {
    std::string name = exeTemplate(kMissionNameTmpl);  // "cNmNN"
    if (name.size() < 5) name.resize(5, '0');
    name[1] = char('1' + missionNo / 20);
    putTwoDigits(name, 3, missionNo % 20 + 1);
    return name;
}

bool missionLoad() {
    State& s = st();
    const std::string base = missionBaseName(g().missionNo);
    std::vector<u8> d;
    if (!resources().read(base + exe().dgString(kMciExt), d) || !parseMciBytes(d, s.mci))
        fatal("Not enough memory.");  // the original's message for any load error (4eac:001c)
    if (!resources().read(base + exe().dgString(kMtmExt), d) || !parseMtmBytes(d, s.mtm))
        fatal("Not enough memory.");
    return true;
}

MciHeader& mci() { return st().mci; }
std::vector<MtmTeam>& mtm() { return st().mtm; }
void missionFreeMtm() { st().mtm.clear(); }

bool loadTextLines(const std::string& name, std::vector<std::string>& lines, size_t lineLen) {
    lines.clear();
    std::vector<u8> d;
    if (!resources().read(name + exe().dgString(kTextExt), d)) return false;
    for (size_t off = 0; off < d.size(); off += lineLen) {
        const char* p = reinterpret_cast<const char*>(d.data() + off);
        lines.emplace_back(p, strnlen(p, std::min(lineLen, d.size() - off)));
    }
    return true;
}

bool loadMissionText(int missionNo, std::vector<std::string>& lines) {
    return loadTextLines(missionBaseName(missionNo), lines);
}

bool loadSealChatter(int seId, std::vector<SealChatterLine>& lines) {
    lines.clear();
    std::string name = exeTemplate(kSealSeTmpl);  // "sealNN"
    putTwoDigits(name, 4, (seId + 1) % 100);
    std::vector<u8> d;
    if (!resources().read(name + exe().dgString(0x39bf), d)) return false;  // ".s"
    for (size_t off = 0; off + kSealChatterSize <= d.size(); off += kSealChatterSize) {
        SealChatterLine l{};
        std::memcpy(l.text, d.data() + off, sizeof l.text);
        l.year_from = d[off + 0x50];
        l.tier_from = d[off + 0x51];
        l.year_to = d[off + 0x52];
        l.tier_to = d[off + 0x53];
        lines.push_back(l);
    }
    return true;
}

// ---------------------------------------------------------------- scoring

MissionStats& stats() { return st().stats; }
MissionResult& result() { return st().result; }

void scoreResetStats() {
    MissionStats& s = stats();
    s.bonus = 0;
    s.roundsHit = 0;
    s.roundsFired = 0;
    s.grenadesHit = 0;
    s.grenadesThrown = 0;
    s.score = 0;
}

namespace {

// score_try_medal (365e:DCEA): signed comparisons; the lesser medal goes to
// each of the first team_size members (the point man included) with 30 %.
void tryMedal(const ScoreInput& in, int a, int b, int c, int score, int prev, int total, u16 m1, u16 m2,
              bool& done, int teamSize) {
    if (!(a <= score && b <= prev && c <= total)) return;
    if (in.team.empty() || !in.team[0].roster) return;
    rosterAddAwards(*in.team[0].roster, true, m1);
    done = true;
    if (m2 == 0) return;
    for (int i = 0; i < teamSize; ++i) {
        if (engine::rng().range(100) < 30 && i < int(in.team.size()) && in.team[size_t(i)].roster)
            rosterAddAwards(*in.team[size_t(i)].roster, i == 0, m2);
    }
}

// score_train_skill (365e:DD6F).
void trainSkill(u8& skill) {
    const int r = engine::rng().range(100);
    if (r < 90 - int(skill)) {
        skill = u8(skill + engine::rng().range(3) + 1);
        if (skill > 90) skill = 90;
    }
}

} // namespace

void scoreMission(const ScoreInput& in, const ScoreContext& ctx) {
    MissionStats& S = stats();
    MissionResult& R = result();
    bool medal = false;
    S.teamSize = u16(in.team.size());
    const u16 hit = S.roundsHit, fired = S.roundsFired;
    R.roundsFired = fired;
    R.roundsHit = hit;

    // score_survival_points (365e:DE0A)
    int alive = 0;
    for (const ScoreUnit& u : in.team)
        if (!u.dead) ++alive;
    s16 score = s16((!in.team.empty() && !in.team[0].dead) ? 200 : 0);
    score = s16(score + (S.teamSize == u16(alive) ? 50 : (alive - int(S.teamSize)) * 100));
    if (in.noCraftLost) score = s16(score + 35);
    score = s16(score + hit * 5);

    if (hit != 0 && fired != 0) {  // (the original divides by zero when nothing was fired)
        const u16 p = u16(u16(u32(hit) * 100) / fired);
        if (p == 100) score = s16(score + 300);
        else if (p >= 80 && p <= 99) score = s16(score + 200);
        else if (p >= 60 && p <= 79) score = s16(score + 175);
        else if (p >= 45 && p <= 59) score = s16(score + 150);
        else if (p > 32 && p < 45) score = s16(score + 100);
    }
    if (S.grenadesHit != 0 && S.grenadesThrown != 0) {
        const u16 q = u16(S.grenadesHit / S.grenadesThrown);
        if (q >= 10) score = s16(score + 300);
        else if (q >= 7) score = s16(score + 200);
        else if (q >= 5) score = s16(score + 150);
        else if (q >= 3) score = s16(score + 100);
    }
    const int o1 = in.objective[0], o2 = in.objective[1], o3 = in.objective[2];
    R.success[0] = u8(o1);
    R.success[1] = u8(o2);
    R.success[2] = u8(o3);
    score = s16(score + in.deadVc * 20 + in.deadNva * 45 - in.deadCivilians * 90 + o3 * 100 + (o1 * 2 + o2) * 150);
    if (o1 && o2) {
        score = s16(score + 500);
        if (o3) score = s16(score + 250);
    }
    // score_snatch_casualties returns -penalty per dead target and the caller
    // subtracts it, so dead Snatch targets add points (365e:DDA9, kept).
    const int penalty[3] = {200, 100, 50};
    for (int i = 0; i < 3; ++i) {
        const int v = mci().objective[i].kind == ObjectiveKind(6) ? -penalty[i] * in.snatchTargetsDead[i] : 0;
        score = s16(score - v);
    }
    int minutes = in.missionMinutes * (mci().objective[0].kind == ObjectiveKind(1) ? 35 : 20);
    if (s16(minutes) > 200) minutes = 200;
    score = s16(score + in.destroyedObjects * 100 + s16(minutes) + in.capturedEnemies * 100 + S.bonus * 250);
    if (!in.teamAllExtracted || score < 0) score = 0;
    S.score = score;

    RosterEntry* leader = in.team.empty() ? nullptr : in.team[0].roster;
    // Promotion of the point man (rank 10 reads the word after the table).
    if (leader && ctx.rank < 11 && promotionThreshold(ctx.rank) <= u16(score + ctx.totalScore))
        rosterSetRank(*leader, true, leader->rank + 1);
    // Field promotions: the first `alive` members, dead or not.
    if (u16(S.score) > 2499) {
        for (int i = 0; i < alive; ++i) {
            const int r = engine::rng().range(100);
            if (r < 30 && i < int(in.team.size()) && in.team[size_t(i)].roster) {
                RosterEntry& e = *in.team[size_t(i)].roster;
                rosterSetRank(e, i == 0, e.rank + 1);
            }
        }
    }
    // Purple Hearts.
    for (size_t i = 0; i < S.teamSize && i < in.team.size(); ++i)
        if (in.team[i].wounded && in.team[i].roster) rosterAddAwards(*in.team[i].roster, i == 0, award::kPurpleHeart);
    // Medal of Honor.
    for (const MohRecord& m : mohRecords()) {
        if (m.year == ctx.year && m.mission == ctx.mission && m.min_score <= u16(S.score) && leader) {
            rosterAddAwards(*leader, true, award::kMedalOfHonor);
            medal = true;
        }
    }
    const int teamSize = S.teamSize;
    const int sc = S.score, prev = ctx.prevScore, total = ctx.totalScore;
    if (!medal) tryMedal(in, 3600, 3200, 10200, sc, prev, total, award::kNavyCross, award::kSilverStar, medal, teamSize);
    if (!medal) tryMedal(in, 3100, 2500, 0, sc, prev, total, award::kSilverStar, award::kBronzeStar, medal, teamSize);
    if (!medal) tryMedal(in, 2850, 2200, 0, sc, prev, total, award::kBronzeStar, award::kNavyAchievement, medal, teamSize);
    if (!medal) tryMedal(in, 2400, 0, 0, sc, prev, total, award::kNavyAchievement, award::kNavyCommendation, medal, teamSize);
    if (!medal) tryMedal(in, 1850, 0, 0, sc, prev, total, award::kNavyCommendation, 0, medal, teamSize);
    // Unit citations for the last two missions of a year. The caller always
    // passes a year KIA count of 0, so this is always the Presidential Unit
    // Citation (quirk kept).
    if ((ctx.mission == 18 || ctx.mission == 19) && leader && ctx.year >= 0 && ctx.year <= 3) {
        if (ctx.yearKia == 1 || ctx.yearKia == 2) rosterAddAwards(*leader, true, u16(award::kNavalUnitCitation << ctx.year));
        if (ctx.yearKia == 0) rosterAddAwards(*leader, true, u16(award::kPresidentialUnitCitation << ctx.year));
    }
    // Skill training after a successful primary objective.
    if (S.score != 0 && R.success[0] != 0) {
        for (size_t i = 0; i < S.teamSize && i < in.team.size(); ++i) {
            if (!in.team[i].roster) continue;
            for (u8& s : in.team[i].roster->skill) trainSkill(s);
        }
    }
}

void awardEvaluateMission(const ScoreInput& in) {
    MissionResult& R = result();
    R.roundsFired = R.roundsHit = 0;
    R.success[0] = R.success[1] = R.success[2] = 0;
    const CampaignHeader& h = header();
    ScoreContext ctx;
    const RosterEntry* leader = in.team.empty() ? nullptr : in.team[0].roster;
    int i = leader ? leader->missions - 1 : 0;
    if (i < 0) i = 0;
    ctx.prevScore = s16(h.history[std::min(i, 23)].score);
    ctx.rank = leader ? leader->rank : 0;
    ctx.totalScore = s16(h.total_score);
    ctx.year = h.year;
    ctx.mission = h.mission;
    ctx.yearKia = 0;
    scoreMission(in, ctx);
}

} // namespace campaign
} // namespace st::game
