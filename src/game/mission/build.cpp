#include "game/mission/build.h"

#include "data/ealib.h"
#include "data/exeimage.h"
#include "engine/rng.h"
#include "game/mission/ai.h"
#include "game/mission/combat.h"
#include "game/mission/craft.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/modern.h"
#include "game/mission/msg.h"
#include "game/mission/people.h"
#include "game/mission/sfx.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/mission/world.h"
#include "game/mission/wquery.h"

#include <cstdio>
#include <cstring>

namespace st::game::mission {

namespace {

constexpr u16 kMciExt = 0x39C2;             // ".mci"
constexpr u16 kMtmExt = 0x39C7;             // ".mtm"
constexpr u16 kCraftReloadsByYear = 0x1356; // s16[4] 7,6,5,4
constexpr u16 kEnemyReloadsByYear = 0x134E; // s16[4] 5,6,7,8

// Objective messages (DS offsets).
constexpr u16 kMsgPatrol = 0x3A1D, kMsgAmbush = 0x3A2F, kMsgDemolition = 0x39E5, kMsgObserve = 0x39D2,
              kMsgRescue = 0x3A53, kMsgSnatch = 0x3A41, kMsgRecover = 0x39FB;

s16 craftReloads() { return exe().dgShort(u16(kCraftReloadsByYear + (ms().year & 3) * 2)); }
s16 enemyReloads() { return exe().dgShort(u16(kEnemyReloadsByYear + (ms().year & 3) * 2)); }

s32 mapUnits(s32 v) { return v >> 8; }

s32 rd32s(const u8* p) { return s32(rd32(p)); }

void readVec(const u8* p, Vec3& v) {
    v.x = rd32s(p);
    v.y = rd32s(p + 4);
    v.z = rd32s(p + 8);
}

void readObjective(const u8* p, MciObjective& o, bool full) {
    o.kind = ObjectiveKind(rd16(p));
    readVec(p + 2, o.pos);
    o.target_team = rds16(p + 0x0E);
    o.target_structure = rds16(p + 0x10);
    std::memcpy(o.description, p + 0x12, 40);
    if (full) std::memcpy(o.route, p + 0x3A, 40);
    else std::memset(o.route, 0, 40);
}

WorldObject* worldObjectAt(int n) {
    auto& v = worldObjects();
    return (n >= 0 && n < int(v.size())) ? v[size_t(n)] : nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool msnLoad(int missionNo) {
    MissionState& S = ms();
    S.missionNo = missionNo;
    char base[16];
    std::snprintf(base, sizeof base, "c%cm%02d", char('1' + missionNo / 20), missionNo % 20 + 1);
    std::vector<u8> mci, mtm;
    if (!resources().read(std::string(base) + dsText(kMciExt), mci) || mci.size() != kMciSize) {
        logError("mission: %s.mci missing or wrong size", base);
        return false;
    }
    if (!resources().read(std::string(base) + dsText(kMtmExt), mtm) || mtm.size() / kMtmTeamSize > kMtmMaxTeams) {
        logError("mission: %s.mtm missing or too large", base);
        return false;
    }
    MciHeader& h = S.mci;
    const u8* p = mci.data();
    h.start_hour = rd16(p + 0x00);
    h.start_minute = rd16(p + 0x02);
    h.world = rd16(p + 0x04);
    h.insertion_method = InsertionMethod(rd16(p + 0x06));
    h.extraction_method = InsertionMethod(rd16(p + 0x08));
    readVec(p + 0x0A, h.insertion);
    readVec(p + 0x16, h.extraction);
    std::memcpy(h.insertion_desc, p + 0x22, 40);
    std::memcpy(h.extraction_desc, p + 0x4A, 40);
    h.fire_support = SupportUnit(rd16(p + 0x72));
    h.break_contact = SupportUnit(rd16(p + 0x74));
    readObjective(p + 0x76, h.objective[0], true);
    readObjective(p + 0xD8, h.objective[1], true);
    readObjective(p + 0x13A, h.objective[2], false);
    h.mtm_count = rd16(p + 0x174);
    S.mtm.clear();
    for (size_t off = 0; off + kMtmTeamSize <= mtm.size(); off += kMtmTeamSize) {
        const u8* r = mtm.data() + off;
        MtmTeam t{};
        t.kind = MtmKind(rd16(r + 0x00));
        readVec(r + 0x02, t.start);
        t.member_count = rd16(r + 0x0E);
        for (int i = 0; i < 8; ++i) t.members[i] = rd16(r + 0x10 + 2 * i);
        t.formation = rd16(r + 0x20);
        t.order_type = AiOrderType(r[0x22]);
        t.behaviour = r[0x23];
        t.deploy_delay = rd16(r + 0x24);
        t.unk_26 = rd16(r + 0x26);
        t.heading = rd16(r + 0x28);
        for (int i = 0; i < 5; ++i) readVec(r + 0x2A + 12 * i, t.waypoints[i]);
        S.mtm.push_back(t);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------

Team* entSpawnSealTeam() {
    MissionState& S = ms();
    Team* t = entGroupCreate(TeamType::Seal, 0, 2, -1, 8);
    const s32 x = mapUnits(S.mci.insertion.x), z = mapUnits(S.mci.insertion.z);
    for (int k = 0; k < 4; ++k) {
        const LoadoutRecord& r = S.loadout[size_t(k)];
        Unit* u = entGroupAddUnit(t, k == 0 ? 0x17 : 0x1F, UnitClass::Seal, r.se_id, x, z);
        for (int i = 0; i < 4; ++i)
            if (r.weapons[i] != -1) entUnitAddWeapon(u, r.weapons[i], s8(r.reloads[i]));
        for (int i = 0; i < 2; ++i)
            if (r.tools[i] != -1) entUnitAddItem(u, r.tools[i], 1);
    }
    return t;
}

Team* entSpawnMtmTeam(MtmTeam& rec) {
    MissionState& S = ms();
    TeamType type;
    UnitClass cls;
    switch (u16(rec.kind)) {
    case 0: type = TeamType::Civilian; cls = UnitClass::Civilian; break;
    case 2: type = TeamType::NvArmy; cls = UnitClass::NvArmy; break;
    case 3: type = TeamType::Friendly; cls = UnitClass::Friendly; break;
    default: type = TeamType::VietCong; cls = UnitClass::VietCong; break;
    }
    Team* t = entGroupCreate(type, rec.formation, 2, 0, 8);
    // Team Size: Decreased removes one more member; every setting removes one.
    if (S.opt.team_size != 2 && S.opt.team_size == 0) {
        int c = s16(rec.member_count) - 1;
        rec.member_count = u16(c < 1 ? 1 : c);
    }
    int c = s16(rec.member_count) - 1;
    rec.member_count = u16(c < 1 ? 1 : c);
    const s32 x = mapUnits(rec.start.x), z = mapUnits(rec.start.z);
    const s16 reloads = enemyReloads();
    for (int i = 0; i < s16(rec.member_count) && i < 8; ++i) {
        int se = s16(rec.members[i]);
        if (se > 0x15) se = 0x15;
        if (se < 1) se = 1;
        Unit* u = entGroupAddUnit(t, 0x1F, cls, s8(se - 1), x, z);
        const SeRecord* sr = u->se;
        entUnitAddWeapon(u, sr ? sr->weapons[0] : -1, reloads);
        int k = 1;
        for (; k < 8; ++k) {
            if (!sr || sr->weapons[k] == -1) break;
            entUnitAddWeapon(u, sr->weapons[k], reloads >> 1);
        }
        if (k == 1) entUnitAddWeapon(u, 0x1A, -1);
        k = 0;
        for (; k < 8; ++k) {
            if (!sr || sr->items[k] == -1) break;
            entUnitAddItem(u, sr->items[k], 1);
        }
        if (k == 0) entUnitAddItem(u, 5, 1);
        if (i == 0) u->body->heading = Angle8(s16(rec.heading) << 3);
        if (u8(rec.order_type) == 3) {
            evtSetPosture(u, engine::rng().range(2) + 1);
            sprSetAnim(u, u8(u->mover->posture));
            unitHideChain(u);
        } else {
            evtSetPosture(u, engine::rng().range(2));
            sprSetAnim(u, u8(u->mover->posture));
        }
        if (type == TeamType::Friendly) {
            evtSetPosture(u, 0);
            sprSetAnim(u, u8(u->mover->posture));
            u->brain->surrendered = 1;
        }
    }
    return t;
}

void entUnitPushBackFromObjective(Unit* u, int dist) {
    MissionState& S = ms();
    const int a = angleWrap(s16((geoBearing(u->body->pos, S.mci.objective[0].pos) + 0xB4) << 3));
    posMovePolar(s32(s16(dist)) << 8, 0, a, u->body->pos);
    u->mover->destination = u->body->pos;
}

Team* entSpawnInsertionCraft(int method, s32 x, s32 z) {
    MissionState& S = ms();
    const s16 reloads = craftReloads();
    const int insMethod = int(u16(S.mci.insertion_method));
    Team* t = nullptr;
    if (method == 8) {
        t = entGroupCreate(TeamType::Helicopter, 3, 2, 4, 8);
        Unit* u = entGroupAddUnit(t, 0x1B, UnitClass::Helicopter, -1, x, z);
        if (insMethod == 8) {
            // Hovering insertion helicopter, destination 2700 units beyond the objective.
            u->body->pos.y = 0x600;
            u->mover->height = 6;
            Mover* mv = u->mover;
            const Vec3& obj = S.mci.objective[0].pos;
            mv->destination.x = wrapAdd(mv->destination.x, (mv->destination.x >= obj.x ? 0xA8C : -0xA8C) * 256);
            mv->destination.z = wrapAdd(mv->destination.z, (mv->destination.z >= obj.z ? 0xA8C : -0xA8C) * 256);
        } else {
            entUnitPushBackFromObjective(u, 0xA8C);
        }
        entUnitAddWeapon(u, 6, reloads);
        entUnitAddWeapon(u, 0x20, reloads);
        S.heloGroup = S.teamCount - 1;
        S.extractionGroup = S.heloGroup;
        S.emergencyGroup = S.heloGroup;
    } else if (method == 1 || method == 2 || method == 4) {
        t = entGroupCreate(TeamType::Boat, 3, 2, 4, 8);
        const s32 bx = x + (method == insMethod ? 0 : 0x546);
        // Method 1 uses the LSSC model (se 0), the others the Mike boat (se -1).
        Unit* u = entGroupAddUnit(t, 0x1B, UnitClass::Boat, method == 1 ? 0 : -1, bx, z);
        evtSetDestToTerrainObj(u, nullptr);
        if (method != insMethod) u->body->pos = u->mover->destination;
        Mover* mv = u->mover;
        mv->desired_heading = s16(geoBearing(u->body->pos, mv->destination));
        mv->heading = mv->desired_heading;
        u->body->heading = Angle8(mv->heading << 3);
        posMovePolar(0xB400, 0, u->body->heading, u->body->pos);
        entUnitAddWeapon(u, 6, reloads);
        entUnitAddWeapon(u, 9, reloads);
        S.extractionGroup = S.teamCount - 1;
        S.boatGroup = S.extractionGroup;
    }
    return t;
}

Team* entSpawnSupportCraft(int kind, s32 x, s32 z) {
    MissionState& S = ms();
    const s16 reloads = craftReloads();
    if (kind == 1) {
        Team* t = entGroupCreate(TeamType::Aircraft, 3, 2, 4, 8);
        Unit* a = entGroupAddUnit(t, 0x1B, UnitClass::Aircraft, -1, x, z);
        entUnitAddWeapon(a, 0x1E, reloads);
        entUnitAddWeapon(a, 0x1E, reloads);
        a->body->roll = 0xAF0;
        entUnitPushBackFromObjective(a, 0x1A5E);
        Unit* b = entGroupAddUnit(t, 0x1B, UnitClass::Aircraft, -1, x + 600, z);
        entUnitAddWeapon(b, 0x1E, reloads);
        entUnitAddWeapon(b, 6, reloads);
        b->body->roll = 0xAF0;
        entUnitPushBackFromObjective(b, 0x1A5E);
        S.airGroup = S.teamCount - 1;
        return t;
    }
    if (kind == 8) {
        Team* t = entGroupCreate(TeamType::Helicopter, 3, 2, 4, 8);
        Unit* a = entGroupAddUnit(t, 0x1B, UnitClass::Helicopter, -1, x, z);
        entUnitAddWeapon(a, 6, reloads);
        entUnitAddWeapon(a, 0x1E, reloads);
        entUnitPushBackFromObjective(a, 0xA8C);
        Unit* b = entGroupAddUnit(t, 0x1B, UnitClass::Helicopter, 0, x + 600, z);
        entUnitAddWeapon(b, 6, reloads);
        entUnitAddWeapon(b, 6, reloads);
        entUnitPushBackFromObjective(b, 0xA8C);
        S.heloGroup = S.teamCount - 1;
        S.emergencyGroup = S.teamCount - 1;
        return t;
    }
    if (kind != 0 && kind != 2 && kind != 4) return nullptr;
    Team* t = entGroupCreate(TeamType::Boat, 0, 2, 4, 8);
    Unit* a = entGroupAddUnit(t, 0x1B, UnitClass::Boat, -1, x + 0x546, z);
    evtSetDestToTerrainObj(a, nullptr);
    a->body->pos = a->mover->destination;
    entUnitAddWeapon(a, 0x1D, reloads);
    entUnitAddWeapon(a, 6, reloads);
    Unit* b = entGroupAddUnit(t, 0x1B, UnitClass::Boat, -1, x + 0x79E, z);
    evtSetDestToTerrainObj(b, nullptr);
    b->body->pos = b->mover->destination;
    entUnitAddWeapon(b, 6, reloads);
    entUnitAddWeapon(b, 0x20, reloads);
    S.boatGroup = S.teamCount - 1;
    return t;
}

Team* entSpawnHiddenGrenadier() {
    MissionState& S = ms();
    const int saved = S.firstMtmGroup;
    S.firstMtmGroup = 0xFF;
    Team* t = entGroupCreate(TeamType::VietCong, 0, 2, 0, 8);
    S.firstMtmGroup = saved;
    t->ai->order_type = AiOrderType::Reserve;
    t->ai->behaviour = ai_behaviour::kGuard;
    t->ai->deploy_timer = 0x70800;
    Unit* u = entGroupAddUnit(t, 0x1F, UnitClass::VietCong, 0, 35, 35);
    entUnitAddWeapon(u, 0x13, -1);   // AK47
    entUnitAddWeapon(u, 0x0E, -1);   // M26
    evtSetPosture(u, 2);
    u->brain->flags = 0;
    unitHideChain(u);
    S.proxyGrenadier = u;
    return t;
}

void entUnitFaceObjective(Unit* u) {
    u->body->heading = Angle8(geoBearing(u->body->pos, ms().mci.objective[0].pos) << 3);
}

void msnBuildWorld() {
    MissionState& S = ms();
    S.mapSelTeam = 0;
    S.teamCount = 0;
    S.firstMtmGroup = 0xFF;
    S.sealTeam = 0;
    entSpawnSealTeam();
    evtTeamSnapFormation(S.sealTeam);
    S.extractingGroup = 0xFF;
    S.breakContactGroup = 0xFF;
    S.fireSupportGroup = 0xFF;
    S.emergencyGroup = 0xFF;
    S.extractionGroup = 0xFF;
    S.insertionGroup = 0xFF;
    S.boatGroup = 0xFF;
    S.airGroup = 0xFF;
    S.heloGroup = 0xFF;
    S.splitGroups = 0;
    const s32 ix = mapUnits(S.mci.insertion.x), iz = mapUnits(S.mci.insertion.z);
    const s32 ex = mapUnits(S.mci.extraction.x), ez = mapUnits(S.mci.extraction.z);
    entSpawnInsertionCraft(int(u16(S.mci.insertion_method)), ix, iz);
    S.insertionGroup = S.teamCount - 1;
    evtTeamSnapFormation(S.insertionGroup);
    int craft = 1;
    if (S.mci.insertion_method == S.mci.extraction_method) {
        Team* c = team(S.extractionGroup);
        if (c && c->type == TeamType::Boat) c->members[0]->mover->destination = S.mci.extraction;
    } else {
        entSpawnInsertionCraft(int(u16(S.mci.extraction_method)), ex, ez);
        craft = 2;
    }
    evtTeamSnapFormation(S.extractionGroup);
    if (u16(S.mci.fire_support) != 0) {
        entSpawnSupportCraft(int(u16(S.mci.fire_support)), ix, iz);
        S.fireSupportGroup = S.teamCount - 1;
        evtTeamSnapFormation(S.fireSupportGroup);
        ++craft;
    }
    if (craft < 3 && u16(S.mci.break_contact) != 0) {
        entSpawnSupportCraft(int(u16(S.mci.break_contact)), ix + 0x4B0, iz + 0x4B0);
        S.breakContactGroup = S.teamCount - 1;
        evtTeamSnapFormation(S.breakContactGroup);
    }
    if (S.emergencyGroup == 0xFF) entSpawnSupportCraft(8, ex, ez);
    if (S.fireSupportGroup == 0xFF) S.fireSupportGroup = S.emergencyGroup;
    modernSpawnPhantomFlight(ix, iz);  // port: Modern gameplay Phantom flight (no-op while off)
    npcSeCacheClear();
    S.firstMtmGroup = S.teamCount;
    for (int i = 0; i < int(S.mci.mtm_count) && i < int(S.mtm.size()); ++i) {
        entSpawnMtmTeam(S.mtm[size_t(i)]);
        evtTeamSnapFormation(S.firstMtmGroup + i);
    }
    entSpawnHiddenGrenadier();
    Team* ec = team(S.extractionGroup);
    S.extractionType = ec ? int(ec->type) : 4;
    S.searchTeam = nullptr;
}

void misScanSpecials() {
    MissionState& S = ms();
    S.hasSpecialObject = false;
    const ModelDesc* tunnel = model(0xC4B2);
    for (const WorldObject* w : worldObjects())
        if (w->model == tunnel) S.hasSpecialObject = true;
    S.hasDemoObjective = msnFindObjectiveTarget(3, nullptr) != -1;
    S.hasAmbushObjective = msnFindObjectiveTarget(2, nullptr) != -1;
    entMarkEnemiesOnStructures();
}

void mapInitMarkers() {
    MissionState& S = ms();
    const MciHeader& h = S.mci;
    S.routeNodes[0] = h.insertion;
    S.routeNodes[1] = h.objective[0].pos;
    const s16 k2 = s16(u16(h.objective[1].kind));
    S.routeNodes[2] = (k2 == 0 || k2 == -1) ? h.extraction : h.objective[1].pos;
    S.routeNodes[3] = h.extraction;
    S.wpSeal = h.objective[0].pos;
    S.wpSplitA = h.objective[0].pos;
    S.wpSplitB = h.objective[0].pos;
    S.wpSupport = h.insertion;
    S.extractMarker = h.extraction;
}

// ---------------------------------------------------------------------------
// Run-time team helpers
// ---------------------------------------------------------------------------

bool entGroupIsPassiveNpc(const Team* t) {
    return t && int(t->type) > 4 && t->fire_order == FireOrder::FieldOfFire;
}

int entGroupSplit(int teamIndex) {
    MissionState& S = ms();
    Team* src = team(teamIndex);
    if (!src) return -1;
    const int n = teamMemberCount(src);
    if (src->type != TeamType::Seal || n < 2) return -1;
    const int saved = S.firstMtmGroup;
    S.firstMtmGroup = 0xFF;
    Team* t = entGroupCreate(src->type, int(src->formation), int(src->fire_order), src->order, 8);
    S.firstMtmGroup = saved;
    int dst = 0;
    for (int i = (n + 1) >> 1; i < n; ++i) {
        Unit* u = src->members[i];
        t->members[dst] = u;
        u->team = t;
        if (u->brain) u->brain->team = t;
        if (dst + 1 < 8) t->members[dst + 1] = nullptr;
        src->members[i] = nullptr;
        ++dst;
    }
    S.splitGroups++;
    return S.teamCount - 1;
}

int entGroupMerge(int a, int b) {
    MissionState& S = ms();
    Team* ta = team(a);
    Team* tb = team(b);
    if (!tb || !ta || ta->type != TeamType::Seal || tb->type != TeamType::Seal) return -1;
    int n = teamMemberCount(ta);
    for (int i = 0; i < 8 && tb->members[i]; ++i) {
        Unit* u = tb->members[i];
        if (n < 8) ta->members[n] = u;
        u->team = ta;
        if (u->brain) u->brain->team = ta;
        if (n + 1 < 8) ta->members[n + 1] = nullptr;
        tb->members[i] = nullptr;
        ++n;
    }
    S.teams[b] = nullptr;
    S.teamCount--;
    S.splitGroups--;
    return a;
}

void entTeamRejoin() {
    MissionState& S = ms();
    Team* sel = team(S.mapSelTeam);
    // A selected SEAL team (possibly a split team about to be merged) falls back to team 0.
    if (sel && sel->type == TeamType::Seal) S.mapSelTeam = 0;
    if (S.splitGroups == 2) entGroupMerge(S.sealTeam, S.teamCount - 1);
    if (S.splitGroups == 1) entGroupMerge(S.sealTeam, S.teamCount - 1);
}

void entOrderExtraction(const Vec3& pos) {
    MissionState& S = ms();
    S.extractingGroup = S.extractionGroup;
    if (S.emergencyGroup != 0xFF) S.extractingGroup = S.emergencyGroup;
    Team* t = team(S.extractingGroup);
    if (!t) return;
    t->order = s16(CraftOrder::EmergencyExtract);
    S.wpSupport = pos;
    t->members[0]->mover->destination = pos;
    entTeamRejoin();
}

bool entAnyCraftMoving() {
    for (int i = 0; i < kMaxTeams && ms().teams[i]; ++i) {
        const Team* t = ms().teams[i];
        if ((t->type == TeamType::Boat || t->type == TeamType::Helicopter) && (t->order == 3 || t->order == 2)) return true;
    }
    return false;
}

void entGroupReplaceLeader(Team* t, Unit* u) {
    if (!t || t->members[0] != u) return;
    for (int i = 1; i < 8 && t->members[i]; ++i) {
        Unit* m = t->members[i];
        if (!(m->status->hit_mask & hit_bit::kKilled) && !(m->mover->flags & mover_flag::kSecured)) {
            t->members[0] = m;
            t->members[i] = u;
            return;
        }
    }
}

bool entGroupHasSecuredMember(int teamIndex) {
    if (teamIndex >= ms().teamCount) return false;
    const Team* t = team(teamIndex);
    if (!t) return false;
    for (int i = 0; i < 8 && t->members[i]; ++i)
        if (t->members[i]->mover->flags & mover_flag::kSecured) return true;
    return false;
}

bool entTeamAllExtracted() {
    const Team* t = team(0);
    if (!t) return true;
    for (int i = 0; i < 8 && t->members[i]; ++i)
        if (!(t->members[i]->mover->flags & mover_flag::kAboard)) return false;
    return true;
}

bool entTeamEnemySighted() {
    const TargetRec& tr = ms().playerTarget;
    if (tr.kind == TargetKind::Unit && tr.target.unit && tr.target.unit->team && isEnemyTeam(tr.target.unit->team))
        return true;
    const Team* t = team(0);
    if (!t) return false;
    for (int i = 1; i < 8 && t->members[i]; ++i)
        if (t->members[i]->brain && aiMindHasContacts(t->members[i]->brain)) return true;
    return false;
}

void entMarkEnemiesOnStructures() {
    for (int i = 0; i < kMaxTeams && ms().teams[i]; ++i) {
        const Team* t = ms().teams[i];
        if (!isEnemyTeam(t)) continue;
        for (int m = 0; m < 8 && t->members[m]; ++m) {
            Unit* u = t->members[m];
            WorldObject* w = wldProbeSolid(u->body);
            if (w && (w->kind == TerrainKind::Water || w->kind == TerrainKind::DeepWater || w->kind == TerrainKind::Shallow))
                u->mover->flags2 |= mover_flag2::kInBoat;
        }
    }
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

int statCountCapturedEnemies() {
    int n = 0;
    for (int i = 0; i < kMaxTeams && ms().teams[i]; ++i) {
        const Team* t = ms().teams[i];
        if (!isEnemyTeam(t)) continue;
        for (int m = 0; m < 8 && t->members[m]; ++m)
            if (t->members[m]->mover->flags & mover_flag::kSearched) ++n;
    }
    return n;
}

int statCountWounded(int teamType) {
    int n = 0;
    for (int i = 0; i < kMaxTeams && ms().teams[i]; ++i) {
        const Team* t = ms().teams[i];
        if (int(t->type) != teamType) continue;
        for (int m = 0; m < 8 && t->members[m]; ++m)
            if (medWoundLevel(t->members[m]->status) != 0) ++n;
    }
    return n;
}

int statCountDead(int teamType) {
    int n = 0;
    for (int i = 0; i < kMaxTeams && ms().teams[i]; ++i) {
        const Team* t = ms().teams[i];
        if (int(t->type) != teamType) continue;
        for (int m = 0; m < 8 && t->members[m]; ++m)
            if (t->members[m]->status->hit_mask & hit_bit::kKilled) ++n;
    }
    return n;
}

bool statNoCraftLost() { return statCountDead(1) + statCountDead(2) + statCountDead(3) == 0; }

void msnTallyCasualties() {
    MissionState& S = ms();
    while (S.splitGroups != 0) {
        if (entGroupMerge(0, S.teamCount - 1) < 0) break;
    }
    Unit* pm = pointMan();
    if (pm && (pm->mover->flags & mover_flag::kAboard)) evtTeamSetMoverFlags(team(0), mover_flag::kAboard);
    // roster_clear_wounded / roster_record_casualty belong to the campaign
    // (front end); the debriefing reads the hit masks from the units.
    S.enemyKia = s16(statCountDead(4) + statCountDead(5));
    S.sealKia = s16(statCountDead(0));
    S.sealWia = s16(statCountWounded(0));
    S.missionMinutes = s16(todMissionMinutes());
    Team* x = team(S.extractingGroup);
    S.extractionType = (S.extractingGroup == 0xFF || !x) ? 4 : int(x->type);
}

// ---------------------------------------------------------------------------
// Objectives (365e:251D)
// ---------------------------------------------------------------------------

bool msnCheckObjective(int i) {
    MissionState& S = ms();
    const MciObjective& o = S.mci.objective[i];
    WorldObject* target = o.target_structure >= 0 ? worldObjectAt(o.target_structure) : nullptr;
    const int teamIdx = S.firstMtmGroup + o.target_team;
    Team* tt = o.target_team >= 0 ? team(teamIdx) : nullptr;
    Unit* pm = pointMan();
    s32 d = 3000;
    const int kind = int(u16(o.kind));
    switch (kind) {
    case 1: case 3: case 4: case 7:
        if (target && !(target->flags & 1)) d = geoDistance(pm->body->pos, target->body->pos);
        break;
    case 2: case 5: case 6:
        if (tt && !(tt->members[0]->mover->flags2 & mover_flag2::kObjectiveDone))
            d = geoDistance(pm->body->pos, tt->members[0]->body->pos);
        break;
    default: break;
    }
    if (d >= 3000) return false;
    bool done = false;
    u16 msgOff = 0;
    switch (kind) {
    case 1:  // Patrol
        if (d < 0x1C2 && !entTeamAllExtracted()) {
            target->flags |= 1;
            msgOff = kMsgPatrol;
            done = true;
        }
        break;
    case 2:  // Ambush
        if (d < 0x960 && (grpCountAlive(tt) == 0 || entGroupHasSecuredMember(teamIdx))) {
            tt->members[0]->mover->flags2 |= mover_flag2::kObjectiveDone;
            msgOff = kMsgAmbush;
            done = true;
        }
        break;
    case 3:  // Demolition
        if (d < 0x960 && target->hit_points <= 0) {
            target->flags |= 1;
            msgOff = kMsgDemolition;
            done = true;
        }
        break;
    case 4:  // Observe
        if (d < 0x4B0) {
            bool ok;
            if (target->kind == TerrainKind::Structure) ok = target->hit_points <= 0;
            else ok = statCountDead(4) + statCountDead(5) > 4;
            if (ok) {
                target->flags |= 1;
                msgOff = kMsgObserve;
                done = true;
            }
        }
        break;
    case 5:  // Rescue
    case 6:  // Snatch
        if (entGroupHasSecuredMember(teamIdx)) {
            tt->members[0]->mover->flags2 |= mover_flag2::kObjectiveDone;
            msgOff = kind == 5 ? kMsgRescue : kMsgSnatch;
            done = true;
        }
        break;
    case 7:  // Recover
        if (d < 0xF0 && !entTeamAllExtracted()) {
            target->flags |= 1;
            msgShowDs(kMsgRecover, 0x400);
            unit1UseTool();
            done = true;
        }
        break;
    default: break;
    }
    if (msgOff) msgShowDs(msgOff, 0x400);
    if (done && !entTeamAllExtracted()) {
        msgHandSignal(pm, 5);
        sfxPlay(0x33, 0x200, &pm->body->pos, 1, pm);
    }
    return done;
}

void msnCheckObjectives() {
    for (int i = 0; i < 3; ++i) msnCheckObjective(i);
}

bool msnIsObjectiveStructure(const WorldObject* w) {
    if (!w) return false;
    for (const MciObjective& o : ms().mci.objective)
        if (worldObjectAt(o.target_structure) == w && s16(u16(o.kind)) > 2) return true;
    return false;
}

bool msnIsObjectiveTeam(const Team* t) {
    if (!t) return false;
    // No kind check; team -1 compares with the team just before the MTM teams (original).
    for (const MciObjective& o : ms().mci.objective)
        if (team(ms().firstMtmGroup + o.target_team) == t) return true;
    return false;
}

int msnObjectiveDemoWeapon(int i) {
    const s16 k = s16(u16(ms().mci.objective[i].kind));
    if (k < 1) return -1;
    return k == 3 ? 0x0C : -1;
}

int msnFindObjectiveTarget(int kind, const Vec3* pos) {
    MissionState& S = ms();
    int result = -1;
    for (const MciObjective& o : S.mci.objective) {
        int idx = -1;
        int d = 24000;
        if (int(u16(o.kind)) == kind) {
            if (kind == 2 || kind == 6) {
                if (o.target_team != -1) {
                    const int ti = S.firstMtmGroup + o.target_team;
                    if (!pos) {
                        idx = ti;
                    } else if (grpCountAlive(team(ti)) > 0) {
                        idx = ti;
                        d = geoDistance(*pos, team(ti)->members[0]->body->pos);
                    }
                }
            } else if (kind == 3) {
                if (o.target_structure != -1) {
                    if (!pos) {
                        idx = o.target_structure;
                    } else if (!wldObjectDestroyed(o.target_structure)) {
                        idx = o.target_structure;
                        d = geoDistance(*pos, worldObjectAt(idx)->body->pos);
                    }
                }
            }
        }
        if ((!pos && idx != -1) || d < 24000) result = idx;
    }
    return result;
}

// 19ac:651B grp_set_craft_status (camp scenes): park a craft on the nearest pad
// (helicopter pad kind 0xF, boats the dock kind 0xE shifted 0x5A00 in x).
void grpSetCraftStatus(int teamIndex, int status) {
    Team* t = team(teamIndex);
    if (!t) return;
    Unit* l = t->members[0];
    const int padKind = t->type == TeamType::Helicopter ? 0xF : 0xE;
    const int idx = wldNearestObject(l->body->pos, padKind);
    if (idx != -1 && status == 0) {
        l->body->pos = worldObjectAt(idx)->body->pos;
        if (padKind == 0xE) {
            l->body->pos.x = wrapAdd(l->body->pos.x, 0x5A00);
        } else {
            l->mover->target_height = l->mover->height_low;
            l->mover->height = l->mover->height_low;
            l->body->pos.y = s32(l->mover->height_low) << 8;
        }
        l->mover->destination = l->body->pos;
    }
    if (status == 4 && t->type == TeamType::Helicopter) l->body->pos.y = 0x12C00;
    t->order = s16(status);
}

} // namespace st::game::mission
