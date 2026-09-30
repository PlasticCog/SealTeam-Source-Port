#include "game/mission/entity.h"

#include "data/ealib.h"
#include "game/mission/ai.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/people.h"
#include "game/mission/state.h"
#include "game/mission/world.h"

#include <cstdio>
#include <cstring>

namespace st::game::mission {

namespace {
std::function<RosterEntry*(int)> g_rosterLookup;
} // namespace

// ---------------------------------------------------------------------------
// Allocation (4511)
// ---------------------------------------------------------------------------

Unit* entAlloc(unsigned flags) {
    MissionState& S = ms();
    Unit* u = S.unitPool.alloc();
    if (flags & 0x01) u->status = S.statusPool.alloc();
    if (flags & 0x02) u->mover = S.moverPool.alloc();
    if (flags & 0x04) u->anim = S.animPool.alloc();
    if (flags & 0x08) u->brain = S.brainPool.alloc();
    return u;
}

Team* ent2Alloc(unsigned flags) {
    MissionState& S = ms();
    Team* t = S.teamPool.alloc();
    if (flags & 0x08) t->ai = S.teamAiPool.alloc();
    return t;
}

void entAddWaypoint(Team* t, const Vec3& pos) {
    if (!t || !t->ai) return;
    WaypointNode* n = ms().waypointPool.alloc();
    n->pos = pos;
    if (!t->ai->waypoints) {
        t->ai->waypoints = n;
        t->ai->cur_waypoint = n;
        return;
    }
    WaypointNode* tail = t->ai->waypoints;
    while (tail->next) tail = tail->next;
    tail->next = n;
    n->prev = tail;
}

void entClearWaypoints(Team* t) {
    if (!t || !t->ai) return;
    t->ai->waypoints = nullptr;
    t->ai->cur_waypoint = nullptr;
}

// ---------------------------------------------------------------------------
// Construction (365e)
// ---------------------------------------------------------------------------

Team* entGroupCreate(TeamType type, int formation, int fireOrder, int order, unsigned flags) {
    MissionState& S = ms();
    int count = 0;
    while (count < kMaxTeams && S.teams[count]) ++count;
    if (count >= kMaxTeams) fatal("mission: team table full");
    Team* t = ent2Alloc(flags);
    S.teams[count] = t;
    S.teamCount++;
    t->type = type;
    t->formation = Formation(formation);
    t->fire_order = FireOrder(fireOrder);
    t->order = s16(order);
    t->map_height = type == TeamType::Seal ? 0x3E800 : 0x7D000;
    t->view_distance = 0x60;
    t->view_heading = -1;
    if (flags & 0x08) {
        if (S.firstMtmGroup == 0xFF) {
            aiGroupOrdersReset(t);
        } else {
            const size_t idx = size_t(count - S.firstMtmGroup);
            const MtmTeam* rec = idx < S.mtm.size() ? &S.mtm[idx] : nullptr;
            aiGroupOrdersFromMission(t, rec, count, S.firstMtmGroup);
        }
    }
    S.teams[count + 1] = nullptr;
    return t;
}

Unit* entGroupAddUnit(Team* t, unsigned flags, UnitClass cls, int se, s32 x, s32 z) {
    if (!t) return nullptr;
    se = s8(se);  // passed as a byte (365e:04CF / 0557: MOV AL,[BP+0Eh]; CBW)
    int n = 0;
    while (n < 8 && t->members[n]) ++n;
    Unit* u = entAlloc(flags);
    if (n < 8) t->members[n] = u;
    u->team = t;
    u->buddy = nullptr;
    unitSetClassWord(u, cls, se);
    if (cls == UnitClass::Boat || cls == UnitClass::Helicopter) se = -1;
    u->body = worldAddObject(u->model, x, z, 0x0101);
    registerUnitBody(u);  // grp_seglist_add
    if (flags & 0x02) evtInitMover(u, cls);
    if (flags & 0x04) sprInitUnitAnim(u, int(cls));
    if (flags & 0x01) entUnitInitPersonnel(u, cls, se);
    if (flags & 0x08) aiMindAttach(u, t);
    if (n + 1 < 8) {
        t->members[n + 1] = nullptr;
    } else {
        // Original quirk (365e:0584..0594): the NULL terminator after the
        // eighth member lands on +0x20/+0x22 (formation, fire order).
        t->formation = Formation(0);
        t->fire_order = FireOrder(0);
    }
    return u;
}

void entWeaponInit(WeaponNode* n, int weapon, int reloads) {
    if (!n || weapon == -1) return;
    n->type = WeaponId(s8(weapon));
    const WeaponDef& w = weaponDef(weapon & 0xFF);
    n->rounds = w.magazine;
    n->reloads = s16(reloads == -1 ? 8 : reloads);
    n->jammed = 0;
    n->unk_0a = 3;
    const u8 modes = u8(w.fire_modes);
    n->fire_mode = modes;
    if (modes & 0x0F) {
        // Prefer Semi, then Single, Full, Grenade.
        n->fire_mode = modes & fire_mode::kSemi;
        if (!n->fire_mode) n->fire_mode = modes & fire_mode::kSingle;
        if (!n->fire_mode) n->fire_mode = modes & fire_mode::kFull;
        if (!n->fire_mode) n->fire_mode = modes & fire_mode::kLauncher;
    }
}

WeaponNode* entUnitAddWeapon(Unit* u, int weapon, int reloads) {
    if (!u) return nullptr;
    if (weapon == -1) {
        weapon = 0;
        reloads = 0;
    }
    MissionState& S = ms();
    WeaponNode* n = nullptr;
    if (!u->loadout) {
        u->loadout = S.loadoutPool.alloc();
        n = S.weaponPool.alloc();
        u->loadout->list = n;
        u->loadout->primary = n;
        u->loadout->secondary = nullptr;
    } else {
        WeaponNode* tail = u->loadout->list;
        if (!tail) return nullptr;
        while (tail->next) tail = tail->next;
        n = S.weaponPool.alloc();
        tail->next = n;
        if (!u->loadout->secondary) u->loadout->secondary = n;
    }
    entWeaponInit(n, weapon, reloads);
    n->next = nullptr;
    return n;
}

void entItemInit(ItemNode* n, int item, int quantity) {
    if (!n || item == -1) return;
    n->type = ItemType(s8(item));
    n->quantity = s16(quantity == -1 ? 8 : quantity);
}

ItemNode* entUnitAddItem(Unit* u, int item, int quantity) {
    if (!u) return nullptr;
    if (item == -1) {
        item = 0;
        quantity = 0;
    }
    MissionState& S = ms();
    ItemNode* n = S.itemPool.alloc();
    if (!u->items) {
        u->items = n;
    } else {
        ItemNode* tail = u->items;
        while (tail->next) tail = tail->next;
        tail->next = n;
    }
    entItemInit(n, item, quantity);
    n->next = nullptr;
    return n;
}

namespace {
// Copy of SE +0x74..+0x8F (28 bytes) into the status block.
void copyStatus(Status* s, const SeRecord* se) {
    for (int i = 0; i < 8; ++i) s->skill[i] = se->skill[i];
    s->size = se->size;
    s->strength = se->strength;
    s->agility = se->agility;
    s->intelligence = se->intelligence;
    s->experience = se->experience;
    s->unk_0d = se->unk_81[0];
    s->hit_mask = u16(se->unk_81[1] | (se->unk_81[2] << 8));
    s->unk_10 = u16(se->unk_81[3] | (se->unk_81[4] << 8));
    s->light_wounds = se->unk_81[5];
    s->heavy_wounds = se->unk_81[6];
    s->bleeding = se->unk_81[7];
    s->unk_15 = se->unk_81[8];
    s->bleed_time = s16(se->unk_81[9] | (se->unk_81[10] << 8));
    s->load = se->load;
    s->unit_class = UnitClass(s16(se->unk_8e));
}
} // namespace

void entUnitInitPersonnel(Unit* u, UnitClass cls, int se) {
    Status* s = nullptr;
    unitInitBody(u, cls);
    if (se == -1) return;
    if (int(cls) < 3) {
        RosterEntry* r = rosterFind(se);
        if (r && r->se) {
            u->roster = r;
            u->se = r->se;
            s = u->status;
            copyStatus(s, r->se);
            for (int i = 0; i < 8; ++i) s->skill[i] = r->skill[i];
            s->bleeding = 0;
            unitAddExperience(s, r->missions);
        }
    } else {
        if (cls == UnitClass::Civilian) se = 0x15;
        else if (cls == UnitClass::Friendly) se = 0x16;
        SeRecord* rec = npcSeLoad(se);
        if (rec) {
            u->roster = nullptr;
            u->se = rec;
            s = u->status;
            copyStatus(s, rec);
            rec->camouflage = u8(se + 1);
            s->bleeding = 0;
        }
    }
    if (s) {
        s->hit_mask = 0;
        s->unk_10 = 0;
        s->light_wounds = 0;
        s->heavy_wounds = 0;
        s->bleeding = 0;
        s->load = 0;
    }
}

void setRosterLookup(std::function<RosterEntry*(int se)> fn) { g_rosterLookup = std::move(fn); }

RosterEntry* rosterFind(int se) { return g_rosterLookup ? g_rosterLookup(se) : nullptr; }

void decodeSe(const u8* p, SeRecord& out) {
    std::memcpy(out.first_name, p + 0x00, 12);
    std::memcpy(out.last_name, p + 0x0C, 20);
    std::memcpy(out.nickname, p + 0x20, 26);
    std::memcpy(out.birthplace, p + 0x3A, 32);
    out.height_ft = p[0x5A];
    out.height_in = p[0x5B];
    out.weight_lb = p[0x5C];
    out.buds_class = p[0x5D];
    out.rating = p[0x5E];
    out.age = p[0x5F];
    out.unk_60 = p[0x60];
    out.rank = p[0x61];
    out.camouflage = p[0x62];
    for (int i = 0; i < 8; ++i) out.weapons[i] = s8(p[0x63 + i]);
    for (int i = 0; i < 8; ++i) out.items[i] = s8(p[0x6B + i]);
    out.unk_73 = p[0x73];
    for (int i = 0; i < 8; ++i) out.skill[i] = p[0x74 + i];
    out.size = p[0x7C];
    out.strength = p[0x7D];
    out.agility = p[0x7E];
    out.intelligence = p[0x7F];
    out.experience = p[0x80];
    std::memcpy(out.unk_81, p + 0x81, 11);
    out.load = rd16(p + 0x8C);
    out.unk_8e = rd16(p + 0x8E);
}

bool loadSeFile(const char* name, SeRecord& out) {
    std::vector<u8> data;
    if (!resources().read(name, data) || data.size() < kSeSize) return false;
    decodeSe(data.data(), out);
    return true;
}

SeRecord* npcSeLoad(int i) {
    MissionState& S = ms();
    if (i < 0 || i >= kNpcSeCache) i = 0;  // >= 31 -> vc01 (entry 0)
    if (S.npcSeCache[size_t(i)]) return S.npcSeCache[size_t(i)];
    char name[16];
    if (i <= 20) std::snprintf(name, sizeof name, "vc%02d.se", i + 1);
    else if (i == 21) std::snprintf(name, sizeof name, "civ01.se");
    else std::snprintf(name, sizeof name, "frnd%02d.se", i - 21);
    SeRecord* rec = S.sePool.alloc();
    if (!loadSeFile(name, *rec)) {
        logWarn("mission: %s not found", name);
        return nullptr;
    }
    S.npcSeCache[size_t(i)] = rec;
    return rec;
}

void npcSeCacheClear() {
    for (auto& p : ms().npcSeCache) p = nullptr;
}

// ---------------------------------------------------------------------------
// Unit and team helpers (19ac)
// ---------------------------------------------------------------------------

void unitSetClassWord(Unit* u, UnitClass cls, int se) {
    u16 m = 0x862A;  // human
    switch (cls) {
    case UnitClass::Boat: m = s16(se) == 0 ? 0x897E : 0x8DB0; break;          // lssc / mike2
    case UnitClass::Helicopter: m = s16(se) == 0 ? 0x726A : 0xC5B2; break;    // cobra / uh1
    case UnitClass::Aircraft: m = 0x992C; break;                               // ov10a
    default: break;
    }
    u->model = model(m);
}

void unitShow(Unit* u) {
    if (u && u->body) u->body->flags |= 0x0101;
}

void unitHideChain(Unit* u) {
    if (!u) return;
    if (u->body) u->body->flags &= 0xFEFE;
    if (u->buddy) {
        Unit* b = u->buddy;
        b->buddy = nullptr;
        unitHideChain(b);
    }
}

int unitIndexInGroup(const Unit* u) {
    if (!u || !u->team) return -1;
    for (int i = 0; i < 8 && u->team->members[i]; ++i)
        if (u->team->members[i] == u) return i;
    return -1;
}

int grpIndex(const Team* t) {
    if (!t) return -1;
    for (int i = 0; i < kMaxTeams && ms().teams[i]; ++i)
        if (ms().teams[i] == t) return i;
    return -1;
}

int grpCountAlive(const Team* t) {
    if (!t) return -1;
    int n = 0;
    for (int i = 0; i < 8 && t->members[i]; ++i)
        if (!(t->members[i]->status->hit_mask & hit_bit::kKilled)) ++n;
    return n;
}

int sealCountAlive() {
    int n = 0;
    for (int i = 0; i < kMaxTeams && ms().teams[i]; ++i)
        if (ms().teams[i]->type == TeamType::Seal) n += grpCountAlive(ms().teams[i]);
    return n;
}

int unitLoadLevel(const Status* s) {
    const int w = s16(s->load) / 10;
    const int c = (s->size >> 1) + s->strength;
    if (c < w) return 3;
    if ((c >> 1) < w) return 2;
    if ((c >> 2) < w) return 1;
    return 0;
}

void unitAddLoad(Status* s, int n) {
    s16 v = s16(s->load + n);
    if (v < 0) v = 0;
    s->load = u16(v);
    unitLoadLevel(s);
}

int grpNearestEnemyDist(const Team* t, const Vec3& pos) {
    int best = 3000;
    if (!t) return best;
    const int mine = int(t->type);
    for (int i = 0; i < kMaxTeams && ms().teams[i]; ++i) {
        const Team* o = ms().teams[i];
        const int other = int(o->type);
        bool hostile;
        if (mine < 4 || mine > 5) hostile = other >= 4 && other <= 5;
        else hostile = other < 4 || other == 7;
        if (!hostile) continue;
        const int d = geoDistance(pos, o->members[0]->body->pos);
        if (d <= best) best = d;
    }
    return best;
}

void unitAddExperience(Status* s, int n) {
    s->experience = u8(s->experience + n * 2);
    if (s->experience > 0x5A) s->experience = 0x5A;
}

bool unitInitBody(Unit* u, UnitClass cls) {
    if (!u || !u->status) return false;
    Status* s = u->status;
    const int c = int(cls);
    if (c < 0 || (c > 4 && c != 8)) {
        s->size = 200;
        s->strength = 0x32;
        s->agility = 0x3C;
        s->intelligence = 0x46;
        s->experience = 1;
        s->skill[2] = 5;
        s->skill[0] = 0x0F;
        s->skill[4] = 0x0F;
        s->skill[1] = 10;
        s->skill[3] = 10;
        s->skill[5] = 10;
        s->skill[6] = 0x14;
        s->skill[7] = 0x1E;
    }
    s->hit_mask = 0;
    s->light_wounds = 0;
    s->heavy_wounds = 0;
    s->bleeding = 0;
    s->unit_class = cls;
    return true;
}

int teamMemberCount(const Team* t) {
    int n = 0;
    while (t && n < 8 && t->members[n]) ++n;
    return n;
}

int teamTableCount() {
    int n = 0;
    while (n < kMaxTeams && ms().teams[n]) ++n;
    return n;
}

} // namespace st::game::mission
