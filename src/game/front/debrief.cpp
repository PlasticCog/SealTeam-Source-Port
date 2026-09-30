// Mission Debriefing with the OIC's talk over the map, the Post-Mission
// Report and the Historic Report clipboards, then the campaign recording and
// the speeches (365e:AFDC..B8CE, B8CE..CEA8, docs/re/seg_365e_b.md 10).
// Also "One Moment Please ..." (1000:2D83).
#include "game/front/front.h"

#include "data/exeimage.h"
#include "data/ealib.h"
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

constexpr u16 kButtonSeg = 0x5200;
constexpr u16 kLabelTable = 0x24bc;
constexpr int kNextButton = 5;       // label "Next" / "Exit" (DS:24C6 -> label buffer)
constexpr int kHistoricButton = 3;   // clickable only when a historic report exists
constexpr int kMusic = 0x0d;
constexpr u16 kNext = 0x4208, kExit = 0x4214;
constexpr u16 kClipH = 0x421c;       // "cliph.RLE"
constexpr u16 kSeparator = 0x4227;   // "  "
constexpr u16 kIsDead = 0x422a, kCasualties = 0x4234, kOic = 0x425c;
constexpr u16 kSeparator2 = 0x4265, kInHospital = 0x4268, kCasualties2 = 0x427d;
constexpr u16 kCheckReport = 0x42b1, kBeforeDebriefing = 0x42d5;
constexpr u16 kMeters = 0x42f3, kTitle = 0x42fa;
constexpr u16 kPmrTitle = 0x4310, kHistoricTitle = 0x456d;
constexpr int kTalkLines = 6;

enum State { Idle = 0, Talking = 1, Report = 2, Historic = 3 };  // g_dbrf_state (DS:EF20)

ButtonList g_buttons;
SpriteSet g_camp("camp");
std::vector<u8> g_cliph;             // cliph.RLE (DS:CEE8)
std::vector<std::string> g_text;     // cYmNN.s (DS:ED26)
std::vector<std::string> g_historic; // hYmNN.s (DS:CEE4)
bool g_hasHistoric = false;          // DS:420E
bool g_reportClosed = false;         // DS:420D (set when a report is closed; not read here)
int g_state = Idle;
int g_line = 0;                      // g_dbrf_line (DS:EF28)
s32 g_timer = 0;                     // DS:EF24
bool g_done = false;                 // DS:EF22
s16 g_score = 0;                     // g_dbrf_score (DS:EF1E)
hooks::View g_view;                  // map camera 0xD86E (the debriefing window)
Vec3 g_leaderPos{};                  // the Point Man's position before he is parked

std::string S(u16 off) { return exe().dgString(off); }

std::string u16str(int v) { return std::to_string(unsigned(u16(v))); }

// str_itoa_pad(v, width, ' ').
std::string padSpaces(int v, int width) {
    std::string s = std::to_string(v);
    while (int(s.size()) < width) s.insert(s.begin(), ' ');
    return s;
}

std::string lastName(const RosterEntry& e) {
    return e.se ? std::string(e.se->last_name, strnlen(e.se->last_name, sizeof e.se->last_name)) : std::string();
}

// ---------------------------------------------------------------- enter / leave

// dbrf_enter_main (365e:AFDC).
void enterMain() {
    picLoad(PicDebriefing);
    screenTransition(PalMission, false);
    g_buttons[kNextButton].label = S(kNext);
}

// dbrf_enter_report (365e:B022).
void enterReport() {
    picLoad(PicOrders);
    screenTransition(PalOrders, false);
    cursorReset();
    ui().focus = kNextButton;
    uiCursorToButton(g_buttons[kNextButton]);
    g_buttons[kNextButton].label = S(kExit);
}

