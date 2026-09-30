#include "game/front/panels.h"

#include "data/exeimage.h"
#include "game/campaign.h"
#include "game/front/common.h"
#include "game/screens.h"
#include "game/ui.h"
#include "gfx/gfx.h"

#include <cstring>

namespace st::game::front {

namespace {

// Recruit table 51D9:0140: SE id -> portrait slot offset (slot = id - table[id]).
constexpr u16 kPortraitSlotSeg = 0x51d9;
constexpr u16 kPortraitSlotOff = 0x140;

std::string field(const char* p, size_t n) { return std::string(p, strnlen(p, n)); }
std::string num(unsigned v) { return std::to_string(v); }  // str_utoa

// Colours of font_draw_text_shadow: the first pushed argument is the text
// colour, the second the shadow (365e:9489).
void text(int x, int y, const std::string& s, u8 color, u8 shadow) { drawTextShadow(x, y, s, shadow, color); }

// "SEAL Team 1" with the digit raised to '2' for the second team.
std::string sealTeam(bool two) {
    std::string s = exe().dgString(0x3fac);
    if (two && s.size() > 10) ++s[10];
    return s;
}

bool nameGreyed(const RosterEntry& cur) {
    return (cur.casualties & hit_bit::kKilled) || campaign::header().mission == 0xff;
}

} // namespace

std::string tableString(u16 table, int i) { return exe().dgStringPtr(u16(table + 2 * i)); }

std::string rankLine(int rank, int rating, bool seamanSpaces) {
    std::string s;
    if (seamanSpaces) {
        // ui_draw_campaign_panel (365e:9C20)
        if (rank >= 5) {
            s = tableString(strtab::kRatings, 0);
        } else if (rating == 0) {
            s = tableString(strtab::kRanks, 0) + "  ";
        } else {
            s = tableString(strtab::kRatings, rating) + "  ";
        }
    } else {
        // ui_draw_bio_panel / ui_draw_rating_panel: "Seaman" replaces the
        // (empty) rating without the separating spaces.
        if (rank >= 5) {
            s = tableString(strtab::kRatings, 0);
        } else {
            s = tableString(strtab::kRatings, rating);
            if (rating != 0) s += "  ";
        }
        if (rating == 0 && rank <= 4) s = tableString(strtab::kRanks, 0);
    }
    if (rank >= 2) s += tableString(strtab::kRanks, rank);
    return s;
}

std::string nameLine(const SeRecord& se) {
    const CampaignHeader& h = campaign::header();
    std::string s = field(se.first_name, sizeof se.first_name) + "  ";
    if (h.nickname[0] != 0) s += "'" + field(h.nickname, sizeof h.nickname) + "'  ";
    s += field(se.last_name, sizeof se.last_name);
    return s;
}

// ui_draw_bio_panel (365e:81F1).
void drawBioPanel(const SeRecord& se, const RosterEntry& cur, SpriteSet& portraits, int portraitSlot, int x, int y) {
    const CampaignHeader& h = campaign::header();
    uiDrawTextPanel("", x - 8, y, 0x124, 0x44);
    fontSelect(FontId::Title);
    text(x, y + 2, exe().dgString(0x3f9f), 0x0f, 0x00);  // "Biography"
    fontSelect(FontId::Dialog);
    const std::string rank = rankLine(se.rank, se.rating, false);
    text(x, y + 0x11, rank, 0x00, 0x10);
    const int w = textWidth(rank);
    text(x + w + 8, y + 0x11, sealTeam(se.buds_class & 1), 0x00, 0x0f);
    fontSelect(FontId::Title);
    text(x, y + 0x1b, nameLine(se), nameGreyed(cur) ? 0x1a : 0x00, 0x0f);
    uiDrawTextPanel(nullptr, x + 0xf4, y + 3, 0x24, 0x21);
    drawSprite(portraits.frame(h.year >> 1, portraitSlot % 8), x + 0xf2, y + 2);
    fontSelect(FontId::Dialog);
    text(x, y + 0x25, num(unsigned(se.age) + h.year) + exe().dgString(0x3fc2), 0x00, 0x10);
    text(x + 0xa8, y + 0x25, num(se.height_ft) + exe().dgString(0x3fcd), 0x00, 0x10);
    text(x + 0xbc, y + 0x25, num(se.height_in) + exe().dgString(0x3fd2), 0x00, 0x10);
    text(x + 0xd0, y + 0x25, num(se.weight_lb) + exe().dgString(0x3fd6), 0x00, 0x10);
    text(x, y + 0x2f, field(se.birthplace, sizeof se.birthplace), 0x00, 0x10);
    text(x + 0xa8, y + 0x2f, exe().dgString(0x3fdb) + tableString(strtab::kBudsClasses, se.buds_class), 0x00, 0x10);
    text(x, y + 0x39, exe().dgString(0x3fe8), 0x00, 0x10);  // "Best Weapon:"
    // Weapon names of campaign+0x14A (a signed byte index into the table).
    const int wpn = h.pm_weapon_a;
    const std::string shortName = exe().dgStringPtr(u16(0x48fe + wpn * 0x22));
    const std::string longName = exe().dgStringPtr(u16(0x4900 + wpn * 0x22));
    text(x + 0x3c, y + 0x39, shortName + exe().dgString(0x3ff5) + longName, 0x00, 0x10);
    text(x + 0xa8, y + 0x39, exe().dgString(0x3ff7), 0x00, 0x10);  // "Camouflage:"
    text(x + 0xdc, y + 0x39, tableString(strtab::kCamouflage, se.camouflage), 0x00, 0x10);
}

// ui_draw_rating_panel (365e:889D). The bars show the SE skills; the STR
// word uses the skills of the roster entry g_cur_roster.
void drawRatingPanel(const SeRecord& se, const RosterEntry& cur, int x, int y) {
    uiDrawTextPanel("", x - 8, y, 0x124, 0x44);
    fontSelect(FontId::Title);
    text(x, y + 2, exe().dgString(0x4004), 0x0f, 0x00);  // "Rating"
    fontSelect(FontId::Dialog);
    const std::string rank = rankLine(se.rank, se.rating, false);
    text(x, y + 0x11, rank, 0x00, 0x0f);
    const int w = textWidth(rank);
    text(x + w + 8, y + 0x11, sealTeam(se.buds_class & 1), 0x00, 0x0f);
    fontSelect(FontId::Title);
    const u8 nameColour = nameGreyed(cur) ? 0x1a : 0x00;
    text(x, y + 0x1b, nameLine(se), nameColour, 0x0f);
    // C division, not clamped below 0 (index -1 reads the entry before the table).
    int idx = (campaign::rosterSkillRating(cur) - 50) / 10;
    if (idx > 3) idx = 3;
    const std::string str = exe().dgString(0x4024) + tableString(strtab::kStrRating, idx);  // "  STR: "
    text(x - textWidth(str) + 0x116, y + 0x1b, str, nameColour, 0x0f);
    fontSelect(FontId::Dialog);
    Gfx& gx = gfx();
    int colX = x, rowY = y + 0x25, k = 0;
    const u8* skills = se.skill;  // SE+0x74.., continues into size/strength/agility
    auto skillAt = [&se, skills](int i) -> u8 {
        if (i < 8) return skills[i];
        if (i == 8) return se.size;
        if (i == 9) return se.strength;
        return se.agility;
    };
    for (int j = 0; j < 9;) {
        text(colX + 0x2a, rowY, tableString(strtab::kSkillNames, k), 0x00, 0x0f);
        int v = skillAt(k);
        if (v < 50) v = 50;
        int len = 90 - v;
        if (len < 0) len = 0;
        len = 40 - len;
        if (len < 1) len = 1;
        gx.fillRect(colX + 1, rowY + 1, len, 4, u16(0xff00));
        gx.fillRect(colX, rowY, len, 4, u16(0xff0f));
        ++j;
        ++k;
        if (k == 2) ++k;  // no Mortar bar
        if (k == 8) ++k;  // no Size bar
        rowY += 10;
        if (j % 3 == 0) {
            colX += 0x63;
            rowY = y + 0x25;
        }
    }
}

// ui_draw_ribbons (365e:9998): bits 0..6 of each history entry, 4 per row.
void drawRibbons(SpriteSet& medals, int x, int y) {
    const CampaignHeader& h = campaign::header();
    const RosterEntry* cur = campaign::rosterFind(h.point_man);
    if (!cur || cur->missions == 0) return;
    int cx = x, cy = y, drawn = 0;
    for (int j = 0; j < cur->missions && drawn < 16 && j < 24; ++j) {
        unsigned bits = h.history[j].awards;
        for (int k = 0; k < 7 && drawn < 16; ++k, bits >>= 1) {
            if (!(bits & 1)) continue;
            const u8* img = medals.frame(0, k % 8);
            drawSprite(img, cx, cy);
            cx += SpriteSet::width(img) + 1;
            ++drawn;
            if (drawn % 4 == 0) {
                cy += SpriteSet::height(img) + 2;
                cx = x;
            }
        }
    }
}

// ui_draw_campaign_panel (365e:9C20).
void drawCampaignPanel(const RosterEntry& r, int portraitSlot, SpriteSet& medals, int x, int y) {
    const CampaignHeader& h = campaign::header();
    static const SeRecord kNoSe{};
    const SeRecord& se = r.se ? *r.se : kNoSe;
    uiDrawTextPanel("", x - 8, y, 0x124, 0x44);
    fontSelect(FontId::Title);
    text(x, y + 2, exe().dgString(0x4154), 0x0f, 0x00);  // "Campaign"
    fontSelect(FontId::Dialog);
    const std::string rank = rankLine(r.rank, se.rating, true);
    text(x, y + 0x11, rank, 0x00, 0x0f);
    const int w = textWidth(rank);
    // Second team for odd recruit years: (|slot| / 4) & 1, sign kept.
    const int a = portraitSlot < 0 ? -portraitSlot : portraitSlot;
    const int q = portraitSlot < 0 ? -(a >> 2) : (a >> 2);
    text(x + w + 8, y + 0x11, sealTeam(q & 1), 0x00, 0x0f);
    fontSelect(FontId::Title);
    const bool over = h.mission == 0xff;
    text(x, y + 0x1b, nameLine(se), (r.casualties & hit_bit::kKilled) || over ? 0x18 : 0x00, 0x0f);
    fontSelect(FontId::Dialog);
    if (!over) {
        const FlowEntry& f = campaign::flow(h.year, h.mission);
        const std::string date = num(h.mission + 1u) + exe().dgString(0x4179) + tableString(strtab::kMonths, f.month) +
                                 exe().dgString(0x417b) + num(1966u + h.year);
        text(x - textWidth(date) + 0x11b, y + 0x11, date, 0x00, 0x0f);
    }
    text(x, y + 0x25, exe().dgString(0x417e), 0x00, 0x0f);  // "Status:"
    const int status = (r.casualties & hit_bit::kKilled) ? 2 : over ? 1 : 0;
    text(x + 0x38, y + 0x25, tableString(strtab::kStatus, status), 0x00, 0x0f);
    text(x + 0x5e, y + 0x25, exe().dgString(0x4186), 0x00, 0x0f);  // "Tours:"
    unsigned tours = r.missions / 6u;
    if (tours > 4) tours = 4;
    text(x + 0x92, y + 0x25, num(tours), 0x00, 0x0f);
    text(x, y + 0x2f, exe().dgString(0x418d), 0x00, 0x0f);  // "Missions:"
    text(x + 0x38, y + 0x2f, num(r.missions), 0x00, 0x0f);
    if (r.missions != 0) {
        text(x + 0x5e, y + 0x2f, exe().dgString(0x4197), 0x00, 0x0f);  // "Victories:"
        text(x + 0x92, y + 0x2f, num(r.wins), 0x00, 0x0f);
    }
    text(x, y + 0x39, exe().dgString(0x41a2), 0x00, 0x0f);  // "Total Score:"
    text(x + 0x38, y + 0x39, num(u16(h.total_score)), 0x00, 0x0f);  // low word, unsigned
    if (r.missions != 0) {
        text(x + 0x5e, y + 0x39, exe().dgString(0x41af), 0x00, 0x0f);  // "Best Score:"
        text(x + 0x92, y + 0x39, num(u16(h.best_score)), 0x00, 0x0f);
    }
    drawRibbons(medals, x + 0xac, y + 0x1d);
}

} // namespace st::game::front
