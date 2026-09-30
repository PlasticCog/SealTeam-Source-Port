// Front-end screens of the campaign flow (segment 365e). Every screen returns
// 0 = quit the game (Alt-X), 1 = back / main menu (Esc), 2 = next step,
// like the original (docs/re/seg_365e_b.md 1).
#pragma once

namespace st::game::front {

int difficultyScreen();          // diff_screen (365e:E5BD), F10 from the menu and the briefing
int recruitScreen();             // recruit_screen (365e:8095), "UDT/SEAL Training School"
int campaignScreen();            // cmpscr_screen (365e:9AA6), "SEAL Campaign" dog tags
int intelScreen();               // intel_screen (365e:74E8), Intel Briefing / Practice Mission
int briefingScreen();            // brf_screen (365e:54BD), Mission Briefing + Patrol/Marching Order
int bullScreen();                // bull_screen (365e:ABB9), SEAL Bull Session
int insertionScreen();           // cut_insertion_screen (365e:D285)
int extractionScreen();          // cut_extraction_screen (365e:D659)
int debriefScreen(bool loadWorld);  // dbrf_screen (365e:B511) incl. mission recording and speeches
int speechScreen(int kind);      // speech_screen (365e:D94A), kind 0..5

void showPleaseWait();           // ui_show_please_wait (1000:2D83)

} // namespace st::game::front