// dbrf_init (365e:B064): mission text; the historic report hYmNN.s and its
// clipboard picture when the flow entry has one.
void init() {
    const CampaignHeader& h = campaign::header();
    enterMain();
    g_state = Idle;
    // team 0 +0x2C = 0x0003E800: part of hooks::debriefWorldEnter
    ui().focus = kNextButton;
    uiCursorToButton(g_buttons[kNextButton]);
    campaign::loadMissionText(h.year * 20 + h.mission, g_text);
    if (campaign::flow(h.year, h.mission).flags & 0xff) {
        g_hasHistoric = true;
        g_buttons[kHistoricButton].flags |= btn::Clickable;
        // The name template (DS:1D14 -> "cNmNN") becomes "h<year+1>m<NN>".
        std::string name = tableString(0x1d14, 0);
        name.resize(5, ' ');
        name[0] = 'h';
        name[1] = char('1' + h.year);
        name[3] = char('0' + (h.mission + 1) / 10);
        name[4] = char('0' + (h.mission + 1) % 10);
        campaign::loadTextLines(name, g_historic);
        g_cliph.clear();
        resources().read(S(kClipH), g_cliph);
    } else {
        g_hasHistoric = false;
        g_buttons[kHistoricButton].flags &= u8(~btn::Clickable);
    }
}

// dbrf_free (365e:B191).
void freeAll() {
    g_camp.free();
    if (g_hasHistoric) {
        g_cliph.clear();
        g_historic.clear();
    }
    g_text.clear();
}

// ---------------------------------------------------------------- talk

// dbrf_say_casualties (365e:B1DD). Only the OIC, the Corpsman and Rear
// Security (loadout records 1..3) are listed; the Point Man is not.
void sayCasualties() {
    const LoadoutRecord* lo = campaign::loadout();
    std::string dead, hospital;
    int kia = 0, wia = 0;
    for (int k = 1; k < campaign::kTeamSize; ++k) {
        const RosterEntry* e = campaign::rosterFind(lo[k].se_id);
        if (!e || !(e->casualties & 0x4000)) continue;
        if (++kia > 1) dead += S(kSeparator);
        dead += lastName(*e) + S(kIsDead);
    }
    if (kia != 0) {
        msg::say(S(kOic), S(kCasualties));
        msg::say(S(kOic), dead);
    }
    for (int k = 1; k < campaign::kTeamSize; ++k) {
        const RosterEntry* e = campaign::rosterFind(lo[k].se_id);
        if (!e || !(e->flags & roster_flag::kWounded)) continue;
        if (++wia > 1) hospital += S(kSeparator2);
        hospital += lastName(*e) + S(kInHospital);
    }
    if (wia != 0) {
        if (kia == 0) msg::say(S(kOic), S(kCasualties2));
        msg::say(S(kOic), hospital);
    }
}

// dbrf_next_line (365e:B3F9): mission lines 14..18 (won) or 19..23 (lost),
// then the casualties.
void nextLine() {
    const bool won = campaign::missionWon(g_score);
    if (g_line < kTalkLines) {
        const int i = (won ? 0 : 5) + g_line + 14;
        const std::string text = i < int(g_text.size()) ? g_text[size_t(i)] : std::string();
        if (g_line <= 4 && !text.empty()) {
            msg::say(S(kOic), text);
        } else {
            g_line = 5;
            sayCasualties();
        }
        ++g_line;
    } else {
        if (g_line == kTalkLines) ++g_line;
        if (msg::count() <= 1) {
            g_line = 0;
            g_state = Idle;
            ui().redrawFrames = 2;
        }
    }
}

// dbrf_auto_advance (365e:B495).
void autoAdvance() {
    if (now() - g_timer >= 0x800) {
        g_timer = now();
        if (g_timer != 0) nextLine();
    }
}

// dbrf_start_talk (365e:B4E3).
void startTalk() {
    mouseRecenter();
    ui().redrawFrames = 2;
    g_timer = now() - 0x800;
    g_line = 0;
    g_state = Talking;
}

// ---------------------------------------------------------------- reports

