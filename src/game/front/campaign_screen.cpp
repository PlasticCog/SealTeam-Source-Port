// "SEAL Campaign" screen (continue campaign): save and load dog tags, the
// Campaign / Biography / Rating info window and the award, promotion and
// new-member messages (365e:9540..A862, docs/re/seg_365e_b.md 7).
#include "game/front/front.h"

#include "data/exeimage.h"
#include "engine/input_layer.h"
#include "engine/palette_fade.h"
#include "engine/sound.h"
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

#include <algorithm>
#include <cstring>

namespace st::game::front {

int& recruitSlot() {
    static int slot = 0;
    return slot;
}

int portraitSlotOf(int seId) {
    const u8* p = exe().at(0x51d9, u16(0x140 + seId));
    return seId - (p ? *p : 0);
}

namespace {

constexpr u16 kButtonSeg = 0x51d9;
constexpr u16 kLabelTable = 0x2478;
constexpr u16 kTitle = 0x41bb;        // " SEAL Campaign  "
constexpr u16 kNamePrompt = 0x41cc;   // "Enter Campaign Name:"
constexpr u16 kCampaignOver1 = 0x40a8, kCampaignOver2 = 0x40c1;
constexpr u16 kWelcome = 0x40f3, kWelcomeEnd = 0x4111, kEmpty1 = 0x4113;
constexpr u16 kAwarded = 0x4116, kAwardedEnd = 0x412d, kEmpty2 = 0x412f, kEmpty3 = 0x4130;
constexpr u16 kPromotion = 0x4131, kPromotionEnd = 0x4151;
constexpr int kNextButton = 18;
constexpr int kPanelX = 0x16, kPanelY = 0x56;
constexpr int kMusic = 0x0e;

ButtonList g_buttons;
SpriteSet g_bos("bos"), g_bin("bin"), g_bis("bis"), g_bil("bil"), g_medals("mdl"), g_portraits("prt2");
RosterEntry* g_cur = nullptr;  // g_cur_roster: the point man
int g_page = 0;                // g_cmpscr_info_page
int g_redrawHold = 0;          // DS:40A6, never set in the program
bool g_done = false;

void selectPointMan() {
    const CampaignHeader& h = campaign::header();
    g_cur = campaign::rosterFind(h.point_man);
    recruitSlot() = g_cur ? portraitSlotOf(g_cur->se_id) : 0;
}

// cmpscr_msg_awards (365e:97DB): medals and promotion of the last mission.
void msgAwards() {
    if (!g_cur || g_cur->missions == 0) return;
    const CampaignHistory& e = campaign::header().history[std::min(g_cur->missions, u8(24)) - 1];
    unsigned bits = e.awards;
    if (bits != 0) {
        for (int i = 0; i < 16; ++i, bits >>= 1) {
            if (!(bits & 1)) continue;
            msg::add(exe().dgString(kAwarded) + tableString(strtab::kMedals, i) + exe().dgString(kAwardedEnd), 1,
                     0x400, 0, 0);
            if (i < 8) msg::add(tableString(strtab::kMedals, i), 4, 0, i, 0);  // medal picture
            msg::add(exe().dgString(kEmpty2), 1, 0x100, 0, 0);
            msg::add(exe().dgString(kEmpty3), 1, 0, 0, 0);
        }
    }
    if (e.rank >= 2)
        msg::add(exe().dgString(kPromotion) + tableString(strtab::kRanks, e.rank) + exe().dgString(kPromotionEnd), 1,
                 0x400, 0, 0);
}

void msgCampaignOver() {
    if (campaign::header().mission != 0xff) return;
    msg::add(exe().dgString(kCampaignOver1), 1, 0x800, 0, 0);
    msg::add(exe().dgString(kCampaignOver2), 1, 0, 0, 0);
}

// cmpscr_msg_new_members (365e:96FB): one reserve recruit per SEAL killed.
// The activated index is looked up as an SE id without the pool base, so
// nothing is announced in the second pool (original behaviour).
void msgNewMembers() {
    for (int n = campaign::result().sealKia; n > 0; --n) {
        const int i = campaign::rosterActivateRecruit();
        if (i == -1) continue;
        const RosterEntry* e = campaign::rosterFind(i);
        if (!e || !e->se) continue;
        const std::string last(e->se->last_name, strnlen(e->se->last_name, sizeof e->se->last_name));
        msg::add(exe().dgString(kWelcome) + last + exe().dgString(kWelcomeEnd), 1, 0x300, 0, 0);
        msg::add(exe().dgString(kEmpty1), 1, 0, 0, 0);
    }
}

// cmpscr_draw_dogtags (365e:A36B).
void drawDogtags(bool drawUsed) {
    const UiState& u = ui();
    for (int i = 1; i < int(g_buttons.size()); ++i) {
        const Button& b = g_buttons[size_t(i)];
        if (b.key != 'v' && b.key != 'l') break;
        const int slot = (i - 1) % 8;
        const int x = 0x19 + 0x23 * slot;
        const u8* img = nullptr;
        const bool active = (u.focus == i && (u.pressed || u.releaseCount != 0)) || (b.flags & btn::Highlighted);
        if (active) {
            if (i < 9) img = campaign::slotUsed(slot) ? g_bis.frame(0, slot) : g_bin.frame(0, slot);
            else if (campaign::slotUsed(slot)) img = g_bil.frame(0, slot);
        } else if (drawUsed && i < 9 && campaign::slotUsed(slot)) {
            img = g_bos.frame(0, slot);
        }
        drawSprite(img, x, 8);
    }
}

void drawContents() {
    uiDrawTitleTab(exe().dgString(kTitle), 0x78, 4);
    static const SeRecord kNoSe{};
    static const RosterEntry kNoEntry{};
    const RosterEntry& cur = g_cur ? *g_cur : kNoEntry;
    const SeRecord& se = cur.se ? *cur.se : kNoSe;
    if (g_page == 0) drawCampaignPanel(cur, recruitSlot(), g_medals, kPanelX, kPanelY);
    else if (g_page == 1) drawBioPanel(se, cur, g_portraits, recruitSlot(), kPanelX, kPanelY);
    else if (g_page == 2) drawRatingPanel(se, cur, kPanelX, kPanelY);
}

// cmpscr_render (365e:A7BB).
void render() {
    UiState& u = ui();
    Gfx& gx = gfx();
    if (u.redrawFrames == 0 && g_redrawHold == 0) {
        engine::ticker().frameLimitWait();
        cursorErase();
    } else {
        picBlitToScreen();
        gx.fillRect(0, 176, 320, 24, u16(0xff00));
        drawDogtags(g_redrawHold < 1);
        drawContents();
        uiDrawButtons(g_buttons);
        --u.redrawFrames;
        if (g_redrawHold != 0) --g_redrawHold;
    }
    gx.clipFull();
    gx.fillRect(0, 176, 320, 24, u16(0xff00));
    msg::draw();
    engine::ticker().frameLimitWait();
    cursorDraw();
}

int handleInput(int key, int dx, int dy) {
    UiState& u = ui();
    CampaignHeader& h = campaign::header();
    int result = 2;
    if ((dx != 0 || dy != 0) && uiPointerUpdate(dx, dy, g_buttons)) u.redrawFrames = 2;
    uiButtonRelease(key);
    if (u.releaseCount != 0 && --u.releaseCount == 0) {
        result = u.focus != -1 ? handleInput(g_buttons[size_t(u.focus)].key, 0, 0) : result;
        u.redrawFrames = 2;
        return result;
    }
    if (inputToggleKeys(key) || uiMenuArrowKeys(key, g_buttons)) return 2;
    const int focus = u.focus;
    switch (key) {
    case engine::key::Enter:
        if (focus != -1 && uiButtonPress(g_buttons[size_t(focus)].key)) u.redrawFrames = 2;
        return result;
    case engine::key::Esc:
        g_done = true;
        return 1;
    case 'c':
        g_page = 0;
        break;
    case 'l':
        if (focus > 8 && focus <= 16 && campaign::slotUsed(focus - 9) && uiConfirmLoadCampaign(focus - 9)) {
            campaign::rosterFree();
            campaign::load(focus - 9);
            h.slot = s8(focus - 9);
            selectPointMan();
            msgAwards();
            g_page = 0;
        }
        break;
    case 'm':
        g_page = (g_page + 1) % 3;
        break;
    case 'n':
        if (h.mission == 0xff) result = 1;
        g_done = true;
        return result;
    case 'v':
        if (focus > 0 && focus <= 8) {
            const int slot = focus - 1;
            if (!campaign::slotUsed(slot) || uiConfirmReplaceCampaign(slot)) {
                h.slot = s8(slot);
                std::string buf;
                uiDialogPrompt(exe().dgString(kNamePrompt), buf, 0x20, 0x3c, 0x19, false, true);
                if (buf.empty() || buf[0] != 1) {
                    campaign::setSlotName(slot, buf);
                    campaign::save(slot);
                    campaign::setLastSlot(slot);
                }
            }
        }
        break;
    case 0x1200:  // Alt-E: erase the focused save slot
        if (focus > 0 && focus <= 8) campaign::deleteSlot(focus - 1);
        break;
    case engine::key::AltX:
        g_done = true;
        return 0;
    default:
        return result;
    }
    u.redrawFrames = 2;
    return result;
}

} // namespace

int campaignScreen() {
    if (g().gameMode != GameMode::Campaign) return 2;
    auto& clock = engine::ticker();
    auto& in = engine::input();
    auto& snd = engine::sound();
    CampaignHeader& h = campaign::header();
    if (g_buttons.empty()) g_buttons = loadButtonList(kButtonSeg, kLabelTable);
    screenTransition(PalL, true);
    // cmpscr_enter
    picLoad(PicLoad);
    msg::setMedalSprites(&g_medals);
    ui().focus = kNextButton;
    uiCursorToButton(g_buttons[kNextButton]);
    msg::reset();
    g_page = 0;
    ui().redrawFrames = 4;
    g_done = false;
    in.resetRepeatTimers();
    in.flushKeyboard();
    in.setMode(engine::InputMode::Menu);
    snd.loadMusic(kMusic, 0);
    if (campaign::fromMission()) campaign::loadAutosave();
    else campaign::load(h.slot);
    selectPointMan();
    msgAwards();
    if (h.mission == 0xff) msgCampaignOver();
    else if (campaign::fromMission()) msgNewMembers();
    int result = 2;
    while (!g_done) {
        snd.startOnce(g_cur && (g_cur->casualties & hit_bit::kKilled) ? 1 : 0);
        engine::paletteFade().request(0, clock.frameDt());
        render();
        present();
        clock.updateGameTime();
        msg::tick();
        const int key = getKey();
        int dx = 0, dy = 0;
        in.getMotion(dx, dy);
        result = handleInput(key, dx, dy);
    }
    cursorSetWait(true);
    render();
    present();
    campaign::saveAutosave();
    campaign::rosterFree();
    snd.stop();
    snd.unload();
    // cmpscr_leave
    cursorReset();
    msg::clear();
    g_portraits.free();
    g_medals.free();
    g_bil.free();
    g_bis.free();
    g_bin.free();
    g_bos.free();
    msg::setMedalSprites(nullptr);
    campaign::loadAutosave();
    g_cur = nullptr;
    return result;
}

} // namespace st::game::front
