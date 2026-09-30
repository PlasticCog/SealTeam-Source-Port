// Mission Briefing screen with the automatic briefing (top-down fly-over),
// the Patrol Order and Marching Order clipboards and the loadout editor
// (365e:4C7A..6FE6, docs/re/seg_365e_a.md 9.5).
#include "game/front/front.h"

#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/sound.h"
#include "engine/ticker.h"
#include "game/campaign.h"
#include "game/front/brfcam.h"
#include "game/front/common.h"
#include "game/front/hooks.h"
#include "game/front/panels.h"
#include "game/globals.h"
#include "game/screens.h"
#include "game/title.h"
#include "game/ui.h"
#include "gfx/gfx.h"

#include <cstring>

namespace st::game::front {

namespace {

constexpr u16 kButtonSeg = 0x518e;       // +0 briefing buttons, +0x70 Marching Order hot spots
constexpr u16 kLabelTable = 0x24b0;
constexpr u16 kMarchingOff = 0x70;
constexpr u16 kNextLabel = 0x3d60;       // "Next"
constexpr u16 kExitLabel = 0x3d6a;       // "Exit"
constexpr u16 kLookHint = 0x3d7e;        // "Look at the Patrol and Marching Orders for more detail."
constexpr u16 kMeters = 0x3db6;          // "Meters"
constexpr u16 kTitle = 0x3dbd;           // " Mission Briefing  "
constexpr u16 kPatrolTitle = 0x3dd1;     // " Patrol Order  "
constexpr u16 kMarchingTitle = 0x3ed3;   // " Marching Order  "
constexpr int kNextButton = 5;
constexpr int kMarchingExit = 28;
constexpr int kMusic = 8;

enum Mode { Idle = 0, Running = 1, Patrol = 2, Marching = 3 };  // g_brf_mode

ButtonList g_buttons;    // 518E:0000
ButtonList g_marching;   // 518E:0070
SpriteSet g_camp("camp");
int g_mode = Idle;
int g_step = 0;          // g_brf_step
s32 g_stepTime = 0;      // g_brf_step_time
bool g_done = false;
int g_editField = -1;    // g_edit_field: byte offset into the loadout table (5178), -1 = none
int g_marchingKey = 0;   // g_marching_key
std::string g_area;      // g_area_name

// ---------------------------------------------------------------- camera

// Briefing camera targets (far 53BA:2230..226B).
Vec3 g_targets[5];
enum Target { Obj1 = 0, Obj2, Obj3, Insertion, Extraction };
Vec3 g_leaderPos{};      // the SEAL leader's position before it is parked

hooks::View g_view;      // viewport/camera 0xD86E: the 128 x 112 briefing window

// ---------------------------------------------------------------- briefing text

void say(const std::string& text, int duration) { msg::add(text, 1, duration, 0, 0); }

std::string field(const char* p, size_t n) { return std::string(p, strnlen(p, n)); }

// brf_step (365e:4E11): one sentence pair and one camera move per step.
void step() {
    const MciHeader& m = campaign::mci();
    auto sentence = [](int i) { return tableString(strtab::kBriefing, i); };
    auto verb = [](ObjectiveKind k) { return tableString(strtab::kObjectiveVerbs, int(k)); };
    auto teamName = [](int type) { return tableString(strtab::kGroupTypes, type); };
    switch (g_step) {
    case 0:
        say(sentence(0), 0x500);
        brfcam::push(&g_targets[Insertion], 7200, 0x500, 0, 1);
        say(exe().dgString(0x3d6f), 0);
        break;
    case 1:
        say(sentence(1), 0x500);
        brfcam::push(&g_targets[Insertion], 3000, 0x500, 0, 1);
        say(g_area + exe().dgString(0x3d70), 0);
        break;
    case 2:
        say(sentence(2) + verb(m.objective[0].kind), 0x500);
        brfcam::push(&g_targets[Obj1], 1800, 0x500, 0, 1);
        say(field(m.objective[0].description, 40), 0);
        if (m.objective[1].kind == ObjectiveKind(0)) g_step += 2;
        break;
    case 3:
        if (m.objective[1].kind == ObjectiveKind(0)) break;
        say(sentence(3) + verb(m.objective[1].kind), 0x500);
        brfcam::push(&g_targets[Obj2], 0x834, 0x500, 0, 1);
        say(field(m.objective[1].description, 40), 0);
        if (m.objective[2].kind == ObjectiveKind(0)) ++g_step;
        break;
    case 4:
        if (m.objective[2].kind == ObjectiveKind(0)) break;
        say(sentence(4) + verb(m.objective[2].kind), 0x500);
        brfcam::push(&g_targets[Obj3], 0x9f6, 0x500, 0, 1);
        say(field(m.objective[2].description, 40), 0);
        break;
    case 5:
        say(sentence(5) + teamName(insertionTeamType()) + exe().dgString(0x3d72), 0x500);
        brfcam::push(&g_targets[Insertion], 0xc4e, 0x500, 0, 1);
        say(exe().dgString(0x3d74), 0);
        break;
    case 6:
        say(sentence(6) + field(m.objective[0].route, 40), 0x500);
        brfcam::push(&g_targets[Obj1], 0x79e, 0x500, 0, 1);
        say(exe().dgString(0x3d75), 0);
        if (m.objective[1].kind == ObjectiveKind(0)) ++g_step;
        break;
    case 7:
        if (m.objective[1].kind == ObjectiveKind(0)) break;
        say(sentence(7) + field(m.objective[1].route, 40), 0x500);
        brfcam::push(&g_targets[Obj2], 0x708, 0x500, 0, 1);
        say(exe().dgString(0x3d76), 0);
        break;
    case 8:
        say(exe().dgString(0x3d77) + teamName(extractionTeamType()), 0x500);  // "A <team>"
        brfcam::push(&g_targets[Extraction], 0x79e, 0x500, 0, 1);
        say(sentence(8), 0);
        break;
    case 9:
        say(sentence(9) + teamName(fireSupportTeamType()) + exe().dgString(0x3d7a), 0x500);
        brfcam::push(&g_targets[Insertion], 0x1194, 0x500, 0, 1);
        say(exe().dgString(0x3d7c), 0);
        break;
    case 10:
        say(sentence(10) + tableString(strtab::kEnemyStrength, std::min<int>(m.mtm_count, 8)), 0x500);
        brfcam::push(&g_targets[Insertion], 7200, 0x500, 0, 1);
        say(exe().dgString(0x3d7d), 0);
        break;
    default:
        break;
    }
    if (g_step < 10) {
        ++g_step;
        return;
    }
    if (g_step == 10) ++g_step;
    if (msg::count() <= 1 && brfcam::count() <= 2) {
        g_step = 0;
        g_mode = Idle;
        ui().redrawFrames = 2;
    }
}

void autoStep() {
    if (now() - g_stepTime >= 0x600) {
        g_stepTime = now();
        if (g_stepTime != 0) step();
    }
}

// brf_start (365e:5485).
void start() {
    mouseRecenter();
    ui().redrawFrames = 2;
    g_stepTime = now() - 0x600;
    g_step = 0;
    g_mode = Running;
    msg::clear();
    brfcam::clear();
}

// ---------------------------------------------------------------- screen modes

void setNextLabel(u16 label, int key) {
    g_buttons[kNextButton].label = exe().dgString(label);
    g_buttons[kNextButton].key = u16(key);
}

// brf_enter_main (365e:4C7A).
void enterMain() {
    screenTransition(PalMission, false);
    picLoad(PicBriefing);
    ui().focus = kNextButton;
    uiCursorToButton(g_buttons[kNextButton]);
    setNextLabel(kNextLabel, 'n');
}

// brf_enter_clipboard (365e:4CEA).
void enterClipboard() {
    cursorSetWait(true);
    screenTransition(PalOrders, false);
    picLoad(PicOrders);
    cursorReset();
    ui().focus = kNextButton;
    uiCursorToButton(g_buttons[kNextButton]);
    setNextLabel(kExitLabel, 'e');
}

// brf_reset_view (365e:4D4B).
void resetView() {
    enterMain();
    g_mode = Idle;
    // (The original also resets team 0's map zoom height to 0x3E800 here.)
    ui().focus = kNextButton;
    uiCursorToButton(g_buttons[kNextButton]);
    cursorSetWait(false);
}

void backToMain() {
    g_mode = Idle;
    enterMain();
    g_stepTime = now();
    ui().redrawFrames = 4;
}

// ---------------------------------------------------------------- clipboards

void textShadow(int x, int y, const std::string& s, u8 color) { drawTextShadow(x, y, s, 0x0f, color); }

// brf_draw_patrol_order (365e:583C).
void drawPatrolOrder() {
    const CampaignHeader& h = campaign::header();
    const MciHeader& m = campaign::mci();
    const FlowEntry& f = campaign::flow(h.year, h.mission);
    uiDrawTitleTab(exe().dgString(kPatrolTitle), 0x78, 4);
    fontSelect(FontId::Clipboard);
    drawTextEmboss(0x24, 0x14, g_area, 0x1c, 0x0f, 0x12);
    drawTextEmboss(0xa2, 0x22, std::to_string(h.mission + 1), 0x1c, 0x0f, 0x12);
    std::string month = tableString(strtab::kMonths, f.month);
    if (month.size() > 3) month.resize(3);
    drawTextEmboss(0xb2, 0x22, month, 0x1c, 0x0f, 0x12);
    drawTextEmboss(0xce, 0x22, std::to_string(1966 + h.year), 0x1c, 0x0f, 0x12);
    drawTextEmboss(0x24, 0x22, exe().dgString(0x3de1), 0x1c, 0x0f, 0x12);  // "Time:      "
    drawTextEmboss(0x62, 0x22, padNumber(tod::hour(), 2) + exe().dgString(0x3ded), 0x1c, 0x0f, 0x12);
    drawTextEmboss(0x72, 0x22, padNumber(tod::minute(), 2), 0x1c, 0x0f, 0x12);
    fontSelect(FontId::Clipboard);
    auto row = [](int y, u16 label, const std::string& value, int vx = 0xa2) {
        textShadow(0x24, y, exe().dgString(label), 0x1c);
        textShadow(vx, y, value, 0x1c);
    };
    auto objName = [](ObjectiveKind k) { return tableString(strtab::kObjectiveNames, int(k)); };
    auto teamName = [](int type) { return tableString(strtab::kGroupTypes, type); };
    int y = 0x30;
    row(y, 0x3def, objName(m.objective[0].kind));
    if (m.objective[1].kind != ObjectiveKind(0)) {
        y = 0x3e;
        row(y, 0x3e00, objName(m.objective[1].kind));
    }
    if (m.objective[2].kind != ObjectiveKind(0)) {
        y += 0x0e;
        row(y, 0x3e12, objName(m.objective[2].kind));
    }
    y += 0x0e;
    row(y, 0x3e23, teamName(insertionTeamType()));
    y += 0x0e;
    row(y, 0x3e33, teamName(extractionTeamType()));
    y += 0x0e;
    row(y, 0x3e44, teamName(fireSupportTeamType()));
    if (u16(m.break_contact) != 0 && breakContactTeamType() != -1) {
        y += 0x0e;
        row(y, 0x3e57, teamName(breakContactTeamType()));
    }
    const int region = areaRegion(m.world);
    y += 0x0e;
    row(y, 0x3e6b, tableString(strtab::kWeather, region));
    y += 0x0e;
    row(y, 0x3e74, exe().dgString(0x3e7a));  // "Tide:" "N/A"
    y += 0x0e;
    row(y, 0x3e7e, tableString(strtab::kTerrain, region));
    y += 0x0e;
    row(y, 0x3e87, tableString(strtab::kEnemyStrength, std::min<int>(m.mtm_count, 8)), 0x9c);
}

// brf_draw_weapon_slot (365e:5DDE): short name in black, then long name and
// total rounds in grey (red while the slot or its reloads are edited).
void drawWeaponSlot(int k, int i, int x, int y) {
    const LoadoutRecord& r = campaign::loadout()[k];
    const int w = r.weapons[i];
    std::string text, rounds;
    u8 colour = 0x00;
    if (w == -1) {
        text = exe().dgString(0x3ea6);  // "Empty Weapon Slot"
        rounds = exe().dgString(0x3eb8);
    } else {
        text4x6(y, x, exe().dgStringPtr(u16(0x48fe + w * 0x22)), 0x00);
        colour = 0x08;
        text = exe().dgString(0x3e9a) + exe().dgString(0x3ea0) + exe().dgStringPtr(u16(0x4900 + w * 0x22));
        const int total = exe().dgShort(u16(0x490c + w * 0x22)) * s8(r.reloads[i]);
        rounds = exe().dgString(0x3ea2) + std::to_string(u16(total)) + exe().dgString(0x3ea4);
    }
    const int wOff = k * 12 + 2 + i, rOff = k * 12 + 6 + i;
    if (g_editField == wOff || g_editField == rOff) colour = 0x04;
    text4x6(y, x, text + exe().dgString(0x3eb9) + rounds, colour);
}

// brf_draw_tool_slot (365e:5FC1).
void drawToolSlot(int k, int i, int x, int y) {
    const int t = campaign::loadout()[k].tools[i];
    std::string text;
    u8 colour = 0x00;
    if (t == -1) {
        text = exe().dgString(0x3ec3);  // "Empty Tool Slot"
    } else {
        text4x6(y, x, campaign::tool(t).short_name, 0x00);
        colour = 0x08;
        text = exe().dgString(0x3ebb) + exe().dgString(0x3ec1) + campaign::tool(t).long_name;
    }
    if (g_editField == k * 12 + 10 + i) colour = 0x04;
    text4x6(y, x, text, colour);
}

// unit_load_level (19ac:63B9) on the SE stats image.
int loadLevel(const SeRecord& se) {
    const int v = s16(se.load) / 10;
    const int l = (se.size >> 1) + se.strength;
    if (l < v) return 3;
    if ((l >> 1) < v) return 2;
    if ((l >> 2) < v) return 1;
    return 0;
}

// brf_draw_marching_order (365e:60D2).
void drawMarchingOrder() {
    uiDrawTitleTab(exe().dgString(kMarchingTitle), 0x70, 4);
    fontSelect(FontId::Clipboard);
    const CampaignHeader& h = campaign::header();
    const bool practice = g().gameMode == GameMode::Practice;
    for (int k = 0; k < campaign::kTeamSize; ++k) {
        const int y = 0x1f + 44 * k;
        const LoadoutRecord& r = campaign::loadout()[k];
        RosterEntry* e = campaign::rosterFind(r.se_id);
        static SeRecord kNoSe{};
        static RosterEntry kNoEntry{};
        RosterEntry& re = e ? *e : kNoEntry;
        SeRecord& se = re.se ? *re.se : kNoSe;
        std::string line = re.rank >= 8 ? tableString(strtab::kRatingAbbrev, 0)
                                        : tableString(strtab::kRatingAbbrev, se.rating);
        if (se.rating == 0 && re.rank <= 4) line = tableString(strtab::kRankAbbrev, 0);  // "SM"
        if (re.rank >= 2) line += tableString(strtab::kRankAbbrev, re.rank);
        line += exe().dgString(0x3ee5) + field(se.first_name, sizeof se.first_name);
        if (!(k == 0 && !practice && h.nickname[0] == 0)) {
            const std::string nick = (k == 0 && !practice) ? field(h.nickname, sizeof h.nickname)
                                                           : field(se.nickname, sizeof se.nickname);
            line += exe().dgString(0x3ee8) + nick + exe().dgString(0x3eeb);
        }
        line += exe().dgString(0x3eed) + field(se.last_name, sizeof se.last_name);
        textShadow(0x28, y - 13, line, g_editField == k * 12 ? 0x04 : 0x1c);
        text4x6(y, 0x28, tableString(strtab::kTeamRoles, k), 0x00);
        int idx = (campaign::rosterSkillRating(re) - 50) / 10;
        if (idx > 3) idx = 3;
        text4x6(y, 0x74, exe().dgString(0x3eef) + tableString(strtab::kStrRating, idx), 0x08);
        se.load = 0;
        campaign::loadoutComputeWeight(k);
        std::string load = exe().dgString(0x3ef4) + std::to_string(s16(se.load) / 10) + exe().dgString(0x3efa) +
                           exe().dgString(0x3eff);
        if (s16(se.load) > 0) load += tableString(strtab::kLoadClasses, loadLevel(se));
        text4x6(y, 0xa4, load, 0x00);
        drawWeaponSlot(k, 0, 0x28, y + 8);
        drawWeaponSlot(k, 1, 0xa4, y + 8);
        drawWeaponSlot(k, 2, 0x28, y + 16);
        drawWeaponSlot(k, 3, 0xa4, y + 16);
        drawToolSlot(k, 0, 0x28, y + 24);
        drawToolSlot(k, 1, 0xa4, y + 24);
    }
}

// ---------------------------------------------------------------- render

// brf_draw_overlay (365e:57BD).
void drawOverlay() {
    if (g_mode == Running) {
        const int metres = s16(u16(u32(g_view.pos.y) >> 8)) / 3;
        text4x6(0x84, 0x74, std::to_string(u16(metres)), 0x0f);
        text4x6(0x84, 0x8c, exe().dgString(kMeters), 0x0f);
    }
    fontSelect(FontId::Title);
    uiDrawTitleTab(exe().dgString(kTitle), 0x78, 4);
    msg::draw();
    brfcam::update();
}

// brf_render (365e:6E0C). The briefing runs in the map view mode, so the
// text bar and the overlay are redrawn every frame of modes 0 and 1.
void render() {
    Gfx& gx = gfx();
    UiState& u = ui();
    if (u.redrawFrames != 0) {
        --u.redrawFrames;
        picBlitToScreen();
        if (g_mode == Idle || g_mode == Running) {
            gx.fillRect(0, 176, 320, 24, u16(0xff00));
            uiDrawTextPanel(nullptr, 0x4d, 0x1c, 0x7e, 0x70);
            if (g_mode == Running) hooks::drawBriefingView(g_view);
            else gx.fillRect(g_view.x, g_view.y, g_view.w, g_view.h, u16(0xff12));  // view_clear_color12
            drawSprite(g_camp.frame(0, areaCampFrame(campaign::mci().world)), 0, 0);
        }
        if (g_mode == Patrol) drawPatrolOrder();
        if (g_mode == Marching) drawMarchingOrder();
        uiDrawButtons(g_buttons);
    } else {
        engine::ticker().frameLimitWait();
        cursorErase();
    }
    if (g_mode == Idle || g_mode == Running) {
        if (g_mode == Running) {
            gx.setClip(g_view.x, g_view.y, g_view.w, g_view.h);
            hooks::drawBriefingView(g_view);
            gx.clipFull();
        }
        gx.fillRect(0, 176, 320, 24, u16(0xff00));
        gx.clipFull();
        drawOverlay();
    }
    // No cursor while the briefing runs.
    if (g_mode != Running) {
        engine::ticker().frameLimitWait();
        cursorDraw();
    }
}

// ---------------------------------------------------------------- input

// roster_cycle_member (365e:68D7): next / previous free SEAL of the pool.
int cycleMember(int cur, int pointMan, bool next) {
    const int base = pointMan >= 24 ? 24 : 0;
    if (RosterEntry* e = campaign::rosterFind(cur)) e->flags &= u8(~roster_flag::kInTeam);
    int c = cur;
    if (next && c < base + 23) ++c;
    else if (!next && c > base) --c;
    for (;;) {
        if (c < base || c >= base + 24) return cur;
        const RosterEntry* e = campaign::rosterFind(c);
        if (c != pointMan && e && e->flags == 0) return c;
        c += next ? 1 : -1;
    }
}

// loadout_edit_field (365e:6979): the first activation selects a field,
// the next ones apply Enter / Space / '+' / '-'. Returns the selected field.
int editField(int f) {
    if (f <= 0 || f >= 28) return -1;
    const int k = f / 7, sub = f % 7;
    LoadoutRecord& r = campaign::loadout()[k];
    const int key = g_marchingKey;
    const int year = campaign::header().year;
    int sel = -1;
    if (sub == 0) {
        if (k == 0) return -1;
        sel = k * 12;
        if (sel != g_editField) return sel;
        if (key == engine::key::Enter || key == engine::key::Space) {
            const int id = cycleMember(r.se_id, campaign::header().point_man, key == engine::key::Enter);
            r.se_id = s8(id);
            campaign::loadoutBuildMember(k, id);
        }
        return sel;
    }
    if (sub >= 1 && sub <= 4) {
        const int i = sub - 1;
        if (key == engine::key::Enter || key == engine::key::Space) {
            sel = k * 12 + 2 + i;
            if (sel != g_editField) return sel;
            r.weapons[i] = s8(campaign::loadoutNextWeapon(r.weapons[i], year, key == engine::key::Enter, sub));
            r.reloads[i] = campaign::weaponReloadsByte(r.weapons[i]);
            return sel;
        }
        if (key == '+' || key == '-') {
            sel = k * 12 + 6 + i;
            if (sel != g_editField) return sel;
            if (key == '+') {
                const int cap = campaign::weaponReloadsByte(r.weapons[i]);
                const int v = s8(r.reloads[i]) + 1;
                r.reloads[i] = u8(std::min(cap, v));
            } else {
                const int v = s8(r.reloads[i]) - 1;
                r.reloads[i] = u8(v < 0 ? 0 : v);
            }
            return sel;
        }
        return -1;
    }
    // Tools E/F: Enter = next (max 2: radio, medical kit, PHK), Space = previous (min empty).
    const int i = sub - 5;
    sel = k * 12 + 10 + i;
    if (sel != g_editField) return sel;
    if (key == engine::key::Enter) r.tools[i] = s8(std::min(r.tools[i] + 1, 2));
    else if (key == engine::key::Space) r.tools[i] = s8(std::max(r.tools[i] - 1, -1));
    return sel;
}

// brf_marching_input (365e:6BD3).
int marchingInput(int key, int dx, int dy) {
    UiState& u = ui();
    int result = 1;
    if (dx != 0 || dy != 0) uiPointerUpdate(dx, dy, g_marching);
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0) {
        if (u.focus == kMarchingExit) {
            marchingInput(g_marching[kMarchingExit].key, 0, 0);
        } else {
            g_editField = editField(u.focus);
            if (g_editField != -1) u.redrawFrames = 2;
            key = 0;
        }
    }
    switch (key) {
    case engine::key::Enter:
    case engine::key::Space:
    case '+':
    case '-':
        if (g_mode == Marching && u.focus != -1 && g_marching[size_t(u.focus)].key != engine::key::Enter) {
            u.pressed = true;
            g_marchingKey = key;
        }
        break;
    case engine::key::Esc:
    case 'e':
        backToMain();
        break;
    case engine::key::AltX:
        result = 0;
        g_done = true;
        break;
    case engine::key::Up: uiPointerUpdate(0, -5, g_marching); break;
    case engine::key::Left: uiPointerUpdate(-8, 0, g_marching); break;
    case engine::key::Right: uiPointerUpdate(8, 0, g_marching); break;
    case engine::key::Down: uiPointerUpdate(0, 5, g_marching); break;
    default: break;
    }
    return result;
}

// brf_handle_input (365e:6689).
int handleInput(int key, int dx, int dy) {
    UiState& u = ui();
    int result = 2;
    if ((dx != 0 || dy != 0) && g_mode == Idle) uiPointerUpdate(dx, dy, g_buttons);
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0) {
        if (g_mode == Idle) {
            if (u.focus != -1) handleInput(g_buttons[size_t(u.focus)].key, 0, 0);
        } else if (g_mode == Patrol || g_mode == Marching) {
            backToMain();
        }
    }
    if (inputToggleKeys(key) || uiMenuArrowKeys(key, g_buttons)) return 2;
    switch (key) {
    case engine::key::Enter:
        if (g_mode == Idle && (u.focus == -1 || g_buttons[size_t(u.focus)].key == engine::key::Enter)) break;
        u.pressed = true;
        break;
    case engine::key::Esc:
        if (g_mode == Idle) {
            result = 1;
            g_done = true;
        } else if (g_mode == Patrol) {
            backToMain();
        }
        break;
    case engine::key::Space:
        if (g_mode == Running) {
            step();
            g_stepTime = now();
            msg::skip();
            brfcam::skip();
        }
        break;
    case 'b':
        if (g_mode == Idle) start();
        break;
    case 'e':
        if (g_mode == Patrol) backToMain();
        break;
    case 'm':
        if (g_mode == Idle) {
            g_mode = Marching;
            enterClipboard();
            u.focus = kMarchingExit;
            uiCursorToButton(g_marching[kMarchingExit]);
            u.redrawFrames = 2;
        }
        break;
    case 'n':
        if (g_mode == Idle) g_done = true;
        break;
    case 'p':
        if (g_mode == Idle) {
            g_mode = Patrol;
            enterClipboard();
            u.redrawFrames = 2;
            uiPointerUpdate(0, 0, g_buttons);
        }
        break;
    case engine::key::AltX:
        result = 0;
        g_done = true;
        break;
    case engine::key::F10:
        if (g_mode == Idle) {
            difficultyScreen();
            resetView();
            u.redrawFrames = 2;
        }
        break;
    default:
        break;
    }
    return result;
}

} // namespace