// dbrf_draw_post_mission_report (365e:B8CE).
void drawPostMissionReport() {
    const CampaignHeader& h = campaign::header();
    const MciHeader& m = campaign::mci();
    const FlowEntry& f = campaign::flow(h.year, h.mission);
    const campaign::MissionResult& r = campaign::result();
    const campaign::MissionStats& st = campaign::stats();
    constexpr u8 kColor = 0x1e;
    constexpr int kLeft = 0x24, kRight = 0xc4;
    auto objName = [](const MciObjective& o) { return tableString(strtab::kObjectiveNames, int(o.kind)); };
    auto otherUnit = [](int v) { return tableString(strtab::kOtherUnits, (v & ~1) / 2); };

    uiDrawTitleTab(S(kPmrTitle), 0x5a, 4);
    text4x6(0x18, kLeft, areaName(m.world), kColor);
    // SEAL Team Two starts at SE 24, but the test is "< 25" (365e:B966).
    text4x6(0x1e, kLeft, S(0x4326) + S(h.point_man < 25 ? 0x4331 : 0x4335), kColor);
    text4x6(0x1e, kRight, S(0x4339), kColor);
    std::string month = tableString(strtab::kMonths, f.month);
    if (month.size() > 3) month.resize(3);
    text4x6(0x24, kLeft,
            S(0x4344) + std::to_string(unsigned(h.mission) + 1) + S(0x434b) + month + S(0x434d) +
                std::to_string(unsigned(h.year) + 1966),
            kColor);
    text4x6(0x24, kRight, S(0x434f) + padNumber(m.start_hour, 2) + padNumber(m.start_minute, 2), kColor);
    int y = 0x2a;
    std::string buf = S(0x4357) + otherUnit(int(m.fire_support));
    if (int(m.break_contact) != 0) buf += S(0x4365) + otherUnit(int(m.break_contact));
    text4x6(y, kLeft, buf, kColor);
    text4x6(y, kRight, S(0x4368) + tableString(strtab::kInsertionCraft, (int(m.insertion_method) & ~1) / 2), kColor);
    y = 0x30;
    buf = S(0x4372) + objName(m.objective[0]);
    if (int(m.objective[1].kind) != 0) buf += S(0x437a) + objName(m.objective[1]);
    if (int(m.objective[2].kind) != 0) buf += S(0x437d) + objName(m.objective[2]);
    text4x6(y, kLeft, buf, kColor);
    text4x6(y, kRight, S(0x4380), kColor);
    const int region = areaRegion(m.world);
    y += 6;
    text4x6(y, kLeft, S(0x438c) + tableString(strtab::kTerrain, region), kColor);
    text4x6(y, kRight, S(0x4396) + padNumber(tod::hour(), 2) + padNumber(tod::minute(), 2), kColor);
    y += 6;
    text4x6(y, kLeft, S(0x439e) + tableString(strtab::kWeather, region), kColor);
    text4x6(y, kRight, S(0x43a8) + tableString(strtab::kExtraction, std::max<int>(0, r.extractionType)), kColor);
    y += 6;
    text4x6(y, kLeft, S(0x43b2) + S(0x43b9), kColor);
    y += 6;
    text4x6(y, kLeft, S(0x43bd) + S(0x43d1) + objName(m.objective[0]) + S(0x43de), kColor);
    y += 6;
    buf = S(0x43e9);
    if (!r.success[0]) buf += S(0x43ef);
    buf += S(0x43f4) + S(0x4404) + u16str(r.minutes) + S(0x4418);
    text4x6(y, kLeft, buf, kColor);
    y += 6;
    if (r.success[1]) {
        text4x6(y, kLeft, S(0x4422) + objName(m.objective[1]) + S(0x4432), kColor);
        y += 6;
    }
    if (r.success[2]) {
        text4x6(y, kLeft, S(0x4450) + objName(m.objective[2]) + S(0x445f), kColor);
        y += 6;
    }
    // Results: the line is printed when there are kills or prisoners, or
    // nothing was captured at all; otherwise "Results: n/a" stays in the
    // buffer and prefixes the captured-items line (kept as in the original).
    buf = S(0x447c);
    if (r.enemyKia != 0) buf += u16str(r.enemyKia) + S(0x4486);
    if (r.enemyCaptured != 0) {
        if (r.enemyKia != 0) buf += S(0x4491);
        buf += u16str(r.enemyCaptured) + S(0x4494);
    } else {
        buf += S(r.enemyKia != 0 ? 0x44a5 : 0x44a7);
    }
    if (r.enemyKia != 0 || r.enemyCaptured != 0 || (r.weaponsCaptured == 0 && r.documentsCaptured == 0)) {
        text4x6(y, kLeft, buf, kColor);
        y += 6;
        buf = S(0x44ab);
    }
    if (r.weaponsCaptured != 0) buf += u16str(r.weaponsCaptured) + S(0x44ac);
    if (r.documentsCaptured != 0) {
        if (r.weaponsCaptured != 0) buf += S(0x44c0);
        buf += u16str(r.documentsCaptured) + S(0x44c3);
    } else if (r.weaponsCaptured != 0) {
        buf += S(0x44da);
    }
    if (!buf.empty()) {
        text4x6(y, kLeft, buf, kColor);
        y += 6;
    }
    buf = S(0x44dc);
    if (r.sealKia != 0) buf += u16str(r.sealKia) + S(0x44f2);
    if (r.sealWia != 0) {
        if (r.sealKia != 0) buf += S(0x44ff);
        buf += u16str(r.sealWia) + S(0x4502);
    } else {
        buf += S(r.sealKia != 0 ? 0x4510 : 0x4512);
    }
    text4x6(y, kLeft, buf, kColor);
    y += 6;
    text4x6(y, kLeft,
            S(0x4518) + S(0x4522) + u16str(st.roundsFired) + S(0x4530) + S(0x4533) + u16str(st.roundsHit) + S(0x453f),
            kColor);
    y += 6;
    text4x6(y, kLeft,
            S(0x4541) + u16str(st.grenadesThrown) + S(0x4553) + S(0x4556) + u16str(st.grenadesHit) + S(0x4564),
            kColor);
    fontSelect(FontId::Clipboard);  // memo.fnt (DS:EEFC)
    y += 6;
    drawTextEmboss(0x24, y, S(0x4566), 0x1c, 0x0f, 0x12);
    drawTextEmboss(0x4c, y, padSpaces(g_score, 4), 0x1c, 0x0f, 0x12);
}

