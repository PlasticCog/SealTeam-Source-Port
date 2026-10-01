// The HUD of the field views (1000:0900..18EB: compass tape, objective
// marker, target diamond and names, text lines, clock box) and the drawing
// of the mission message queue (1000:79F7 msg_draw_signal_icon, 80F1
// msg_draw, 8339 msg_draw_queue). docs/re/seg_1000.md 9-10.
#include "game/mission/loop.h"

#include "core/settings.h"
#include "data/exeimage.h"
#include "game/front/common.h"
#include "game/globals.h"
#include "game/mission/build.h"
#include "game/mission/combat.h"
#include "game/mission/entity.h"
#include "game/mission/exedata.h"
#include "game/mission/geo.h"
#include "game/mission/modern.h"
#include "game/mission/msg.h"
#include "game/mission/state.h"
#include "game/mission/world.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/font.h"
#include "gfx/gfx.h"
#include "render/r3d.h"
#include "render/r3dhires.h"
#include "render/sprites.h"

#include <cstdlib>
#include <string>

namespace st::game::mission {
namespace loop {

namespace {

constexpr u16 kRangeColors = 0x0540;     // u8[4] by range band
constexpr u16 kStrMeters = 0x0544;       // " m"
constexpr u16 kStrObjectiveObj = 0x0547; // " Objective"
constexpr u16 kStrTeamB = 0x0552;        // " b"
constexpr u16 kStrTeamA = 0x0555;        // " a"
constexpr u16 kStrObjectiveTeam = 0x0558;// " Objective"
constexpr u16 kStrDemo = 0x0563;         // "DEMO: "
constexpr u16 kStrSp1 = 0x056A;          // " "
constexpr u16 kStrColon1 = 0x056C;       // ":"
constexpr u16 kStrSp2 = 0x056E;          // " "
constexpr u16 kStrParen1 = 0x0570;       // " ("
constexpr u16 kStrZero = 0x0573;         // "0"
constexpr u16 kStrParen2 = 0x0575;       // ")"
constexpr u16 kStrSp3 = 0x0577;          // " "
constexpr u16 kStrSp4 = 0x0579;          // " "
constexpr u16 kStrColon2 = 0x057B;       // ":"
constexpr u16 kStrReloading = 0x0586;    // "Reloading "
constexpr u16 kStrParen3 = 0x0591;       // " ("
constexpr u16 kStrParen4 = 0x0594;       // ")"
constexpr u16 kStrDetail = 0x0596;       // "Detail Level: "
constexpr u16 kTeamTypeNames = 0x1C7C;   // char*[8]
constexpr u16 kFireModeNames = 0x1CA2;   // char*[8]
constexpr u16 kCompassLabels = 0x1D84;   // char*[12]
constexpr u16 kSignalIconFrames = 0x36E8;// s16[]: first frame of each hand-signal icon

const Camera& cam() { return *ls().cur; }

// str_itoa_pad (19ac:5F83): decimal with a fixed width, space or zero padded.
std::string itoaPad(int v, int width, char pad) {
    const bool neg = v < 0;
    std::string s = std::to_string(neg ? -v : v);
    if (neg) s.insert(s.begin(), '-');
    while (int(s.size()) < width) s.insert(s.begin(), pad);
    return s;
}

std::string utoa(int v) { return std::to_string(unsigned(u16(v))); }

// Enhanced full-screen 3D: the HUD is drawn over the scene. Every element
// gets a dark backing strip in the high-resolution layer (render::
// hudBackingRect: the glyph rows plus the shadow row, one page pixel to
// either side, so the strips of consecutive text rows join without
// overlapping) and 4x6 text a 1-pixel black drop shadow; the positions are
// the original's.
void backing(int x, int y, int w, int h, u8 once = 0) {
    if (hudOverScene()) render::hudBackingRect(x, y, w, h, once);
}

// The shadow is drawn without the 4-pixel alignment of text4x6 (x is aligned already).
void shadow4x6(int y, int x, const std::string& s) {
    Gfx& gx = gfx();
    gx.setTextColors(0x00, 0);
    gx.setTextOpaque(false);
    gx.draw4x6String(font4x6(), s, x + 1, y + 1);
}

// 4x6 text in colour 15 (colour word DS:5074 = 0xFF0F, DS:F424 = 0);
// backed and shadowed over the scene.
void text(int y, int x, const std::string& s) {
    x &= ~3;
    if (hudOverScene()) {
        backing(x - 1, y, 4 * int(s.size()) + 3, 7);
        shadow4x6(y, x, s);
    }
    front::text4x6(y, x, s, 0x0F);
}

// 4x6 text inside an element that has its own backing (the compass tape).
void textOnBacking(int y, int x, const std::string& s) {
    x &= ~3;
    if (hudOverScene()) shadow4x6(y, x, s);
    front::text4x6(y, x, s, 0x0F);
}

// Proportional-font text of the message queue, likewise.
void textProp(int x, int y, const std::string& s) {
    if (hudOverScene()) {
        const Font* f = gfx().font();
        backing(x - 1, y, textWidth(s) + 3, (f ? f->height() : 0) + 1);
        drawTextShadow(x, y, s, 0x00, 0x0F);
    } else {
        drawText(x, y, s, 0x0F);
    }
}
void textPropCentered(int y, const std::string& s) { textProp(gfx().clipCx() - textWidth(s) / 2, y, s); }

// gfx_line(colour, y1, x1, y2, x2) of the original (2255:1962).
void line(u8 c, int y1, int x1, int y2, int x2) { gfx().line(x1, y1, x2, y2, u16(Gfx::kSolid | c)); }

// hud_draw_target_diamond (1000:0BDB).
void drawTargetDiamond(int x, int y, int range, u8 colour) {
    const Camera& v = cam();
    const Unit* pm = pointMan();
    if ((colour == exe().dgByte(kRangeColors) || !pm || u8(pm->mover->move_mode) != 2) && v.rect_y < y) {
        line(colour, y, x + 8, y - 8, x);
        line(colour, y + 8, x, y, x + 8);
        line(colour, y, x - 8, y + 8, x);
        line(colour, y - 8, x, y, x - 8);
    }
    y += 10;
    if (v.rect_y < y && y < v.rect_y + v.rect_h - 8 && v.rect_x < x - 8 && x < v.rect_x + v.rect_w - 0x14)
        text(y, x - 8, itoaPad((range / 3) >> 2, 3, ' ') + dsText(kStrMeters));
}

// 360 - heading in degrees (0 stays 0), the compass column origin.
int compassOrigin(int headingDeg) {
    const int d = headingDeg - 360;
    return d == -360 ? 0 : -d;
}

} // namespace

bool hudOverScene() {
    if (!settings().effectiveFullScreen3d()) return false;
    const int m = ms().viewMode;
    return m != 1 && m != 0x0C;
}

// text4x6_draw_centered (365e:94DE): x = (clip_cx - 2*len + 1) & ~3.
void hudText4x6Centered(int y, const std::string& s) {
    text(y, (gfx().clipCx() - 2 * int(s.size()) + 1) & ~3, s);
}

// ---------------------------------------------------------------------------
// Compass tape (1000:0900)
// ---------------------------------------------------------------------------

// The compass tape (labels row rect_y + 6, tape line rect_y + 0x10, heading
// mark rect_y + 0x11) and the objective marker below it (rect_y + 0x12 ..
// 0x17) share one backing strip over the scene: x 0x40..0xFF, the tape's
// rows and the marker's rows are drawn adjacent to each other.
constexpr int kCompassStripX = 0x40, kCompassStripW = 0xC0;

void hudDrawCompass() {
    const LoopState& L = ls();
    if (!L.compassEnabled) return;
    const Unit* pm = pointMan();
    if (!pm) return;
    const Camera& v = cam();
    const int c = compassOrigin(pm->body->heading >> 3);
    const int y0 = v.rect_y + 0x10;
    backing(kCompassStripX, v.rect_y + 5, kCompassStripW, 0x11 - 5 + 1);
    line(0x0F, y0, 0xF9, y0, 0x45);
    int a = c - 0x2D;
    if (a < 0) a = c + 0x13B;
    for (int k = 0; k < 0xB5; k += 2) {
        const int x = 0x45 + k;
        if (a % 15 == 0) line(0x0F, v.rect_y + 0x0F - 1, x, y0, x);
        else if (a % 5 == 0) line(0x0F, y0 - 1, x, y0, x);
        if (a == c) line(0x0F, v.rect_y + 0x11, x, y0, x);
        if (a % 30 == 0) textOnBacking(v.rect_y + 6, 0x41 + k, exe().dgStringPtr(u16(kCompassLabels + 2 * (a / 30))));
        if (++a == 360) a = 0;
    }
}

// ---------------------------------------------------------------------------
// Objective marker (1000:0A2C)
// ---------------------------------------------------------------------------

void hudDrawObjectiveMarker() {
    const Unit* pm = pointMan();
    if (!pm) return;
    const MissionState& S = ms();
    const Camera& v = cam();
    const int c = compassOrigin(pm->body->heading >> 3);
    int o = -1;
    if (geoDistance(pm->body->pos, S.wpSeal) >= 15) o = compassOrigin(geoBearing(pm->body->pos, S.wpSeal));
    const int y = v.rect_y;
    // Under the compass tape the marker's rows continue the tape's strip;
    // without a tape (the chase view) only the arrow itself is backed.
    const bool underTape = S.viewMode == 0 && ls().compassEnabled;
    if (o != -1 && underTape) backing(kCompassStripX, y + 0x12, kCompassStripW, 0x18 - 0x12 + 1);
    int a = c - 0x2D;
    if (a < 0) a = c + 0x13B;
    for (int k = 0; k < 0xB5; k += 2) {
        const int x = 0x45 + k;
        if (o == a) {
            if (!underTape) backing(x - 4, y + 0x11, 9, 6);
            line(0x0F, y + 0x12, x, y + 0x15, x - 3);
            line(0x0F, y + 0x15, x + 3, y + 0x12, x);
            o = -1;
        }
        if (++a == 360) a = 0;
    }
    if (o == -1) return;
    if (c + 0xB4 > 0x167 && o < c - 0xB4) o += 0x168;
    int x1, x2;
    if (c < o && o < c + 0xB4) {
        if (!underTape) backing(0xF6, y + 0x10, 6, 9);
        line(0x0F, y + 0x14, 0xFA, y + 0x11, 0xF7);
        x1 = 0xF7;
        x2 = 0xFA;
    } else {
        if (!underTape) backing(0x44, y + 0x10, 6, 9);
        line(0x0F, y + 0x14, 0x45, y + 0x11, 0x48);
        x1 = 0x48;
        x2 = 0x45;
    }
    line(0x0F, y + 0x17, x1, y + 0x14, x2);
}

// ---------------------------------------------------------------------------
// Target info (1000:0D44)
// ---------------------------------------------------------------------------

void hudDrawTargetInfo() {
    const LoopState& L = ls();
    const MissionState& S = ms();
    const Unit* pm = pointMan();
    if (!pm || L.fullRedraw != 0) return;
    const Camera& v = cam();
    const render::RenderContext& ctx = render::renderContext();
    const int rx = ctx.reticleX, ry = ctx.reticleY;
    if (rx < v.rect_x || rx >= v.rect_x + v.rect_w || ry < v.rect_y || ry >= v.rect_y + v.rect_h) return;
    const TargetRec& tr = S.playerTarget;
    const WeaponNode* w = pm->loadout ? pm->loadout->primary : nullptr;
    auto diamond = [&] {
        const int band = w ? wpnRangeBand(w, tr.range) : 0;
        drawTargetDiamond(rx, ry, tr.range, exe().dgByte(u16(kRangeColors + band)));
    };
    if (tr.kind == TargetKind::Structure) {
        const WorldObject* obj = tr.target.structure;
        if (!obj) return;  // original: falls back to the first enemy team read as a unit
        diamond();
        const std::string name = modelName(obj->model);
        int x = rx - 2 * (int(name.size()) & ~1);
        int y = ry + 0x10;
        if (v.rect_y < y && y < v.rect_y + v.rect_h - 8 && v.rect_x < x && x < v.rect_x + v.rect_w - 0x20) text(y, x, name);
        x = rx - 0x14;
        y = ry + 0x16;
        if (!msnIsObjectiveStructure(obj)) return;
        if (y <= v.rect_y || y >= v.rect_y + v.rect_h - 8 || x <= v.rect_x || x >= v.rect_x + v.rect_w - 0x28) return;
        text(y, x, dsText(kStrObjectiveObj));
        return;
    }
    if (tr.kind != TargetKind::Unit) return;
    const Unit* u = tr.target.unit;
    if (!u || !u->team) return;
    diamond();
    int y = ry + 0x10;
    int x = rx - 0x10;
    std::string name = exe().dgStringPtr(u16(kTeamTypeNames + 2 * int(u->team->type)));
    if (u->team->type == TeamType::Seal) {
        // Split fire teams: " b" for the last team when two splits exist, else " a".
        const bool b = S.splitGroups >= 2 && u->team == team(S.teamCount - 1);
        name += dsText(b ? kStrTeamB : kStrTeamA);
        x -= 4;
    }
    // Port: Modern gameplay names the snatch target ("Viet Cong (target)",
    // 72 pixels; the line is clipped to the view like the original's).
    if (modernIsSnatchTarget(u)) name += " (target)";
    if (v.rect_y < y && y < v.rect_y + v.rect_h - 8 && v.rect_x < x && x < v.rect_x + v.rect_w - 0x30) text(y, x, name);
    x = rx - 0x14;
    y = ry + 0x16;
    if (y <= v.rect_y || y >= v.rect_y + v.rect_h - 8 || x <= v.rect_x || x >= v.rect_x + v.rect_w - 0x28) return;
    if (!msnIsObjectiveTeam(u->team)) return;
    text(y, x, dsText(kStrObjectiveTeam));
}

// ---------------------------------------------------------------------------
// Text lines (1000:10C2)
// ---------------------------------------------------------------------------

void hudDrawTextLines() {
    const MissionState& S = ms();
    const Unit* pm = pointMan();
    if (!pm) return;
    const Camera& v = cam();
    const int yb = v.rect_y + v.rect_h;
    if (S.time < S.demoExplodeTime)
        text(yb - 0x1C, 4, dsText(kStrDemo) + itoaPad(int(wrapSub(S.demoExplodeTime, S.time) >> 8), 2, '0'));
    const Loadout* lo = pm->loadout;
    const WeaponNode* tool = lo ? lo->secondary : nullptr;
    if ((S.time < S.grenadeInfoTime || S.grenadeAiming) && tool) {
        const WeaponDef& d = weaponDef(u8(tool->type));
        text(yb - 7, 4, std::string(d.short_name) + dsText(kStrSp1) + d.long_name + dsText(kStrColon1) +
                            utoa(tool->rounds + tool->reloads));
    }
    const WeaponNode* w = lo ? lo->primary : nullptr;
    if (S.time < S.weaponInfoTime && w) {
        const WeaponDef& d = weaponDef(u8(w->type));
        std::string s = std::string(d.short_name) + dsText(kStrSp2) + d.long_name + dsText(kStrParen1);
        if (S.weaponReadyTime <= S.time) {
            int rounds;
            if (u8(w->type) == 10) rounds = (w->fire_mode & 8) ? 3 - w->m203_count : w->rounds + w->m203_count - 3;
            else rounds = w->rounds;
            s += utoa(rounds);
        } else {
            s += dsText(kStrZero);
        }
        s += dsText(kStrParen2);
        if (w->fire_mode & 0x0F) {
            int n = 0;
            for (int m = w->fire_mode & 0x0F; m != 0; m >>= 1) ++n;
            s += dsText(kStrSp3) + exe().dgStringPtr(u16(kFireModeNames + 2 * n));
        }
        text(yb - 0x0E, 4, s);
    }
    if (S.time < S.toolInfoTime && pm->items) {
        const ItemDef& d = itemDef(u8(pm->items->type));
        text(yb - 0x0E, 0xCC, std::string(d.short_name) + dsText(kStrSp4) + d.long_name + dsText(kStrColon2) +
                                  utoa(pm->items->quantity));
    }
    // "Team a: <order>" (g_team_msg_ptr DS:0514): the game never sets the
    // pointer, the line is dead code in the shipped executable.
    if (S.time < S.weaponReadyTime && w) {
        const WeaponDef& d = weaponDef(u8(w->type));
        text(yb - 0x15, 4, dsText(kStrReloading) + d.short_name + dsText(kStrParen3) + utoa(w->reloads) + dsText(kStrParen4));
    }
    if (S.time < g().detailMsgTime) text(yb - 7, 0x100, dsText(kStrDetail) + utoa(g().detailLevel + 1));
}

// ---------------------------------------------------------------------------
// Clock box of the time compression (1000:18EB)
// ---------------------------------------------------------------------------

void hudDrawClockBox() {
    const MissionState& S = ms();
    Gfx& gx = gfx();
    if (S.viewMode == 1) {
        gx.fillRect(0xDF, 3, 0x22, 7, u16(Gfx::kSolid | 0x11));
        gx.setTextColors(0x00, 0);
        mapDrawClock(0xE0, 3);
    } else {
        const Camera& v = cam();
        const int yb = v.rect_y + v.rect_h;
        const bool over = hudOverScene();
        // Over the scene the black box becomes a backing strip, darkened once
        // per rendered frame (time compression redraws the clock without
        // rendering), and the digits get the drop shadow.
        if (over) backing(0x92, yb + 1, 0x22, 8, 1);
        else gx.fillRect(0x92, yb + 1, 0x22, 8, u16(Gfx::kSolid | 0x00));
        gx.setTextColors(0x0F, 0);
        mapDrawClock(0x94, yb + 2, over);
    }
}

// ---------------------------------------------------------------------------
// Message queue drawing (1000:79F7, 80F1, 8339)
// ---------------------------------------------------------------------------

namespace {

// msg_draw_signal_icon (1000:79F7): the animated hand-signal icon of the
// hnds bank; progress = (g_time - start) * 10 / duration.
void drawSignalIcon(int x, int y, int icon, Ticks expiry, int duration) {
    if (icon == -1) return;
    const MissionState& S = ms();
    s32 progress = 0;
    if (duration != 0) progress = s32((std::int64_t(wrapSub(S.time, expiry)) + duration) * 10 / duration);
    if (progress < 0) progress = 0;
    const int first = exe().dgShort(u16(kSignalIconFrames + 2 * icon));
    const int count = exe().dgShort(u16(kSignalIconFrames + 2 * (icon + 1))) - first;
    int f = int(progress);
    if (count < f) f = count;
    if (f != 0) f += first - 1;
    if (const render::SpriteImage* bg = render::spriteFrame("hnds", 4, 5)) {
        const int w = bg->w(), h = bg->h();
        gfx().spriteScaled(x - (w >> 1), y - h, w, h, bg->rle.data());
    }
    if (const render::SpriteImage* fr = render::spriteFrame("hnds", std::abs(f) >> 3, f % 8))
        gfx().spriteScaled(x - 0x0F, y - 0x1F, 0x1E, 0x1E, fr->rle.data());
    gfx().setSpriteRemap(nullptr);
}

FontId kindFont(int kind) {
    switch (kind) {
    case 1: return FontId::Title;      // propbold
    case 2: return FontId::Clipboard;  // memo
    default: return FontId::Dialog;    // prop
    }
}

int fontHeight() {
    const Font* f = gfx().font();
    return f ? f->height() : 0;
}

} // namespace

void msgDraw() {
    const MissionState& S = ms();
    MessageQueue& q = messages();
    const Message& e = q.entries[size_t(q.head)];
    if (e.style == -1) return;
    const Message& n = q.entries[size_t((q.head + 1) % 8)];
    const bool mapMode = S.viewMode == 1 || S.viewMode == 0x0C;
    if (e.kind == 3) {
        int y;
        if (mapMode) y = e.style == 3 ? 0x99 : 0xC1;
        else y = S.viewMode == 0 ? 0x96 : 0x84;
        if (e.style == 3) {
            drawSignalIcon(0x22, y - 0x14, e.icon, e.expiry, e.duration);
            text(y - 0x3C, 0x22 - 2 * int(e.text.size()), e.text);
        } else {
            hudText4x6Centered(y, e.text);
        }
        if (q.count <= 1 || e.style != 3 || n.style == -1) return;
        if (n.style == 3) text(y - 0x11, (0x11 - int(n.text.size())) * 2, n.text);
        else hudText4x6Centered(y + 7, n.text);
        return;
    }
    if (e.kind == 4) return;  // medal pictures (msg_draw_signal_picture) are never queued in a mission
    fontSelect(kindFont(e.kind));
    const int y = e.style == 6 ? 0x11 : (0x62 - fontHeight()) * 2;
    if (e.style == 6) textProp(0xDC, y, e.text);
    else textPropCentered(y, e.text);
    if (q.count <= 1 || n.kind == 4) return;
    if (n.style == 6) textProp(0xDC, y + fontHeight() + 1, n.text);
    else if (n.style != -1) textPropCentered(y + fontHeight() + 1, n.text);
}

void msgDrawQueue() {
    const MissionState& S = ms();
    MessageQueue& q = messages();
    const Message& e = q.entries[size_t(q.head)];
    if (e.style == -1) return;
    if (S.time < e.expiry) msgDraw();
    if (q.count <= 1 || e.style == 3) return;
    const int saved = q.head;
    for (int k = q.count - 1; k > 0; --k) {
        q.head = (q.head + 1) % 8;
        Message& m = q.entries[size_t(q.head)];
        if (m.style != 3) continue;
        if (m.expiry == -1) m.expiry = wrapAdd(S.time, m.duration);
        else if (S.time < m.expiry) msgDraw();
        break;
    }
    q.head = saved;
}

} // namespace loop
} // namespace st::game::mission
