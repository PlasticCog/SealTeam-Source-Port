// "UDT/SEAL Training School" - new campaign: starting year, point man,
// Biography / Rating pages and nickname (365e:8001..91D8,
// docs/re/seg_365e_b.md 5).
#include "game/front/front.h"

#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/ticker.h"
#include "game/campaign.h"
#include "game/front/common.h"
#include "game/front/panels.h"
#include "game/front/state.h"
#include "game/globals.h"
#include "game/screens.h"
#include "game/title.h"
#include "game/ui.h"
#include "gfx/gfx.h"

#include <cstring>

namespace st::game::front {

namespace {

constexpr u16 kRecruitSeg = 0x51d0;      // +0x18 buttons, +0x78 recruit table u8[16]
constexpr u16 kButtonOff = 0x18;
constexpr u16 kLabelTable = 0x246e;
constexpr u16 kRecruitTable = 0x78;
constexpr u16 kTitle = 0x402c;           // " UDT/SEAL Training School  "
constexpr u16 kRatingLabel = 0x4048;     // "Rating"
constexpr u16 kBiographyLabel = 0x404f;  // "Biography"
constexpr u16 kNicknamePrompt = 0x4059;  // "Enter Nickname:"
constexpr int kStartButton = 3, kYearButton = 2, kPageButton = 4;
constexpr int kPanelX = 0x16, kPanelY = 0x7f;

ButtonList g_buttons;
SpriteSet g_portraits("port");
RosterEntry g_cur{};                 // scratch roster entry 51D0:0000 (g_cur_roster)
std::unique_ptr<SeRecord> g_se;      // g_recruit_se
bool g_showRating = false;           // g_recruit_show_rating (DS:3F7E)
bool g_done = false;

int recruitId(int slot) {
    const u8* p = exe().at(kRecruitSeg, u16(kRecruitTable + slot));
    return slot + (p ? *p : 0);
}

// Loads the SE of the current recruit without refreshing the roster
// skills: the Rating page's STR word keeps using the first recruit shown
// (the original only updates the id and the SE pointer, 365e:9040).
void reloadSe() {
    g_se = campaign::loadSealSe(g_cur.se_id);
    g_cur.se = g_se.get();
    if (g_se) campaign::header().pm_weapon_a = g_se->weapons[0];
}

void drawContents() {
    CampaignHeader& h = campaign::header();
    uiDrawTitleTab(exe().dgString(kTitle), 0x58, 4);
    static const SeRecord kNoSe{};
    const SeRecord& se = g_se ? *g_se : kNoSe;
    if (g_showRating) drawRatingPanel(se, g_cur, kPanelX, kPanelY);
    else drawBioPanel(se, g_cur, g_portraits, recruitSlot(), kPanelX, kPanelY);
    g_buttons[kYearButton].label = std::to_string(1966 + h.year);
    // The page button offers the other page.
    if (!g_showRating) {
        g_buttons[kPageButton].label = exe().dgString(kRatingLabel);
        g_buttons[kPageButton].key = 'r';
    } else {
        g_buttons[kPageButton].label = exe().dgString(kBiographyLabel);
        g_buttons[kPageButton].key = 'b';
    }
}

void render() {
    UiState& u = ui();
    if (u.redrawFrames != 0) {
        --u.redrawFrames;
        picBlitToScreen();
        drawContents();
        uiDrawButtons(g_buttons);
    } else {
        engine::ticker().frameLimitWait();
        cursorErase();
    }
    renderEnd();
}

void changeRecruit(int delta) {
    CampaignHeader& h = campaign::header();
    int i = recruitSlot() % 4 + delta;
    if (i > 3) i = 3;
    if (i < 0) i = 0;
    recruitSlot() = i + h.year * 4;
    g_cur.se_id = u8(recruitId(recruitSlot()));
    reloadSe();
}

int handleInput(int key, int dx, int dy) {
    UiState& u = ui();
    CampaignHeader& h = campaign::header();
    int result = 2;
    if ((dx != 0 || dy != 0) && uiPointerUpdate(dx, dy, g_buttons)) u.redrawFrames = 2;
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0 && u.focus != -1) {
        handleInput(g_buttons[size_t(u.focus)].key, 0, 0);
        u.redrawFrames = 2;
    }
    if (inputToggleKeys(key) || uiMenuArrowKeys(key, g_buttons)) return 2;
    switch (key) {
    case engine::key::Enter:
        if (u.focus != -1 && uiButtonPress(g_buttons[size_t(u.focus)].key)) u.redrawFrames = 2;
        return result;
    case engine::key::Esc:
        result = 1;
        g_done = true;
        return result;
    case '1':
        // Next starting year: first recruit of that year, its voice and background.
        h.mission = 0;
        h.year = u8((h.year + 1) % 4);
        recruitSlot() = h.year * 4;
        g_cur.se_id = u8(recruitId(recruitSlot()));
        g().missionNo = h.year * 20;
        cursorSetWait(true);
        render();
        present();
        picLoad(PicNew + h.year);
        voice::stop();
        g_se.reset();
        voice::load(h.year);
        voice::play();
        u.redrawFrames = 2;
        cursorReset();
        reloadSe();
        return result;
    case 'b':
    case 'r':
        g_showRating = !g_showRating;
        break;
    case 'e':
        // Undocumented: cycle the best weapon down (only while > 0) / up.
        if (h.pm_weapon_a > 0) h.pm_weapon_a = s8(campaign::loadoutNextWeapon(h.pm_weapon_a, h.year, false, 1));
        break;
    case 'w':
        h.pm_weapon_a = s8(campaign::loadoutNextWeapon(h.pm_weapon_a, h.year, true, 1));
        break;
    case 'n':
        changeRecruit(+1);
        break;
    case 'p':
        changeRecruit(-1);
        break;
    case 's': {
        engine::input().flushKeyboard();
        std::string buf;
        uiDialogPrompt(exe().dgString(kNicknamePrompt), buf, 0x40, 0x94, 0x0e, false, false);
        if (!buf.empty() && buf[0] == 1) break;  // cancelled
        std::memset(h.nickname, 0, sizeof h.nickname);
        std::memcpy(h.nickname, buf.data(), std::min(buf.size(), sizeof h.nickname - 1));
        g_done = true;
        break;
    }
    case engine::key::AltX:
        result = 0;
        g_done = true;
        return result;
    default:
        return result;
    }
    u.redrawFrames = 2;
    return result;
}

} // namespace

