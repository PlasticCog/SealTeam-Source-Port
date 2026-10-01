// The map screen / control panel of segment 19ac (2B60..5F6A, docs/re/
// seg_19ac.md 7-8): coordinate transforms with the map camera, routes and
// markers, the 41-button control panel (table DS:0BF4 read from st.exe),
// team list, orders menu, info panel and Team Info table, pointer / focus
// handling, map_screen_keys and insert_keys. The orders themselves are the
// simulation's (orders.cpp: mapOrderKey, mapSetWaypoint, reinsert*).
#include "game/mission/loop.h"

#include "core/settings.h"
#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "game/campaign.h"
#include "game/front/common.h"
#include "game/mission/build.h"
#include "game/mission/craft.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/modern.h"
#include "game/mission/sim.h"
#include "game/mission/state.h"
#include "game/mission/teams.h"
#include "game/mission/world.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/gfx.h"
#include "render/r3d.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace st::game::mission {
namespace loop {

namespace {

void mapFocusButtonInternal(int n);  // map_focus_button (19ac:504E)

constexpr u16 kButtonTable = 0x0BF4;    // UIBUTTON[41], 16 bytes each
constexpr int kButtonCount = 41;
constexpr u16 kOrderNames = 0x1BDE;     // char*[]: the button labels (bound by map_init)
constexpr u16 kVersion = 0x1B84;        // char* -> "SEAL Team  V1.0"
constexpr u16 kMonthNames = 0x1B90;     // char*[13]
constexpr u16 kRankAbbrev = 0x1BC4;     // char*[8] rating prefixes
constexpr u16 kPostureAbbrev = 0x1C38;  // char*[4]: Up, Cr, Pr, Dd
constexpr u16 kDeadAbbrev = 0x1C3E;     // the 'Dd' entry
constexpr u16 kCraftAbbrev = 0x1C58;    // char*[4]
constexpr u16 kEnemyState = 0x1C64;     // char*[5]
constexpr u16 kEnemyAlert = 0x1C6E;     // char*[], by AI behaviour >> 4
constexpr u16 kTeamTypeNames = 0x1C7C;  // char*[8]
constexpr u16 kSealRoles = 0x1C94;      // char*[8]
constexpr u16 kMissionTypes = 0x1D72;   // char*[8]
constexpr u16 kRankSuffix = 0x251E;     // char*[12]
constexpr u16 kStrDot1 = 0x0FCD;        // "."
constexpr u16 kStrDot2 = 0x0FCF;        // "."
constexpr u16 kStrTeamB = 0x0FD1;       // " b"
constexpr u16 kStrTeamA = 0x0FD4;       // " a"
constexpr u16 kStrTeamOrders = 0x0FD7;  // "   TEAM ORDERS"
constexpr u16 kStrClockTmpl = 0x0FE6;   // "  :  :   "
constexpr u16 kStrScale = 0x0FF0;       // "      SCALE        Mtrs"
constexpr u16 kStrOneTo = 0x1008;       // "1:"
constexpr u16 kStrPictoMap = 0x100B;    // "PictoMap USNavy"
constexpr u16 kStrObjective1 = 0x101B;  // "1st Objective:"
constexpr u16 kStrObjective2 = 0x102A;
constexpr u16 kStrObjective3 = 0x1039;
constexpr u16 kStrHeaderPos = 0x1048;   // "TEAM  Pos Spd Hdg Weapon "
constexpr u16 kStrHeaderName = 0x1062;  // "Rank   Name       Grenade"
constexpr u16 kStrEmpty = 0x107C;       // ""

struct MapButton {
    s16 x = 0, y = 0, w = 0, h = 0;  // +0..+6
    u16 key = 0;                     // +8 hot key
    u8 underline = 0;                // +0xA underlined label character (0xFF: none)
};

MapButton g_buttons[kButtonCount];
bool g_buttonsLoaded = false;
u8 g_textColour = 0x0F;  // g_text_fg (DS:5074) low byte, set by the callers of map_draw_button

void loadButtons() {
    if (g_buttonsLoaded) return;
    for (int i = 0; i < kButtonCount; ++i) {
        const u8* r = exe().dg(u16(kButtonTable + 16 * i));
        if (!r) fatal("map: button table missing from st.exe");
        g_buttons[i].x = rds16(r);
        g_buttons[i].y = rds16(r + 2);
        g_buttons[i].w = rds16(r + 4);
        g_buttons[i].h = rds16(r + 6);
        g_buttons[i].key = rd16(r + 8);
        g_buttons[i].underline = r[10];
    }
    g_buttonsLoaded = true;
}

const MapButton& btn(int n) {
    loadButtons();
    if (n < 0 || n >= kButtonCount) n = 0;
    return g_buttons[n];
}

std::string orderName(int n) { return exe().dgStringPtr(u16(kOrderNames + 2 * n)); }
std::string tableStr(u16 table, int i) { return exe().dgStringPtr(u16(table + 2 * i)); }
std::string utoa(int v) { return std::to_string(unsigned(u16(v))); }

void setColour(u8 c) { g_textColour = c; }
void text(int y, int x, const std::string& s) { front::text4x6(y, x, s, g_textColour); }

// gfx_line(colour, y1, x1, y2, x2).
void line(u16 c, int y1, int x1, int y2, int x2) { gfx().line(x1, y1, x2, y2, c); }

s32 mapHeight() { return ls().camMap.pos.y; }

// rect_has_point (2255:2622): r.x <= x < r.x + w, r.y <= y < r.y + h.
bool rectHasPoint(int rx, int ry, int rw, int rh, int x, int y) { return rx <= x && x < rx + rw && ry <= y && y < ry + rh; }

bool isHostile(const Team* t) { return t && int(t->type) != 0 && int(t->type) > 3; }
bool isCraft(const Team* t) { return t && int(t->type) > 0 && int(t->type) < 4; }

// input_has_pointer (19ac:29B9).
bool hasPointer() { return engine::input().mousePresent() || engine::input().joystickPresent(); }

// map_draw_triangle (19ac:31E1): apex at (x, y), base s below (above when
// flipped); s <= 0 = auto size from the map height.
void drawTriangle(int x, int y, u16 colour, int s, bool flipped) {
    if (s < 1) {
        const s32 h = mapHeight() >> 8;
        s = h != 0 ? int((16000 / h) >> 3) * 4 : 2;
        if (s < 2) s = 2;
    }
    const int d = flipped ? -s : s;
    const s16 pts[6] = {s16(x - s), s16(y + d), s16(x + s), s16(y + d), s16(x), s16(y)};
    gfx().fillPolygon(pts, 3, colour);
}

// map_draw_cross (19ac:327C): 3-pixel X (flag 0) or + (flag != 0).
void drawCross(int x, int y, u16 colour, bool plus) {
    if (!plus) {
        line(colour, y + 3, x + 3, y - 3, x - 3);
        line(colour, y - 3, x + 3, y + 3, x - 3);
    } else {
        line(colour, y + 3, x, y - 3, x);
        line(colour, y, x + 3, y, x - 3);
    }
}

// Port (Modern gameplay): the snatch target's mark gets a one-pixel light
// red ring three pixels outside the disc of radius r and a "!" to its right.
void drawSnatchMark(int x, int y, int r) {
    constexpr u16 kColour = 0xFF0C;
    const int R = r + 3;
    // Midpoint circle, eight octants.
    int px = R, py = 0, err = 1 - R;
    Gfx& gx = gfx();
    while (px >= py) {
        gx.pixel(x + px, y + py, kColour);
        gx.pixel(x - px, y + py, kColour);
        gx.pixel(x + px, y - py, kColour);
        gx.pixel(x - px, y - py, kColour);
        gx.pixel(x + py, y + px, kColour);
        gx.pixel(x - py, y + px, kColour);
        gx.pixel(x + py, y - px, kColour);
        gx.pixel(x - py, y - px, kColour);
        ++py;
        if (err < 0) err += 2 * py + 1;
        else {
            --px;
            err += 2 * (py - px) + 1;
        }
    }
    const u8 saved = g_textColour;
    setColour(0x0C);
    text(y - 3, x + R + 2, "!");
    setColour(saved);
}

// map_draw_button (19ac:3839).
void drawButton(int n) {
    const MapButton& b = btn(n);
    const UiState& u = ui();
    Gfx& gx = gfx();
    const bool pressed = u.focus == n && u.pressed;
    gx.fillRect(b.x - 2, b.y - 2, b.w - 1, b.h - 1, u16(Gfx::kSolid | 0x11));
    if (!pressed) gx.rect(b.x - 1, b.y - 1, b.w - 2, b.h - 2, u16(Gfx::kSolid | 0x0F));
    gx.rect(b.x - 2, b.y - 2, b.w - 1, b.h - 1, u16(Gfx::kSolid | 0x07));
    const int p = pressed ? 1 : 0;
    text(b.y + p, b.x + p, orderName(n));
    const int ux = b.underline;
    line(u16(Gfx::kSolid | 0x08), b.y + p + 6, b.x + 4 * ux + 7, b.y + p + 6, b.x + 4 * (ux + 1));
}

// map_press_button (19ac:5231).
void pressButton(int n) {
    if (ms().mapSelTeam == 0) {
        mapFocusButtonInternal(n);
        ui().pressed = true;
    }
    ls().fullRedraw = 2;
}

} // namespace

const s16* mapButtonRect(int n) { return &btn(n).x; }

// ---------------------------------------------------------------------------
// Set-up and coordinates (19ac:2E69..2FE8)
// ---------------------------------------------------------------------------

void mapInit() {
    LoopState& L = ls();
    loadButtons();
    picLoad(PicMap);  // map_load_screen: mapscr.pic (the cursors are loaded at start-up)
    L.mapCursorMode = 0;
    ui().focus = 2;
    if (Team* t0 = team(0)) t0->map_height = 0x3E800;
    ui().cursorX = 100;
    ui().cursorY = 100;
    L.teamInfoNames = false;
}

void mapSelectTeamHeight() {
    LoopState& L = ls();
    const MissionState& S = ms();
    Team* t = team(S.mapSelTeam);
    if (!t || !L.cur) return;
    if (t->map_height == -1) {
        const Unit* pm = pointMan();
        const Unit* l = t->members[0];
        if (!pm || !l) return;
        const int d = geoDistance(pm->body->pos, l->body->pos);
        const s32 v = s32(u32(d + (400 - d % 400)) << 9);
        L.cur->pos.y = v;
        t->map_height = v;
    } else {
        L.cur->pos.y = t->map_height;
    }
}

void mapScreenToWorld(int sx, int sy, s32& x, s32& z) {
    const Camera& m = ls().camMap;
    const s32 hs = m.pos.y >> 8;
    z = wrapAdd(s32(u32(s32((s32(0x57 - sy) * hs) >> 8)) << 8), m.pos.z);
    x = wrapAdd(s32(u32(s32(sx - 0x6E)) * u32(hs)), m.pos.x);
}

bool mapWorldToScreen(const Vec3& pos, int& sx, int& sy) {
    const Camera& m = ls().camMap;
    const s32 v[3] = {s32(u32(wrapSub(pos.x, m.pos.x)) << 8), s32(u32(wrapSub(pos.z, m.pos.z)) << 8),
                      s32(u32(m.pos.y) << 8)};
    s16 px, py;
    render::projectPage(v, px, py);
    sx = px;
    sy = py;
    const MapButton& area = btn(0);
    return rectHasPoint(area.x, area.y, area.w, area.h, sx, sy);
}

// ---------------------------------------------------------------------------
// Routes and markers (19ac:30BA, 3300)
// ---------------------------------------------------------------------------

void mapDrawRoutes() {
    const MissionState& S = ms();
    for (int i = 0; i < 3; ++i) {
        int x0, y0, x1, y1;
        mapWorldToScreen(S.routeNodes[i], x0, y0);
        mapWorldToScreen(S.routeNodes[i + 1], x1, y1);
        line(0x5A0F, y0, x0, y1, x1);
    }
    int k = S.teamCount - 1;
    if (S.splitGroups == 2) {
        if (const Team* t = team(k); t && t->members[0]) {
            int x0, y0, x1, y1;
            mapWorldToScreen(t->members[0]->body->pos, x0, y0);
            mapWorldToScreen(S.wpSplitB, x1, y1);
            line(0x5A01, y0, x0, y1, x1);
        }
    }
    if (S.splitGroups != 0) {
        if (S.splitGroups == 2) --k;
        if (const Team* t = team(k); t && t->members[0]) {
            int x0, y0, x1, y1;
            mapWorldToScreen(t->members[0]->body->pos, x0, y0);
            mapWorldToScreen(S.wpSplitA, x1, y1);
            line(0x5A09, y0, x0, y1, x1);
        }
    }
}

void mapDrawMarkers() {
    const MissionState& S = ms();
    const MciHeader& m = S.mci;
    int sx, sy;
    static const u16 kObjColours[3] = {0xFF04, 0xFF0C, 0xFF0E};
    for (int i = 0; i < 3; ++i)
        if (u16(m.objective[i].kind) != 0 && mapWorldToScreen(m.objective[i].pos, sx, sy))
            drawTriangle(sx, sy, kObjColours[i], 0, false);
    if (mapWorldToScreen(S.wpSeal, sx, sy)) drawCross(sx, sy, 0xFF0F, true);
    if (mapWorldToScreen(S.wpSupport, sx, sy)) drawCross(sx, sy, 0xFF04, false);
    if (S.splitGroups != 0 && mapWorldToScreen(S.wpSplitA, sx, sy)) drawCross(sx, sy, 0xFF09, true);
    if (S.splitGroups > 1 && mapWorldToScreen(S.wpSplitB, sx, sy)) drawCross(sx, sy, 0xFF01, false);
    // Support craft destinations (teams 1..): shades 0x1F, 0x19, 0x13, ...
    u8 shade = 0x1F;
    for (int i = 1; i < kMaxTeams && S.teams[i]; ++i, shade = u8(shade - 6)) {
        const Team* t = S.teams[i];
        if (!isCraft(t) || !t->members[0] || !t->members[0]->mover) continue;
        const Vec3 dest = t->members[0]->mover->destination;
        if ((t->order == 5 || t->order == 4) && mapWorldToScreen(dest, sx, sy))
            drawTriangle(sx, sy, u16(0xFF00 | shade), 0, true);
    }
    fxMarkersHide(true);
    int n = 0;
    const Team* sel = team(S.mapSelTeam);
    for (int i = 0; i < kMaxTeams && S.teams[i]; ++i) {
        const Team* t = S.teams[i];
        for (int k = 0; k < 8 && t->members[k]; ++k) {
            Unit* u = t->members[k];
            if (!(u->body->flags & obj3d_flag::kEnabled)) continue;
            if (!mapWorldToScreen(u->body->pos, sx, sy)) continue;
            int colour;
            const bool targeted = S.playerTarget.kind == TargetKind::Unit && S.playerTarget.target.unit == u;
            if (isEnemyTeam(t) && (targeted || t == sel)) colour = 0xFF04;
            else colour = t->type == TeamType::Seal ? 0xFF01 : -1;
            if (colour == -1) continue;
            const s32 h = mapHeight() >> 8;
            int r = h != 0 ? int((16000 / h) >> 4) * 2 : 1;
            if (r < 1) r = 1;
            gfx().fillCircle(sx, sy, r, u16(colour));
            // Port: Modern gameplay rings the snatch target's mark and puts a
            // "!" beside it; only once the original has drawn the mark (his
            // team selected or he is the player's target), never earlier.
            if (isEnemyTeam(t) && modernIsSnatchTarget(u)) drawSnatchMark(sx, sy, r);
            if (t->type == TeamType::Seal && unitAlive(u)) fxPlaceUnitMarker(u, n++);
        }
    }
}

// ---------------------------------------------------------------------------
// Team list (19ac:39B7)
// ---------------------------------------------------------------------------

namespace {
// Colour of a team entry: 1 selected, 0xC for enemy teams, 9 for the others
// (the original compares the sides of the team and of team 0).
u8 teamListColour(int index, const Team* t) {
    if (index == ms().mapSelTeam) return 0x01;
    return isEnemyTeam(t) ? 0x0C : 0x09;
}
} // namespace

void mapDrawTeamList() {
    const MissionState& S = ms();
    const UiState& u = ui();
    if (S.viewMode == 0x0C && S.mapSelTeam != 0) return;
    setColour(0x01);
    if (S.mapSelTeam < 3) {
        for (int b = 2; b <= 4; ++b) {
            drawButton(b);
            const int k = b - 2;
            const Team* t = team(k);
            if (!t) continue;  // original: reads through a NULL team pointer
            setColour(teamListColour(k, t));
            const int p = (u.focus == b && u.pressed) ? 1 : 0;
            const int x = btn(b).x + p, y = btn(b).y + p;
            text(y, x + 4, utoa(b - 1));
            text(y, x + 8, dsText(kStrDot1));
            text(y, x + 0x0C, tableStr(kTeamTypeNames, int(t->type)));
            if (k != 0 && t->order != 6) drawTriangle(0xDA, y + 3, u16(0xFF00 | u8(0x25 - 6 * k)), 2, true);
        }
        return;
    }
    const Team* sel = team(S.mapSelTeam);
    if (!sel) return;
    if (!((S.mapSelTeam < 6 && !isEnemyTeam(sel)) || sel->type == TeamType::Seal)) return;
    int k = 3;
    const Team* t = team(k);
    while (t && isHostile(t)) t = team(++k);
    if (!t) return;
    for (int b = 5; b <= 7; ++b) {
        if (isHostile(t)) return;
        drawButton(b);
        setColour(teamListColour(k, t));
        const int p = (u.focus == b && u.pressed) ? 1 : 0;
        const int x = btn(b).x + p, y = btn(b).y + p;
        text(y, x + 4, utoa(b - 1));
        text(y, x + 8, dsText(kStrDot2));
        std::string name = tableStr(kTeamTypeNames, int(t->type));
        if (t->type == TeamType::Seal) {
            name += dsText((S.splitGroups == 2 && S.teamCount - k == 1) ? kStrTeamB : kStrTeamA);
        } else if (isCraft(t) && t->order != 6) {
            drawTriangle(0xDA, y + 3, u16(0xFF00 | u8(0x25 - 6 * k)), 2, true);
        }
        text(y, x + 0x0C, name);
        do {
            t = team(++k);
        } while (t && t->type != TeamType::Seal && isHostile(t));
        if (!t) return;
    }
}

// ---------------------------------------------------------------------------
// Orders menu (19ac:3E76)
// ---------------------------------------------------------------------------

void mapDrawOrdersMenu() {
    const MissionState& S = ms();
    if (S.viewMode != 0x0C) {
        const Team* t = team(S.mapSelTeam);
        setColour(0x00);
        text(btn(11).y - 8, btn(11).x, dsText(kStrTeamOrders));
        if (t && (t->type == TeamType::Seal || int(t->type) > 3)) {
            for (int b = 0x0B; b < 0x0F; ++b) {
                setColour(int(t->fire_order) == b - 0x0B ? 0x00 : 0x08);
                drawButton(b);
            }
            if (S.sealTeam == S.mapSelTeam) {
                for (int b = 0x0F; b < 0x13; ++b) {
                    setColour(t->order == b - 0x0F ? 0x00 : 0x08);
                    drawButton(b);
                }
            } else {
                for (int b = 0x1F; b < 0x23; ++b) {
                    const bool on = (b == 0x1F && t->order == 0) || (b == 0x20 && t->order == 4) || (b == 0x21 && t->order == 1);
                    setColour(on ? 0x00 : 0x08);
                    drawButton(b);
                }
                for (int b = 0x23; b < 0x27; ++b) {
                    const int fo = int(t->fire_order);
                    const bool on = (b == 0x23 && t->order == 2) || (b == 0x24 && fo == 6) || (b == 0x25 && fo == 5) ||
                                    (b == 0x26 && fo == 4);
                    setColour(on ? 0x00 : 0x08);
                    drawButton(b);
                }
            }
            if (S.sealTeam == S.mapSelTeam) {
                for (int b = 0x13; b < 0x17; ++b) {
                    setColour(int(t->formation) == b - 0x13 ? 0x00 : 0x08);
                    drawButton(b);
                }
            }
        } else if (t && isCraft(t)) {
            const int type = int(t->type);
            for (int b = 0x17; b < 0x1E; ++b) {
                const bool shown = b == 0x1A || (b == 0x17 && type == 1) || (b == 0x18 && type == 2) ||
                                   (b == 0x19 && type == 3) || (b == 0x1B && (type == 1 || type == 2)) ||
                                   (b == 0x1D && S.emergencyGroup == S.mapSelTeam) || b == 0x1C;
                if (!shown) continue;
                const bool on = (b >= 0x17 && b <= 0x19 && t->order == 5) || (b == 0x1A && t->order == 6) ||
                                (b == 0x1B && t->order == 2) || (b == 0x1D && t->order == 3) || (b == 0x1C && t->order == 4);
                setColour(on ? 0x00 : 0x08);
                drawButton(b);
            }
        }
    }
    for (int b = 0x27; b < 0x29; ++b) {
        setColour(0x08);
        drawButton(b);
    }
}

// ---------------------------------------------------------------------------
// Clock and info panel (19ac:417F, 421E)
// ---------------------------------------------------------------------------

void mapDrawClock(int x, int y, bool shadow) {
    const MissionState& S = ms();
    const u8 c = gfx().textFg();
    const std::string tmpl = dsText(kStrClockTmpl), hh = front::padNumber(S.todHour, 2),
                      mm = front::padNumber(S.todMinute, 2), ss = front::padNumber(S.todSecond, 2);
    if (shadow) {
        // Full-screen 3D HUD (hud.cpp): a black copy one pixel down and right,
        // drawn without the 4-pixel alignment of text4x6.
        Gfx& gx = gfx();
        gx.setTextColors(0x00, 0);
        gx.setTextOpaque(false);
        const int ax = x & ~3;
        gx.draw4x6String(font4x6(), tmpl, ax + 1, y + 1);
        gx.draw4x6String(font4x6(), hh, ax + 1, y + 1);
        gx.draw4x6String(font4x6(), mm, ax + 0x0C + 1, y + 1);
        gx.draw4x6String(font4x6(), ss, ax + 0x18 + 1, y + 1);
    }
    setColour(c);
    text(y, x, tmpl);
    text(y, x, hh);
    text(y, x + 0x0C, mm);
    text(y, x + 0x18, ss);
}

namespace {

// Colour of a Team Info row (19ac:4592..).
u8 teamInfoColour(const Team* t, const Unit* u) {
    if (unitDead(u)) return 0x07;
    const bool enemy = int(t->type) >= 4;
    const u8 bf = u->brain ? u->brain->flags : 0;
    if (enemy && (bf & brain_flag::kFleeing)) return 0x02;
    if (enemy && (bf & brain_flag::kSuppressed)) return 0x0A;
    if (enemy && (bf & brain_flag::kEngaging)) return 0x04;
    if (enemy && (bf & brain_flag::kAlerted)) return 0x0C;
    if (!enemy && u->status && u->status->bleeding) return 0x00;
    if (!enemy && u->status && u->status->heavy_wounds) return 0x08;
    if (!enemy || (bf & brain_flag::kActive)) return 0x01;
    return 0x09;
}

std::string lastName(const Unit* u) {
    if (!u->se) return {};
    return std::string(u->se->last_name, strnlen(u->se->last_name, sizeof u->se->last_name));
}

} // namespace

void mapDrawInfoPanel() {
    const LoopState& L = ls();
    const MissionState& S = ms();
    Gfx& gx = gfx();
    const CampaignHeader& h = campaign::header();
    const FlowEntry& flow = campaign::flow(h.year, h.mission);
    setColour(0x08);
    text(1, 8, exe().dgStringPtr(kVersion));
    gx.fillRect(0x30, 0xAB, 0x60, 7, u16(Gfx::kSolid | 0x11));
    setColour(0x08);
    text(0xAB, 0x30, dsText(kStrScale));
    // "1:" + (word DS:D873 >> 2): the middle word of the map height.
    const s16 scale = s16(u32(mapHeight()) >> 8);
    setColour(0x00);
    text(0xAB, 0x60, dsText(kStrOneTo) + utoa(scale >> 2));
    setColour(0x02);
    text(0xB4, 8, S.areaName);
    setColour(0x08);
    text(0xAB, 8, dsText(kStrPictoMap));
    static const u16 kObjText[3] = {kStrObjective1, kStrObjective2, kStrObjective3};
    static const u16 kObjColour[3] = {0xFF04, 0xFF0C, 0xFF0E};
    for (int i = 0; i < 3; ++i) {
        const int kind = int(u16(S.mci.objective[i].kind));
        if (kind == 0) continue;
        drawTriangle(0xDA, 0x0B + 6 * i, kObjColour[i], 2, false);
        setColour(0x08);
        text(0x0A + 6 * i, 0xE0, dsText(kObjText[i]));
        setColour(0x00);
        text(0x0A + 6 * i, 0x118, tableStr(kMissionTypes, kind));
    }
    setColour(0x00);
    gx.fillRect(0xDC, 2, 0x50, 7, u16(Gfx::kSolid | 0x11));
    text(3, 0x110, utoa(h.mission + 1));
    std::string month = tableStr(kMonthNames, flow.month);
    if (month.size() > 3) month.resize(3);
    text(3, 0x11C, month);
    text(3, 0x12C, utoa(h.year + 0x7AE));
    gx.setTextColors(0x00, 0);
    mapDrawClock(0xE0, 3);
    // Team Info box relative to button 4 (224, 56, 92, 11).
    const MapButton& b4 = btn(4);
    gx.fillRect(b4.x - 4, b4.y + 8, b4.w + 8, b4.h * 3, u16(Gfx::kSolid | 0x11));
    int rowY = b4.y;
    setColour(0x00);
    const Team* sel = team(S.mapSelTeam);
    const bool names = L.teamInfoNames && sel && sel->type == TeamType::Seal;
    text(b4.y + 10, b4.x - 4, dsText(names ? kStrHeaderName : kStrHeaderPos));
    rowY += 0x10;
    if (!sel) return;
    const Unit* pm = pointMan();
    for (int i = 0; i < 8 && sel->members[i]; ++i, rowY += 6) {
        const Unit* u = sel->members[i];
        setColour(teamInfoColour(sel, u));
        if (sel->type == TeamType::Seal) {
            // Role index: offset by the member count when another SEAL team is selected.
            int role = i;
            if (pm && sel != pm->team) role += teamMemberCount(sel);
            if (!L.teamInfoNames) {
                text(rowY, 0xDC, tableStr(kSealRoles, role));
            } else {
                std::string s = dsText(kStrEmpty);
                const int rank = u->roster ? u->roster->rank : 0;
                const int rating = u->se ? u->se->rating : 0;
                // Original quirk: for ranks >= 8 the table itself is read as the string.
                if (rank < 8) s += tableStr(kRankAbbrev, rating);
                else s += exe().dgString(kRankAbbrev);
                if (rating == 0 || rank != 0) s += tableStr(kRankSuffix, rank);
                text(rowY, 0xDC, s);
                text(rowY, 0xF8, lastName(u));
            }
        } else if (int(sel->type) < 4) {
            text(rowY, 0xDC, tableStr(kCraftAbbrev, int(sel->type)));
            text(rowY, 0xEC, utoa(i + 1));
        } else {
            int state = 0;
            if (!(u->brain && u->brain->surrendered)) {
                const int ot = sel->ai ? int(sel->ai->order_type) : 0;
                if (ot == 1) state = 1;
                else if (ot == 3) state = 2;
                else state = (sel->ai && (sel->ai->behaviour & 2)) ? 3 : 4;
            }
            text(rowY, 0xDC, tableStr(kEnemyState, state));
            text(rowY, 0xEC, utoa(u->se ? u->se->camouflage : 0));
            text(rowY, 0xF4, tableStr(kEnemyAlert, sel->ai ? (sel->ai->behaviour >> 4) : 0));
        }
        if (!L.teamInfoNames && u->mover) {
            std::string sp = std::to_string(u->mover->speed);
            while (sp.size() < 3) sp.insert(sp.begin(), ' ');
            text(rowY, 0x104, sp);
        }
        if (!L.teamInfoNames && u->mover && u->status && int(u->status->unit_class) < 5)
            text(rowY, 0xF8, unitDead(u) ? exe().dgStringPtr(kDeadAbbrev) : tableStr(kPostureAbbrev, u8(u->mover->posture)));
        if (!L.teamInfoNames) {
            const int hd = std::abs(std::abs(u->body->heading >> 3) - 360) % 360;
            text(rowY, 0x114, utoa(hd));
        }
        setColour((u->brain && (u->brain->flags & brain_flag::kContact)) ? 0x0C : 0x00);
        if (u->loadout) {
            const WeaponNode* w = L.teamInfoNames ? u->loadout->secondary : u->loadout->primary;
            if (w) {
                text(rowY, 0x124, weaponDef(u8(w->type)).short_name);
                // The column has room for one digit before the screen edge;
                // "Modern gameplay" loadouts (12, 20 magazines) are drawn
                // right-aligned to the same edge (the original never shows
                // more than one digit here).
                const std::string mags = utoa(w->reloads);
                const int x = settings().effectiveModernGameplay() && mags.size() > 1 ? 0x13C - 4 * (int(mags.size()) - 1) : 0x13C;
                text(rowY, x, mags);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Pointer, focus and hit testing (19ac:4BF0..511E)
// ---------------------------------------------------------------------------

namespace {

// map_hit_test (19ac:4BF0).
int mapHitTest() {
    const MissionState& S = ms();
    const Team* sel = team(S.mapSelTeam);
    const int type = sel ? int(sel->type) : 0;
    const UiState& u = ui();
    for (int c = 0; c <= 0x28; ++c) {
        if (c == 2 && S.mapSelTeam > 2) c = 5;
        if (c == 0x17 && (type < 1 || type > 3)) c = 0x27;
        if (c == 0x0B && type > 0 && type < 4) c = type == 2 ? 0x18 : type == 3 ? 0x19 : 0x17;
        if (c == 0x1D && S.emergencyGroup != S.mapSelTeam) c = 0x1E;
        const MapButton& b = btn(c);
        if (rectHasPoint(b.x, b.y, b.w, b.h - 2, u.cursorX, u.cursorY)) return c;
    }
    return -1;
}

// map_step_focus (19ac:4D0A).
void mapStepFocus(int dir) {
    const MissionState& S = ms();
    const Team* sel = team(S.mapSelTeam);
    const int type = sel ? int(sel->type) : 0;
    const bool craft = type > 0 && type < 4;
    int& f = ui().focus;
    if (dir < 0) {
        if (f == 0) {
            f = 0x28;
            return;
        }
        --f;
        if (f == 0x26) {
            if (S.sealTeam == S.mapSelTeam || type > 3) f = 0x16;
            else if (craft) f = 0x1D;
        }
        if (f == 0x1E && type == 0) f = 0x0E;
        if (f == 0x1D && S.emergencyGroup != S.mapSelTeam) f = 0x1C;
        if (f == 0x1B && type == 3) f = 0x1A;
        if (f == 0x19) {
            if (type == 2) f = 0x18;
            else if (type == 1) f = 0x17;
        }
        if (f == 0x18 && type == 3) f = 0x0A;
        if (f == 0x17 && type == 2) f = 0x0A;
        if (f == 0x16 && craft) f = S.mapSelTeam < 3 ? 4 : 7;
        if (f == 0x0A) f = type == 4 ? 7 : 4;
        if (f == 4 && type == 4) f = 1;
        return;
    }
    if (dir == 0) return;
    if (f == 0x28) {
        f = 0;
        return;
    }
    ++f;
    if (f == 5) f = 0x0B;
    if (f == 8) f = 0x0B;
    if (f == 0x0B && craft) f = type == 2 ? 0x18 : type == 3 ? 0x19 : 0x17;
    if (f == 0x0F && type == 0) f = 0x1F;
    if (f == 0x18 && type == 1) f = 0x1A;
    if (f == 0x19 && type == 2) f = 0x1A;
    if (f == 0x1B && type == 3) f = 0x1C;
    if (f == 0x1D && S.emergencyGroup != S.mapSelTeam) f = 0x27;
    if (f == 0x1E) f = 0x27;
    if (f == 0x17 && (S.sealTeam == S.mapSelTeam || type > 3)) f = 0x27;
}

// map_move_pointer (19ac:508C).
void mapMovePointer(int dx, int dy) {
    LoopState& L = ls();
    UiState& u = ui();
    if (L.mapCursorMode == 1) {
        uiCursorMove(dx, dy);
        if (mapHitTest() != 0) L.mapCursorMode = 0;
        return;
    }
    if (L.mapCursorMode == 2) {
        uiCursorMove(dx, dy);
        const int hit = mapHitTest();
        if (hit != -1) u.focus = hit;
    } else if (L.mapCursorMode == 0) {
        if (dx > 0 || (dx == 0 && dy > 0)) mapStepFocus(1);
        else if (dx < 0 || dy < 0) mapStepFocus(-1);
        mapFocusButtonInternal(u.focus);
    } else {
        return;
    }
    if (u.focus == 0) L.mapCursorMode = 1;
}

// map_zoom_keys (19ac:511E): '+' / 'x' expand, '-' / 'z' zoom, arrows move the pointer.
bool mapZoomKeys(int key) {
    LoopState& L = ls();
    const MissionState& S = ms();
    Camera& m = L.camMap;
    auto store = [&] {
        if (Team* t = team(S.mapSelTeam); t && L.cur) t->map_height = L.cur->pos.y;
    };
    if (key == '+' || key == 'x') {
        if (m.pos.y < 0x3E8000) m.pos.y = wrapAdd(m.pos.y, 0x3E800);
        store();
        if (hasPointer()) return true;
        mapFocusButtonInternal(0x27);
        ui().pressed = true;
        return true;
    }
    if (key == '-' || key == 'z') {
        if (m.pos.y > 0x3E800) m.pos.y = wrapSub(m.pos.y, 0x3E800);
        store();
        if (hasPointer()) return true;
        mapFocusButtonInternal(0x28);
        ui().pressed = true;
        return true;
    }
    switch (key) {
    case engine::key::Up: mapMovePointer(0, -5); return true;
    case engine::key::Left: mapMovePointer(-8, 0); return true;
    case engine::key::Right: mapMovePointer(8, 0); return true;
    case engine::key::Down: mapMovePointer(0, 5); return true;
    default: return false;
    }
}

// A waypoint from the cursor position (Enter / space on the map).
void waypointFromCursor() {
    s32 x, z;
    mapScreenToWorld(ui().cursorX, ui().cursorY, x, z);
    mapSetWaypoint(x, z);
}

// Leave the map (space / 'm' on the map screen).
void leaveMap() {
    cursorReset();
    if (ms().opt.map == 0) {
        clkRestore();
        engine::input().resetRepeatTimers();
    }
    evtForceCraftEngineSound();
    if (ls().prevViewMode == 2) viewSetChase();
    else viewSetFirstPerson();
}

void mapFocusButtonInternal(int n) {
    const MapButton& b = btn(n);
    UiState& u = ui();
    u.focus = n;
    u.cursorX = b.x + (b.w >> 1);
    u.cursorY = b.y + (b.h >> 1);
    ls().mapCursorMode = 0;
    uiCursorMove(0, 0);
}

} // namespace

// Port: the mouse wheel on the map screen, a notch per Zoom / Expand press
// (map_zoom_keys); up brings the map closer.
void mapWheelZoom(int notches) {
    for (; notches > 0; --notches) mapZoomKeys('-');
    for (; notches < 0; ++notches) mapZoomKeys('+');
}

// ---------------------------------------------------------------------------
// map_screen_keys (19ac:52A1)
// ---------------------------------------------------------------------------

void mapScreenKeys(int key, int dx, int dy) {
    LoopState& L = ls();
    MissionState& S = ms();
    UiState& u = ui();
    if (dx == 0 && dy == 0) {
        if (mapHitTest() != 0) L.mapCursorMode = 0;
    } else {
        if (L.mapCursorMode != 1) L.mapCursorMode = 2;
        mapMovePointer(dx, dy);
    }
    uiButtonRelease(key);
    if (u.releaseCount != 0) {
        --u.releaseCount;
        L.fullRedraw = 2;
    }
    if (mapZoomKeys(key)) return;
    if (key == 9 || key == 0x0F00) {
        // Tab / Shift-Tab: next / previous friendly team.
        for (;;) {
            if (key == 9) {
                const Team* next = team(S.mapSelTeam + 1);
                if (!next || !next->members[0]) S.mapSelTeam = 0;
                else ++S.mapSelTeam;
            } else {
                S.mapSelTeam = S.mapSelTeam == 0 ? S.teamCount - 1 : S.mapSelTeam - 1;
            }
            const Team* t = team(S.mapSelTeam);
            if (t && int(t->type) < 4) break;
            if (!t) {  // port guard: the original walks the NULL-terminated table
                S.mapSelTeam = 0;
                break;
            }
        }
        mapSelectTeamHeight();
        int b;
        if (S.mapSelTeam < 6) {
            b = S.mapSelTeam + 2;
        } else {
            int v;
            if (S.splitGroups < 2 || S.teamCount - S.mapSelTeam != 1) v = S.firstMtmGroup > 3 ? 1 : 0;
            else v = S.firstMtmGroup < 4 ? 1 : 2;
            b = v + 5;
        }
        mapFocusButtonInternal(b);
        L.fullRedraw = 2;
        u.pressed = true;
        return;
    }
    if (key == engine::key::Enter) {
        if (L.mapCursorMode == 0) {
            const int hk = btn(u.focus).key;
            if (hk != engine::key::Enter) {
                cmdOrderKeys(hk);
                mapScreenKeys(hk, 0, 0);
                L.fullRedraw = 2;
                u.pressed = true;
            }
            return;
        }
        if (L.mapCursorMode != 1) return;
        waypointFromCursor();
        u.pressed = true;
        return;
    }
    if (key == engine::key::Space || key == 'm') {
        leaveMap();
        return;
    }
    if (key >= '1' && key <= '6') {
        int k = key - '1';
        const Team* t = team(k);
        if (k > 2) {
            int n = (S.firstMtmGroup < 4 ? 1 : 0) + k - 3;
            while (t) {
                if (isHostile(t)) {
                    t = team(++k);
                    continue;
                }
                if (n == 0) break;
                if (--n > 0) {
                    t = team(++k);
                    continue;
                }
                break;
            }
        }
        if (!t) return;
        S.mapSelTeam = k;
        if (key - '1' < 6) mapFocusButtonInternal(key - '1' + 2);
        mapSelectTeamHeight();
        u.pressed = true;
        L.fullRedraw = 2;
        cursorReset();
        return;
    }
    switch (key) {
    case 'a':
    case 'b':
    case 'u':
    case 'k':
    case 'o':
    case 'e':
    case 'y': {
        const int b = mapOrderKey(key);
        if (b >= 0) {
            mapFocusButtonInternal(b);
            u.pressed = true;
            L.fullRedraw = 2;
        } else if (key == 'o' && !S.radioDamaged && S.mapSelTeam != 0 && S.mapSelTeam < S.firstMtmGroup &&
                   team(S.mapSelTeam)) {
            // 19ac:~5A10: only the "Engine malfunction!" path (radio intact, a craft
            // selected) sets the loiter order and forces the redraw.
            L.fullRedraw = 2;
        }
        return;
    }
    case 'f':
    case 't':
    case 'w':
        pressButton(mapOrderKey(key));  // also sets team 0's fire order
        return;
    case 'c': pressButton(0x0E); return;
    case 'd': pressButton(0x15); return;
    case 'h': pressButton(0x0F); return;
    case 'i': pressButton(0x14); return;
    case 'l': pressButton(0x13); return;
    case 's': pressButton(0x10); return;
    case 'v': pressButton(0x16); return;
    case 'p':
        pressButton(0x11);
        L.fullRedraw = 2;
        return;
    case 'j':
        pressButton(0x12);
        L.fullRedraw = 2;
        return;
    case 0x1400:  // Alt-T: the clock button
        mapFocusButtonInternal(1);
        u.pressed = true;
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// insert_keys (19ac:5C8E)
// ---------------------------------------------------------------------------

void insertKeys(int key, int dx, int dy) {
    LoopState& L = ls();
    MissionState& S = ms();
    UiState& u = ui();
    if (dx != 0 && S.viewMode == 7 && key == engine::key::Space)
        L.orbitHeading = s16(angleWrap(L.orbitHeading + (dx > 0 ? -80 : 80)));
    if (S.viewMode == 0x0C) {
        if (dx == 0 && dy == 0) {
            if (mapHitTest() != 0) L.mapCursorMode = 0;
        } else {
            if (L.mapCursorMode != 1) L.mapCursorMode = 2;
            mapMovePointer(dx, dy);
        }
    }
    if (mapZoomKeys(key)) return;
    if (key == 'r') {
        if (S.viewMode == 7) {
            reinsertBegin();
        } else if (S.viewMode == 0x0C) {
            if (reinsertConfirm()) {
                clkReset();
                engine::input().resetRepeatTimers();
            }
        }
        return;
    }
    if (key > 'r') return;
    if ((key & 0xFF) == engine::key::Enter) {
        if (L.mapCursorMode == 0) {
            const int hk = btn(u.focus).key;
            if (hk == engine::key::Enter) return;
            insertKeys(hk, 0, 0);
            L.fullRedraw = 2;
            u.pressed = true;
            return;
        }
        if (L.mapCursorMode != 1) return;
    } else {
        if ((key & 0xFF) != engine::key::Space) return;
        if (L.mapCursorMode != 1) return;
    }
    // A new insertion point (DS:0EA0 = wpSupport) from the cursor.
    s32 x, z;
    mapScreenToWorld(u.cursorX, u.cursorY, x, z);
    S.wpSupport.x = x;
    S.wpSupport.z = z;
    u.pressed = true;
}

} // namespace loop
} // namespace st::game::mission