int briefingScreen() {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    auto& snd = engine::sound();
    if (g_buttons.empty()) {
        g_buttons = loadButtonList(kButtonSeg, kLabelTable);
        g_marching = loadButtonList(kButtonSeg, 0, kMarchingOff);
    }
    campaign::missionLoad();
    const MciHeader& m = campaign::mci();
    clock.resetClock();
    hooks::briefingWorldEnter();
    g_area = areaName(m.world);
    tod::set(10, 20);
    msg::reset();
    brfcam::attach(&g_view);
    brfcam::reset(6000);
    g_targets[Obj1] = m.objective[0].pos;
    g_targets[Obj2] = m.objective[1].pos;
    g_targets[Obj3] = m.objective[2].pos;
    g_targets[Insertion] = m.insertion;
    g_targets[Extraction] = m.extraction;
    screenTransition(PalMission, true);
    // brf_init_viewports: the top-down briefing window (view 0xD86E).
    g_view = hooks::View{};
    g_view.pos = Vec3{0, 1000 << 8, 0};
    g_view.pitch = s16(-90 * 8);
    g_view.x = 0x4c;
    g_view.y = 0x1c;
    g_view.w = 0x80;
    g_view.h = 0x70;
    // brf_init_scene
    resetView();
    g_camp.free();
    // The SEAL leader starts at the insertion point; its position is the
    // first camera target (the unit itself is parked outside the map).
    g_leaderPos = Vec3{(m.insertion.x >> 8) << 8, 0, (m.insertion.z >> 8) << 8};
    brfcam::push(&g_leaderPos, 6000, 0x400, 0, 1);
    msg::add(exe().dgString(kLookHint), 1, 0x400, 0, 0);
    int result = 2;
    ui().redrawFrames = 4;
    g_step = 0;
    g_editField = -1;
    snd.loadMusic(kMusic, 0);
    if (g().gameMode == GameMode::Demo) {
        g_done = true;
    } else {
        g_done = false;
        in.resetRepeatTimers();
        in.flushKeyboard();
        in.setMode(engine::InputMode::Menu);
    }
    g_stepTime = now() - 0x1800;
    screenTransition(PalMission, false);
    while (!g_done) {
        snd.startOnce(0);
        engine::paletteFade().request(0, clock.frameDt());
        render();
        present();
        clock.updateGameTime();
        tod::update();
        msg::tick();
        brfcam::tick();
        if (g_mode == Running) {
            autoStep();
        } else if (g_mode == Idle && now() - g_stepTime >= 0x1e00) {
            g_stepTime = now();
            if (g_stepTime != 0) start();
        }
        const int key = getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        result = g_mode == Marching ? marchingInput(key, dx, dy) : handleInput(key, dx, dy);
    }
    if (g().gameMode != GameMode::Demo) {
        cursorSetWait(true);
        render();
        present();
    }
    snd.stop();
    snd.unload();
    // brf_leave: the point man's A/B weapons are remembered for the campaign.
    campaign::header().pm_weapon_a = campaign::loadout()[0].weapons[0];
    campaign::header().pm_weapon_b = campaign::loadout()[0].weapons[1];
    g_camp.free();
    hooks::briefingWorldLeave();
    campaign::missionFreeMtm();
    msg::clear();
    cursorReset();
    return result;
}

} // namespace st::game::front