// dbrf_draw_historic_report (365e:C952): 28 lines cut to 58 characters.
void drawHistoricReport() {
    if (!g_hasHistoric) return;
    uiDrawTitleTab(S(kHistoricTitle), 0x6e, 4);
    int y = 0x18;
    for (int i = 0; i < 28; ++i, y += 6) {
        std::string line = i < int(g_historic.size()) ? g_historic[size_t(i)] : std::string();
        if (line.size() > 58) line.resize(58);
        text4x6(y, 0x28, line, 0x1e);
    }
}

// ---------------------------------------------------------------- render

// dbrf_draw_overlay (365e:B84F).
void drawOverlay() {
    if (g_state == Talking) {
        const int metres = s16(u16(u32(g_view.pos.y) >> 8)) / 3;
        text4x6(0x84, 0x9c, std::to_string(u16(metres)), 0x0f);
        text4x6(0x84, 0xb4, S(kMeters), 0x0f);
    }
    uiDrawTitleTab(S(kTitle), 0x6c, 4);
    msg::draw();  // propbold
    brfcam::update();
}

// dbrf_render (365e:CCA0). The debriefing runs in the map view mode
// (g_view_mode 1), so the text bar and the overlay are redrawn every frame of
// states 0 and 1.
void render() {
    Gfx& gx = gfx();
    UiState& u = ui();
    if (u.redrawFrames != 0) {
        --u.redrawFrames;
        picBlitToScreen();
        if (g_state == Idle || g_state == Talking) {
            gx.fillRect(0, 0xaf, 320, 0x19, u16(0xff00));
            uiDrawTextPanel(nullptr, 0x75, 0x1c, 0x7e, 0x70);
            const u8* camp = g_camp.frame(0, areaCampFrame(campaign::mci().world) + 4);
            drawSprite(camp, 320 - SpriteSet::width(camp), 0);
            if (g_hasHistoric && !g_cliph.empty()) drawSprite(g_cliph.data(), 0x2a, 0x66);
            if (g_state == Talking) hooks::drawDebriefView(g_view);  // view_clear_map_ground
            else gx.fillRect(g_view.x, g_view.y, g_view.w, g_view.h, u16(0xff12));  // view_clear_color12
        }
        if (g_state == Report) drawPostMissionReport();
        if (g_state == Historic) drawHistoricReport();
        uiDrawButtons(g_buttons);
    } else {
        engine::ticker().frameLimitWait();
        cursorErase();
    }
    if (g_state == Idle || g_state == Talking) {
        if (g_state == Talking) {
            gx.setClip(g_view.x, g_view.y, g_view.w, g_view.h);
            hooks::drawDebriefView(g_view);
            gx.clipFull();
        }
        gx.fillRect(0, 0xaf, 320, 0x19, u16(0xff00));
        gx.clipFull();
        drawOverlay();
    }
    if (g_state != Talking) {
        engine::ticker().frameLimitWait();
        cursorDraw();
    }
}