int recruitScreen() {
    auto& clock = engine::ticker();
    auto& in = engine::input();
    CampaignHeader& h = campaign::header();
    if (g_buttons.empty()) g_buttons = loadButtonList(kRecruitSeg, kLabelTable, kButtonOff);
    recruitSlot() = recruitSlot() % 4 + h.year * 4;
    screenTransition(PalNew, true);
    // recruit_enter: background, portraits, voices, focus Start Campaign.
    picLoad(PicNew + h.year);
    g_portraits.free();
    voice::load(h.year);
    ui().focus = kStartButton;
    uiCursorToButton(g_buttons[kStartButton]);
    g_showRating = false;
    campaign::rosterEntryInit(g_cur, g_se, recruitId(recruitSlot()));
    if (g_se) {
        h.pm_weapon_a = g_se->weapons[0];
        h.pm_weapon_b = g_se->weapons[1];
    }
    h.nickname[0] = 0;
    ui().redrawFrames = 2;
    g_done = false;
    in.resetRepeatTimers();
    in.flushKeyboard();
    in.setMode(engine::InputMode::Menu);
    voice::play();
    int result = 2;
    while (!g_done) {
        engine::paletteFade().request(0, clock.frameDt());
        render();
        present();
        clock.updateGameTime();
        voice::tick(clock.frameDt());
        const int key = getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        result = handleInput(key, dx, dy);
    }
    cursorSetWait(true);
    render();
    present();
    // recruit_leave
    cursorReset();
    voice::free();
    g_portraits.free();
    const int id = g_cur.se_id;
    g_se.reset();
    g_cur.se = nullptr;
    // Whatever the result, the new campaign is written to c8.cmp and read back.
    h.point_man = s16(id);
    campaign::rosterNew(id);
    campaign::save(8);
    campaign::rosterFree();
    campaign::load(8);
    return result;
}

} // namespace st::game::front
