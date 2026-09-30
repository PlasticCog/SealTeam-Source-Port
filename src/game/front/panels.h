// Information panels shared by the recruit and campaign screens
// (docs/re/seg_365e_b.md 6): Biography (365e:81F1), Rating (365e:889D),
// Campaign (365e:9C20) and the medal ribbons (365e:9998). Also the string
// tables of st.exe the front end uses.
#pragma once

#include "core/common.h"
#include "game/types.h"

#include <string>

namespace st::game::front {

class SpriteSet;

// Near-pointer string tables in DGROUP (read from st.exe).
namespace strtab {
constexpr u16 kMonths = 0x1b90;         // [13], 1..12
constexpr u16 kRatings = 0x1baa;        // [13]
constexpr u16 kRatingAbbrev = 0x1bc4;   // [13]
constexpr u16 kGroupTypes = 0x1c7c;     // [8]
constexpr u16 kTeamRoles = 0x1c8c;      // [4]
constexpr u16 kObjectiveVerbs = 0x1d5e; // [9]
constexpr u16 kObjectiveNames = 0x1d72; // [9]
constexpr u16 kBudsClasses = 0x24cc;    // [16]
constexpr u16 kSkillNames = 0x24ec;     // [12]
constexpr u16 kRanks = 0x2506;          // [12]
constexpr u16 kRankAbbrev = 0x251e;     // [12]
constexpr u16 kCamouflage = 0x2536;     // [6]
constexpr u16 kMedals = 0x2542;         // [16]
constexpr u16 kStatus = 0x2562;         // [3]
constexpr u16 kLocations = 0x2566;      // indexed by world number (camp worlds 28..31)
constexpr u16 kLoadClasses = 0x256a;    // [4]
constexpr u16 kEnemyStrength = 0x2572;  // [9]
constexpr u16 kBriefing = 0x2586;       // [11]
constexpr u16 kBullWrapUp = 0x25a8;     // [1]
constexpr u16 kTerrain = 0x25ac;        // [4]
constexpr u16 kWeather = 0x25b6;        // [4]
constexpr u16 kInsertionCraft = 0x25c0; // by (method & ~1) / 2
constexpr u16 kOtherUnits = 0x25cc;     // by (code & ~1) / 2
constexpr u16 kExtraction = 0x25d8;     // by extraction team type
constexpr u16 kStrRating = 0x25e4;      // [4] (index may be negative, as in the original)
} // namespace strtab

// Entry i of a near-pointer string table (i may be out of range: the
// original reads whatever pointer is there).
std::string tableString(u16 table, int i);

// Rank line of the panels: rating name (ranks < 5) + "  ", "Seaman" for
// ratingless ranks < 5, then the rank name for ranks >= 2. `seamanSpaces`:
// the Campaign panel appends "  " after "Seaman".
std::string rankLine(int rank, int rating, bool seamanSpaces);

// The name line: first name + "  " + ["'" nickname "'  "] + last name, the
// nickname from the campaign header.
std::string nameLine(const SeRecord& se);

// cur = g_cur_roster: its KIA bit greys the name line; its skills give the
// STR word on the Rating panel (365e:889D reads the roster, not the SE).
void drawBioPanel(const SeRecord& se, const RosterEntry& cur, SpriteSet& portraits, int portraitSlot, int x, int y);
void drawRatingPanel(const SeRecord& se, const RosterEntry& cur, int x, int y);
void drawCampaignPanel(const RosterEntry& r, int portraitSlot, SpriteSet& medals, int x, int y);
void drawRibbons(SpriteSet& medals, int x, int y);

} // namespace st::game::front