// ---------------------------------------------------------------- input

void closeReport() {
    g_state = Idle;
    g_reportClosed = true;
    enterMain();
    g_timer = now();
    ui().redrawFrames = 4;
}

// dbrf_handle_input (365e:C9F5).
int handleInput(int key, int dx, int dy) {
    UiState& u = ui();
    int result = 1;
    if ((dx != 0 || dy != 0) && g_state == Idle) uiPointerUpdate(dx, dy, g_buttons);
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0) {
        if (g_state == Idle) {
            if (u.focus != -1) handleInput(g_buttons[size_t(u.focus)].key, 0, 0);
        } else if (g_state == Report || g_state == Historic) {
            closeReport();
        }
    }
    if (inputToggleKeys(key) || uiMenuArrowKeys(key, g_buttons)) return 1;
    switch (key) {
    case engine::key::Enter:
        if (g_state != Idle) u.pressed = true;
        else if (u.focus != -1 && g_buttons[size_t(u.focus)].key != engine::key::Enter) u.pressed = true;
        break;
    case engine::key::Esc:
        if (g_state == Report || g_state == Historic) closeReport();
        else if (g_state == Idle) g_done = true;
        break;
    case engine::key::Space:
        if (g_state == Talking) {
            nextLine();
            g_timer = now();
            msg::skip();
            brfcam::skip();
        }
        break;
    case 'd':
        if (g().gameMode != GameMode::Demo && g_state == Idle) startTalk();
        break;
    case 'e':
        if (g_state == Report || g_state == Historic) closeReport();
        break;
    case 'h':
        if (g_hasHistoric && g_state == Idle) {
            g_state = Historic;
            enterReport();
            u.redrawFrames = 2;
        }
        break;
    case 'p':
        if (g_state == Idle) {
            g_state = Report;
            enterReport();
            u.redrawFrames = 2;
        }
        break;
    case 'n':
        g_done = true;
        break;
    case engine::key::AltX:
        result = 0;
        g_done = true;
        break;
    default:
        break;
    }
    return result;
}

} // namespace

