// Cameras, view modes and the frame render driver of segment 1000
// (view_init_cameras 2261 .. view_next_enemy 2D01, view_render_frame 1F92,
// hud_reset_mission 1EB0; docs/re/seg_1000.md 7-8), the renderer hooks the
// loop installs (unit lookup, effect sprites 1000:6DD5 / 6F4D, scenery) and
// the scenery hook (veg_update / veg_reset with the current camera).
#include "game/mission/loop.h"

#include "core/settings.h"
#include "engine/rng.h"
#include "engine/sound.h"
#include "engine/ticker.h"
#include "game/front/common.h"
#include "platform/system.h"
#include "game/globals.h"
#include "game/mission/build.h"
#include "game/mission/craft.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/modern.h"
#include "game/mission/people.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/mission/world.h"
#include "game/mission/wquery.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/gfx.h"
#include "render/impactfx.h"
#include "render/r3d.h"
#include "render/sky.h"
#include "render/sprites.h"
#include "render/veg.h"
#include "render/world.h"

#include <cstdlib>

namespace st::game::mission {
namespace loop {

namespace {

constexpr u16 kBannerInsertion = 0x0604;   // " Insertion  "
constexpr u16 kBannerReinsert = 0x0611;    // "Esc to skip  -  R to Reinsert"
constexpr u16 kBannerExtraction = 0x062F;  // " Extraction  "
constexpr u16 kViewBufferSmall = 0x063D;   // "View Buffer Too Small"
constexpr u16 kObjectListSmall = 0x0653;   // "Object List Too Small"

// (d * sin_fx) >> 8 with the 32-bit wrap-around of crt_aFlmul / crt_aFlshr.
s32 mulShr8(s32 d, s32 fx) { return s32(u32(d) * u32(fx)) >> 8; }

// math_mul_8_8 (2255:1062): (a * b) >> 8, 16-bit result.
int mul88(int a, int b) { return s16((s32(s16(a)) * s16(b)) >> 8); }

const Vec3* leaderPos(int teamIndex) {
    const Team* t = team(teamIndex);
    return (t && t->members[0]) ? &t->members[0]->body->pos : nullptr;
}

// Camera altitude rule of the orbit views: the leader's altitude when it is
// above 0x1800, else 0x1800 (32-bit compare of 1000:2860 / 2BB3).
s32 orbitAltitude(const Vec3& target) { return target.y > 0x1800 ? target.y : 0x1800; }

// ---------------------------------------------------------------------------
// Effect sprites of ordnance objects (1000:6DD5 spr_draw_explosion,
// 1000:6F4D spr_draw_projectile) and the scenery branch of 348e:0A90 that
// belongs to the mission (muzzle flash / explosion / cloud / impact of a
// projectile found with prj_find_by_effect).
// ---------------------------------------------------------------------------

void drawExplosion(int x, int y, int scale, bool impact, const Projectile* p) {
    const MissionState& S = ms();
    // Without a projectile the original uses 4 * scale - rng(max(1, scale / 2))
    // and then reads the record through a NULL pointer; the game never calls
    // it that way (the painter always has the projectile).
    if (!p) return;
    // Port: with the Enhanced impact effects a human hit's puff is the
    // visual-only one at the victim's torso (render/impactfx), not the
    // round's own at bullet height (above a prone man's head).
    if (impact && settings().effectiveImpactFx() && ImpactSurface(p->impact_surface) == ImpactSurface::Blood) return;
    // 16-bit arithmetic: low word of (g_time - detonation time) times 4, at least 4.
    u16 e16 = u16(u16(u32(S.time) - u32(p->detonate_time)) * 4u);
    if (e16 < 4) e16 = 4;
    int e = s16(e16);
    const render::SpriteImage* img;
    if (impact) {
        img = render::spriteFrame("impc", 0, p->hit >> 5);
    } else {
        const int r = (std::abs(e) >> 8) % 4;
        img = render::spriteFrame(p->weapon == 0x0C ? "exlg" : "exsm", 0, r);
    }
    if (!img) return;
    bool draw;
    if (p->detonate_time == p->impact_time) {
        if (e < 0x40) e = 0x40;
        draw = e < scale * 3;
    } else {
        draw = e < (scale << 3);
    }
    if (!draw) return;
    const int w = mul88(img->w(), e), h = mul88(img->h(), e);
    // Port: Enhanced impact effects tint the puff by the surface that was hit.
    if (impact && settings().effectiveImpactFx())
        gfx().setSpriteRemap(render::impactRemap(ImpactSurface(p->impact_surface)));
    gfx().spriteScaled(x - (w >> 1), y - (h >> (impact ? 1 : 0)), w, h, img->rle.data());
    gfx().setSpriteRemap(nullptr);
}

void drawProjectile(int x, int y, int scale, const Projectile* p) {
    const MissionState& S = ms();
    if (p->state & prj_state::kDud) return;
    const u8* remap = nullptr;
    if (p->weapon == 0x10 || p->weapon == 0x11) remap = render::remapTable(p->weapon == 0x10 ? 3 : 2);
    if (p->weapon == 0x21 || p->weapon == 0x1A) remap = nullptr;
    int r;
    const char* set;
    if ((p->fire_modes & fire_mode::kIllumination) && S.time < wrapAdd(p->launch_time, 0xB400)) {
        r = engine::rng().range(4) + 4;  // burning flare: a random exsm frame
        set = "exsm";
    } else {
        const s32 rem = wrapSub(S.time, p->detonate_time) % 0x400;
        r = std::abs(int(rem >> 6) - 7);
        if (r > 7) r = 7;
        set = "fxsm";
    }
    const render::SpriteImage* img = render::spriteFrame(set, 0, r);
    if (!img) return;
    u16 e16 = u16(u32(S.time) - u32(p->detonate_time));
    if (e16 < 4) e16 = 4;
    int e = s16(e16);
    if (e < 0) e = 0x7FFF;
    int s = scale << 1;
    if (s > e) s = e;
    if (p->fire_modes & fire_mode::kIllumination) s += s >> 1;
    const int w = mul88(img->w(), s), h = mul88(img->h(), s);
    gfx().setSpriteRemap(remap);
    gfx().spriteScaled(x - w, y - h, w, h, img->rle.data());
    gfx().setSpriteRemap(nullptr);
}

// 348e:0A90, the ordnance part (called by render/sprites for non-unit billboards).
bool effectPainter(const Obj3D* obj, int x, int y, int scale, int rotation) {
    Projectile* p = prjFindByEffect(obj);
    if (!p) return false;
    if (obj == p->fx_muzzle) {
        // The Point Man's own muzzle flash is not drawn in the first-person view.
        if (ms().viewMode == 0 && p->owner == pointMan()) return true;
        const render::SpriteImage* img = render::spriteFrame("fxmu", 0, rotation);
        if (!img) return true;
        const int c = int(p->wclass);
        if (c == 0x0F || c == 0x10 || c == 0x14) gfx().setSpriteRemap(render::remapTable(3));
        render::sprDrawCentered(x, y, scale << 1, img->rle.data());
        gfx().setSpriteRemap(nullptr);
        return true;
    }
    if (obj == p->fx_explosion) {
        drawExplosion(x, y, scale, false, p);
        return true;
    }
    if (obj == p->fx_cloud) {
        drawProjectile(x, y, scale, p);
        return true;
    }
    if (obj == p->fx_impact) {
        drawExplosion(x, y, scale >> 2, true, p);
        return true;
    }
    return false;
}

// wld_probe_kind(pos, 5) for the scenery placement (1000:3757).
bool blockerProbe(s32 x, s32 z) {
    Vec3 p{x, 0, z};
    return wldProbeKind(p, 5);
}

const Obj3D* g_vegPoolMark = nullptr;  // first vegetation object the pools were handed over for

void ensureVegPools() {
    const FxPools& fx = fxPools();
    const Obj3D* mark = fx.vegetation.empty() ? nullptr : fx.vegetation[0];
    if (mark == g_vegPoolMark) return;
    g_vegPoolMark = mark;
    render::vegSetPools(fx.vegetation, fx.trees);
}

} // namespace

LoopState& ls() {
    static LoopState instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Cameras (1000:21F0..2261)
// ---------------------------------------------------------------------------

namespace {
// view_init_camera (1000:21F0): x, z, altitude in map units, pitch in degrees.
void initCamera(Camera& c, s32 x, s32 z, s32 alt, int pitch, int rx, int ry, int rw, int rh) {
    c = Camera{};
    c.pos.x = s32(u32(x) << 8);
    c.pos.z = s32(u32(z) << 8);
    c.pos.y = s32(u32(alt) << 8);
    c.pitch = s16(pitch << 3);
    c.yaw = 0;
    c.roll = 0;
    c.zoom = 8;
    c.rect_x = s16(rx);
    c.rect_y = s16(ry);
    c.rect_w = s16(rw);
    c.rect_h = s16(rh);
}
} // namespace

void viewInitCameras() {
    LoopState& L = ls();
    initCamera(L.camMain, -1000, 15, 0, 0, 0, 8, 320, 171);
    initCamera(L.camMap, 0, 0, 1000, -90, 8, 10, 204, 155);
    initCamera(L.camSmall, -1000, 0, 24, 0, 0, 24, 320, 139);
    L.cur = &L.camMain;
}

void viewSelectCamera(int mode) {
    LoopState& L = ls();
    switch (mode) {
    case 0:
        L.cur = &L.camMain;
        break;
    case 0x0C:
        viewEnterMap();
        break;
    case 7:
        // mis_start_insertion (1000:0028): orbit distance 0x264 (the simulation owns the rest).
        L.orbitDist = 0x264;
        L.cur = &L.camSmall;
        break;
    case 8:
        L.orbitDist = 0x120;  // mis_start_extraction (1000:00B0)
        L.cur = &L.camSmall;
        break;
    case 2:
        // mis_insertion_clear (1000:007A) sets g_orbit_dist = 0xC0 after
        // view_set_chase; the value is only read by modes 7/8, which reset it.
        L.orbitDist = 0xC0;
        L.cur = &L.camSmall;
        break;
    default:
        L.cur = &L.camSmall;
        break;
    }
    L.curPos = L.cur->pos;
}

void viewModeStored(int /*mode*/) {
    ls().fullRedraw = 2;
    fxMarkersHide(true);
}

void viewEnterMap() {
    LoopState& L = ls();
    MissionState& S = ms();
    if (S.viewMode == 0 || S.viewMode == 2) L.prevViewMode = S.viewMode;
    L.cur = &L.camMap;
    L.camMap.pos.x = L.camMain.pos.x;
    L.camMap.pos.z = L.camMain.pos.z;
    mapSelectTeamHeight();
    S.viewMode = 1;
    L.fullRedraw = 2;
    render::world().skyColor = 0xFF;
    render::world().groundColor = 0xFF;
}

namespace {
// hud_show_weapon_lines (1000:109F).
void hudShowWeaponLines() {
    MissionState& S = ms();
    S.toolInfoTime = S.grenadeInfoTime = S.weaponInfoTime = wrapAdd(S.time, 0x200);
}
} // namespace

void viewSetFirstPerson() {
    simSetViewMode(0);  // g_full_redraw = 2, camera main, veg_update(1), mode 0, fx_markers_hide
    hudShowWeaponLines();
}

void viewSetChase() {
    simSetViewMode(2);
    hudShowWeaponLines();
}

bool viewSetTeamCamera(int n) {
    const MissionState& S = ms();
    if (n < 4) {
        const Team* t = team(S.insertionGroup + n);
        if (!t || int(t->type) == 0 || int(t->type) >= 4) return false;
    } else {
        if (S.splitGroups == 0 || (n == 5 && S.splitGroups <= 1)) return false;
    }
    simSetViewMode(n < 2 ? n + 3 : n + 0x0D);
    return true;
}

void viewSetMode(int mode) { simSetViewMode(mode); }

void viewSetTargetCamera() {
    MissionState& S = ms();
    if (S.playerTarget.kind != TargetKind::Unit) return;
    LoopState& L = ls();
    L.fullRedraw = 2;
    L.cur = &L.camSmall;
    S.viewMode = 0x0D;
    fxMarkersHide(true);
    viewVegUpdate(true);  // after the mode store, unlike view_set_mode
}

void viewSetEyeHeight(const Unit* u) {
    LoopState& L = ls();
    if (!u || !u->mover) return;
    int h = u->mover->height;
    if (h < 0) h = 3 - (h >> 3);
    if (u->mover->flags & mover_flag::kAboard) {
        // Aboard the extraction craft: its leader's altitude (map units) + 0x24.
        if (const Vec3* craft = leaderPos(ms().extractingGroup)) h += (craft->y >> 8) + 0x24;
    }
    if (h < 1) h = 1;
    L.camMain.pos.y = s32(u32(s32(h)) << 8);
}

// Port: the middle mouse button / the pad's "Recentre camera" action. An
// orbit view (chase, team, target camera) swings back behind the leader it
// follows: orbit angle = his heading + 180 degrees, the angle the original
// itself sets when the Point Man bounces off an obstacle
// (evt_unit_bounce_off_obstacle). The original has no other way back once
// the right button has swung the camera around him.
void viewRecentreCamera() {
    LoopState& L = ls();
    const MissionState& S = ms();
    const int mode = S.viewMode;
    // First person has no orbit; map, insertion / extraction and the enemy
    // camera (re-aimed every frame) are left alone.
    if (mode < 2 || mode == 7 || mode == 8 || mode == 0x0C || mode == 0x0E) return;
    const Unit* pm = pointMan();
    Team* t = mode == 2 ? (pm ? pm->team : nullptr) : team(L.viewTeam);
    if (!t || !t->members[0] || !t->members[0]->body) return;
    t->view_heading = s16(angleWrap(t->members[0]->body->heading + 0x5A0));
    if (team(L.viewTeam) == t) L.viewHeading = t->view_heading;
}

void viewInitOrbitAngle(Team* t) {
    if (!t || t->view_heading != -1) return;
    const Unit* pm = pointMan();
    const Unit* l = t->members[0];
    if (!pm || !l) return;
    t->view_heading = s16(mathHeading(pm->body->pos.x, pm->body->pos.z, l->body->pos.x, l->body->pos.z));
}

void viewUpdateCamera(int mode) {
    LoopState& L = ls();
    MissionState& S = ms();
    Unit* pm = pointMan();
    if (!pm || !L.cur) return;
    Camera& c = *L.cur;
    L.viewTeam = 0;
    const Vec3& pmPos = pm->body->pos;
    auto orbit = [&](const Vec3& target, s32 d, int a8) {
        c.pos.x = wrapSub(target.x, mulShr8(d, mathSinFx(a8)));
        c.pos.z = wrapAdd(target.z, mulShr8(d, mathCosFx(a8)));
    };
    // Port (Enhanced): an orbit view looks back at its target along the orbit
    // angle exactly. The original re-derives the yaw from the two positions
    // through its heading tables (whole degrees for geo_bearing, 1/8 degree
    // with up to 3/8 degree of rounding sawtooth for math_heading as the
    // camera dollies in): a pixel at 320 wide, a visible lurch at 1080p wide
    // view. Original keeps the original's yaw.
    const bool exactYaw = !settings().original();
    auto lookBack = [&](int a8, int originalYaw) { c.yaw = exactYaw ? s16(angleWrap(a8 + 0x5A0)) : s16(originalYaw); };
    switch (mode) {
    case 0:
        c.pos.x = pmPos.x;
        c.pos.z = pmPos.z;
        c.yaw = pm->body->heading;
        break;
    case 1: {
        const Vec3* l = leaderPos(S.mapSelTeam);
        if (!l) break;
        if (S.mapSelTeam < S.firstMtmGroup) {
            c.pos.x = l->x;
            c.pos.z = l->z;
        } else {
            // Enemy teams: the low byte of the low words is cleared (16-bit AND 0xFF00).
            c.pos.x = s32(u32(l->x) & 0xFFFFFF00u);
            c.pos.z = s32(u32(l->z) & 0xFFFFFF00u);
        }
        break;
    }
    case 0x0C:
        c.pos.x = S.wpSupport.x;
        c.pos.z = S.wpSupport.z;
        break;
    case 2:
    case 0x0E: {
        Team* t = pm->team;
        if (mode == 0x0E) {
            L.viewTeam = S.firstMtmGroup + L.enemyViewIdx;
            t = team(L.viewTeam);
            if (!t || !t->members[0]) break;
            t->view_heading = -1;
        }
        if (!t || !t->members[0]) break;
        viewInitOrbitAngle(t);
        const Vec3& l = t->members[0]->body->pos;
        orbit(l, s32(t->view_distance), t->view_heading);
        lookBack(t->view_heading, geoBearing(c.pos, l) << 3);
        c.pos.y = 0x1800;
        if (entTeamAllExtracted())
            if (const Vec3* craft = leaderPos(S.extractingGroup)) c.pos.y = wrapAdd(c.pos.y, craft->y);
        break;
    }
    case 3:
    case 4:
    case 0x0D:
    case 0x0F:
    case 0x10:
    case 0x11:
    case 0x12: {
        int shift = 2;
        Team* t;
        if (mode == 0x0D) {
            if (S.playerTarget.kind == TargetKind::Unit && S.playerTarget.target.unit)
                L.targetCamTeam = S.playerTarget.target.unit->team;
            else if (!L.targetCamTeam)
                L.targetCamTeam = team(S.firstMtmGroup);
            t = const_cast<Team*>(L.targetCamTeam);
            L.viewTeam = grpIndex(t);
            if (L.viewTeam == -1) L.viewTeam = S.firstMtmGroup;
            shift = 0;
        } else if (mode == 0x11 || mode == 0x12) {
            const int n = (S.splitGroups == 2 && mode == 0x11) ? S.teamCount - 1 : S.teamCount;
            L.viewTeam = n - 1;
            t = team(L.viewTeam);
            shift = 0;
        } else {
            const int k = (mode == 0x0F || mode == 0x10) ? mode - 10 : mode;
            L.viewTeam = S.insertionGroup + k - 3;
            t = team(L.viewTeam);
        }
        if (!t || !t->members[0]) break;
        viewInitOrbitAngle(t);
        const Vec3& l = t->members[0]->body->pos;
        orbit(l, s32(t->view_distance) << shift, t->view_heading);
        lookBack(t->view_heading, geoBearing(c.pos, l) << 3);
        c.pos.y = orbitAltitude(l);
        break;
    }
    case 5:
    case 6: {
        L.viewTeam = mode == 5 ? S.insertionGroup : S.extractionGroup;
        const Team* t = team(L.viewTeam);
        if (!t || !t->members[0]) break;
        const Vec3& l = t->members[0]->body->pos;
        if (L.fullRedraw == 2) {
            // A random +-15 degree offset around the bearing leader -> Point Man.
            L.orbitAngle = s16(mathHeading(l.x, l.z, pmPos.x, pmPos.z));
            const int r = engine::rng().range(30);
            L.orbitAngle = s16(angleWrap(L.orbitAngle + ((15 - r) << 3)));
        }
        orbit(l, L.orbitDistCraft, L.orbitAngle);
        lookBack(L.orbitAngle, mathHeading(c.pos.x, c.pos.z, l.x, l.z));
        c.pos.y = orbitAltitude(l);
        break;
    }
    case 7:
    case 8: {
        const Vec3* target = &pmPos;
        if (mode == 8) {
            L.viewTeam = S.extractingGroup;
            const Vec3* craft = leaderPos(L.viewTeam);
            if (!craft) break;
            target = craft;
            if (entTeamAllExtracted()) L.orbitDist = wrapAdd(L.orbitDist, (0x1D * S.frameTicks) >> 8);
            if (!wldIsCamp()) {
                if (L.orbitDist < 0x1E0) L.orbitHeading = s16(mathHeading(craft->x, craft->z, pmPos.x, pmPos.z));
                else L.orbitHeading = s16(mathHeading(S.wpSupport.x, S.wpSupport.z, craft->x, craft->z));
            }
        } else {
            L.orbitHeading = s16(mathHeading(S.wpSeal.x, S.wpSeal.z, pmPos.x, pmPos.z));
            L.orbitDist = wrapSub(L.orbitDist, (0x33 * S.frameTicks) >> 8);
        }
        orbit(*target, L.orbitDist, L.orbitHeading);
        lookBack(L.orbitHeading, mathHeading(c.pos.x, c.pos.z, target->x, target->z));
        c.pos.y = (mode != 7 && target->y > 0x1800) ? target->y : 0x1800;
        break;
    }
    default:
        break;
    }
    L.curPos = c.pos;
    if (const Vec3* l = leaderPos(L.viewTeam)) engine::sound().setListener(&l->x);
}

void viewNextEnemy() {
    LoopState& L = ls();
    const MissionState& S = ms();
    if (S.teamCount <= S.firstMtmGroup || S.firstMtmGroup == 0xFF) return;
    for (int guard = 0; guard < kMaxTeams + 1; ++guard) {
        ++L.enemyViewIdx;
        if (S.firstMtmGroup + L.enemyViewIdx >= S.teamCount) L.enemyViewIdx = 0;
        const Team* t = team(S.firstMtmGroup + L.enemyViewIdx);
        if (!t || !t->members[0]) continue;
        if (!isEnemyTeam(t)) continue;
        const Unit* l = t->members[0];
        if (unitDead(l)) return;
        if (l->brain && (l->brain->flags & brain_flag::kActive)) return;
    }
}

// ---------------------------------------------------------------------------
// hud_reset_mission (1000:1EB0), the parts the loop owns. The simulation does
// view_set_first_person (through simSetViewMode) and its own timers.
// ---------------------------------------------------------------------------

void hudResetMission() {
    LoopState& L = ls();
    MissionState& S = ms();
    render::skyState().dawnTime = S.time;
    Unit* pm = pointMan();
    viewSetEyeHeight(pm);
    mapInit();
    if (pm) L.orbitHeading = pm->body->heading;
    // g_orbit_dist / g_orbit_dist_craft = 0xC0, the sky offsets and the enemy
    // view index are set by run() before simInit (mis_start_insertion, which
    // runs inside simInit, overrides the orbit distance).
    render::modelsSetTimeOfDay(S.todHour);
}

// ---------------------------------------------------------------------------
// Renderer hooks and scenery
// ---------------------------------------------------------------------------

bool viewEnsureRenderer() {
    if (!render::modelsLoad() || !render::skyLoad()) return false;
    render::spritesLoad();
    return true;
}

void viewInstallRenderHooks() {
    render::setUnitLookup(unitFromBody);
    render::setEffectPainter(effectPainter);
    render::setAnimUpdate(sprUpdateAnim);
    render::setUnitMark(modernIsSnatchTarget);  // port: Modern gameplay, the marked snatch target
    render::setBlockerProbe(blockerProbe);
    g_vegPoolMark = nullptr;
    render::impactFxReset();  // port: Enhanced impact effects
}

void viewUpdateRenderContext() {
    const MissionState& S = ms();
    render::RenderContext& ctx = render::renderContext();
    ctx.frameTicks = S.frameTicks;
    ctx.time = S.time;
    ctx.todHour = S.todHour;
    ctx.reinsertPoint = S.wpSupport;
    ctx.viewMode = S.viewMode;
    ctx.detail = g().detailLevel;
    ctx.fullScreen3d = false;  // viewRenderFrame sets it for the field views
    const TargetRec& tr = S.playerTarget;
    ctx.reticleTarget = nullptr;
    if (tr.kind == TargetKind::Unit && tr.target.unit) ctx.reticleTarget = tr.target.unit->body;
    else if (tr.kind == TargetKind::Structure && tr.target.structure) ctx.reticleTarget = tr.target.structure->body;
    render::setPointMan(pointMan());
}

void viewVegUpdate(bool force) {
    LoopState& L = ls();
    if (!L.cur) return;
    ensureVegPools();
    viewUpdateRenderContext();
    const Unit* pm = pointMan();
    render::vegSetPointMan(pm ? &pm->body->pos : nullptr, entTeamAllExtracted());
    render::vegUpdate(*L.cur, force);
}

void viewVegReset() {
    ensureVegPools();
    render::vegReset();
}

// ---------------------------------------------------------------------------
// view_render_frame (1000:1F92)
// ---------------------------------------------------------------------------

void viewRenderFrame() {
    LoopState& L = ls();
    MissionState& S = ms();
    Gfx& gx = gfx();
    if (!L.cur) return;
    const Camera& cam = *L.cur;
    const int mode = S.viewMode;
    const bool mapMode = mode == 1 || mode == 0x0C;
    viewUpdateRenderContext();
    // Enhanced "Full-screen 3D": the field views fill the window height and
    // the HUD (bands included) is drawn over the scene (hud.cpp backs every
    // element with a dark strip and a shadow).
    const bool fullScreen = hudOverScene();
    render::renderContext().fullScreen3d = fullScreen;
    // The insertion / extraction banners of the full-redraw branch below are
    // overdrawn by the full-height view; they are redrawn after every render.
    auto drawBanners = [&] {
        if (mode == 7) {
            uiDrawTitleTab(dsText(kBannerInsertion), 0x80, 4);
            hudText4x6Centered(cam.rect_y + cam.rect_h + 7, dsText(kBannerReinsert));
        } else if (mode == 8) {
            uiDrawTitleTab(dsText(kBannerExtraction), 0x7C, 4);
        }
    };
    if (L.fullRedraw == 0) {
        if (mapMode) cursorErase();
    } else {
        --L.fullRedraw;
        if (mapMode) {
            picBlitToScreen();
            uiDrawTextPanel(nullptr, 9, 10, 0xCA, 0x9B);
            mapDrawTeamList();
            mapDrawOrdersMenu();
            gx.fillRect(0, 0xC0, 0x140, 8, u16(Gfx::kSolid | 0x00));
        } else {
            gx.clipFull();
            gx.clear(0);
            if (!fullScreen) drawBanners();
        }
    }
    if (mapMode) {
        render::viewClearMapGround(cam);
        gx.fillRect(0, 0xC0, 0x140, 8, u16(Gfx::kSolid | 0x00));
    } else {
        gx.setClip(cam.rect_x, cam.rect_y, cam.rect_w, cam.rect_h);
        render::todDrawSkyGround(cam, g().detailLevel, S.time, S.todHour);
    }
    // Port: Enhanced impact effects. The particles move with the mission's
    // frame ticks and are drawn, with the visual-only puffs, inside the frame
    // of the field views only (the hook is removed again so the front end's
    // views never see it).
    const bool impactFx = settings().effectiveImpactFx();
    if (impactFx) {
        render::impactFxUpdate(S.frameTicks, S.time);
        if (!mapMode) render::setPostDrawHook(render::impactFxDraw);
    }
    const int r = render::renderView(cam, true);
    if (impactFx) render::setPostDrawHook(nullptr);
    L.renderResult = r & 0xFF;
    if (L.renderResult == render::kViewBufferTooSmall) fatal("%s", dsText(kViewBufferSmall).c_str());
    if (L.renderResult == render::kObjectListTooSmall) fatal("%s", dsText(kObjectListSmall).c_str());
    if (mapMode) {
        const Camera& m = L.camMap;
        gx.setClip(m.rect_x, m.rect_y, m.rect_w, m.rect_h);
        mapDrawRoutes();
        mapDrawMarkers();
        gx.clipFull();
        mapDrawInfoPanel();
        msgDrawQueue();
        if (const Unit* pm = pointMan()) engine::sound().setListener(&pm->body->pos.x);
        engine::sound().updateSfx();
        if (S.opt.map == 0) engine::ticker().frameLimitWait();
        cursorDraw();
    } else {
        if (fullScreen) {
            gx.clipFull();
            drawBanners();
            gx.setClip(cam.rect_x, cam.rect_y, cam.rect_w, cam.rect_h);
        }
        hudDrawTextLines();
        msgDrawQueue();
        if (const Vec3* l = leaderPos(L.viewTeam)) engine::sound().setListener(&l->x);
        engine::sound().updateSfx();
        if (mode == 0) {
            hudDrawTargetInfo();
            hudDrawCompass();
            hudDrawObjectiveMarker();
        }
        if (mode == 2) hudDrawObjectiveMarker();
    }
}

} // namespace loop
} // namespace st::game::mission