// dbrf_screen (365e:B511); the original is always called with loadWorld = 1.
int debriefScreen(bool loadWorld) {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    auto& snd = engine::sound();
    CampaignHeader& h = campaign::header();
    if (g_buttons.empty()) g_buttons = loadButtonList(kButtonSeg, kLabelTable);
    g_score = std::max<s16>(0, campaign::stats().score);
    g_state = Idle;
    screenTransition(PalMission, true);
    campaign::missionLoad();
    const MciHeader& m = campaign::mci();
    clock.resetClock();
    g_timer = now();
    tod::set(m.start_hour, m.start_minute);
    const int minutes = campaign::result().minutes;
    tod::add(minutes / 60, minutes % 60);
    msg::reset();
    brfcam::attach(&g_view);
    brfcam::reset(6000);
    // dbrf_init_viewports (365e:CBF5): the map camera 0xD86E, the 128 x 112
    // window at (0x74, 0x1C), looking straight down from 1000.
    g_view = hooks::View{};
    g_view.pos = Vec3{0, 1000 << 8, 0};
    g_view.pitch = s16(-90 * 8);
    g_view.x = 0x74;
    g_view.y = 0x1c;
    g_view.w = 0x80;
    g_view.h = 0x70;
    // dbrf_init_scene (365e:CC6E) + mis_load_world; the world part (eye
    // height, view_enter_map, parking the Point Man) is in the hook.
    g_leaderPos = Vec3{};
    if (loadWorld) hooks::debriefWorldEnter(m.world, g_view, g_leaderPos);
    init();  // + dbrf_load_camp_anim: the "camp" set is loaded on first use
    g_timer = now();
    msg::add(S(kCheckReport), 1, 0x400, 0, 0);
    msg::add(S(kBeforeDebriefing), 1, 0, 0, 0);
    brfcam::push(&g_leaderPos, 6000, 0x400, 0, 1);
    int result = 1;
    ui().redrawFrames = 4;
    g_line = 0;
    in.resetRepeatTimers();
    in.flushKeyboard();
    in.setMode(engine::InputMode::Menu);
    g_done = false;
    snd.loadMusic(kMusic, 0);
    int sequence = 0;
    const RosterEntry* pm = campaign::rosterFind(h.point_man);
    // history[missions] of the Point Man (campaign+0x33+12n bit 0x40); the
    // original reads past the 24 entries for longer careers - guarded here.
    if (pm && pm->missions < 24 && (h.history[pm->missions].casualties & 0x4000)) sequence = 2;
    else sequence = campaign::missionWon(g_score) ? 0 : 1;
    screenTransition(PalMission, true);
    while (!g_done) {
        snd.startOnce(sequence);
        engine::paletteFade().request(0, clock.frameDt());
        render();
        present();
        clock.updateGameTime();
        tod::update();
        msg::tick();
        if (g_state == Talking) {
            autoAdvance();
        } else if (g().gameMode != GameMode::Demo && g_state == Idle && now() - g_timer >= 0x4000) {
            g_timer = now();
            if (g_timer != 0) startTalk();
        }
        const int key = getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        result = handleInput(key, dx, dy);
    }
    cursorSetWait(true);
    render();
    present();
    snd.stop();
    snd.unload();
    msg::clear();
    cursorReset();
    freeAll();
    if (loadWorld) hooks::debriefWorldLeave();
    campaign::missionFreeMtm();
    if (int(g().gameMode) <= 2) {
        campaign::recordMission(g_score);
        campaign::loadoutReleaseTeam();
        if (result != 0) {
            campaign::saveAutosave();
            int kind = -1;
            if (sequence == 2) kind = 0;                        // Chaplain: the Point Man was killed
            else if (campaign::rosterCountKia() > 10) kind = 5; // relieved of command
            else if (u8(h.mission) == 0xff) kind = 4;           // campaign over
            else if (h.mission == 0) kind = h.year;             // a new year begins
            if (kind >= 0) result = speechScreen(kind);
        }
    }
    return result;
}

// ui "One Moment Please ..." (1000:2D83).
void showPleaseWait() {
    engine::paletteFade().request(0, engine::ticker().frameDt());
    engine::ticker().wait(0x180);
    uiDrawTextPanel(exe().dgString(0x669).c_str(), 100, 0x5a, 0x68, 0x10);
    present();
}

} // namespace st::game::front
